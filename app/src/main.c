/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>

#include <app_version.h>

#include "ble.h"
#include "ppg.h"
#include "acc.h"
#include "battery.h"
#include "battery_led_indicator.h"
#include "charge_detector.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define GPIO_NODE DT_NODELABEL(gpio0)
#define SENS_ENABLE_PIN 8

/** Dim green-only PPG during off-body connect probe (see CONFIG_TGM_CONNECT_PROBE_DURATION_S). */
#define CONNECT_PROBE_GREEN_PA 5

/** Remapped soft floor (%): stop PPG/ACC; keep MCU, BLE, and charge. */
#define LOW_BATTERY_BLE_RESCUE_PCT 5

/** Soft floor (mV) = remapped 0% / CG320B_VMIN — not chemical empty (~3.0 V). */
#define LOW_BATTERY_BLE_RESCUE_MV 3610

/** Below this, Protocol A uses dim red/IR so a 6 min session can finish. */
#define PROTOCOL_A_DIM_LED_MV 3900
#define WORN_LED_RED_FULL 32
#define WORN_LED_IR_FULL 128
#define WORN_LED_RED_DIM 16
#define WORN_LED_IR_DIM 64

enum device_state_t
{
	DEVICE_STATE_NOT_WORN_OFF_DOCK = 0,
	DEVICE_STATE_NOT_WORN_ON_DOCK = 1,
	DEVICE_STATE_WORN = 2,
	DEVICE_STATE_INIT,
};

static volatile bool on_dock = false;
static volatile bool worn = false;

static const struct device *gpio = DEVICE_DT_GET(GPIO_NODE);
static const struct device *temp_sensor = DEVICE_DT_GET(DT_NODELABEL(temp));
static const struct gpio_dt_spec chrsts_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), chrsts_gpios, 0);

static struct gpio_callback chrsts_cb;

static struct k_work_delayable temperature_work;
#if !IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
static struct k_work_delayable chrsts_debounce_work;
#endif
static uint16_t temp_measurement_interval_s = CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL;
static uint16_t off_dock_hot_sustain_s = 0;

static enum device_state_t device_state = DEVICE_STATE_INIT;
static bool floor_sensors_off;

static bool dock_raw;
#if !IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
static uint8_t dock_raw_stable_s;
static uint8_t dock_mismatch_votes;
#endif

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
/** LTC4124 STAT: blink = charging, steady assert = taper, steady inactive = undock. */
enum chrsts_stat_phase {
	STAT_PHASE_OFF = 0,
	STAT_PHASE_CHARGING,
	STAT_PHASE_TAPER,
};

static atomic_t chrsts_edge_count;
static enum chrsts_stat_phase stat_phase = STAT_PHASE_OFF;
static uint8_t stat_assert_hold_s;
static uint8_t stat_inactive_hold_s;
#endif

/** Below this voltage, never drive solid (full) battery LEDs — weak-cell ADC reads high. */
#define BATTERY_LED_SOLID_MIN_MV 3900

static bool chrsts_gpio_active(void);
static bool chrsts_read_on_dock_majority(void);
#if !IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
static void feed_dock_gpio(bool gpio_on_dock);
#endif
static void publish_status(void);
static int update_state(bool on_dock_new_state, bool worn_new_state);
static void chrsts_log_snapshot(const char *tag, bool majority_on_dock);
static bool user_mode_overrides_auto(void);
static void user_mode_apply(void);
static void user_mode_changed_cb(void);
static void ppg_apply_off_body_leds(void);
static void apply_worn_sensing_leds(void);
static void sync_off_body_sensors(void);
static bool status_charge_active_effective(void);
#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
static void chrsts_process_stat_activity(void);
#endif

static void sync_off_body_sensors(void)
{
	if (ble_is_connected() || tgm_service_connect_probe_active())
	{
		return;
	}

	(void)acc_stop();
}

static bool status_charge_active_effective(void)
{
#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	const bool stat_charging = (stat_phase == STAT_PHASE_CHARGING);

	if (user_mode_overrides_auto()) {
		/* Manual mode 1: mV trend and/or STAT blink. */
		return on_dock && (charge_detector_is_active() || stat_charging);
	}

	return on_dock && stat_charging;
#else
	return on_dock && charge_detector_is_active();
#endif
}

static bool stat_on_pad_now(void)
{
#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	return (stat_phase == STAT_PHASE_CHARGING ||
		stat_phase == STAT_PHASE_TAPER);
#else
	return on_dock;
#endif
}

static void apply_battery_leds(void)
{
	int32_t mv;
	uint8_t battery_pct;

	/* Live temple stream owns the emitters. A zombie "connected" link
	 * with no successful notifies must not block pad green / off-pad dark.
	 */
	if (ble_is_connected() && tgm_service_ppg_stream_live())
	{
		return;
	}

	/* Kill leftover worn red/IR before status green (or dark). */
	(void)ppg_set_led_pa(PPG_LED_RED, 0);
	(void)ppg_set_led_pa(PPG_LED_IR, 0);

	mv = battery_get_last_measurement();
	battery_pct = battery_voltage_to_percent(mv);

	if (mv < BATTERY_LED_SOLID_MIN_MV &&
	    battery_pct > BATTERY_LED_FULL_THRESHOLD_PCT)
	{
		battery_pct = BATTERY_LED_FULL_THRESHOLD_PCT;
	}

	/* Disconnected: status LEDs even if worn is still latched (no PPG).
	 * STAT charging/taper counts as on-pad so leftover Dual A mode 3
	 * cannot hide the charge light.
	 */
	bool led_on_pad = on_dock;

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	if (stat_phase == STAT_PHASE_CHARGING || stat_phase == STAT_PHASE_TAPER) {
		led_on_pad = true;
	}
#endif

	battery_led_indicator_apply(led_on_pad, false, battery_pct, mv,
				    status_charge_active_effective());
}

static bool user_mode_overrides_auto(void)
{
	return tgm_service_get_user_device_mode() != 0U;
}

static void user_mode_apply(void)
{
	const uint8_t mode = tgm_service_get_user_device_mode();
	bool target_on_dock;
	bool target_worn;

	switch (mode) {
	case 1U:
		target_on_dock = true;
		target_worn = false;
		break;
	case 2U:
		target_on_dock = false;
		target_worn = false;
		break;
	case 3U:
		target_on_dock = false;
		target_worn = true;
		if (battery_below_soft_floor()) {
			const int32_t mv = battery_get_last_measurement();

			LOG_WRN("Mode 3 refused: soft floor %dmV gauge=%u%%",
				mv, battery_voltage_to_percent(mv));
			tgm_service_fw_log_printf(
				"fw: mode3 refused soft_floor mv=%d", mv);
			tgm_service_set_user_device_mode(0U);
			target_worn = false;
		}
		break;
	default:
		return;
	}

	LOG_INF("User device mode %u -> on_dock=%d worn=%d",
		tgm_service_get_user_device_mode(), target_on_dock, target_worn);
	update_state(target_on_dock, target_worn);
}

static void user_mode_changed_cb(void)
{
	user_mode_apply();
}

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
/**
 * LTC4124 STAT: blink (edges) = charging; steady assert = taper; steady inactive = undock.
 * Replaces legacy "stable GPIO level" debounce that never latched during blink.
 */
static void chrsts_process_stat_activity(void)
{
	const unsigned int edges = (unsigned int)atomic_set(&chrsts_edge_count, 0);
	const bool asserted = chrsts_gpio_active();
	const enum chrsts_stat_phase prev = stat_phase;
	const bool charge_was = (prev == STAT_PHASE_CHARGING);
	bool want_on_dock = on_dock;
	int32_t mv = battery_get_last_measurement();

	if (user_mode_overrides_auto()) {
		const bool charge_det_was = charge_detector_is_active();
		const bool charge_det_now = charge_detector_update(mv, on_dock);

		/* Still track STAT phase so mode 1 can OR blink with mV rise. */
		if (edges >= (unsigned int)CONFIG_CHRSTS_BLINK_EDGE_MIN ||
		    (edges > 0U && stat_phase == STAT_PHASE_CHARGING)) {
			stat_phase = STAT_PHASE_CHARGING;
			stat_assert_hold_s = 0;
			stat_inactive_hold_s = 0;
		} else if (asserted) {
			stat_inactive_hold_s = 0;
			if (stat_assert_hold_s < UINT8_MAX) {
				stat_assert_hold_s++;
			}
			if (stat_assert_hold_s >= (uint8_t)CONFIG_CHRSTS_TAPER_HOLD_S) {
				stat_phase = STAT_PHASE_TAPER;
			}
		} else {
			stat_assert_hold_s = 0;
			if (stat_inactive_hold_s < UINT8_MAX) {
				stat_inactive_hold_s++;
			}
			if (stat_inactive_hold_s >= (uint8_t)CONFIG_CHRSTS_UNDOCK_HOLD_S) {
				stat_phase = STAT_PHASE_OFF;
			}
		}

		if (charge_det_now != charge_det_was ||
		    (stat_phase == STAT_PHASE_CHARGING) != charge_was) {
			LOG_INF("charge_active user_mode det=%d stat=%d mv=%d on_dock=%d",
				charge_det_now, stat_phase == STAT_PHASE_CHARGING, mv,
				on_dock);
			tgm_service_fw_log_printf(
				"fw: charge_active um det=%d st=%d mv=%d on=%d",
				charge_det_now, stat_phase == STAT_PHASE_CHARGING, mv,
				on_dock);
			publish_status();
		}

		/* Pad wins over leftover Dual A / nRF Connect mode 3 (worn)
		 * when CCC is off, or when CCC is leftover on a zombie link
		 * (no successful notify for 4 s). A live Protocol A / Dual A
		 * session keeps mode 3: PPG notifies succeed, so idle is false
		 * even if STAT still looks on-pad. Mode 1 stays as written
		 * when STAT is off.
		 */
		const bool stat_on_pad = (stat_phase == STAT_PHASE_CHARGING ||
					  stat_phase == STAT_PHASE_TAPER);
		const bool leftover_stream =
			!tgm_service_sensor_notify_enabled() ||
			tgm_service_link_notify_idle();

		if (stat_on_pad && leftover_stream &&
		    !tgm_service_mode3_pad_grace_active()) {
			if (tgm_service_get_user_device_mode() == 3U) {
				tgm_service_set_user_device_mode(0U);
			}
			if (!on_dock || worn) {
				LOG_INF("STAT on-pad wins over user mode — on_dock=1 worn=0");
				update_state(true, false);
			}
			if (ble_is_connected() && tgm_service_link_notify_idle()) {
				ble_force_recover("STAT pad wins idle link");
			}
		}
		return;
	}

	(void)charge_detector_update(mv, on_dock);

	if (edges >= (unsigned int)CONFIG_CHRSTS_BLINK_EDGE_MIN ||
	    (edges > 0U && stat_phase == STAT_PHASE_CHARGING)) {
		stat_phase = STAT_PHASE_CHARGING;
		stat_assert_hold_s = 0;
		stat_inactive_hold_s = 0;
		want_on_dock = true;
		dock_raw = true;
	} else if (asserted) {
		stat_inactive_hold_s = 0;
		if (stat_assert_hold_s < UINT8_MAX) {
			stat_assert_hold_s++;
		}
		if (stat_phase == STAT_PHASE_CHARGING) {
			want_on_dock = true;
			dock_raw = true;
			if (stat_assert_hold_s >= (uint8_t)CONFIG_CHRSTS_TAPER_HOLD_S) {
				stat_phase = STAT_PHASE_TAPER;
			}
		} else if (stat_assert_hold_s >= (uint8_t)CONFIG_CHRSTS_TAPER_HOLD_S) {
			stat_phase = STAT_PHASE_TAPER;
			want_on_dock = true;
			dock_raw = true;
		}
	} else {
		stat_assert_hold_s = 0;
		if (stat_inactive_hold_s < UINT8_MAX) {
			stat_inactive_hold_s++;
		}
		if (stat_inactive_hold_s >= (uint8_t)CONFIG_CHRSTS_UNDOCK_HOLD_S) {
			stat_phase = STAT_PHASE_OFF;
			want_on_dock = false;
			dock_raw = false;
		}
	}

	if (prev != stat_phase) {
		LOG_INF("chrsts phase %d -> %d edges=%u assert=%d on_dock=%d mv=%d",
			(int)prev, (int)stat_phase, edges, asserted, want_on_dock, mv);
		tgm_service_fw_log_printf(
			"fw: chrsts phase %d->%d e=%u a=%d on=%d mv=%d",
			(int)prev, (int)stat_phase, edges, asserted, want_on_dock, mv);
	}

	if (want_on_dock != on_dock || (want_on_dock && worn)) {
		if (want_on_dock && worn) {
			LOG_WRN("STAT on-pad with worn=1 — clearing worn");
		}
		chrsts_log_snapshot(want_on_dock ? "stat_on" : "stat_off", want_on_dock);
		update_state(want_on_dock, want_on_dock ? false : worn);
	} else if (prev != stat_phase) {
		/* Same dock/worn; charge_active (blink↔taper) changed. */
		publish_status();
		apply_battery_leds();
	}
}
#else /* !CONFIG_CHRSTS_STAT_ACTIVITY — legacy stable-level debounce */
static void feed_dock_gpio(bool gpio_on_dock)
{
	int32_t mv = battery_get_last_measurement();

	if (user_mode_overrides_auto()) {
		const bool charge_was = charge_detector_is_active();
		const bool charge_now = charge_detector_update(mv, on_dock);

		if (charge_now != charge_was) {
			LOG_INF("charge_active %d -> %d (mv=%d on_dock=%d user_mode)",
				charge_was, charge_now, mv, on_dock);
			tgm_service_fw_log_printf("fw: charge_active %d mv=%d on=%d",
						  charge_now, mv, on_dock);
			publish_status();
		}
		return;
	}

	const bool charge_was = charge_detector_is_active();
	const bool charge_now = charge_detector_update(mv, gpio_on_dock);

	if (charge_now != charge_was)
	{
		LOG_INF("charge_active %d -> %d (mv=%d on_dock=%d)",
			charge_was, charge_now, mv, gpio_on_dock);
		tgm_service_fw_log_printf("fw: charge_active %d mv=%d on=%d",
					  charge_now, mv, gpio_on_dock);
	}

	if (gpio_on_dock != dock_raw)
	{
		if (dock_mismatch_votes == 0)
		{
			chrsts_log_snapshot(gpio_on_dock ? "cand_on" : "cand_off", gpio_on_dock);
		}

		if (dock_mismatch_votes < UINT8_MAX)
		{
			dock_mismatch_votes++;
		}

		if (CONFIG_DOCK_GPIO_MISMATCH_VOTES > 0 &&
		    dock_mismatch_votes < CONFIG_DOCK_GPIO_MISMATCH_VOTES)
		{
			LOG_DBG("chrsts debounce: vote %u/%u (cand=%d raw=%d)",
				dock_mismatch_votes, CONFIG_DOCK_GPIO_MISMATCH_VOTES,
				gpio_on_dock, dock_raw);
			return;
		}

		dock_raw = gpio_on_dock;
		dock_mismatch_votes = 0;
		dock_raw_stable_s = 0;
		chrsts_log_snapshot(dock_raw ? "raw_on" : "raw_off", gpio_on_dock);
		return;
	}

	dock_mismatch_votes = 0;

	if (dock_raw_stable_s < UINT8_MAX)
	{
		dock_raw_stable_s++;
	}

	if (CONFIG_DOCK_STATE_STABLE_S > 0 &&
	    dock_raw_stable_s < CONFIG_DOCK_STATE_STABLE_S)
	{
		if (dock_raw_stable_s == 1U || dock_raw_stable_s == CONFIG_DOCK_STATE_STABLE_S)
		{
			LOG_DBG("chrsts stable countdown: %u/%u raw=%d on=%d",
				dock_raw_stable_s, CONFIG_DOCK_STATE_STABLE_S,
				dock_raw, on_dock);
		}
		return;
	}

	if (dock_raw == on_dock)
	{
		return;
	}

	if (dock_raw && worn)
	{
		LOG_WRN("Dock stable on pad with worn=1 — clearing worn");
		update_state(dock_raw, false);
		return;
	}

	chrsts_log_snapshot(dock_raw ? "stable_on" : "stable_off", gpio_on_dock);
	update_state(dock_raw, worn);
}
#endif /* CONFIG_CHRSTS_STAT_ACTIVITY */

static void dock_poll_and_refresh(void)
{
	static uint8_t heartbeat_polls;
	bool majority_on_dock = chrsts_read_on_dock_majority();

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	chrsts_process_stat_activity();
#else
	feed_dock_gpio(majority_on_dock);
#endif

	if (++heartbeat_polls >= 5U)
	{
		heartbeat_polls = 0U;
		chrsts_log_snapshot("hb", majority_on_dock);
	}

	if ((!worn || on_dock) && !tgm_service_connect_probe_active())
	{
		apply_battery_leds();
	}
}

static void publish_status(void)
{
	uint8_t battery_pct = battery_voltage_to_percent(battery_get_last_measurement());
	bool status_on_dock = on_dock;
	bool status_worn = on_dock ? false : worn;
	bool status_charge_active = status_charge_active_effective();
	uint8_t status_state;

	if (status_on_dock)
	{
		status_worn = false;
		status_state = DEVICE_STATE_NOT_WORN_ON_DOCK;
	}
	else if (status_worn)
	{
		status_state = DEVICE_STATE_WORN;
	}
	else
	{
		status_state = DEVICE_STATE_NOT_WORN_OFF_DOCK;
	}

	tgm_service_send_status_notify(status_on_dock, status_worn, status_state,
				       battery_pct, status_charge_active);
}

static int16_t read_die_temp_centi(void)
{
	int err;
	struct sensor_value temp_value;

	err = sensor_sample_fetch(temp_sensor);
	if (err)
	{
		return INT16_MIN;
	}

	err = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (err)
	{
		return INT16_MIN;
	}

	return temp_value.val1 * 100 + temp_value.val2 / 10000;
}

static int chrsts_pin_physical(void)
{
	return gpio_pin_get(gpio, chrsts_gpio.pin);
}

static void chrsts_log_snapshot(const char *tag, bool majority_on_dock)
{
	const int phy = chrsts_pin_physical();
	const int dt = gpio_pin_get_dt(&chrsts_gpio);
	const bool active = chrsts_gpio_active();
	const int32_t mv = battery_get_last_measurement();

	tgm_service_refresh_chrsts_bench();

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	LOG_INF("chrsts %s: phy=%d dt=%d active=%d maj=%d invert=%d dock_raw=%d on_dock=%d "
		"phase=%d edges=%ld assert_h=%u inact_h=%u charge=%d mv=%d",
		tag, phy, dt, active, majority_on_dock,
		IS_ENABLED(CONFIG_CHRSTS_INVERT) ? 1 : 0,
		dock_raw, on_dock, (int)stat_phase, (long)atomic_get(&chrsts_edge_count),
		stat_assert_hold_s, stat_inactive_hold_s,
		status_charge_active_effective() ? 1 : 0, mv);

	tgm_service_fw_log_printf(
		"fw: chrsts %s phy=%d dt=%d act=%d maj=%d inv=%d raw=%d on=%d ph=%d e=%ld ah=%u ih=%u ch=%d mv=%d",
		tag, phy, dt, active, majority_on_dock,
		IS_ENABLED(CONFIG_CHRSTS_INVERT) ? 1 : 0,
		dock_raw, on_dock, (int)stat_phase, (long)atomic_get(&chrsts_edge_count),
		stat_assert_hold_s, stat_inactive_hold_s,
		status_charge_active_effective() ? 1 : 0, mv);
#else
	LOG_INF("chrsts %s: phy=%d dt=%d active=%d maj=%d invert=%d dock_raw=%d on_dock=%d "
		"stable=%u/%u mismatch=%u mv=%d",
		tag, phy, dt, active, majority_on_dock,
		IS_ENABLED(CONFIG_CHRSTS_INVERT) ? 1 : 0,
		dock_raw, on_dock, dock_raw_stable_s, CONFIG_DOCK_STATE_STABLE_S,
		dock_mismatch_votes, mv);

	tgm_service_fw_log_printf(
		"fw: chrsts %s phy=%d dt=%d act=%d maj=%d inv=%d raw=%d on=%d stab=%u/%u mis=%u mv=%d",
		tag, phy, dt, active, majority_on_dock,
		IS_ENABLED(CONFIG_CHRSTS_INVERT) ? 1 : 0,
		dock_raw, on_dock, dock_raw_stable_s, CONFIG_DOCK_STATE_STABLE_S,
		dock_mismatch_votes, mv);
#endif
}

static bool chrsts_gpio_active(void)
{
	int level = gpio_pin_get_dt(&chrsts_gpio);

#if IS_ENABLED(CONFIG_CHRSTS_INVERT)
	return level == 0;
#else
	return level != 0;
#endif
}

static bool chrsts_read_on_dock_majority(void)
{
	int votes = 0;
	int samples = CONFIG_CHRSTS_SAMPLE_COUNT;
	int majority = (samples / 2) + 1;

	if (samples <= 0)
	{
		return chrsts_gpio_active();
	}

	for (int i = 0; i < samples; i++)
	{
		if (chrsts_gpio_active())
		{
			votes++;
		}

		if (i + 1 < samples)
		{
			k_sleep(K_MSEC(CONFIG_CHRSTS_SAMPLE_INTERVAL_MS));
		}
	}

	return votes >= majority;
}

static void reset_off_dock_hot_sustain(void)
{
	off_dock_hot_sustain_s = 0;
}

static int update_state(bool on_dock_new_state, bool worn_new_state)
{
	if (on_dock_new_state)
	{
		worn_new_state = false;
		reset_off_dock_hot_sustain();
	}

	if (on_dock_new_state == on_dock && worn_new_state == worn)
	{
		return 0;
	}

	if (on_dock_new_state != on_dock)
	{
		on_dock = on_dock_new_state;

		if (on_dock)
		{
			charge_detector_reset();
		}
		else
		{
			reset_off_dock_hot_sustain();
			charge_detector_reset();
		}

		(void)charge_detector_update(battery_get_last_measurement(), on_dock);
	}

	if (worn_new_state != worn)
	{
		worn = worn_new_state;

		if (worn)
		{
			device_state = DEVICE_STATE_WORN;
			/* Sensing LEDs wait for PPG CCC (ppg_stream_start_cb). */
		}
		else if (!ble_is_connected() && !tgm_service_connect_probe_active())
		{
			int err;

			err = ppg_set_led_pa(PPG_LED_RED, 0);
			if (err)
			{
				LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_RED);
				return err;
			}

			err = ppg_set_led_pa(PPG_LED_IR, 0);
			if (err)
			{
				LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_IR);
				return err;
			}
		}
	}

	if (on_dock)
	{
		if (worn)
		{
			LOG_WRN("On-dock invariant: forcing worn=0");
			worn = false;
			ppg_apply_off_body_leds();
		}

		device_state = DEVICE_STATE_NOT_WORN_ON_DOCK;
	}
	else if (worn)
	{
		device_state = DEVICE_STATE_WORN;
	}
	else if (device_state == DEVICE_STATE_INIT)
	{
		device_state = DEVICE_STATE_NOT_WORN_OFF_DOCK;
	}
	else if (!worn)
	{
		device_state = DEVICE_STATE_NOT_WORN_OFF_DOCK;
	}

	LOG_INF("Device state %d (on_dock=%d worn=%d charge_active=%d)",
		device_state, on_dock, worn, charge_detector_is_active());

	tgm_service_set_device_worn(worn);
	tgm_service_set_on_dock(on_dock);
	sync_off_body_sensors();

	if (ble_is_connected())
	{
		battery_led_indicator_suspend();
	}
	else
	{
		apply_battery_leds();
	}

	publish_status();

	return 0;
}

static void battery_measurement_ready(int32_t battery_mv)
{
	uint8_t battery_pct = battery_voltage_to_percent(battery_mv);
	const bool low_for_ble = (battery_pct < LOW_BATTERY_BLE_RESCUE_PCT) ||
				 (battery_mv < LOW_BATTERY_BLE_RESCUE_MV);

	(void)charge_detector_update(battery_mv, on_dock);

	if (low_for_ble)
	{
		if (!floor_sensors_off) {
			LOG_WRN("Soft floor (remapped 0%%): %dmV gauge=%u%% chem=%u%% — PPG/ACC off, MCU stays up",
				battery_mv, battery_pct,
				battery_voltage_to_chemistry_percent(battery_mv));
			tgm_service_fw_log_printf("fw: floor sensors off mv=%d pct=%u",
						  battery_mv, battery_pct);
			tgm_service_clear_optical_leds();
			floor_sensors_off = true;
		}
		if (tgm_service_get_user_device_mode() == 3U &&
		    !tgm_service_mode3_soft_floor_hold_active()) {
			tgm_service_set_user_device_mode(0U);
		}
		if (worn && !tgm_service_mode3_soft_floor_hold_active())
		{
			update_state(on_dock, false);
		}
		if (!ble_is_connected() && !tgm_service_connect_probe_active())
		{
			apply_battery_leds();
		}
		return;
	}

	if (floor_sensors_off) {
		floor_sensors_off = false;
		tgm_service_fw_log_printf("fw: floor sensors resume mv=%d pct=%u",
					  battery_mv, battery_pct);
		tgm_service_try_start_ppg_acc();
	}
	publish_status();
}

static void ppg_apply_off_body_leds(void)
{
	(void)ppg_set_led_pa(PPG_LED_RED, 0);
	(void)ppg_set_led_pa(PPG_LED_IR, 0);
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0);
}

static void connect_probe_begin_cb(void)
{
	if (worn)
	{
		return;
	}

	LOG_INF("Connect probe: dim green PPG LED (PA=%d)", CONNECT_PROBE_GREEN_PA);
	battery_led_indicator_suspend();
	sync_off_body_sensors();
	(void)ppg_ensure_awake();
	(void)ppg_set_led_pa(PPG_LED_RED, 0);
	(void)ppg_set_led_pa(PPG_LED_IR, 0);
	(void)ppg_set_led_pa(PPG_LED_GREEN, CONNECT_PROBE_GREEN_PA);
}

static void connect_probe_end_cb(void)
{
	LOG_INF("Connect probe: PPG LEDs off");
	sync_off_body_sensors();

	if (!worn)
	{
		apply_battery_leds();
	}
}

static void diagnostic_snapshot_cb(void)
{
	publish_status();
}

static void ble_link_connected_cb(void)
{
	int16_t centitemp = read_die_temp_centi();

	LOG_INF("BLE link up: on_dock=%d worn=%d state=%d bat=%d%% charge=%d die=%d.%02d C probe=%ds",
		on_dock, worn, device_state,
		battery_voltage_to_percent(battery_get_last_measurement()),
		status_charge_active_effective() ? 1 : 0,
		centitemp >= 0 ? centitemp / 100 : 0,
		centitemp >= 0 ? centitemp % 100 : 0,
		CONFIG_TGM_CONNECT_PROBE_DURATION_S);

	battery_led_indicator_suspend();
	ppg_apply_off_body_leds();

	publish_status();
	chrsts_log_snapshot("link", chrsts_read_on_dock_majority());
}

static void apply_worn_sensing_leds(void)
{
	const int32_t cached_mv = battery_get_last_measurement();
	bool dim = (cached_mv > 0 && cached_mv < PROTOCOL_A_DIM_LED_MV);
	uint8_t red_pa = dim ? WORN_LED_RED_DIM : WORN_LED_RED_FULL;
	uint8_t ir_pa = dim ? WORN_LED_IR_DIM : WORN_LED_IR_FULL;

	(void)ppg_ensure_awake();
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0);
	(void)ppg_set_led_pa(PPG_LED_RED, red_pa);
	(void)ppg_set_led_pa(PPG_LED_IR, ir_pa);

	const int32_t under_mv = battery_read_now_mv();

	if (under_mv > 0)
	{
		tgm_service_fw_log_printf("fw: bat under_load mv=%d cached=%d dim=%u",
					  under_mv, cached_mv, dim ? 1u : 0u);
		if (under_mv < PROTOCOL_A_DIM_LED_MV && !dim)
		{
			dim = true;
			red_pa = WORN_LED_RED_DIM;
			ir_pa = WORN_LED_IR_DIM;
			(void)ppg_set_led_pa(PPG_LED_RED, red_pa);
			(void)ppg_set_led_pa(PPG_LED_IR, ir_pa);
			tgm_service_fw_log_printf("fw: bat sag dim leds ir=%u red=%u",
						  ir_pa, red_pa);
		}
	}
	else
	{
		tgm_service_fw_log_printf("fw: bat under_load mv=0 cached=%d dim=%u",
					  cached_mv, dim ? 1u : 0u);
	}

	LOG_INF("Worn sensing LEDs red=%u ir=%u cached=%dmV under=%dmV",
		red_pa, ir_pa, cached_mv, under_mv);
}

static void ble_link_disconnected_cb(void)
{
	reset_off_dock_hot_sustain();
	/* Drop leftover worn PPG red immediately; pad then goes green (never red). */
	ppg_apply_off_body_leds();
	apply_battery_leds();

	/* Advertising restarts from ble.c .recycled (Nordic NCS ≥ 3.0 pattern). */
	LOG_INF("BLE link down: on_dock=%d worn=%d state=%d",
		on_dock, worn, device_state);
}

#if !IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
static void chrsts_debounce_handler(struct k_work *work)
{
	ARG_UNUSED(work);
	bool majority_on_dock = chrsts_read_on_dock_majority();

	(void)battery_start_measurement();
	feed_dock_gpio(majority_on_dock);
	chrsts_log_snapshot("edge", majority_on_dock);
}
#endif

static void temperature_work_handler(struct k_work *work)
{
	int err;
	struct sensor_value temp_value;
	int16_t centitemp;

	err = sensor_sample_fetch(temp_sensor);
	if (err)
	{
		LOG_ERR("Failed to fetch temperature sample with error %d", err);
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
		return;
	}

	err = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (err)
	{
		LOG_ERR("Failed to get temperature value with error %d", err);
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
		return;
	}

	centitemp = temp_value.val1 * 100 + temp_value.val2 / 10000;
	LOG_INF("Temperature: %d.%02d C", centitemp / 100, centitemp % 100);

	if (ble_is_connected() || worn || tgm_service_connect_probe_active())
	{
		tgm_service_send_temp_notify(centitemp);
	}

	k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
}

void temperature_set_measurement_interval_seconds(uint16_t seconds)
{
	temp_measurement_interval_s = (seconds == 0u) ? 1u : seconds;
	if (temperature_work.work.handler != NULL)
	{
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
	}
}

int32_t battery_voltage_read(void)
{
	return battery_get_last_measurement();
}

static void boot_apply_led_and_worn_policy(void)
{
	worn = false;
	reset_off_dock_hot_sustain();
	charge_detector_reset();
	tgm_service_set_device_worn(false);
	tgm_service_set_on_dock(on_dock);
	ppg_apply_off_body_leds();

	device_state = on_dock ? DEVICE_STATE_NOT_WORN_ON_DOCK
			       : DEVICE_STATE_NOT_WORN_OFF_DOCK;
	apply_battery_leds();
}

static void chrsts_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	ARG_UNUSED(pins);

#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	atomic_inc(&chrsts_edge_count);
	LOG_DBG("chrsts edge phy=%d dt=%d count=%ld", chrsts_pin_physical(),
		gpio_pin_get_dt(&chrsts_gpio), (long)atomic_get(&chrsts_edge_count));
#else
	LOG_INF("chrsts GPIO edge phy=%d dt=%d", chrsts_pin_physical(),
		gpio_pin_get_dt(&chrsts_gpio));
	tgm_service_fw_log_printf("fw: chrsts edge phy=%d dt=%d deb=%dms",
				  chrsts_pin_physical(), gpio_pin_get_dt(&chrsts_gpio),
				  CONFIG_CHRSTS_DEBOUNCE_MS);

	(void)k_work_reschedule(&chrsts_debounce_work, K_MSEC(CONFIG_CHRSTS_DEBOUNCE_MS));
#endif
}

static void chrsts_bench_fill(struct tgm_chrsts_bench_t *out)
{
	bool majority_on_dock = chrsts_read_on_dock_majority();
	const int phy = chrsts_pin_physical();
	const int dt = gpio_pin_get_dt(&chrsts_gpio);

	if (!out)
	{
		return;
	}

	out->phy = (uint8_t)((phy < 0) ? 0 : phy);
	out->dt = (uint8_t)((dt < 0) ? 0 : dt);
	out->active = chrsts_gpio_active() ? 1U : 0U;
	out->maj = majority_on_dock ? 1U : 0U;
	out->dock_raw = dock_raw ? 1U : 0U;
	out->on_dock = on_dock ? 1U : 0U;
#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	{
		const atomic_val_t edges = atomic_get(&chrsts_edge_count);

		out->stable_s = (uint8_t)stat_phase;
		out->mismatch = (uint8_t)((edges > 255) ? 255 : edges);
	}
#else
	out->stable_s = dock_raw_stable_s;
	out->mismatch = dock_mismatch_votes;
#endif
}

static void ppg_stream_start_cb(void)
{
	if (ble_is_connected() || tgm_service_connect_probe_active())
	{
		apply_worn_sensing_leds();
	}
}

static void ir_pulse_worn_cb(bool pulse)
{
	if (user_mode_overrides_auto()) {
		return;
	}

	update_state(on_dock, pulse);
}

struct tgm_service_cb tgm_service_callbacks = {
	.bat_cb = battery_voltage_read,
	.link_connected_cb = ble_link_connected_cb,
	.link_disconnected_cb = ble_link_disconnected_cb,
	.ppg_stream_start_cb = ppg_stream_start_cb,
	.probe_begin_cb = connect_probe_begin_cb,
	.probe_end_cb = connect_probe_end_cb,
	.diagnostic_snapshot_cb = diagnostic_snapshot_cb,
	.chrsts_bench_cb = chrsts_bench_fill,
	.user_mode_changed_cb = user_mode_changed_cb,
	.ir_pulse_worn_cb = ir_pulse_worn_cb,
};

int main(void)
{
	int err;

	printk("TGM Application %s\n", APP_VERSION_STRING);

	if (!device_is_ready(gpio))
	{
		LOG_ERR("GPIO is not ready");
		return -1;
	}

	err = gpio_pin_configure(gpio, 10, GPIO_OUTPUT_HIGH);
	if (err)
	{
		LOG_ERR("Failed to latch BATEN (P0.10), err=%d", err);
		return err;
	}
	printk("*** BATTERY POWER ENABLED ON P0.10 ***\n");

	err = gpio_pin_configure(gpio, SENS_ENABLE_PIN, GPIO_OUTPUT_HIGH);

	err = ble_init();
	if (err)
	{
		LOG_ERR("ble_init() returned %d", err);
		return err;
	}

	tgm_service_init(&tgm_service_callbacks);
	battery_led_indicator_init();

	k_work_init_delayable(&temperature_work, temperature_work_handler);

	ble_adv_start();

	err = battery_init(battery_measurement_ready);
	if (err)
	{
		LOG_ERR("battery_init() returned %d", err);
	}

	err = battery_start_measurement();
	if (err)
	{
		LOG_ERR("battery_start_measurement() returned %d", err);
	}

	err = gpio_pin_set(gpio, SENS_ENABLE_PIN, 1);
	if (err)
	{
		LOG_ERR("Failed to enable sensor power");
		return err;
	}

	k_sleep(K_MSEC(100));

	err = ppg_init();
	if (err)
	{
		LOG_ERR("ppg_init() returned %d", err);
	}

	err = acc_init();
	if (err)
	{
		LOG_ERR("acc_init() returned %d", err);
	}

	err = gpio_pin_configure_dt(&chrsts_gpio, GPIO_INPUT);
	if (err)
	{
		LOG_ERR("Failed to configure chrsts pin");
		return err;
	}

	gpio_init_callback(&chrsts_cb, chrsts_callback, BIT(chrsts_gpio.pin));
	err = gpio_add_callback(gpio, &chrsts_cb);
	if (err)
	{
		LOG_ERR("Failed to add callback to chrsts pin");
		return err;
	}

#if !IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	k_work_init_delayable(&chrsts_debounce_work, chrsts_debounce_handler);
#endif

	on_dock = chrsts_read_on_dock_majority();
	dock_raw = on_dock;
#if IS_ENABLED(CONFIG_CHRSTS_STAT_ACTIVITY)
	stat_phase = on_dock ? STAT_PHASE_TAPER : STAT_PHASE_OFF;
	atomic_set(&chrsts_edge_count, 0);
	stat_assert_hold_s = on_dock ? (uint8_t)CONFIG_CHRSTS_TAPER_HOLD_S : 0;
	stat_inactive_hold_s = on_dock ? 0 : (uint8_t)CONFIG_CHRSTS_UNDOCK_HOLD_S;
#else
	dock_raw_stable_s = CONFIG_DOCK_STATE_STABLE_S;
#endif
	chrsts_log_snapshot("boot", on_dock);

	err = gpio_pin_interrupt_configure(gpio, chrsts_gpio.pin, GPIO_INT_EDGE_BOTH);
	if (err)
	{
		LOG_ERR("Failed to configure chrsts pin interrupt");
		return err;
	}

	boot_apply_led_and_worn_policy();
	sync_off_body_sensors();

	k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
	LOG_INF("Temperature monitoring started");
	LOG_INF("=== INITIALIZATION COMPLETE ===");

	dock_poll_and_refresh();

	uint32_t uptime_s = 0;
	uint8_t dock_poll_s = 0;
	uint8_t led_refresh_s = 0;
	uint8_t dock_battery_s = 0;
	while (1) {
		(void)gpio_pin_set(gpio, 10, 1);
		ble_ensure_advertising();
		if (++dock_poll_s >= 3) {
			dock_poll_s = 0;
			dock_poll_and_refresh();
		}

		if (on_dock) {
			if (++dock_battery_s >= 60) {
				dock_battery_s = 0;
				(void)battery_start_measurement();
			}
		} else {
			dock_battery_s = 0;
		}

		if (++led_refresh_s >= 2) {
			led_refresh_s = 0;
			const bool link_idle = tgm_service_link_notify_idle();
			const bool pad_owns_leds =
				stat_on_pad_now() &&
				!tgm_service_mode3_pad_grace_active() &&
				(!tgm_service_sensor_notify_enabled() ||
				 link_idle);

			if (pad_owns_leds) {
				if (ble_is_connected() && link_idle) {
					ble_force_recover("led policy pad idle");
				}
				if (!tgm_service_connect_probe_active()) {
					apply_battery_leds();
				}
			} else if (ble_is_connected() &&
				   tgm_service_ppg_notify_active() &&
				   !link_idle) {
				apply_worn_sensing_leds();
			} else if (!tgm_service_connect_probe_active()) {
				if (ble_is_connected() &&
				    (tgm_service_sensor_stream_stale() ||
				     link_idle)) {
					ble_force_recover("led policy idle/stale");
				}
				if (!tgm_service_ppg_notify_active()) {
					ppg_apply_off_body_leds();
				}
				apply_battery_leds();
			}
		}

		uptime_s++;
		const uint16_t reboot_s = tgm_service_get_debug_reboot_interval_s();
		if (reboot_s > 0 && uptime_s >= reboot_s) {
			LOG_WRN("Debug reboot after %us (interval %us)", uptime_s, reboot_s);
			k_sleep(K_MSEC(100));
			sys_reboot(SYS_REBOOT_WARM);
		}

		k_sleep(K_SECONDS(1));
	}

	return 0;
}
