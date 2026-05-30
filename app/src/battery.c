/*
 * Copyright (c) 2024 WeeGee bv
 * PROJECT: POWER_PROFILER_INTEGRATION (2026-02-27)
 *
 * nRF52832 SAADC battery measurement for CG-320B (15 mAh).
 * ADC: 12-bit, 0.6V internal ref, devicetree gain (PCB uses ADC_GAIN_1).
 * For REV10 with 1/6 gain: add overlay with zephyr,gain = "ADC_GAIN_1_6".
 * Voltage divider scale 11: 4.35V peak -> ~0.395V at ADC input.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>

#include "battery.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(battery, CONFIG_APP_LOG_LEVEL);

#define VOLTAGE_DIVIDER_SCALE 11
/* Plausible LiPo voltage bounds (mV) for sanity-checking scaling. */
#define BATTERY_VOLTAGE_MIN_MV 2000
#define BATTERY_VOLTAGE_MAX_MV 5000

/* Plausibility window for the raw ADC-pin millivolt reading itself
 * (i.e. BEFORE applying VOLTAGE_DIVIDER_SCALE).
 *
 * - The nRF52 SAADC pin physically can only see 0..0.6 V (ADC_GAIN_1) or
 *   0..3.6 V (ADC_GAIN_1_6). Anything outside [50 mV, 4000 mV] at the pin
 *   means the sample is corrupt — typically:
 *     * raw=-1 sign-issue from a uint vs int16_t mistake
 *     * the divider FET hasn't actually powered up (raw≈0)
 *     * pin contention with PPG/ACC bring-up
 *
 * Bogus samples are dropped without updating battery_value or the smoothing
 * history, and never fall into the critical-low shutdown path.
 */
#define IMPLAUSIBLE_RAW_MV_MIN  50
#define IMPLAUSIBLE_RAW_MV_MAX  4000

/* CG-320B discharge curve: 4.35V = 100%, 3.0V = 0% */
#define CG320B_VMAX_MV 4350
#define CG320B_VMIN_MV  3000

/* Critical low threshold - protects cell from over-discharge */
#define CRITICAL_LOW_VOLTAGE_MV 2800

/* Rolling max of recent readings to ignore LED-pulse voltage sag.
 * Prevents "percent jumps" during high-current PPG LED pulses.
 */
#define VOLTAGE_SMOOTH_SAMPLES 3
static int32_t voltage_history[VOLTAGE_SMOOTH_SAMPLES];
static uint8_t voltage_history_idx;

static const struct adc_dt_spec adc_chan0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct gpio_dt_spec battery_enable = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), baten_gpios, 0);

/* Boost-regulator latch (BATEN / P0.10) early-init.
 *
 * On battery power, P0.10 is the enable latch for the on-board boost
 * regulator that produces the chip's 3V3 rail. The regulator's enable
 * input has an external pull-down: any time P0.10 is left as a Zephyr
 * input or floating, it is pulled LOW, the boost de-latches, the rail
 * decays through the output cap, and the chip browns out.
 *
 * Previously this pin was first asserted inside battery_init(), which is
 * called from safe_boot_work_handler() at t = CONFIG_ORALABLE_SAFE_BOOT_DELAY_S
 * (10 s). The 10-second window between every CPU reset and that first
 * GPIO drive was long enough for the boost to drop, leaving the device
 * dead and SWD unresponsive ("Could not connect to target") even though
 * the J-Link saw VTref = 3.3 V from the dev-board side. v1.0.22..v1.0.24
 * all hit this trap after any soft reset (MPU fault, watchdog, JLink r).
 *
 * Fix: drive BATEN HIGH from a SYS_INIT() that runs immediately after
 * the GPIO driver is ready, before main() and any user threads. That
 * collapses the float window from 10 s to a few hundred microseconds
 * (just MCUBoot + Zephyr arch init), which is well within the boost
 * regulator's hold time.
 */
static int oralable_baten_early_assert(void)
{
    if (!gpio_is_ready_dt(&battery_enable)) {
        return -ENODEV;
    }
    return gpio_pin_configure_dt(&battery_enable, GPIO_OUTPUT_HIGH);
}
SYS_INIT(oralable_baten_early_assert, POST_KERNEL,
         CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

/* MUST be int16_t. The nRF52 SAADC stores 12-bit samples as a SIGNED 16-bit
 * value. With single-ended input and the divider unpowered/transient, the
 * SAADC happily returns a small negative reading (e.g. raw=-1 = 0xFFFF) which,
 * if cast through uint16_t, becomes 65535 and scales to ~57.6 V at the pin —
 * the v1.0.17 "raw_mv=57596" symptom. Storing as int16_t makes the sign
 * extend correctly so a -1 raw becomes ~0 mV after adc_raw_to_millivolts_dt,
 * which our IMPLAUSIBLE_RAW_MV guard then rejects.
 */
static int16_t adc_buf;
static struct adc_sequence sequence = {
    .buffer = &adc_buf,
    .buffer_size = sizeof(adc_buf),
};

static int32_t battery_value;

static struct k_work_delayable battery_measurement_work;

static battery_data_ready_t app_data_ready;

static uint16_t measurement_interval_s = CONFIG_BATTERY_MEASUREMENT_INTERVAL;

static bool is_plausible_battery_mv(int32_t mv)
{
    return (mv >= BATTERY_VOLTAGE_MIN_MV && mv <= BATTERY_VOLTAGE_MAX_MV);
}

static void take_battery_measurement(struct k_work *work)
{
    int err;

    // Enable the battery voltage divider
    err = gpio_pin_set_dt(&battery_enable, 1);
    if (err)
    {
        LOG_ERR("Failed to enable battery voltage divider, err %d", err);
        // Try again later
        k_work_reschedule(&battery_measurement_work, K_SECONDS(1));
        return;
    }

    // Wait for the voltage to stabilize
    k_sleep(K_MSEC(1));

    // Read the battery voltage
    err = adc_read(adc_chan0.dev, &sequence);
    if (err)
    {
        LOG_ERR("ADC read failed, err %d", err);
    }
    else
    {
        int32_t raw_mv = (int32_t)adc_buf;

        err = adc_raw_to_millivolts_dt(&adc_chan0, &raw_mv);
        if (err)
        {
            LOG_ERR("ADC raw to millivolts failed, err %d", err);
        }
        else if (raw_mv < IMPLAUSIBLE_RAW_MV_MIN || raw_mv > IMPLAUSIBLE_RAW_MV_MAX)
        {
            /* Reject single bogus samples (typically caused by transient pin
             * contention while PPG/ACC are powering up, or a sign-mismatch
             * giving 65535 → ~57.6 V). DO NOT update battery_value, DO NOT
             * touch the smoothing history, and DO NOT fall into the
             * critical-low shutdown path on the basis of one fluke reading.
             */
            LOG_WRN("Battery: implausible raw_mv=%d (window %d..%dmV) — sample discarded",
                    raw_mv, IMPLAUSIBLE_RAW_MV_MIN, IMPLAUSIBLE_RAW_MV_MAX);
        }
        else
        {
            /* Scale to actual battery voltage */
            /* Default scaling: SAADC pin sees Vbat / 11. */
            const int32_t v_div = raw_mv * VOLTAGE_DIVIDER_SCALE;
            /* Fallbacks for misconfigured gain/overlay builds:
             * - Some boards/builds may end up with an effective 6x mismatch in adc_raw_to_millivolts_dt().
             * - Some hardware variants may not match the assumed divider scale.
             */
            const int32_t v_nodiv = raw_mv;
            const int32_t v_div_over6 = (raw_mv * VOLTAGE_DIVIDER_SCALE) / 6;
            const int32_t v_nodiv_over6 = raw_mv / 6;

            int32_t chosen = v_div;
            if (!is_plausible_battery_mv(chosen)) {
                if (is_plausible_battery_mv(v_div_over6)) {
                    chosen = v_div_over6;
                    LOG_WRN("Battery scaling fallback: using divider/6 (raw_mv=%d -> %dmV)", raw_mv, chosen);
                } else if (is_plausible_battery_mv(v_nodiv)) {
                    chosen = v_nodiv;
                    LOG_WRN("Battery scaling fallback: using no-divider (raw_mv=%d -> %dmV)", raw_mv, chosen);
                } else if (is_plausible_battery_mv(v_nodiv_over6)) {
                    chosen = v_nodiv_over6;
                    LOG_WRN("Battery scaling fallback: using no-divider/6 (raw_mv=%d -> %dmV)", raw_mv, chosen);
                } else {
                    /* Last resort: clamp into range to avoid emitting nonsense values. */
                    if (chosen < BATTERY_VOLTAGE_MIN_MV) {
                        chosen = BATTERY_VOLTAGE_MIN_MV;
                    } else if (chosen > BATTERY_VOLTAGE_MAX_MV) {
                        chosen = BATTERY_VOLTAGE_MAX_MV;
                    }
                    LOG_WRN("Battery scaling out of range: raw_mv=%d v_div=%d clamped_to=%dmV",
                            raw_mv, v_div, chosen);
                }
            }

            battery_value = chosen;

            /* Rolling max: ignore brief drops during LED pulses */
            voltage_history[voltage_history_idx] = battery_value;
            voltage_history_idx = (voltage_history_idx + 1) % VOLTAGE_SMOOTH_SAMPLES;
            int32_t max_v = voltage_history[0];
            for (int i = 1; i < VOLTAGE_SMOOTH_SAMPLES; i++) {
                if (voltage_history[i] > max_v) max_v = voltage_history[i];
            }
            uint8_t battery_pct = battery_voltage_to_percent(max_v);

#if defined(CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN)
            /* Only trigger the protective shutdown when the smoothed rolling
             * MAX is below the critical threshold. A single bad sample (e.g.
             * caused by ADC contention with PPG/ACC bring-up, or a momentary
             * dip during an LED pulse) must NEVER reboot the device — that
             * was the v1.0.16 reboot loop.
             *
             * We also require the smoothing window to be filled with real
             * readings (no zeroed history slots) so that an early bad sample
             * cannot trip the gate while voltage_history[1..2] are still 0.
             */
            bool history_filled = true;
            for (int i = 0; i < VOLTAGE_SMOOTH_SAMPLES; i++) {
                if (voltage_history[i] == 0) {
                    history_filled = false;
                    break;
                }
            }
            if (history_filled && max_v < CRITICAL_LOW_VOLTAGE_MV) {
                LOG_ERR("Battery critical: max(last %d)=%dmV < %dmV - shutting down to protect cell",
                        VOLTAGE_SMOOTH_SAMPLES, max_v, CRITICAL_LOW_VOLTAGE_MV);
                k_msleep(100); /* Allow log to flush */
                sys_reboot(SYS_REBOOT_COLD);
            }
#endif
            LOG_INF("Battery: %dmV -> %d%%", battery_value, battery_pct);
            tgm_service_send_battery_notify(battery_value);
            if (app_data_ready)
            {
                app_data_ready(battery_value);
            }
        }
    }

    // Disable the battery voltage divider
    err = gpio_pin_set_dt(&battery_enable, 0);
    if (err)
    {
        LOG_ERR("Failed to disable battery voltage divider, err %d", err);
    }

    // Schedule the next measurement (runtime-configurable)
    if (measurement_interval_s > 0)
    {
        k_work_reschedule(&battery_measurement_work, K_SECONDS(measurement_interval_s));
    }
    return;
}

int battery_init(battery_data_ready_t battery_data_ready_cb)
{
    if (!adc_is_ready_dt(&adc_chan0))
    {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    int err = adc_channel_setup_dt(&adc_chan0);
    if (err)
    {
        LOG_ERR("ADC channel setup failed, err %d", err);
        return err;
    }

    err = adc_sequence_init_dt(&adc_chan0, &sequence);
    if (err)
    {
        LOG_ERR("ADC sequence init failed, err %d", err);
        return err;
    }

    app_data_ready = battery_data_ready_cb;
    k_work_init_delayable(&battery_measurement_work, take_battery_measurement);

    /* Initialize voltage history for smoothing */
    for (int i = 0; i < VOLTAGE_SMOOTH_SAMPLES; i++) {
        voltage_history[i] = 0;
    }
    voltage_history_idx = 0;

    // Configure battery enable pin as output and set it high
    err = gpio_pin_configure_dt(&battery_enable, GPIO_OUTPUT);
    if (err)
    {
        LOG_ERR("GPIO pin configure failed, err %d", err);
        return err;
    }
    return err;
}

int battery_start_measurement()
{
    // Take an immediate measurement
    int err = k_work_reschedule(&battery_measurement_work, K_NO_WAIT);
    if (err < 0)
    {
        return err;
    }
    else
    {
        return 0;
    }
}

int32_t battery_get_last_measurement()
{
    return battery_value;
}

uint8_t battery_voltage_to_percent(int32_t voltage_mv)
{
    /* CG-320B linear fit: 4.35V = 100%, 3.0V = 0% */
    if (voltage_mv >= CG320B_VMAX_MV) return 100;
    if (voltage_mv <= CG320B_VMIN_MV) return 0;
    /* percent = (V - Vmin) * 100 / (Vmax - Vmin) */
    int32_t range = CG320B_VMAX_MV - CG320B_VMIN_MV;
    int32_t pct = (voltage_mv - CG320B_VMIN_MV) * 100 / range;
    return (uint8_t)(pct > 100 ? 100 : pct);
}

uint8_t battery_get_percentage(void)
{
    return battery_voltage_to_percent(battery_value);
}

void battery_get_usage_telemetry(struct battery_usage_telemetry_t *out)
{
    if (!out) {
        return;
    }
    out->voltage_mv = battery_value;
    out->percent = battery_voltage_to_percent(battery_value);
    /* app/ target currently has no power_profiler module wired in. */
    out->mah_consumed_scaled = 0;
    out->remaining_minutes = 0;
}

void battery_set_measurement_interval_seconds(uint16_t seconds)
{
    measurement_interval_s = seconds;
    /* Defensive: do NOT reschedule if the work has not yet been initialised
     * (battery_init() not yet called from safe_boot_work_handler). Scheduling
     * an uninitialised k_work_delayable corrupts the kernel timeout list when
     * k_work_init_delayable() later zeroes the struct. handler==NULL is the
     * canonical "not init'd" sentinel for k_work.
     */
    if (measurement_interval_s > 0 && battery_measurement_work.work.handler != NULL)
    {
        k_work_reschedule(&battery_measurement_work, K_SECONDS(measurement_interval_s));
    }
}

uint16_t battery_get_measurement_interval_seconds(void)
{
    return measurement_interval_s;
}
