/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <app/drivers/maxm86161.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(maxm86161, CONFIG_MAXM86161_LOG_LEVEL);

// Use R, IR and Green
#define COLORS 3u

int ppg_sensor_start(const struct i2c_dt_spec *i2c)
{
	int err = 0;

	// Set the FIFO AFULL threshold based on desired FIFO_SIZE
	uint8_t fifo_config1 = (128 - (COLORS * CONFIG_PPG_SAMPLES_PER_FRAME));
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_FIFO_CONFIG1, &fifo_config1, 1);
	if (err)
	{
		LOG_ERR("Failed to set FIFO AFULL threshold");
	}

	// Set the LED sequence to have red first, IR second and Green last (green = LED1, ir = LED2, red = LED3)
	uint8_t led_seq_reg[3] = {0x23, 0x01, 0x00};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED_SEQ_REG1, led_seq_reg, 3);
	if (err)
	{
		LOG_ERR("Failed to set LED sequence");
	}

	// Set the LED range to 31mA
	uint8_t led_range[2] = {0x0, 0x0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED_RANGE1, led_range, 2);
	if (err)
	{
		LOG_ERR("Failed to set LED range");
	}

	// Configure the PPG ADC range to be 8192nA full scale and set the pulse width to maximum
	uint8_t ppg_config1 = 0b00010111;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_PPG_CONFIG1, &ppg_config1, 1);
	if (err)
	{
		LOG_ERR("Failed to set PPG config 1");
	}

	// Set the sample rate to 50Hz without averaging
	uint8_t ppg_config2 = 0b00001000;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_PPG_CONFIG2, &ppg_config2, 1);
	if (err)
	{
		LOG_ERR("Failed to set PPG config 2");
	}

	// Set the drive strength of the LEDs (green = LED1, ir = LED2, red = LED3)
	// TODO tune these values, each count = 0.12mA with LED range set to 31mA
	uint8_t led_pa[3] = {0, 0, 0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED1_PA, led_pa, 3);
	if (err)
	{
		LOG_ERR("Failed to set LED pulse amplitude");
	}

	// Reset the interrupt status registers by simply reading them
	uint8_t dummy[2];
	err = i2c_burst_read_dt(i2c, MAXM86161_REG_INT_STAT_1, dummy, 2);

	// Flush the FIFO and configure the interrupt to be cleared when FIFO_DATA register is read
	uint8_t fifo_config2 = 0b00011010;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_FIFO_CONFIG2, &fifo_config2, 1);
	if (err)
	{
		LOG_ERR("Failed to flush FIFO");
	}

	// Enable the FIFO interrupt
	uint8_t int_en_1 = 0b10000000;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_INT_EN_1, &int_en_1, 1);
	if (err)
	{
		LOG_ERR("Failed to enable FIFO interrupt");
	}

	// Enable the PPG sensor in low power mode
	uint8_t system_control = 0b00001100;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &system_control, 1);
	if (err)
	{
		LOG_ERR("Failed to enable PPG sensor");
	}

	return 0;
}

int ppg_sensor_stop(const struct i2c_dt_spec *i2c)
{
	int err = 0;

	// Disable the FIFO interrupt
	uint8_t int_en_1 = 0;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_INT_EN_1, &int_en_1, 1);
	if (err)
	{
		LOG_ERR("Failed to disable FIFO interrupt");
	}

	// Turn off the LEDs
	uint8_t led_pa[3] = {0, 0, 0};
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_LED1_PA, led_pa, 3);
	if (err)
	{
		LOG_ERR("Failed to turn off LEDs");
	}

	// Put the sensor in power-save mode
	uint8_t system_control = 0b00000010;
	err = i2c_burst_write_dt(i2c, MAXM86161_REG_SYSTEM_CONTROL, &system_control, 1);
	if (err)
	{
		LOG_ERR("Failed to disable PPG sensor");
	}

	return 0;
}

int ppg_sensor_get_data(const struct i2c_dt_spec *i2c, struct ppg_sample *ppg_data, uint8_t *sample_count)
{
	int err;

	// Check for overflow
	uint8_t fifo_overflow;
	err = i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_OVF_CNT, &fifo_overflow, 1);
	if (err)
	{
		LOG_ERR("Failed to read FIFO overflow count");
	}

	// Check for data count
	uint8_t fifo_data_count;
	err = i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_DATA_CNT, &fifo_data_count, 1);
	if (err)
	{
		LOG_ERR("Failed to read FIFO data count");
	}
	else
	{
		LOG_DBG("FIFO data count: %d", fifo_data_count);

		if (fifo_data_count > CONFIG_PPG_SAMPLES_PER_FRAME * COLORS)
		{
			LOG_WRN("FIFO overflow detected, discarding data (samples = %d)", fifo_data_count);
			fifo_data_count = CONFIG_PPG_SAMPLES_PER_FRAME * COLORS;
		}
	}

	// Read the FIFO data
	uint8_t fifo_data[fifo_data_count * 3]; // 3 bytes per sample
	err = i2c_burst_read_dt(i2c, MAXM86161_REG_FIFO_DATA, fifo_data, fifo_data_count * 3);
	if (err)
	{
		LOG_ERR("Failed to read FIFO data");
	}

	*sample_count = fifo_data_count / COLORS;
	// Parse the data
	for (int i = 0; i < *sample_count; i++)
	{
		ppg_data[i].red = (((fifo_data[i * 6] << 16) | (fifo_data[i * 6 + 1] << 8) | fifo_data[i * 6 + 2]) & 0x7ffff);
		ppg_data[i].ir = (((fifo_data[i * 6 + 3] << 16) | (fifo_data[i * 6 + 4] << 8) | fifo_data[i * 6 + 5]) & 0x7ffff);
		ppg_data[i].green = (((fifo_data[i * 6 + 6] << 16) | (fifo_data[i * 6 + 7] << 8) | fifo_data[i * 6 + 8]) & 0x7ffff);
		LOG_DBG("PPG data: red = %d, ir = %d, green = %d", ppg_data[i].red, ppg_data[i].ir, ppg_data[i].green);
	}

	return 0;
}

int ppg_sensor_read_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t *data)
{
	int err = i2c_burst_read_dt(i2c, reg, data, 1);
	if (err)
	{
		LOG_ERR("Failed to read register 0x%02x", reg);
	}

	return err;
}

int ppg_sensor_write_reg(const struct i2c_dt_spec *i2c, uint8_t reg, uint8_t data)
{
	int err = i2c_burst_write_dt(i2c, reg, &data, 1);
	if (err)
	{
		LOG_ERR("Failed to write register 0x%02x", reg);
	}

	return err;
}