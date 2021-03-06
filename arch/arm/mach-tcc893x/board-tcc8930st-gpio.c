/*
 * linux/arch/arm/mach-tcc892x/board-tcc8920-gpio.c
 *
 * Copyright (C) 2011 Telechips, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <mach/gpio.h>
#include <linux/i2c.h>
#include "board-tcc8930st.h"
#include <asm/mach-types.h>

static struct board_gpio_irq_config tcc8930st_gpio_irqs[] = {
	{ -1, -1 },
};

void __init tcc8930st_init_gpio(void)
{
	if (!machine_is_tcc8930st())
		return;

	board_gpio_irqs = tcc8930st_gpio_irqs;
	printk(KERN_INFO "TCC8930ST GPIO initialized\n");
	return;
}
