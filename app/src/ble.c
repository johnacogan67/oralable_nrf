/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>

#include "ble.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble, CONFIG_APP_LOG_LEVEL);

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)), // No classic Bluetooth is supported, generally connectable
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN)          // Include complete local name in advertising
};

static const struct bt_data sd[] = {}; // Can hold data for a scan response if applicable

static struct k_work adv_work;

void mtu_exchange_cb(
    struct bt_conn *conn, uint8_t att_err,
    struct bt_gatt_exchange_params *params)
{
    if (att_err)
    {
        LOG_ERR("MTU exchange returned with error code %d", att_err);
    }
    else
    {
        LOG_INF("MTU sucessfully set to %d", CONFIG_BT_L2CAP_TX_MTU);
    }
}

static void request_mtu_exchange(struct bt_conn *conn)
{
    int err;
    static struct bt_gatt_exchange_params exchange_params;
    exchange_params.func = mtu_exchange_cb;

    err = bt_gatt_exchange_mtu(conn, &exchange_params);
    if (err)
    {
        LOG_ERR("MTU exchange failed (err %d)", err);
    }
    else
    {
        LOG_INF("MTU exchange pending ...");
    }
}

static void request_data_len_update(struct bt_conn *conn)
{
    int err;

    err = bt_conn_le_data_len_update(conn, BT_LE_DATA_LEN_PARAM_MAX);
    if (err)
    {
        LOG_ERR("Data length update request failed: %d", err);
        return;
    }

    LOG_INF("Data length updated to %d", CONFIG_BT_CTLR_DATA_LENGTH_MAX);
    request_mtu_exchange(conn);
}

void on_connected(struct bt_conn *conn, uint8_t err)
{
    if (err)
    {
        LOG_ERR("Connection error %d", err);
        return;
    }

    char addr[BT_ADDR_LE_STR_LEN];
    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    LOG_INF("Connected to device with address %s", addr);

    struct bt_conn_info info;
    err = bt_conn_get_info(conn, &info);
    if (err)
    {
        LOG_ERR("bt_conn_get_info() returned %d", err);
        return;
    }

    double connection_interval = info.le.interval * 1.25; // in ms
    uint16_t supervision_timeout = info.le.timeout * 10;  // in ms
    LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, info.le.interval, supervision_timeout);

    request_data_len_update(conn);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    LOG_INF("Disconnected, reason %d", reason);

    // Restart advertising
    k_work_submit(&adv_work);
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    double connection_interval = interval * 1.25; // in ms
    uint16_t supervision_timeout = timeout * 10;  // in ms
    LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms", connection_interval, latency, supervision_timeout);
}

struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .le_param_updated = on_le_param_updated,
};

static void advertising_process(struct k_work *work)
{
    int err;

    struct bt_le_adv_param adv_param = *BT_LE_ADV_PARAM(
        (
            BT_LE_ADV_OPT_CONNECTABLE |
            BT_LE_ADV_OPT_ONE_TIME |
            BT_LE_ADV_OPT_USE_IDENTITY),
        800, // Advertising interval set to about 500ms
        801,
        NULL);

    LOG_INF("Starting advertising with default parameters");
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));

    if (err)
    {
        LOG_ERR("Advertising failed to start, err: %d\n", err);
        return;
    }
    LOG_INF("Advertising started\n");
}

/**
 * @brief Initialize the BLE functionality
 *
 * Register the callbacks and enable the BLE stack
 *
 * @return int Error code
 */
int ble_init(void)
{
    // Register connection callbacks
    bt_conn_cb_register(&connection_callbacks);

    // Initialize advertising work
    k_work_init(&adv_work, advertising_process);

    int err = bt_enable(NULL);
    if (err)
    {
        LOG_ERR("Bluetooth init failed (err %d)\n", err);
        return err;
    }
    LOG_INF("Bluetooth initialized\n");

    return 0;
}

/**
 * @brief Start advertising
 *
 * Start advertising with preset parameters
 *
 * @return int Error code
 */
int ble_adv_start(void)
{
    k_work_submit(&adv_work);
    return 0;
}