/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <app/drivers/lis2dtw12.h>
#include <zephyr/drivers/i2c.h>

#include <errno.h>
#include <string.h>

#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
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

BUILD_ASSERT(LIS2DTW12_CTRL1_ODR50_LP4 == 0x43);
BUILD_ASSERT(LIS2DTW12_FIFO_MAX_BURST_SAMPLES * LIS2DTW12_FIFO_RAW_BYTES_PER_SAMPLE ==
	     LIS2DTW12_FIFO_MAX_BURST_BYTES);
BUILD_ASSERT(CONFIG_ACC_SAMPLES_PER_FRAME <= LIS2DTW12_FIFO_MAX_BURST_SAMPLES);

static bool acc_fifo_backlog;
static uint32_t acc_fifo_overflow_events;
static uint32_t acc_fifo_backlog_events;

bool acc_sensor_fifo_backlog(void)
{
    return acc_fifo_backlog;
}

uint32_t acc_sensor_fifo_overflow_events(void)
{
    return acc_fifo_overflow_events;
}

uint32_t acc_sensor_fifo_backlog_events(void)
{
    return acc_fifo_backlog_events;
}

int acc_sensor_start(const struct i2c_dt_spec *i2c)
{
    int err = 0;

    // Set the FIFO threshold and FIFO mode. 0x20|25 = 0x39 keeps stream mode.
    uint8_t fifo_ctrl = (uint8_t)(LIS2DTW12_FIFO_MODE_STREAM | CONFIG_ACC_SAMPLES_PER_FRAME);
    err = i2c_burst_write_dt(i2c, LIS2DTW12_FIFO_CTRL, &fifo_ctrl, 1);
    if (err)
    {
        LOG_ERR("Failed to set FIFO threshold");
        return err;
    }

    // Enable low-noise configuration, 2g scale
    uint8_t ctrl6 = 0b00000100;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_6, &ctrl6, 1);
    if (err)
    {
        LOG_ERR("Failed to set CTRL6");
        return err;
    }

    // Route FIFO threshold interrupt to INT1
    uint8_t ctrl4 = 0b00000010;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL4_INT1_PAD_CTRL, &ctrl4, 1);
    if (err)
    {
        LOG_ERR("Failed to route FIFO threshold interrupt to INT1");
        return err;
    }

    // Enable the interrupts
    uint8_t ctrl7 = 0b00100000;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_7, &ctrl7, 1);
    if (err)
    {
        LOG_ERR("Failed to enable the interrupts");
        return err;
    }

    // ODR 50 Hz, low-power mode 4. The approved source value is 0x43, not 0x44.
    uint8_t ctrl1 = LIS2DTW12_CTRL1_ODR50_LP4;
    uint8_t ctrl1_readback = 0;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL1, &ctrl1, 1);
    if (err)
    {
        LOG_ERR("Failed to enable accelerometer sensor");
        return err;
    }

    err = i2c_burst_read_dt(i2c, LIS2DTW12_CTRL1, &ctrl1_readback, 1);
    if (err)
    {
        LOG_ERR("Failed to read CTRL1");
        return err;
    }
    LOG_INF("LIS2DTW12 CTRL1 readback 0x%02x", ctrl1_readback);
    if (ctrl1_readback != ctrl1 ||
        (ctrl1_readback & LIS2DTW12_CTRL1_ODR_MASK) != LIS2DTW12_CTRL1_ODR_50HZ)
    {
        LOG_ERR("CTRL1 0x%02x is not ODR 50 Hz low-power mode 4", ctrl1_readback);
        return -EIO;
    }

    acc_fifo_backlog = false;
    return 0;
}

int acc_sensor_stop(const struct i2c_dt_spec *i2c)
{
    int err;
    int ret = 0;

    // Disable the interrupts
    uint8_t ctrl7 = 0;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL_7, &ctrl7, 1);
    if (err)
    {
        LOG_ERR("Failed to disable interrupts");
        ret = err;
    }

    // Put the sensor in power-down mode
    uint8_t ctrl1 = 0;
    err = i2c_burst_write_dt(i2c, LIS2DTW12_CTRL1, &ctrl1, 1);
    if (err)
    {
        LOG_ERR("Failed to disable accelerometer sensor");
        ret = err;
    }

    return ret;
}

int acc_sensor_get_data(const struct i2c_dt_spec *i2c, struct acc_sample *acc_data, uint8_t *sample_count)
{
    int err;
    uint8_t fifo_samples;
    uint8_t available;
    uint8_t take;
    bool overflow;
    uint8_t fifo_data[LIS2DTW12_FIFO_MAX_BURST_BYTES];

    *sample_count = 0;
    acc_fifo_backlog = false;

    err = i2c_burst_read_dt(i2c, LIS2DTW12_FIFO_SAMPLES, &fifo_samples, 1);
    if (err)
    {
        LOG_ERR("Failed to read FIFO samples");
        return err;
    }

    overflow = (fifo_samples & LIS2DTW12_FIFO_SAMPLES_OVR) != 0u;
    available = fifo_samples & LIS2DTW12_FIFO_SAMPLES_DIFF;
    if (overflow)
    {
        acc_fifo_overflow_events++;
        LOG_WRN("ACC FIFO overflow, unread=%u", available);
    }
    else if (available > CONFIG_ACC_SAMPLES_PER_FRAME)
    {
        acc_fifo_backlog_events++;
        LOG_DBG("ACC FIFO backlog samples=%u", available);
    }
    else
    {
        LOG_DBG("FIFO data count: %u", available);
    }

    if (available == 0u)
    {
        return 0;
    }

    take = available > CONFIG_ACC_SAMPLES_PER_FRAME ? CONFIG_ACC_SAMPLES_PER_FRAME : available;
    if (take > LIS2DTW12_FIFO_MAX_BURST_SAMPLES)
    {
        take = LIS2DTW12_FIFO_MAX_BURST_SAMPLES;
    }

    err = i2c_burst_read_dt(i2c, LIS2DTW12_OUT_X_L, fifo_data,
                            (uint32_t)take * LIS2DTW12_FIFO_RAW_BYTES_PER_SAMPLE);
    if (err)
    {
        LOG_ERR("Failed to read FIFO data");
        return err;
    }

    for (uint8_t i = 0; i < take; i++)
    {
        const uint8_t *raw = &fifo_data[i * LIS2DTW12_FIFO_RAW_BYTES_PER_SAMPLE];

        acc_data[i].x = (int16_t)((raw[1] << 8) | raw[0]);
        acc_data[i].y = (int16_t)((raw[3] << 8) | raw[2]);
        acc_data[i].z = (int16_t)((raw[5] << 8) | raw[4]);
        LOG_DBG("ACC data: x = %d, y = %d, z = %d", acc_data[i].x, acc_data[i].y, acc_data[i].z);
    }

    *sample_count = take;
    acc_fifo_backlog = available > take;
    return 0;
}
