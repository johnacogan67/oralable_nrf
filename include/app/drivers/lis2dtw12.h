/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef LIS2DTW12_H
#define LIS2DTW12_H

#include <stdbool.h>
#include <stdint.h>

#include <zephyr/drivers/i2c.h>

struct acc_sample
{
    int16_t x;
    int16_t y;
    int16_t z;
};

/* CTRL1 (0x20): ODR bits 7:4, MODE bits 3:2, LP_MODE bits 1:0.
 * 0x43 is ODR 50 Hz, low-power mode, LP mode 4. 0x44 is 50 Hz high-performance, not LP4.
 */
#define LIS2DTW12_CTRL1_ODR_SHIFT 4
#define LIS2DTW12_CTRL1_ODR_MASK 0xF0
#define LIS2DTW12_CTRL1_ODR_50HZ 0x40
#define LIS2DTW12_CTRL1_MODE_LOW_POWER 0x00
#define LIS2DTW12_CTRL1_LP_MODE_4 0x03
#define LIS2DTW12_CTRL1_ODR50_LP4 \
	(LIS2DTW12_CTRL1_ODR_50HZ | LIS2DTW12_CTRL1_MODE_LOW_POWER | LIS2DTW12_CTRL1_LP_MODE_4)

/* FIFO_CTRL (0x2E): bit 5 selects stream mode; bits 4:0 are the watermark. */
#define LIS2DTW12_FIFO_MODE_STREAM 0x20
#define LIS2DTW12_FIFO_SAMPLES_OVR 0x40
#define LIS2DTW12_FIFO_SAMPLES_DIFF 0x3F
#define LIS2DTW12_FIFO_MAX_BURST_SAMPLES 25
#define LIS2DTW12_FIFO_RAW_BYTES_PER_SAMPLE 6
#define LIS2DTW12_FIFO_MAX_BURST_BYTES 150

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

/** @brief True when a previous read left unread accelerometer samples in the FIFO. */
bool acc_sensor_fifo_backlog(void);

/** @brief FIFO_OVR events. A sample count above the batch size is not included. */
uint32_t acc_sensor_fifo_overflow_events(void);

/** @brief Times the unread count exceeded one batch without FIFO_OVR. */
uint32_t acc_sensor_fifo_backlog_events(void);

#endif // LIS2DTW12_H