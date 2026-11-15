/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Off-body battery status via the optical LEDs.
 * Off the case: dark.
 * On the case, cell in hold: short green flash.
 * On the case, cell voltage rising and under hold: green flash.
 * On the case, pin present, under hold, voltage not up after 15 min: red on.
 * Infrared stays off. A live pulse stream owns the LEDs.
 */

#ifndef BATTERY_LED_INDICATOR_H_
#define BATTERY_LED_INDICATOR_H_

#include <stdbool.h>
#include <stdint.h>

/** Battery % above which a full-looking SOC is clamped on-pad (legacy). */
#define BATTERY_LED_FULL_THRESHOLD_PCT 80
/* LP270829 hold lines. This cell has read 4202–4224 mV when the case has filled it. */
/** Enter the slow green flash at 100% on the 3.30–4.20 V gauge. */
#define BATTERY_LED_HOLD_GREEN_PCT 100
/** Enter the slow green flash at/above this cell voltage. */
#define BATTERY_LED_HOLD_GREEN_MV 4200
/** Leave the slow flash below this SOC. 89% is just under 4.10 V on this gauge. */
#define BATTERY_LED_HOLD_GREEN_EXIT_PCT 89
/** Leave the slow flash below this voltage. */
#define BATTERY_LED_HOLD_GREEN_EXIT_MV 4100

int battery_led_indicator_init(void);

/**
 * Apply battery/charge indicator LEDs when the device is NOT worn.
 * No-op when @p worn is true (PPG sensing owns the emitters).
 *
 * Off dock: status LEDs off.
 * On dock, under hold: green flash until 15 min of samples show the cell
 * has not risen by 12 mV. Then red stays on. A later rise returns to green.
 * On dock, in hold: short green flash. Solid green is not used.
 * Caller must not invoke this while a live PPG stream owns the emitters.
 */
void battery_led_indicator_apply(bool on_dock, bool worn, uint8_t battery_pct,
				 int32_t battery_mv, bool charge_active);

/** Stop battery-indicator flash work and turn off status LEDs (connect probe). */
void battery_led_indicator_suspend(void);

#endif /* BATTERY_LED_INDICATOR_H_ */
