/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>

#include <app_version.h>
#include "tgm_service.h"
#include "ppg.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(tgm_service, CONFIG_APP_LOG_LEVEL);

static bool notify_ppg_data;
static bool notify_acc_data;
static bool notify_temp_data;
static bool notify_battery;
static bool notify_read_ppg_reg;
static bool notify_write_ppg_reg;

static uint32_t ppg_frame_counter = 0;
static uint32_t acc_frame_counter = 0;

static struct tgm_service_ppg_data_t tgm_service_ppg_data;
static struct tgm_service_acc_data_t tgm_service_acc_data;
static struct tgm_service_temp_data_t tgm_service_temp_data;
static uint16_t bat_value;
static uint64_t uuid_value;
static char fw_version[15] = APP_VERSION_STRING;
static struct tgm_service_cb *tgm_service_cb = NULL;

static void tgm_service_ccc_ppg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for ppg data");
    notify_ppg_data = (value == BT_GATT_CCC_NOTIFY);
}

static void tgm_service_ccc_acc_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    LOG_INF("Enabled notifications for acc data");
    notify_acc_data = (value == BT_GATT_CCC_NOTIFY);
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
    BT_GATT_CCC(tgm_service_ccc_write_ppg_reg_data_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE), );

int tgm_service_init(struct tgm_service_cb *callbacks)
{
    if (callbacks)
    {
        tgm_service_cb = callbacks;
    }

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

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[9], &ppg_data_notify, sizeof(struct tgm_service_ppg_data_t));
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

    return bt_gatt_notify(NULL, &tgm_service_svc.attrs[12], &acc_data_notify, sizeof(struct tgm_service_acc_data_t));
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