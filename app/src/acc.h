/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef ACC_H_
#define ACC_H_

#include <app/drivers/lis2dtw12.h>
#include <zephyr/kernel.h>

/**
 * @brief Initialize the accelerometer sensor
 *
 * @return int 0 on success, negative error code on failure
 */
int acc_init(void);

/**
 * @brief Start the accelerometer sensor with default configuration
 *
 * @return int 0 on success, negative error code on failure
 */
int acc_start(void);

/**
 * @brief Stop the accelerometer sensor
 *
 * @return int 0 on success, negative error code on failure
 */
int acc_stop(void);

/**
 * @brief Request one FIFO read outside the GPIO interrupt path.
 */
void acc_poll_once(void);

/**
 * @brief Enable or disable the shared 20 ms FIFO poll.
 *
 * This keeps Gen 2 streaming while the REV11 INT_ACC net is verified.
 */
void acc_set_notify_poll(bool enabled);

/** @brief True while the accelerometer notify poll wants the 20 ms loop. */
bool acc_sample_loop_enabled(void);

/**
 * @brief Read the LIS2DTW12 FIFO.
 *
 * Caller holds sensor_i2c_lock. Does not sleep.
 */
int acc_read_fifo(struct acc_sample *dst, uint8_t *count);

/**
 * @brief Read a register from the accelerometer sensor and report over BLE
 *
 * @param[in] reg Register address
 * @return int 0 on success, negative error code on failure
 */
int acc_read_reg(uint8_t reg);

/**
 * @brief Write a register to the accelerometer sensor and report over BLE
 *
 * @param[in] reg Register address
 * @param[in] data Data to write
 * @return int 0 on success, negative error code on failure
 */
int acc_write_reg(uint8_t reg, uint8_t data);

#endif /* ACC_H_ */
