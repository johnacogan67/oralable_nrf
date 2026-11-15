/*
 * Minutes model for the LP270829 (35 mAh). There is no current shunt.
 * Currents are the old design figures, not a bench meter:
 *   stream  1.875 mA
 *   linked  0.80 mA   (BLE up, PPG notify off)
 *   idle    0.20 mA   (advertising, off the case)
 * Discharge is integrated only while off the Oralable case.
 * Sitting on the case clears the session and the next undock takes a fresh
 * budget from the chemistry voltage (3.0 V = 0%, 4.35 V = 100%) times 35 mAh.
 */

#ifndef POWER_PROFILER_H_
#define POWER_PROFILER_H_

#include <stdbool.h>
#include <stdint.h>

/** Close the last interval at the previous current, then store the new state. */
void power_profiler_accumulate(bool connected, bool streaming, bool on_dock,
			       int32_t voltage_mv);

/** Milliamp-hours used since the clip last left the case. */
float power_profiler_get_total_mah(void);

/**
 * Minutes left if the clip stays in the current state.
 * 0xFFFFFFFF while on the case, or before a budget is latched.
 */
uint32_t power_profiler_get_remaining_minutes(void);

bool power_profiler_budget_latched(void);

#endif /* POWER_PROFILER_H_ */
