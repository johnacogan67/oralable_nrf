/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef ACC_H_
#define ACC_H_

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