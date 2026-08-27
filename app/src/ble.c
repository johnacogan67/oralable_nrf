/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <stdbool.h>
#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gap.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include "ble.h"
#include "tgm_service.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ble, CONFIG_APP_LOG_LEVEL);

/* TGM primary service — scan response helps nRF Connect / iOS identify Oralable without relying on name alone. */
#define ORALABLE_TGM_SVC_UUID_VAL \
    BT_UUID_128_ENCODE(0x3a0ff000, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

static const struct bt_data ad[] = {
    BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
    BT_DATA(BT_DATA_NAME_COMPLETE, DEVICE_NAME, DEVICE_NAME_LEN),
};

static const struct bt_data sd[] = {
    BT_DATA_BYTES(BT_DATA_UUID128_ALL, ORALABLE_TGM_SVC_UUID_VAL),
};

static struct k_work adv_work;
static struct k_work_delayable adv_fallback_work;
static struct bt_conn *current_conn;
static bool connected;
static bool advertising;
/** Fast connectable advertising until this uptime (ms), then slow interval to save energy. */
static int64_t fast_adv_until_ms;

#define ADV_FALLBACK_S 2

static const char *hci_disconnect_reason_str(uint8_t reason)
{
    switch (reason)
    {
    case BT_HCI_ERR_CONN_TIMEOUT:
        return "connection/supervision timeout";
    case BT_HCI_ERR_REMOTE_USER_TERM_CONN:
        return "remote user terminated";
    case BT_HCI_ERR_LOCALHOST_TERM_CONN:
        return "local host terminated";
    case 0x3d:
        return "MIC failure";
    case 0x3e:
        return "failed to establish";
    default:
        return "see Core Spec Vol 2 Part D";
    }
}

bool ble_is_connected(void)
{
    return connected;
}

bool ble_is_advertising(void)
{
    return advertising;
}

static void adv_fallback_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (connected)
    {
        return;
    }

    LOG_WRN("Adv fallback: .recycled missed — starting advertising");
    tgm_service_fw_log_printf("fw: adv fallback recycled-miss");
    k_work_submit(&adv_work);
}

void ble_ensure_advertising(void)
{
    if (connected)
    {
        /* Zombie: stack still "connected", CCC on, no notifies — red stays
         * on and advertising never restarts.
         */
        if (tgm_service_sensor_stream_stale())
        {
            ble_force_recover("connected stale stream");
        }
        return;
    }

    /* Nordic samples restart advertising only when it is not running.
     * Do not force-stop healthy advertising (avoids masking stack issues).
     */
    if (!advertising)
    {
        k_work_submit(&adv_work);
    }
}

void ble_force_recover(const char *why)
{
    LOG_WRN("BLE recover: %s connected=%d", why ? why : "?", connected);
    tgm_service_fw_log_printf("fw: ble recover %s", why ? why : "?");
    tgm_service_clear_optical_leds();

    if (current_conn)
    {
        (void)bt_conn_disconnect(current_conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }

    if (connected)
    {
        connected = false;
        advertising = false;
        tgm_service_on_disconnect();
    }

    fast_adv_until_ms = k_uptime_get() + 90000;
    (void)k_work_reschedule(&adv_fallback_work, K_SECONDS(ADV_FALLBACK_S));
}

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

static void request_conn_param_update(struct bt_conn *conn)
{
    struct bt_le_conn_param preferred = BT_LE_CONN_PARAM_INIT(
        CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
        CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
        CONFIG_BT_PERIPHERAL_PREF_LATENCY,
        CONFIG_BT_PERIPHERAL_PREF_TIMEOUT);

    int err = bt_conn_le_param_update(conn, &preferred);
    if (err)
    {
        LOG_WRN("Connection parameter update request failed: %d", err);
    }
    else
    {
        LOG_INF("Connection parameter update requested (timeout %d ms)",
                CONFIG_BT_PERIPHERAL_PREF_TIMEOUT * 10);
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
    LOG_INF("Connection parameters: interval %.2f ms, latency %d intervals, timeout %d ms",
            connection_interval, info.le.latency, supervision_timeout);

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }
    current_conn = bt_conn_ref(conn);
    connected = true;
    advertising = false;
    (void)k_work_cancel_delayable(&adv_fallback_work);
    tgm_service_on_connected();

    /* Prefer long supervision timeout before CCC storm (iOS often starts ~240–720 ms). */
    request_conn_param_update(conn);
    request_data_len_update(conn);
}

void on_disconnected(struct bt_conn *conn, uint8_t reason)
{
    ARG_UNUSED(conn);

    LOG_INF("Disconnected, HCI reason 0x%02x (%s)", reason, hci_disconnect_reason_str(reason));

    if (current_conn)
    {
        bt_conn_unref(current_conn);
        current_conn = NULL;
    }

    if (connected)
    {
        connected = false;
        advertising = false;
        tgm_service_on_disconnect();
    }

    fast_adv_until_ms = k_uptime_get() + 90000;
    /* Prefer .recycled; if it never runs, fallback starts adv. */
    (void)k_work_reschedule(&adv_fallback_work, K_SECONDS(ADV_FALLBACK_S));
}

/**
 * Connection object fully released — safe point to restart advertising
 * (Nordic Academy BLE Fundamentals Lesson 2 / extended_adv sample).
 */
static void on_recycled(void)
{
    LOG_INF("Connection recycled; restarting advertising");
    (void)k_work_cancel_delayable(&adv_fallback_work);
    k_work_submit(&adv_work);
}

void on_le_param_updated(struct bt_conn *conn, uint16_t interval, uint16_t latency, uint16_t timeout)
{
    ARG_UNUSED(conn);

    double connection_interval = interval * 1.25; // in ms
    uint16_t supervision_timeout = timeout * 10;  // in ms
    LOG_INF("Connection parameters updated: interval %.2f ms, latency %d intervals, timeout %d ms",
            connection_interval, latency, supervision_timeout);
}

struct bt_conn_cb connection_callbacks = {
    .connected = on_connected,
    .disconnected = on_disconnected,
    .le_param_updated = on_le_param_updated,
    .recycled = on_recycled,
};

static void advertising_process(struct k_work *work)
{
    int err;

    ARG_UNUSED(work);

    if (connected)
    {
        return;
    }

    if (advertising)
    {
        (void)bt_le_adv_stop();
        advertising = false;
    }

    struct bt_le_adv_param adv_param = *BT_LE_ADV_PARAM(
        (BT_LE_ADV_OPT_CONNECTABLE | BT_LE_ADV_OPT_USE_IDENTITY),
        (k_uptime_get() < fast_adv_until_ms) ? 160 : 800,
        (k_uptime_get() < fast_adv_until_ms) ? 161 : 801,
        NULL);

    LOG_INF("Starting connectable advertising (interval ~%s)",
            (k_uptime_get() < fast_adv_until_ms) ? "100 ms" : "500 ms");
    err = bt_le_adv_start(&adv_param, ad, ARRAY_SIZE(ad), sd, ARRAY_SIZE(sd));
    if (err && err != -EALREADY)
    {
        LOG_ERR("Advertising failed to start, err: %d", err);
        return;
    }

    advertising = true;
    LOG_INF("Advertising started");
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

    k_work_init(&adv_work, advertising_process);
    k_work_init_delayable(&adv_fallback_work, adv_fallback_work_handler);

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
