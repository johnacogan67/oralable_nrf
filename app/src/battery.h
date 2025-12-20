/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BATTERY_H_
#define BATTERY_H_

#include <zephyr/kernel.h>

typedef void (*battery_data_ready_t)(int32_t battery_value);

int battery_init(battery_data_ready_t battery_data_ready_cb);
int battery_start_measurement(void);
int32_t battery_get_last_measurement(void);
uint8_t battery_voltage_to_percent(int32_t voltage_mv);

#endif /* BATTERY_H_ */
