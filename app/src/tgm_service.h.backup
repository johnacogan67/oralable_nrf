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

#define BT_UUID_TGM BT_UUID_DECLARE_128(BT_UUID_TGM_VAL)
#define BT_UUID_TGM_PPG BT_UUID_DECLARE_128(BT_UUID_TGM_PPG_VAL)
#define BT_UUID_TGM_ACC BT_UUID_DECLARE_128(BT_UUID_TGM_ACC_VAL)
#define BT_UUID_TGM_TEMP BT_UUID_DECLARE_128(BT_UUID_TGM_TEMP_VAL)
#define BT_UUID_TGM_BAT BT_UUID_DECLARE_128(BT_UUID_TGM_BAT_VAL)
#define BT_UUID_TGM_UUID BT_UUID_DECLARE_128(BT_UUID_TGM_UUID_VAL)
#define BT_UUID_TGM_FW BT_UUID_DECLARE_128(BT_UUID_TGM_FW_VAL)
#define BT_UUID_TGM_READ_PPG_REG BT_UUID_DECLARE_128(BT_UUID_TGM_READ_PPG_REG_VAL)
#define BT_UUID_TGM_WRITE_PPG_REG BT_UUID_DECLARE_128(BT_UUID_TGM_WRITE_PPG_REG_VAL)

#define CONFIG_TEMP_SAMPLES_PER_FRAME 10

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

/** @brief Callback type for when PPG data is pulled. */
typedef void (*tgm_service_ppg_cb_t)(struct tgm_service_ppg_data_t *ppg_data);

/** @brief Callback type for when accelerometer data is pulled. */
typedef void (*tgm_service_acc_cb_t)(struct tgm_service_acc_data_t *acc_data);

/** @brief Callback type for when temperature data is pulled. */
typedef void (*tgm_service_temp_cb_t)(struct tgm_service_temp_data_t *temp_data);

/** @brief Callback type for when the battery value is pulled. */
typedef int32_t (*tgm_service_bat_cb_t)(void);

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
 * @param[in] battery_value Battery value in mV (uint16)
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

/**
 * @}
 */

#endif /* TGM_SERVICE_H_ */