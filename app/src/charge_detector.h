/*
 * Copyright (c) 2024 WeeGee bv
 *
 * Dock/charge inference from battery voltage trend when chrsts GPIO is wrong
 * or absent. GPIO on-dock always wins when it reads active.
 */

#ifndef CHARGE_DETECTOR_H_
#define CHARGE_DETECTOR_H_

#include <stdbool.h>
#include <stdint.h>

/** Reset history and inferred dock (e.g. leaving dock). */
void charge_detector_reset(void);

/**
 * Feed a battery sample and whether chrsts GPIO reads on-dock.
 * @return true when cell voltage is rising while docked and not yet at Vmax.
 */
bool charge_detector_update(int32_t battery_mv, bool gpio_on_dock);

/** Always false — dock is chrsts GPIO only (voltage inference removed in 1.0.56). */
bool charge_detector_infers_on_dock(void);

/** Last result from charge_detector_update(). */
bool charge_detector_is_active(void);

#endif /* CHARGE_DETECTOR_H_ */
