/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>

#include <app_version.h>
#include "tgm_service.h"
#include "battery.h"
#include "power_profiler.h"
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
static bool notify_battery_stats;
static bool notify_fw_log;
static bool notify_fw_config_state;

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

/* Streaming health counters (helps debug 1 pkt/s + disconnect loops). */
static uint32_t ppg_notify_ok = 0;
static uint32_t ppg_notify_err = 0;
static int32_t ppg_last_notify_err = 0;
static uint32_t acc_notify_ok = 0;
static uint32_t acc_notify_err = 0;
static int32_t acc_last_notify_err = 0;
static uint32_t fwlog_sent = 0;
static uint32_t fwlog_err = 0;
static int32_t fwlog_last_err = 0;

/* Firmware config state (readable by iOS). */
struct fw_config_state_t {
    uint8_t version; /* struct version */
    uint8_t led_green_pa;
    uint8_t led_ir_pa;
    uint8_t led_red_pa;
    uint8_t stream_stats_period_s; /* 0 disables */
    uint8_t battery_interval_s; /* 0 disables periodic battery */
    uint8_t temp_interval_s; /* 0 disables periodic temp */
    uint8_t stream_enable_mask; /* bit0=ppg bit1=acc */
};

static struct fw_config_state_t fw_config_state = {
    .version = 1,
    .led_green_pa = 5,
    .led_ir_pa = 128,
    .led_red_pa = 32,
    .stream_stats_period_s = 5,
    .battery_interval_s = CONFIG_BATTERY_MEASUREMENT_INTERVAL,
    .temp_interval_s = CONFIG_TEMPERATURE_MEASUREMENT_INTERVAL,
    .stream_enable_mask = 0x03,
};

static uint8_t stream_stats_period_s = 5;

/* Declared in main.c */
void temperature_set_measurement_interval_seconds(uint16_t seconds);

static struct k_work_delayable stream_stats_work;

/* Firmware config TLV opcodes */
enum fw_cfg_opcode_t {
    FW_CFG_SET_LED_PA = 0x01,            /* [led_id,u8 pa] led_id: 0=green 1=ir 2=red */
    FW_CFG_SET_STATS_PERIOD_S = 0x02,     /* [u8 seconds] 0 disables */
    FW_CFG_REQUEST_CONN_PARAM_UPDATE = 0x03, /* [] */
    FW_CFG_SET_BATTERY_INTERVAL_S = 0x04, /* [u8 seconds] 0 disables */
    FW_CFG_SET_TEMP_INTERVAL_S = 0x05,    /* [u8 seconds] 0 disables */
    FW_CFG_SET_STREAM_ENABLE_MASK = 0x06, /* [u8 mask] bit0=ppg bit1=acc */
};

static ssize_t write_fw_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    if (offset != 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }
    if (len < 1u)
    {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t *b = (const uint8_t *)buf;
    uint8_t op = b[0];

    switch (op)
    {
    case FW_CFG_SET_LED_PA:
        if (len != 3u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        {
            uint8_t led_id = b[1];
            uint8_t pa = b[2];

            enum ppg_led_t led;
            if (led_id == 0) { led = PPG_LED_GREEN; fw_config_state.led_green_pa = pa; }
            else if (led_id == 1) { led = PPG_LED_IR; fw_config_state.led_ir_pa = pa; }
            else if (led_id == 2) { led = PPG_LED_RED; fw_config_state.led_red_pa = pa; }
            else { return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED); }

            int err = ppg_set_led_pa(led, pa);
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
        if (stream_stats_period_s > 0 && (notify_ppg_data || notify_acc_data || notify_fw_log))
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
            struct bt_le_conn_param preferred = BT_LE_CONN_PARAM_INIT(
                CONFIG_BT_PERIPHERAL_PREF_MIN_INT,
                CONFIG_BT_PERIPHERAL_PREF_MAX_INT,
                CONFIG_BT_PERIPHERAL_PREF_LATENCY,
                CONFIG_BT_PERIPHERAL_PREF_TIMEOUT
            );
            int err = bt_conn_le_param_update(conn, &preferred);
            tgm_service_fw_log_printf("fw: cfg conn_param_update err=%d", err);
        }
        break;

    case FW_CFG_SET_BATTERY_INTERVAL_S:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        fw_config_state.battery_interval_s = b[1];
        battery_set_measurement_interval_seconds(fw_config_state.battery_interval_s);
        tgm_service_fw_log_printf("fw: cfg battery_interval_s=%u", fw_config_state.battery_interval_s);
        break;

    case FW_CFG_SET_TEMP_INTERVAL_S:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        /* Applied in main.c via a shared extern setter. */
        fw_config_state.temp_interval_s = b[1];
        temperature_set_measurement_interval_seconds(fw_config_state.temp_interval_s);
        tgm_service_fw_log_printf("fw: cfg temp_interval_s=%u", fw_config_state.temp_interval_s);
        break;

    case FW_CFG_SET_STREAM_ENABLE_MASK:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        fw_config_state.stream_enable_mask = b[1];
        if ((fw_config_state.stream_enable_mask & 0x01) == 0) { (void)ppg_stop(); }
        if ((fw_config_state.stream_enable_mask & 0x02) == 0) { (void)acc_stop(); }
        if ((fw_config_state.stream_enable_mask & 0x01) != 0) { (void)ppg_start(); }
        if ((fw_config_state.stream_enable_mask & 0x02) != 0) { (void)acc_start(); }
        tgm_service_fw_log_printf("fw: cfg stream_enable_mask=0x%02x", fw_config_state.stream_enable_mask);
        break;

    default:
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }

    return len;
}

static void stream_stats_work_handler(struct k_work *work)
{
    /* Only report while actively streaming or while fw-log notifications are enabled. */
    if (!(notify_ppg_data || notify_acc_data || notify_fw_log))
    {
        return;
    }

    tgm_service_fw_log_printf(
        "fw: stats ppg_ok=%u ppg_err=%u ppg_last=%d acc_ok=%u acc_err=%u acc_last=%d log_ok=%u log_err=%u log_last=%d",
        ppg_notify_ok, ppg_notify_err, ppg_last_notify_err,
        acc_notify_ok, acc_notify_err, acc_last_notify_err,
        fwlog_sent, fwlog_err, fwlog_last_err
    );

    if (stream_stats_period_s > 0)
    {
        k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
    }
}

static void tgm_service_ccc_ppg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for ppg data");
    notify_ppg_data = (value == BT_GATT_CCC_NOTIFY);
    if (notify_ppg_data || notify_acc_data)
        power_profiler_set_state(POWER_STATE_STREAM);
    else
        power_profiler_set_state(POWER_STATE_CONN);

    if (notify_ppg_data)
    {
        if (stream_stats_period_s > 0)
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
    }
}

static void tgm_service_ccc_acc_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for acc data");
    notify_acc_data = (value == BT_GATT_CCC_NOTIFY);
    if (notify_ppg_data || notify_acc_data)
        power_profiler_set_state(POWER_STATE_STREAM);
    else
        power_profiler_set_state(POWER_STATE_CONN);

    if (notify_acc_data)
    {
        if (stream_stats_period_s > 0)
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
    }
}

static void tgm_service_ccc_temp_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for temp data");
    notify_temp_data = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_bat_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for battery data");
    notify_battery = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_read_ppg_reg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for read ppg reg data");
    notify_read_ppg_reg = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_write_ppg_reg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for write ppg reg data");
    notify_write_ppg_reg = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_status_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for device status");
    notify_status = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_battery_stats_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for battery stats");
    notify_battery_stats = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_fw_log_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for firmware logs");
    notify_fw_log = (value == BT_GATT_CCC_NOTIFY);

    if (notify_fw_log)
    {
        if (stream_stats_period_s > 0)
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
    }
}

static void tgm_service_ccc_fw_config_state_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for firmware config state");
    notify_fw_config_state = (value == BT_GATT_CCC_NOTIFY);
}

static ssize_t get_fw_config_state(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    const struct fw_config_state_t *value = attr->user_data;
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(*value));
}

static struct tgm_battery_stats_t battery_stats_data;
static struct k_work_delayable battery_stats_work;

static void battery_stats_work_handler(struct k_work *work)
{
    struct battery_usage_telemetry_t tel;
    battery_get_usage_telemetry(&tel);

    /* Build 6-byte payload: no float; scaled uint16_t for mAh (0.01 mAh units) */
    uint16_t voltage_mv = (uint16_t)(tel.voltage_mv > 0 ? tel.voltage_mv : 0);
    uint16_t mah_scaled = (uint16_t)(tel.mah_consumed_scaled > 0xFFFF ? 0xFFFF : tel.mah_consumed_scaled);
    uint32_t remaining = tel.remaining_minutes;

    battery_stats_data.voltage_hi = (uint8_t)((voltage_mv >> 8) & 0xFF);
    battery_stats_data.voltage_lo = (uint8_t)(voltage_mv & 0xFF);
    battery_stats_data.percent = tel.percent;
    battery_stats_data.mah_consumed_hi = (uint8_t)((mah_scaled >> 8) & 0xFF);
    battery_stats_data.mah_consumed_lo = (uint8_t)(mah_scaled & 0xFF);
    battery_stats_data.est_mins_remaining = (remaining > 255) ? 255 : (uint8_t)remaining;

    tgm_service_send_battery_stats_notify(&battery_stats_data);
    k_work_reschedule(&battery_stats_work, K_SECONDS(60));
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
        return bt_gatt_attr_read(conn, attr, buf, len, offset, value, sizeof(bat_value));
    }

    return 0;
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

    LOG_INF("Reading firmware version");
    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
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
        BT_UUID_TGM_BATTERY_STATS,
        BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        NULL, NULL,
        &battery_stats_data),
    BT_GATT_CCC(tgm_service_ccc_battery_stats_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),

    /* Firmware config: write commands + read/notify applied state. */
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

int tgm_service_init(struct tgm_service_cb *callbacks)
{
    if (callbacks)
    {
        tgm_service_cb = callbacks;
    }

    /* Seed runtime config values from exported state struct. */
    stream_stats_period_s = fw_config_state.stream_stats_period_s;
    battery_set_measurement_interval_seconds(fw_config_state.battery_interval_s);
    temperature_set_measurement_interval_seconds(fw_config_state.temp_interval_s);

    k_work_init_delayable(&battery_stats_work, battery_stats_work_handler);
    k_work_reschedule(&battery_stats_work, K_SECONDS(60));

    k_work_init_delayable(&stream_stats_work, stream_stats_work_handler);
    return 0;
}

int tgm_service_send_battery_notify(int32_t battery_value)
{
    if (!notify_battery)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[6], &battery_value, sizeof(battery_value));
}

int tgm_service_send_ppg_notify(struct ppg_sample *ppg_data, uint8_t sample_cnt)
{
    struct tgm_service_ppg_data_t ppg_data_notify = {
        .frame_counter = ppg_frame_counter++};
    memcpy(&ppg_data_notify.ppg_data, ppg_data, sizeof(struct ppg_sample) * sample_cnt);
    if (!notify_ppg_data)
    {
        return -EACCES;
    }

    int err = bt_gatt_notify(NULL, &tgm_service_svc.attrs[9], &ppg_data_notify, sizeof(struct tgm_service_ppg_data_t));
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
    struct tgm_service_acc_data_t acc_data_notify = {
        .frame_counter = acc_frame_counter++};
    memcpy(&acc_data_notify.acc_data, acc_data, sizeof(struct acc_sample) * sample_cnt);
    if (!notify_acc_data)
    {
        return -EACCES;
    }

    int err = bt_gatt_notify(NULL, &tgm_service_svc.attrs[12], &acc_data_notify, sizeof(struct tgm_service_acc_data_t));
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

    if (!notify_temp_data)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[15], &tgm_service_temp_data, sizeof(tgm_service_temp_data));
}

int tgm_service_send_read_ppg_reg_notify(uint8_t ppg_reg_data)
{
    if (!notify_read_ppg_reg)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[18], &ppg_reg_data, sizeof(ppg_reg_data));
}

int tgm_service_send_write_ppg_reg_notify(uint8_t ppg_reg_data)
{
    if (!notify_write_ppg_reg)
    {
        return -EACCES;
    }

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[21], &ppg_reg_data, sizeof(ppg_reg_data));
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

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[24], &device_status, sizeof(device_status));
}

int tgm_service_send_battery_stats_notify(const struct tgm_battery_stats_t *stats)
{
    if (!notify_battery_stats || !stats)
    {
        return -EACCES;
    }
    /* 6-byte payload: Voltage_HI, Voltage_LO, Percent, mAh_HI, mAh_LO, Est_Mins */
    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[30], stats, 6);
}

int tgm_service_send_fw_log_notify(const void *data, uint16_t len)
{
    if (!notify_fw_log || !data || len == 0)
    {
        return -EACCES;
    }
    /* FW_LOG value attr index (see BT_GATT_SERVICE_DEFINE layout): attrs[27] */
    int err = bt_gatt_notify(NULL, &tgm_service_svc.attrs[27], data, len);
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
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    if (n <= 0)
    {
        return;
    }

    /* Best-effort: if truncated, still send what fits. */
    uint16_t send_len = (uint16_t)((n >= (int)sizeof(buf)) ? (sizeof(buf) - 1) : n);
    (void)tgm_service_send_fw_log_notify(buf, send_len);
}