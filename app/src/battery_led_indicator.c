/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Off-body battery status using PPG LEDs (Phase 0 vitals policy):
 *
 * Status LEDs are green-only. Never drive red for charge / idle status.
 *
 * On dock / charger (no BLE):
 *   - Flash green while SOC/voltage is still below hold (STAT charging or
 *     early taper). LTC4124 STAT taper alone is not "full".
 *   - Rare green pulse when SOC/voltage is in hold. Continuous solid green
 *     would drain a small cell after the charger has stopped.
 *   - If voltage sags out of hold, flash again until the cell is back in
 *     hold (LTC4124 recharge / STAT blink).
 *
 * Off dock (no BLE): status LEDs off.
 *
 * BLE connected:
 *   - No status LEDs — optical emitters are PPG-only (red/IR while PPG CCC is on).
 */

#include "battery_led_indicator.h"
#include "ppg.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(battery_led, CONFIG_APP_LOG_LEVEL);

/** Status LED peak PA (~half prior drive; iOS mirrors state when dim). */
#define LED_PA_STATUS 28U
/** Charging flash: brief pulse, long off — on-dock only. */
#define LED_FLASH_ON_MS 350U
#define LED_FLASH_OFF_MS 2000U
/** Hold: rare pulse so the LED does not discharge a hold-range cell. */
#define LED_HOLD_ON_MS 200U
#define LED_HOLD_OFF_MS 10000U

enum indicator_pattern {
	INDICATOR_NONE = 0,
	INDICATOR_FLASH_GREEN,
	INDICATOR_HOLD_GREEN,
};

static struct k_work_delayable flash_work;
static enum indicator_pattern active_pattern = INDICATOR_NONE;
static bool flash_phase_on;
static bool hold_latched;
static uint32_t led_gen;
static uint32_t flash_gen;

static uint32_t pattern_on_ms(enum indicator_pattern pattern)
{
	return pattern == INDICATOR_HOLD_GREEN ? LED_HOLD_ON_MS : LED_FLASH_ON_MS;
}

static uint32_t pattern_off_ms(enum indicator_pattern pattern)
{
	return pattern == INDICATOR_HOLD_GREEN ? LED_HOLD_OFF_MS : LED_FLASH_OFF_MS;
}

static bool voltage_enters_hold(uint8_t battery_pct, int32_t battery_mv)
{
	if (battery_pct >= BATTERY_LED_HOLD_GREEN_PCT) {
		return true;
	}
	return battery_mv >= BATTERY_LED_HOLD_GREEN_MV;
}

static bool voltage_leaves_hold(uint8_t battery_pct, int32_t battery_mv)
{
	return (battery_pct < BATTERY_LED_HOLD_GREEN_EXIT_PCT) &&
	       (battery_mv < BATTERY_LED_HOLD_GREEN_EXIT_MV);
}

static bool in_hold_range(uint8_t battery_pct, int32_t battery_mv)
{
	if (voltage_enters_hold(battery_pct, battery_mv)) {
		hold_latched = true;
	} else if (voltage_leaves_hold(battery_pct, battery_mv)) {
		hold_latched = false;
	}
	return hold_latched;
}

static void stop_flash(void)
{
	(void)k_work_cancel_delayable(&flash_work);
	active_pattern = INDICATOR_NONE;
}

static void apply_dark(void)
{
	stop_flash();
	hold_latched = false;
	(void)ppg_ensure_awake();
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0U);
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_stop_streaming("battery-dark");
}

static void flash_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (flash_gen != led_gen) {
		return;
	}

	(void)ppg_ensure_awake_status_leds();

	if (flash_gen != led_gen) {
		return;
	}

	flash_phase_on = !flash_phase_on;
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_set_led_pa(PPG_LED_GREEN, flash_phase_on ? LED_PA_STATUS : 0U);
	const uint32_t delay_ms = flash_phase_on
					  ? pattern_on_ms(active_pattern)
					  : pattern_off_ms(active_pattern);
	(void)k_work_reschedule(&flash_work, K_MSEC(delay_ms));
}

static void start_pattern(enum indicator_pattern pattern)
{
	flash_gen = led_gen;

	if (active_pattern == pattern) {
		(void)ppg_set_led_pa(PPG_LED_RED, 0U);
		(void)ppg_set_led_pa(PPG_LED_IR, 0U);
		(void)ppg_set_led_pa(PPG_LED_GREEN,
				     flash_phase_on ? LED_PA_STATUS : 0U);
		const uint32_t delay_ms = flash_phase_on
						  ? pattern_on_ms(pattern)
						  : pattern_off_ms(pattern);
		(void)k_work_reschedule(&flash_work, K_MSEC(delay_ms));
		return;
	}

	stop_flash();
	active_pattern = pattern;
	flash_phase_on = true;
	flash_gen = led_gen;
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_set_led_pa(PPG_LED_GREEN, LED_PA_STATUS);
	(void)k_work_reschedule(&flash_work, K_MSEC(pattern_on_ms(pattern)));
}

int battery_led_indicator_init(void)
{
	k_work_init_delayable(&flash_work, flash_work_handler);
	return 0;
}

void battery_led_indicator_suspend(void)
{
	led_gen++;
	apply_dark();
}

void battery_led_indicator_apply(bool on_dock, bool worn, uint8_t battery_pct,
				 int32_t battery_mv, bool charge_active)
{
	enum indicator_pattern want;

	ARG_UNUSED(charge_active);

	if (worn) {
		if (active_pattern != INDICATOR_NONE) {
			led_gen++;
			stop_flash();
		}
		LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
			on_dock, charge_active, worn, battery_mv, battery_pct,
			active_pattern);
		return;
	}

	if (!on_dock) {
		if (active_pattern != INDICATOR_NONE || hold_latched) {
			led_gen++;
			apply_dark();
		}
		LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
			on_dock, charge_active, worn, battery_mv, battery_pct,
			active_pattern);
		return;
	}

	want = in_hold_range(battery_pct, battery_mv) ? INDICATOR_HOLD_GREEN
						      : INDICATOR_FLASH_GREEN;

	/*
	 * Same pattern: leave the delayable work alone so a 10 s hold-off
	 * is not restarted every status tick.
	 */
	if (active_pattern == want) {
		return;
	}

	led_gen++;
	(void)ppg_ensure_awake_status_leds();
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	start_pattern(want);

	LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
		on_dock, charge_active, worn, battery_mv, battery_pct,
		active_pattern);
}
