/*
 * Copyright (c) 2024 WeeGee bv
 */

#ifndef PPG_H_
#define PPG_H_

#include <zephyr/kernel.h>

enum ppg_led_t
{
    PPG_LED_GREEN,
    PPG_LED_IR,
    PPG_LED_RED,
};

/**
 * @brief Initialize the PPG sensor
 *
 * @return int 0 on success, negative error code on failure
 */
int ppg_init(void);

/**
 * @brief Start the PPG sensor with default configuration
 *
 * @return int 0 on success, negative error code on failure
 */
int ppg_start(void);

/**
 * @brief Stop PPG FIFO streaming (disable interrupt) but keep the sensor powered
 *        so battery-indicator LEDs keep working.
 */
int ppg_stop_streaming(void);

/**
 * @brief Full PPG shutdown (LEDs off, power-save). Use only when powering down sensing.
 */
int ppg_stop(void);

/**
 * @brief Ensure MAXM86161 is awake so LED pulse-amplitude writes are visible.
 */
int ppg_ensure_awake(void);

/**
 * @brief Wake the AFE for status-LED PA writes, then disable FIFO IRQ.
 *        Collection stays off until ppg_start().
 */
int ppg_ensure_awake_status_leds(void);

/**
 * @brief Read PPG FIFO once and notify if notifications are enabled.
 *
 * Used as a poll fallback when the MAXM86161 interrupt does not fire.
 */
void ppg_poll_once(void);

/** @brief Start or stop the periodic PPG poll fallback (500 ms). */
void ppg_set_notify_poll(bool enabled);

/**
 * @brief Read a register from the PPG sensor and report over BLE
 *
 * @param[in] reg Register address
 * @return int 0 on success, negative error code on failure
 */
int ppg_read_reg(uint8_t reg);

/**
 * @brief Write a register to the PPG sensor and report over BLE
 *
 * @param[in] reg Register address
 * @param[in] data Data to write
 * @return int 0 on success, negative error code on failure
 */
int ppg_write_reg(uint8_t reg, uint8_t data);

/**
 * @brief Set the pulse amplitude of a specific LED
 *
 * @param[in] led LED to set the pulse amplitude for
 * @param[in] pa Pulse amplitude
 * @return int 0 on success, negative error code on failure
 */
int ppg_set_led_pa(enum ppg_led_t led, uint8_t pa);

#endif /* PPG_H_ */