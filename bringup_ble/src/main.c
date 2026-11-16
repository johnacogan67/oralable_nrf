/*
 * Copyright (c) 2026 ByteExplain / JAC Dental
 * SPDX-License-Identifier: Apache-2.0
 *
 * Gen 2 bench advertiser for nRF Connect.
 * Drives BATEN high. Does not init I2C, the ADC, or the sensors.
 * This is not the Oralable GATT.
 */

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

static const struct gpio_dt_spec baten =
	GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), baten_gpios);

static struct k_work adv_work;
static struct k_work_delayable adv_fallback_work;
static bool connected;

static const struct bt_data ad[] = {
	BT_DATA_BYTES(BT_DATA_FLAGS, (BT_LE_AD_GENERAL | BT_LE_AD_NO_BREDR)),
	BT_DATA(BT_DATA_NAME_COMPLETE, CONFIG_BT_DEVICE_NAME,
		sizeof(CONFIG_BT_DEVICE_NAME) - 1),
};

#define GEN2_SVC_UUID_VAL \
	BT_UUID_128_ENCODE(0x4c150001, 0x54a1, 0x4b2e, 0x9c01, 0x6f0a0b0c0d0e)
#define GEN2_CHR_UUID_VAL \
	BT_UUID_128_ENCODE(0x4c150002, 0x54a1, 0x4b2e, 0x9c01, 0x6f0a0b0c0d0e)

static struct bt_uuid_128 gen2_svc_uuid = BT_UUID_INIT_128(GEN2_SVC_UUID_VAL);
static struct bt_uuid_128 gen2_chr_uuid = BT_UUID_INIT_128(GEN2_CHR_UUID_VAL);

static ssize_t read_mark(struct bt_conn *conn, const struct bt_gatt_attr *attr,
			  void *buf, uint16_t len, uint16_t offset)
{
	const char mark[] = "gen2";

	return bt_gatt_attr_read(conn, attr, buf, len, offset, mark,
				 sizeof(mark) - 1);
}

BT_GATT_SERVICE_DEFINE(gen2_svc,
	BT_GATT_PRIMARY_SERVICE(&gen2_svc_uuid),
	BT_GATT_CHARACTERISTIC(&gen2_chr_uuid.uuid, BT_GATT_CHRC_READ,
			       BT_GATT_PERM_READ, read_mark, NULL, NULL),
);

/* GPIO is ready at PRE_KERNEL_1 priority 40. nRF54 PSA/CRACEN init is
 * priority 50. A debugger halt releases this latch, so drive it before
 * that crypto init. The pin has an external pull-down.
 */
static int baten_early_assert(void)
{
	if (!gpio_is_ready_dt(&baten)) {
		return -ENODEV;
	}

	return gpio_pin_configure_dt(&baten, GPIO_OUTPUT_HIGH);
}

SYS_INIT(baten_early_assert, PRE_KERNEL_1, 41);

static int hold_baten(void)
{
	int err;

	if (!gpio_is_ready_dt(&baten)) {
		printk("BATEN gpio not ready\n");
		return -ENODEV;
	}

	err = gpio_pin_configure_dt(&baten, GPIO_OUTPUT_HIGH);
	if (err) {
		printk("BATEN drive failed %d\n", err);
		return err;
	}

	printk("BATEN high pin %d\n", baten.pin);
	return 0;
}

static void adv_work_handler(struct k_work *work)
{
	int err;

	ARG_UNUSED(work);

	if (connected) {
		return;
	}

	err = bt_le_adv_start(BT_LE_ADV_CONN, ad, ARRAY_SIZE(ad), NULL, 0);
	if (err && err != -EALREADY) {
		printk("adv start failed %d\n", err);
		return;
	}

	printk("adv %s\n", CONFIG_BT_DEVICE_NAME);
}

static void adv_fallback_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	if (connected) {
		return;
	}

	printk("adv fallback\n");
	k_work_submit(&adv_work);
}

static void connected_cb(struct bt_conn *conn, uint8_t err)
{
	char addr[BT_ADDR_LE_STR_LEN];

	bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
	if (err) {
		printk("connect failed %u %s\n", err, addr);
		return;
	}

	connected = true;
	printk("connected %s\n", addr);
}

static void disconnected_cb(struct bt_conn *conn, uint8_t reason)
{
	ARG_UNUSED(conn);

	connected = false;
	printk("disconnected 0x%02x\n", reason);
	(void)k_work_reschedule(&adv_fallback_work, K_SECONDS(2));
}

static void recycled_cb(void)
{
	k_work_submit(&adv_work);
}

BT_CONN_CB_DEFINE(conn_cb) = {
	.connected = connected_cb,
	.disconnected = disconnected_cb,
	.recycled = recycled_cb,
};

int main(void)
{
	int err;

	printk("Oralable Gen2 BLE\n");
	(void)hold_baten();

	k_work_init(&adv_work, adv_work_handler);
	k_work_init_delayable(&adv_fallback_work, adv_fallback_handler);

	err = bt_enable(NULL);
	if (err) {
		printk("bt_enable failed %d\n", err);
		return 0;
	}

	printk("bt ready\n");
	k_work_submit(&adv_work);

	while (1) {
		(void)gpio_pin_set_dt(&baten, 1);
		if (!connected) {
			k_work_submit(&adv_work);
		}
		printk("alive conn=%d\n", connected ? 1 : 0);
		k_sleep(K_SECONDS(5));
	}

	return 0;
}
