/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BLE_H_
#define BLE_H_

#include <stdbool.h>

/**@file
 * @defgroup ble BLE Service
 * @{
 * @brief API for interacting with the BLE functionality
 */

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

int ble_init(void);
int ble_adv_start(void);

/** @return true when connectable advertising is active. */
bool ble_is_advertising(void);

/** Restart advertising if not connected and not already advertising. */
void ble_ensure_advertising(void);

/** @return true when a central is connected. */
bool ble_is_connected(void);

/** Drop a stuck link, stop sensors/LEDs, and restart advertising if recycle is late. */
void ble_force_recover(const char *why);

/**
 * @}
 */

#endif /* BLE_H_ */