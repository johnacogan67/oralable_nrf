#include "power_profiler.h"
#include "battery.h"

#include <zephyr/kernel.h>

#define CELL_CAPACITY_MAH 35.0f
#define PROFILE_MA_STREAM 1.875f
#define PROFILE_MA_CONNECTED 0.80f
#define PROFILE_MA_IDLE 0.20f
#define PROFILE_MINUTES_UNKNOWN 0xFFFFFFFFu

static int64_t last_ms;
static bool have_last;
static bool last_connected;
static bool last_streaming;
static bool last_on_dock;
static float mah_used;
static float budget_mah;
static bool budget_latched;

static float current_ma(bool connected, bool streaming)
{
	if (streaming) {
		return PROFILE_MA_STREAM;
	}
	if (connected) {
		return PROFILE_MA_CONNECTED;
	}
	return PROFILE_MA_IDLE;
}

static void latch_budget(int32_t voltage_mv)
{
	uint8_t chem;

	if (budget_latched || voltage_mv <= 0) {
		return;
	}

	chem = battery_voltage_to_chemistry_percent(voltage_mv);
	budget_mah = CELL_CAPACITY_MAH * ((float)chem / 100.0f);
	budget_latched = true;
}

void power_profiler_accumulate(bool connected, bool streaming, bool on_dock,
			       int32_t voltage_mv)
{
	const int64_t now = k_uptime_get();

	if (!have_last) {
		have_last = true;
		last_ms = now;
		last_connected = connected;
		last_streaming = streaming;
		last_on_dock = on_dock;
		if (!on_dock) {
			latch_budget(voltage_mv);
		}
		return;
	}

	if (now > last_ms && !last_on_dock) {
		float hours = (float)(now - last_ms) / 3600000.0f;

		if (hours > 1.0f) {
			hours = 1.0f;
		}
		mah_used += current_ma(last_connected, last_streaming) * hours;
	}

	if (on_dock && !last_on_dock) {
		mah_used = 0.0f;
		budget_mah = 0.0f;
		budget_latched = false;
	}

	if (!on_dock) {
		latch_budget(voltage_mv);
	}

	last_ms = now;
	last_connected = connected;
	last_streaming = streaming;
	last_on_dock = on_dock;
}

float power_profiler_get_total_mah(void)
{
	return mah_used;
}

uint32_t power_profiler_get_remaining_minutes(void)
{
	float left;
	float ma;
	float minutes;

	if (last_on_dock || !budget_latched) {
		return PROFILE_MINUTES_UNKNOWN;
	}

	left = budget_mah - mah_used;
	if (left <= 0.0f) {
		return 0;
	}

	ma = current_ma(last_connected, last_streaming);
	if (ma < 0.05f) {
		return PROFILE_MINUTES_UNKNOWN;
	}

	minutes = (left / ma) * 60.0f;
	if (minutes > 65534.0f) {
		return 65534u;
	}
	return (uint32_t)minutes;
}

bool power_profiler_budget_latched(void)
{
	return budget_latched;
}
