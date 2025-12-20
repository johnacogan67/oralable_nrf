/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef BLE_H_
#define BLE_H_

/**@file
 * @defgroup ble BLE Service
 * @{
 * @brief API for interacting with the BLE functionality
 */

#define DEVICE_NAME CONFIG_BT_DEVICE_NAME
#define DEVICE_NAME_LEN (sizeof(DEVICE_NAME) - 1)

int ble_init(void);
int ble_adv_start(void);

/**
 * @}
 */

#endif /* BLE_H_ */