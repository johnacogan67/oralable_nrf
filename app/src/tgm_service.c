/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <zephyr/kernel.h>

#include <app_version.h>
#include "tgm_service.h"
#include "battery.h"
#include "ble.h"
#include "ppg.h"
#include "acc.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tgm_service, CONFIG_APP_LOG_LEVEL);

static bool notify_ppg_data;
static bool notify_acc_data;
static bool notify_temp_data;
static bool notify_battery;
static bool notify_read_ppg_reg;
static bool notify_write_ppg_reg;
static bool notify_status;
static bool notify_fw_log;
static bool notify_fw_config_state;

static uint32_t ppg_notify_ok = 0;
static uint32_t ppg_notify_err = 0;
static int32_t ppg_last_notify_err = 0;
static uint32_t acc_notify_ok = 0;
static uint32_t acc_notify_err = 0;
static int32_t acc_last_notify_err = 0;
static uint32_t fwlog_sent = 0;
static uint32_t fwlog_err = 0;
static int32_t fwlog_last_err = 0;

static struct tgm_fw_config_state_t fw_config_state = {
    .version = 1,
    .led_green_pa = 5,
    .led_ir_pa = 128,
    .led_red_pa = 32,
    .stream_stats_period_s = 5,
    .battery_interval_s = 10,
    .temp_interval_s = 1,
    .stream_enable_mask = 0x03,
};

static uint8_t stream_stats_period_s = 5;
static struct k_work_delayable stream_stats_work;

static uint32_t ppg_frame_counter = 0;
static uint32_t acc_frame_counter = 0;

static struct tgm_service_ppg_data_t tgm_service_ppg_data;
static struct tgm_service_acc_data_t tgm_service_acc_data;
static struct tgm_service_temp_data_t tgm_service_temp_data;
static struct tgm_service_status_t device_status;
static int32_t bat_value;
static uint64_t uuid_value;
static char fw_version[15] = APP_VERSION_STRING;
static struct tgm_service_cb *tgm_service_cb = NULL;
static struct k_work battery_notify_work;
static struct k_work ppg_start_work;
static struct k_work acc_start_work;
static struct k_work_delayable link_keepalive_work;
static struct k_work_delayable connect_probe_end_work;

static bool device_worn;
static bool connect_probe_active;

#define GATT_NOTIFY_LOG_INTERVAL_MS 1000
#define GATT_TX_BACKOFF_MS          80

static int64_t gatt_tx_quiet_until_ms;

static bool any_stream_notify_enabled(void)
{
    return notify_ppg_data || notify_acc_data || notify_temp_data || notify_battery ||
           notify_read_ppg_reg || notify_write_ppg_reg || notify_status || notify_fw_log;
}

void temperature_set_measurement_interval_seconds(uint16_t seconds);

static void tgm_service_probe_begin(void);

enum fw_cfg_opcode_t {
    FW_CFG_SET_LED_PA = 0x01,
    FW_CFG_SET_STATS_PERIOD_S = 0x02,
    FW_CFG_REQUEST_CONN_PARAM_UPDATE = 0x03,
    FW_CFG_SET_BATTERY_INTERVAL_S = 0x04,
    FW_CFG_SET_TEMP_INTERVAL_S = 0x05,
    FW_CFG_SET_STREAM_ENABLE_MASK = 0x06,
    FW_CFG_REQUEST_STATUS_SNAPSHOT = 0x07,
    FW_CFG_RESTART_CONNECT_PROBE = 0x08,
};

static void stream_stats_work_handler(struct k_work *work);

static ssize_t write_fw_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);
    ARG_UNUSED(flags);

    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len < 1u)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *b = (const uint8_t *)buf;
    const uint8_t op = b[0];

    switch (op)
    {
    case FW_CFG_SET_LED_PA:
        if (len != 3u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        {
            const uint8_t led_id = b[1];
            const uint8_t pa = b[2];
            enum ppg_led_t led;

            if (led_id == 0)
            {
                led = PPG_LED_GREEN;
                fw_config_state.led_green_pa = pa;
            }
            else if (led_id == 1)
            {
                led = PPG_LED_IR;
                fw_config_state.led_ir_pa = pa;
            }
            else if (led_id == 2)
            {
                led = PPG_LED_RED;
                fw_config_state.led_red_pa = pa;
            }
            else
            {
                return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
            }

            const int err = ppg_set_led_pa(led, pa);
            tgm_service_fw_log_printf("fw: cfg led=%u pa=%u err=%d", led_id, pa, err);
        }
        break;

    case FW_CFG_SET_STATS_PERIOD_S:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        stream_stats_period_s = b[1];
        fw_config_state.stream_stats_period_s = stream_stats_period_s;
        tgm_service_fw_log_printf("fw: cfg stats_period_s=%u", stream_stats_period_s);
        if (stream_stats_period_s > 0 &&
            (notify_ppg_data || notify_acc_data || notify_fw_log))
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
        break;

    case FW_CFG_REQUEST_CONN_PARAM_UPDATE:
        if (len != 1u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        {
            const struct bt_le_conn_param preferred = BT_LE_CONN_PARAM_INIT(
                CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
                CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
                CONFIG_BT_PERIPHERAL_PREF_LATENCY,
                CONFIG_BT_PERIPHERAL_PREF_TIMEOUT);
            const int err = bt_conn_le_param_update(conn, &preferred);
            tgm_service_fw_log_printf("fw: cfg conn_param_update err=%d", err);
        }
        break;

    case FW_CFG_SET_BATTERY_INTERVAL_S:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        fw_config_state.battery_interval_s = (b[1] == 0u) ? 10u : b[1];
        battery_set_measurement_interval_seconds(fw_config_state.battery_interval_s);
        tgm_service_fw_log_printf("fw: cfg battery_interval_s=%u", fw_config_state.battery_interval_s);
        break;

    case FW_CFG_SET_TEMP_INTERVAL_S:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        fw_config_state.temp_interval_s = (b[1] == 0u) ? 1u : b[1];
        temperature_set_measurement_interval_seconds(fw_config_state.temp_interval_s);
        tgm_service_fw_log_printf("fw: cfg temp_interval_s=%u", fw_config_state.temp_interval_s);
        break;

    case FW_CFG_SET_STREAM_ENABLE_MASK:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        fw_config_state.stream_enable_mask = (uint8_t)(b[1] | 0x03u);
        if ((fw_config_state.stream_enable_mask & 0x01) == 0)
        {
            (void)ppg_stop();
        }
        if ((fw_config_state.stream_enable_mask & 0x02) == 0)
        {
            (void)acc_stop();
        }
        if ((fw_config_state.stream_enable_mask & 0x01) != 0)
        {
            (void)ppg_start();
        }
        if ((fw_config_state.stream_enable_mask & 0x02) != 0)
        {
            (void)acc_start();
        }
        tgm_service_fw_log_printf("fw: cfg stream_enable_mask=0x%02x", fw_config_state.stream_enable_mask);
        break;

    case FW_CFG_REQUEST_STATUS_SNAPSHOT:
        if (len != 1u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        tgm_service_emit_diagnostic_snapshot();
        break;

    case FW_CFG_RESTART_CONNECT_PROBE:
        if (len != 1u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        if (CONFIG_TGM_CONNECT_PROBE_DURATION_S > 0)
        {
            connect_probe_active = false;
            k_work_cancel_delayable(&connect_probe_end_work);
            tgm_service_probe_begin();
            (void)k_work_reschedule(&connect_probe_end_work,
                                    K_SECONDS(CONFIG_TGM_CONNECT_PROBE_DURATION_S));
            tgm_service_fw_log_printf("fw: cfg connect_probe_restart %ds",
                                    CONFIG_TGM_CONNECT_PROBE_DURATION_S);
        }
        break;

    default:
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    return len;
}

static void stream_stats_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!(notify_ppg_data || notify_acc_data || notify_fw_log))
    {
        return;
    }

    tgm_service_fw_log_printf(
        "fw: stats ppg_ok=%u ppg_err=%u ppg_last=%d acc_ok=%u acc_err=%u acc_last=%d "
        "log_ok=%u log_err=%u log_last=%d probe=%u",
        ppg_notify_ok, ppg_notify_err, ppg_last_notify_err,
        acc_notify_ok, acc_notify_err, acc_last_notify_err,
        fwlog_sent, fwlog_err, fwlog_last_err,
        connect_probe_active ? 1u : 0u);

    if (stream_stats_period_s > 0)
    {
        k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
    }
}

static void link_keepalive_arm(void)
{
    if (ble_is_connected())
    {
        (void)k_work_reschedule(&link_keepalive_work, K_SECONDS(5));
    }
}

static int gatt_notify_checked(const struct bt_gatt_attr *attr, const void *data, uint16_t len,
                               const char *label, bool high_priority)
{
    static int64_t last_log_ms;
    int64_t now = k_uptime_get();

    if (!high_priority && now < gatt_tx_quiet_until_ms)
    {
        return -ENOBUFS;
    }

    int err = bt_gatt_notify(NULL, attr, data, len);

    if (!err)
    {
        if (now >= gatt_tx_quiet_until_ms)
        {
            gatt_tx_quiet_until_ms = 0;
        }
    }
    else if (err == -ENOMEM)
    {
        gatt_tx_quiet_until_ms = now + GATT_TX_BACKOFF_MS;
    }

    if (err && err != -EACCES && err != -ENOBUFS)
    {
        if ((now - last_log_ms) >= GATT_NOTIFY_LOG_INTERVAL_MS)
        {
            LOG_WRN("GATT notify failed (%s): err=%d len=%u", label, err, len);
            last_log_ms = now;
        }
    }

    return err;
}

static void link_keepalive_work_handler(struct k_work *work);
static void tgm_service_resume_sensor_streams(void);
static void tgm_service_suspend_sensor_streams(void);

static bool streaming_allowed(void)
{
    return device_worn || connect_probe_active;
}

bool tgm_service_connect_probe_active(void)
{
    return connect_probe_active;
}

static void tgm_service_probe_begin(void)
{
    if (connect_probe_active)
    {
        return;
    }

    connect_probe_active = true;
    LOG_INF("Connect probe: streaming enabled for %d s (worn=%d)",
            CONFIG_TGM_CONNECT_PROBE_DURATION_S, device_worn);

    if (tgm_service_cb && tgm_service_cb->probe_begin_cb)
    {
        tgm_service_cb->probe_begin_cb();
    }

    tgm_service_resume_sensor_streams();
}

static void tgm_service_probe_end(void)
{
    if (!connect_probe_active)
    {
        return;
    }

    connect_probe_active = false;
    LOG_INF("Connect probe ended (worn=%d)", device_worn);

    if (tgm_service_cb && tgm_service_cb->probe_end_cb)
    {
        tgm_service_cb->probe_end_cb();
    }

    if (!device_worn)
    {
        tgm_service_suspend_sensor_streams();
    }
}

static void connect_probe_end_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);
    tgm_service_probe_end();
}

static void tgm_service_resume_sensor_streams(void)
{
    if (notify_ppg_data && streaming_allowed())
    {
        (void)k_work_submit(&ppg_start_work);
    }

    if (notify_acc_data && streaming_allowed())
    {
        (void)k_work_submit(&acc_start_work);
    }
}

static void tgm_service_suspend_sensor_streams(void)
{
    ppg_set_notify_poll(false);
    (void)ppg_stop();
    (void)acc_stop();
}

void tgm_service_set_device_worn(bool worn)
{
    if (device_worn == worn)
    {
        return;
    }

    device_worn = worn;
    LOG_INF("Streaming policy: %s", worn ? "on-body (full rate)" : "off-body (minimal BLE)");

    if (worn)
    {
        tgm_service_resume_sensor_streams();
    }
    else if (!connect_probe_active)
    {
        tgm_service_suspend_sensor_streams();
    }
}

static void tgm_service_battery_notify_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_battery)
    {
        return;
    }

    if (!(tgm_service_cb && tgm_service_cb->bat_cb))
    {
        LOG_WRN("Battery notify requested but callback is not set");
        return;
    }

    int32_t now_mv = tgm_service_cb->bat_cb();
    if (now_mv <= 0)
    {
        LOG_INF("Battery notify deferred: no cached mV yet");
        return;
    }

    int err = tgm_service_send_battery_notify(now_mv);
    if (err && err != -EACCES)
    {
        LOG_WRN("Deferred battery notify failed (%d)", err);
    }
    else if (!err)
    {
        LOG_INF("Battery notify sent: %dmV", now_mv);
    }
}

static void tgm_service_ppg_start_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_ppg_data || !streaming_allowed())
    {
        return;
    }

    int err = ppg_start();
    if (err)
    {
        LOG_WRN("Deferred PPG start failed (%d)", err);
    }

    ppg_set_notify_poll(true);
    ppg_poll_once();
}

static void tgm_service_acc_start_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_acc_data || !streaming_allowed())
    {
        return;
    }

    int err = acc_start();
    if (err)
    {
        LOG_WRN("Deferred ACC start failed (%d)", err);
    }
}

static void tgm_service_ccc_ppg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_ppg_data = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("PPG notifications %s", notify_ppg_data ? "enabled" : "disabled");
    if (notify_ppg_data)
    {
        link_keepalive_arm();
        (void)k_work_submit(&ppg_start_work);
    }
    else
    {
        ppg_set_notify_poll(false);
        (void)ppg_stop();
    }
}

static void tgm_service_ccc_acc_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_acc_data = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("ACC notifications %s", notify_acc_data ? "enabled" : "disabled");
    if (notify_acc_data)
    {
        link_keepalive_arm();
        (void)k_work_submit(&acc_start_work);
    }
    else
    {
        (void)acc_stop();
    }
}

static void tgm_service_ccc_temp_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for temp data");
    notify_temp_data = (value == BT_GATT_CCC_NOTIFY);
    if (notify_temp_data)
    {
        link_keepalive_arm();
    }
}

static void tgm_service_ccc_bat_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for battery data");
    notify_battery = (value == BT_GATT_CCC_NOTIFY);
    if (notify_battery)
    {
        link_keepalive_arm();
        /* Do not notify directly in the CCC callback context. */
        (void)k_work_submit(&battery_notify_work);
        if (battery_get_last_measurement() <= 0)
        {
            (void)battery_start_measurement();
        }
    }
}

static void tgm_service_ccc_read_ppg_reg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for read ppg reg data");
    notify_read_ppg_reg = (value == BT_GATT_CCC_NOTIFY);
    if (notify_read_ppg_reg)
    {
        link_keepalive_arm();
    }
}

static void tgm_service_ccc_write_ppg_reg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for write ppg reg data");
    notify_write_ppg_reg = (value == BT_GATT_CCC_NOTIFY);
    if (notify_write_ppg_reg)
    {
        link_keepalive_arm();
    }
}

static void tgm_service_ccc_status_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for device status");
    notify_status = (value == BT_GATT_CCC_NOTIFY);
    if (notify_status)
    {
        link_keepalive_arm();
    }
}

static void tgm_service_ccc_fw_log_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_fw_log = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Firmware log notifications %s", notify_fw_log ? "enabled" : "disabled");
    if (notify_fw_log)
    {
        link_keepalive_arm();
        tgm_service_fw_log_printf("fw: log stream open");
        if (stream_stats_period_s > 0)
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
    }
}

static void tgm_service_ccc_fw_config_state_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_fw_config_state = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Firmware config state notifications %s",
            notify_fw_config_state ? "enabled" : "disabled");
    if (notify_fw_config_state)
    {
        link_keepalive_arm();
    }
}

static ssize_t get_fw_config_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset)
{
    const struct tgm_fw_config_state_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

// Callback function to get the battery value when the client reads this value
static ssize_t get_bat_value(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    // Get a pointer to bat_value
    const int32_t *value = attr->user_data;

    LOG_INF("Reading battery value");

    if (tgm_service_cb && tgm_service_cb->bat_cb)
    {
        bat_value = tgm_service_cb->bat_cb();
    }

    if (bat_value <= 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(bat_value));
}

// Callback function to get the UUID value when the client reads this value
static ssize_t get_uuid_value(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    // Get a pointer to uuid_value
    const uint64_t *value = attr->user_data;

    LOG_INF("Reading UUID value");

    uuid_value = ((uint64_t)NRF_FICR->DEVICEID[1] << 32) | NRF_FICR->DEVICEID[0];
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

// Callback function to get the firmware version when the client reads this value
static ssize_t get_fw_version(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    // Get a pointer to fw_version string
    const char *value = attr->user_data;

    LOG_INF("Reading firmware version: %s", fw_version);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value,
                             strnlen(value, sizeof(fw_version)));
}

// Callback function to get the device status when the client reads this value
static ssize_t get_status_value(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    const struct tgm_service_status_t *value = attr->user_data;

    LOG_INF("Reading device status");
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

// Callback function to read the PPG register when the client writes to this value
static ssize_t read_ppg_reg(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len != 1u)
    {
        LOG_DBG("Invalid length for PPG register read");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (offset != 0)
    {
        LOG_DBG("Invalid offset for PPG register read");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    uint8_t ppg_reg = *((uint8_t *)buf);

    LOG_INF("Reading PPG register");
    ppg_read_reg(ppg_reg);

    return len;
}

// Callback function to write the PPG register when the client writes to this value
static ssize_t write_ppg_reg(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (len != 2u)
    {
        LOG_DBG("Invalid length for PPG register write");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    if (offset != 0)
    {
        LOG_DBG("Invalid offset for PPG register write");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    uint8_t ppg_reg = *((uint8_t *)buf);
    uint8_t ppg_reg_data = *((uint8_t *)buf + 1);

    LOG_INF("Writing PPG register");
    ppg_write_reg(ppg_reg, ppg_reg_data);

    return len;
}

BT_GATT_SERVICE_DEFINE(
    tgm_service_svc,
    BT_GATT_PRIMARY_SERVICE(BT_UUID_TGM),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_UUID,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        get_uuid_value, NULL,
        &uuid_value),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_FW,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        get_fw_version, NULL,
        &fw_version),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_BAT,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        get_bat_value, NULL,
        &bat_value),
    BT_GATT_CCC(tgm_service_ccc_bat_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_PPG,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        NULL, NULL,
        &tgm_service_ppg_data),
    BT_GATT_CCC(tgm_service_ccc_ppg_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_ACC,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        NULL, NULL,
        &tgm_service_acc_data),
    BT_GATT_CCC(tgm_service_ccc_acc_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_TEMP,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        NULL, NULL,
        &tgm_service_temp_data),
    BT_GATT_CCC(tgm_service_ccc_temp_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_READ_PPG_REG,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_WRITE,
        NULL, read_ppg_reg,
        NULL),
    BT_GATT_CCC(tgm_service_ccc_read_ppg_reg_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_WRITE_PPG_REG,
        BT_GATT_CHRC_WRITE | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_WRITE,
        NULL, write_ppg_reg,
        NULL),
    BT_GATT_CCC(tgm_service_ccc_write_ppg_reg_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_STATUS,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        get_status_value, NULL,
        &device_status),
    BT_GATT_CCC(tgm_service_ccc_status_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_FW_LOG,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        NULL, NULL,
        NULL),
    BT_GATT_CCC(tgm_service_ccc_fw_log_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_FW_CONFIG,
        BT_GATT_CHRC_WRITE,
        BT_GATT_PERM_WRITE,
        NULL, write_fw_config,
        NULL),
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_FW_CONFIG_STATE,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        get_fw_config_state, NULL,
        &fw_config_state),
    BT_GATT_CCC(tgm_service_ccc_fw_config_state_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
);

/* GATT attribute indices for notify targets (see BT_GATT_SERVICE_DEFINE order). */
enum {
    TGM_ATTR_BAT_VALUE = 6,
    TGM_ATTR_PPG_VALUE = 9,
    TGM_ATTR_ACC_VALUE = 12,
    TGM_ATTR_TEMP_VALUE = 15,
    TGM_ATTR_READ_PPG_REG_VALUE = 18,
    TGM_ATTR_WRITE_PPG_REG_VALUE = 21,
    TGM_ATTR_STATUS_VALUE = 24,
    TGM_ATTR_FW_LOG_VALUE = 27,
    TGM_ATTR_FW_CONFIG_STATE_VALUE = 32,
};

int tgm_service_init(struct tgm_service_cb *callbacks)
{
    if (callbacks)
    {
        tgm_service_cb = callbacks;
    }

    k_work_init(&battery_notify_work, tgm_service_battery_notify_work_handler);
    k_work_init(&ppg_start_work, tgm_service_ppg_start_work_handler);
    k_work_init(&acc_start_work, tgm_service_acc_start_work_handler);
    k_work_init_delayable(&link_keepalive_work, link_keepalive_work_handler);

    k_work_init_delayable(&connect_probe_end_work, connect_probe_end_work_handler);
    k_work_init_delayable(&stream_stats_work, stream_stats_work_handler);
    stream_stats_period_s = fw_config_state.stream_stats_period_s;

    return 0;
}

void tgm_service_on_connected(void)
{
    gatt_tx_quiet_until_ms = 0;
    link_keepalive_arm();

    if (CONFIG_TGM_CONNECT_PROBE_DURATION_S > 0)
    {
        tgm_service_probe_begin();
        (void)k_work_reschedule(&connect_probe_end_work,
                                K_SECONDS(CONFIG_TGM_CONNECT_PROBE_DURATION_S));
    }
    else if (device_worn)
    {
        tgm_service_resume_sensor_streams();
    }

    if (tgm_service_cb && tgm_service_cb->link_connected_cb)
    {
        tgm_service_cb->link_connected_cb();
    }
}

void tgm_service_on_disconnect(void)
{
    k_work_cancel_delayable(&connect_probe_end_work);
    tgm_service_probe_end();

    notify_ppg_data = false;
    notify_acc_data = false;
    notify_temp_data = false;
    notify_battery = false;
    notify_read_ppg_reg = false;
    notify_write_ppg_reg = false;
    notify_status = false;
    notify_fw_log = false;
    notify_fw_config_state = false;

    k_work_cancel_delayable(&stream_stats_work);
    gatt_tx_quiet_until_ms = 0;
    ppg_set_notify_poll(false);
    k_work_cancel_delayable(&link_keepalive_work);
    tgm_service_suspend_sensor_streams();

    if (tgm_service_cb && tgm_service_cb->link_disconnected_cb)
    {
        tgm_service_cb->link_disconnected_cb();
    }
}

int tgm_service_send_battery_notify(int32_t battery_value)
{
    if (!notify_battery)
    {
        return -EACCES;
    }

    bat_value = battery_value;

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_BAT_VALUE],
                                 &bat_value, sizeof(bat_value), "battery", true);
}

int tgm_service_send_ppg_notify(struct ppg_sample *ppg_data, uint8_t sample_cnt)
{
    if (!streaming_allowed())
    {
        return -EACCES;
    }

    if (!notify_ppg_data)
    {
        return -EACCES;
    }

    if (sample_cnt == 0)
    {
        return 0;
    }

    if (sample_cnt > CONFIG_PPG_SAMPLES_PER_FRAME)
    {
        sample_cnt = CONFIG_PPG_SAMPLES_PER_FRAME;
    }

    tgm_service_ppg_data.frame_counter = ppg_frame_counter++;
    memcpy(tgm_service_ppg_data.ppg_data, ppg_data,
           sizeof(struct ppg_sample) * sample_cnt);

    const size_t notify_len = sizeof(uint32_t) + (sizeof(struct ppg_sample) * sample_cnt);

    const int err = gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_PPG_VALUE],
                                        &tgm_service_ppg_data, notify_len, "ppg", false);
    if (err)
    {
        ppg_notify_err++;
        ppg_last_notify_err = err;
    }
    else
    {
        ppg_notify_ok++;
    }
    return err;
}

int tgm_service_send_acc_notify(struct acc_sample *acc_data, uint8_t sample_cnt)
{
    if (!streaming_allowed())
    {
        return -EACCES;
    }

    if (!notify_acc_data)
    {
        return -EACCES;
    }

    if (sample_cnt == 0)
    {
        return 0;
    }

    if (sample_cnt > CONFIG_ACC_SAMPLES_PER_FRAME)
    {
        sample_cnt = CONFIG_ACC_SAMPLES_PER_FRAME;
    }

    tgm_service_acc_data.frame_counter = acc_frame_counter++;
    memcpy(tgm_service_acc_data.acc_data, acc_data,
           sizeof(struct acc_sample) * sample_cnt);

    const size_t notify_len = sizeof(uint32_t) + (sizeof(struct acc_sample) * sample_cnt);

    const int err = gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_ACC_VALUE],
                                        &tgm_service_acc_data, notify_len, "acc", false);
    if (err)
    {
        acc_notify_err++;
        acc_last_notify_err = err;
    }
    else
    {
        acc_notify_ok++;
    }
    return err;
}

int tgm_service_send_temp_notify(int16_t new_temp)
{
    tgm_service_temp_data.frame_counter++;
    tgm_service_temp_data.centitemp = new_temp;

    if (!streaming_allowed())
    {
        return -EACCES;
    }

    if (!notify_temp_data)
    {
        return -EACCES;
    }

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_TEMP_VALUE],
                                 &tgm_service_temp_data, sizeof(tgm_service_temp_data), "temp",
                                 false);
}

int tgm_service_send_read_ppg_reg_notify(uint8_t ppg_reg_data)
{
    if (!notify_read_ppg_reg)
    {
        return -EACCES;
    }

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_READ_PPG_REG_VALUE],
                                 &ppg_reg_data, sizeof(ppg_reg_data), "ppg-reg-read", true);
}

int tgm_service_send_write_ppg_reg_notify(uint8_t ppg_reg_data)
{
    if (!notify_write_ppg_reg)
    {
        return -EACCES;
    }

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_WRITE_PPG_REG_VALUE],
                                 &ppg_reg_data, sizeof(ppg_reg_data), "ppg-reg-write", true);
}

int tgm_service_send_status_notify(bool charging, bool worn, uint8_t state, uint8_t battery_pct)
{
    device_status.charging = charging ? 1 : 0;
    device_status.worn = worn ? 1 : 0;
    device_status.device_state = state;
    device_status.battery_pct = battery_pct;

    if (!notify_status)
    {
        return -EACCES;
    }

    LOG_INF("Sending status: charging=%d, worn=%d, state=%d, battery=%d%%",
            charging, worn, state, battery_pct);

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                 &device_status, sizeof(device_status), "status", true);
}

int tgm_service_send_fw_log_notify(const void *data, uint16_t len)
{
    if (!notify_fw_log || !data || len == 0)
    {
        return -EACCES;
    }

    const int err = gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_FW_LOG_VALUE],
                                        data, len, "fw-log", true);
    if (err)
    {
        fwlog_err++;
        fwlog_last_err = err;
    }
    else
    {
        fwlog_sent++;
    }
    return err;
}

void tgm_service_fw_log_printf(const char *fmt, ...)
{
    if (!notify_fw_log || !fmt)
    {
        return;
    }

    char buf[160];
    va_list ap;

    va_start(ap, fmt);
    const int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
    {
        return;
    }

    const uint16_t send_len = (uint16_t)((n >= (int)sizeof(buf)) ? (sizeof(buf) - 1) : n);
    (void)tgm_service_send_fw_log_notify(buf, send_len);
}

void tgm_service_emit_diagnostic_snapshot(void)
{
    tgm_service_fw_log_printf(
        "fw: snap chg=%u worn=%u st=%u bat=%u probe=%u",
        device_status.charging, device_status.worn, device_status.device_state,
        device_status.battery_pct, connect_probe_active ? 1u : 0u);

    if (notify_status)
    {
        (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                  &device_status, sizeof(device_status), "status-snap", true);
    }

    if (notify_fw_config_state)
    {
        (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_FW_CONFIG_STATE_VALUE],
                                  &fw_config_state, sizeof(fw_config_state), "fwcfg-snap", true);
    }

    if (tgm_service_cb && tgm_service_cb->diagnostic_snapshot_cb)
    {
        tgm_service_cb->diagnostic_snapshot_cb();
    }
}

static void link_keepalive_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!ble_is_connected())
    {
        return;
    }

    if (any_stream_notify_enabled())
    {
        if (notify_battery && tgm_service_cb && tgm_service_cb->bat_cb)
        {
            int32_t now_mv = tgm_service_cb->bat_cb();

            if (now_mv > 0)
            {
                (void)tgm_service_send_battery_notify(now_mv);
            }
        }
        else if (notify_status)
        {
            (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                      &device_status, sizeof(device_status), "status-keepalive",
                                      true);
        }
        else if (notify_temp_data)
        {
            (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_TEMP_VALUE],
                                      &tgm_service_temp_data, sizeof(tgm_service_temp_data),
                                      "temp-keepalive", true);
        }
    }

    link_keepalive_arm();
}