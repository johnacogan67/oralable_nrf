/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <app/drivers/lis2dtw12.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(lis2dtw12, CONFIG_LIS2DTW12_LOG_LEVEL);

typedef enum
{
    // Temperature
    LIS2DTW12_OUT_T_L = 0x0D,
    LIS2DTW12_OUT_T_H = 0x0E,

    // Part ID
    LIS2DTW12_WHO_AM_I = 0x0F,

    // Control registers
    LIS2DTW12_CTRL1 = 0x20,
    LIS2DTW12_CTRL2 = 0x21,
    LIS2DTW12_CTRL3 = 0x22,
    LIS2DTW12_CTRL4_INT1_PAD_CTRL = 0x23,
    LIS2DTW12_CTRL5_INT2_PAD_CTRL = 0x24,
    LIS2DTW12_CTRL_6 = 0x25,

    // Status
    LIS2DTW12_STATUS = 0x27,

    // Acceleration data
    LIS2DTW12_OUT_X_L = 0x28,
    LIS2DTW12_OUT_X_H = 0x29,
    LIS2DTW12_OUT_Y_L = 0x2A,
    LIS2DTW12_OUT_Y_H = 0x2B,
    LIS2DTW12_OUT_Z_L = 0x2C,
    LIS2DTW12_OUT_Z_H = 0x2D,

    // FIFO
    LIS2DTW12_FIFO_CTRL = 0x2E,
    LIS2DTW12_FIFO_SAMPLES = 0x2F,

    // Interrupts
    LIS2DTW12_TAP_THS_X = 0x30,
    LIS2DTW12_TAP_THS_Y = 0x31,
    LIS2DTW12_TAP_THS_Z = 0x32,
    LIS2DTW12_INT_DUR = 0x33,
    LIS2DTW12_WAKE_UP_THS = 0x34,
    LIS2DTW12_WAKE_UP_DUR = 0x35,
    LIS2DTW12_FREE_FALL = 0x36,
    LIS2DTW12_STATUS_DUP = 0x37,
    LIS2DTW12_WAKE_UP_SRC = 0x38,
    LIS2DTW12_TAP_SRC = 0x39,
    LIS2DTW12_SIXD_SRC = 0x3A,
    LIS2DTW12_ALL_INT_SRC = 0x3B,
    LIS2DTW12_X_OFS_USR = 0x3C,
    LIS2DTW12_Y_OFS_USR = 0x3D,
    LIS2DTW12_Z_OFS_USR = 0x3E,

    LIS2DTW12_CTRL_7 = 0x3F,

} LIS2DTW12_REG_map_t;

int acc_sensor_start(const struct i2c_dt_spec *i2c)
{
    int err = 0;

    // Set the FIFO threshold and FIFO mode (stop collecting when FIFO is full)
    uint8_t fifo_ctrl = (0x20 | CONFIG_ACC_SAMPLES_PER_FRAME);
    err = i2c_burst_write_dt(i2c, LIS2DTW12_FIFO_CTRL, &fifo_ctrl, 1);
    if (err)
    {
        LOG_ERR("Failed to set FIFO threshold");
    }

    // Enable low-noise configuration, 2g scale
    uint8_t ctrl6 = 0b00000100;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_6, &ctrl6, 1);
    if (err)
    {
        LOG_ERR("Failed to set CTRL6");
    }

    // Route FIFO threshold interrupt to INT1
    uint8_t ctrl4 = 0b00000010;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL4_INT1_PAD_CTRL, &ctrl4, 1);
    if (err)
    {
        LOG_ERR("Failed to route FIFO threshold interrupt to INT1");
    }

    // Enable the interrupts
    uint8_t ctrl7 = 0b00100000;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_7, &ctrl7, 1);
    if (err)
    {
        LOG_ERR("Failed to enable the interrupts");
    }

    // Enable the accelerometer sensor in low power mode 4 at 50Hz
    uint8_t ctrl1 = 0b01000011;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL1, &ctrl1, 1);
    if (err)
    {
        LOG_ERR("Failed to enable accelerometer sensor");
    }

    return 0;
}

int acc_sensor_stop(const struct i2c_dt_spec *i2c)
{
    int err = 0;

    // Disable the interrupts
    uint8_t ctrl7 = 0;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_7, &ctrl7, 1);
    if (err)
    {
        LOG_ERR("Failed to disable interrupts");
    }

    // Put the sensor in power-down mode
    uint8_t ctrl1 = 0;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL1, &ctrl1, 1);
    if (err)
    {
        LOG_ERR("Failed to disable accelerometer sensor");
    }

    return 0;
}

int acc_sensor_get_data(const struct i2c_dt_spec *i2c, struct acc_sample *acc_data, uint8_t *sample_count)
{
    int err;

    // Check the FIFO state
    uint8_t fifo_samples;
    err = i2c_burst_read_dt(i2c, LIS2DTW12_FIFO_SAMPLES, &fifo_samples, 1);
    if (err)
    {
        LOG_ERR("Failed to read FIFO samples");
    }

    uint8_t fifo_data_count;
    // Check for overflow
    if (fifo_samples & 0x40)
    {
        LOG_WRN("FIFO overflow detected, discarding data");
        fifo_data_count = CONFIG_ACC_SAMPLES_PER_FRAME;
    }
    else
    {
        fifo_data_count = fifo_samples & 0x3F;
        LOG_DBG("FIFO data count: %d", fifo_data_count);
    }

    // Read the FIFO data
    uint8_t byte_count = fifo_data_count * 3 * 2; // 2 bytes per axis, 3 axes
    uint8_t fifo_data[byte_count];
    err = i2c_burst_read_dt(i2c, LIS2DTW12_OUT_X_L, fifo_data, byte_count);
    if (err)
    {
        LOG_ERR("Failed to read FIFO data");
    }

    *sample_count = fifo_data_count;
    // Parse the data
    for (int i = 0; i < *sample_count; i++)
    {
        // Combine bytes into 16-bit words in 2's complement
        acc_data[i].x = ((fifo_data[i * 6 + 1] << 8) | fifo_data[i * 6]);
        acc_data[i].y = ((fifo_data[i * 6 + 3] << 8) | fifo_data[i * 6 + 2]);
        acc_data[i].z = ((fifo_data[i * 6 + 5] << 8) | fifo_data[i * 6 + 4]);

        LOG_DBG("ACC data: x = %d, y = %d, z = %d", acc_data[i].x, acc_data[i].y, acc_data[i].z);
    }

    return 0;
}