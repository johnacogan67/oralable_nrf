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
#include "power_profiler.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(battery, CONFIG_APP_LOG_LEVEL);

#define VOLTAGE_DIVIDER_SCALE 11

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

uint16_t adc_buf;
static struct adc_sequence sequence = {
    .buffer = &adc_buf,
    .buffer_size = sizeof(adc_buf),
};

static int32_t battery_value;

static struct k_work_delayable battery_measurement_work;

static battery_data_ready_t app_data_ready;

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
        else
        {
            /* Scale to actual battery voltage */
            battery_value = raw_mv * VOLTAGE_DIVIDER_SCALE;

            /* Rolling max: ignore brief drops during LED pulses */
            voltage_history[voltage_history_idx] = battery_value;
            voltage_history_idx = (voltage_history_idx + 1) % VOLTAGE_SMOOTH_SAMPLES;
            int32_t max_v = voltage_history[0];
            for (int i = 1; i < VOLTAGE_SMOOTH_SAMPLES; i++) {
                if (voltage_history[i] > max_v) max_v = voltage_history[i];
            }
            uint8_t battery_pct = battery_voltage_to_percent(max_v);

#if defined(CONFIG_BATTERY_CRITICAL_LOW_SHUTDOWN)
            if (battery_value < CRITICAL_LOW_VOLTAGE_MV) {
                LOG_ERR("Battery critical: %dmV < %dmV - shutting down to protect cell",
                        battery_value, CRITICAL_LOW_VOLTAGE_MV);
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

    // Schedule the next measurement
    k_work_reschedule(&battery_measurement_work, K_SECONDS(CONFIG_BATTERY_MEASUREMENT_INTERVAL));
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
    /* Link to power_profiler: mAh consumed (scaled x100) and remaining minutes */
    float total_mah = power_profiler_get_total_mah();
    out->mah_consumed_scaled = (uint32_t)(total_mah * 100.0f);
    out->remaining_minutes = power_profiler_get_remaining_minutes();
}
