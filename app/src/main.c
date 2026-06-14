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
#include "battery_led_indicator.h"
#include "tgm_service.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

#define GPIO_NODE DT_NODELABEL(gpio0)
#define SENS_ENABLE_PIN 8

// Device temperature thresholds, with hysteresis
#define DEVICE_WORN_TEMPERATURE_THRESHOLD 2550
#define DEVICE_NOT_WORN_TEMPERATURE_THRESHOLD 2450

/** Dim green-only PPG during off-body connect probe (see CONFIG_TGM_CONNECT_PROBE_DURATION_S). */
#define CONNECT_PROBE_GREEN_PA 5

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
static uint16_t temp_measurement_interval_s = CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL;
static struct charging_work_t
{
	struct k_work work;
	bool charging_new_state;
} charging_work;

static enum device_state_t device_state = DEVICE_STATE_INIT;

static void apply_battery_leds(void)
{
	uint8_t battery_pct = battery_voltage_to_percent(battery_get_last_measurement());
	battery_led_indicator_apply(charging, worn, battery_pct);
}

static void publish_status(void)
{
	uint8_t battery_pct = battery_voltage_to_percent(battery_get_last_measurement());
	tgm_service_send_status_notify(charging, worn, device_state, battery_pct);
}

static int update_state(bool charging_new_state, bool worn_new_state)
{
	if (charging_new_state == charging && worn_new_state == worn)
	{
		return 0;
	}

	if (charging_new_state != charging)
	{
		charging = charging_new_state;

		if (charging)
		{
			device_state = worn ? DEVICE_STATE_WORN : DEVICE_STATE_NOT_WORN_CHARGING;
		}
		else if (worn)
		{
			device_state = DEVICE_STATE_WORN;
		}
		else
		{
			device_state = DEVICE_STATE_NOT_WORN_NOT_CHARGING;
		}
	}

	if (worn_new_state != worn)
	{
		worn = worn_new_state;

		if (worn)
		{
			int err;

			device_state = DEVICE_STATE_WORN;
			apply_battery_leds();

			err = ppg_set_led_pa(PPG_LED_GREEN, 0);
			if (err)
			{
				LOG_ERR("Failed to disable green LED for worn mode");
				return err;
			}

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
		}
		else
		{
			int err;

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

			device_state = charging ? DEVICE_STATE_NOT_WORN_CHARGING
						: DEVICE_STATE_NOT_WORN_NOT_CHARGING;
			apply_battery_leds();
		}
	}
	else if (!worn)
	{
		apply_battery_leds();
	}

	if (device_state == DEVICE_STATE_INIT)
	{
		device_state = charging ? DEVICE_STATE_NOT_WORN_CHARGING
					: DEVICE_STATE_NOT_WORN_NOT_CHARGING;
		apply_battery_leds();
	}

	LOG_INF("Device state changed to %d (charging=%d worn=%d)", device_state, charging, worn);

	tgm_service_set_device_worn(worn);
	publish_status();

	return 0;
}

static void battery_measurement_ready(int32_t battery_mv)
{
	ARG_UNUSED(battery_mv);

	if (!worn)
	{
		apply_battery_leds();
	}

	publish_status();
}

static void ppg_apply_off_body_leds(void)
{
	(void)ppg_set_led_pa(PPG_LED_RED, 0);
	(void)ppg_set_led_pa(PPG_LED_IR, 0);
	(void)ppg_set_led_pa(PPG_LED_GREEN, 0);
}

static void connect_probe_begin_cb(void)
{
	if (worn)
	{
		return;
	}

	LOG_INF("Connect probe: dim green PPG LED (PA=%d)", CONNECT_PROBE_GREEN_PA);
	(void)ppg_set_led_pa(PPG_LED_RED, 0);
	(void)ppg_set_led_pa(PPG_LED_IR, 0);
	(void)ppg_set_led_pa(PPG_LED_GREEN, CONNECT_PROBE_GREEN_PA);
}

static void connect_probe_end_cb(void)
{
	if (worn)
	{
		return;
	}

	LOG_INF("Connect probe: PPG LEDs off");
	ppg_apply_off_body_leds();
	apply_battery_leds();
}

static void diagnostic_snapshot_cb(void)
{
	publish_status();
}

static void ble_link_connected_cb(void)
{
	int err;
	struct sensor_value temp_value;
	int16_t centitemp = 0;

	err = sensor_sample_fetch(temp_sensor);
	if (!err)
	{
		err = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value);
		if (!err)
		{
			centitemp = temp_value.val1 * 100 + temp_value.val2 / 10000;
		}
	}

	LOG_INF("BLE link up: charging=%d worn=%d state=%d bat=%d%% die=%d.%02d C probe=%ds",
		charging, worn, device_state,
		battery_voltage_to_percent(battery_get_last_measurement()),
		centitemp / 100, centitemp % 100,
		CONFIG_TGM_CONNECT_PROBE_DURATION_S);

	publish_status();
}

static void ble_link_disconnected_cb(void)
{
	if (!worn)
	{
		ppg_apply_off_body_leds();
		apply_battery_leds();
	}

	LOG_INF("BLE link down: charging=%d worn=%d state=%d",
		charging, worn, device_state);
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
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
		return;
	}

	err = sensor_channel_get(temp_sensor, SENSOR_CHAN_DIE_TEMP, &temp_value);
	if (err)
	{
		LOG_ERR("Failed to get temperature value with error %d", err);
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
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

	/* Off-body: BLE-notify temp during worn or connect probe window. */
	if (worn || tgm_service_connect_probe_active())
	{
		tgm_service_send_temp_notify(centitemp);
	}

	k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
}

void temperature_set_measurement_interval_seconds(uint16_t seconds)
{
	temp_measurement_interval_s = (seconds == 0u) ? 1u : seconds;
	if (temperature_work.work.handler != NULL)
	{
		k_work_reschedule(&temperature_work, K_SECONDS(temp_measurement_interval_s));
	}
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
	.link_connected_cb = ble_link_connected_cb,
	.link_disconnected_cb = ble_link_disconnected_cb,
	.probe_begin_cb = connect_probe_begin_cb,
	.probe_end_cb = connect_probe_end_cb,
	.diagnostic_snapshot_cb = diagnostic_snapshot_cb,
};

int main(void)
{
	int err;

	printk("TGM Application %s\n", APP_VERSION_STRING);

	// Initialize the gpio port
	if (!device_is_ready(gpio))
	{
		LOG_ERR("GPIO is not ready");
		return -1;
	}

	/* CRITICAL: battery boost latch (BATEN / P0.10) must be driven HIGH
	 * atomically once GPIO is confirmed ready. If this configure step fails,
	 * the pin can float and battery-only operation may die after a delay.
	 */
	err = gpio_pin_configure(gpio, 10, GPIO_OUTPUT_HIGH);
	if (err)
	{
		LOG_ERR("Failed to latch BATEN (P0.10), err=%d", err);
		return err;
	}
	printk("*** BATTERY POWER ENABLED ON P0.10 ***\n");

	// Initialize the sens_enable pin as output
	err = gpio_pin_configure(gpio, SENS_ENABLE_PIN, GPIO_OUTPUT_HIGH);

	err = ble_init();
	if (err)
	{
		LOG_ERR("ble_init() returned %d", err);
		return err;
	}

	tgm_service_init(&tgm_service_callbacks);
	battery_led_indicator_init();

	// Initialize the temperature monitoring
	k_work_init_delayable(&temperature_work, temperature_work_handler);

	ble_adv_start();

	err = battery_init(battery_measurement_ready);
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

	charging = (gpio_pin_get_dt(&chrsts_gpio) != 0);
	LOG_INF("Initial charging state: %s", charging ? "on dock" : "off dock");

	// Track the charging state
	err = gpio_pin_interrupt_configure(gpio, chrsts_gpio.pin,
					   charging ? GPIO_INT_LEVEL_INACTIVE
						    : GPIO_INT_LEVEL_ACTIVE);
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

	device_state = charging ? DEVICE_STATE_NOT_WORN_CHARGING
				: DEVICE_STATE_NOT_WORN_NOT_CHARGING;
	apply_battery_leds();

    // Start the temperature monitoring
    k_work_reschedule(&temperature_work, K_NO_WAIT);
    LOG_INF("Temperature monitoring started");

    LOG_INF("=== INITIALIZATION COMPLETE ===");
    
    // NEW: Power management loop
    // Sensors run autonomously via interrupts and send data via BLE
    // Main loop just needs to stay alive
    while (1) {
        /* Defensive re-assert of BATEN latch for battery-only robustness. */
        (void)gpio_pin_set(gpio, 10, 1);
        // Sleep to save power - wake every 1 second to maintain BLE connection
        k_sleep(K_SECONDS(1));
    }

	return 0;
}
