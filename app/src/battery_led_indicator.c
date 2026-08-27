/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Off-body battery status using PPG LEDs (Phase 0 vitals policy):
 *
 * Status LEDs are green-only. Never drive red for charge / idle status.
 *
 * On dock / charger (no BLE) — LTC4124 STAT:
 *   - Flashing green while charge_active (STAT blinking) and SOC/voltage still low
 *   - Solid green when charge_active is false (taper / hold)
 *   - Solid green when SOC or voltage is already high even if STAT still blinks
 *     (avoids days of flash-green on a cell that is already hold-range)
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
/** Asymmetric flash: brief pulse, long off — on-dock only. */
#define LED_FLASH_ON_MS 350U
#define LED_FLASH_ON_DOCK_MS 2000U

enum indicator_pattern {
	INDICATOR_NONE = 0,
	INDICATOR_SOLID_GREEN,
	INDICATOR_FLASH_GREEN,
};

static struct k_work_delayable flash_work;
static enum indicator_pattern active_pattern = INDICATOR_NONE;
static bool flash_phase_on;
static uint32_t led_gen;
static uint32_t flash_gen;

static bool should_hold_solid_green(uint8_t battery_pct, int32_t battery_mv)
{
	if (battery_pct >= BATTERY_LED_HOLD_GREEN_PCT) {
		return true;
	}
	return battery_mv >= BATTERY_LED_HOLD_GREEN_MV;
}

static void stop_flash(void)
{
	(void)k_work_cancel_delayable(&flash_work);
	active_pattern = INDICATOR_NONE;
}

static void apply_dark(void)
{
	stop_flash();
	(void)ppg_ensure_awake();
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0U);
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_stop_streaming();
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
	const uint32_t delay_ms = flash_phase_on ? LED_FLASH_ON_MS
						 : LED_FLASH_ON_DOCK_MS;
	(void)k_work_reschedule(&flash_work, K_MSEC(delay_ms));
}

static void start_flash_green(void)
{
	flash_gen = led_gen;

	if (active_pattern == INDICATOR_FLASH_GREEN) {
		(void)ppg_set_led_pa(PPG_LED_RED, 0U);
		(void)ppg_set_led_pa(PPG_LED_IR, 0U);
		(void)ppg_set_led_pa(PPG_LED_GREEN,
				     flash_phase_on ? LED_PA_STATUS : 0U);
		const uint32_t delay_ms = flash_phase_on ? LED_FLASH_ON_MS
							 : LED_FLASH_ON_DOCK_MS;
		(void)k_work_reschedule(&flash_work, K_MSEC(delay_ms));
		return;
	}

	stop_flash();
	active_pattern = INDICATOR_FLASH_GREEN;
	flash_phase_on = true;
	flash_gen = led_gen;
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_set_led_pa(PPG_LED_GREEN, LED_PA_STATUS);
	(void)k_work_reschedule(&flash_work, K_MSEC(LED_FLASH_ON_MS));
}

static void apply_solid_green(void)
{
	if (active_pattern == INDICATOR_SOLID_GREEN) {
		(void)ppg_set_led_pa(PPG_LED_RED, 0U);
		(void)ppg_set_led_pa(PPG_LED_IR, 0U);
		(void)ppg_set_led_pa(PPG_LED_GREEN, LED_PA_STATUS);
		return;
	}

	stop_flash();
	active_pattern = INDICATOR_SOLID_GREEN;
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_set_led_pa(PPG_LED_GREEN, LED_PA_STATUS);
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
	led_gen++;

	if (worn) {
		stop_flash();
		LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
			on_dock, charge_active, worn, battery_mv, battery_pct, active_pattern);
		return;
	}

	if (!on_dock) {
		apply_dark();
		LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
			on_dock, charge_active, worn, battery_mv, battery_pct, active_pattern);
		return;
	}

	(void)ppg_ensure_awake_status_leds();
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);

	if (!charge_active || should_hold_solid_green(battery_pct, battery_mv)) {
		apply_solid_green();
	} else {
		start_flash_green();
	}

	LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
		on_dock, charge_active, worn, battery_mv, battery_pct, active_pattern);
}
