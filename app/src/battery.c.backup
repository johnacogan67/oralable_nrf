/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>

#include "battery.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(battery, CONFIG_APP_LOG_LEVEL);

#define VOLTAGE_DIVIDER_SCALE 11

static const struct adc_dt_spec adc_chan0 = ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);
static const struct gpio_dt_spec battery_enable = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), baten_gpios, 0);

uint16_t adc_buf;
static struct adc_sequence sequence = {
    .buffer = &adc_buf,
    .buffer_size = sizeof(adc_buf),
};

static int32_t battery_value;

static struct k_work_delayable battery_measurement_work;

static battery_data_ready_t app_data_ready;

static void take_battery_measurement(struct k_work *work)
{
    int err;

    // Enable the battery voltage divider
    err = gpio_pin_set_dt(&battery_enable, 1);
    if (err)
    {
        LOG_ERR("Failed to enable battery voltage divider, err %d", err);
        // Try again later
        k_work_reschedule(&battery_measurement_work, K_SECONDS(1));
        return;
    }

    // Wait for the voltage to stabilize
    k_sleep(K_MSEC(1));

    // Read the battery voltage
    err = adc_read(adc_chan0.dev, &sequence);
    if (err)
    {
        LOG_ERR("ADC read failed, err %d", err);
    }
    else
    {
        battery_value = (int32_t)adc_buf;
        LOG_DBG("ADC read success, value: %d", battery_value);

        err = adc_raw_to_millivolts_dt(&adc_chan0, &battery_value);
        // Scale the value back to the actual battery voltage
        battery_value *= VOLTAGE_DIVIDER_SCALE;
        if (err)
        {
            LOG_ERR("ADC raw to millivolts failed, err %d", err);
        }
        else
        {
            LOG_INF("Battery voltage: %dmV", battery_value);
            tgm_service_send_battery_notify(battery_value);
            if (app_data_ready)
            {
                app_data_ready(battery_value);
            }
        }
    }

    // Disable the battery voltage divider
    err = gpio_pin_set_dt(&battery_enable, 0);
    if (err)
    {
        LOG_ERR("Failed to disable battery voltage divider, err %d", err);
    }

    // Schedule the next measurement
    k_work_reschedule(&battery_measurement_work, K_SECONDS(CONFIG_BATTERY_MEASUREMENT_INTERVAL));
    return;
}

int battery_init(battery_data_ready_t battery_data_ready_cb)
{
    if (!adc_is_ready_dt(&adc_chan0))
    {
        LOG_ERR("ADC device not ready");
        return -ENODEV;
    }

    int err = adc_channel_setup_dt(&adc_chan0);
    if (err)
    {
        LOG_ERR("ADC channel setup failed, err %d", err);
        return err;
    }

    err = adc_sequence_init_dt(&adc_chan0, &sequence);
    if (err)
    {
        LOG_ERR("ADC sequence init failed, err %d", err);
        return err;
    }

    app_data_ready = battery_data_ready_cb;
    k_work_init_delayable(&battery_measurement_work, take_battery_measurement);

    // Configure battery enable pin as output and set it high
    err = gpio_pin_configure_dt(&battery_enable, GPIO_OUTPUT);
    if (err)
    {
        LOG_ERR("GPIO pin configure failed, err %d", err);
        return err;
    }
    return err;
}

int battery_start_measurement()
{
    // Take an immediate measurement
    int err = k_work_reschedule(&battery_measurement_work, K_NO_WAIT);
    if (err < 0)
    {
        return err;
    }
    else
    {
        return 0;
    }
}

int32_t battery_get_last_measurement()
{
    return battery_value;
}