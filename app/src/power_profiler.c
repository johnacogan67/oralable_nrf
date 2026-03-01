/*
 * Copyright (c) 2024 WeeGee bv
 * Power Profiler - state-aware mAh integration using k_uptime_get()
 * PROJECT: POWER_PROFILER_INTEGRATION (2026-02-27)
 *
 * Current estimates (REV10 nRF52832):
 *   ADV:    25 µA  (advertising only)
 *   CONN:   50 µA  (connected, no streaming)
 *   STREAM: 1.4 mA (PPG + Accel notifications enabled)
 *
 * Formula: mAh_total += (current_amps * duration_ms) / 3600000.0
 */

#include <zephyr/kernel.h>
#include "power_profiler.h"
#include "battery.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(power_profiler, CONFIG_APP_LOG_LEVEL);

#define CG320B_CAPACITY_MAH 15

/* Current draw in Amps for each state */
#define CURRENT_IDLE_A    (0.000010f)  /* 10 µA */
#define CURRENT_ADV_A     (0.000025f)  /* 25 µA */
#define CURRENT_CONN_A    (0.000050f)  /* 50 µA */
#define CURRENT_STREAM_A  (0.001400f)  /* 1.4 mA */

static power_state_t current_state = POWER_STATE_IDLE;
static uint64_t state_enter_time_ms = 0;
static float total_mah = 0.0f;

static float get_current_for_state(power_state_t state)
{
	switch (state) {
	case POWER_STATE_IDLE:
		return CURRENT_IDLE_A;
	case POWER_STATE_ADV:
		return CURRENT_ADV_A;
	case POWER_STATE_CONN:
		return CURRENT_CONN_A;
	case POWER_STATE_STREAM:
		return CURRENT_STREAM_A;
	}
	return CURRENT_IDLE_A;
}

void power_profiler_init(void)
{
	current_state = POWER_STATE_IDLE;
	state_enter_time_ms = k_uptime_get();
	total_mah = 0.0f;
	LOG_INF("Power profiler initialized");
}

void power_profiler_set_state(power_state_t new_state)
{
	if (new_state == current_state) {
		return;
	}

	uint64_t now_ms = k_uptime_get();
	uint64_t duration_ms = now_ms - state_enter_time_ms;

	/* Integrate mAh for time spent in previous state */
	float current_a = get_current_for_state(current_state);
	float delta_mah = (current_a * (float)duration_ms) / 3600000.0f;
	total_mah += delta_mah;

	LOG_DBG("Power state %d -> %d, duration %llu ms, delta %.4f mAh, total %.4f mAh",
		(int)current_state, (int)new_state, duration_ms, (double)delta_mah, (double)total_mah);

	current_state = new_state;
	state_enter_time_ms = now_ms;
}

float power_profiler_get_total_mah(void)
{
	/* Add partial mAh for current state */
	uint64_t now_ms = k_uptime_get();
	uint64_t duration_ms = now_ms - state_enter_time_ms;
	float current_a = get_current_for_state(current_state);
	float partial = (current_a * (float)duration_ms) / 3600000.0f;
	return total_mah + partial;
}

power_state_t power_profiler_get_state(void)
{
	return current_state;
}

uint32_t power_profiler_get_remaining_minutes(void)
{
	float consumed = power_profiler_get_total_mah();
	float remaining_mah = (float)CG320B_CAPACITY_MAH - consumed;
	if (remaining_mah <= 0.0f) {
		return 0;
	}

	float current_a = get_current_for_state(current_state);
	if (current_a < 0.000001f) {
		return 0xFFFFFFFF; /* Essentially infinite at very low draw */
	}

	float hours = remaining_mah / (current_a * 1000.0f);
	return (uint32_t)(hours * 60.0f);
}

void power_profiler_get_telemetry(struct power_profiler_telemetry *out)
{
	if (!out) {
		return;
	}

	out->voltage_mv = battery_get_last_measurement();
	out->percent = battery_voltage_to_percent(out->voltage_mv);
	out->remaining_minutes = power_profiler_get_remaining_minutes();
	out->total_mah = power_profiler_get_total_mah();
}
