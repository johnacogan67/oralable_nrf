/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include "tgm_service.h"
#include "acc.h"
#include "ppg.h"
#include "sensor_i2c.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(acc, CONFIG_APP_LOG_LEVEL);

static struct gpio_callback acc_int_cb;
static struct k_work read_acc_data_work;
static bool acc_running;
static bool acc_notify_poll;

#ifdef CONFIG_LIS2DTW12
#include <app/drivers/lis2dtw12.h>

#define LIS2DTW12_NODE DT_NODELABEL(lis2dtw12)
BUILD_ASSERT(DT_NODE_HAS_STATUS(LIS2DTW12_NODE, okay));
BUILD_ASSERT(DT_REG_ADDR(LIS2DTW12_NODE) == 0x19);
/* I2C child: no Zephyr device instance. I2C_DT_SPEC_GET is the bus handle. */
static const struct gpio_dt_spec acc_int = GPIO_DT_SPEC_GET(LIS2DTW12_NODE, int_gpios);

static struct i2c_dt_spec i2c = I2C_DT_SPEC_GET(LIS2DTW12_NODE);
#else
// Give a build error
#error "No valid accelerometer sensor driver enabled"
#endif

/**
 * @brief Callback function for the accelerometer sensor interrupt
 *
 * @param dev Pointer to the gpio port that triggered the callback
 * @param cb Pointer to the callback data that was used to set up the callback
 * @param pins Bitmask of pins that triggered the callback
 * @return int
 */
static void acc_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    if (pins & BIT(acc_int.pin))
    {
        // Read the accelerometer data outside of the ISR
        k_work_submit(&read_acc_data_work);
    }

    return;
}

int acc_read_fifo(struct acc_sample *dst, uint8_t *count)
{
    if (!acc_running)
    {
        *count = 0;
        return 0;
    }

    return acc_sensor_get_data(&i2c, dst, count);
}

static void acc_read_and_notify(void)
{
    struct acc_sample acc_data[CONFIG_ACC_SAMPLES_PER_FRAME];
    uint8_t sample_count;

    sensor_i2c_lock();
    int err = acc_read_fifo(acc_data, &sample_count);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to read accelerometer data (%d)", err);
        return;
    }

    if (sample_count == 0)
    {
        (void)tgm_service_send_acc_notify(acc_data, 0);
        return;
    }

    err = tgm_service_send_acc_notify(acc_data, sample_count);
    if (err && err != -EACCES)
    {
        LOG_WRN("ACC notify failed (%d)", err);
    }

    {
        static uint8_t drain_extra;

        if (acc_sensor_fifo_backlog() && drain_extra < 3u)
        {
            drain_extra++;
            (void)k_work_submit(&read_acc_data_work);
        }
        else
        {
            drain_extra = 0;
        }
    }
}

static void acc_read_data(struct k_work *work)
{
    ARG_UNUSED(work);
    acc_read_and_notify();
}

void acc_poll_once(void)
{
    k_work_submit(&read_acc_data_work);
}

bool acc_sample_loop_enabled(void)
{
    return acc_notify_poll;
}

void acc_set_notify_poll(bool enabled)
{
    acc_notify_poll = enabled;
    sensor_sample_loop_update();
}

int acc_init(void)
{
    int err;

    // Initialize the I2C bus
    if (!i2c_is_ready_dt(&i2c))
    {
        LOG_ERR("I2C device not ready during initialization of accelerometer sensor");
        return -ENODEV;
    }

    // Initialize the GPIO port
    if (!gpio_is_ready_dt(&acc_int))
    {
        LOG_ERR("GPIO device not ready during initialization of accelerometer sensor");
        return -ENODEV;
    }

    // Intialize the int pin as input
    err = gpio_pin_configure_dt(&acc_int, GPIO_INPUT);
    if (err)
    {
        LOG_ERR("Failed to configure accelerometer sensor int pin as input");
        return err;
    }

    // Initialize the interrupt callback
    gpio_init_callback(&acc_int_cb, acc_int_handler, BIT(acc_int.pin));
    err = gpio_add_callback(acc_int.port, &acc_int_cb);
    if (err)
    {
        LOG_ERR("Failed to add callback to accelerometer sensor int pin");
        return err;
    }

    // Initialize the work item
    k_work_init(&read_acc_data_work, acc_read_data);

    /* LIS2DTW12 WHO_AM_I is 0x0F. A live part returns 0x44. */
    uint8_t who = 0;
    sensor_i2c_lock();
    err = i2c_reg_read_byte_dt(&i2c, 0x0F, &who);
    sensor_i2c_unlock();
    LOG_INF("LIS2DTW12 WHO_AM_I 0x%02x err=%d", who, err);

    return 0;
}

int acc_start(void)
{
    if (acc_running)
    {
        return 0;
    }

    sensor_i2c_lock();
    int err = acc_sensor_start(&i2c);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to start accelerometer sensor");
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&acc_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (err)
    {
        LOG_ERR("Failed to configure accelerometer sensor int pin interrupt");
        sensor_i2c_lock();
        (void)acc_sensor_stop(&i2c);
        sensor_i2c_unlock();
        return err;
    }

    acc_running = true;
    return 0;
}

int acc_stop(void)
{
    acc_set_notify_poll(false);

    if (!acc_running)
    {
        return 0;
    }

    int err = gpio_pin_interrupt_configure_dt(&acc_int, GPIO_INT_DISABLE);
    if (err)
    {
        LOG_ERR("Failed to disable accelerometer sensor int pin interrupt");
        return err;
    }

    sensor_i2c_lock();
    err = acc_sensor_stop(&i2c);
    sensor_i2c_unlock();
    /* Clear even if I2C stop failed so acc_start() will re-init. */
    acc_running = false;
    if (err)
    {
        LOG_ERR("Failed to stop accelerometer sensor");
        return err;
    }

    return 0;
}
