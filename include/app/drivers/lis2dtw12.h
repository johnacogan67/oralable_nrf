/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef LIS2DTW12_H
#define LIS2DTW12_H

#include <zephyr/drivers/i2c.h>

struct acc_sample
{
    int16_t x;
    int16_t y;
    int16_t z;
};

/**@file
 * @defgroup lis2dtw12 LIS2DTW12 Driver implementation
 * @{
 * @brief Driver for using the LIS2DTW12. Since this sensor is used in the context of accelerometer, the API is named acc_sensor.
 */

/**
 * @brief Start the LIS2DTW12 sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @return int 0 on success, negative error code on failure
 */
int acc_sensor_start(const struct i2c_dt_spec *i2c);

/**
 * @brief Stop the LIS2DTW12 sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @return int 0 on success, negative error code on failure
 */
int acc_sensor_stop(const struct i2c_dt_spec *i2c);

/**
 * @brief Get the latest accelerometer data
 *
 * @param[in] i2c Pointer to the I2C device
 * @param[out] acc_data Pointer to the accelerometer data struct
 * @param[out] sample_count Number of samples read
 * @return int 0 on success, negative error code on failure
 */
int acc_sensor_get_data(const struct i2c_dt_spec *i2c, struct acc_sample *acc_data, uint8_t *sample_count);

#endif // LIS2DTW12_H