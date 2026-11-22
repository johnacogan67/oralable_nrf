/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Off-body battery status using the optical LEDs:
 *
 * Off dock: LEDs off. A coil that misses completely looks the same,
 * because the status pin never moves.
 *
 * On dock, cell in hold (4.20 V): short green flash every 10 s.
 * The slow flash ends once the cell falls below 4.10 V.
 * A solid green would keep drawing after the charger has stopped.
 *
 * On dock, under hold: green flash (350 ms on, 2 s off) while the window
 * is open or the cell has risen. A blinking status pin means the charger
 * is delivering current, so that case stays on the green flash.
 * After 15 min on the case with the pin steady and the cell not 12 mV
 * above the sample taken at the start of the window, red stays on.
 * A later rise of 12 mV returns to the green flash and opens a new window.
 *
 * Infrared stays off. A live pulse stream owns the emitters.
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "battery_led_indicator.h"
#include "ppg.h"
#include "tgm_service.h"

LOG_MODULE_REGISTER(battery_led, CONFIG_APP_LOG_LEVEL);

/** Status LED peak PA (~half prior drive; iOS mirrors state when dim). */
#define LED_PA_STATUS 28U
/** Charging flash: brief pulse, long off — on-dock only. */
#define LED_FLASH_ON_MS 350U
#define LED_FLASH_OFF_MS 2000U
/** Hold: rare pulse so the LED does not discharge a hold-range cell. */
#define LED_HOLD_ON_MS 200U
#define LED_HOLD_OFF_MS 10000U
/** On-case window before a flat or falling cell is called not charging. */
#define FILL_WINDOW_MS (15 * 60 * 1000)
/** Rise across that window that counts as current entering the cell. */
#define FILL_RISE_MV 12

enum indicator_pattern {
	INDICATOR_NONE = 0,
	INDICATOR_FLASH_GREEN,
	INDICATOR_HOLD_GREEN,
	INDICATOR_FAULT_RED,
};

static struct k_work_delayable flash_work;
static enum indicator_pattern active_pattern = INDICATOR_NONE;
static bool flash_phase_on;
static bool hold_latched;
static uint32_t led_gen;
static uint32_t flash_gen;
static bool fill_tracking;
static bool fill_fault;
static int32_t fill_base_mv;
static int64_t fill_base_ms;

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

static void fill_reset(void)
{
	fill_tracking = false;
	fill_fault = false;
}

/*
 * The status pin blinks whether or not current is entering the cell.
 * Compare the cell voltage with the sample from the start of a 15 min
 * on-case window. A miss of 12 mV latches the red fault until a later
 * sample clears it.
 */
static void fill_note(int32_t battery_mv)
{
	const int64_t now = k_uptime_get();

	if (battery_mv <= 0) {
		return;
	}

	if (!fill_tracking) {
		fill_tracking = true;
		fill_base_mv = battery_mv;
		fill_base_ms = now;
		fill_fault = false;
		return;
	}

	if ((now - fill_base_ms) < FILL_WINDOW_MS) {
		return;
	}

	if (battery_mv >= (fill_base_mv + FILL_RISE_MV)) {
		fill_base_mv = battery_mv;
		fill_base_ms = now;
		fill_fault = false;
		return;
	}

	fill_fault = true;
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
	fill_reset();
	(void)ppg_ensure_awake();
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0U);
	(void)ppg_set_led_pa(PPG_LED_RED, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_stop_streaming("battery-dark");
}

static void apply_solid_red(void)
{
	stop_flash();
	active_pattern = INDICATOR_FAULT_RED;
	(void)ppg_ensure_awake_status_leds();
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0U);
	(void)ppg_set_led_pa(PPG_LED_IR, 0U);
	(void)ppg_set_led_pa(PPG_LED_RED, LED_PA_STATUS);
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
		if (active_pattern != INDICATOR_NONE || hold_latched ||
		    fill_tracking) {
			led_gen++;
			apply_dark();
		}
		LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u -> pattern %d",
			on_dock, charge_active, worn, battery_mv, battery_pct,
			active_pattern);
		return;
	}

	if (in_hold_range(battery_pct, battery_mv)) {
		fill_reset();
		want = INDICATOR_HOLD_GREEN;
	} else if (charge_active) {
		/* STAT is blinking, so current is entering the cell.
		 * Do not latch the not-filling red over an active charge.
		 */
		fill_reset();
		want = INDICATOR_FLASH_GREEN;
	} else {
		fill_note(battery_mv);
		want = fill_fault ? INDICATOR_FAULT_RED : INDICATOR_FLASH_GREEN;
	}

	/*
	 * Same pattern: leave the delayable work alone so a 10 s hold-off
	 * is not restarted every status tick. The caller clears red before
	 * this runs, so a held red fault has to be written again.
	 */
	if (active_pattern == want) {
		if (want == INDICATOR_FAULT_RED) {
			(void)ppg_ensure_awake_status_leds();
			(void)ppg_set_led_pa(PPG_LED_GREEN, 0U);
			(void)ppg_set_led_pa(PPG_LED_IR, 0U);
			(void)ppg_set_led_pa(PPG_LED_RED, LED_PA_STATUS);
		}
		return;
	}

	led_gen++;
	if (want == INDICATOR_FAULT_RED) {
		apply_solid_red();
	} else {
		(void)ppg_ensure_awake_status_leds();
		(void)ppg_set_led_pa(PPG_LED_RED, 0U);
		(void)ppg_set_led_pa(PPG_LED_IR, 0U);
		start_pattern(want);
	}

	LOG_INF("Battery LED: on_dock=%d charge=%d worn=%d mv=%dmV pct=%u base=%dmV -> pattern %d",
		on_dock, charge_active, worn, battery_mv, battery_pct,
		fill_base_mv, active_pattern);
	if (want == INDICATOR_FAULT_RED) {
		tgm_service_fw_log_printf("fw: bat led red mv=%d base=%d",
					  battery_mv, fill_base_mv);
	} else if (want == INDICATOR_HOLD_GREEN) {
		tgm_service_fw_log_printf("fw: bat led hold mv=%d", battery_mv);
	} else {
		tgm_service_fw_log_printf("fw: bat led flash mv=%d", battery_mv);
	}
}
