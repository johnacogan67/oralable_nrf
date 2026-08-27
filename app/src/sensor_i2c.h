/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef SENSOR_I2C_H_
#define SENSOR_I2C_H_

/**
 * @brief Lock the shared sensor I2C bus (MAXM86161 + LIS2DTW12 on i2c0).
 *
 * PPG and ACC work items can run concurrently on the system workqueue;
 * serialize all TWI transactions to avoid bus corruption / hard faults.
 */
void sensor_i2c_lock(void);

/** @brief Release the shared sensor I2C bus lock. */
void sensor_i2c_unlock(void);

#endif /* SENSOR_I2C_H_ */
