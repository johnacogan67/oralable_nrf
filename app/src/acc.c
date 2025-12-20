/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include "tgm_service.h"
#include "acc.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(acc, CONFIG_APP_LOG_LEVEL);

static struct gpio_callback acc_int_cb;
static struct k_work read_acc_data_work;

#ifdef CONFIG_LIS2DTW12
#include <app/drivers/lis2dtw12.h>

#define LIS2DTW12_NODE DT_NODELABEL(lis2dtw12)
static struct gpio_dt_spec acc_int = GPIO_DT_SPEC_GET(LIS2DTW12_NODE, int_gpios);

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
    if (pins & BIT(acc_int.pin))
    {
        // Read the accelerometer data outside of the ISR
        k_work_submit(&read_acc_data_work);
    }

    return;
}

static void acc_read_data(struct k_work *work)
{
    struct acc_sample acc_data[CONFIG_ACC_SAMPLES_PER_FRAME];
    uint8_t sample_count;

    // Get the accelerometer data
    int err = acc_sensor_get_data(&i2c, acc_data, &sample_count);
    if (err)
    {
        LOG_ERR("Failed to read accelerometer data");
        return;
    }

    // Notify the client of the accelerometer data
    err = tgm_service_send_acc_notify(acc_data, sample_count);
    if (err)
    {
        LOG_DBG("Failed to send accelerometer data notification");
        return;
    }

    return;
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

    return 0;
}

int acc_start(void)
{
    // Enable the interrupt
    int err = gpio_pin_interrupt_configure_dt(&acc_int, GPIO_INT_EDGE_TO_ACTIVE);
    if (err)
    {
        LOG_ERR("Failed to configure accelerometer sensor int pin interrupt");
        return err;
    }

    // Start the accelerometer sensor
    err = acc_sensor_start(&i2c);
    if (err)
    {
        LOG_ERR("Failed to start accelerometer sensor");
        return err;
    }

    return 0;
}

int acc_stop(void)
{
    // Stop the accelerometer sensor
    int err = acc_sensor_stop(&i2c);
    if (err)
    {
        LOG_ERR("Failed to stop accelerometer sensor");
        return err;
    }

    // Disable the interrupt
    err = gpio_pin_interrupt_configure_dt(&acc_int, GPIO_INT_DISABLE);
    if (err)
    {
        LOG_ERR("Failed to disable accelerometer sensor int pin interrupt");
        return err;
    }

    return 0;
}
