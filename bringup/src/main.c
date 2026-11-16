/*
 * Copyright (c) 2026 ByteExplain / JAC Dental
 * SPDX-License-Identifier: Apache-2.0
 *
 * Gen 2 first image. Prints on RTT. Does not touch power or sensor pins.
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

int main(void)
{
	printk("Oralable Gen2 RTT hello\n");

	while (1) {
		printk("Oralable Gen2 alive\n");
		k_sleep(K_SECONDS(2));
	}

	return 0;
}
