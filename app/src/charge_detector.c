/*
 * Copyright (c) 2024 WeeGee bv
 */

#include "charge_detector.h"

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(charge_det, CONFIG_APP_LOG_LEVEL);

/** CG-320B at Vmax — above this, cell is full (trickle only). */
#define CHARGE_FULL_MV 4320

/** Minimum rise across history window to declare active charge. */
#define CHARGE_MIN_RISE_MV 12

#define CHARGE_HISTORY_LEN 4

static int32_t voltage_history[CHARGE_HISTORY_LEN];
static uint8_t voltage_history_count;
static bool charge_active;
static bool last_gpio_on_dock;

static void append_voltage(int32_t battery_mv)
{
	if (voltage_history_count < CHARGE_HISTORY_LEN)
	{
		voltage_history[voltage_history_count++] = battery_mv;
	}
	else
	{
		for (int i = 1; i < CHARGE_HISTORY_LEN; i++)
		{
			voltage_history[i - 1] = voltage_history[i];
		}

		voltage_history[CHARGE_HISTORY_LEN - 1] = battery_mv;
	}
}

void charge_detector_reset(void)
{
	voltage_history_count = 0;
	charge_active = false;
	last_gpio_on_dock = false;
}

bool charge_detector_infers_on_dock(void)
{
	return false;
}

bool charge_detector_update(int32_t battery_mv, bool gpio_on_dock)
{
	if (gpio_on_dock && !last_gpio_on_dock)
	{
		voltage_history_count = 0;
		charge_active = true;
	}

	last_gpio_on_dock = gpio_on_dock;
	append_voltage(battery_mv);

	if (!gpio_on_dock)
	{
		charge_active = false;
		return false;
	}

	if (battery_mv >= CHARGE_FULL_MV)
	{
		charge_active = false;
		LOG_DBG("Charge inactive: at Vmax (%dmV)", battery_mv);
		return false;
	}

	if (voltage_history_count < 2)
	{
		return charge_active;
	}

	const int32_t oldest = voltage_history[0];
	const int32_t rise_mv = battery_mv - oldest;

	if (rise_mv >= CHARGE_MIN_RISE_MV)
	{
		charge_active = true;
	}
	else if (rise_mv <= -CHARGE_MIN_RISE_MV)
	{
		charge_active = false;
	}

	LOG_DBG("Charge detect: mv=%d rise=%dmV gpio=%d active=%d",
		battery_mv, rise_mv, gpio_on_dock, charge_active);

	return charge_active;
}

bool charge_detector_is_active(void)
{
	return charge_active;
}
