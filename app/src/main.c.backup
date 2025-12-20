/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>

#include <app_version.h>

#include "ble.h"
#include "ppg.h"
#include "acc.h"
#include "battery.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define GPIO_NODE DT_NODELABEL(gpio0)
#define SENS_ENABLE_PIN 8

// Device temperature thresholds, with hysteresis
#define DEVICE_WORN_TEMPERATURE_THRESHOLD 2550
#define DEVICE_NOT_WORN_TEMPERATURE_THRESHOLD 2450

enum device_state_t
{
	DEVICE_STATE_NOT_WORN_NOT_CHARGING,
	DEVICE_STATE_NOT_WORN_CHARGING,
	DEVICE_STATE_WORN,
	DEVICE_STATE_INIT,
};

static volatile bool charging = false;
static volatile bool worn = false;

static const struct device *gpio = DEVICE_DT_GET(GPIO_NODE);
static const struct device *temp_sensor = DEVICE_DT_GET(DT_NODELABEL(temp));
static const struct gpio_dt_spec chrsts_gpio = GPIO_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), chrsts_gpios, 0);

static struct gpio_callback chrsts_cb;

static struct k_work_delayable temperature_work;
static struct charging_work_t
{
	struct k_work work;
	bool charging_new_state;
} charging_work;

static enum device_state_t device_state = DEVICE_STATE_INIT;

static int update_state(bool charging_new_state, bool worn_new_state)
{
	int err;

	if ((device_state == DEVICE_STATE_INIT) && !charging && !worn)
	{
		// Initialize the green LED to a low value to indicate that the device is still working
		err = ppg_set_led_pa(PPG_LED_GREEN, 5);
		if (err)
		{
			LOG_ERR("Failed to set PPG LED PA for LED %d", PPG_LED_GREEN);
			return err;
		}

		device_state = DEVICE_STATE_NOT_WORN_NOT_CHARGING;
	}

	if (charging_new_state == charging && worn_new_state == worn)
	{
		// No change, do nothing
		return 0;
	}

	if (charging_new_state != charging)
	{
		// Charging state changed
		charging = charging_new_state;
		// Check if charging started or stopped
		if (charging)
		{
			// Enable the green LED
			err = ppg_set_led_pa(PPG_LED_GREEN, 32);
			if (err)
			{
				LOG_ERR("Failed to set PPG LED PA for LED %d", PPG_LED_GREEN);
				return err;
			}

			// Charging started
			device_state = DEVICE_STATE_NOT_WORN_CHARGING;
		}
		else
		{
			// Disable the green LED
			err = ppg_set_led_pa(PPG_LED_GREEN, 0);
			if (err)
			{
				LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_GREEN);
				return err;
			}

			if (worn)
			{
				// Charging stopped while worn	
				device_state = DEVICE_STATE_WORN;
			}
			else
			{
				// Charging stopped while not worn
				device_state = DEVICE_STATE_NOT_WORN_NOT_CHARGING;
			}
		}
	}

	if (worn_new_state != worn)
	{
		// Worn state changed
		worn = worn_new_state;
		// Check if worn started or stopped
		if (worn)
		{
			// Enable the Red and IR LEDs
			err = ppg_set_led_pa(PPG_LED_RED, 32);
			if (err)
			{
				LOG_ERR("Failed to set PPG LED PA for LED %d", PPG_LED_RED);
				return err;
			}

			err = ppg_set_led_pa(PPG_LED_IR, 128);
			if (err)
			{
				LOG_ERR("Failed to set PPG LED PA for LED %d", PPG_LED_IR);
				return err;
			}

			// Worn started
			device_state = DEVICE_STATE_WORN;
		}
		else
		{
			// Disable the Red and IR LEDs
			err = ppg_set_led_pa(PPG_LED_RED, 0);
			if (err)
			{
				LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_RED);
				return err;
			}

			err = ppg_set_led_pa(PPG_LED_IR, 0);
			if (err)
			{
				LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_IR);
				return err;
			}
			
			if (charging)
			{
				// Worn stopped while charging
				device_state = DEVICE_STATE_NOT_WORN_CHARGING;
			}
			else
			{
				// Worn stopped while not charging
				device_state = DEVICE_STATE_NOT_WORN_NOT_CHARGING;

				// Set the green LED to a low value to indicate that the device is still working
				err = ppg_set_led_pa(PPG_LED_GREEN, 5);
				if (err)
				{
					LOG_ERR("Failed to disable PPG LED PA for LED %d", PPG_LED_GREEN);
					return err;
				}
			}
		}
	}

	LOG_INF("Device state changed to %d", device_state);

	return 0;
}

static void charging_work_handler(struct k_work *work)
{
	struct charging_work_t *charging_work = CONTAINER_OF(work, struct charging_work_t, work);

	update_state(charging_work->charging_new_state, worn);
}

static void temperature_work_handler(struct k_work *work)
{
	int err;
	struct sensor_value temp_value;
	int16_t centitemp; // Temperature in centi-degrees Celsius

	err = sensor_sample_fetch(temp_sensor);
	if (err)
	{
		LOG_ERR("Failed to fetch temperature sample with error %d", err);
		k_work_reschedule(&temperature_work, K_SECONDS(CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL));
		return;
	}

	err = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (err)
	{
		LOG_ERR("Failed to get temperature value with error %d", err);
		k_work_reschedule(&temperature_work, K_SECONDS(CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL));
		return;
	}

	centitemp = temp_value.val1 * 100 + temp_value.val2 / 10000;
	LOG_INF("Temperature: %d.%02d C", centitemp / 100, centitemp % 100);

	// Check if the device is worn (temperature > 30C)
	if (centitemp > DEVICE_WORN_TEMPERATURE_THRESHOLD)
	{
		update_state(charging, true);
	}
	else if (centitemp < DEVICE_NOT_WORN_TEMPERATURE_THRESHOLD)
	{
		update_state(charging, false);
	}

	tgm_service_send_temp_notify(centitemp);

	k_work_reschedule(&temperature_work, K_SECONDS(CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL));
}

int32_t battery_voltage_read(void)
{
	return battery_get_last_measurement();
}

static void chrsts_callback(const struct device *dev, struct gpio_callback *cb, uint32_t pins)
{
	int err;
	uint8_t pin_state;
	pin_state = gpio_pin_get_dt(&chrsts_gpio);

	if (pin_state == 0)
	{
		LOG_INF("Chrsts pin is not active, device is not charging");

		charging_work.charging_new_state = false;
		k_work_submit(&charging_work.work);

		// Toggle the interrupt to notify when the charging state changes to active
		err = gpio_pin_interrupt_configure(gpio, chrsts_gpio.pin, GPIO_INT_LEVEL_ACTIVE);
		if (err)
		{
			LOG_ERR("Failed to configure chrsts pin interrupt");
		}
	}
	else
	{
		LOG_INF("Chrsts pin is active, device is charging");

		charging_work.charging_new_state = true;
		k_work_submit(&charging_work.work);

		// Toggle the interrupt to notify when the charging state changes to inactive
		err = gpio_pin_interrupt_configure(gpio, chrsts_gpio.pin, GPIO_INT_LEVEL_INACTIVE);
		if (err)
		{
			LOG_ERR("Failed to configure chrsts pin interrupt");
		}
	}
}

struct tgm_service_cb tgm_service_callbacks = {
	.bat_cb = battery_voltage_read,
};

int main(void)
{
	int err;

	printk("TGM Application %s\n", APP_VERSION_STRING);

	/* CRITICAL: Battery power latch MUST be first - before ANY returns */
	gpio_pin_configure(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 10, GPIO_OUTPUT);
	gpio_pin_set(DEVICE_DT_GET(DT_NODELABEL(gpio0)), 10, 1);
	printk("*** BATTERY POWER ENABLED ON P0.10 ***\n");

	// Initialize the gpio port
	if (!device_is_ready(gpio))
	{
		LOG_ERR("GPIO is not ready");
		return -1;
	}

	// Initialize the sens_enable pin as output
	err = gpio_pin_configure(gpio, SENS_ENABLE_PIN, GPIO_OUTPUT_HIGH);

	err = ble_init();
	if (err)
	{
		LOG_ERR("ble_init() returned %d", err);
		return err;
	}

	tgm_service_init(&tgm_service_callbacks);

	// Initialize the temperature monitoring
	k_work_init_delayable(&temperature_work, temperature_work_handler);

	ble_adv_start();

	err = battery_init(NULL);
	if (err)
	{
		LOG_ERR("battery_init() returned %d", err);
	}

	// Start the battery measurement before any of the sensors are started
	err = battery_start_measurement();
	if (err)
	{
		LOG_ERR("battery_start_measurement() returned %d", err);
	}

	// Enable the power for the sensors
	err = gpio_pin_set(gpio, SENS_ENABLE_PIN, 1);
	if (err)
	{
		LOG_ERR("Failed to enable sensor power");
		return err;
	}

	// Wait for the sensor to power up
	k_sleep(K_MSEC(100));

	err = ppg_init();
	if (err)
	{
		LOG_ERR("ppg_init() returned %d", err);
	}

	err = acc_init();
	if (err)
	{
		LOG_ERR("acc_init() returned %d", err);
	}

	// Initialize the chrsts pin as input
	err = gpio_pin_configure_dt(&chrsts_gpio, GPIO_INPUT);
	if (err)
	{
		LOG_ERR("Failed to configure chrsts pin");
		return err;
	}

	// Initialize the callback for the chrsts pin
	gpio_init_callback(&chrsts_cb, chrsts_callback, BIT(chrsts_gpio.pin));
	err = gpio_add_callback(gpio, &chrsts_cb);
	if (err)
	{
		LOG_ERR("Failed to add callback to chrsts pin");
		return err;
	}

	k_work_init(&charging_work.work, charging_work_handler);

	// Track the charging state
	err = gpio_pin_interrupt_configure(gpio, chrsts_gpio.pin, GPIO_INT_LEVEL_ACTIVE);
	if (err)
	{
		LOG_ERR("Failed to configure chrsts pin interrupt");
		return err;
	}

	// Start the sensors
	err = ppg_start();
	if (err)
	{
		LOG_ERR("Failed to start the PPG sensor with error %d", err);
	}

	err = acc_start();
	if (err)
	{
		LOG_ERR("Failed to start the accelerometer sensor with error %d", err);
	}

    // Start the temperature monitoring
    k_work_reschedule(&temperature_work, K_NO_WAIT);
    LOG_INF("Temperature monitoring started");

    LOG_INF("=== INITIALIZATION COMPLETE ===");
    
    // NEW: Power management loop
    // Sensors run autonomously via interrupts and send data via BLE
    // Main loop just needs to stay alive
    while (1) {
        // Sleep to save power - wake every 1 second to maintain BLE connection
        k_sleep(K_SECONDS(1));
    }

	return 0;
}
