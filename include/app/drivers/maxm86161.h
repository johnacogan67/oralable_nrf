/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef MAXM86161_H
#define MAXM86161_H

#include <zephyr/drivers/i2c.h>

typedef enum
{
	// Status
	MAXM86161_REG_INT_STAT_1 = 0x00,
	MAXM86161_REG_INT_STAT_2 = 0x01,
	MAXM86161_REG_INT_EN_1 = 0x02,
	MAXM86161_REG_INT_EN_2 = 0x03,

	// Fifo
	MAXM86161_REG_FIFO_W_PTR = 0x04,
	MAXM86161_REG_FIFO_R_PTR = 0x05,
	MAXM86161_REG_FIFO_OVF_CNT = 0x06,
	MAXM86161_REG_FIFO_DATA_CNT = 0x07,
	MAXM86161_REG_FIFO_DATA = 0x08,
	MAXM86161_REG_FIFO_CONFIG1 = 0x09,
	MAXM86161_REG_FIFO_CONFIG2 = 0x0A,

	// System Control
	MAXM86161_REG_SYSTEM_CONTROL = 0x0D,

	// PPG Config
	MAXM86161_REG_PPG_SYNC_CTRL = 0x10,
	MAXM86161_REG_PPG_CONFIG1 = 0x11,
	MAXM86161_REG_PPG_CONFIG2 = 0x12,
	MAXM86161_REG_PPG_CONFIG3 = 0x13,
	MAXM86161_REG_PROX_INT_TH = 0x14,
	MAXM86161_REG_PHOTO_DIODE_BIAS = 0x15,

	// PPG Picket Fence Detect and Replace
	MAXM86161_REG_PICKET_FENCE = 0x16,

	// LED Sequence Control
	MAXM86161_REG_LED_SEQ_REG1 = 0x20,
	MAXM86161_REG_LED_SEQ_REG2 = 0x21,
	MAXM86161_REG_LED_SEQ_REG3 = 0x22,

	// LED Pulse Amplitude
	MAXM86161_REG_LED1_PA = 0x23,
	MAXM86161_REG_LED2_PA = 0x24,
	MAXM86161_REG_LED3_PA = 0x25,
	MAXM86161_REG_LED4_PA = 0x26,
	MAXM86161_REG_LED5_PA = 0x27,
	MAXM86161_REG_LED6_PA = 0x28,
	MAXM86161_REG_LED_PILOT_PA = 0x29,
	MAXM86161_REG_LED_RANGE1 = 0x2A,
	MAXM86161_REG_LED_RANGE2 = 0x2B,

	// Temp
	MAXM86161_REG_TEMP_CONFIG = 0x40,
	MAXM86161_REG_TEMP_INT = 0x41,
	MAXM86161_REG_TEMP_FRAC = 0x42,

	// Part ID
	MAXM86161_REG_REV_ID = 0xFE,
	MAXM86161_REG_PART_ID = 0xFF,

} MAXM86161_REG_map_t;

struct ppg_sample
{
    uint32_t red;
    uint32_t ir;
    uint32_t green;
};

/**@file
 * @defgroup maxm86161 MAXM86161 Driver implementation
 * @{
 * @brief Driver for using the MAXM86161. Since this sensor is used in the context of PPG, the API is named ppg_sensor.
 */

/**
 * @brief Start the MAXM86161 sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @return int 0 on success, negative error code on failure
 */
int ppg_sensor_start(const struct i2c_dt_spec *i2c);

/**
 * @brief Stop the MAXM86161 sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @return int 0 on success, negative error code on failure
 */
int ppg_sensor_stop(const struct i2c_dt_spec *i2c);

/**
 * @brief Get the latest PPG data
 *
 * @param[in] i2c Pointer to the I2C device
 * @param[out] ppg_data Pointer to the PPG data struct
 * @param[out] sample_count Number of samples read
 * @return int 0 on success, negative error code on failure
 */
int ppg_sensor_get_data(const struct i2c_dt_spec *i2c, struct ppg_sample *ppg_data, uint8_t *sample_count);

/**
 * @brief Read a register from the PPG sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @param[in] reg Register address
 * @param[out] data Pointer to the data read
 * @return int 0 on success, negative error code on failure
 */
int ppg_sensor_read_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *data);

/**
 * @brief Write a register to the PPG sensor
 *
 * @param[in] i2c Pointer to the I2C device
 * @param[in] reg Register address
 * @param[in] data Data to write
 * @return int 0 on success, negative error code on failure
 */
int ppg_sensor_write_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t data);

#endif // MAXM86161_H