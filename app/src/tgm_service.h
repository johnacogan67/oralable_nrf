/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef TGM_SERVICE_H_
#define TGM_SERVICE_H_

#include <app/drivers/maxm86161.h> // For ppg_sample, but fix this later
#include <app/drivers/lis2dtw12.h> // For acc_sample, but fix this later

/**@file
 * @defgroup tgm_service TGM Service implementation
 * @{
 * @brief API for the TGM Bluetooth service.
 */

/**
 * @brief TGM Service 128 bit UUID for the GATT service and its characteristics
 *
 * Generated using guidgenerator.com
 */
#define BT_UUID_TGM_VAL \
    BT_UUID_128_ENCODE(0x3a0ff000, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_PPG_VAL \
    BT_UUID_128_ENCODE(0x3a0ff001, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_ACC_VAL \
    BT_UUID_128_ENCODE(0x3a0ff002, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_TEMP_VAL \
    BT_UUID_128_ENCODE(0x3a0ff003, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_BAT_VAL \
    BT_UUID_128_ENCODE(0x3a0ff004, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_UUID_VAL \
    BT_UUID_128_ENCODE(0x3a0ff005, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_FW_VAL \
    BT_UUID_128_ENCODE(0x3a0ff006, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_READ_PPG_REG_VAL \
    BT_UUID_128_ENCODE(0x3a0ff007, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_WRITE_PPG_REG_VAL \
    BT_UUID_128_ENCODE(0x3a0ff008, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_STATUS_VAL \
    BT_UUID_128_ENCODE(0x3a0ff009, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_FW_LOG_VAL \
    BT_UUID_128_ENCODE(0x3a0ff00a, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_FW_CONFIG_VAL \
    BT_UUID_128_ENCODE(0x3a0ff00b, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM_FW_CONFIG_STATE_VAL \
    BT_UUID_128_ENCODE(0x3a0ff00c, 0x98c4, 0x46b2, 0x94af, 0x1aee0fd4c48e)

#define BT_UUID_TGM BT_UUID_DECLARE_128(BT_UUID_TGM_VAL)
#define BT_UUID_TGM_PPG BT_UUID_DECLARE_128(BT_UUID_TGM_PPG_VAL)
#define BT_UUID_TGM_ACC BT_UUID_DECLARE_128(BT_UUID_TGM_ACC_VAL)
#define BT_UUID_TGM_TEMP BT_UUID_DECLARE_128(BT_UUID_TGM_TEMP_VAL)
#define BT_UUID_TGM_BAT BT_UUID_DECLARE_128(BT_UUID_TGM_BAT_VAL)
#define BT_UUID_TGM_UUID BT_UUID_DECLARE_128(BT_UUID_TGM_UUID_VAL)
#define BT_UUID_TGM_FW BT_UUID_DECLARE_128(BT_UUID_TGM_FW_VAL)
#define BT_UUID_TGM_READ_PPG_REG BT_UUID_DECLARE_128(BT_UUID_TGM_READ_PPG_REG_VAL)
#define BT_UUID_TGM_WRITE_PPG_REG BT_UUID_DECLARE_128(BT_UUID_TGM_WRITE_PPG_REG_VAL)
#define BT_UUID_TGM_STATUS BT_UUID_DECLARE_128(BT_UUID_TGM_STATUS_VAL)
#define BT_UUID_TGM_FW_LOG BT_UUID_DECLARE_128(BT_UUID_TGM_FW_LOG_VAL)
#define BT_UUID_TGM_FW_CONFIG BT_UUID_DECLARE_128(BT_UUID_TGM_FW_CONFIG_VAL)
#define BT_UUID_TGM_FW_CONFIG_STATE BT_UUID_DECLARE_128(BT_UUID_TGM_FW_CONFIG_STATE_VAL)

#define CONFIG_TEMP_SAMPLES_PER_FRAME 10

/** Readable firmware config state (matches iOS `3A0FF00C`). */
struct tgm_fw_config_state_t
{
    uint8_t version;
    uint8_t led_green_pa;
    uint8_t led_ir_pa;
    uint8_t led_red_pa;
    uint8_t stream_stats_period_s;
    uint8_t battery_interval_s;
    uint8_t temp_interval_s;
    uint8_t stream_enable_mask;
    /** v2 chrsts bench (FW >= 1.0.60); sample fresh on each `00C` read. */
    uint8_t chrsts_phy;
    uint8_t chrsts_dt;
    uint8_t chrsts_active;
    uint8_t chrsts_maj;
    uint8_t chrsts_dock_raw;
    uint8_t chrsts_on_dock;
    uint8_t chrsts_stable_s;
    uint8_t chrsts_mismatch;
};

/** Live chrsts GPIO sample for bench / `3A0FF00C` v2. */
struct tgm_chrsts_bench_t
{
    uint8_t phy;
    uint8_t dt;
    uint8_t active;
    uint8_t maj;
    uint8_t dock_raw;
    uint8_t on_dock;
    uint8_t stable_s;
    uint8_t mismatch;
};

/** @brief PPG Data Struct used by the TGM service to inform the client of new PPG data. */
struct tgm_service_ppg_data_t
{
    /** Frame counter*/
    uint32_t frame_counter;
    /** PPG data. */
    struct ppg_sample ppg_data[CONFIG_PPG_SAMPLES_PER_FRAME];
};

/** @brief Accelerometer Data Struct used by the TGM service to inform the client of new accelerometer data. */
struct tgm_service_acc_data_t
{
    /** Frame counter*/
    uint32_t frame_counter;
    /** ACC data. */
    struct acc_sample acc_data[CONFIG_ACC_SAMPLES_PER_FRAME];
};

/** @brief Temperature Data Struct used by the TGM service to inform the client of new temperature data. */
struct tgm_service_temp_data_t
{
    /** Frame counter*/
    uint32_t frame_counter;
    /** Temperature data. */
    int16_t centitemp;
};

/** @brief Device Status Struct - sent to iOS app (3A0FF009, 5 bytes, FW >= 1.0.47). */
struct tgm_service_status_t
{
    /** On charging dock (chrsts GPIO): 0 = off dock, 1 = on dock */
    uint8_t on_dock;
    /** Worn state: 0 = no IR pulse, 1 = IR pulse latched (or mode 3) */
    uint8_t worn;
    /** device_state_t enum value (0–2) */
    uint8_t device_state;
    /** Battery percentage 0-100 */
    uint8_t battery_pct;
    /** Charge active: 0 = not charging / full, 1 = cell voltage rising on dock */
    uint8_t charge_active;
};

/** @brief Callback type for when PPG data is pulled. */
typedef void (*tgm_service_ppg_cb_t)(struct tgm_service_ppg_data_t *ppg_data);

/** @brief Callback type for when accelerometer data is pulled. */
typedef void (*tgm_service_acc_cb_t)(struct tgm_service_acc_data_t *acc_data);

/** @brief Callback type for when temperature data is pulled. */
typedef void (*tgm_service_temp_cb_t)(struct tgm_service_temp_data_t *temp_data);

/** @brief Callback type for when the battery value is pulled. */
typedef int32_t (*tgm_service_bat_cb_t)(void);

/** @brief Optional link / bench-probe hooks (logging, LED profile). */
typedef void (*tgm_service_void_cb_t)(void);

/** @brief IR-pulse worn latch changed (Automatic mode). */
typedef void (*tgm_service_bool_cb_t)(bool on);

/** @brief Sample chrsts GPIO for `3A0FF00C` v2 bench fields. */
typedef void (*tgm_service_chrsts_bench_cb_t)(struct tgm_chrsts_bench_t *out);

/** @brief Callback struct used by the TGM Service. */
struct tgm_service_cb
{
    /** PPG data callback. */
    tgm_service_ppg_cb_t ppg_cb;
    /** Accelerometer data callback. */
    tgm_service_acc_cb_t acc_cb;
    /** Temperature data callback. */
    tgm_service_temp_cb_t temp_cb;
    /** Battery value callback. */
    tgm_service_bat_cb_t bat_cb;
    /** Called when BLE central connects (after tgm_service_on_connected). */
    tgm_service_void_cb_t link_connected_cb;
    /** Called when BLE link drops (before sensor suspend). */
    tgm_service_void_cb_t link_disconnected_cb;
    /** PPG FIFO started after CCC — apply worn sensing LEDs here, not on connect. */
    tgm_service_void_cb_t ppg_stream_start_cb;
    /** Off-body connect probe started — apply dim PPG LEDs. */
    tgm_service_void_cb_t probe_begin_cb;
    /** Connect probe ended — restore off-body LED policy. */
    tgm_service_void_cb_t probe_end_cb;
    /** Refresh status notify + die temp after diagnostic snapshot command. */
    tgm_service_void_cb_t diagnostic_snapshot_cb;
    /** Sample P0.05 chrsts for config-state v2 bench read. */
    tgm_service_chrsts_bench_cb_t chrsts_bench_cb;
    /** Called after `00B` opcode 0x09 sets explicit user device mode. */
    tgm_service_void_cb_t user_mode_changed_cb;
    /** Automatic worn: IR AC pulse present (not die temperature). */
    tgm_service_bool_cb_t ir_pulse_worn_cb;
};

/** @brief Initialize the TGM Service.
 *
 * This function registers application callback functions with the TGM Service
 *
 * @param[in] callbacks Struct containing pointers to callback functions
 *			used by the service. This pointer can be NULL
 *			if no callback functions are defined.
 *
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int tgm_service_init(struct tgm_service_cb *callbacks);

/** @brief Start link keepalive when a central connects. */
void tgm_service_on_connected(void);

/** @brief Clear notification state when the BLE link drops. */
void tgm_service_on_disconnect(void);

/** @brief Record worn status for GATT. Does not start or stop PPG/ACC.
 *
 * Sensors follow the BLE link (and CCC), not this flag.
 */
void tgm_service_set_device_worn(bool worn);

/** @brief Mirror charger/dock state so connect probe can be skipped on Qi pad. */
void tgm_service_set_on_dock(bool on_dock);

/** @brief User override from `00B` opcode 0x09: 0=auto, 1=charger, 2=idle, 3=worn. */
uint8_t tgm_service_get_user_device_mode(void);

/** @brief Set user mode without GATT (0=auto). Used when STAT pad wins over leftover worn. */
void tgm_service_set_user_device_mode(uint8_t mode);

/** @brief Optional bench reboot interval (seconds). 0 = disabled. Set via opcode 0x0A. */
uint16_t tgm_service_get_debug_reboot_interval_s(void);

/** @brief True during the post-connect diagnostic probe window (off-body streaming). */
bool tgm_service_connect_probe_active(void);

/** @brief True when PPG CCC is on and a BLE link (or connect probe) is up. */
bool tgm_service_ppg_notify_active(void);

/** @brief True when a PPG notify succeeded in the last 2 s. */
bool tgm_service_ppg_stream_live(void);

/** @brief True when PPG/ACC CCC is on but no successful notify for 4 s.
 *
 * If PPG CCC is on, ACC/battery success does not keep a dead PPG stream alive.
 */
bool tgm_service_sensor_stream_stale(void);

/** @brief Zero PPG LEDs and stop PPG/ACC. Call first on BLE stall recover. */
void tgm_service_clear_optical_leds(void);

/** @brief Start PPG/ACC if BLE + CCC and above the 5% soft floor. */
void tgm_service_try_start_ppg_acc(void);

/** @brief True when PPG or ACC CCC notify is enabled (live central stream). */
bool tgm_service_sensor_notify_enabled(void);

/** @brief True for a few seconds after an explicit GATT mode-3 write.
 *
 * STAT pad-wins must not clear worn in the gap before PPG/ACC CCC.
 */
bool tgm_service_mode3_pad_grace_active(void);

/** @brief True for 6 minutes after an explicit GATT mode-3 write.
 *
 * Soft floor must not clear worn during a Protocol A session.
 */
bool tgm_service_mode3_soft_floor_hold_active(void);

/** @brief True outside connect-probe lockout (kept for diagnostics; worn is IR pulse). */
bool tgm_service_worn_from_temperature_allowed(void);

/** @brief Push status/config snapshot to fw-log + status notify (opcode 0x07). */
void tgm_service_emit_diagnostic_snapshot(void);

/** @brief Refresh chrsts bench bytes in `3A0FF00C` from chrsts_bench_cb. */
void tgm_service_refresh_chrsts_bench(void);

/** @brief Best-effort UTF-8 line to `3A0FF00A` when notifications enabled. */
void tgm_service_fw_log_printf(const char *fmt, ...);

/** @brief Notify the client of a PPG data change.
 *
 * This function notifies the connected client device of an update to the PPG
 * data
 *
 * @param[in] ppg_data ppg_sample struct containing the PPG sensor data
 * @param[in] sample_cnt Number of samples to be sent
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int tgm_service_send_ppg_notify(struct ppg_sample *ppg_data, uint8_t sample_cnt);

/** @brief Notify the client of an accelerometer data change.
 *
 * This function notifies the connected client device of an update to the accelerometer
 * data
 *
 * @param[in] acc_data acc_sample struct containing the accelerometer sensor data
 * @param[in] sample_cnt Number of samples to be sent
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int tgm_service_send_acc_notify(struct acc_sample *acc_data, uint8_t sample_cnt);

/** @brief Notify the client of a temperature data change.
 *
 * This function notifies the connected client device of an update to the temperature
 * data
 *
 * @param[in] new_temp Temperature value in centi-degrees Celsius (int16)
 *
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int tgm_service_send_temp_notify(int16_t new_temp);

/** @brief Notify the client of a battery value change.
 *
 * This function notifies the connected client device of an update to the battery
 * value
 *
 * @param[in] battery_value Battery value in millivolts (int32)
 *
 *
 * @retval 0 If the operation was successful.
 *           Otherwise, a (negative) error code is returned.
 */
int tgm_service_send_battery_notify(int32_t battery_value);

/**
 * @brief Notify the client of a PPG register read.
 *
 * This function notifies the connected client device of a read PPG register
 *
 * @param[in] ppg_reg_data PPG register data
 * @retval 0 If the operation was successful.
 */
int tgm_service_send_read_ppg_reg_notify(uint8_t ppg_reg_data);

/**
 * @brief Notify the client of a PPG register write.
 *
 * This function notifies the connected client device of a write PPG register
 *
 * @param[in] ppg_reg_data PPG register data
 * @retval 0 If the operation was successful.
 */
int tgm_service_send_write_ppg_reg_notify(uint8_t ppg_reg_data);

/** @brief Notify the client of device status change (3A0FF009).
 *
 * @param[in] on_dock True if clip is on charging dock (chrsts).
 * @param[in] worn True if worn on body (off dock only).
 * @param[in] state device_state_t enum value.
 * @param[in] battery_pct Battery percentage 0-100.
 * @param[in] charge_active True if cell voltage rising on dock.
 */
int tgm_service_send_status_notify(bool on_dock, bool worn, uint8_t state,
				   uint8_t battery_pct, bool charge_active);

/**
 * @}
 */

#endif /* TGM_SERVICE_H_ */