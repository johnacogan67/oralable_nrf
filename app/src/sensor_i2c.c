/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/kernel.h>

#include "sensor_i2c.h"

static K_MUTEX_DEFINE(sensor_i2c_mutex);

void sensor_i2c_lock(void)
{
    (void)k_mutex_lock(&sensor_i2c_mutex, K_FOREVER);
}

void sensor_i2c_unlock(void)
{
    (void)k_mutex_unlock(&sensor_i2c_mutex);
}
