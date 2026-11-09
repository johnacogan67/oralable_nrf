/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>

#include "tgm_service.h"
#include "ppg.h"
#include "acc.h"
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
static bool ppg_running;
static bool ppg_notify_poll;
static int64_t ppg_poll_log_ms;
static const char *ppg_keep_logged;

/* Chips are programmed at 50 sps. One tick is one sample period. */
#define SENSOR_SAMPLE_PERIOD_MS 20

static void sensor_sample_work_handler(struct k_work *work);
static K_WORK_DEFINE(sensor_sample_work, sensor_sample_work_handler);

static void sensor_sample_timer_handler(struct k_timer *timer)
{
	ARG_UNUSED(timer);
	/* ISR context: queue the read. Do not touch I2C here. */
	(void)k_work_submit(&sensor_sample_work);
}

K_TIMER_DEFINE(sensor_sample_timer, sensor_sample_timer_handler, NULL);
static bool sample_loop_on;

void sensor_sample_loop_update(void)
{
	const bool want = ppg_notify_poll || acc_sample_loop_enabled();

	if (want == sample_loop_on)
	{
		return;
	}

	sample_loop_on = want;
	if (want)
	{
		k_timer_start(&sensor_sample_timer, K_MSEC(SENSOR_SAMPLE_PERIOD_MS),
			      K_MSEC(SENSOR_SAMPLE_PERIOD_MS));
	}
	else
	{
		k_timer_stop(&sensor_sample_timer);
	}
}

static void ppg_poll_apply(bool enabled, bool log_edge)
{
	if (enabled && !ppg_notify_poll)
	{
		ppg_poll_log_ms = 0;
		if (log_edge)
		{
			tgm_service_fw_log_printf("fw: ppg poll on");
		}
	}
	else if (!enabled && ppg_notify_poll && log_edge)
	{
		tgm_service_fw_log_printf("fw: ppg poll off");
	}

	ppg_notify_poll = enabled;
	sensor_sample_loop_update();
}

#if CONFIG_MAXM86161
#include <app/drivers/maxm86161.h>
#include <app/drivers/lis2dtw12.h>

#include <errno.h>

#define MAXM86161_NODE DT_NODELABEL(maxm86161)
BUILD_ASSERT(DT_NODE_HAS_STATUS(MAXM86161_NODE, okay));
BUILD_ASSERT(DT_REG_ADDR(MAXM86161_NODE) == 0x62);
/* I2C child: no Zephyr device instance. I2C_DT_SPEC_GET is the bus handle. */
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

static int ppg_fifo_read(struct ppg_sample *dst, uint8_t *count)
{
	if (!ppg_running)
	{
		*count = 0;
		return 0;
	}

	return ppg_sensor_get_data(&i2c, dst, count);
}

static void ppg_deliver(int err, struct ppg_sample *samples, uint8_t count)
{
	if (err)
	{
		LOG_ERR("Failed to read PPG data");
		tgm_service_note_ppg_read(err, false);
		return;
	}

	if (count == 0)
	{
		tgm_service_note_ppg_read(0, true);
		return;
	}

	err = tgm_service_send_ppg_notify(samples, count);
	if (err && err != -EACCES)
	{
		LOG_WRN("PPG notify failed (%d)", err);
	}
}

static void acc_deliver(int err, struct acc_sample *samples, uint8_t count)
{
	if (err)
	{
		LOG_ERR("Failed to read accelerometer data (%d)", err);
		return;
	}

	if (count == 0)
	{
		return;
	}

	err = tgm_service_send_acc_notify(samples, count);
	if (err && err != -EACCES)
	{
		LOG_WRN("ACC notify failed (%d)", err);
	}
}

static void ppg_read_and_notify(void)
{
	struct ppg_sample ppg_data[CONFIG_PPG_SAMPLES_PER_FRAME];
	uint8_t sample_count;

	sensor_i2c_lock();
	const int err = ppg_fifo_read(ppg_data, &sample_count);
	sensor_i2c_unlock();
	ppg_deliver(err, ppg_data, sample_count);
}

static void sensor_sample_work_handler(struct k_work *work)
{
	struct ppg_sample ppg_data[CONFIG_PPG_SAMPLES_PER_FRAME];
	struct acc_sample acc_data[CONFIG_ACC_SAMPLES_PER_FRAME];
	uint8_t ppg_n = 0;
	uint8_t acc_n = 0;
	int ppg_err = 0;
	int acc_err = 0;
	const bool read_ppg = ppg_notify_poll;
	const bool read_acc = acc_sample_loop_enabled();

	ARG_UNUSED(work);

	if (!read_ppg && !read_acc)
	{
		return;
	}

	if (read_ppg)
	{
		const int64_t now = k_uptime_get();

		if (ppg_poll_log_ms == 0 || (now - ppg_poll_log_ms) >= 4000)
		{
			ppg_poll_log_ms = now;
			tgm_service_fw_log_printf("fw: ppg poll");
		}
	}

	sensor_i2c_lock();
	if (read_ppg)
	{
		ppg_err = ppg_fifo_read(ppg_data, &ppg_n);
	}
	if (read_acc)
	{
		acc_err = acc_read_fifo(acc_data, &acc_n);
	}
	sensor_i2c_unlock();

	if (read_ppg)
	{
		ppg_deliver(ppg_err, ppg_data, ppg_n);
	}
	if (read_acc)
	{
		acc_deliver(acc_err, acc_data, acc_n);
	}

	{
		static uint8_t drain_extra;
		const bool more = (read_ppg && ppg_sensor_fifo_backlog()) ||
				  (read_acc && acc_sensor_fifo_backlog());

		if (more && drain_extra < 3u)
		{
			drain_extra++;
			(void)k_work_submit(&sensor_sample_work);
		}
		else
		{
			drain_extra = 0;
		}
	}
}

static void ppg_read_data(struct k_work *work)
{
	ARG_UNUSED(work);
	ppg_read_and_notify();
}

void ppg_poll_once(void)
{
    k_work_submit(&read_ppg_data_work);
}

void ppg_set_notify_poll(bool enabled)
{
    ppg_poll_apply(enabled, true);
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
    k_work_init(&ppg_reg_work.reg_work, ppg_reg_work_handler);

    uint8_t part_id = 0;
    sensor_i2c_lock();
    err = i2c_reg_read_byte_dt(&i2c, MAXM86161_REG_PART_ID, &part_id);
    sensor_i2c_unlock();
    LOG_INF("MAXM86161 part id 0x%02x err=%d", part_id, err);

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

int ppg_stop_streaming(const char *why)
{
	int err;

	if (tgm_service_ppg_notify_active())
	{
		if (why != ppg_keep_logged)
		{
			ppg_keep_logged = why;
			tgm_service_fw_log_printf("fw: ppg poll keep %s", why ? why : "?");
		}
		return 0;
	}

	if (ppg_notify_poll)
	{
		tgm_service_fw_log_printf("fw: ppg poll off %s", why ? why : "?");
	}
	ppg_keep_logged = NULL;
	ppg_poll_apply(false, false);

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

	return ppg_stop_streaming("status-leds");
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
    int err;

    sensor_i2c_lock();
    err = ppg_sensor_read_reg(&i2c, reg, &data);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to read PPG sensor register 0x%02X", reg);
        return err;
    }

    err = tgm_service_send_read_ppg_reg_notify(data);
    if (err && err != -EACCES)
    {
        LOG_WRN("PPG register read notify failed (%d)", err);
    }

    return 0;
}

static bool ppg_sampling_contract_write(uint8_t reg, uint8_t data)
{
    if (reg == MAXM86161_REG_PPG_CONFIG2)
    {
        return data != MAXM86161_PPG_CONFIG2_NOMINAL_50HZ;
    }

    return reg >= MAXM86161_REG_LED_SEQ_REG1 && reg <= MAXM86161_REG_LED_SEQ_REG3;
}

static int ppg_note_config2(uint8_t reg)
{
    uint8_t config2 = 0;
    const int err = ppg_sensor_read_reg(&i2c, MAXM86161_REG_PPG_CONFIG2, &config2);

    if (err)
    {
        return err;
    }

    LOG_INF("PPG config2 after reg 0x%02x is 0x%02x", reg, config2);
    tgm_service_fw_log_printf("fw: ppg cfg2=0x%02x after 0x%02x", config2, reg);
    if (ppg_running && config2 != MAXM86161_PPG_CONFIG2_NOMINAL_50HZ)
    {
        LOG_ERR("PPG config2 0x%02x while streaming", config2);
        return -EIO;
    }

    return 0;
}

int ppg_write_reg(uint8_t reg, uint8_t data)
{
    int err;
    uint8_t readback = 0;
    const bool rate_reg = reg == MAXM86161_REG_PPG_CONFIG1 || reg == MAXM86161_REG_PPG_CONFIG2 ||
                          (reg >= MAXM86161_REG_LED_SEQ_REG1 && reg <= MAXM86161_REG_LED_SEQ_REG3);

    if (ppg_running && ppg_sampling_contract_write(reg, data))
    {
        LOG_WRN("Rejected PPG register 0x%02x while streaming", reg);
        tgm_service_fw_log_printf("fw: ppg reg 0x%02x rejected streaming", reg);
        return -EPERM;
    }

    sensor_i2c_lock();
    err = ppg_sensor_write_reg(&i2c, reg, data);
    if (!err)
    {
        err = ppg_sensor_read_reg(&i2c, reg, &readback);
    }
    if (!err && rate_reg)
    {
        err = ppg_note_config2(reg);
    }
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to write PPG sensor register 0x%02X", reg);
        return err;
    }

    err = tgm_service_send_write_ppg_reg_notify(readback);
    if (err && err != -EACCES)
    {
        LOG_WRN("PPG register write notify failed (%d)", err);
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

    sensor_i2c_lock();
    err = ppg_sensor_write_reg(&i2c, (uint8_t)(MAXM86161_REG_LED1_PA + led), pa);
    sensor_i2c_unlock();
    if (err)
    {
        LOG_ERR("Failed to set PPG LED PA for LED %d", led);
    }

    return err;
}
