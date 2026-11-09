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
/* Gen1 CG-320B hold lines. Gen2 (LP260820) must get its own enter/exit pair. */
/** Enter hold LED at/above this SOC (~4.10 V on the 3.61–4.20 V gauge). */
#define BATTERY_LED_HOLD_GREEN_PCT 83
/** Enter hold LED at/above this cell voltage. Rested full on this case is ~4.15 V. */
#define BATTERY_LED_HOLD_GREEN_MV 4100
/** Leave hold LED below this SOC (hysteresis). */
#define BATTERY_LED_HOLD_GREEN_EXIT_PCT 75
/** Leave hold LED below this voltage (hysteresis, ~4.05 V). */
#define BATTERY_LED_HOLD_GREEN_EXIT_MV 4050

int battery_led_indicator_init(void);

/**
 * Apply battery/charge indicator LEDs when the device is NOT worn.
 * No-op when @p worn is true (PPG sensing owns red/IR).
 *
 * Status LEDs never use red.
 *
 * On dock (no BLE):
 *   - Flash green while SOC/voltage is below hold (charging or early STAT taper)
 *   - Rare green pulse when SOC/voltage is in hold (STAT taper or top-up)
 * Off dock: status LEDs off.
 * Caller must not invoke this while BLE is connected (status LEDs off).
 */
void battery_led_indicator_apply(bool on_dock, bool worn, uint8_t battery_pct,
				 int32_t battery_mv, bool charge_active);

/** Stop battery-indicator flash work and turn off status LEDs (connect probe). */
void battery_led_indicator_suspend(void);

#endif /* BATTERY_LED_INDICATOR_H_ */
