/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BATTERY_H
#define BATTERY_H

typedef void (*battery_data_ready_t)(int32_t battery_value);

// Function declarations
int battery_init(battery_data_ready_t battery_data_ready_cb);
int battery_start_measurement();
int32_t battery_get_last_measurement();

#endif // BATTERY_H