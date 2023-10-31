// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2019-2021, The Linux Foundation. All rights reserved.
 */

#include <linux/proc_fs.h>
#include <linux/time.h>
#include <linux/jiffies.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/mutex.h>
#include <linux/iio/consumer.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/firmware.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/reboot.h>
#include <linux/rpmsg.h>
#include <linux/pm_wakeup.h>
#include <linux/power_supply.h>
#include "oplus_voocphy.h"
#include "../oplus_charger.h"
#include "../oplus_vooc.h"
#include "../oplus_debug_info.h"

typedef enum _FASTCHG_STATUS
{
	ADSP_VPHY_FAST_NOTIFY_UNKNOW,
	ADSP_VPHY_FAST_NOTIFY_PRESENT,
	ADSP_VPHY_FAST_NOTIFY_ONGOING,
	ADSP_VPHY_FAST_NOTIFY_ABSENT,
	ADSP_VPHY_FAST_NOTIFY_FULL,
	ADSP_VPHY_FAST_NOTIFY_BAD_CONNECTED,
	ADSP_VPHY_FAST_NOTIFY_BATT_TEMP_OVER,
	ADSP_VPHY_FAST_NOTIFY_BTB_TEMP_OVER,
	ADSP_VPHY_FAST_NOTIFY_DUMMY_START,
	ADSP_VPHY_FAST_NOTIFY_ADAPTER_COPYCAT,
	ADSP_VPHY_FAST_NOTIFY_ERR_COMMU,
	ADSP_VPHY_FAST_NOTIFY_SWITCH_TEMP_RANGE,
	ADSP_VPHY_FAST_NOTIFY_COMMU_TIME_OUT,
	ADSP_VPHY_FAST_NOTIFY_COMMU_CLK_ERR,
	ADSP_VPHY_FAST_NOTIFY_COMMU_SEND_ERR,
	ADSP_VPHY_FAST_NOTIFY_HW_VBATT_HIGH,
	ADSP_VPHY_FAST_NOTIFY_HW_TBATT_HIGH,
}FASTCHG_STATUS;

static struct oplus_voocphy_manager *g_voocphy_chip = NULL;

void __attribute__((weak)) oplus_adsp_voocphy_set_current_level(void)
{

}

int __attribute__((weak)) oplus_adsp_voocphy_get_fast_chg_type(void)
{
	return 0;
}

void __attribute__((weak)) oplus_adsp_voocphy_cancle_err_check(void)
{
	return;
}

int __attribute__((weak)) oplus_adsp_voocphy_enable(bool enable)
{
	return 0;
}

int __attribute__((weak)) oplus_adsp_voocphy_reset_again(void)
{
	return 0;
}

static struct oplus_voocphy_operations oplus_adsp_voocphy_ops = {
	.adsp_voocphy_enable = oplus_adsp_voocphy_enable,
	.adsp_voocphy_reset_again = oplus_adsp_voocphy_reset_again,
};

void oplus_adsp_voocphy_handle_status(struct power_supply *psy, int intval)
{
	struct oplus_voocphy_manager *chip = g_voocphy_chip;

	if (!chip) {
		printk(KERN_ERR "!!!chip null, adsp voocphy non handle status: [%d]\n", intval);
		return;
	}

	if (!psy) {
		printk(KERN_ERR "!!!psy null, adsp voocphy non handle status: [%d]\n", intval);
		return;
	}

	printk(KERN_ERR "!!![adsp_voocphy] status: [%d]\n", intval);
	chip->adsp_voocphy_rx_data = (intval & 0xFF);
	if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_PRESENT) {
		chip->fastchg_start = true;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_start = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_ing = false;
		chip->fast_chg_type = ((intval >> 8) & 0x7F) ;
		printk(KERN_ERR "!!![adsp_voocphy] fastchg start: [%d], adapter version: [0x%0x]\n",
			chip->fastchg_start, chip->fast_chg_type);
		power_supply_changed(psy);
		oplus_adsp_voocphy_set_current_level();
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_DUMMY_START) {
		chip->fastchg_start = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_start = true;
		chip->fastchg_to_normal = false;
		chip->fastchg_ing = false;
		chip->fast_chg_type = ((intval >> 8) & 0x7F);
		printk(KERN_ERR "!!![adsp_voocphy] fastchg dummy start: [%d], adapter version: [0x%0x]\n",
				chip->fastchg_dummy_start, chip->fast_chg_type);
		power_supply_changed(psy);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_ONGOING) {
		chip->fastchg_ing = true;
		printk(KERN_ERR "!!!! [adsp_voocphy] fastchg ongoing\n");
		oplus_adsp_voocphy_set_current_level();
		if (chip->fast_chg_type == FASTCHG_CHARGER_TYPE_UNKOWN) {
			chip->fastchg_start = true;
			chip->fast_chg_type = oplus_adsp_voocphy_get_fast_chg_type();
			power_supply_changed(psy);
		}
		if (oplus_chg_check_disable_charger() != false) {
			oplus_chg_disable_charge();
			oplus_chg_suspend_charger();
		}
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_FULL
		|| (intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_BAD_CONNECTED) {
		chip->fastchg_start = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_start = false;
		chip->fastchg_to_normal = true;
		chip->fastchg_ing = false;
		oplus_chg_unsuspend_charger();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg to normal: [%d]\n",
			chip->fastchg_to_normal);
		power_supply_changed(psy);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_BATT_TEMP_OVER) {
		chip->fastchg_start = false;
		chip->fastchg_to_warm = true;
		chip->fastchg_dummy_start = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_ing = false;
		oplus_chg_unsuspend_charger();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg to warm: [%d]\n",
				chip->fastchg_to_warm);
		power_supply_changed(psy);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_ERR_COMMU) {
		chip->fastchg_start = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_start = false;
		chip->fastchg_to_normal = false;
		chip->fastchg_ing = false;
		chip->fast_chg_type = 0;
		oplus_chg_set_charger_type_unknown();
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg err commu: [%d]\n", intval);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_SWITCH_TEMP_RANGE) {
		chip->fastchg_start = false;
		chip->fastchg_to_warm = false;
		chip->fastchg_dummy_start = true;
		chip->fastchg_to_normal = false;
		chip->fastchg_ing = false;
		oplus_chg_set_charger_type_unknown();
		power_supply_changed(psy);
		printk(KERN_ERR "!!![adsp_voocphy] fastchg switch temp range: [%d]\n", intval);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_COMMU_CLK_ERR) {
		oplus_chg_set_charger_type_unknown();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg commu clk err: [%d]\n", intval);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_HW_VBATT_HIGH
		|| (intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_HW_TBATT_HIGH) {
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg hw vbatt || tbatt high: [%d]\n", intval);
	} else if ((intval & 0xFF) == ADSP_VPHY_FAST_NOTIFY_COMMU_TIME_OUT) {
		oplus_chg_voocphy_err();
		oplus_chg_set_charger_type_unknown();
		oplus_chg_wake_update_work();
		printk(KERN_ERR "!!![adsp_voocphy] fastchg timeout: [%d]\n", intval);
	} else {
		oplus_chg_voocphy_err();
		oplus_chg_wake_update_work();
		printk(KERN_ERR "!!![adsp_voocphy] non handle status: [%d]\n", intval);
	}

	if ((intval & 0xFF) != ADSP_VPHY_FAST_NOTIFY_PRESENT) {
		oplus_adsp_voocphy_cancle_err_check();
	}
}

static int adsp_voocphy_probe(struct platform_device *pdev)
{
	struct oplus_voocphy_manager *chip = NULL;

	pr_err("%s: start\n", __func__);

	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip) {
		pr_err("oplus_voocphy_manager devm_kzalloc failed.\n");
		return -ENOMEM;
	}

	chip->dev = &pdev->dev;

	oplus_voocphy_set_chip(chip);
	chip->ops = &oplus_adsp_voocphy_ops;
	g_voocphy_chip = chip;
	oplus_adsp_voocphy_clear_status();

	pr_err("%s: end\n", __func__);

	return 0;
}

static const struct of_device_id adsp_voocphy_table[] = {
	{ .compatible = "oplus,adsp-voocphy" },
	{},
};

static struct platform_driver adsp_voocphy_driver = {
	.driver = {
		.name = "oplus_adsp_voocphy",
		.of_match_table = adsp_voocphy_table,
	},
	.probe = adsp_voocphy_probe,
};

int adsp_voocphy_init(void)
{
	int ret;
	pr_err("%s: start\n", __func__);
	ret = platform_driver_register(&adsp_voocphy_driver);
	return ret;
}

void adsp_voocphy_exit(void)
{
	platform_driver_unregister(&adsp_voocphy_driver);
}

MODULE_DESCRIPTION("oplus adsp voocphy driver");
MODULE_LICENSE("GPL v2");
