/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include <zephyr/kernel.h>

typedef void (*battery_data_ready_t)(int32_t battery_value);

struct battery_usage_telemetry_t {
	uint32_t voltage_mv;
	uint8_t percent;
	uint32_t mah_consumed_scaled;
	uint32_t remaining_minutes;
};

int battery_init(battery_data_ready_t battery_data_ready_cb);
int battery_start_measurement(void);
int32_t battery_get_last_measurement(void);
/** One ADC sample now, unsmoothed. 0 if the read is implausible. Does not update the gauge. */
int32_t battery_read_now_mv(void);
/** User / remapped gauge (3.61 V = 0%, 4.35 V = 100%). */
uint8_t battery_voltage_to_percent(int32_t voltage_mv);
/** Chemistry curve (3.0 V = 0%, 4.35 V = 100%) — for logs / diagnostics only. */
uint8_t battery_voltage_to_chemistry_percent(int32_t voltage_mv);
void battery_get_usage_telemetry(struct battery_usage_telemetry_t *out);
void battery_set_measurement_interval_seconds(uint16_t seconds);
/** True when last gauge is <5% or <3.61 V. False if no sample yet (mv<=0). */
bool battery_below_soft_floor(void);

#endif /* BATTERY_H_ */
