/*
 * Copyright (c) 2024 WeeGee bv
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/uuid.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#include <app_version.h>
#include "tgm_service.h"
#include "battery.h"
#include "ble.h"
#include "ppg.h"
#include "acc.h"
#include "i2c_pin_scan.h"

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
static bool notify_battery_usage;
static uint8_t battery_usage_payload[TGM_BATTERY_USAGE_BYTES];
static uint16_t att_mtu = TGM_ATT_MTU_DEFAULT;
static uint8_t ppg_wire[TGM_PPG_FULL_FRAME_BYTES];
static uint8_t acc_wire[TGM_ACC_FULL_FRAME_BYTES];
static uint8_t temp_wire[TGM_TEMP_VALUE_BYTES];
static uint8_t bat_wire[TGM_BATTERY_VALUE_BYTES];
static uint8_t id_wire[TGM_DEVICE_ID_VALUE_BYTES];
static uint8_t status_wire[TGM_STATUS_VALUE_BYTES];
static uint8_t fw_config_wire[TGM_FW_CONFIG_STATE_BYTES];

static uint32_t ppg_notify_ok = 0;
static uint32_t ppg_notify_err = 0;
static int32_t ppg_last_notify_err = 0;
static int32_t ppg_quiet_err = 0;
static uint8_t ppg_quiet_empty = 0;
static int64_t ppg_quiet_log_ms;
static uint32_t acc_notify_ok = 0;
static uint32_t acc_notify_err = 0;
static int32_t acc_last_notify_err = 0;
static uint32_t fwlog_sent = 0;
static uint32_t fwlog_err = 0;
static int32_t fwlog_last_err = 0;

static struct tgm_fw_config_state_t fw_config_state = {
    .version = 2,
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
static struct k_work_delayable sensor_health_work;
static int64_t sensor_stream_watch_ms;
static int64_t last_ppg_notify_ok_ms;
static int64_t last_acc_notify_ok_ms;
static int64_t last_ppg_sample_ms;
static int64_t last_acc_sample_ms;
static int64_t last_acc_motion_ms;
static int16_t last_acc_motion_x;
static int16_t last_acc_motion_y;
static int16_t last_acc_motion_z;
static bool acc_motion_have_ref;
static int64_t last_bat_notify_ok_ms;
static int64_t last_fwlog_notify_ok_ms;
static int64_t mode3_pad_grace_until_ms;
static int64_t mode3_soft_floor_hold_until_ms;

#define SENSOR_NOTIFY_STALL_MS 4000
#define MODE3_PAD_GRACE_MS 5000
#define MODE3_SOFT_FLOOR_HOLD_MS (6 * 60 * 1000)
/** ~100 mg at ±2 g / 14-bit left-justified — ignores noise, catches tiny moves. */
#define ACC_MOTION_DELTA_LSB 400
/** PPG sensing + no ACC motion this long while off-body → desk/bench abandon. */
#define ACC_NO_MOTION_RECOVER_MS (10 * 60 * 1000)

static uint32_t ppg_frame_counter = 0;
static uint32_t acc_frame_counter = 0;
/* Non-zero while a notify was refused. The same bytes are sent again. */
static uint16_t ppg_held_len;
static uint16_t acc_held_len;
/* The 20 ms poll reads about one sample. Notify only a full frame. */
static struct ppg_sample ppg_batch[CONFIG_PPG_SAMPLES_PER_FRAME];
static uint8_t ppg_batch_n;
static struct acc_sample acc_batch[CONFIG_ACC_SAMPLES_PER_FRAME];
static uint8_t acc_batch_n;

static struct tgm_service_ppg_data_t tgm_service_ppg_data;
static struct tgm_service_acc_data_t tgm_service_acc_data;
static struct tgm_service_temp_data_t tgm_service_temp_data;
static struct tgm_service_status_t device_status;
static int32_t bat_value;
static uint64_t uuid_value;
/* 2.0.1-gen2-nrfconnect is 22 characters. The old 15-byte buffer would clip it. */
static char fw_version[32] = APP_VERSION_STRING;

BUILD_ASSERT(TGM_PPG_FULL_FRAME_BYTES == 244);
BUILD_ASSERT(TGM_ACC_FULL_FRAME_BYTES == 154);
BUILD_ASSERT(TGM_PPG_FULL_FRAME_MIN_ATT_MTU == 247);
BUILD_ASSERT(TGM_ACC_FULL_FRAME_MIN_ATT_MTU == 157);
BUILD_ASSERT(sizeof(struct tgm_service_status_t) == TGM_STATUS_VALUE_BYTES);
BUILD_ASSERT(sizeof(struct tgm_fw_config_state_t) == TGM_FW_CONFIG_STATE_BYTES);
BUILD_ASSERT(sizeof(APP_VERSION_STRING) <= sizeof(fw_version));

static struct tgm_service_cb *tgm_service_cb = NULL;
static struct k_work battery_notify_work;
static struct k_work ppg_start_work;
static struct k_work acc_start_work;
static struct k_work_delayable link_keepalive_work;
static struct k_work_delayable connect_probe_end_work;

static bool device_worn;
static bool connect_probe_active;
static bool charger_on_dock;
static uint8_t user_device_mode;
static uint16_t debug_reboot_interval_s = CONFIG_TGM_DEBUG_REBOOT_INTERVAL_S;
static int64_t worn_temp_lockout_until_ms;

/* IR pulse worn (Automatic): IIR DC + |AC|. PI on ≈ 0.0005, off ≈ 0.0002. */
static uint32_t ir_pulse_dc;
static uint32_t ir_pulse_ac_lp;
static bool ir_pulse_filt_init;
static bool ir_pulse_latched;
static int64_t ir_pulse_good_since_ms;
static int64_t ir_pulse_bad_since_ms;

#define IR_PULSE_DC_MIN 2000U
#define IR_PULSE_ON_MS 2500
#define IR_PULSE_HOLD_MS 20000

static void ir_pulse_set_latched(bool on);
static void ir_pulse_reset(void);
static void ir_pulse_feed(const struct ppg_sample *samples, uint8_t n);

static void worn_temp_extend_lockout(int seconds)
{
	int64_t until = k_uptime_get() + (int64_t)seconds * 1000LL;

	if (until > worn_temp_lockout_until_ms)
	{
		worn_temp_lockout_until_ms = until;
	}
}

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
static bool streaming_allowed(void);
static bool ppg_mtu_blocks_stream(void);
static bool acc_mtu_blocks_stream(void);
static void tgm_service_resume_sensor_streams(void);

static ssize_t reject_prepared_or_command_write(uint8_t flags)
{
    if ((flags & (BT_GATT_WRITE_FLAG_PREPARE | BT_GATT_WRITE_FLAG_EXECUTE |
                  BT_GATT_WRITE_FLAG_CMD)) != 0u)
    {
        return BT_GATT_ERR(BT_ATT_ERR_NOT_SUPPORTED);
    }

    return 0;
}

enum fw_cfg_opcode_t {
    FW_CFG_SET_LED_PA = 0x01,
    FW_CFG_SET_STATS_PERIOD_S = 0x02,
    FW_CFG_REQUEST_CONN_PARAM_UPDATE = 0x03,
    FW_CFG_SET_BATTERY_INTERVAL_S = 0x04,
    FW_CFG_SET_TEMP_INTERVAL_S = 0x05,
    FW_CFG_SET_STREAM_ENABLE_MASK = 0x06,
    FW_CFG_REQUEST_STATUS_SNAPSHOT = 0x07,
    FW_CFG_RESTART_CONNECT_PROBE = 0x08,
    FW_CFG_SET_USER_DEVICE_MODE = 0x09,
    FW_CFG_SET_DEBUG_REBOOT_INTERVAL_S = 0x0A,
};

static void stream_stats_work_handler(struct k_work *work);

static void fw_log_open_work_handler(struct k_work *work);
static struct k_work_delayable fw_log_open_work;

static void status_open_work_handler(struct k_work *work);
static struct k_work_delayable status_open_work;

void tgm_service_refresh_chrsts_bench(void)
{
    struct tgm_chrsts_bench_t sample;

    if (!tgm_service_cb || !tgm_service_cb->chrsts_bench_cb)
    {
        return;
    }

    memset(&sample, 0, sizeof(sample));
    tgm_service_cb->chrsts_bench_cb(&sample);

    fw_config_state.version = 2;
    fw_config_state.chrsts_phy = sample.phy;
    fw_config_state.chrsts_dt = sample.dt;
    fw_config_state.chrsts_active = sample.active;
    fw_config_state.chrsts_maj = sample.maj;
    fw_config_state.chrsts_dock_raw = sample.dock_raw;
    fw_config_state.chrsts_on_dock = sample.on_dock;
    fw_config_state.chrsts_stable_s = sample.stable_s;
    fw_config_state.chrsts_mismatch = sample.mismatch;
}

static ssize_t write_fw_config(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                               const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(attr);

    {
        const ssize_t rejected = reject_prepared_or_command_write(flags);

        if (rejected != 0)
        {
            return rejected;
        }
    }

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
            }
            else if (led_id == 1)
            {
                led = PPG_LED_IR;
            }
            else if (led_id == 2)
            {
                led = PPG_LED_RED;
            }
            else
            {
                return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
            }

            const int err = ppg_set_led_pa(led, pa);
            tgm_service_fw_log_printf("fw: cfg led=%u pa=%u err=%d", led_id, pa, err);
            if (err)
            {
                return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
            }
            if (led_id == 0)
            {
                fw_config_state.led_green_pa = pa;
            }
            else if (led_id == 1)
            {
                fw_config_state.led_ir_pa = pa;
            }
            else
            {
                fw_config_state.led_red_pa = pa;
            }
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
            if (err && err != -EALREADY)
            {
                return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
            }
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
        tgm_service_fw_log_printf(
            "fw: cfg stream_enable_mask req=0x%02x applied=0x%02x (OR 0x03 keeps PPG/ACC)",
            b[1], fw_config_state.stream_enable_mask);
        if ((fw_config_state.stream_enable_mask & 0x01) == 0)
        {
            (void)ppg_stop_streaming("stream-mask");
        }
        if ((fw_config_state.stream_enable_mask & 0x02) == 0)
        {
            (void)acc_stop();
        }
        if ((fw_config_state.stream_enable_mask & 0x01) != 0 && streaming_allowed() &&
            !ppg_mtu_blocks_stream())
        {
            (void)ppg_start();
        }
        else if ((fw_config_state.stream_enable_mask & 0x01) != 0 && streaming_allowed())
        {
            tgm_service_fw_log_printf("fw: ppg_start skip mtu=%u need=%u", att_mtu,
                                      TGM_PPG_FULL_FRAME_MIN_ATT_MTU);
        }
        if ((fw_config_state.stream_enable_mask & 0x02) != 0 && streaming_allowed() &&
            !acc_mtu_blocks_stream())
        {
            (void)acc_start();
        }
        else if ((fw_config_state.stream_enable_mask & 0x02) != 0 && streaming_allowed())
        {
            tgm_service_fw_log_printf("fw: acc_start skip mtu=%u need=%u", att_mtu,
                                      TGM_ACC_FULL_FRAME_MIN_ATT_MTU);
        }
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

    case FW_CFG_SET_USER_DEVICE_MODE:
        if (len != 2u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        if (b[1] > 3u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
        }
        user_device_mode = b[1];
        if (b[1] == 3u)
        {
            mode3_pad_grace_until_ms = k_uptime_get() + MODE3_PAD_GRACE_MS;
            mode3_soft_floor_hold_until_ms = k_uptime_get() + MODE3_SOFT_FLOOR_HOLD_MS;
            tgm_service_fw_log_printf("fw: mode3 pad grace %d ms hold %d s",
                                      MODE3_PAD_GRACE_MS, MODE3_SOFT_FLOOR_HOLD_MS / 1000);
        }
        else
        {
            mode3_pad_grace_until_ms = 0;
            mode3_soft_floor_hold_until_ms = 0;
        }
        tgm_service_fw_log_printf("fw: cfg user_mode=%u (0=auto 1=charger 2=idle 3=worn)",
                                  user_device_mode);
        if (tgm_service_cb && tgm_service_cb->user_mode_changed_cb)
        {
            tgm_service_cb->user_mode_changed_cb();
        }
        tgm_service_emit_diagnostic_snapshot();
        break;

    case FW_CFG_SET_DEBUG_REBOOT_INTERVAL_S:
        if (len != 3u)
        {
            return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
        }
        debug_reboot_interval_s = (uint16_t)b[1] | ((uint16_t)b[2] << 8);
        tgm_service_fw_log_printf("fw: cfg debug_reboot_s=%u", debug_reboot_interval_s);
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
    tgm_service_fw_log_printf(
        "fw: rate mtu=%u ppg_ovf=%u ppg_backlog=%u acc_ovf=%u acc_backlog=%u",
        att_mtu, ppg_sensor_fifo_overflow_events(), ppg_sensor_fifo_backlog_events(),
        acc_sensor_fifo_overflow_events(), acc_sensor_fifo_backlog_events());

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

    const uint16_t max_payload = att_mtu > 3u ? (uint16_t)(att_mtu - 3u) : 0u;

    if (len > max_payload)
    {
        LOG_WRN("GATT notify blocked (%s): len=%u mtu=%u", label, len, att_mtu);
        return -EMSGSIZE;
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
    /* Sensors follow BLE + CCC, not worn. Below 5% or gauge empty they stay off. */
    if (battery_below_soft_floor()) {
        return false;
    }

    return ble_is_connected() || connect_probe_active;
}

static bool ppg_mtu_blocks_stream(void)
{
    return att_mtu < TGM_PPG_FULL_FRAME_MIN_ATT_MTU;
}

static bool acc_mtu_blocks_stream(void)
{
    return att_mtu < TGM_ACC_FULL_FRAME_MIN_ATT_MTU;
}

static int gatt_notify_admitted(const struct bt_gatt_attr *attr, const void *data, uint16_t len,
                                const char *label, bool high_priority)
{
    int err = gatt_notify_checked(attr, data, len, label, high_priority);

    if (err == -ENOMEM || err == -ENOBUFS)
    {
        err = gatt_notify_checked(attr, data, len, label, high_priority);
    }

    return err;
}

bool tgm_service_ppg_notify_active(void)
{
    return notify_ppg_data && streaming_allowed();
}

bool tgm_service_ppg_stream_live(void)
{
    if (!notify_ppg_data || last_ppg_notify_ok_ms <= 0)
    {
        return false;
    }

    return (k_uptime_get() - last_ppg_notify_ok_ms) < 2000;
}

/** Firmware log still reaching the phone means the link is up. */
static bool fw_log_link_alive(int64_t now)
{
    return notify_fw_log && last_fwlog_notify_ok_ms > 0 &&
           (now - last_fwlog_notify_ok_ms) < SENSOR_NOTIFY_STALL_MS;
}

static void log_ppg_quiet_if_due(int64_t now)
{
    const int64_t last = last_ppg_notify_ok_ms > 0 ? last_ppg_notify_ok_ms
                                                   : sensor_stream_watch_ms;

    if (last <= 0 || (now - last) < 1000)
    {
        return;
    }

    if (ppg_quiet_log_ms > 0 && (now - ppg_quiet_log_ms) < 1000)
    {
        return;
    }

    ppg_quiet_log_ms = now;
    tgm_service_fw_log_printf("fw: ppg quiet err=%d empty=%u",
                              ppg_quiet_err, ppg_quiet_empty);
}

void tgm_service_note_ppg_read(int err, bool empty)
{
    if (err)
    {
        ppg_quiet_err = err;
        ppg_quiet_empty = 0;
    }
    else if (empty)
    {
        ppg_quiet_err = 0;
        ppg_quiet_empty = 1;
    }
}

bool tgm_service_sensor_stream_stale(void)
{
    const bool watch_ppg = notify_ppg_data && !ppg_mtu_blocks_stream();
    const bool watch_acc = notify_acc_data && !acc_mtu_blocks_stream();

    if (!ble_is_connected() || !(watch_ppg || watch_acc))
    {
        return false;
    }

    /* Below 5%: sensors are meant to be silent; do not drop the BLE link. */
    if (battery_below_soft_floor())
    {
        return false;
    }

    const int64_t now = k_uptime_get();

    if (fw_log_link_alive(now))
    {
        return false;
    }

    /* AFE still producing samples is not a zombie by itself — recover
     * sample silence here. GATT-idle zombies use tgm_service_link_notify_idle.
     */
    if (watch_ppg)
    {
        int64_t last = last_ppg_sample_ms > 0 ? last_ppg_sample_ms
                                              : sensor_stream_watch_ms;
        if (last <= 0)
        {
            return false;
        }
        return (now - last) >= SENSOR_NOTIFY_STALL_MS;
    }

    int64_t last = last_acc_sample_ms > 0 ? last_acc_sample_ms
                                          : sensor_stream_watch_ms;
    if (last <= 0)
    {
        return false;
    }
    return (now - last) >= SENSOR_NOTIFY_STALL_MS;
}

bool tgm_service_link_notify_idle(void)
{
    const bool watch_ppg = notify_ppg_data && !ppg_mtu_blocks_stream();
    const bool watch_acc = notify_acc_data && !acc_mtu_blocks_stream();

    if (!ble_is_connected() || !(watch_ppg || watch_acc))
    {
        return false;
    }

    /* Below 5%: sensors are meant to be silent; do not drop the BLE link. */
    if (battery_below_soft_floor())
    {
        return false;
    }

    const int64_t now = k_uptime_get();

    if (fw_log_link_alive(now))
    {
        return false;
    }

    int64_t last;

    /* PPG CCC: only PPG notify success proves a live central. ACC/battery
     * must not keep a dead PPG ATT path alive (1.0.77). A firmware-log
     * notify in the last 4 s does, so a quiet FIFO is not a dead radio.
     */
    if (watch_ppg)
    {
        last = last_ppg_notify_ok_ms > 0 ? last_ppg_notify_ok_ms
                                         : sensor_stream_watch_ms;
    }
    else
    {
        last = last_acc_notify_ok_ms > 0 ? last_acc_notify_ok_ms
                                         : sensor_stream_watch_ms;
    }

    if (last <= 0)
    {
        return false;
    }

    return (now - last) >= SENSOR_NOTIFY_STALL_MS;
}

static void acc_motion_reset(void)
{
    last_acc_motion_ms = k_uptime_get();
    acc_motion_have_ref = false;
}

static void acc_motion_feed(const struct acc_sample *acc_data, uint8_t sample_cnt)
{
    if (!acc_data || sample_cnt == 0)
    {
        return;
    }

    const int64_t now = k_uptime_get();

    for (uint8_t i = 0; i < sample_cnt; i++)
    {
        const int16_t x = acc_data[i].x;
        const int16_t y = acc_data[i].y;
        const int16_t z = acc_data[i].z;

        if (!acc_motion_have_ref)
        {
            last_acc_motion_x = x;
            last_acc_motion_y = y;
            last_acc_motion_z = z;
            last_acc_motion_ms = now;
            acc_motion_have_ref = true;
            continue;
        }

        const int32_t dx = (int32_t)x - (int32_t)last_acc_motion_x;
        const int32_t dy = (int32_t)y - (int32_t)last_acc_motion_y;
        const int32_t dz = (int32_t)z - (int32_t)last_acc_motion_z;
        const int32_t adx = dx < 0 ? -dx : dx;
        const int32_t ady = dy < 0 ? -dy : dy;
        const int32_t adz = dz < 0 ? -dz : dz;

        if (adx >= ACC_MOTION_DELTA_LSB || ady >= ACC_MOTION_DELTA_LSB ||
            adz >= ACC_MOTION_DELTA_LSB)
        {
            last_acc_motion_x = x;
            last_acc_motion_y = y;
            last_acc_motion_z = z;
            last_acc_motion_ms = now;
        }
    }
}

/** PPG AFE still producing samples (sensing), regardless of GATT notify success. */
static bool ppg_sensing_active(void)
{
    if (!notify_ppg_data || last_ppg_sample_ms <= 0)
    {
        return false;
    }

    return (k_uptime_get() - last_ppg_sample_ms) < SENSOR_NOTIFY_STALL_MS;
}

/**
 * Desk / bench abandon: PPG still sensing, ACC flat for 10 min, and not worn.
 * Overnight on-temple (worn=1) stays connected through stillness.
 */
static bool tgm_service_ppg_sensing_no_motion(void)
{
    if (!ble_is_connected() || !ppg_sensing_active())
    {
        return false;
    }

    /* Still on the person — do not recycle for sleep stillness. */
    if (device_worn)
    {
        return false;
    }

    if (battery_below_soft_floor())
    {
        return false;
    }

    /* Need ACC samples to judge motion (CCC on + FIFO). */
    if (!notify_acc_data || last_acc_sample_ms <= 0 || last_acc_motion_ms <= 0)
    {
        return false;
    }

    return (k_uptime_get() - last_acc_motion_ms) >= ACC_NO_MOTION_RECOVER_MS;
}

void tgm_service_clear_optical_leds(void)
{
    tgm_service_suspend_sensor_streams();
}

void tgm_service_try_start_ppg_acc(void)
{
    tgm_service_resume_sensor_streams();
}

bool tgm_service_sensor_notify_enabled(void)
{
    return notify_ppg_data || notify_acc_data;
}

bool tgm_service_mode3_pad_grace_active(void)
{
    return mode3_pad_grace_until_ms > 0 && k_uptime_get() < mode3_pad_grace_until_ms;
}

bool tgm_service_mode3_soft_floor_hold_active(void)
{
    return mode3_soft_floor_hold_until_ms > 0 &&
           k_uptime_get() < mode3_soft_floor_hold_until_ms;
}

bool tgm_service_connect_probe_active(void)
{
    return connect_probe_active;
}

bool tgm_service_worn_from_temperature_allowed(void)
{
	if (connect_probe_active)
	{
		return false;
	}

	if (CONFIG_TGM_POST_PROBE_WORN_LOCKOUT_S <= 0)
	{
		return true;
	}

	return k_uptime_get() >= worn_temp_lockout_until_ms;
}

static void tgm_service_probe_begin(void)
{
    if (connect_probe_active)
    {
        return;
    }

    connect_probe_active = true;

    if (CONFIG_TGM_CONNECT_PROBE_DURATION_S > 0 &&
        CONFIG_TGM_POST_PROBE_WORN_LOCKOUT_S > 0)
    {
        worn_temp_extend_lockout(CONFIG_TGM_CONNECT_PROBE_DURATION_S +
                                 CONFIG_TGM_POST_PROBE_WORN_LOCKOUT_S);
    }

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

    if (CONFIG_TGM_POST_PROBE_WORN_LOCKOUT_S > 0)
    {
        worn_temp_extend_lockout(CONFIG_TGM_POST_PROBE_WORN_LOCKOUT_S);
    }

    LOG_INF("Connect probe ended (worn=%d)", device_worn);

    if (tgm_service_cb && tgm_service_cb->probe_end_cb)
    {
        tgm_service_cb->probe_end_cb();
    }

    if (!ble_is_connected())
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

void tgm_service_note_att_mtu(uint16_t mtu)
{
    if (mtu < TGM_ATT_MTU_DEFAULT)
    {
        mtu = TGM_ATT_MTU_DEFAULT;
    }

    if (mtu == att_mtu)
    {
        return;
    }

    att_mtu = mtu;
    LOG_INF("ATT MTU %u", mtu);
    tgm_service_fw_log_printf("fw: att mtu=%u ppg_need=%u acc_need=%u", att_mtu,
                              TGM_PPG_FULL_FRAME_MIN_ATT_MTU, TGM_ACC_FULL_FRAME_MIN_ATT_MTU);
    tgm_service_resume_sensor_streams();
}

static void tgm_service_suspend_sensor_streams(void)
{
    ir_pulse_reset();
    (void)ppg_set_led_pa(PPG_LED_RED, 0);
    (void)ppg_set_led_pa(PPG_LED_IR, 0);
    (void)ppg_set_led_pa(PPG_LED_GREEN, 0);
    ppg_set_notify_poll(false);
    ppg_batch_n = 0;
    acc_batch_n = 0;
    (void)ppg_stop();
    (void)acc_stop();
}

void tgm_service_set_on_dock(bool on_dock)
{
    charger_on_dock = on_dock;
    battery_profile_touch();
}

bool tgm_service_is_on_dock(void)
{
    return charger_on_dock;
}

uint8_t tgm_service_get_user_device_mode(void)
{
    return user_device_mode;
}

void tgm_service_set_user_device_mode(uint8_t mode)
{
    if (mode > 3u) {
        return;
    }
    if (user_device_mode == mode) {
        return;
    }
    user_device_mode = mode;
    if (mode != 3u) {
        mode3_pad_grace_until_ms = 0;
        mode3_soft_floor_hold_until_ms = 0;
    }
    tgm_service_fw_log_printf("fw: user_mode=%u (internal)", user_device_mode);
}

uint16_t tgm_service_get_debug_reboot_interval_s(void)
{
    return debug_reboot_interval_s;
}

void tgm_service_set_device_worn(bool worn)
{
    if (device_worn == worn)
    {
        return;
    }

    device_worn = worn;
    LOG_INF("Worn status: %s (PPG/ACC follow BLE, not worn)",
            worn ? "on-body" : "off-body");
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

    if (!notify_ppg_data || !streaming_allowed() || ppg_mtu_blocks_stream())
    {
        tgm_service_fw_log_printf("fw: ppg_start skip ccc=%u conn=%u floor=%u mtu=%u",
                                  notify_ppg_data ? 1u : 0u,
                                  ble_is_connected() ? 1u : 0u,
                                  battery_below_soft_floor() ? 1u : 0u,
                                  att_mtu);
        return;
    }

    sensor_stream_watch_ms = k_uptime_get();
    last_ppg_notify_ok_ms = 0;
    last_ppg_sample_ms = 0;

    int err = ppg_start();
    if (err)
    {
        LOG_WRN("Deferred PPG start failed (%d)", err);
        tgm_service_fw_log_printf("fw: ppg_start fail %d", err);
    }
    else
    {
        tgm_service_fw_log_printf("fw: ppg_start ok");
    }

    ppg_set_notify_poll(true);
    ppg_poll_once();

    if (tgm_service_cb && tgm_service_cb->ppg_stream_start_cb)
    {
        tgm_service_cb->ppg_stream_start_cb();
    }
}

static void tgm_service_acc_start_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_acc_data || !streaming_allowed() || acc_mtu_blocks_stream())
    {
        acc_set_notify_poll(false);
        tgm_service_fw_log_printf("fw: acc_start skip ccc=%u conn=%u floor=%u mtu=%u",
                                  notify_acc_data ? 1u : 0u,
                                  ble_is_connected() ? 1u : 0u,
                                  battery_below_soft_floor() ? 1u : 0u,
                                  att_mtu);
        return;
    }

    if (sensor_stream_watch_ms == 0 || !notify_ppg_data)
    {
        sensor_stream_watch_ms = k_uptime_get();
    }
    last_acc_notify_ok_ms = 0;
    last_acc_sample_ms = 0;

    int err = acc_start();
    if (err)
    {
        LOG_WRN("Deferred ACC start failed (%d)", err);
        tgm_service_fw_log_printf("fw: acc_start fail %d", err);
        acc_set_notify_poll(false);
        return;
    }

    /* Polling bypasses a bad/unverified INT_ACC mapping during Gen 2 bring-up. */
    acc_set_notify_poll(true);
    acc_poll_once();
    tgm_service_fw_log_printf("fw: acc_start ok");
}

static void tgm_service_ccc_ppg_data_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_ppg_data = (value == BT_GATT_CCC_NOTIFY);
    battery_profile_touch();
    LOG_INF("PPG notifications %s", notify_ppg_data ? "enabled" : "disabled");
    if (notify_ppg_data)
    {
        link_keepalive_arm();
        sensor_stream_watch_ms = k_uptime_get();
        last_ppg_notify_ok_ms = 0;
        last_ppg_sample_ms = 0;
        acc_motion_reset();
        (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
        (void)k_work_submit(&ppg_start_work);
    }
    else
    {
        ppg_held_len = 0;
        ppg_batch_n = 0;
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
        if (sensor_stream_watch_ms == 0)
        {
            sensor_stream_watch_ms = k_uptime_get();
        }
        last_acc_notify_ok_ms = 0;
        last_acc_sample_ms = 0;
        acc_motion_reset();
        (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
        (void)k_work_submit(&acc_start_work);
    }
    else
    {
        acc_held_len = 0;
        acc_batch_n = 0;
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
    LOG_INF("Device status notifications %s", value == BT_GATT_CCC_NOTIFY ? "enabled" : "disabled");
    notify_status = (value == BT_GATT_CCC_NOTIFY);
    if (notify_status)
    {
        link_keepalive_arm();
        (void)k_work_reschedule(&status_open_work, K_MSEC(250));
    }
    else
    {
        (void)k_work_cancel_delayable(&status_open_work);
    }
}

static void status_open_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_status)
    {
        return;
    }

    tgm_service_refresh_chrsts_bench();

    if (tgm_service_cb && tgm_service_cb->diagnostic_snapshot_cb)
    {
        tgm_service_cb->diagnostic_snapshot_cb();
    }
}

static void fw_log_open_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!notify_fw_log)
    {
        return;
    }

    tgm_service_refresh_chrsts_bench();
	tgm_service_fw_log_printf("fw: log stream open");
	i2c_pin_scan_replay();
    tgm_service_fw_log_printf(
        "fw: chrsts open phy=%u dt=%u act=%u maj=%u raw=%u on=%u stab=%u mis=%u",
        fw_config_state.chrsts_phy, fw_config_state.chrsts_dt,
        fw_config_state.chrsts_active, fw_config_state.chrsts_maj,
        fw_config_state.chrsts_dock_raw, fw_config_state.chrsts_on_dock,
        fw_config_state.chrsts_stable_s, fw_config_state.chrsts_mismatch);
}

static void tgm_service_ccc_fw_log_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    notify_fw_log = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Firmware log notifications %s", notify_fw_log ? "enabled" : "disabled");
    if (notify_fw_log)
    {
        link_keepalive_arm();
        (void)k_work_reschedule(&fw_log_open_work, K_MSEC(250));
        if (stream_stats_period_s > 0)
        {
            k_work_reschedule(&stream_stats_work, K_SECONDS(stream_stats_period_s));
        }
    }
    else
    {
        (void)k_work_cancel_delayable(&fw_log_open_work);
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

static void encode_fw_config_state(uint8_t out[TGM_FW_CONFIG_STATE_BYTES])
{
    out[0] = fw_config_state.version;
    out[1] = fw_config_state.led_green_pa;
    out[2] = fw_config_state.led_ir_pa;
    out[3] = fw_config_state.led_red_pa;
    out[4] = fw_config_state.stream_stats_period_s;
    out[5] = fw_config_state.battery_interval_s;
    out[6] = fw_config_state.temp_interval_s;
    out[7] = fw_config_state.stream_enable_mask;
    out[8] = fw_config_state.chrsts_phy;
    out[9] = fw_config_state.chrsts_dt;
    out[10] = fw_config_state.chrsts_active;
    out[11] = fw_config_state.chrsts_maj;
    out[12] = fw_config_state.chrsts_dock_raw;
    out[13] = fw_config_state.chrsts_on_dock;
    out[14] = fw_config_state.chrsts_stable_s;
    out[15] = fw_config_state.chrsts_mismatch;
}

static void encode_status(void)
{
    status_wire[0] = device_status.on_dock;
    status_wire[1] = device_status.worn;
    status_wire[2] = device_status.device_state;
    status_wire[3] = device_status.battery_pct;
    status_wire[4] = device_status.charge_active;
}

static ssize_t get_fw_config_state(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                   void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);

    tgm_service_refresh_chrsts_bench();
    encode_fw_config_state(fw_config_wire);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, fw_config_wire,
                             TGM_FW_CONFIG_STATE_BYTES);
}

// Callback function to get the battery value when the client reads this value
static ssize_t get_bat_value(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);

    LOG_INF("Reading battery value");

    if (tgm_service_cb && tgm_service_cb->bat_cb)
    {
        bat_value = tgm_service_cb->bat_cb();
    }

    if (bat_value <= 0)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    sys_put_le32((uint32_t)bat_value, bat_wire);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, bat_wire, TGM_BATTERY_VALUE_BYTES);
}

// Callback function to get the UUID value when the client reads this value
static ssize_t get_uuid_value(struct bt_conn *conn, const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset)
{
    ARG_UNUSED(attr);

    LOG_INF("Reading UUID value");

#if defined(CONFIG_SOC_SERIES_NRF54LX)
    uuid_value = ((uint64_t)NRF_FICR->INFO.DEVICEID[1] << 32) | NRF_FICR->INFO.DEVICEID[0];
#else
    uuid_value = ((uint64_t)NRF_FICR->DEVICEID[1] << 32) | NRF_FICR->DEVICEID[0];
#endif
    sys_put_le64(uuid_value, id_wire);
    return bt_gatt_attr_read(conn, attr, buf, len, offset, id_wire, TGM_DEVICE_ID_VALUE_BYTES);
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
    ARG_UNUSED(attr);

    tgm_service_refresh_chrsts_bench();

    LOG_INF("Reading device status");
    encode_status();
    return bt_gatt_attr_read(conn, attr, buf, len, offset, status_wire, TGM_STATUS_VALUE_BYTES);
}

// Callback function to read the PPG register when the client writes to this value
static ssize_t read_ppg_reg(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);

    {
        const ssize_t rejected = reject_prepared_or_command_write(flags);

        if (rejected != 0)
        {
            return rejected;
        }
    }

    if (offset != 0)
    {
        LOG_DBG("Invalid offset for PPG register read");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 1u)
    {
        LOG_DBG("Invalid length for PPG register read");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t ppg_reg = *((const uint8_t *)buf);
    const int err = ppg_read_reg(ppg_reg);

    LOG_INF("Reading PPG register 0x%02x", ppg_reg);
    if (err)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

// Callback function to write the PPG register when the client writes to this value
static ssize_t write_ppg_reg(struct bt_conn *conn, const struct bt_gatt_attr *attr, const void *buf, uint16_t len, uint16_t offset, uint8_t flags)
{
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);

    {
        const ssize_t rejected = reject_prepared_or_command_write(flags);

        if (rejected != 0)
        {
            return rejected;
        }
    }

    if (offset != 0)
    {
        LOG_DBG("Invalid offset for PPG register write");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_OFFSET);
    }

    if (len != 2u)
    {
        LOG_DBG("Invalid length for PPG register write");
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }

    const uint8_t ppg_reg = ((const uint8_t *)buf)[0];
    const uint8_t ppg_reg_data = ((const uint8_t *)buf)[1];
    const int err = ppg_write_reg(ppg_reg, ppg_reg_data);

    LOG_INF("Writing PPG register 0x%02x", ppg_reg);
    if (err == -EPERM)
    {
        return BT_GATT_ERR(BT_ATT_ERR_VALUE_NOT_ALLOWED);
    }
    if (err)
    {
        return BT_GATT_ERR(BT_ATT_ERR_UNLIKELY);
    }

    return len;
}

static void battery_usage_fill(uint8_t *out)
{
    struct battery_usage_telemetry_t tel;
    uint16_t mv;
    uint16_t mah;
    uint16_t mins;
    uint8_t flags = 0x01;

    battery_get_usage_telemetry(&tel);
    mv = tel.voltage_mv > 0xFFFF ? 0xFFFF : (uint16_t)tel.voltage_mv;
    mah = tel.mah_consumed_scaled > 0xFFFF ? 0xFFFF : (uint16_t)tel.mah_consumed_scaled;
    mins = tel.remaining_minutes >= 0xFFFF ? 0xFFFF : (uint16_t)tel.remaining_minutes;
    if (charger_on_dock) {
        flags |= 0x02;
        mins = 0xFFFF;
    }

    out[0] = (uint8_t)(mv & 0xFF);
    out[1] = (uint8_t)((mv >> 8) & 0xFF);
    out[2] = tel.percent;
    out[3] = flags;
    out[4] = (uint8_t)(mah & 0xFF);
    out[5] = (uint8_t)((mah >> 8) & 0xFF);
    out[6] = (uint8_t)(mins & 0xFF);
    out[7] = (uint8_t)((mins >> 8) & 0xFF);
}

static ssize_t get_battery_usage(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                 void *buf, uint16_t len, uint16_t offset)
{
    battery_usage_fill(battery_usage_payload);
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
                             battery_usage_payload, sizeof(battery_usage_payload));
}

static void tgm_service_ccc_battery_usage_cfg_changed(const struct bt_gatt_attr *attr, uint16_t value)
{
    ARG_UNUSED(attr);
    notify_battery_usage = (value == BT_GATT_CCC_NOTIFY);
    LOG_INF("Battery usage notifications %s", notify_battery_usage ? "enabled" : "disabled");
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
    BT_GATT_CHARACTERISTIC(
        BT_UUID_TGM_BATTERY_USAGE,
        BT_GATT_CHRC_READ | BT_GATT_CHRC_NOTIFY,
        BT_GATT_PERM_READ,
        get_battery_usage, NULL,
        battery_usage_payload),
    BT_GATT_CCC(tgm_service_ccc_battery_usage_cfg_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
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
    TGM_ATTR_BATTERY_USAGE_VALUE = 35,
};

static int64_t newest_subscribed_notify_ok_ms(void)
{
    int64_t newest = 0;

    if (notify_ppg_data && last_ppg_notify_ok_ms > newest)
    {
        newest = last_ppg_notify_ok_ms;
    }
    if (notify_acc_data && last_acc_notify_ok_ms > newest)
    {
        newest = last_acc_notify_ok_ms;
    }
    if (notify_battery && last_bat_notify_ok_ms > newest)
    {
        newest = last_bat_notify_ok_ms;
    }
    if (notify_fw_log && last_fwlog_notify_ok_ms > newest)
    {
        newest = last_fwlog_notify_ok_ms;
    }
    return newest;
}

static void sensor_health_work_handler(struct k_work *work)
{
    ARG_UNUSED(work);

    if (!ble_is_connected() || !(notify_ppg_data || notify_acc_data))
    {
        return;
    }

    const bool watch_ppg = notify_ppg_data && !ppg_mtu_blocks_stream();
    const bool watch_acc = notify_acc_data && !acc_mtu_blocks_stream();

    if (!watch_ppg && !watch_acc)
    {
        (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
        return;
    }

    const int64_t now = k_uptime_get();

    if (watch_ppg)
    {
        /* A PPG notify in the last 2 s, or a firmware-log notify in the
         * last 4 s, is a live link. Log the quiet PPG read and leave it up.
         */
        if (!tgm_service_ppg_stream_live())
        {
            log_ppg_quiet_if_due(now);
        }

        if (tgm_service_ppg_stream_live() || fw_log_link_alive(now))
        {
            (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
            return;
        }

        const int64_t last_ppg = (last_ppg_sample_ms > 0) ? last_ppg_sample_ms
                                                         : sensor_stream_watch_ms;

        if (last_ppg > 0 && (now - last_ppg) >= SENSOR_NOTIFY_STALL_MS)
        {
            LOG_WRN("PPG sample stall %d ms — recovering BLE", (int)(now - last_ppg));
            tgm_service_fw_log_printf("fw: ppg sample stall %d", (int)(now - last_ppg));
            ble_force_recover("ppg sample stall");
            return;
        }
        /* AFE can keep pulsing with no central. Drop that zombie even if
         * samples are fresh. A live Protocol A link has successful PPG notifies.
         */
        if (tgm_service_link_notify_idle())
        {
            LOG_WRN("PPG notify idle — recovering BLE");
            tgm_service_fw_log_printf("fw: ppg notify idle");
            ble_force_recover("ppg notify idle");
            return;
        }
        /* Desk/bench: off-body (worn=0), PPG still sensing, ACC flat 10 min. */
        if (tgm_service_ppg_sensing_no_motion())
        {
            LOG_WRN("Off-body PPG sensing, no ACC motion 10 min — recovering BLE");
            tgm_service_fw_log_printf("fw: desk no motion 10m");
            ble_force_recover("desk no motion");
            return;
        }
        (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
        return;
    }

    const int64_t last_any = newest_subscribed_notify_ok_ms();
    const int64_t last_all = (last_any > 0) ? last_any : sensor_stream_watch_ms;

    if (last_all > 0 && (now - last_all) >= SENSOR_NOTIFY_STALL_MS)
    {
        LOG_WRN("All notify stall %d ms — recovering BLE", (int)(now - last_all));
        tgm_service_fw_log_printf("fw: all notify stall %d", (int)(now - last_all));
        ble_force_recover("all notify stall");
        return;
    }

    (void)k_work_reschedule(&sensor_health_work, K_SECONDS(1));
}

static int tgm_wire_golden_check(void)
{
    uint8_t ppg[16];
    uint8_t acc[10];
    uint8_t temp[TGM_TEMP_VALUE_BYTES];
    static const uint8_t ppg_expect[16] = {
        0x04, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00,
        0x45, 0x23, 0x01, 0x00, 0xff, 0xff, 0x07, 0x00};
    static const uint8_t acc_expect[10] = {
        0x04, 0x03, 0x02, 0x01, 0xff, 0xff, 0x34, 0x12, 0x00, 0x80};
    static const uint8_t temp_expect[TGM_TEMP_VALUE_BYTES] = {
        0x01, 0x00, 0x00, 0x00, 0x59, 0x08, 0x00, 0x00};

    sys_put_le32(0x01020304u, ppg);
    sys_put_le32(1u, &ppg[4]);
    sys_put_le32(0x12345u, &ppg[8]);
    sys_put_le32(0x7FFFFu, &ppg[12]);
    sys_put_le32(0x01020304u, acc);
    sys_put_le16((uint16_t)(int16_t)-1, &acc[4]);
    sys_put_le16(0x1234u, &acc[6]);
    sys_put_le16((uint16_t)(int16_t)-32768, &acc[8]);
    sys_put_le32(1u, temp);
    sys_put_le16(2137u, &temp[4]);
    temp[6] = 0;
    temp[7] = 0;

    if (memcmp(ppg, ppg_expect, sizeof(ppg_expect)) != 0 ||
        memcmp(acc, acc_expect, sizeof(acc_expect)) != 0 ||
        memcmp(temp, temp_expect, sizeof(temp_expect)) != 0)
    {
        return -EINVAL;
    }

    return 0;
}

int tgm_service_init(struct tgm_service_cb *callbacks)
{
    const int golden = tgm_wire_golden_check();

    if (golden)
    {
        LOG_ERR("TGM wire layout does not match the contract vectors");
        return golden;
    }

    if (CONFIG_BATTERY_MEASUREMENT_INTERVAL > 255)
    {
        LOG_WRN("v2 battery_interval_s=%u cannot represent acquisition interval %u s",
                fw_config_state.battery_interval_s, CONFIG_BATTERY_MEASUREMENT_INTERVAL);
    }

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
    k_work_init_delayable(&sensor_health_work, sensor_health_work_handler);
    k_work_init_delayable(&fw_log_open_work, fw_log_open_work_handler);
    k_work_init_delayable(&status_open_work, status_open_work_handler);
    stream_stats_period_s = fw_config_state.stream_stats_period_s;

    return 0;
}

void tgm_service_on_connected(void)
{
    att_mtu = TGM_ATT_MTU_DEFAULT;
    gatt_tx_quiet_until_ms = 0;
    link_keepalive_arm();

    if (CONFIG_TGM_CONNECT_PROBE_DURATION_S > 0 && !charger_on_dock)
    {
        tgm_service_probe_begin();
        (void)k_work_reschedule(&connect_probe_end_work,
                                K_SECONDS(CONFIG_TGM_CONNECT_PROBE_DURATION_S));
    }
    else
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
    ppg_held_len = 0;
    acc_held_len = 0;
    ppg_batch_n = 0;
    acc_batch_n = 0;
    notify_temp_data = false;
    notify_battery = false;
    notify_read_ppg_reg = false;
    notify_write_ppg_reg = false;
    notify_status = false;
    notify_fw_log = false;
    notify_fw_config_state = false;
    notify_battery_usage = false;
    att_mtu = TGM_ATT_MTU_DEFAULT;
    battery_profile_touch();
    last_ppg_notify_ok_ms = 0;
    last_acc_notify_ok_ms = 0;
    last_ppg_sample_ms = 0;
    last_acc_sample_ms = 0;
    last_acc_motion_ms = 0;
    acc_motion_have_ref = false;
    last_bat_notify_ok_ms = 0;
    last_fwlog_notify_ok_ms = 0;
    sensor_stream_watch_ms = 0;

    k_work_cancel_delayable(&stream_stats_work);
    k_work_cancel_delayable(&sensor_health_work);
    k_work_cancel_delayable(&fw_log_open_work);
    k_work_cancel_delayable(&status_open_work);
    gatt_tx_quiet_until_ms = 0;
    ppg_set_notify_poll(false);
    k_work_cancel_delayable(&link_keepalive_work);

    /* Collect only while a central is connected. Stop FIFO IRQ / ACC on every
     * link drop, including worn — resume after reconnect + CCC.
     */
    tgm_service_suspend_sensor_streams();

    if (tgm_service_cb && tgm_service_cb->link_disconnected_cb)
    {
        tgm_service_cb->link_disconnected_cb();
    }
}

int tgm_service_send_battery_usage_notify(void)
{
    if (!notify_battery_usage) {
        return -EACCES;
    }

    battery_usage_fill(battery_usage_payload);
    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_BATTERY_USAGE_VALUE],
                               battery_usage_payload, sizeof(battery_usage_payload),
                               "battery-usage", false);
}

int tgm_service_send_battery_notify(int32_t battery_value)
{
    if (!notify_battery)
    {
        return -EACCES;
    }

    bat_value = battery_value;
    sys_put_le32((uint32_t)bat_value, bat_wire);

    const int err = gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_BAT_VALUE],
                                        bat_wire, TGM_BATTERY_VALUE_BYTES, "battery", true);
    if (!err)
    {
        last_bat_notify_ok_ms = k_uptime_get();
    }
    return err;
}

static void ir_pulse_set_latched(bool on)
{
    if (ir_pulse_latched == on) {
        return;
    }

    ir_pulse_latched = on;
    /* A wrapped filter step prints as billions. That number is not a pulse size. */
    if (ir_pulse_ac_lp > 100000u) {
        tgm_service_fw_log_printf("fw: ir_pulse worn=%u dc=%u",
                                  on ? 1u : 0u, ir_pulse_dc);
    } else {
        tgm_service_fw_log_printf("fw: ir_pulse worn=%u ac=%u dc=%u",
                                  on ? 1u : 0u, ir_pulse_ac_lp, ir_pulse_dc);
    }
    if (tgm_service_cb && tgm_service_cb->ir_pulse_worn_cb) {
        tgm_service_cb->ir_pulse_worn_cb(on);
    }
}

static void ir_pulse_reset(void)
{
    ir_pulse_filt_init = false;
    ir_pulse_dc = 0;
    ir_pulse_ac_lp = 0;
    ir_pulse_good_since_ms = 0;
    ir_pulse_bad_since_ms = 0;
    ir_pulse_set_latched(false);
}

static void ir_pulse_feed(const struct ppg_sample *samples, uint8_t n)
{
    const int64_t now = k_uptime_get();

    if (samples == NULL || n == 0) {
        return;
    }

    for (uint8_t i = 0; i < n; i++) {
        const uint32_t ir = samples[i].ir;

        if (!ir_pulse_filt_init) {
            ir_pulse_dc = ir;
            ir_pulse_ac_lp = 0;
            ir_pulse_filt_init = true;
            continue;
        }

        int32_t dc = (int32_t)ir_pulse_dc;
        int32_t x = (int32_t)ir;
        int32_t resid = x - dc;

        if (resid < 0) {
            resid = -resid;
        }
        dc += (x - dc) / 32;
        if (dc < 0) {
            dc = 0;
        }
        ir_pulse_dc = (uint32_t)dc;
        ir_pulse_ac_lp += ((uint32_t)resid - ir_pulse_ac_lp) / 8;
    }

    if (ir_pulse_dc < IR_PULSE_DC_MIN) {
        ir_pulse_good_since_ms = 0;
        if (ir_pulse_bad_since_ms == 0) {
            ir_pulse_bad_since_ms = now;
        }
        if ((now - ir_pulse_bad_since_ms) >= IR_PULSE_HOLD_MS) {
            ir_pulse_set_latched(false);
        }
        return;
    }

    /* PI on ≈ 0.0005 (ac*10000 >= dc*5); off ≈ 0.0002 (ac*10000 < dc*2). */
    const bool pulse_on = ((uint64_t)ir_pulse_ac_lp * 10000ULL) >=
                          ((uint64_t)ir_pulse_dc * 5ULL);
    const bool pulse_off = ((uint64_t)ir_pulse_ac_lp * 10000ULL) <
                           ((uint64_t)ir_pulse_dc * 2ULL);

    if (pulse_on) {
        ir_pulse_bad_since_ms = 0;
        if (ir_pulse_good_since_ms == 0) {
            ir_pulse_good_since_ms = now;
        }
        if ((now - ir_pulse_good_since_ms) >= IR_PULSE_ON_MS) {
            ir_pulse_set_latched(true);
        }
    } else if (pulse_off) {
        ir_pulse_good_since_ms = 0;
        if (ir_pulse_bad_since_ms == 0) {
            ir_pulse_bad_since_ms = now;
        }
        if ((now - ir_pulse_bad_since_ms) >= IR_PULSE_HOLD_MS) {
            ir_pulse_set_latched(false);
        }
    }
}

static int notify_counted_frame(const struct bt_gatt_attr *attr, uint8_t *wire, uint16_t len,
                                uint16_t *held_len, uint32_t *counter, uint32_t *ok,
                                uint32_t *fail, int32_t *last_err, const char *label)
{
    const int err = gatt_notify_admitted(attr, wire, len, label, true);

    if (err)
    {
        (*fail)++;
        *last_err = err;
        /* Keep this frame. A new payload with the same counter is a different error. */
        if ((err == -ENOMEM || err == -ENOBUFS) && *held_len == 0)
        {
            *held_len = len;
        }
        return err;
    }

    *held_len = 0;
    (*counter)++;
    (*ok)++;
    return 0;
}

static int ppg_emit_batch(void)
{
    uint16_t offset = TGM_FRAME_HEADER_BYTES;

    sys_put_le32(ppg_frame_counter, ppg_wire);
    tgm_service_ppg_data.frame_counter = ppg_frame_counter;
    for (uint8_t i = 0; i < CONFIG_PPG_SAMPLES_PER_FRAME; i++)
    {
        tgm_service_ppg_data.ppg_data[i] = ppg_batch[i];
        sys_put_le32(ppg_batch[i].red, &ppg_wire[offset]);
        sys_put_le32(ppg_batch[i].ir, &ppg_wire[offset + 4]);
        sys_put_le32(ppg_batch[i].green, &ppg_wire[offset + 8]);
        offset = (uint16_t)(offset + TGM_PPG_SAMPLE_STRIDE);
    }
    ppg_batch_n = 0;

    const int err = notify_counted_frame(&tgm_service_svc.attrs[TGM_ATTR_PPG_VALUE],
                                         ppg_wire, offset, &ppg_held_len,
                                         &ppg_frame_counter, &ppg_notify_ok,
                                         &ppg_notify_err, &ppg_last_notify_err, "ppg");
    ppg_quiet_empty = 0;
    if (err)
    {
        ppg_quiet_err = err;
    }
    else
    {
        last_ppg_notify_ok_ms = k_uptime_get();
        ppg_quiet_err = 0;
    }
    return err;
}

int tgm_service_send_ppg_notify(struct ppg_sample *ppg_data, uint8_t sample_cnt)
{
    uint8_t src = 0;

    if (!streaming_allowed() || !notify_ppg_data || ppg_mtu_blocks_stream())
    {
        ppg_batch_n = 0;
        return -EACCES;
    }

    if (ppg_held_len != 0)
    {
        const int err = notify_counted_frame(&tgm_service_svc.attrs[TGM_ATTR_PPG_VALUE],
                                              ppg_wire, ppg_held_len, &ppg_held_len,
                                              &ppg_frame_counter, &ppg_notify_ok,
                                              &ppg_notify_err, &ppg_last_notify_err, "ppg");

        ppg_quiet_empty = 0;
        if (err)
        {
            ppg_quiet_err = err;
            return err;
        }
        last_ppg_notify_ok_ms = k_uptime_get();
        ppg_quiet_err = 0;
    }

    if (sample_cnt == 0)
    {
        return 0;
    }

    if (sample_cnt > CONFIG_PPG_SAMPLES_PER_FRAME)
    {
        return -EINVAL;
    }

    while (src < sample_cnt)
    {
        const uint8_t room = (uint8_t)(CONFIG_PPG_SAMPLES_PER_FRAME - ppg_batch_n);
        uint8_t take = (uint8_t)(sample_cnt - src);

        if (take > room)
        {
            take = room;
        }

        memcpy(&ppg_batch[ppg_batch_n], &ppg_data[src], (size_t)take * sizeof(ppg_batch[0]));
        ir_pulse_feed(&ppg_data[src], take);
        ppg_batch_n = (uint8_t)(ppg_batch_n + take);
        src = (uint8_t)(src + take);
        last_ppg_sample_ms = k_uptime_get();
        ppg_quiet_empty = 0;

        if (ppg_batch_n < CONFIG_PPG_SAMPLES_PER_FRAME)
        {
            return 0;
        }

        const int err = ppg_emit_batch();
        if (err)
        {
            return err;
        }
    }

    return 0;
}

static int acc_emit_batch(void)
{
    uint16_t offset = TGM_FRAME_HEADER_BYTES;

    sys_put_le32(acc_frame_counter, acc_wire);
    tgm_service_acc_data.frame_counter = acc_frame_counter;
    for (uint8_t i = 0; i < CONFIG_ACC_SAMPLES_PER_FRAME; i++)
    {
        tgm_service_acc_data.acc_data[i] = acc_batch[i];
        sys_put_le16((uint16_t)acc_batch[i].x, &acc_wire[offset]);
        sys_put_le16((uint16_t)acc_batch[i].y, &acc_wire[offset + 2]);
        sys_put_le16((uint16_t)acc_batch[i].z, &acc_wire[offset + 4]);
        offset = (uint16_t)(offset + TGM_ACC_SAMPLE_STRIDE);
    }
    acc_batch_n = 0;

    const int err = notify_counted_frame(&tgm_service_svc.attrs[TGM_ATTR_ACC_VALUE],
                                         acc_wire, offset, &acc_held_len,
                                         &acc_frame_counter, &acc_notify_ok,
                                         &acc_notify_err, &acc_last_notify_err, "acc");
    if (!err)
    {
        last_acc_notify_ok_ms = k_uptime_get();
    }
    return err;
}

int tgm_service_send_acc_notify(struct acc_sample *acc_data, uint8_t sample_cnt)
{
    uint8_t src = 0;

    if (!streaming_allowed() || !notify_acc_data || acc_mtu_blocks_stream())
    {
        acc_batch_n = 0;
        return -EACCES;
    }

    if (acc_held_len != 0)
    {
        const int err = notify_counted_frame(&tgm_service_svc.attrs[TGM_ATTR_ACC_VALUE],
                                              acc_wire, acc_held_len, &acc_held_len,
                                              &acc_frame_counter, &acc_notify_ok,
                                              &acc_notify_err, &acc_last_notify_err, "acc");

        if (err)
        {
            return err;
        }
        last_acc_notify_ok_ms = k_uptime_get();
    }

    if (sample_cnt == 0)
    {
        return 0;
    }

    if (sample_cnt > CONFIG_ACC_SAMPLES_PER_FRAME)
    {
        return -EINVAL;
    }

    while (src < sample_cnt)
    {
        const uint8_t room = (uint8_t)(CONFIG_ACC_SAMPLES_PER_FRAME - acc_batch_n);
        uint8_t take = (uint8_t)(sample_cnt - src);

        if (take > room)
        {
            take = room;
        }

        memcpy(&acc_batch[acc_batch_n], &acc_data[src], (size_t)take * sizeof(acc_batch[0]));
        acc_motion_feed(&acc_data[src], take);
        acc_batch_n = (uint8_t)(acc_batch_n + take);
        src = (uint8_t)(src + take);
        last_acc_sample_ms = k_uptime_get();

        if (acc_batch_n < CONFIG_ACC_SAMPLES_PER_FRAME)
        {
            return 0;
        }

        const int err = acc_emit_batch();
        if (err)
        {
            return err;
        }
    }

    return 0;
}

int tgm_service_send_temp_notify(int16_t new_temp)
{
    const uint32_t counter = ++tgm_service_temp_data.frame_counter;

    tgm_service_temp_data.centitemp = new_temp;
    sys_put_le32(counter, temp_wire);
    sys_put_le16((uint16_t)new_temp, &temp_wire[4]);
    temp_wire[6] = 0;
    temp_wire[7] = 0;

    if (!streaming_allowed())
    {
        return -EACCES;
    }

    if (!notify_temp_data)
    {
        return -EACCES;
    }

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_TEMP_VALUE],
                                 temp_wire, TGM_TEMP_VALUE_BYTES, "temp",
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

int tgm_service_send_status_notify(bool on_dock, bool worn, uint8_t state,
				   uint8_t battery_pct, bool charge_active)
{
    device_status.on_dock = on_dock ? 1 : 0;
    device_status.worn = worn ? 1 : 0;
    device_status.device_state = state;
    device_status.battery_pct = battery_pct;
    device_status.charge_active = charge_active ? 1 : 0;

    encode_status();

    if (!notify_status)
    {
        return -EACCES;
    }

    LOG_INF("Sending status: on_dock=%d, worn=%d, state=%d, battery=%d%%, charge_active=%d",
            on_dock, worn, state, battery_pct, charge_active);

    return gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                 status_wire, TGM_STATUS_VALUE_BYTES, "status", true);
}

int tgm_service_send_fw_log_notify(const void *data, uint16_t len)
{
    if (!notify_fw_log || !data || len == 0)
    {
        return -EACCES;
    }

    const int err = gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_FW_LOG_VALUE],
                                        data, len, "fw-log", false);
    if (err)
    {
        fwlog_err++;
        fwlog_last_err = err;
    }
    else
    {
        fwlog_sent++;
        last_fwlog_notify_ok_ms = k_uptime_get();
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

    uint16_t send_len = (uint16_t)((n >= (int)sizeof(buf)) ? (sizeof(buf) - 1) : n);
    const uint16_t max_payload = att_mtu > 3u ? (uint16_t)(att_mtu - 3u) : 0u;

    /* Default ATT MTU 23 cannot carry a useful log line. Do not let it consume TX buffers. */
    if (att_mtu <= TGM_ATT_MTU_DEFAULT || max_payload == 0u)
    {
        LOG_INF("fw log dropped at MTU %u", att_mtu);
        return;
    }

    if (send_len > max_payload)
    {
        send_len = max_payload;
        if ((buf[send_len] & 0xC0) == 0x80)
        {
            while (send_len > 0u && (buf[send_len] & 0xC0) == 0x80)
            {
                send_len--;
            }
        }
        LOG_INF("fw log truncated to %u at MTU %u", send_len, att_mtu);
    }

    if (send_len == 0u)
    {
        return;
    }

    (void)tgm_service_send_fw_log_notify(buf, send_len);
}

void tgm_service_emit_diagnostic_snapshot(void)
{
    tgm_service_refresh_chrsts_bench();

    tgm_service_fw_log_printf(
        "fw: snap dock=%u worn=%u st=%u bat=%u chg=%u probe=%u",
        device_status.on_dock, device_status.worn, device_status.device_state,
        device_status.battery_pct, device_status.charge_active,
        connect_probe_active ? 1u : 0u);

    if (notify_status)
    {
        encode_status();
        (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                  status_wire, TGM_STATUS_VALUE_BYTES, "status-snap", true);
    }

    if (notify_fw_config_state)
    {
        encode_fw_config_state(fw_config_wire);
        (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_FW_CONFIG_STATE_VALUE],
                                  fw_config_wire, TGM_FW_CONFIG_STATE_BYTES, "fwcfg-snap", true);
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
            encode_status();
            (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_STATUS_VALUE],
                                      status_wire, TGM_STATUS_VALUE_BYTES, "status-keepalive",
                                      true);
        }
        else if (notify_temp_data)
        {
            (void)gatt_notify_checked(&tgm_service_svc.attrs[TGM_ATTR_TEMP_VALUE],
                                      temp_wire, TGM_TEMP_VALUE_BYTES,
                                      "temp-keepalive", true);
        }
    }

    link_keepalive_arm();
}
