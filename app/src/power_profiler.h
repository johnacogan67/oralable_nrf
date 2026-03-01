/*
 * Copyright (c) 2024 WeeGee bv
 * Power Profiler for Oralable MAM - state-aware mAh integration
 * PROJECT: POWER_PROFILER_INTEGRATION (2026-02-27)
 *
 * Tracks duration in each power state to compute total mAh consumed.
 * Used for runtime projections on CG-320B (15 mAh) battery.
 */

#ifndef POWER_PROFILER_H_
#define POWER_PROFILER_H_

#include <stdint.h>
#include <stdbool.h>

/** Power states - used for current draw modeling */
typedef enum {
	POWER_STATE_IDLE,   /* Before advertising */
	POWER_STATE_ADV,    /* Advertising only: ~25 µA */
	POWER_STATE_CONN,   /* Connected, no streaming: ~50 µA */
	POWER_STATE_STREAM  /* PPG + Accel streaming: ~1.4 mA */
} power_state_t;

/** Initialize the power profiler (call once at startup) */
void power_profiler_init(void);

/** Update power state and integrate mAh since last transition */
void power_profiler_set_state(power_state_t new_state);

/** Get total mAh consumed since power-on */
float power_profiler_get_total_mah(void);

/** Get current power state */
power_state_t power_profiler_get_state(void);

/** Get estimated remaining runtime in minutes at current state
 *  Based on CG-320B capacity (15 mAh) minus consumed mAh
 */
uint32_t power_profiler_get_remaining_minutes(void);

/** Telemetry struct for battery stats characteristic */
struct power_profiler_telemetry {
	int32_t voltage_mv;
	uint8_t percent;
	uint32_t remaining_minutes;
	float total_mah;
};

/** Populate telemetry struct (for BatteryStats notify) */
void power_profiler_get_telemetry(struct power_profiler_telemetry *out);

#endif /* POWER_PROFILER_H_ */
