/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include <zephyr/kernel.h>

typedef void (*battery_data_ready_t)(int32_t battery_value);

/** Telemetry struct: voltage, percent, mAh consumed (from power_profiler), remaining minutes */
struct battery_usage_telemetry_t {
	int32_t voltage_mv;
	uint8_t percent;
	uint32_t mah_consumed_scaled; /* mAh * 100 for 0.01 mAh resolution */
	uint32_t remaining_minutes;
};

int battery_init(battery_data_ready_t battery_data_ready_cb);
int battery_start_measurement(void);
int32_t battery_get_last_measurement(void);
uint8_t battery_voltage_to_percent(int32_t voltage_mv);
uint8_t battery_get_percentage(void);

/** Populate telemetry from battery ADC + power_profiler (links to power_profiler values) */
void battery_get_usage_telemetry(struct battery_usage_telemetry_t *out);

/** Update periodic measurement interval (seconds). 0 disables periodic rescheduling. */
void battery_set_measurement_interval_seconds(uint16_t seconds);

/** Returns current measurement interval (seconds). */
uint16_t battery_get_measurement_interval_seconds(void);

#endif /* BATTERY_H_ */
