/*
 * Gen 2 I2C pin-matrix diagnostic (low-noise replacement for i2c_pin_scan.c)
 *
 * Purpose
 * -------
 * Systematically test the unresolved PCB00003 Gen 2 sensor wiring without
 * trusting the old scanner's false 0x00 "hits". It:
 *
 *   - Keeps P1.08 high at all times because it is the existing power-latch
 *     candidate. It is never driven low by this diagnostic.
 *   - Tests P1.08-only, then one additional SENS_EN candidate at a time.
 *   - Tests every ordered SDA/SCL combination from the safe candidate list.
 *   - Requires both lines to be released/high before attempting I2C. This
 *     rejects the old scanner's stuck-low false acknowledgements.
 *   - Treats LIS2DTW12 WHO_AM_I == 0x44 as a definitive identification.
 *   - Reports a MAXM86161 part ID only when it is neither 0x00 nor 0xFF.
 *   - Replays at most a compact header, five summaries and a few real matches
 *     over the TGM firmware-log characteristic, paced to avoid -ENOBUFS.
 *
 * This code does not update the board DTS. It is a first-board measurement
 * tool; apply the confirmed pin map only after a valid identity response and
 * a scope/DMM check of SENS_EN and the 3V3 sensor rail.
 */

#include "i2c_pin_scan.h"

#include <errno.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "tgm_service.h"

#define LIS_ADDR            0x19u
#define LIS_WHO_AM_I        0x0Fu
#define LIS_WHO_EXPECTED    0x44u
#define MAXM_ADDR           0x62u
#define MAXM_PART_ID        0xFFu

/* A deliberately slow, conservative bit-banged I2C clock. */
#define HALF_US             30
#define SCL_WAIT_US         300
#define ENABLE_SETTLE_MS    25
#define LOG_PACE_MS         180

#define REPORT_LEN          120
#define MATCH_CAP           5

struct scan_pin {
    const struct device *port;
    gpio_pin_t pin;
    const char *name;
};

#if defined(CONFIG_SOC_SERIES_NRF54LX) && defined(CONFIG_ORALABLE_I2C_PIN_SCAN)

/*
 * P1.08 stays asserted as the base power/latch candidate throughout the test.
 * The remaining entries are driven high one at a time in addition to P1.08.
 * Do not add P1.00/P1.01 (LFXO), P1.02 (CHRSTS), P1.04 (BATEN), or P1.05
 * (BATVOL) to this list.
 */
static const struct scan_pin latch_pin = {
    DEVICE_DT_GET(DT_NODELABEL(gpio1)), 8, "P1.08"
};

static const struct scan_pin sens_en_candidates[] = {
    { DEVICE_DT_GET(DT_NODELABEL(gpio1)), 6, "P1.06" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 2, "P2.02" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 4, "P0.04" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 5, "P2.05" },
};

/* Includes the active DTS pair P1.07/P1.03 and the REV11 hypotheses P0.03/P2.02. */
static const struct scan_pin bus_pins[] = {
    { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 3, "P0.03" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio0)), 4, "P0.04" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio1)), 3, "P1.03" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio1)), 6, "P1.06" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio1)), 7, "P1.07" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 1, "P2.01" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 2, "P2.02" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 4, "P2.04" },
    { DEVICE_DT_GET(DT_NODELABEL(gpio2)), 5, "P2.05" },
};

struct matrix_stats {
    uint16_t pairs;
    uint16_t idle_low;
    uint16_t lis_ack;
    uint16_t maxm_ack;
    uint16_t lis_id_ok;
    uint16_t maxm_id_plausible;
    uint16_t both_id_ok;
};

static char header_line[REPORT_LEN];
static char summary_lines[1 + ARRAY_SIZE(sens_en_candidates)][REPORT_LEN];
static char match_lines[MATCH_CAP][REPORT_LEN];
static char footer_line[REPORT_LEN];
static uint8_t match_count;
static bool scan_ran;

static bool same_pin(const struct scan_pin *a, const struct scan_pin *b)
{
    return a->port == b->port && a->pin == b->pin;
}

static void drive_high(const struct scan_pin *p)
{
    (void)gpio_pin_configure(p->port, p->pin, GPIO_OUTPUT_HIGH);
}

static void release_pin(const struct scan_pin *p)
{
    /* Never release the base power/latch candidate during a running scan. */
    if (same_pin(p, &latch_pin)) {
        drive_high(p);
        return;
    }
    (void)gpio_pin_configure(p->port, p->pin, GPIO_DISCONNECTED);
}

static void line_low(const struct scan_pin *p)
{
    (void)gpio_pin_configure(p->port, p->pin, GPIO_OUTPUT_LOW);
}

static void line_release(const struct scan_pin *p)
{
    /* Input plus a weak pull-up prevents a floating line from looking low. */
    (void)gpio_pin_configure(p->port, p->pin, GPIO_INPUT | GPIO_PULL_UP);
}

static int line_high(const struct scan_pin *p)
{
    return gpio_pin_get(p->port, p->pin) > 0;
}

static int scl_rise(const struct scan_pin *scl)
{
    int waited = 0;

    line_release(scl);
    while (!line_high(scl)) {
        if (waited >= SCL_WAIT_US) {
            return -ETIMEDOUT;
        }
        k_busy_wait(10);
        waited += 10;
    }
    return 0;
}

static int bb_start(const struct scan_pin *sda, const struct scan_pin *scl)
{
    line_release(sda);
    if (scl_rise(scl)) {
        return -ETIMEDOUT;
    }
    k_busy_wait(HALF_US);
    line_low(sda);
    k_busy_wait(HALF_US);
    line_low(scl);
    return 0;
}

static int bb_stop(const struct scan_pin *sda, const struct scan_pin *scl)
{
    line_low(sda);
    k_busy_wait(HALF_US);
    if (scl_rise(scl)) {
        return -ETIMEDOUT;
    }
    k_busy_wait(HALF_US);
    line_release(sda);
    k_busy_wait(HALF_US);
    return 0;
}

static int write_bit(const struct scan_pin *sda, const struct scan_pin *scl, int bit)
{
    line_low(scl);
    if (bit) {
        line_release(sda);
    } else {
        line_low(sda);
    }
    k_busy_wait(HALF_US);
    if (scl_rise(scl)) {
        return -ETIMEDOUT;
    }
    k_busy_wait(HALF_US);
    line_low(scl);
    return 0;
}

static int read_bit(const struct scan_pin *sda, const struct scan_pin *scl, int *bit)
{
    line_low(scl);
    line_release(sda);
    k_busy_wait(HALF_US);
    if (scl_rise(scl)) {
        return -ETIMEDOUT;
    }
    *bit = line_high(sda);
    k_busy_wait(HALF_US);
    line_low(scl);
    return 0;
}

static int write_byte(const struct scan_pin *sda, const struct scan_pin *scl, uint8_t byte)
{
    int bit;
    int err;

    for (int i = 7; i >= 0; i--) {
        err = write_bit(sda, scl, (byte >> i) & 1u);
        if (err) {
            return err;
        }
    }
    err = read_bit(sda, scl, &bit);
    if (err) {
        return err;
    }
    return bit ? -EIO : 0; /* High is NACK; low is ACK. */
}

static int read_byte(const struct scan_pin *sda, const struct scan_pin *scl, uint8_t *byte)
{
    int bit;
    int err;
    uint8_t value = 0;

    for (int i = 7; i >= 0; i--) {
        err = read_bit(sda, scl, &bit);
        if (err) {
            return err;
        }
        value |= (uint8_t)(bit << i);
    }
    err = write_bit(sda, scl, 1); /* NACK after one byte. */
    if (err) {
        return err;
    }
    *byte = value;
    return 0;
}

static int bb_read_reg(const struct scan_pin *sda, const struct scan_pin *scl,
                       uint8_t addr, uint8_t reg, uint8_t *out)
{
    int err;

    *out = 0;
    err = bb_start(sda, scl);
    if (err) {
        return err;
    }
    err = write_byte(sda, scl, (uint8_t)(addr << 1));
    if (err) {
        (void)bb_stop(sda, scl);
        return err;
    }
    err = write_byte(sda, scl, reg);
    if (err) {
        (void)bb_stop(sda, scl);
        return err;
    }
    err = bb_start(sda, scl);
    if (err) {
        (void)bb_stop(sda, scl);
        return err;
    }
    err = write_byte(sda, scl, (uint8_t)((addr << 1) | 1u));
    if (err) {
        (void)bb_stop(sda, scl);
        return err;
    }
    err = read_byte(sda, scl, out);
    (void)bb_stop(sda, scl);
    return err;
}

static bool ports_ready(void)
{
    return device_is_ready(DEVICE_DT_GET(DT_NODELABEL(gpio0))) &&
           device_is_ready(DEVICE_DT_GET(DT_NODELABEL(gpio1))) &&
           device_is_ready(DEVICE_DT_GET(DT_NODELABEL(gpio2)));
}

static void remember_match(const char *en, const struct scan_pin *sda,
                           const struct scan_pin *scl, uint8_t lis, int lis_err,
                           uint8_t maxm, int maxm_err)
{
    if (match_count >= MATCH_CAP) {
        return;
    }

    (void)snprintf(match_lines[match_count], REPORT_LEN,
                   "i2cm MATCH en=%s sda=%s scl=%s lis=%02x/%d maxm=%02x/%d",
                   en, sda->name, scl->name, lis, lis_err, maxm, maxm_err);
    match_count++;
}

static void test_pair(const char *en_name, const struct scan_pin *en,
                      const struct scan_pin *sda, const struct scan_pin *scl,
                      struct matrix_stats *stats)
{
    uint8_t lis = 0;
    uint8_t maxm = 0;
    int lis_err;
    int maxm_err;
    bool lis_ok;
    bool maxm_ok;

    if (same_pin(sda, scl) || (en && (same_pin(sda, en) || same_pin(scl, en))) ||
        same_pin(sda, &latch_pin) || same_pin(scl, &latch_pin)) {
        return;
    }

    line_release(sda);
    line_release(scl);
    k_busy_wait(HALF_US);
    if (!line_high(sda) || !line_high(scl)) {
        /* A low idle line is a wiring/pull-up/power fault, never a sensor hit. */
        stats->idle_low++;
        release_pin(sda);
        release_pin(scl);
        return;
    }

    stats->pairs++;
    lis_err = bb_read_reg(sda, scl, LIS_ADDR, LIS_WHO_AM_I, &lis);
    maxm_err = bb_read_reg(sda, scl, MAXM_ADDR, MAXM_PART_ID, &maxm);

    if (!lis_err) {
        stats->lis_ack++;
    }
    if (!maxm_err) {
        stats->maxm_ack++;
    }

    lis_ok = (lis_err == 0 && lis == LIS_WHO_EXPECTED);
    /* The exact MAXM86161 revision value is recorded rather than hard-coded. */
    maxm_ok = (maxm_err == 0 && maxm != 0x00u && maxm != 0xFFu);

    if (lis_ok) {
        stats->lis_id_ok++;
    }
    if (maxm_ok) {
        stats->maxm_id_plausible++;
    }
    if (lis_ok && maxm_ok) {
        stats->both_id_ok++;
    }

    if (lis_ok || maxm_ok) {
        remember_match(en_name, sda, scl, lis, lis_err, maxm, maxm_err);
    }

    release_pin(sda);
    release_pin(scl);
}

void i2c_pin_scan_run(void)
{
    uint16_t total_pairs = 0;
    uint16_t total_lis = 0;
    uint16_t total_maxm = 0;
    uint16_t total_both = 0;

    memset(summary_lines, 0, sizeof(summary_lines));
    memset(match_lines, 0, sizeof(match_lines));
    match_count = 0;
    scan_ran = false;

    if (!ports_ready()) {
        (void)snprintf(header_line, REPORT_LEN, "i2cm ERROR gpio_not_ready");
        return;
    }

    /* Preserve power through the whole scan. Never drive this pin low. */
    drive_high(&latch_pin);
    k_msleep(ENABLE_SETTLE_MS);

    (void)snprintf(header_line, REPORT_LEN,
                   "i2cm v2 start base=P1.08 en_runs=%u bus_pins=%u",
                   (unsigned int)(1u + ARRAY_SIZE(sens_en_candidates)),
                   (unsigned int)ARRAY_SIZE(bus_pins));

    for (size_t e = 0; e <= ARRAY_SIZE(sens_en_candidates); e++) {
        const struct scan_pin *en = (e == 0u) ? NULL : &sens_en_candidates[e - 1u];
        const char *en_name = (en == NULL) ? "P1.08(base)" : en->name;
        struct matrix_stats stats = {0};

        if (en != NULL) {
            drive_high(en);
            k_msleep(ENABLE_SETTLE_MS);
        }

        for (size_t s = 0; s < ARRAY_SIZE(bus_pins); s++) {
            for (size_t c = 0; c < ARRAY_SIZE(bus_pins); c++) {
                test_pair(en_name, en, &bus_pins[s], &bus_pins[c], &stats);
            }
        }

        if (en != NULL) {
            release_pin(en);
        }

        total_pairs += stats.pairs;
        total_lis += stats.lis_id_ok;
        total_maxm += stats.maxm_id_plausible;
        total_both += stats.both_id_ok;
        (void)snprintf(summary_lines[e], REPORT_LEN,
                       "i2cm EN=%s pairs=%u idle_low=%u lis_ack=%u lis44=%u max_ack=%u maxid=%u both=%u",
                       en_name, (unsigned int)stats.pairs,
                       (unsigned int)stats.idle_low, (unsigned int)stats.lis_ack,
                       (unsigned int)stats.lis_id_ok, (unsigned int)stats.maxm_ack,
                       (unsigned int)stats.maxm_id_plausible,
                       (unsigned int)stats.both_id_ok);
    }

    drive_high(&latch_pin);
    (void)snprintf(footer_line, REPORT_LEN,
                   "i2cm done pairs=%u lis44=%u maxid=%u both=%u matches=%u",
                   (unsigned int)total_pairs, (unsigned int)total_lis,
                   (unsigned int)total_maxm, (unsigned int)total_both,
                   (unsigned int)match_count);
    scan_ran = true;
}

void i2c_pin_scan_replay(void)
{
    if (!scan_ran) {
        return;
    }

    /* Keep the log stream below the BLE controller/notification queue limit. */
    tgm_service_fw_log_printf("%s", header_line);
    k_msleep(LOG_PACE_MS);

    for (size_t i = 0; i < ARRAY_SIZE(summary_lines); i++) {
        tgm_service_fw_log_printf("%s", summary_lines[i]);
        k_msleep(LOG_PACE_MS);
    }

    for (uint8_t i = 0; i < match_count; i++) {
        tgm_service_fw_log_printf("%s", match_lines[i]);
        k_msleep(LOG_PACE_MS);
    }

    tgm_service_fw_log_printf("%s", footer_line);
}

#else

void i2c_pin_scan_run(void)
{
}

void i2c_pin_scan_replay(void)
{
}

#endif /* CONFIG_SOC_SERIES_NRF54LX && CONFIG_ORALABLE_I2C_PIN_SCAN */
