/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>

#include "tgm_service.h"
#include "ppg.h"
#include "sensor_i2c.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ppg, CONFIG_APP_LOG_LEVEL);

static struct gpio_callback ppg_int_cb;
static struct k_work read_ppg_data_work;

struct ppg_reg_work_t
{
    struct k_work reg_work;
    uint8_t reg;
    uint8_t data;
    bool read;
};

static void ppg_reg_work_handler(struct k_work *work);

static struct ppg_reg_work_t ppg_reg_work;
static struct k_work_delayable ppg_poll_work;
static bool ppg_running;
static bool ppg_notify_poll;

#if CONFIG_MAXM86161
#include <app/drivers/maxm86161.h>

#define MAXM86161_NODE DT_NODELABEL(maxm86161)
/* int-gpios carries GPIO_ACTIVE_LOW|GPIO_PULL_UP from DT (open-drain FIFO IRQ). */
static const struct gpio_dt_spec ppg_int =
	GPIO_DT_SPEC_GET(MAXM86161_NODE, int_gpios);

static struct i2c_dt_spec i2c = I2C_DT_SPEC_GET(MAXM86161_NODE);
#else
// Give a build error
#error "No valid PPG sensor driver enabled"
#endif

/**
 * @brief Callback function for the PPG sensor interrupt
 *
 * @param dev Pointer to the gpio port that triggered the callback
 * @param cb Pointer to the callback data that was used to set up the callback
 * @param pins Bitmask of pins that triggered the callback
 * @return int
 */
static void ppg_int_handler(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    if (pins & BIT(ppg_int.pin))
    {
        // Read the PPG data outside of the ISR
        k_work_submit(&read_ppg_data_work);
    }

    return;
}

static void ppg_read_and_notify(void)
{
    struct ppg_sample ppg_data[CONFIG_PPG_SAMPLES_PER_FRAME];
    uint8_t sample_count;

    sensor_i2c_lock();
    int err = ppg_sensor_get_data(&i2c, ppg_data, &sample_count);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to read PPG data");
        return;
    }

    if (sample_count == 0)
    {
        return;
    }

    err = tgm_service_send_ppg_notify(ppg_data, sample_count);
    if (err && err != -EACCES)
    {
        LOG_WRN("PPG notify failed (%d)", err);
    }
}

static void ppg_read_data(struct k_work *work)
{
    ARG_UNUSED(work);
    ppg_read_and_notify();
}

static void ppg_poll_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!ppg_notify_poll)
    {
        return;
    }

    ppg_read_and_notify();
    k_work_reschedule(&ppg_poll_work, K_MSEC(500));
}

void ppg_poll_once(void)
{
    k_work_submit(&read_ppg_data_work);
}

void ppg_set_notify_poll(bool enabled)
{
    ppg_notify_poll = enabled;
    if (enabled)
    {
        k_work_reschedule(&ppg_poll_work, K_MSEC(500));
    }
    else
    {
        k_work_cancel_delayable(&ppg_poll_work);
    }
}

int ppg_init(void)
{
    // Initialize the I2C bus
    if (!i2c_is_ready_dt(&i2c))
    {
        LOG_ERR("I2C device not ready during initialization of PPG sensor");
        return -ENODEV;
    }

    if (!gpio_is_ready_dt(&ppg_int))
    {
        LOG_ERR("GPIO device not ready during initialization of PPG sensor");
        return -ENODEV;
    }

    /* GPIO_INPUT | DT flags (ACTIVE_LOW + PULL_UP) so EDGE_TO_ACTIVE is falling. */
    int err = gpio_pin_configure_dt(&ppg_int, GPIO_INPUT);
    if (err)
    {
        LOG_ERR("Failed to configure PPG sensor int pin as input");
        return err;
    }

    // Initialize the interrupt callback
    gpio_init_callback(&ppg_int_cb, ppg_int_handler, BIT(ppg_int.pin));
    err = gpio_add_callback(ppg_int.port, &ppg_int_cb);
    if (err)
    {
        LOG_ERR("Failed to add callback to PPG sensor int pin");
        return err;
    }

    // Initialize the work item
    k_work_init(&read_ppg_data_work, ppg_read_data);
    k_work_init_delayable(&ppg_poll_work, ppg_poll_work_handler);
    k_work_init(&ppg_reg_work.reg_work, ppg_reg_work_handler);

    return 0;
}

int ppg_start(void)
{
	int err;

	if (ppg_running)
	{
		err = gpio_pin_interrupt_configure_dt(&ppg_int, GPIO_INT_EDGE_TO_ACTIVE);
		if (err)
		{
			LOG_ERR("Failed to re-enable PPG sensor int interrupt");
		}

		return err;
	}

	sensor_i2c_lock();
	err = ppg_sensor_start(&i2c);
	sensor_i2c_unlock();
	if (err)
	{
		LOG_ERR("Failed to start PPG sensor");
		return err;
	}

	err = gpio_pin_interrupt_configure_dt(&ppg_int, GPIO_INT_EDGE_TO_ACTIVE);
	if (err)
	{
		LOG_ERR("Failed to configure PPG sensor int pin interrupt");
		sensor_i2c_lock();
		(void)ppg_sensor_stop(&i2c);
		sensor_i2c_unlock();
		return err;
	}

	ppg_running = true;
	return 0;
}

int ppg_stop_streaming(void)
{
	int err;

	ppg_set_notify_poll(false);

	if (!ppg_running)
	{
		return 0;
	}

	err = gpio_pin_interrupt_configure_dt(&ppg_int, GPIO_INT_DISABLE);
	if (err)
	{
		LOG_ERR("Failed to disable PPG sensor int pin interrupt");
		return err;
	}

	return 0;
}

int ppg_ensure_awake(void)
{
	int err;

	if (!ppg_running)
	{
		return ppg_start();
	}

	sensor_i2c_lock();
	err = ppg_sensor_ensure_awake(&i2c);
	sensor_i2c_unlock();
	if (err)
	{
		LOG_WRN("PPG ensure_awake failed (%d), restarting sensor", err);
		ppg_running = false;
		return ppg_start();
	}

	return 0;
}

int ppg_ensure_awake_status_leds(void)
{
	int err = ppg_ensure_awake();

	if (err)
	{
		return err;
	}

	return ppg_stop_streaming();
}

int ppg_stop(void)
{
    if (!ppg_running)
    {
        return 0;
    }

    int err = gpio_pin_interrupt_configure_dt(&ppg_int, GPIO_INT_DISABLE);
    if (err)
    {
        LOG_ERR("Failed to disable PPG sensor int pin interrupt");
        return err;
    }

    sensor_i2c_lock();
    err = ppg_sensor_stop(&i2c);
    sensor_i2c_unlock();
    /* Clear even if I2C stop failed so ppg_start() will re-init. */
    ppg_running = false;
    if (err)
    {
        LOG_ERR("Failed to stop PPG sensor");
        return err;
    }

    return 0;
}

int ppg_read_reg(uint8_t reg)
{
    uint8_t data;
    sensor_i2c_lock();
    int err = ppg_sensor_read_reg(&i2c, reg, &data);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to read PPG sensor register 0x%02X", reg);
        return err;
    }

    return 0;
}

int ppg_write_reg(uint8_t reg, uint8_t data)
{
    sensor_i2c_lock();
    int err = ppg_sensor_write_reg(&i2c, reg, data);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to write PPG sensor register 0x%02X", reg);
        return err;
    }

    return 0;
}

static void ppg_reg_work_handler(struct k_work *work)
{
    int err;
    struct ppg_reg_work_t *ppg_reg_work = CONTAINER_OF(work, struct ppg_reg_work_t, reg_work);

    uint8_t final_reg_data;
    sensor_i2c_lock();
    if (ppg_reg_work->read)
    {
        err = ppg_sensor_read_reg(&i2c, ppg_reg_work->reg, &final_reg_data);
        sensor_i2c_unlock();
        if (err)
        {
            LOG_ERR("Failed to read PPG sensor register 0x%02X", ppg_reg_work->reg);
            return;
        }

        err = tgm_service_send_read_ppg_reg_notify(final_reg_data);
    }
    else
    {
        err = ppg_sensor_write_reg(&i2c, ppg_reg_work->reg, ppg_reg_work->data);
        if (err)
        {
            sensor_i2c_unlock();
            LOG_ERR("Failed to write PPG sensor register 0x%02X", ppg_reg_work->reg);
            return;
        }
        err = ppg_sensor_read_reg(&i2c, ppg_reg_work->reg, &final_reg_data);
        sensor_i2c_unlock();
        if (err)
        {
            LOG_ERR("Failed to read PPG sensor register 0x%02X after writing", ppg_reg_work->reg);
        }

        err = tgm_service_send_write_ppg_reg_notify(final_reg_data);
    }

    if (err)
    {
        LOG_ERR("Failed to send PPG register value notification");
        return;
    }

    return;
}

int ppg_set_led_pa(enum ppg_led_t led, uint8_t pa)
{
    int err;

    err = ppg_write_reg(MAXM86161_REG_LED1_PA + led, pa);
    if (err)
    {
        LOG_ERR("Failed to set PPG LED PA for LED %d", led);
    }

    return 0;
}
