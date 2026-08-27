/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Off-body battery status via PPG LEDs. Status indication is green-only.
 * Never drive red for charge / idle (PPG red/IR is sensing only, while worn).
 */

#ifndef BATTERY_LED_INDICATOR_H_
#define BATTERY_LED_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

/** Battery % above which a full-looking SOC is clamped on-pad (legacy). */
#define BATTERY_LED_FULL_THRESHOLD_PCT 80
/** On-pad: solid green at/above this SOC even if STAT still blinks. */
#define BATTERY_LED_HOLD_GREEN_PCT 70
/** On-pad: solid green at/above this voltage even if STAT still blinks. */
#define BATTERY_LED_HOLD_GREEN_MV 4050

int battery_led_indicator_init(void);

/**
 * Apply battery/charge indicator LEDs when the device is NOT worn.
 * No-op when @p worn is true (PPG sensing owns red/IR).
 *
 * Status LEDs never use red.
 *
 * On dock (no BLE):
 *   - Flash green only while charging with SOC/voltage still below hold
 *   - Solid green on STAT taper / hold, or when SOC/voltage is already high
 * Off dock: status LEDs off.
 * Caller must not invoke this while BLE is connected (status LEDs off).
 */
void battery_led_indicator_apply(bool on_dock, bool worn, uint8_t battery_pct,
				 int32_t battery_mv, bool charge_active);

/** Stop battery-indicator flash work and turn off status LEDs (connect probe). */
void battery_led_indicator_suspend(void);

#endif /* BATTERY_LED_INDICATOR_H_ */
