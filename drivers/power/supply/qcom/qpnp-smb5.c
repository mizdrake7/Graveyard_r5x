/* Copyright (c) 2018-2020 The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/power_supply.h>
#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_gpio.h>
#include <linux/log2.h>
#include <linux/qpnp/qpnp-revid.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/iio/consumer.h>
#include <linux/pmic-voter.h>
#include "smb5-reg.h"
#include "smb5-lib.h"
#include "schgm-flash.h"

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun Chen  PSW.BSP.CHG  2018/04/25  OPPO_CHARGE */
#include <soc/oppo/boot_mode.h>
#include <soc/oppo/device_info.h>
#include <soc/oppo/oppo_project.h>

#include "../../oppo/oppo_charger.h"
#include "../../oppo/oppo_gauge.h"
#include "../../oppo/oppo_vooc.h"
#include "../../oppo/oppo_short.h"
#include "../../oppo/oppo_adapter.h"
#include <linux/of_gpio.h>
#include <linux/proc_fs.h>
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/*Kun.Zhang  PWS.BSP.CHG  2019/04/08  add for charge*/
extern struct oppo_chg_chip *g_oppo_chip;
extern struct smb_charger *g_smb_chip;
static bool fv_adjust_enable = false;
static int fv_adjust_count = 0;

//int schgm_flash_get_vreg_ok(struct smb_charger *chg, int *val);
//int schgm_flash_init(struct smb_charger *chg);

//irqreturn_t schgm_flash_default_irq_handler(int irq, void *data);
//irqreturn_t schgm_flash_ilim2_irq_handler(int irq, void *data);
//irqreturn_t schgm_flash_state_change_irq_handler(int irq, void *data);
static void smbchg_set_chargerid_switch_val(int value);
static int smbchg_chargerid_switch_gpio_init(struct oppo_chg_chip *chip);
static void oppo_usbid_irq_init(struct oppo_chg_chip *chip);
static void oppo_set_usbid_sleep(struct oppo_chg_chip *chip);
static bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip);
static int oppo_shortc_gpio_init(struct oppo_chg_chip *chip);
static int oppo_usbid_gpio_init(struct oppo_chg_chip *chip);
static void oppo_set_usbid_active(struct oppo_chg_chip *chip);

extern void oppo_chg_turn_off_charging(struct oppo_chg_chip *chip);
extern void oppo_chg_turn_on_charging(struct oppo_chg_chip *chip);
int oppo_tbatt_power_off_task_init(struct oppo_chg_chip *chip);
#endif

static struct smb_params smb5_pmi632_params = {
	.fcc			= {
		.name   = "fast charge current",
		.reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.fv			= {
		.name   = "float voltage",
		.reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
		.min_u  = 3600000,
		.max_u  = 4800000,
		.step_u = 10000,
	},
	.usb_icl		= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.icl_max_stat		= {
		.name   = "dcdc icl max status",
		.reg    = ICL_MAX_STATUS_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.icl_stat		= {
		.name   = "input current limit status",
		.reg    = ICL_STATUS_REG,
		.min_u  = 0,
		.max_u  = 3000000,
		.step_u = 50000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= DCDC_OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 500000,
		.max_u	= 1000000,
		.step_u	= 250000,
	},
	.jeita_cc_comp_hot	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_HOT_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.jeita_cc_comp_cold	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_COLD_REG,
		.min_u	= 0,
		.max_u	= 1575000,
		.step_u	= 25000,
	},
	.freq_switcher		= {
		.name	= "switching frequency",
		.reg	= DCDC_FSW_SEL_REG,
		.min_u	= 600,
		.max_u	= 1200,
		.step_u	= 400,
		.set_proc = smblib_set_chg_freq,
	},
	.aicl_5v_threshold		= {
		.name   = "AICL 5V threshold",
		.reg    = USBIN_5V_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 4700,
		.step_u = 100,
	},
	.aicl_cont_threshold		= {
		.name   = "AICL CONT threshold",
		.reg    = USBIN_CONT_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 8800,
		.step_u = 100,
		.get_proc = smblib_get_aicl_cont_threshold,
		.set_proc = smblib_set_aicl_cont_threshold,
	},
};

static struct smb_params smb5_pm8150b_params = {
	.fcc			= {
		.name   = "fast charge current",
		.reg    = CHGR_FAST_CHARGE_CURRENT_CFG_REG,
		.min_u  = 0,
		.max_u  = 8000000,
		.step_u = 50000,
	},
	.fv			= {
		.name   = "float voltage",
		.reg    = CHGR_FLOAT_VOLTAGE_CFG_REG,
		.min_u  = 3600000,
		.max_u  = 4790000,
		.step_u = 10000,
	},
	.usb_icl		= {
		.name   = "usb input current limit",
		.reg    = USBIN_CURRENT_LIMIT_CFG_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.icl_max_stat		= {
		.name   = "dcdc icl max status",
		.reg    = ICL_MAX_STATUS_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.icl_stat		= {
		.name   = "aicl icl status",
		.reg    = AICL_ICL_STATUS_REG,
		.min_u  = 0,
		.max_u  = 5000000,
		.step_u = 50000,
	},
	.otg_cl			= {
		.name	= "usb otg current limit",
		.reg	= DCDC_OTG_CURRENT_LIMIT_CFG_REG,
		.min_u	= 500000,
		.max_u	= 3000000,
		.step_u	= 500000,
	},
	.dc_icl		= {
		.name   = "DC input current limit",
		.reg    = DCDC_CFG_REF_MAX_PSNS_REG,
		.min_u  = 0,
		.max_u  = DCIN_ICL_MAX_UA,
		.step_u = 50000,
	},
	.jeita_cc_comp_hot	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_HOT_REG,
		.min_u	= 0,
		.max_u	= 8000000,
		.step_u	= 25000,
		.set_proc = NULL,
	},
	.jeita_cc_comp_cold	= {
		.name	= "jeita fcc reduction",
		.reg	= JEITA_CCCOMP_CFG_COLD_REG,
		.min_u	= 0,
		.max_u	= 8000000,
		.step_u	= 25000,
		.set_proc = NULL,
	},
	.freq_switcher		= {
		.name	= "switching frequency",
		.reg	= DCDC_FSW_SEL_REG,
		.min_u	= 600,
		.max_u	= 1200,
		.step_u	= 400,
		.set_proc = smblib_set_chg_freq,
	},
	.aicl_5v_threshold		= {
		.name   = "AICL 5V threshold",
		.reg    = USBIN_5V_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 4700,
		.step_u = 100,
	},
	.aicl_cont_threshold		= {
		.name   = "AICL CONT threshold",
		.reg    = USBIN_CONT_AICL_THRESHOLD_REG,
		.min_u  = 4000,
		.max_u  = 11800,
		.step_u = 100,
		.get_proc = smblib_get_aicl_cont_threshold,
		.set_proc = smblib_set_aicl_cont_threshold,
	},
};

//bool oppo_usbid_check_is_gpio(struct oppo_chg_chip *chip);
static bool oppo_usbid_check_is_gpio(struct oppo_chg_chip *chip)
{
        return true;
}


#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun Chen  PSW.BSP.CHG  2018-05-03  OPPO_CHARGE */
struct smb_dt_props {
	int			usb_icl_ua;
	struct device_node	*revid_dev_node;
	enum float_options	float_option;
	int			chg_inhibit_thr_mv;
	bool			no_battery;
	bool			hvdcp_disable;
	bool			hvdcp_autonomous;
	bool			adc_based_aicl;
	int			sec_charger_config;
	int			auto_recharge_soc;
	int			auto_recharge_vbat_mv;
	int			wd_bark_time;
	int			wd_snarl_time_cfg;
	int			batt_profile_fcc_ua;
	int			batt_profile_fv_uv;
	int			term_current_src;
	int			term_current_thresh_hi_ma;
	int			term_current_thresh_lo_ma;
	int			disable_suspend_on_collapse;
};

struct smb5 {
	struct smb_charger	chg;
	struct dentry		*dfs_root;
	struct smb_dt_props	dt;
};
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/15, sjc Add for OTG debug */
static int __debug_mask = PR_MISC | PR_OTG | PR_INTERRUPT | PR_REGISTER;
#else
static int __debug_mask;
#endif

module_param_named(
	debug_mask, __debug_mask, int, 0600
);

static int __pd_disabled;
module_param_named(
	pd_disabled, __pd_disabled, int, 0600
);

static int __weak_chg_icl_ua = 500000;
module_param_named(
	weak_chg_icl_ua, __weak_chg_icl_ua, int, 0600
);

enum {
	BAT_THERM = 0,
	MISC_THERM,
	CONN_THERM,
	SMB_THERM,
};

static const struct clamp_config clamp_levels[] = {
	{ {0x11C6, 0x11F9, 0x13F1}, {0x60, 0x2E, 0x90} },
	{ {0x11C6, 0x11F9, 0x13F1}, {0x60, 0x2B, 0x9C} },
};

#define PMI632_MAX_ICL_UA	3000000
#define PM6150_MAX_FCC_UA	3000000
static int smb5_chg_config_init(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct pmic_revid_data *pmic_rev_id;
	struct device_node *revid_dev_node, *node = chg->dev->of_node;
	int rc = 0;

	revid_dev_node = of_parse_phandle(node, "qcom,pmic-revid", 0);
	if (!revid_dev_node) {
		pr_err("Missing qcom,pmic-revid property\n");
		return -EINVAL;
	}

	pmic_rev_id = get_revid_data(revid_dev_node);
	if (IS_ERR_OR_NULL(pmic_rev_id)) {
		/*
		 * the revid peripheral must be registered, any failure
		 * here only indicates that the rev-id module has not
		 * probed yet.
		 */
		rc =  -EPROBE_DEFER;
		goto out;
	}

	switch (pmic_rev_id->pmic_subtype) {
	case PM8150B_SUBTYPE:
		chip->chg.chg_param.smb_version = PM8150B_SUBTYPE;
		chg->param = smb5_pm8150b_params;
		chg->name = "pm8150b_charger";
		chg->wa_flags |= CHG_TERMINATION_WA;
		break;
	case PM6150_SUBTYPE:
		chip->chg.chg_param.smb_version = PM6150_SUBTYPE;
		chg->param = smb5_pm8150b_params;
		chg->name = "pm6150_charger";
		chg->wa_flags |= SW_THERM_REGULATION_WA | CHG_TERMINATION_WA;
		if (pmic_rev_id->rev4 >= 2)
			chg->uusb_moisture_protection_enabled = true;
		chg->main_fcc_max = PM6150_MAX_FCC_UA;
		break;
	case PMI632_SUBTYPE:
		chip->chg.chg_param.smb_version = PMI632_SUBTYPE;
		chg->wa_flags |= WEAK_ADAPTER_WA | USBIN_OV_WA
				| CHG_TERMINATION_WA;
		chg->param = smb5_pmi632_params;
		chg->use_extcon = true;
		chg->name = "pmi632_charger";
		/* PMI632 does not support PD */
		chg->pd_not_supported = true;
		chg->lpd_disabled = true;
		if (pmic_rev_id->rev4 >= 2)
			chg->uusb_moisture_protection_enabled = true;
		chg->hw_max_icl_ua =
			(chip->dt.usb_icl_ua > 0) ? chip->dt.usb_icl_ua
						: PMI632_MAX_ICL_UA;
		break;
	default:
		pr_err("PMIC subtype %d not supported\n",
				pmic_rev_id->pmic_subtype);
		rc = -EINVAL;
		goto out;
	}

	chg->chg_freq.freq_5V			= 600;
	chg->chg_freq.freq_6V_8V		= 800;
	chg->chg_freq.freq_9V			= 1050;
	chg->chg_freq.freq_12V                  = 1200;
	chg->chg_freq.freq_removal		= 1050;
	chg->chg_freq.freq_below_otg_threshold	= 800;
	chg->chg_freq.freq_above_otg_threshold	= 800;

	if (of_property_read_bool(node, "qcom,disable-sw-thermal-regulation"))
		chg->wa_flags &= ~SW_THERM_REGULATION_WA;

	if (of_property_read_bool(node, "qcom,disable-fcc-restriction"))
		chg->main_fcc_max = -EINVAL;

out:
	of_node_put(revid_dev_node);
	return rc;
}

#define PULL_NO_PULL	0
#define PULL_30K	30
#define PULL_100K	100
#define PULL_400K	400
static int get_valid_pullup(int pull_up)
{
	/* pull up can only be 0/30K/100K/400K) */
	switch (pull_up) {
	case PULL_NO_PULL:
		return INTERNAL_PULL_NO_PULL;
	case PULL_30K:
		return INTERNAL_PULL_30K_PULL;
	case PULL_100K:
		return INTERNAL_PULL_100K_PULL;
	case PULL_400K:
		return INTERNAL_PULL_400K_PULL;
	default:
		return INTERNAL_PULL_100K_PULL;
	}
}

#define INTERNAL_PULL_UP_MASK	0x3
static int smb5_configure_internal_pull(struct smb_charger *chg, int type,
					int pull)
{
	int rc;
	int shift = type * 2;
	u8 mask = INTERNAL_PULL_UP_MASK << shift;
	u8 val = pull << shift;

	rc = smblib_masked_write(chg, BATIF_ADC_INTERNAL_PULL_UP_REG,
				mask, val);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't configure ADC pull-up reg rc=%d\n", rc);

	return rc;
}

#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/27, sjc Modify for OTG current limit (V3.1)  */
#define MICRO_1P5A              1500000
#else
#define MICRO_1P5A              1000000
#endif
#define MICRO_P1A				100000
#define MICRO_1PA				1000000
#define MICRO_3PA				3000000
#define MICRO_4PA				4000000
#define OTG_DEFAULT_DEGLITCH_TIME_MS		50
#define DEFAULT_WD_BARK_TIME			64
#define DEFAULT_WD_SNARL_TIME_8S		0x07
#define DEFAULT_FCC_STEP_SIZE_UA		100000
#define DEFAULT_FCC_STEP_UPDATE_DELAY_MS	1000
static int smb5_parse_dt(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	int rc, byte_len, tmp;

	if (!node) {
		pr_err("device tree node missing\n");
		return -EINVAL;
	}

	of_property_read_u32(node, "qcom,sec-charger-config",
					&chip->dt.sec_charger_config);
	chg->sec_cp_present =
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP ||
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP_PL;

	chg->sec_pl_present =
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_PL ||
		chip->dt.sec_charger_config == POWER_SUPPLY_CHARGER_SEC_CP_PL;

        /* disable step_chg */
        chg->step_chg_enabled = false;

	chg->typec_legacy_use_rp_icl = of_property_read_bool(node,
				"qcom,typec-legacy-rp-icl");

	chg->sw_jeita_enabled = of_property_read_bool(node,
				"qcom,sw-jeita-enable");

	chg->pd_not_supported = chg->pd_not_supported ||
			of_property_read_bool(node, "qcom,usb-pd-disable");

	chg->lpd_disabled = of_property_read_bool(node, "qcom,lpd-disable");

	rc = of_property_read_u32(node, "qcom,wd-bark-time-secs",
					&chip->dt.wd_bark_time);
	if (rc < 0 || chip->dt.wd_bark_time < MIN_WD_BARK_TIME)
		chip->dt.wd_bark_time = DEFAULT_WD_BARK_TIME;

	rc = of_property_read_u32(node, "qcom,wd-snarl-time-config",
					&chip->dt.wd_snarl_time_cfg);
	if (rc < 0)
		chip->dt.wd_snarl_time_cfg = DEFAULT_WD_SNARL_TIME_8S;

	chip->dt.no_battery = of_property_read_bool(node,
						"qcom,batteryless-platform");

	rc = of_property_read_u32(node,
			"qcom,fcc-max-ua", &chip->dt.batt_profile_fcc_ua);
	if (rc < 0)
		chip->dt.batt_profile_fcc_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,fv-max-uv", &chip->dt.batt_profile_fv_uv);
	if (rc < 0)
		chip->dt.batt_profile_fv_uv = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,usb-icl-ua", &chip->dt.usb_icl_ua);
	if (rc < 0)
		chip->dt.usb_icl_ua = -EINVAL;

	rc = of_property_read_u32(node,
				"qcom,otg-cl-ua", &chg->otg_cl_ua);
	if (rc < 0)
		chg->otg_cl_ua =
			(chip->chg.chg_param.smb_version == PMI632_SUBTYPE)
						? MICRO_1PA : MICRO_3PA;

	rc = of_property_read_u32(node, "qcom,chg-term-src",
			&chip->dt.term_current_src);
	if (rc < 0)
		chip->dt.term_current_src = ITERM_SRC_UNSPECIFIED;

	rc = of_property_read_u32(node, "qcom,chg-term-current-ma",
			&chip->dt.term_current_thresh_hi_ma);

	if (chip->dt.term_current_src == ITERM_SRC_ADC)
		rc = of_property_read_u32(node, "qcom,chg-term-base-current-ma",
				&chip->dt.term_current_thresh_lo_ma);

	if (of_find_property(node, "qcom,thermal-mitigation", &byte_len)) {
		chg->thermal_mitigation = devm_kzalloc(chg->dev, byte_len,
			GFP_KERNEL);

		if (chg->thermal_mitigation == NULL)
			return -ENOMEM;

		chg->thermal_levels = byte_len / sizeof(u32);
		rc = of_property_read_u32_array(node,
				"qcom,thermal-mitigation",
				chg->thermal_mitigation,
				chg->thermal_levels);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't read threm limits rc = %d\n", rc);
			return rc;
		}
	}

	rc = of_property_read_u32(node, "qcom,charger-temp-max",
			&chg->charger_temp_max);
	if (rc < 0)
		chg->charger_temp_max = -EINVAL;

	rc = of_property_read_u32(node, "qcom,smb-temp-max",
			&chg->smb_temp_max);
	if (rc < 0)
		chg->smb_temp_max = -EINVAL;

	rc = of_property_read_u32(node, "qcom,float-option",
						&chip->dt.float_option);
	if (!rc && (chip->dt.float_option < 0 || chip->dt.float_option > 4)) {
		pr_err("qcom,float-option is out of range [0, 4]\n");
		return -EINVAL;
	}

	chip->dt.hvdcp_disable = of_property_read_bool(node,
						"qcom,hvdcp-disable");
	chg->hvdcp_disable = chip->dt.hvdcp_disable;

	chip->dt.hvdcp_autonomous = of_property_read_bool(node,
						"qcom,hvdcp-autonomous-enable");

	rc = of_property_read_u32(node, "qcom,chg-inhibit-threshold-mv",
				&chip->dt.chg_inhibit_thr_mv);
	if (!rc && (chip->dt.chg_inhibit_thr_mv < 0 ||
				chip->dt.chg_inhibit_thr_mv > 300)) {
		pr_err("qcom,chg-inhibit-threshold-mv is incorrect\n");
		return -EINVAL;
	}

	chip->dt.auto_recharge_soc = -EINVAL;
	rc = of_property_read_u32(node, "qcom,auto-recharge-soc",
				&chip->dt.auto_recharge_soc);
	if (!rc && (chip->dt.auto_recharge_soc < 0 ||
			chip->dt.auto_recharge_soc > 100)) {
		pr_err("qcom,auto-recharge-soc is incorrect\n");
		return -EINVAL;
	}
	chg->auto_recharge_soc = chip->dt.auto_recharge_soc;

	chip->dt.auto_recharge_vbat_mv = -EINVAL;
	rc = of_property_read_u32(node, "qcom,auto-recharge-vbat-mv",
				&chip->dt.auto_recharge_vbat_mv);
	if (!rc && (chip->dt.auto_recharge_vbat_mv < 0)) {
		pr_err("qcom,auto-recharge-vbat-mv is incorrect\n");
		return -EINVAL;
	}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.chargerid_switch_gpio = 
                                of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
                if (g_oppo_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                        chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n", 
                                        rc, g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                } else {
                                rc = gpio_request(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
                                if (rc) {
                                        chg_err("unable to request chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                                } else {
                                        smbchg_chargerid_switch_gpio_init(g_oppo_chip);
                                }
                        chg_debug("chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
                }
        }
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.usbid_gpio =
                                of_get_named_gpio(node, "qcom,usbid-gpio", 0);
                if (g_oppo_chip->normalchg_gpio.usbid_gpio <= 0) {
                        chg_err("Couldn't read qcom,usbid-gpio rc=%d, qcom,usbid-gpio:%d\n",
                                        rc, g_oppo_chip->normalchg_gpio.usbid_gpio);
                } else {
                        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                                rc = gpio_request(g_oppo_chip->normalchg_gpio.usbid_gpio, "usbid-gpio");
                                if (rc) {
                                        chg_err("unable to request usbid-gpio:%d\n",
                                                        g_oppo_chip->normalchg_gpio.usbid_gpio);
                                } else {
                                        oppo_usbid_gpio_init(g_oppo_chip);
                                        oppo_usbid_irq_init(g_oppo_chip);
                                }
                        }
                        chg_debug("usbid-gpio:%d\n", g_oppo_chip->normalchg_gpio.usbid_gpio);
                }
        }
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-05-15  shortc */
        if (g_oppo_chip) {
                g_oppo_chip->normalchg_gpio.shortc_gpio = 
                                of_get_named_gpio(node, "qcom,shortc-gpio", 0);
                        if (g_oppo_chip->normalchg_gpio.shortc_gpio <= 0) {
                                chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n", 
                                                rc, g_oppo_chip->normalchg_gpio.shortc_gpio);
                        } else {
                                if(oppo_shortc_check_is_gpio(g_oppo_chip) == true) {
                                        chg_debug("This project use gpio for shortc hw check\n");
                                        rc = gpio_request(g_oppo_chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
                                        if(rc){
                                                chg_err("unable to request shortc-gpio:%d\n", 
                                                        g_oppo_chip->normalchg_gpio.shortc_gpio);
                                        } else {
                                                oppo_shortc_gpio_init(g_oppo_chip);
                                        }
                                } else {
                                        chg_err("chip->normalchg_gpio.shortc_gpio is not valid or get_PCB_Version() < V0.3:%d\n", 
                                                        get_PCB_Version());
                                }
                                chg_debug("shortc-gpio:%d\n", g_oppo_chip->normalchg_gpio.shortc_gpio);
                        }
        }
#endif

#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/28, sjc Modify for charging */
        chg->dcp_icl_ua = chip->dt.usb_icl_ua;
#else
        chg->dcp_icl_ua = -EINVAL;
#endif

	chg->suspend_input_on_debug_batt = of_property_read_bool(node,
					"qcom,suspend-input-on-debug-batt");

	rc = of_property_read_u32(node, "qcom,otg-deglitch-time-ms",
					&chg->otg_delay_ms);
	if (rc < 0)
		chg->otg_delay_ms = OTG_DEFAULT_DEGLITCH_TIME_MS;

	chg->fcc_stepper_enable = of_property_read_bool(node,
					"qcom,fcc-stepping-enable");

	chg->uusb_moisture_protection_enabled =
				chg->uusb_moisture_protection_enabled &&
				of_property_read_bool(node,
				"qcom,uusb-moisture-protection-enable");

	chg->hw_die_temp_mitigation = of_property_read_bool(node,
					"qcom,hw-die-temp-mitigation");

	chg->hw_connector_mitigation = of_property_read_bool(node,
					"qcom,hw-connector-mitigation");

	chg->hw_skin_temp_mitigation = of_property_read_bool(node,
					"qcom,hw-skin-temp-mitigation");

	chg->en_skin_therm_mitigation = of_property_read_bool(node,
					"qcom,en-skin-therm-mitigation");

	chg->connector_pull_up = -EINVAL;
	of_property_read_u32(node, "qcom,connector-internal-pull-kohm",
					&chg->connector_pull_up);

	chg->smb_pull_up = -EINVAL;
	of_property_read_u32(node, "qcom,smb-internal-pull-kohm",
					&chg->smb_pull_up);

	chip->dt.adc_based_aicl = of_property_read_bool(node,
					"qcom,adc-based-aicl");

	/* Extract ADC channels */
	rc = smblib_get_iio_channel(chg, "mid_voltage", &chg->iio.mid_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "usb_in_voltage",
					&chg->iio.usbin_v_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "chg_temp", &chg->iio.temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "usb_in_current",
					&chg->iio.usbin_i_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "sbux_res", &chg->iio.sbux_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "vph_voltage", &chg->iio.vph_v_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "die_temp", &chg->iio.die_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "conn_temp",
					&chg->iio.connector_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "skin_temp", &chg->iio.skin_temp_chan);
	if (rc < 0)
		return rc;

	rc = smblib_get_iio_channel(chg, "smb_temp", &chg->iio.smb_temp_chan);
	if (rc < 0)
		return rc;

	chip->dt.disable_suspend_on_collapse = of_property_read_bool(node,
					"qcom,disable-suspend-on-collapse");

	of_property_read_u32(node, "qcom,fcc-step-delay-ms",
					&chg->chg_param.fcc_step_delay_ms);
	if (chg->chg_param.fcc_step_delay_ms <= 0)
		chg->chg_param.fcc_step_delay_ms =
					DEFAULT_FCC_STEP_UPDATE_DELAY_MS;

	of_property_read_u32(node, "qcom,fcc-step-size-ua",
					&chg->chg_param.fcc_step_size_ua);
	if (chg->chg_param.fcc_step_size_ua <= 0)
		chg->chg_param.fcc_step_size_ua = DEFAULT_FCC_STEP_SIZE_UA;

	/*
	 * If property is present parallel charging with CP is disabled
	 * with HVDCP3 adapter.
	 */
	chg->hvdcp3_standalone_config = of_property_read_bool(node,
					"qcom,hvdcp3-standalone-config");
	of_property_read_u32(node, "qcom,hvdcp3-max-icl-ua",
					&chg->chg_param.hvdcp3_max_icl_ua);
	if (chg->chg_param.hvdcp3_max_icl_ua <= 0)
		chg->chg_param.hvdcp3_max_icl_ua = MICRO_3PA;

	/* Used only in Adapter CV mode of operation */
	of_property_read_u32(node, "qcom,qc4-max-icl-ua",
				&chg->chg_param.qc4_max_icl_ua);
	if (chg->chg_param.qc4_max_icl_ua <= 0)
		chg->chg_param.qc4_max_icl_ua = MICRO_4PA;

	chg->wls_icl_ua = DCIN_ICL_MAX_UA;
	rc = of_property_read_u32(node, "qcom,wls-current-max-ua",
			&tmp);
	if (!rc && tmp < DCIN_ICL_MAX_UA)
		chg->wls_icl_ua = tmp;

	chg->aicl_disable = of_property_read_bool(node, "qcom,aicl-disable");

	chg->dcin_uusb_over_gpio_en = of_property_read_bool(node,
					"qcom,dcin-uusb-over-gpio-en");

	if (chg->dcin_uusb_over_gpio_en) {
		chg->micro_usb_gpio = of_get_named_gpio(node,
					"qcom,micro-usb-gpio", 0);
		if (!gpio_is_valid(chg->micro_usb_gpio)) {
			pr_err(" micro_usb_gpio not specified\n");
		} else {
			rc = devm_gpio_request(chg->dev, chg->micro_usb_gpio,
						"micro_usb");
			if (rc)
				pr_err("request micro_usb_gpio failed, rc=%d\n",
						rc);

			rc = gpio_direction_input(chg->micro_usb_gpio);
			if (rc)
				pr_err("Unable to set dir for micro_usb_gpio\n");

			chg->micro_usb_irq = gpio_to_irq(chg->micro_usb_gpio);

			rc = devm_request_threaded_irq(chg->dev,
						chg->micro_usb_irq,
						NULL,
						smb_micro_usb_irq_handler,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"micro_usb_irq", chg);
			if (rc < 0)
				dev_err(chg->dev, "Unable to request micro_usb_irq: %dn",
						rc);

			enable_irq_wake(chg->micro_usb_irq);
		}

		chg->dc_9v_gpio = of_get_named_gpio(node, "qcom,dc-9v-gpio", 0);

		if (!gpio_is_valid(chg->dc_9v_gpio)) {
			pr_err("dc_9v_gpio not specified\n");
		} else {
			rc = devm_gpio_request(chg->dev, chg->dc_9v_gpio,
						"dc_9v");
			if (rc)
				pr_err("Request dc_9v gpio failed, rc=%d\n",
					rc);

			rc = gpio_direction_input(chg->dc_9v_gpio);
			if (rc)
				pr_err("unable to set dir for dc_9v gpio\n");

			chg->dc_9v_irq = gpio_to_irq(chg->dc_9v_gpio);

			rc = devm_request_threaded_irq(chg->dev, chg->dc_9v_irq,
						NULL,
						smb_micro_usb_irq_handler,
						IRQF_TRIGGER_RISING |
						IRQF_TRIGGER_FALLING |
						IRQF_ONESHOT,
						"dc_9v_irq", chg);
			if (rc < 0)
				dev_err(chg->dev, "Unable to request dc_9v_irq: %dn",
					rc);
			enable_irq_wake(chg->dc_9v_irq);
		}

		chg->usb_switch_gpio = of_get_named_gpio(node,
					"qcom,usb-switch-gpio", 0);

		if (!gpio_is_valid(chg->usb_switch_gpio)) {
			pr_err("usb_switch_gpio not specified\n");
		} else {
			rc = devm_gpio_request(chg->dev, chg->usb_switch_gpio,
						"usb_switch");
			if (rc)
				pr_err("Request usb_switch gpio failed, rc=%d\n",
					rc);

			rc = gpio_direction_output(chg->usb_switch_gpio, 1);
			if (rc)
				pr_err("Unable to set dir for usb_switch gpio\n");
		}

		chg->usb_hub_33v_en_gpio = of_get_named_gpio(node,
						"qcom,usb-hub-33v-en-gpio", 0);

		if (!gpio_is_valid(chg->usb_hub_33v_en_gpio)) {
			pr_err("usb_hub_33v_en_gpio not specified\n");
		} else {
			rc = devm_gpio_request(chg->dev,
						chg->usb_hub_33v_en_gpio,
						"usb_hub_33v_en");
			if (rc)
				pr_err("Request usb_hub_33v_en gpio failed, rc=%d\n",
					 rc);

			rc = gpio_direction_output(chg->usb_hub_33v_en_gpio, 1);
			if (rc)
				pr_err("Unable to set dir for usb_hub_33v_en gpio\n");
		}
	}

	return 0;
}

static int smb5_set_prop_comp_clamp_level(struct smb_charger *chg,
			     const union power_supply_propval *val)
{
	int rc = 0, i;
	struct clamp_config clamp_config;
	enum comp_clamp_levels level;

	level = val->intval;
	if (level >= MAX_CLAMP_LEVEL) {
		pr_err("Invalid comp clamp level=%d\n", val->intval);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(clamp_config.reg); i++) {
		rc = smblib_write(chg, clamp_levels[level].reg[i],
			     clamp_levels[level].val[i]);
		if (rc < 0)
			dev_err(chg->dev,
				"Failed to configure comp clamp settings for reg=0x%04x rc=%d\n",
				   clamp_levels[level].reg[i], rc);
	}

	chg->comp_clamp_level = val->intval;

	return rc;
}

/************************
 * USB PSY REGISTRATION *
 ************************/
static enum power_supply_property smb5_usb_props[] = {
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_PD_CURRENT_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_TYPEC_MODE,
	POWER_SUPPLY_PROP_TYPEC_POWER_ROLE,
	POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION,
	POWER_SUPPLY_PROP_LOW_POWER,
	POWER_SUPPLY_PROP_PD_ACTIVE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_CURRENT_NOW,
	POWER_SUPPLY_PROP_BOOST_CURRENT,
	POWER_SUPPLY_PROP_PE_START,
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        POWER_SUPPLY_PROP_OTG_SWITCH,
        POWER_SUPPLY_PROP_OTG_ONLINE,
#endif
	POWER_SUPPLY_PROP_CTM_CURRENT_MAX,
	POWER_SUPPLY_PROP_HW_CURRENT_MAX,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_PD_VOLTAGE_MIN,
	POWER_SUPPLY_PROP_CONNECTOR_TYPE,
	POWER_SUPPLY_PROP_CONNECTOR_HEALTH,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN,
	POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT,
	POWER_SUPPLY_PROP_SMB_EN_MODE,
	POWER_SUPPLY_PROP_SMB_EN_REASON,
	POWER_SUPPLY_PROP_ADAPTER_CC_MODE,
	POWER_SUPPLY_PROP_SCOPE,
	POWER_SUPPLY_PROP_MOISTURE_DETECTED,
	POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED,
	POWER_SUPPLY_PROP_QC_OPTI_DISABLE,
	POWER_SUPPLY_PROP_VOLTAGE_VPH,
	POWER_SUPPLY_PROP_THERM_ICL_LIMIT,
	POWER_SUPPLY_PROP_SKIN_HEALTH,
	POWER_SUPPLY_PROP_APSD_RERUN,
	POWER_SUPPLY_PROP_APSD_TIMEOUT,
};

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
bool __attribute__((weak)) oppo_get_otg_switch_status(void)
{
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return false;
	}

	return chip->otg_switch;
}

bool __attribute__((weak)) oppo_get_otg_online_status(void)
{
        return false;
}

void __attribute__((weak)) oppo_set_otg_switch_status(bool value)
{
	struct oppo_chg_chip *chip = g_oppo_chip;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return;
	}
    	chip->otg_switch = !!value;
}
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Add for charging */
static bool use_present_status = false;
#endif
static int smb5_usb_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval;
	int rc = 0;
	val->intval = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_usb_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/26, sjc Modify for charging */
                if (use_present_status) {
                        rc = smblib_get_prop_usb_present(chg, val);
                } else {
                        rc = smblib_get_prop_usb_online(chg, val);
                }
#else
                rc = smblib_get_prop_usb_online(chg, val);
#endif
                if (!val->intval)
                        break;

#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-06-11  avoid not recognize USB */
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                   (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
#else
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                        (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && ((chg->real_charger_type == POWER_SUPPLY_TYPE_USB) ||
                        (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP))) {
#endif
                        val->intval = 0;
                } else {
                        val->intval = 1;
                }

		if (chg->real_charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
			if (chg->dcin_uusb_over_gpio_en &&
				gpio_is_valid(chg->dc_9v_gpio) &&
				gpio_get_value(chg->dc_9v_gpio))
				val->intval = 1;
			else
				val->intval = 0;
		}
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_DESIGN:
		rc = smblib_get_prop_usb_voltage_max_design(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_usb_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
		if (chg->usbin_forced_max_uv)
			val->intval = chg->usbin_forced_max_uv;
		else
			smblib_get_prop_usb_voltage_max_design(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_usb_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, PD_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		val->intval = get_effective_result(chg->usb_icl_votable);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB_PD;
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = chg->real_charger_type;
		break;
	case POWER_SUPPLY_PROP_TYPEC_MODE:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = POWER_SUPPLY_TYPEC_NONE;
		else
			val->intval = chg->typec_mode;
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = POWER_SUPPLY_TYPEC_PR_NONE;
		else
			rc = smblib_get_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_CC_ORIENTATION:
		if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
			val->intval = 0;
		else
			rc = smblib_get_prop_typec_cc_orientation(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_SRC_RP:
		rc = smblib_get_prop_typec_select_rp(chg, val);
		break;
	case POWER_SUPPLY_PROP_LOW_POWER:
		if (chg->sink_src_mode == SRC_MODE)
			rc = smblib_get_prop_low_power(chg, val);
		else
			rc = -ENODATA;
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		val->intval = chg->pd_active;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_NOW:
		rc = smblib_get_prop_usb_current_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		val->intval = chg->boost_current_ua;
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_get_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		val->intval = chg->system_suspend_supported;
		break;
	case POWER_SUPPLY_PROP_PE_START:
		rc = smblib_get_pe_start(chg, val);
		break;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
                val->intval = oppo_get_otg_switch_status();
                break;
        case POWER_SUPPLY_PROP_OTG_ONLINE:
                val->intval = oppo_get_otg_online_status();
                break;
#endif
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable, CTM_VOTER);
		break;
	case POWER_SUPPLY_PROP_HW_CURRENT_MAX:
		rc = smblib_get_charge_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_get_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		val->intval = chg->voltage_max_uv;
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		val->intval = chg->voltage_min_uv;
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		val->intval = get_client_vote(chg->usb_icl_votable,
					      USB_PSY_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_TYPE:
		val->intval = chg->connector_type;
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
		if (chg->connector_health == -EINVAL)
			val->intval = smblib_get_prop_connector_health(chg);
		else
			val->intval = chg->connector_health;
		break;
	case POWER_SUPPLY_PROP_SCOPE:
		val->intval = POWER_SUPPLY_SCOPE_UNKNOWN;
		rc = smblib_get_prop_usb_present(chg, &pval);
		if (rc < 0)
			break;
		val->intval = pval.intval ? POWER_SUPPLY_SCOPE_DEVICE
				: chg->otg_present ? POWER_SUPPLY_SCOPE_SYSTEM
				: POWER_SUPPLY_SCOPE_UNKNOWN;
		break;
	case POWER_SUPPLY_PROP_SMB_EN_MODE:
		mutex_lock(&chg->smb_lock);
		val->intval = chg->sec_chg_selected;
		mutex_unlock(&chg->smb_lock);
		break;
	case POWER_SUPPLY_PROP_SMB_EN_REASON:
		val->intval = chg->cp_reason;
		break;
	case POWER_SUPPLY_PROP_MOISTURE_DETECTED:
		val->intval = chg->moisture_present;
		break;
	case POWER_SUPPLY_PROP_HVDCP_OPTI_ALLOWED:
		val->intval = !chg->flash_active;
		break;
	case POWER_SUPPLY_PROP_QC_OPTI_DISABLE:
		if (chg->hw_die_temp_mitigation)
			val->intval = POWER_SUPPLY_QC_THERMAL_BALANCE_DISABLE
					| POWER_SUPPLY_QC_INOV_THERMAL_DISABLE;
		if (chg->hw_connector_mitigation)
			val->intval |= POWER_SUPPLY_QC_CTM_DISABLE;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_VPH:
		rc = smblib_get_prop_vph_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
		val->intval = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER);
		break;
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
		val->intval = chg->adapter_cc_mode;
		break;
	case POWER_SUPPLY_PROP_SKIN_HEALTH:
		val->intval = smblib_get_skin_temp_status(chg);
		break;
	case POWER_SUPPLY_PROP_APSD_RERUN:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_APSD_TIMEOUT:
		val->intval = chg->apsd_ext_timeout;
		break;
	default:
		pr_err("get prop %d is not supported in usb\n", psp);
		rc = -EINVAL;
		break;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

#define MIN_THERMAL_VOTE_UA	500000
static int smb5_usb_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int icl, rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_PD_CURRENT_MAX:
		rc = smblib_set_prop_pd_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_POWER_ROLE:
		rc = smblib_set_prop_typec_power_role(chg, val);
		break;
	case POWER_SUPPLY_PROP_TYPEC_SRC_RP:
		rc = smblib_set_prop_typec_select_rp(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_ACTIVE:
		rc = smblib_set_prop_pd_active(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_IN_HARD_RESET:
		rc = smblib_set_prop_pd_in_hard_reset(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_USB_SUSPEND_SUPPORTED:
		chg->system_suspend_supported = val->intval;
		break;
	case POWER_SUPPLY_PROP_BOOST_CURRENT:
		rc = smblib_set_prop_boost_current(chg, val);
		break;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
                oppo_set_otg_switch_status(!!val->intval);
                break;
#endif
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
		rc = vote(chg->usb_icl_votable, CTM_VOTER,
						val->intval >= 0, val->intval);
		break;
	case POWER_SUPPLY_PROP_PR_SWAP:
		rc = smblib_set_prop_pr_swap_in_progress(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MAX:
		rc = smblib_set_prop_pd_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_PD_VOLTAGE_MIN:
		rc = smblib_set_prop_pd_voltage_min(chg, val);
		break;
	case POWER_SUPPLY_PROP_SDP_CURRENT_MAX:
		rc = smblib_set_prop_sdp_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
		chg->connector_health = val->intval;
		power_supply_changed(chg->usb_psy);
		break;
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
		if (!is_client_vote_enabled(chg->usb_icl_votable,
						THERMAL_THROTTLE_VOTER)) {
			chg->init_thermal_ua = get_effective_result(
							chg->usb_icl_votable);
			icl = chg->init_thermal_ua + val->intval;
		} else {
			icl = get_client_vote(chg->usb_icl_votable,
					THERMAL_THROTTLE_VOTER) + val->intval;
		}

		if (icl >= MIN_THERMAL_VOTE_UA)
			rc = vote(chg->usb_icl_votable, THERMAL_THROTTLE_VOTER,
				(icl != chg->init_thermal_ua) ? true : false,
				icl);
		else
			rc = -EINVAL;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
		smblib_set_prop_usb_voltage_max_limit(chg, val);
		break;
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
		chg->adapter_cc_mode = val->intval;
		break;
	case POWER_SUPPLY_PROP_APSD_RERUN:
		del_timer_sync(&chg->apsd_timer);
		chg->apsd_ext_timeout = false;
		smblib_rerun_apsd(chg);
		break;
	default:
		pr_debug("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smb5_usb_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/02/18, sjc Add for OTG sw */
        case POWER_SUPPLY_PROP_OTG_SWITCH:
#endif
	case POWER_SUPPLY_PROP_CTM_CURRENT_MAX:
	case POWER_SUPPLY_PROP_CONNECTOR_HEALTH:
	case POWER_SUPPLY_PROP_THERM_ICL_LIMIT:
	case POWER_SUPPLY_PROP_VOLTAGE_MAX_LIMIT:
	case POWER_SUPPLY_PROP_ADAPTER_CC_MODE:
	case POWER_SUPPLY_PROP_APSD_RERUN:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc usb_psy_desc = {
	.name = "usb",
#ifndef CONFIG_PRODUCT_REALME_TRINKET
// wenbin.liu@BSP.CHG.Basic, 2017/09/22 
// Delete for usb init unknow 
        .type = POWER_SUPPLY_TYPE_USB_PD,
#else
        .type = POWER_SUPPLY_TYPE_UNKNOWN,
#endif
	.properties = smb5_usb_props,
	.num_properties = ARRAY_SIZE(smb5_usb_props),
	.get_property = smb5_usb_get_prop,
	.set_property = smb5_usb_set_prop,
	.property_is_writeable = smb5_usb_prop_is_writeable,
};

static int smb5_init_usb_psy(struct smb5 *chip)
{
	struct power_supply_config usb_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_cfg.drv_data = chip;
	usb_cfg.of_node = chg->dev->of_node;
	chg->usb_psy = devm_power_supply_register(chg->dev,
						  &usb_psy_desc,
						  &usb_cfg);
	if (IS_ERR(chg->usb_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->usb_psy);
	}

	return 0;
}

/********************************
 * USB PC_PORT PSY REGISTRATION *
 ********************************/
static enum power_supply_property smb5_usb_port_props[] = {
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CURRENT_MAX,
};

static int smb5_usb_port_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_USB;
		break;
	case POWER_SUPPLY_PROP_ONLINE:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-05-15  recognize CDP */
                if (use_present_status) {
                        rc = smblib_get_prop_usb_present(chg, val);
                } else {
                        rc = smblib_get_prop_usb_online(chg, val);
                }
#else
                rc = smblib_get_prop_usb_online(chg, val);
#endif
		if (!val->intval)
			break;

#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-06-11  avoid not recognize USB */
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                   (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && (chg->real_charger_type == POWER_SUPPLY_TYPE_USB)) {
#else
                if (((chg->typec_mode == POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) ||
                        (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB))
                        && ((chg->real_charger_type == POWER_SUPPLY_TYPE_USB) ||
                        (chg->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP))) {
#endif
                        val->intval = 1;
                } else {
                        val->intval = 0;
                }
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = 5000000;
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	default:
		pr_err_ratelimited("Get prop %d is not supported in pc_port\n",
				psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb5_usb_port_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	int rc = 0;

	switch (psp) {
	default:
		pr_debug("Set prop %d is not supported in pc_port\n",
				psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_port_psy_desc = {
	.name		= "pc_port",
	.type		= POWER_SUPPLY_TYPE_USB,
	.properties	= smb5_usb_port_props,
	.num_properties	= ARRAY_SIZE(smb5_usb_port_props),
	.get_property	= smb5_usb_port_get_prop,
	.set_property	= smb5_usb_port_set_prop,
};

static int smb5_init_usb_port_psy(struct smb5 *chip)
{
	struct power_supply_config usb_port_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_port_cfg.drv_data = chip;
	usb_port_cfg.of_node = chg->dev->of_node;
	chg->usb_port_psy = devm_power_supply_register(chg->dev,
						  &usb_port_psy_desc,
						  &usb_port_cfg);
	if (IS_ERR(chg->usb_port_psy)) {
		pr_err("Couldn't register USB pc_port power supply\n");
		return PTR_ERR(chg->usb_port_psy);
	}

	return 0;
}

/*****************************
 * USB MAIN PSY REGISTRATION *
 *****************************/

static enum power_supply_property smb5_usb_main_props[] = {
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_TYPE,
	POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED,
	POWER_SUPPLY_PROP_FCC_DELTA,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_FLASH_ACTIVE,
	POWER_SUPPLY_PROP_FLASH_TRIGGER,
	POWER_SUPPLY_PROP_TOGGLE_STAT,
	POWER_SUPPLY_PROP_MAIN_FCC_MAX,
	POWER_SUPPLY_PROP_IRQ_STATUS,
	POWER_SUPPLY_PROP_FORCE_MAIN_FCC,
	POWER_SUPPLY_PROP_FORCE_MAIN_ICL,
	POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_HOT_TEMP,
};

static int smb5_usb_main_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fv, &val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_TYPE:
		val->intval = POWER_SUPPLY_TYPE_MAIN;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED:
		rc = smblib_get_prop_input_current_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_SETTLED:
		rc = smblib_get_prop_input_voltage_settled(chg, val);
		break;
	case POWER_SUPPLY_PROP_FCC_DELTA:
		rc = smblib_get_prop_fcc_delta(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_icl_current(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_FLASH_ACTIVE:
		val->intval = chg->flash_active;
		break;
	case POWER_SUPPLY_PROP_FLASH_TRIGGER:
		rc = schgm_flash_get_vreg_ok(chg, &val->intval);
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
		val->intval = chg->main_fcc_max;
		break;
	case POWER_SUPPLY_PROP_IRQ_STATUS:
		rc = smblib_get_irq_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
		rc = smblib_get_charge_param(chg, &chg->param.fcc,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
		rc = smblib_get_charge_param(chg, &chg->param.usb_icl,
							&val->intval);
		break;
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
		val->intval = chg->comp_clamp_level;
		break;
	/* Use this property to report SMB health */
	case POWER_SUPPLY_PROP_HEALTH:
		if (chg->use_bq_pump) {
			rc = val->intval = -ENODATA;
			break;
		}
		rc = val->intval = smblib_get_prop_smb_health(chg);
		break;
	/* Use this property to report overheat status */
	case POWER_SUPPLY_PROP_HOT_TEMP:
		val->intval = chg->thermal_overheat;
		break;
	default:
		pr_debug("get prop %d is not supported in usb-main\n", psp);
		rc = -EINVAL;
		break;
	}
	if (rc < 0)
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);

	return rc;
}

static int smb5_usb_main_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval = {0, };
	enum power_supply_type real_chg_type = chg->real_charger_type;
	int rc = 0, offset_ua = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_set_charge_param(chg, &chg->param.fv, val->intval);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		/* Adjust Main FCC for QC3.0 + SMB1390 */
		rc = smblib_get_qc3_main_icl_offset(chg, &offset_ua);
		if (rc < 0)
			offset_ua = 0;

		rc = smblib_set_charge_param(chg, &chg->param.fcc,
						val->intval + offset_ua);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_icl_current(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_FLASH_ACTIVE:
		if ((chg->chg_param.smb_version == PMI632_SUBTYPE)
				&& (chg->flash_active != val->intval)) {
			chg->flash_active = val->intval;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-06-28  avoid flash current ripple when flash work */
                        smblib_set_opt_switcher_freq(chg,
                                chg->flash_active ? chg->chg_freq.freq_removal : chg->chg_freq.freq_5V);
#endif
			rc = smblib_get_prop_usb_present(chg, &pval);
			if (rc < 0)
				pr_err("Failed to get USB preset status rc=%d\n",
						rc);
			if (pval.intval) {
				rc = smblib_force_vbus_voltage(chg,
					chg->flash_active ? FORCE_5V_BIT
								: IDLE_BIT);
				if (rc < 0)
					pr_err("Failed to force 5V\n");
				else
					chg->pulse_cnt = 0;
			} else {
				/* USB absent & flash not-active - vote 100mA */
				vote(chg->usb_icl_votable, SW_ICL_MAX_VOTER,
							true, SDP_100_MA);
			}

			pr_debug("flash active VBUS 5V restriction %s\n",
				chg->flash_active ? "applied" : "removed");

			/* Update userspace */
			if (chg->batt_psy)
				power_supply_changed(chg->batt_psy);
		}
		break;
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
		rc = smblib_toggle_smb_en(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
		chg->main_fcc_max = val->intval;
		rerun_election(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
		vote_override(chg->fcc_main_votable, CC_MODE_VOTER,
				(val->intval < 0) ? false : true, val->intval);
		if (val->intval >= 0)
			chg->chg_param.forced_main_fcc = val->intval;

		/* Main FCC updated re-calculate FCC */
		rerun_election(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
		vote_override(chg->usb_icl_votable, CC_MODE_VOTER,
				(val->intval < 0) ? false : true, val->intval);
		/* Main ICL updated re-calculate ILIM */
		if (real_chg_type == POWER_SUPPLY_TYPE_USB_HVDCP_3 ||
			real_chg_type == POWER_SUPPLY_TYPE_USB_HVDCP_3P5)
			rerun_election(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
		rc = smb5_set_prop_comp_clamp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_HOT_TEMP:
		rc = smblib_set_prop_thermal_overheat(chg, val->intval);
		break;
	default:
		pr_err("set prop %d is not supported\n", psp);
		rc = -EINVAL;
		break;
	}

	return rc;
}

static int smb5_usb_main_prop_is_writeable(struct power_supply *psy,
				enum power_supply_property psp)
{
	int rc;

	switch (psp) {
	case POWER_SUPPLY_PROP_TOGGLE_STAT:
	case POWER_SUPPLY_PROP_MAIN_FCC_MAX:
	case POWER_SUPPLY_PROP_FORCE_MAIN_FCC:
	case POWER_SUPPLY_PROP_FORCE_MAIN_ICL:
	case POWER_SUPPLY_PROP_COMP_CLAMP_LEVEL:
	case POWER_SUPPLY_PROP_HOT_TEMP:
		rc = 1;
		break;
	default:
		rc = 0;
		break;
	}

	return rc;
}

static const struct power_supply_desc usb_main_psy_desc = {
	.name		= "main",
	.type		= POWER_SUPPLY_TYPE_MAIN,
	.properties	= smb5_usb_main_props,
	.num_properties	= ARRAY_SIZE(smb5_usb_main_props),
	.get_property	= smb5_usb_main_get_prop,
	.set_property	= smb5_usb_main_set_prop,
	.property_is_writeable = smb5_usb_main_prop_is_writeable,
};

static int smb5_init_usb_main_psy(struct smb5 *chip)
{
	struct power_supply_config usb_main_cfg = {};
	struct smb_charger *chg = &chip->chg;

	usb_main_cfg.drv_data = chip;
	usb_main_cfg.of_node = chg->dev->of_node;
	chg->usb_main_psy = devm_power_supply_register(chg->dev,
						  &usb_main_psy_desc,
						  &usb_main_cfg);
	if (IS_ERR(chg->usb_main_psy)) {
		pr_err("Couldn't register USB main power supply\n");
		return PTR_ERR(chg->usb_main_psy);
	}

	return 0;
}

/*************************
 * DC PSY REGISTRATION   *
 *************************/

#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
static enum power_supply_property smb5_dc_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_ONLINE,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_CURRENT_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION,
	POWER_SUPPLY_PROP_REAL_TYPE,
	POWER_SUPPLY_PROP_DC_RESET,
};

static int smb5_dc_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		val->intval = get_effective_result(chg->dc_suspend_votable);
		break;
	case POWER_SUPPLY_PROP_PRESENT:
		rc = smblib_get_prop_dc_present(chg, val);
		break;
	case POWER_SUPPLY_PROP_ONLINE:
		rc = smblib_get_prop_dc_online(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
		rc = smblib_get_prop_dc_voltage_now(chg, val);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_get_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		rc = smblib_get_prop_dc_voltage_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_REAL_TYPE:
		val->intval = POWER_SUPPLY_TYPE_WIPOWER;
		break;
	case POWER_SUPPLY_PROP_DC_RESET:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		rc = smblib_get_prop_voltage_wls_output(chg, val);
		break;
	default:
		return -EINVAL;
	}
	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}
	return 0;
}

static int smb5_dc_set_prop(struct power_supply *psy,
		enum power_supply_property psp,
		const union power_supply_propval *val)
{
	struct smb5 *chip = power_supply_get_drvdata(psy);
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = vote(chg->dc_suspend_votable, WBC_VOTER,
				(bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_CURRENT_MAX:
		rc = smblib_set_prop_dc_current_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		rc = smblib_set_prop_voltage_wls_output(chg, val);
		break;
	case POWER_SUPPLY_PROP_DC_RESET:
		rc = smblib_set_prop_dc_reset(chg);
		break;
	default:
		return -EINVAL;
	}

	return rc;
}

static int smb5_dc_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_INPUT_VOLTAGE_REGULATION:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc dc_psy_desc = {
	.name = "dc",
	.type = POWER_SUPPLY_TYPE_WIRELESS,
	.properties = smb5_dc_props,
	.num_properties = ARRAY_SIZE(smb5_dc_props),
	.get_property = smb5_dc_get_prop,
	.set_property = smb5_dc_set_prop,
	.property_is_writeable = smb5_dc_prop_is_writeable,
};

static int smb5_init_dc_psy(struct smb5 *chip)
{
	struct power_supply_config dc_cfg = {};
	struct smb_charger *chg = &chip->chg;

	dc_cfg.drv_data = chip;
	dc_cfg.of_node = chg->dev->of_node;
	chg->dc_psy = devm_power_supply_register(chg->dev,
						  &dc_psy_desc,
						  &dc_cfg);
	if (IS_ERR(chg->dc_psy)) {
		pr_err("Couldn't register USB power supply\n");
		return PTR_ERR(chg->dc_psy);
	}

	return 0;
}
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/03/07, sjc Add for charging*/
/*************************
 * AC PSY REGISTRATION *
 *************************/
 
static enum power_supply_property ac_props[] = {
        POWER_SUPPLY_PROP_ONLINE,
};

static int ac_get_property(struct power_supply *psy,
        enum power_supply_property psp,
        union power_supply_propval *val)
{
        int rc = 0;

        if (!g_oppo_chip)
                return -EINVAL;

        if (g_oppo_chip->charger_exist) {
                if ((g_oppo_chip->charger_type == POWER_SUPPLY_TYPE_USB_DCP) || (oppo_vooc_get_fastchg_started() == true)
                        || (oppo_vooc_get_fastchg_to_normal() == true) || (oppo_vooc_get_fastchg_to_warm() == true)
                        || (oppo_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE) || (oppo_vooc_get_btb_temp_over() == true)) {
                        g_oppo_chip->ac_online = true;
                } else {
                        g_oppo_chip->ac_online = false;
                }
        } else {
                if ((oppo_vooc_get_fastchg_started() == true) || (oppo_vooc_get_fastchg_to_normal() == true)
                        || (oppo_vooc_get_fastchg_to_warm() == true) || (oppo_vooc_get_adapter_update_status() == ADAPTER_FW_NEED_UPDATE)
                        || (oppo_vooc_get_btb_temp_over() == true) || g_oppo_chip->mmi_fastchg == 0) {
                        g_oppo_chip->ac_online = true;
                } else {
                        g_oppo_chip->ac_online = false;
                }
        }
#ifdef CONFIG_OPPO_CHARGER_MTK
        if (g_oppo_chip->ac_online) {
                chg_debug("chg_exist:%d, ac_online:%d\n",g_oppo_chip->charger_exist,g_oppo_chip->ac_online);
        }
#endif
        switch (psp) {
        case POWER_SUPPLY_PROP_ONLINE:
                val->intval = g_oppo_chip->ac_online;
                 break;
        default:
                rc = -EINVAL;
                break;
        }
        return rc;
}

static const struct power_supply_desc ac_psy_desc = {
        .name = "ac",
        .type = POWER_SUPPLY_TYPE_MAINS,
        .properties = ac_props,
        .num_properties = ARRAY_SIZE(ac_props),
        .get_property = ac_get_property,
};

static int smb5_init_ac_psy(struct smb5 *chip)
{
        struct power_supply_config ac_cfg = {};
        struct smb_charger *chg = &chip->chg;

        ac_cfg.drv_data = chip;
        ac_cfg.of_node = chg->dev->of_node;
        chg->ac_psy = devm_power_supply_register(chg->dev,
                                                  &ac_psy_desc,
                                                  &ac_cfg);
        if (IS_ERR(chg->ac_psy)) {
                chg_err("Couldn't register AC power supply\n");
                return PTR_ERR(chg->ac_psy);
        } else {
                chg_debug("success register AC power supply\n");
        }
        return 0;
}
#endif


/*************************
 * BATT PSY REGISTRATION *
 *************************/
static enum power_supply_property smb5_batt_props[] = {
	POWER_SUPPLY_PROP_INPUT_SUSPEND,
	POWER_SUPPLY_PROP_STATUS,
	POWER_SUPPLY_PROP_HEALTH,
	POWER_SUPPLY_PROP_PRESENT,
	POWER_SUPPLY_PROP_CHARGE_TYPE,
	POWER_SUPPLY_PROP_CAPACITY,
	POWER_SUPPLY_PROP_CHARGER_TEMP,
	POWER_SUPPLY_PROP_CHARGER_TEMP_MAX,
	POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED,
	POWER_SUPPLY_PROP_VOLTAGE_NOW,
	POWER_SUPPLY_PROP_VOLTAGE_MAX,
	POWER_SUPPLY_PROP_VOLTAGE_QNOVO,
	POWER_SUPPLY_PROP_CURRENT_NOW,
	POWER_SUPPLY_PROP_CURRENT_QNOVO,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
	POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT,
	POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT,
	POWER_SUPPLY_PROP_TEMP,
	POWER_SUPPLY_PROP_TECHNOLOGY,
	POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED,
	POWER_SUPPLY_PROP_SW_JEITA_ENABLED,
	POWER_SUPPLY_PROP_CHARGE_DONE,
	POWER_SUPPLY_PROP_PARALLEL_DISABLE,
	POWER_SUPPLY_PROP_SET_SHIP_MODE,
	POWER_SUPPLY_PROP_DIE_HEALTH,
	POWER_SUPPLY_PROP_RERUN_AICL,
	POWER_SUPPLY_PROP_DP_DM,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX,
	POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT,
	POWER_SUPPLY_PROP_CHARGE_COUNTER,
	POWER_SUPPLY_PROP_CYCLE_COUNT,
	POWER_SUPPLY_PROP_RECHARGE_SOC,
	POWER_SUPPLY_PROP_CHARGE_FULL,
	POWER_SUPPLY_PROP_FORCE_RECHARGE,
	POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE,
#ifdef CONFIG_PRODUCT_REALME_TRINKET
        /* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
                POWER_SUPPLY_PROP_CHARGE_NOW,
                POWER_SUPPLY_PROP_AUTHENTICATE,
                POWER_SUPPLY_PROP_CHARGE_TIMEOUT,
                POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY,
                POWER_SUPPLY_PROP_FAST_CHARGE,
                POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE,
                POWER_SUPPLY_PROP_BATTERY_FCC,
                POWER_SUPPLY_PROP_BATTERY_SOH,
                POWER_SUPPLY_PROP_BATTERY_CC,
                POWER_SUPPLY_PROP_BATTERY_RM,
                POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE,
                POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE,
                POWER_SUPPLY_PROP_VOOCCHG_ING,
                POWER_SUPPLY_PROP_CALL_MODE,
#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
                POWER_SUPPLY_PROP_CHARGERID_VOLT,
#endif
#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
                POWER_SUPPLY_PROP_SHIP_MODE,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
                POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE,
                POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE,
                POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS,
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
                POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE,
                POWER_SUPPLY_PROP_SHORT_C_HW_STATUS,
#endif
#endif

};

#define DEBUG_ACCESSORY_TEMP_DECIDEGC	250
static int smb5_batt_get_prop(struct power_supply *psy,
		enum power_supply_property psp,
		union power_supply_propval *val)
{
	struct smb_charger *chg = power_supply_get_drvdata(psy);
	int rc = 0;

	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        if (oppo_chg_show_vooc_logo_ornot() == 1) {
                                val->intval = POWER_SUPPLY_STATUS_CHARGING;
                        } else if (!g_oppo_chip->authenticate) {
                                val->intval = POWER_SUPPLY_STATUS_NOT_CHARGING;
                        } else {
                                val->intval = g_oppo_chip->prop_status;
                        } 
                } else {
                        rc = smblib_get_prop_batt_status(chg, val);
                }
#else
                rc = smblib_get_prop_batt_status(chg, val);
#endif
		break;
	case POWER_SUPPLY_PROP_HEALTH:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = oppo_chg_get_prop_batt_health(g_oppo_chip);
                } else {
                        rc = smblib_get_prop_batt_health(chg, val);
                }
#else
                rc = smblib_get_prop_batt_health(chg, val);
#endif
		break;
	case POWER_SUPPLY_PROP_PRESENT:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_exist;
                } else {
                        rc = smblib_get_prop_batt_present(chg, val);
                }
#else
                rc = smblib_get_prop_batt_present(chg, val);
#endif
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_get_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TYPE:
		rc = smblib_get_prop_batt_charge_type(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->ui_soc;
                } else {
                        rc = smblib_get_prop_batt_capacity(chg, val);
                }
#else
                rc = smblib_get_prop_batt_capacity(chg, val);
#endif
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_get_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT_MAX:
		rc = smblib_get_prop_system_temp_level_max(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP:
		rc = smblib_get_prop_charger_temp(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGER_TEMP_MAX:
		val->intval = chg->charger_temp_max;
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_get_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
		/* disable step_chg */
		chg->step_chg_enabled = false;
		break;
	case POWER_SUPPLY_PROP_SW_JEITA_ENABLED:
		val->intval = chg->sw_jeita_enabled;
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_NOW:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
        /* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->batt_volt * 1000;
                        } else {
                                rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
                        } 
#else
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_VOLTAGE_NOW, val);
#endif
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		val->intval = get_client_vote(chg->fv_votable,
				BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		val->intval = get_client_vote_locked(chg->fv_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CURRENT_NOW:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
        /* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->icharging;
                                break;
                        } else {
                                rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CURRENT_NOW, val);
                        }
#else
                        rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CURRENT_NOW, val);
#endif	
		if (!rc)
			val->intval *= (-1);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		val->intval = get_client_vote_locked(chg->fcc_votable,
				QNOVO_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		val->intval = get_client_vote(chg->fcc_votable,
					      BATT_PROFILE_VOTER);
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT:
		val->intval = get_effective_result(chg->fcc_votable);
		break;
	case POWER_SUPPLY_PROP_CHARGE_TERM_CURRENT:
		rc = smblib_get_prop_batt_iterm(chg, val);
		break;
    case POWER_SUPPLY_PROP_TEMP:
#ifdef CONFIG_PRODUCT_REALME_TRINKET
            /* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Modify for charging*/
            if (g_oppo_chip) {
                val->intval = g_oppo_chip->temperature;
            } else {
                rc = smblib_get_prop_from_bms(chg, POWER_SUPPLY_PROP_TEMP, val);
            }
#else
        if (chg->typec_mode == POWER_SUPPLY_TYPEC_SINK_DEBUG_ACCESSORY)
            val->intval = DEBUG_ACCESSORY_TEMP_DECIDEGC;
        else
            rc = smblib_get_prop_from_bms(chg,
                    POWER_SUPPLY_PROP_TEMP, val);
#endif
		break;
	case POWER_SUPPLY_PROP_TECHNOLOGY:
		val->intval = POWER_SUPPLY_TECHNOLOGY_LION;
		break;
	case POWER_SUPPLY_PROP_CHARGE_DONE:
		rc = smblib_get_prop_batt_charge_done(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		val->intval = get_client_vote(chg->pl_disable_votable,
					      USER_VOTER);
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as device is active */
		val->intval = 0;
		break;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_CHARGE_NOW:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->charger_volt;
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_AUTHENTICATE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->authenticate;
                } else {
                        val->intval = true;
                }
                break;

        case POWER_SUPPLY_PROP_CHARGE_TIMEOUT:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->chging_over_time;
                } else {
                        val->intval = false;
                }
                break;

        case POWER_SUPPLY_PROP_CHARGE_TECHNOLOGY:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->vooc_project;
                } else {
                        val->intval = true;
                }
                break;

        case POWER_SUPPLY_PROP_FAST_CHARGE:
                if (g_oppo_chip) {
                        val->intval = oppo_chg_show_vooc_logo_ornot();
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:     //add for MMI_CHG TEST
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->mmi_chg;
                } else {
                        val->intval = 1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_FCC:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_fcc;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_SOH:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_soh;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_CC:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->batt_cc;
                } else {
                        val->intval = -1;
                }
                break;

        case POWER_SUPPLY_PROP_BATTERY_RM:
                if (g_oppo_chip)
                        val->intval = g_oppo_chip->batt_rm;
                else
                        val->intval = -1;
                break;

        case POWER_SUPPLY_PROP_BATTERY_NOTIFY_CODE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->notify_code;
                } else {
                        val->intval = 0;
                }
                break;

        case POWER_SUPPLY_PROP_ADAPTER_FW_UPDATE:
                val->intval = oppo_vooc_get_adapter_update_status();
                break;

        case POWER_SUPPLY_PROP_VOOCCHG_ING:
                val->intval = oppo_vooc_get_fastchg_ing();
                break;

        case POWER_SUPPLY_PROP_CALL_MODE:
                val->intval = g_oppo_chip->calling_on;
                break;

#ifdef CONFIG_OPPO_CHECK_CHARGERID_VOLT
        case POWER_SUPPLY_PROP_CHARGERID_VOLT:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->chargerid_volt;
                } else {
                        val->intval = 0;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->enable_shipmode;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
                if (g_oppo_chip) {
                        val->intval = g_oppo_chip->short_c_batt.update_change;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
                if (g_oppo_chip) {
                        val->intval = (int)g_oppo_chip->short_c_batt.in_idle;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_CV_STATUS:
                if (g_oppo_chip) {
                        val->intval = (int)oppo_short_c_batt_get_cv_status(g_oppo_chip);
                }
                break;
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
                case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->short_c_batt.is_feature_hw_on;
                        }
                        break;  
                        
                case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
                        if (g_oppo_chip) {
                                val->intval = g_oppo_chip->short_c_batt.shortc_gpio_status;
                        }
                        break;
#endif
#endif
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		if (chg->die_health == -EINVAL)
			val->intval = smblib_get_prop_die_health(chg);
		else
			val->intval = chg->die_health;
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		val->intval = chg->pulse_cnt;
		break;
	case POWER_SUPPLY_PROP_RERUN_AICL:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_COUNTER:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CHARGE_COUNTER, val);
		break;
	case POWER_SUPPLY_PROP_CYCLE_COUNT:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CYCLE_COUNT, val);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		val->intval = chg->auto_recharge_soc;
		break;
	case POWER_SUPPLY_PROP_CHARGE_QNOVO_ENABLE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_CHARGE_FULL:
		rc = smblib_get_prop_from_bms(chg,
				POWER_SUPPLY_PROP_CHARGE_FULL, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_RECHARGE:
		val->intval = 0;
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		val->intval = chg->fcc_stepper_enable;
		break;
	default:
		pr_err("batt power supply prop %d not supported\n", psp);
		return -EINVAL;
	}

	if (rc < 0) {
		pr_debug("Couldn't get prop %d rc = %d\n", psp, rc);
		return -ENODATA;
	}

	return 0;
}

static int smb5_batt_set_prop(struct power_supply *psy,
		enum power_supply_property prop,
		const union power_supply_propval *val)
{
	int rc = 0;
	struct smb_charger *chg = power_supply_get_drvdata(psy);

	switch (prop) {
	case POWER_SUPPLY_PROP_STATUS:
		rc = smblib_set_prop_batt_status(chg, val);
		break;
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
		rc = smblib_set_prop_input_suspend(chg, val);
		break;
	case POWER_SUPPLY_PROP_CHARGE_CONTROL_LIMIT:
		rc = smblib_set_prop_system_temp_level(chg, val);
		break;
	case POWER_SUPPLY_PROP_CAPACITY:
		rc = smblib_set_prop_batt_capacity(chg, val);
		break;
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
		vote(chg->pl_disable_votable, USER_VOTER, (bool)val->intval, 0);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_MAX:
		chg->batt_profile_fv_uv = val->intval;
		vote(chg->fv_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_VOLTAGE_QNOVO:
		vote(chg->fv_votable, QNOVO_VOTER, (val->intval >= 0),
			val->intval);
		break;
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
                /* disable step_chg */
                chg->step_chg_enabled = false;
		break;
	case POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX:
		chg->batt_profile_fcc_ua = val->intval;
		vote(chg->fcc_votable, BATT_PROFILE_VOTER, true, val->intval);
		break;
	case POWER_SUPPLY_PROP_CURRENT_QNOVO:
		vote(chg->pl_disable_votable, PL_QNOVO_VOTER,
			val->intval != -EINVAL && val->intval < 2000000, 0);
		if (val->intval == -EINVAL) {
			vote(chg->fcc_votable, BATT_PROFILE_VOTER,
					true, chg->batt_profile_fcc_ua);
			vote(chg->fcc_votable, QNOVO_VOTER, false, 0);
		} else {
			vote(chg->fcc_votable, QNOVO_VOTER, true, val->intval);
			vote(chg->fcc_votable, BATT_PROFILE_VOTER, false, 0);
		}
		break;
	case POWER_SUPPLY_PROP_SET_SHIP_MODE:
		/* Not in ship mode as long as the device is active */
		if (!val->intval)
			break;
		if (chg->pl.psy)
			power_supply_set_property(chg->pl.psy,
				POWER_SUPPLY_PROP_SET_SHIP_MODE, val);
		rc = smblib_set_prop_ship_mode(chg, val);
		break;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
                if (g_oppo_chip) {
                        if (val->intval == 0) {
                                chg_debug("mmi_chg: set 0\n");
                                g_oppo_chip->mmi_chg = 0;
                                oppo_chg_turn_off_charging(g_oppo_chip);
                                if (oppo_vooc_get_fastchg_started() == true) {
                                        oppo_chg_set_chargerid_switch_val(0);
                                        oppo_vooc_switch_mode(NORMAL_CHARGER_MODE);
                                        g_oppo_chip->mmi_fastchg = 0;
                                }
                        } else {
                                chg_debug("mmi_chg: set 1\n");
                                g_oppo_chip->mmi_chg = 1;
                                if (g_oppo_chip->mmi_fastchg == 0) {
                                        oppo_chg_clear_chargerid_info();
                                }
                                g_oppo_chip->mmi_fastchg = 1;
                                oppo_chg_turn_on_charging(g_oppo_chip);
                        }
                }
                break;

        case POWER_SUPPLY_PROP_CALL_MODE:
                g_oppo_chip->calling_on = val->intval;
                break;

#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
                if (g_oppo_chip) {
                        g_oppo_chip->enable_shipmode = val->intval;
                }
                break;
#endif

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
                if (g_oppo_chip) {
                        chg_debug("[OPPO_CHG] [short_c_batt]: set update change[%d]\n", val->intval);
                        oppo_short_c_batt_update_change(g_oppo_chip, val->intval);
                        g_oppo_chip->short_c_batt.update_change = val->intval;
                }
                break;

        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
                if (g_oppo_chip) {
                        chg_debug("[short_c_batt]: set in idle[%d]\n", !!val->intval);
                        g_oppo_chip->short_c_batt.in_idle = !!val->intval;
                }
                break;
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
                case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
                        if (g_oppo_chip) {
                                printk(KERN_ERR "[OPPO_CHG] [short_c_hw_check]: set is_feature_hw_on [%d]\n", val->intval);
                                g_oppo_chip->short_c_batt.is_feature_hw_on = val->intval;
                        }
                        break;
        
                case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
                        break;
#endif
#endif
	case POWER_SUPPLY_PROP_RERUN_AICL:
		rc = smblib_run_aicl(chg, RERUN_AICL);
		break;
	case POWER_SUPPLY_PROP_DP_DM:
		if (!chg->flash_active)
			rc = smblib_dp_dm(chg, val->intval);
		break;
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
		rc = smblib_set_prop_input_current_limited(chg, val);
		break;
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		chg->die_health = val->intval;
		power_supply_changed(chg->batt_psy);
		break;
	case POWER_SUPPLY_PROP_RECHARGE_SOC:
		rc = smblib_set_prop_rechg_soc_thresh(chg, val);
		break;
	case POWER_SUPPLY_PROP_FORCE_RECHARGE:
			/* toggle charging to force recharge */
			vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
					true, 0);
			/* charge disable delay */
			msleep(50);
			vote(chg->chg_disable_votable, FORCE_RECHARGE_VOTER,
					false, 0);
		break;
	case POWER_SUPPLY_PROP_FCC_STEPPER_ENABLE:
		chg->fcc_stepper_enable = val->intval;
		break;
	default:
		rc = -EINVAL;
	}

	return rc;
}

static int smb5_batt_prop_is_writeable(struct power_supply *psy,
		enum power_supply_property psp)
{
	switch (psp) {
	case POWER_SUPPLY_PROP_STATUS:
	case POWER_SUPPLY_PROP_INPUT_SUSPEND:
	case POWER_SUPPLY_PROP_SYSTEM_TEMP_LEVEL:
	case POWER_SUPPLY_PROP_CAPACITY:
	case POWER_SUPPLY_PROP_PARALLEL_DISABLE:
	case POWER_SUPPLY_PROP_DP_DM:
	case POWER_SUPPLY_PROP_RERUN_AICL:
	case POWER_SUPPLY_PROP_INPUT_CURRENT_LIMITED:
	case POWER_SUPPLY_PROP_STEP_CHARGING_ENABLED:
	case POWER_SUPPLY_PROP_DIE_HEALTH:
		return 1;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        case POWER_SUPPLY_PROP_MMI_CHARGING_ENABLE:
        case POWER_SUPPLY_PROP_CALL_MODE:
#ifdef CONFIG_OPPO_SHIP_MODE_SUPPORT
        case POWER_SUPPLY_PROP_SHIP_MODE:
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_BATT_UPDATE_CHANGE:
        case POWER_SUPPLY_PROP_SHORT_C_BATT_IN_IDLE:
#endif
#ifdef CONFIG_OPPO_SHORT_HW_CHECK
        case POWER_SUPPLY_PROP_SHORT_C_HW_FEATURE:
        case POWER_SUPPLY_PROP_SHORT_C_HW_STATUS:
#endif
                return 1;
#endif
	default:
		break;
	}

	return 0;
}

static const struct power_supply_desc batt_psy_desc = {
	.name = "battery",
	.type = POWER_SUPPLY_TYPE_BATTERY,
	.properties = smb5_batt_props,
	.num_properties = ARRAY_SIZE(smb5_batt_props),
	.get_property = smb5_batt_get_prop,
	.set_property = smb5_batt_set_prop,
	.property_is_writeable = smb5_batt_prop_is_writeable,
};

static int smb5_init_batt_psy(struct smb5 *chip)
{
	struct power_supply_config batt_cfg = {};
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	batt_cfg.drv_data = chg;
	batt_cfg.of_node = chg->dev->of_node;
	chg->batt_psy = devm_power_supply_register(chg->dev,
					   &batt_psy_desc,
					   &batt_cfg);
	if (IS_ERR(chg->batt_psy)) {
		pr_err("Couldn't register battery power supply\n");
		return PTR_ERR(chg->batt_psy);
	}

	return rc;
}

/********************************
 * DUAL-ROLE CLASS REGISTRATION *
 ********************************/
static enum dual_role_property smb5_dr_properties[] = {
	DUAL_ROLE_PROP_SUPPORTED_MODES,
	DUAL_ROLE_PROP_MODE,
	DUAL_ROLE_PROP_PR,
	DUAL_ROLE_PROP_DR,
};

static int smb5_dr_get_property(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop, unsigned int *val)
{
	struct smb_charger *chg = dual_role_get_drvdata(dual_role);
	union power_supply_propval pval = {0, };
	/* Initializing pr, dr and mode to value 2 corresponding to NONE case */
	int mode = 2, pr = 2, dr = 2, rc = 0;

	if (!chg)
		return -ENODEV;

	rc = smblib_get_prop_usb_present(chg, &pval);
	if (rc < 0) {
		pr_err("Couldn't get usb present status, rc=%d\n", rc);
		return rc;
	}

	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC) {
		if (chg->typec_mode == POWER_SUPPLY_TYPEC_NONE) {
			mode = DUAL_ROLE_PROP_MODE_NONE;
			pr = DUAL_ROLE_PROP_PR_NONE;
			dr = DUAL_ROLE_PROP_DR_NONE;
		} else if (chg->typec_mode <
				POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) {
			mode = DUAL_ROLE_PROP_MODE_DFP;
			pr = DUAL_ROLE_PROP_PR_SRC;
			dr = DUAL_ROLE_PROP_DR_HOST;
		} else if (chg->typec_mode >=
				POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) {
			mode = DUAL_ROLE_PROP_MODE_UFP;
			pr = DUAL_ROLE_PROP_PR_SNK;
			dr = DUAL_ROLE_PROP_DR_DEVICE;
		}
	} else if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB) {
		pr = DUAL_ROLE_PROP_PR_NONE;

		if (chg->otg_present) {
			mode = DUAL_ROLE_PROP_MODE_DFP;
			dr = DUAL_ROLE_PROP_DR_HOST;
		} else if (pval.intval) {
			mode = DUAL_ROLE_PROP_MODE_UFP;
			dr = DUAL_ROLE_PROP_DR_DEVICE;
		} else {
			mode = DUAL_ROLE_PROP_MODE_NONE;
			dr = DUAL_ROLE_PROP_DR_NONE;
		}
	}

	switch (prop) {
	case DUAL_ROLE_PROP_MODE:
		*val = mode;
		break;
	case DUAL_ROLE_PROP_PR:
		*val = pr;
		break;
	case DUAL_ROLE_PROP_DR:
		*val = dr;
		break;
	default:
		pr_err("dual role class get property %d not supported\n", prop);
		return -EINVAL;
	}

	return 0;
}

static int smb5_dr_set_property(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop, const unsigned int *val)
{
	struct smb_charger *chg = dual_role_get_drvdata(dual_role);
	int rc = 0;

	if (!chg)
		return -ENODEV;

	mutex_lock(&chg->dr_lock);
	switch (prop) {
	case DUAL_ROLE_PROP_MODE:
		if (chg->pr_swap_in_progress) {
			pr_debug("Already in mode transition. Skipping request\n");
			mutex_unlock(&chg->dr_lock);
			return 0;
		}

		switch (*val) {
		case DUAL_ROLE_PROP_MODE_UFP:
			if (chg->typec_mode >=
					POWER_SUPPLY_TYPEC_SOURCE_DEFAULT) {
				chg->dr_mode = DUAL_ROLE_PROP_MODE_UFP;
			} else {
				chg->pr_swap_in_progress = true;
				rc = smblib_force_dr_mode(chg,
						DUAL_ROLE_PROP_MODE_UFP);
				if (rc < 0) {
					chg->pr_swap_in_progress = false;
					pr_err("Failed to force UFP mode, rc=%d\n",
						rc);
				}
			}
			break;
		case DUAL_ROLE_PROP_MODE_DFP:
			if (chg->typec_mode < POWER_SUPPLY_TYPEC_SOURCE_DEFAULT
				&& chg->typec_mode != POWER_SUPPLY_TYPEC_NONE) {
				chg->dr_mode = DUAL_ROLE_PROP_MODE_DFP;
			} else {
				chg->pr_swap_in_progress = true;
				rc = smblib_force_dr_mode(chg,
						DUAL_ROLE_PROP_MODE_DFP);
				if (rc < 0) {
					chg->pr_swap_in_progress = false;
					pr_err("Failed to force DFP mode, rc=%d\n",
						rc);
				}
			}
			break;
		default:
			pr_err("Invalid role (not DFP/UFP): %d\n", *val);
			rc = -EINVAL;
		}

		/*
		 * Schedule delayed work to check if the device latched to
		 * the requested mode.
		 */
		if (chg->pr_swap_in_progress && !rc) {
			cancel_delayed_work_sync(&chg->role_reversal_check);
			vote(chg->awake_votable, DR_SWAP_VOTER, true, 0);
			queue_delayed_work(system_power_efficient_wq, &chg->role_reversal_check,
				msecs_to_jiffies(ROLE_REVERSAL_DELAY_MS));
		}
		break;
	default:
		pr_err("dual role class set property %d not supported\n", prop);
		rc = -EINVAL;
	}

	mutex_unlock(&chg->dr_lock);
	return rc;
}

static int smb5_dr_prop_writeable(struct dual_role_phy_instance *dual_role,
			enum dual_role_property prop)
{
	struct smb_charger *chg = dual_role_get_drvdata(dual_role);

	if (!chg)
		return -ENODEV;

	/* uUSB connector does not support role switch */
	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
		return 0;

	switch (prop) {
	case DUAL_ROLE_PROP_MODE:
		return 1;
	default:
		break;
	}

	return 0;
}

static const struct dual_role_phy_desc dr_desc = {
	.name = "otg_default",
	.supported_modes = DUAL_ROLE_SUPPORTED_MODES_DFP_AND_UFP,
	.properties = smb5_dr_properties,
	.num_properties = ARRAY_SIZE(smb5_dr_properties),
	.get_property = smb5_dr_get_property,
	.set_property = smb5_dr_set_property,
	.property_is_writeable = smb5_dr_prop_writeable,
};

static int smb5_init_dual_role_class(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc = 0;

	/* Register dual role class for only non-PD TypeC and uUSB designs */
	if (!chg->pd_not_supported)
		return rc;

	mutex_init(&chg->dr_lock);
	chg->dual_role = devm_dual_role_instance_register(chg->dev, &dr_desc);
	if (IS_ERR(chg->dual_role)) {
		pr_err("Couldn't register dual role class\n");
		rc = PTR_ERR(chg->dual_role);
	} else {
		chg->dual_role->drv_data = chg;
	}

	return rc;
}

/******************************
 * VBUS REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vbus_reg_ops = {
	.enable = smblib_vbus_regulator_enable,
	.disable = smblib_vbus_regulator_disable,
	.is_enabled = smblib_vbus_regulator_is_enabled,
};

static int smb5_init_vbus_regulator(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	chg->vbus_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vbus_vreg),
				      GFP_KERNEL);
	if (!chg->vbus_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vbus_vreg->rdesc.owner = THIS_MODULE;
	chg->vbus_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vbus_vreg->rdesc.ops = &smb5_vbus_reg_ops;
	chg->vbus_vreg->rdesc.of_match = "qcom,smb5-vbus";
	chg->vbus_vreg->rdesc.name = "qcom,smb5-vbus";

	chg->vbus_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vbus_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vbus_vreg->rdev)) {
		rc = PTR_ERR(chg->vbus_vreg->rdev);
		chg->vbus_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VBUS regulator rc=%d\n", rc);
	}

	return rc;
}

/******************************
 * VCONN REGULATOR REGISTRATION *
 ******************************/

static struct regulator_ops smb5_vconn_reg_ops = {
	.enable = smblib_vconn_regulator_enable,
	.disable = smblib_vconn_regulator_disable,
	.is_enabled = smblib_vconn_regulator_is_enabled,
};

static int smb5_init_vconn_regulator(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct regulator_config cfg = {};
	int rc = 0;

	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_MICRO_USB)
		return 0;

	chg->vconn_vreg = devm_kzalloc(chg->dev, sizeof(*chg->vconn_vreg),
				      GFP_KERNEL);
	if (!chg->vconn_vreg)
		return -ENOMEM;

	cfg.dev = chg->dev;
	cfg.driver_data = chip;

	chg->vconn_vreg->rdesc.owner = THIS_MODULE;
	chg->vconn_vreg->rdesc.type = REGULATOR_VOLTAGE;
	chg->vconn_vreg->rdesc.ops = &smb5_vconn_reg_ops;
	chg->vconn_vreg->rdesc.of_match = "qcom,smb5-vconn";
	chg->vconn_vreg->rdesc.name = "qcom,smb5-vconn";

	chg->vconn_vreg->rdev = devm_regulator_register(chg->dev,
						&chg->vconn_vreg->rdesc, &cfg);
	if (IS_ERR(chg->vconn_vreg->rdev)) {
		rc = PTR_ERR(chg->vconn_vreg->rdev);
		chg->vconn_vreg->rdev = NULL;
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't register VCONN regulator rc=%d\n", rc);
	}

	return rc;
}

/***************************
 * HARDWARE INITIALIZATION *
 ***************************/
static int smb5_configure_typec(struct smb_charger *chg)
{
	union power_supply_propval pval = {0, };
	int rc;
	u8 val = 0;

	rc = smblib_read(chg, LEGACY_CABLE_STATUS_REG, &val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't read Legacy status rc=%d\n", rc);
		return rc;
	}

	/*
	 * Across reboot, standard typeC cables get detected as legacy cables
	 * due to VBUS attachment prior to CC attach/dettach. To handle this,
	 * "early_usb_attach" flag is used, which assumes that across reboot,
	 * the cable connected can be standard typeC. However, its jurisdiction
	 * is limited to PD capable designs only. Hence, for non-PD type designs
	 * reset legacy cable detection by disabling/enabling typeC mode.
	 */
	if (chg->pd_not_supported && (val & TYPEC_LEGACY_CABLE_STATUS_BIT)) {
		pval.intval = POWER_SUPPLY_TYPEC_PR_NONE;
		smblib_set_prop_typec_power_role(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable TYPEC rc=%d\n", rc);
			return rc;
		}

		/* delay before enabling typeC */
		msleep(50);

		pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
		smblib_set_prop_typec_power_role(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable TYPEC rc=%d\n", rc);
			return rc;
		}
	}

	smblib_apsd_enable(chg, true);
	smblib_hvdcp_detect_enable(chg, false);

	rc = smblib_masked_write(chg, TYPE_C_CFG_REG,
				BC1P2_START_ON_CC_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev, "failed to write TYPE_C_CFG_REG rc=%d\n",
				rc);

		return rc;
	}

	/* Use simple write to clear interrupts */
	rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_1_REG, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	val = chg->lpd_disabled ? 0 : TYPEC_WATER_DETECTION_INT_EN_BIT;
	/* Use simple write to enable only required interrupts */
	rc = smblib_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
				TYPEC_SRC_BATT_HPWR_INT_EN_BIT | val);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	/* enable try.snk and clear force sink for DRP mode */
	rc = smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
				EN_TRY_SNK_BIT | EN_SNK_ONLY_BIT,
				EN_TRY_SNK_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure TYPE_C_MODE_CFG_REG rc=%d\n",
				rc);
		return rc;
	} else {
		chg->typec_try_mode |= EN_TRY_SNK_BIT;
	}

	/* For PD capable targets configure VCONN for software control */
	if (!chg->pd_not_supported) {
		rc = smblib_masked_write(chg, TYPE_C_VCONN_CONTROL_REG,
				 VCONN_EN_SRC_BIT | VCONN_EN_VALUE_BIT,
				 VCONN_EN_SRC_BIT);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure VCONN for SW control rc=%d\n",
				rc);
			return rc;
		}
	}

	if (chg->chg_param.smb_version != PMI632_SUBTYPE) {
		/*
		 * Enable detection of unoriented debug
		 * accessory in source mode
		 */
		rc = smblib_masked_write(chg, DEBUG_ACCESS_SRC_CFG_REG,
					 EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT,
					 EN_UNORIENTED_DEBUG_ACCESS_SRC_BIT);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure TYPE_C_DEBUG_ACCESS_SRC_CFG_REG rc=%d\n",
					rc);
			return rc;
		}

		rc = smblib_masked_write(chg, USBIN_LOAD_CFG_REG,
				USBIN_IN_COLLAPSE_GF_SEL_MASK |
				USBIN_AICL_STEP_TIMING_SEL_MASK,
				0);
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't set USBIN_LOAD_CFG_REG rc=%d\n", rc);
			return rc;
		}
	}

	/* Set CC threshold to 1.6 V in source mode */
	rc = smblib_masked_write(chg, TYPE_C_EXIT_STATE_CFG_REG,
				SEL_SRC_UPPER_REF_BIT, SEL_SRC_UPPER_REF_BIT);
	if (rc < 0)
		dev_err(chg->dev,
			"Couldn't configure CC threshold voltage rc=%d\n", rc);

#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* zhangkun.PSW.BSP.CHG  2019-04-27  increase OTG_CURRENT_LIMIT to recognize 500G Seagate disk */
            //smblib_write(chg, 0x1152, 0x02);
        rc = smblib_masked_write(chg, DCDC_OTG_CURRENT_LIMIT_CFG_REG,
				DCDC_OTG_CURRENT_LIMIT_1000MA_BIT, DCDC_OTG_CURRENT_LIMIT_1000MA_BIT);
        if (rc < 0)
		dev_err(chg->dev,
			"Couldn't DCDC_OTG_CURRENT_LIMIT_CFG_REG rc=%d\n", rc);
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* zhangkun.PSW.BSP.CHG  2019-04-27  reduce DCD time */
        smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG, DCD_TIMEOUT_SEL_BIT, 0);
#endif

	return rc;
}

static int smb5_configure_micro_usb(struct smb_charger *chg)
{
	int rc;

	/* For micro USB connector, use extcon by default */
	chg->use_extcon = true;
	chg->pd_not_supported = true;

	rc = smblib_masked_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
					MICRO_USB_STATE_CHANGE_INT_EN_BIT,
					MICRO_USB_STATE_CHANGE_INT_EN_BIT);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure Type-C interrupts rc=%d\n", rc);
		return rc;
	}

	if (chg->uusb_moisture_protection_enabled) {
		/* Enable moisture detection interrupt */
		rc = smblib_masked_write(chg, TYPE_C_INTERRUPT_EN_CFG_2_REG,
				TYPEC_WATER_DETECTION_INT_EN_BIT,
				TYPEC_WATER_DETECTION_INT_EN_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable moisture detection interrupt rc=%d\n",
				rc);
			return rc;
		}

		/* Enable uUSB factory mode */
		rc = smblib_masked_write(chg, TYPEC_U_USB_CFG_REG,
					EN_MICRO_USB_FACTORY_MODE_BIT,
					EN_MICRO_USB_FACTORY_MODE_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable uUSB factory mode c=%d\n",
				rc);
			return rc;
		}

		/* Disable periodic monitoring of CC_ID pin */
		rc = smblib_write(chg,
			((chg->chg_param.smb_version == PMI632_SUBTYPE)
				? PMI632_TYPEC_U_USB_WATER_PROTECTION_CFG_REG
				: TYPEC_U_USB_WATER_PROTECTION_CFG_REG), 0);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable periodic monitoring of CC_ID rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/04/24, sjc Add for otg id value change support */
static void otg_enable_pmic_id_value (void)
{
        return;
}

static void otg_disable_pmic_id_value (void)
{
        return;
}

void otg_enable_id_value (void)
{
        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                oppo_set_usbid_active(g_oppo_chip);
                usbid_change_handler(0, g_oppo_chip);
                printk(KERN_ERR "[OPPO_CHG][%s]: usbid_gpio=%d\n",
                                __func__, gpio_get_value(g_oppo_chip->normalchg_gpio.usbid_gpio));
        } else {
                otg_enable_pmic_id_value();
        }
}

void otg_disable_id_value (void)
{
        if (oppo_usbid_check_is_gpio(g_oppo_chip) == true) {
                oppo_set_usbid_sleep(g_oppo_chip);
                usbid_change_handler(0, g_oppo_chip);
                printk(KERN_ERR "[OPPO_CHG][%s]: usbid_gpio=%d\n",
                                __func__, gpio_get_value(g_oppo_chip->normalchg_gpio.usbid_gpio));
        } else {
                otg_disable_pmic_id_value();
        }
}
#endif

#define RAW_ITERM(iterm_ma, max_range)				\
		div_s64((int64_t)iterm_ma * ADC_CHG_ITERM_MASK, max_range)
static int smb5_configure_iterm_thresholds_adc(struct smb5 *chip)
{
	u8 *buf;
	int rc = 0;
	s16 raw_hi_thresh, raw_lo_thresh, max_limit_ma;
	struct smb_charger *chg = &chip->chg;

	if (chip->chg.chg_param.smb_version == PMI632_SUBTYPE)
		max_limit_ma = ITERM_LIMITS_PMI632_MA;
	else
		max_limit_ma = ITERM_LIMITS_PM8150B_MA;

	if (chip->dt.term_current_thresh_hi_ma < (-1 * max_limit_ma)
		|| chip->dt.term_current_thresh_hi_ma > max_limit_ma
		|| chip->dt.term_current_thresh_lo_ma < (-1 * max_limit_ma)
		|| chip->dt.term_current_thresh_lo_ma > max_limit_ma) {
		dev_err(chg->dev, "ITERM threshold out of range rc=%d\n", rc);
		return -EINVAL;
	}

	/*
	 * Conversion:
	 *	raw (A) = (term_current * ADC_CHG_ITERM_MASK) / max_limit_ma
	 * Note: raw needs to be converted to big-endian format.
	 */

	if (chip->dt.term_current_thresh_hi_ma) {
		raw_hi_thresh = RAW_ITERM(chip->dt.term_current_thresh_hi_ma,
					max_limit_ma);
		raw_hi_thresh = sign_extend32(raw_hi_thresh, 15);
		buf = (u8 *)&raw_hi_thresh;
		raw_hi_thresh = buf[1] | (buf[0] << 8);

		rc = smblib_batch_write(chg, CHGR_ADC_ITERM_UP_THD_MSB_REG,
				(u8 *)&raw_hi_thresh, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ITERM threshold HIGH rc=%d\n",
					rc);
			return rc;
		}
	}

	if (chip->dt.term_current_thresh_lo_ma) {
		raw_lo_thresh = RAW_ITERM(chip->dt.term_current_thresh_lo_ma,
					max_limit_ma);
		raw_lo_thresh = sign_extend32(raw_lo_thresh, 15);
		buf = (u8 *)&raw_lo_thresh;
		raw_lo_thresh = buf[1] | (buf[0] << 8);

		rc = smblib_batch_write(chg, CHGR_ADC_ITERM_LO_THD_MSB_REG,
				(u8 *)&raw_lo_thresh, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ITERM threshold LOW rc=%d\n",
					rc);
			return rc;
		}
	}

	return rc;
}

static int smb5_configure_iterm_thresholds(struct smb5 *chip)
{
	int rc = 0;
	struct smb_charger *chg = &chip->chg;

	switch (chip->dt.term_current_src) {
	case ITERM_SRC_ADC:
		if (chip->chg.chg_param.smb_version == PM8150B_SUBTYPE) {
			rc = smblib_masked_write(chg, CHGR_ADC_TERM_CFG_REG,
					TERM_BASED_ON_SYNC_CONV_OR_SAMPLE_CNT,
					TERM_BASED_ON_SAMPLE_CNT);
			if (rc < 0) {
				dev_err(chg->dev, "Couldn't configure ADC_ITERM_CFG rc=%d\n",
						rc);
				return rc;
			}
		}
		rc = smb5_configure_iterm_thresholds_adc(chip);
		break;
	default:
		break;
	}

	return rc;
}

static int smb5_configure_mitigation(struct smb_charger *chg)
{
	int rc;
	u8 chan = 0, src_cfg = 0;

	if (!chg->hw_die_temp_mitigation && !chg->hw_connector_mitigation &&
			!chg->hw_skin_temp_mitigation) {
		src_cfg = THERMREG_SW_ICL_ADJUST_BIT;
	} else {
		if (chg->hw_die_temp_mitigation) {
			chan = DIE_TEMP_CHANNEL_EN_BIT;
			src_cfg = THERMREG_DIE_ADC_SRC_EN_BIT
				| THERMREG_DIE_CMP_SRC_EN_BIT;
		}

		if (chg->hw_connector_mitigation) {
			chan |= CONN_THM_CHANNEL_EN_BIT;
			src_cfg |= THERMREG_CONNECTOR_ADC_SRC_EN_BIT;
		}

		if (chg->hw_skin_temp_mitigation) {
			chan |= MISC_THM_CHANNEL_EN_BIT;
			src_cfg |= THERMREG_SKIN_ADC_SRC_EN_BIT;
		}

		rc = smblib_masked_write(chg, BATIF_ADC_CHANNEL_EN_REG,
			CONN_THM_CHANNEL_EN_BIT | DIE_TEMP_CHANNEL_EN_BIT |
			MISC_THM_CHANNEL_EN_BIT, chan);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't enable ADC channel rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, MISC_THERMREG_SRC_CFG_REG,
		THERMREG_SW_ICL_ADJUST_BIT | THERMREG_DIE_ADC_SRC_EN_BIT |
		THERMREG_DIE_CMP_SRC_EN_BIT | THERMREG_SKIN_ADC_SRC_EN_BIT |
		SKIN_ADC_CFG_BIT | THERMREG_CONNECTOR_ADC_SRC_EN_BIT, src_cfg);
	if (rc < 0) {
		dev_err(chg->dev,
				"Couldn't configure THERM_SRC reg rc=%d\n", rc);
		return rc;
	}

	return 0;
}

static int smb5_init_dc_peripheral(struct smb_charger *chg)
{
	int rc = 0;

	/* PMI632 does not have DC peripheral */
	if (chg->chg_param.smb_version == PMI632_SUBTYPE)
		return 0;

	/* Set DCIN ICL to 100 mA */
	rc = smblib_set_charge_param(chg, &chg->param.dc_icl, DCIN_ICL_MIN_UA);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set dc_icl rc=%d\n", rc);
		return rc;
	}

	/* Disable DC Input missing poller function */
	rc = smblib_masked_write(chg, DCIN_LOAD_CFG_REG,
					INPUT_MISS_POLL_EN_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't disable DC Input missing poller rc=%d\n", rc);
		return rc;
	}

	return rc;
}

static int smb5_init_hw(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	int rc, type = 0;
	u8 val = 0;
#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Kun.Zhang@BSP.CHG.Basic, 2019/04/09, Remove for charging */
       u8 mask = 0;
#endif
	union power_supply_propval pval;

	if (chip->dt.no_battery)
		chg->fake_capacity = 50;

	if (chip->dt.batt_profile_fcc_ua < 0)
		smblib_get_charge_param(chg, &chg->param.fcc,
				&chg->batt_profile_fcc_ua);

	if (chip->dt.batt_profile_fv_uv < 0)
		smblib_get_charge_param(chg, &chg->param.fv,
				&chg->batt_profile_fv_uv);

	smblib_get_charge_param(chg, &chg->param.usb_icl,
				&chg->default_icl_ua);
	smblib_get_charge_param(chg, &chg->param.aicl_5v_threshold,
				&chg->default_aicl_5v_threshold_mv);
	chg->aicl_5v_threshold_mv = chg->default_aicl_5v_threshold_mv;
	smblib_get_charge_param(chg, &chg->param.aicl_cont_threshold,
				&chg->default_aicl_cont_threshold_mv);
	chg->aicl_cont_threshold_mv = chg->default_aicl_cont_threshold_mv;

	if (chg->charger_temp_max == -EINVAL) {
		rc = smblib_get_thermal_threshold(chg,
					DIE_REG_H_THRESHOLD_MSB_REG,
					&chg->charger_temp_max);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't get charger_temp_max rc=%d\n",
					rc);
			return rc;
		}
	}

	/*
	 * If SW thermal regulation WA is active then all the HW temperature
	 * comparators need to be disabled to prevent HW thermal regulation,
	 * apart from DIE_TEMP analog comparator for SHDN regulation.
	 */
	if (chg->wa_flags & SW_THERM_REGULATION_WA) {
		rc = smblib_write(chg, MISC_THERMREG_SRC_CFG_REG,
					THERMREG_SW_ICL_ADJUST_BIT
					| THERMREG_DIE_CMP_SRC_EN_BIT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't disable HW thermal regulation rc=%d\n",
				rc);
			return rc;
		}
	} else {
		/* configure temperature mitigation */
		rc = smb5_configure_mitigation(chg);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure mitigation rc=%d\n",
					rc);
			return rc;
		}
	}

	/*
	 * Disable HVDCP autonomous mode operation by default, providing a DT
	 * knob to turn it on if required. Additionally, if specified in DT,
	 * disable HVDCP and HVDCP authentication algorithm.
	 */
	val = (chg->hvdcp_disable) ? 0 :
		(HVDCP_AUTH_ALG_EN_CFG_BIT | HVDCP_EN_BIT);
	if (chip->dt.hvdcp_autonomous)
		val |= HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT;

	rc = smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
			(HVDCP_AUTH_ALG_EN_CFG_BIT | HVDCP_EN_BIT |
			 HVDCP_AUTONOMOUS_MODE_EN_CFG_BIT),
			val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure HVDCP rc=%d\n", rc);
		return rc;
	}

	/*
	 * PMI632 can have the connector type defined by a dedicated register
	 * PMI632_TYPEC_MICRO_USB_MODE_REG or by a common TYPEC_U_USB_CFG_REG.
	 */
	if (chg->chg_param.smb_version == PMI632_SUBTYPE) {
		rc = smblib_read(chg, PMI632_TYPEC_MICRO_USB_MODE_REG, &val);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't read USB mode rc=%d\n", rc);
			return rc;
		}
		type = !!(val & MICRO_USB_MODE_ONLY_BIT);
	}

	/*
	 * If PMI632_TYPEC_MICRO_USB_MODE_REG is not set and for all non-PMI632
	 * check the connector type using TYPEC_U_USB_CFG_REG.
	 */
	if (!type) {
		rc = smblib_read(chg, TYPEC_U_USB_CFG_REG, &val);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't read U_USB config rc=%d\n",
					rc);
			return rc;
		}

		type = !!(val & EN_MICRO_USB_MODE_BIT);
	}

	pr_debug("Connector type=%s\n", type ? "Micro USB" : "TypeC");

	if (type) {
		chg->connector_type = POWER_SUPPLY_CONNECTOR_MICRO_USB;
		rc = smb5_configure_micro_usb(chg);
	} else {
		chg->connector_type = POWER_SUPPLY_CONNECTOR_TYPEC;
		rc = smb5_configure_typec(chg);
	}
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure TypeC/micro-USB mode rc=%d\n", rc);
		return rc;
	}

	/*
	 * PMI632 based hw init:
	 * - Rerun APSD to ensure proper charger detection if device
	 *   boots with charger connected.
	 * - Initialize flash module for PMI632
	 */
	if (chg->chg_param.smb_version == PMI632_SUBTYPE) {
		schgm_flash_init(chg);
	}

	smblib_rerun_apsd_if_required(chg);

	/* Use ICL results from HW */
	rc = smblib_icl_override(chg, HW_AUTO_MODE);
	if (rc < 0) {
		pr_err("Couldn't disable ICL override rc=%d\n", rc);
		return rc;
	}

	/* set OTG current limit */
	rc = smblib_set_charge_param(chg, &chg->param.otg_cl, chg->otg_cl_ua);
	if (rc < 0) {
		pr_err("Couldn't set otg current limit rc=%d\n", rc);
		return rc;
	}

	/* vote 0mA on usb_icl for non battery platforms */
	vote(chg->usb_icl_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->dc_suspend_votable,
		DEFAULT_VOTER, chip->dt.no_battery, 0);
	vote(chg->fcc_votable, HW_LIMIT_VOTER,
		chip->dt.batt_profile_fcc_ua > 0, chip->dt.batt_profile_fcc_ua);
	vote(chg->fv_votable, HW_LIMIT_VOTER,
		chip->dt.batt_profile_fv_uv > 0, chip->dt.batt_profile_fv_uv);
	vote(chg->fcc_votable,
		BATT_PROFILE_VOTER, chg->batt_profile_fcc_ua > 0,
		chg->batt_profile_fcc_ua);
	vote(chg->fv_votable,
		BATT_PROFILE_VOTER, chg->batt_profile_fv_uv > 0,
		chg->batt_profile_fv_uv);

	/* Some h/w limit maximum supported ICL */
	vote(chg->usb_icl_votable, HW_LIMIT_VOTER,
			chg->hw_max_icl_ua > 0, chg->hw_max_icl_ua);

	/* Initialize DC peripheral configurations */
	rc = smb5_init_dc_peripheral(chg);
	if (rc < 0)
		return rc;
            /*
             * AICL configuration:
             * AICL ADC disable
             */
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Jianchao.Shi@BSP.CHG.Basic, 2017/04/06, sjc Modify for charging */
            rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
                            SUSPEND_ON_COLLAPSE_USBIN_BIT | USBIN_AICL_START_AT_MAX_BIT
                                    | USBIN_AICL_ADC_EN_BIT | USBIN_AICL_RERUN_EN_BIT, USBIN_AICL_RERUN_EN_BIT);
            if (rc < 0) {
                    dev_err(chg->dev, "Couldn't configure AICL rc=%d\n", rc);
                    return rc;
            }
#else

	/*
	 * AICL configuration: enable aicl and aicl rerun and based on DT
	 * configuration enable/disable ADB based AICL and Suspend on collapse.
	 */
	mask = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_ADC_EN_BIT
			| USBIN_AICL_EN_BIT |
			SUSPEND_ON_COLLAPSE_USBIN_BIT;

	/* Disable AICL if battery is not present. */
	rc = smblib_get_prop_batt_present(chg, &pval);
	if (rc < 0) {
		pr_err("Couldn't get battery status rc=%d\n", rc);
		return rc;
	}

	if (pval.intval && !chg->aicl_disable) {
		val = USBIN_AICL_PERIODIC_RERUN_EN_BIT | USBIN_AICL_EN_BIT;
		pr_info("battery present = %d AICL on\n", pval.intval);
	} else {
		val = 0;
		pr_err("battery present = %d AICL off\n", pval.intval);
	}

	if (!chip->dt.disable_suspend_on_collapse)
		val |= SUSPEND_ON_COLLAPSE_USBIN_BIT;
	if (chip->dt.adc_based_aicl)
		val |= USBIN_AICL_ADC_EN_BIT;

	rc = smblib_masked_write(chg, USBIN_AICL_OPTIONS_CFG_REG,
			mask, val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't config AICL rc=%d\n", rc);
		return rc;
	}
#endif

	rc = smblib_write(chg, AICL_RERUN_TIME_CFG_REG,
				AICL_RERUN_TIME_12S_VAL);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure AICL rerun interval rc=%d\n", rc);
		return rc;
	}

	/* enable the charging path */
	rc = vote(chg->chg_disable_votable, DEFAULT_VOTER, false, 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't enable charging rc=%d\n", rc);
		return rc;
	}

	/* configure VBUS for software control */
	rc = smblib_masked_write(chg, DCDC_OTG_CFG_REG, OTG_EN_SRC_CFG_BIT, 0);
	if (rc < 0) {
		dev_err(chg->dev,
			"Couldn't configure VBUS for SW control rc=%d\n", rc);
		return rc;
	}

	val = (ilog2(chip->dt.wd_bark_time / 16) << BARK_WDOG_TIMEOUT_SHIFT)
			& BARK_WDOG_TIMEOUT_MASK;
	val |= BITE_WDOG_TIMEOUT_8S;
	val |= (chip->dt.wd_snarl_time_cfg << SNARL_WDOG_TIMEOUT_SHIFT)
			& SNARL_WDOG_TIMEOUT_MASK;

	rc = smblib_masked_write(chg, SNARL_BARK_BITE_WD_CFG_REG,
			BITE_WDOG_DISABLE_CHARGING_CFG_BIT |
			SNARL_WDOG_TIMEOUT_MASK | BARK_WDOG_TIMEOUT_MASK |
			BITE_WDOG_TIMEOUT_MASK,
			val);
	if (rc < 0) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* enable WD BARK and enable it on plugin */
	val = WDOG_TIMER_EN_ON_PLUGIN_BIT | BARK_WDOG_INT_EN_BIT;
	rc = smblib_masked_write(chg, WD_CFG_REG,
			WATCHDOG_TRIGGER_AFP_EN_BIT |
			WDOG_TIMER_EN_ON_PLUGIN_BIT |
			BARK_WDOG_INT_EN_BIT, val);
	if (rc < 0) {
		pr_err("Couldn't configue WD config rc=%d\n", rc);
		return rc;
	}

	/* set termination current threshold values */
	rc = smb5_configure_iterm_thresholds(chip);
	if (rc < 0) {
		pr_err("Couldn't configure ITERM thresholds rc=%d\n",
				rc);
		return rc;
	}

	/* configure float charger options */
	switch (chip->dt.float_option) {
	case FLOAT_SDP:
		val = FORCE_FLOAT_SDP_CFG_BIT;
		break;
	case DISABLE_CHARGING:
		val = FLOAT_DIS_CHGING_CFG_BIT;
		break;
	case SUSPEND_INPUT:
		val = SUSPEND_FLOAT_CFG_BIT;
		break;
	case FLOAT_DCP:
	default:
		val = 0;
		break;
	}

	chg->float_cfg = val;
	/* Update float charger setting and set DCD timeout 300ms */
	rc = smblib_masked_write(chg, USBIN_OPTIONS_2_CFG_REG,
				FLOAT_OPTIONS_MASK | DCD_TIMEOUT_SEL_BIT, val);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't change float charger setting rc=%d\n",
			rc);
		return rc;
	}

	switch (chip->dt.chg_inhibit_thr_mv) {
	case 50:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_50MV);
		break;
	case 100:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_100MV);
		break;
	case 200:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_200MV);
		break;
	case 300:
		rc = smblib_masked_write(chg, CHARGE_INHIBIT_THRESHOLD_CFG_REG,
				CHARGE_INHIBIT_THRESHOLD_MASK,
				INHIBIT_ANALOG_VFLT_MINUS_300MV);
		break;
	case 0:
		rc = smblib_masked_write(chg, CHGR_CFG2_REG,
				CHARGER_INHIBIT_BIT, 0);
	default:
		break;
	}

	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure charge inhibit threshold rc=%d\n",
			rc);
		return rc;
	}

	rc = smblib_write(chg, CHGR_FAST_CHARGE_SAFETY_TIMER_CFG_REG,
					FAST_CHARGE_SAFETY_TIMER_768_MIN);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set CHGR_FAST_CHARGE_SAFETY_TIMER_CFG_REG rc=%d\n",
			rc);
		return rc;
	}

	rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
				(chip->dt.auto_recharge_vbat_mv != -EINVAL) ?
				VBAT_BASED_RECHG_BIT : 0);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure VBAT-rechg CHG_CFG2_REG rc=%d\n",
			rc);
		return rc;
	}

	/* program the auto-recharge VBAT threshold */
	if (chip->dt.auto_recharge_vbat_mv != -EINVAL) {
		u32 temp = VBAT_TO_VRAW_ADC(chip->dt.auto_recharge_vbat_mv);

		temp = ((temp & 0xFF00) >> 8) | ((temp & 0xFF) << 8);
		rc = smblib_batch_write(chg,
			CHGR_ADC_RECHARGE_THRESHOLD_MSB_REG, (u8 *)&temp, 2);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure ADC_RECHARGE_THRESHOLD REG rc=%d\n",
				rc);
			return rc;
		}
		/* Program the sample count for VBAT based recharge to 3 */
		rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
					NO_OF_SAMPLE_FOR_RCHG,
					2 << NO_OF_SAMPLE_FOR_RCHG_SHIFT);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = smblib_masked_write(chg, CHGR_CFG2_REG, RECHG_MASK,
				(chip->dt.auto_recharge_soc != -EINVAL) ?
				SOC_BASED_RECHG_BIT : VBAT_BASED_RECHG_BIT);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure SOC-rechg CHG_CFG2_REG rc=%d\n",
			rc);
		return rc;
	}

	/* program the auto-recharge threshold */
	if (chip->dt.auto_recharge_soc != -EINVAL) {
		pval.intval = chip->dt.auto_recharge_soc;
		rc = smblib_set_prop_rechg_soc_thresh(chg, &pval);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHG_RCHG_SOC_REG rc=%d\n",
					rc);
			return rc;
		}

		/* Program the sample count for SOC based recharge to 1 */
		rc = smblib_masked_write(chg, CHGR_NO_SAMPLE_TERM_RCHG_CFG_REG,
						NO_OF_SAMPLE_FOR_RCHG, 0);
		if (rc < 0) {
			dev_err(chg->dev, "Couldn't configure CHGR_NO_SAMPLE_FOR_TERM_RCHG_CFG rc=%d\n",
				rc);
			return rc;
		}
	}

	rc = smblib_disable_hw_jeita(chg, true);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't set hw jeita rc=%d\n", rc);
		return rc;
	}

	rc = smblib_masked_write(chg, DCDC_ENG_SDCDC_CFG5_REG,
			ENG_SDCDC_BAT_HPWR_MASK, BOOST_MODE_THRESH_3P6_V);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DCDC_ENG_SDCDC_CFG5 rc=%d\n",
				rc);
		return rc;
	}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Yichun.Chen  PSW.BSP.CHG  2018-05-11  init JEITA range */
            if (0) {                                      /* when 1k BAT_THERMAL resistance    */
                    smblib_write(chg, 0x1094, 0x07);
                    smblib_write(chg, 0x1095, 0xF2);       /*      soft hot threshold   90C     */
                    smblib_write(chg, 0x1096, 0x5D);
                    smblib_write(chg, 0x1097, 0xFC);       /*      soft cold threshold -30C     */
                    smblib_write(chg, 0x1098, 0x07);
                    smblib_write(chg, 0x1099, 0x67);       /*      hard hot threshold   95C     */
                    smblib_write(chg, 0x109A, 0x59);
                    smblib_write(chg, 0x109B, 0x68);       /*      hard cold threshold -35C     */
            } else {                                      /* when 5p1k BAT_THERMAL resistance  */
                    smblib_write(chg, 0x1094, 0x13);
                    smblib_write(chg, 0x1095, 0xBF);       /*      soft hot threshold   90C     */
                    smblib_write(chg, 0x1096, 0x5E);
                    smblib_write(chg, 0x1097, 0x68);       /*      soft cold threshold -30C     */
                    smblib_write(chg, 0x1098, 0x13);
                    smblib_write(chg, 0x1099, 0x63);       /*      hard hot threshold   95C     */
                    smblib_write(chg, 0x109A, 0x5A);
                    smblib_write(chg, 0x109B, 0x12);       /*      hard cold threshold -35C     */
            }
#endif
    
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Yichun.Chen  PSW.BSP.CHG  2018-06-08  increase OTG_CURRENT_LIMIT to recognize 500G Seagate disk */
            smblib_write(chg, 0x1152, 0x02);
#endif
    
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Yichun.Chen  PSW.BSP.CHG  2018-06-27  reduce DCD time */
            rc = smblib_masked_write(chg, 0x1363, 0x20, 0);
        if (rc < 0) {
            chg_err("failed to config DCD time rc = %d\n", rc);
            return rc;
        }
#endif

	if (chg->connector_pull_up != -EINVAL) {
		rc = smb5_configure_internal_pull(chg, CONN_THERM,
				get_valid_pullup(chg->connector_pull_up));
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure CONN_THERM pull-up rc=%d\n",
				rc);
			return rc;
		}
	}

	if (chg->smb_pull_up != -EINVAL) {
		rc = smb5_configure_internal_pull(chg, SMB_THERM,
				get_valid_pullup(chg->smb_pull_up));
		if (rc < 0) {
			dev_err(chg->dev,
				"Couldn't configure SMB pull-up rc=%d\n",
				rc);
			return rc;
		}
	}

	return rc;
}

static int smb5_post_init(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval pval;
	int rc;

	/*
	 * In case the usb path is suspended, we would have missed disabling
	 * the icl change interrupt because the interrupt could have been
	 * not requested
	 */
	rerun_election(chg->usb_icl_votable);

	/* configure power role for dual-role */
	pval.intval = POWER_SUPPLY_TYPEC_PR_DUAL;
	rc = smblib_set_prop_typec_power_role(chg, &pval);
	if (rc < 0) {
		dev_err(chg->dev, "Couldn't configure DRP role rc=%d\n",
				rc);
		return rc;
	}

	rerun_election(chg->temp_change_irq_disable_votable);

	return 0;
}

/****************************
 * DETERMINE INITIAL STATUS *
 ****************************/

static int smb5_determine_initial_status(struct smb5 *chip)
{
	struct smb_irq_data irq_data = {chip, "determine-initial-status"};
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval val;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		return rc;
	}
	chg->early_usb_attach = val.intval;

	if (chg->bms_psy)
		smblib_suspend_on_debug_battery(chg);

	usb_plugin_irq_handler(0, &irq_data);
	typec_attach_detach_irq_handler(0, &irq_data);
	typec_state_change_irq_handler(0, &irq_data);
	usb_source_change_irq_handler(0, &irq_data);
	chg_state_change_irq_handler(0, &irq_data);
	icl_change_irq_handler(0, &irq_data);
	batt_temp_changed_irq_handler(0, &irq_data);
	wdog_bark_irq_handler(0, &irq_data);
	typec_or_rid_detection_change_irq_handler(0, &irq_data);
	wdog_snarl_irq_handler(0, &irq_data);

	return 0;
}

/**************************
 * INTERRUPT REGISTRATION *
 **************************/

static struct smb_irq_info smb5_irqs[] = {
	/* CHARGER IRQs */
	[CHGR_ERROR_IRQ] = {
		.name		= "chgr-error",
		.handler	= default_irq_handler,
	},
	[CHG_STATE_CHANGE_IRQ] = {
		.name		= "chg-state-change",
		.handler	= chg_state_change_irq_handler,
		.wake		= true,
	},
	[STEP_CHG_STATE_CHANGE_IRQ] = {
		.name		= "step-chg-state-change",
	},
	[STEP_CHG_SOC_UPDATE_FAIL_IRQ] = {
		.name		= "step-chg-soc-update-fail",
	},
	[STEP_CHG_SOC_UPDATE_REQ_IRQ] = {
		.name		= "step-chg-soc-update-req",
	},
	[FG_FVCAL_QUALIFIED_IRQ] = {
		.name		= "fg-fvcal-qualified",
	},
	[VPH_ALARM_IRQ] = {
		.name		= "vph-alarm",
	},
	[VPH_DROP_PRECHG_IRQ] = {
		.name		= "vph-drop-prechg",
	},
	/* DCDC IRQs */
	[OTG_FAIL_IRQ] = {
		.name		= "otg-fail",
		.handler	= default_irq_handler,
	},
	[OTG_OC_DISABLE_SW_IRQ] = {
		.name		= "otg-oc-disable-sw",
	},
	[OTG_OC_HICCUP_IRQ] = {
		.name		= "otg-oc-hiccup",
	},
	[BSM_ACTIVE_IRQ] = {
		.name		= "bsm-active",
	},
	[HIGH_DUTY_CYCLE_IRQ] = {
		.name		= "high-duty-cycle",
		.handler	= high_duty_cycle_irq_handler,
		.wake		= true,
	},
	[INPUT_CURRENT_LIMITING_IRQ] = {
		.name		= "input-current-limiting",
		.handler	= default_irq_handler,
	},
	[CONCURRENT_MODE_DISABLE_IRQ] = {
		.name		= "concurrent-mode-disable",
	},
	[SWITCHER_POWER_OK_IRQ] = {
		.name		= "switcher-power-ok",
		.handler	= switcher_power_ok_irq_handler,
	},
	/* BATTERY IRQs */
	[BAT_TEMP_IRQ] = {
		.name		= "bat-temp",
		.handler	= batt_temp_changed_irq_handler,
		.wake		= true,
	},
	[ALL_CHNL_CONV_DONE_IRQ] = {
		.name		= "all-chnl-conv-done",
	},
	[BAT_OV_IRQ] = {
		.name		= "bat-ov",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_LOW_IRQ] = {
		.name		= "bat-low",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_THERM_OR_ID_MISSING_IRQ] = {
		.name		= "bat-therm-or-id-missing",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BAT_TERMINAL_MISSING_IRQ] = {
		.name		= "bat-terminal-missing",
		.handler	= batt_psy_changed_irq_handler,
	},
	[BUCK_OC_IRQ] = {
		.name		= "buck-oc",
	},
	[VPH_OV_IRQ] = {
		.name		= "vph-ov",
	},
	/* USB INPUT IRQs */
	[USBIN_COLLAPSE_IRQ] = {
		.name		= "usbin-collapse",
		.handler	= default_irq_handler,
	},
	[USBIN_VASHDN_IRQ] = {
		.name		= "usbin-vashdn",
		.handler	= default_irq_handler,
	},
	[USBIN_UV_IRQ] = {
		.name		= "usbin-uv",
		.handler	= usbin_uv_irq_handler,
		.wake		= true,
		.storm_data	= {true, 3000, 5},
	},
	[USBIN_OV_IRQ] = {
		.name		= "usbin-ov",
		.handler	= usbin_ov_irq_handler,
	},
	[USBIN_PLUGIN_IRQ] = {
		.name		= "usbin-plugin",
		.handler	= usb_plugin_irq_handler,
		.wake           = true,
	},
	[USBIN_REVI_CHANGE_IRQ] = {
		.name		= "usbin-revi-change",
	},
	[USBIN_SRC_CHANGE_IRQ] = {
		.name		= "usbin-src-change",
		.handler	= usb_source_change_irq_handler,
		.wake           = true,
	},
	[USBIN_ICL_CHANGE_IRQ] = {
		.name		= "usbin-icl-change",
		.handler	= icl_change_irq_handler,
		.wake           = true,
	},
	/* DC INPUT IRQs */
	[DCIN_VASHDN_IRQ] = {
		.name		= "dcin-vashdn",
	},
	[DCIN_UV_IRQ] = {
		.name		= "dcin-uv",
		.handler	= dcin_uv_irq_handler,
		.wake		= true,
	},
	[DCIN_OV_IRQ] = {
		.name		= "dcin-ov",
		.handler	= default_irq_handler,
	},
	[DCIN_PLUGIN_IRQ] = {
		.name		= "dcin-plugin",
		.handler	= dc_plugin_irq_handler,
		.wake           = true,
	},
	[DCIN_REVI_IRQ] = {
		.name		= "dcin-revi",
	},
	[DCIN_PON_IRQ] = {
		.name		= "dcin-pon",
		.handler	= default_irq_handler,
	},
	[DCIN_EN_IRQ] = {
		.name		= "dcin-en",
		.handler	= default_irq_handler,
	},
	/* TYPEC IRQs */
	[TYPEC_OR_RID_DETECTION_CHANGE_IRQ] = {
		.name		= "typec-or-rid-detect-change",
		.handler	= typec_or_rid_detection_change_irq_handler,
		.wake           = true,
	},
	[TYPEC_VPD_DETECT_IRQ] = {
		.name		= "typec-vpd-detect",
	},
	[TYPEC_CC_STATE_CHANGE_IRQ] = {
		.name		= "typec-cc-state-change",
		.handler	= typec_state_change_irq_handler,
		.wake           = true,
	},
	[TYPEC_VCONN_OC_IRQ] = {
		.name		= "typec-vconn-oc",
		.handler	= default_irq_handler,
	},
	[TYPEC_VBUS_CHANGE_IRQ] = {
		.name		= "typec-vbus-change",
	},
	[TYPEC_ATTACH_DETACH_IRQ] = {
		.name		= "typec-attach-detach",
		.handler	= typec_attach_detach_irq_handler,
		.wake		= true,
	},
	[TYPEC_LEGACY_CABLE_DETECT_IRQ] = {
		.name		= "typec-legacy-cable-detect",
		.handler	= default_irq_handler,
	},
	[TYPEC_TRY_SNK_SRC_DETECT_IRQ] = {
		.name		= "typec-try-snk-src-detect",
	},
	/* MISCELLANEOUS IRQs */
	[WDOG_SNARL_IRQ] = {
		.name		= "wdog-snarl",
		.handler	= wdog_snarl_irq_handler,
		.wake		= true,
	},
	[WDOG_BARK_IRQ] = {
		.name		= "wdog-bark",
		.handler	= wdog_bark_irq_handler,
		.wake		= true,
	},
	[AICL_FAIL_IRQ] = {
		.name		= "aicl-fail",
	},
	[AICL_DONE_IRQ] = {
		.name		= "aicl-done",
		.handler	= default_irq_handler,
	},
	[SMB_EN_IRQ] = {
		.name		= "smb-en",
		.handler	= smb_en_irq_handler,
	},
	[IMP_TRIGGER_IRQ] = {
		.name		= "imp-trigger",
	},
	/*
	 * triggered when DIE or SKIN or CONNECTOR temperature across
	 * either of the _REG_L, _REG_H, _RST, or _SHDN thresholds
	 */
	[TEMP_CHANGE_IRQ] = {
		.name		= "temp-change",
		.handler	= temp_change_irq_handler,
		.wake		= true,
	},
	[TEMP_CHANGE_SMB_IRQ] = {
		.name		= "temp-change-smb",
	},
	/* FLASH */
	[VREG_OK_IRQ] = {
		.name		= "vreg-ok",
	},
	[ILIM_S2_IRQ] = {
		.name		= "ilim2-s2",
		.handler	= schgm_flash_ilim2_irq_handler,
	},
	[ILIM_S1_IRQ] = {
		.name		= "ilim1-s1",
	},
	[VOUT_DOWN_IRQ] = {
		.name		= "vout-down",
	},
	[VOUT_UP_IRQ] = {
		.name		= "vout-up",
	},
	[FLASH_STATE_CHANGE_IRQ] = {
		.name		= "flash-state-change",
		.handler	= schgm_flash_state_change_irq_handler,
	},
	[TORCH_REQ_IRQ] = {
		.name		= "torch-req",
	},
	[FLASH_EN_IRQ] = {
		.name		= "flash-en",
	},
	/* SDAM */
	[SDAM_STS_IRQ] = {
		.name		= "sdam-sts",
		.handler	= sdam_sts_change_irq_handler,
	},
};

static int smb5_get_irq_index_byname(const char *irq_name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (strcmp(smb5_irqs[i].name, irq_name) == 0)
			return i;
	}

	return -ENOENT;
}

static int smb5_request_interrupt(struct smb5 *chip,
				struct device_node *node, const char *irq_name)
{
	struct smb_charger *chg = &chip->chg;
	int rc, irq, irq_index;
	struct smb_irq_data *irq_data;

	irq = of_irq_get_byname(node, irq_name);
	if (irq < 0) {
		pr_err("Couldn't get irq %s byname\n", irq_name);
		return irq;
	}

	irq_index = smb5_get_irq_index_byname(irq_name);
	if (irq_index < 0) {
		pr_err("%s is not a defined irq\n", irq_name);
		return irq_index;
	}

	if (!smb5_irqs[irq_index].handler)
		return 0;

	irq_data = devm_kzalloc(chg->dev, sizeof(*irq_data), GFP_KERNEL);
	if (!irq_data)
		return -ENOMEM;

	irq_data->parent_data = chip;
	irq_data->name = irq_name;
	irq_data->storm_data = smb5_irqs[irq_index].storm_data;
	mutex_init(&irq_data->storm_data.storm_lock);

	smb5_irqs[irq_index].enabled = true;
	rc = devm_request_threaded_irq(chg->dev, irq, NULL,
					smb5_irqs[irq_index].handler,
					IRQF_ONESHOT, irq_name, irq_data);
	if (rc < 0) {
		pr_err("Couldn't request irq %d\n", irq);
		return rc;
	}

	smb5_irqs[irq_index].irq = irq;
	smb5_irqs[irq_index].irq_data = irq_data;
	if (smb5_irqs[irq_index].wake)
		enable_irq_wake(irq);

	return rc;
}

static int smb5_request_interrupts(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	struct device_node *node = chg->dev->of_node;
	struct device_node *child;
	int rc = 0;
	const char *name;
	struct property *prop;

	for_each_available_child_of_node(node, child) {
		of_property_for_each_string(child, "interrupt-names",
					    prop, name) {
			rc = smb5_request_interrupt(chip, child, name);
			if (rc < 0)
				return rc;
		}
	}

	vote(chg->limited_irq_disable_votable, CHARGER_TYPE_VOTER, true, 0);
	vote(chg->hdc_irq_disable_votable, CHARGER_TYPE_VOTER, true, 0);

	return rc;
}

static void smb5_free_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (smb5_irqs[i].irq > 0) {
			if (smb5_irqs[i].wake)
				disable_irq_wake(smb5_irqs[i].irq);

			devm_free_irq(chg->dev, smb5_irqs[i].irq,
						smb5_irqs[i].irq_data);
		}
	}
}

static void smb5_disable_interrupts(struct smb_charger *chg)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(smb5_irqs); i++) {
		if (smb5_irqs[i].irq > 0)
			disable_irq(smb5_irqs[i].irq);
	}
}

#if defined(CONFIG_DEBUG_FS)

static int force_batt_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->batt_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_batt_psy_update_ops, NULL,
			force_batt_psy_update_write, "0x%02llx\n");

static int force_usb_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

	power_supply_changed(chg->usb_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_usb_psy_update_ops, NULL,
			force_usb_psy_update_write, "0x%02llx\n");

static int force_dc_psy_update_write(void *data, u64 val)
{
	struct smb_charger *chg = data;

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/09, sjc Add for charging */
        if (chg->dc_psy)
#endif
	power_supply_changed(chg->dc_psy);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(force_dc_psy_update_ops, NULL,
			force_dc_psy_update_write, "0x%02llx\n");

static void smb5_create_debugfs(struct smb5 *chip)
{
	struct dentry *file;

	chip->dfs_root = debugfs_create_dir("charger", NULL);
	if (IS_ERR_OR_NULL(chip->dfs_root)) {
		pr_err("Couldn't create charger debugfs rc=%ld\n",
			(long)chip->dfs_root);
		return;
	}

	file = debugfs_create_file("force_batt_psy_update", 0600,
			    chip->dfs_root, chip, &force_batt_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_batt_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_usb_psy_update", 0600,
			    chip->dfs_root, chip, &force_usb_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_usb_psy_update file rc=%ld\n",
			(long)file);

	file = debugfs_create_file("force_dc_psy_update", 0600,
			    chip->dfs_root, chip, &force_dc_psy_update_ops);
	if (IS_ERR_OR_NULL(file))
		pr_err("Couldn't create force_dc_psy_update file rc=%ld\n",
			(long)file);
}

#else

static void smb5_create_debugfs(struct smb5 *chip)
{}

#endif

static int smb5_show_charger_status(struct smb5 *chip)
{
	struct smb_charger *chg = &chip->chg;
	union power_supply_propval val;
	int usb_present, batt_present, batt_health, batt_charge_type;
	int rc;

	rc = smblib_get_prop_usb_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get usb present rc=%d\n", rc);
		return rc;
	}
	usb_present = val.intval;

	rc = smblib_get_prop_batt_present(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt present rc=%d\n", rc);
		return rc;
	}
	batt_present = val.intval;

	rc = smblib_get_prop_batt_health(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt health rc=%d\n", rc);
		val.intval = POWER_SUPPLY_HEALTH_UNKNOWN;
	}
	batt_health = val.intval;

	rc = smblib_get_prop_batt_charge_type(chg, &val);
	if (rc < 0) {
		pr_err("Couldn't get batt charge type rc=%d\n", rc);
		return rc;
	}
	batt_charge_type = val.intval;

	pr_info("SMB5 status - usb:present=%d type=%d batt:present = %d health = %d charge = %d\n",
		usb_present, chg->real_charger_type,
		batt_present, batt_health, batt_charge_type);
	return rc;
}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump registers */
static bool show_regs_mask;
static ssize_t show_regs_mask_write(struct file *file, const char __user *buff, size_t count, loff_t *ppos)
{
        char mask[16];

        if (copy_from_user(&mask, buff, count)) {
                chg_err("show_regs_mask_write error.\n");
                return -EFAULT;
        }

        if (strncmp(mask, "1", 1) == 0) {
                show_regs_mask = true;
                chg_debug("Show regs mask enable.\n");
        } else if (strncmp(mask, "0", 1) == 0) {
                show_regs_mask = false;
                chg_debug("Show regs mask disable.\n");
        } else {
                show_regs_mask = false;
                return -EFAULT;
        }

        return count;
}

static const struct file_operations show_regs_mask_fops = {
        .write = show_regs_mask_write,
        .llseek = noop_llseek,
};

static void init_proc_show_regs_mask(void)
{
        if (!proc_create("show_regs_mask", S_IWUSR | S_IWGRP | S_IWOTH, NULL, &show_regs_mask_fops)) {
                printk(KERN_ERR "proc_create show_regs_mask_fops fail\n");
        }
}
#endif
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen PSW.BSP.CHG  2018-05-25 Add for show voter */
static bool show_voter_mask;
static ssize_t show_voter_mask_write(struct file *file, const char __user *buff, size_t count, loff_t *ppos)
{
        char mask[16];

        if (copy_from_user(&mask, buff, count)) {
                chg_err("show_voter_mask_write error.\n");
                return -EFAULT;
        }

        if (strncmp(mask, "1", 1) == 0) {
                show_voter_mask = true;
                chg_debug("show voter mask enable.\n");
        } else if (strncmp(mask, "0", 1) == 0) {
                show_voter_mask = false;
                chg_debug("show voter mask disable.\n");
        } else {
                show_voter_mask = false;
                return -EFAULT;
        }

        return count;
}

static const struct file_operations show_voter_mask_fops = {
        .write = show_voter_mask_write,
        .llseek = noop_llseek,
};

static void init_proc_show_voter_mask(void)
{
        if (!proc_create("show_voter_mask", S_IWUSR | S_IWGRP | S_IWOTH, NULL, &show_voter_mask_fops)) {
                printk(KERN_ERR "proc_create show_voter_mask_fops fail\n");
        }
}
#endif
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018/04/25  OPPO_CHARGE*/
static int smbchg_usb_suspend_disable(void);
static int smbchg_usb_suspend_enable(void);
static int smbchg_charging_enble(void);
static bool oppo_chg_is_usb_present(void);
static int qpnp_get_prop_charger_voltage_now(void);

#define NUM_MAX_CLIENTS         16
struct client_vote {
        bool    enabled;
        int     value;
};

struct votable {
        const char              *name;
        struct list_head        list;
        struct client_vote      votes[NUM_MAX_CLIENTS];
        int                     num_clients;
        int                     type;
        int                     effective_client_id;
        int                     effective_result;
        struct mutex            vote_lock;
        void                    *data;
        int                     (*callback)(struct votable *votable,
                                                void *data,
                                                int effective_result,
                                                const char *effective_client);
        char                    *client_strs[NUM_MAX_CLIENTS];
        bool                    voted_on;
        struct dentry           *root;
        struct dentry           *status_ent;
        u32                     force_val;
        struct dentry           *force_val_ent;
        bool                    force_active;
        struct dentry           *force_active_ent;
};

static bool d_reg_mask = false;

static void dump_regs(void)
{
	int i;
	int j;
	int rc;
	u8 stat;
	int base[] = {0x1000, 0x1100, 0x1200, 0x1300, 0x1400, 0x1600, 0x1800, 0x1900};
	struct smb_charger *chg = NULL;

	if (!g_oppo_chip || !d_reg_mask)
		return;

	chg = &g_oppo_chip->pmic_spmi.smb5_chip->chg;
	if (!chg)
		return;

	pr_err("================= %s: begin ======================\n", __func__);

	for (j = 0; j < 8; j++) {
		for (i = 0; i < 255; i++) {
			rc = smblib_read(chg, base[j] + i, &stat);
			if (rc < 0) {
				pr_err("Couldn't read %x rc=%d\n", base[j] + i, rc);
			} else {
				pr_err("%x : %x\n", base[j] + i, stat);
			}
		}

		msleep(1000);
	}

	pr_err("================= %s: end ======================\n", __func__);

	d_reg_mask = false;
}

static int smbchg_kick_wdt(void)
{
        return 0;
}

static int oppo_chg_hw_init(void)
{
        int boot_mode = get_boot_mode();

        if (boot_mode != MSM_BOOT_MODE__RF && boot_mode != MSM_BOOT_MODE__WLAN) {
                smbchg_usb_suspend_disable();
        } else {
                smbchg_usb_suspend_enable();
        }
        smbchg_charging_enble();

        return 0;
}

static int smbchg_set_fastchg_current_raw(int current_ma)
{
        int rc = 0;

        rc = vote(g_smb_chip->fcc_votable, DEFAULT_VOTER,
                        true, current_ma * 1000);
        if (rc < 0) {
                chg_err("Couldn't vote fcc_votable[%d], rc=%d\n", current_ma, rc);
        } else {
                chg_debug("vote fcc_votable[%d], rc = %d\n", current_ma, rc);
        }

        return rc;
}

static void smbchg_set_aicl_point(int vol)
{
        return;
}

static void smbchg_aicl_enable(bool enable)
{
        int rc = 0;

        rc = smblib_masked_write(g_smb_chip, USBIN_AICL_OPTIONS_CFG_REG,
                        USBIN_AICL_EN_BIT, enable ? USBIN_AICL_EN_BIT : 0);
        if (rc < 0) {
                chg_err("Couldn't write USBIN_AICL_OPTIONS_CFG_REG rc=%d\n", rc);
        }
}

static void smbchg_rerun_aicl(void)
{
        smbchg_aicl_enable(false);

        /* Add a delay so that AICL successfully clears */
        msleep(50);

        smbchg_aicl_enable(true);
}

static bool oppo_chg_is_normal_mode(void)
{
        int boot_mode = get_boot_mode();

        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                return false;
        }

        return true;
}


static bool oppo_chg_is_suspend_status(void)
{
        int rc = 0;
        u8 stat;

        if (!g_oppo_chip) {
                return false;
        }

        rc = smblib_read(g_smb_chip, POWER_PATH_STATUS_REG, &stat);
        if (rc < 0) {
                chg_err("oppo_chg_is_suspend_status: Couldn't read POWER_PATH_STATUS rc=%d\n", rc);
                return false;
        }

        return (bool)(stat & USBIN_SUSPEND_STS_BIT);
}

static void oppo_chg_clear_suspend(void)
{
        int rc;

        if (!g_oppo_chip) {
                return;
        }       

        rc = smblib_masked_write(g_smb_chip, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 1);
        if (rc < 0) {
                chg_err("oppo_chg_monitor_work: Couldn't set USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

        msleep(50);

        rc = smblib_masked_write(g_smb_chip, USBIN_CMD_IL_REG, USBIN_SUSPEND_BIT, 0);
        if (rc < 0) {
                chg_err("oppo_chg_monitor_work: Couldn't clear USBIN_SUSPEND_BIT rc=%d\n", rc);
        }
}


static void oppo_chg_check_clear_suspend(void)
{
        use_present_status = true;
        oppo_chg_clear_suspend();
        use_present_status = false;
}


static int usb_icl[] = {
        300, 500, 900, 1200, 1350, 1500, 1750, 2000, 3000,
};

#define USBIN_25MA      25000
static int oppo_chg_set_input_current(int current_ma)
{
        int rc = 0, i = 0;
        int chg_vol = 0;
        int aicl_point = 0;
        struct oppo_chg_chip *chip = g_oppo_chip;

        chg_debug( "AICL setting_value = %d, pre_value = %d\n", current_ma, g_smb_chip->pre_current_ma);

        if (g_smb_chip->pre_current_ma == current_ma) {
                return rc;
        } else {
                g_smb_chip->pre_current_ma = current_ma;
        }
/*  rm adjust vfloat by zhangkun
        if (fv_adjust_enable == true && g_oppo_chip->limits.vfloat_sw_set > 4350 && g_oppo_chip->ui_soc >= 85) {
                chg_debug("adjust_fv setting 4350mV\n");
                vote(g_smb_chip->fv_votable, BATT_PROFILE_VOTER, true, 4350 * 1000);
                chip->limits.vfloat_sw_set = 4350;
        }
*/

        if (chip->batt_volt > 4100) {
                aicl_point = 4550;
        } else {
                aicl_point = 4500;
        }

        smbchg_aicl_enable(false);

        if (current_ma < 500) {
                i = 0;
                goto aicl_end;
        }

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
        }

        i = 1;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                chg_debug( "use 500 here\n");
                goto aicl_boost_back;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                chg_debug( "use 500 here\n");
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                chg_debug( "use 500 here\n");
                goto aicl_end;
        } else if (current_ma < 900) {
                goto aicl_end;
        }

        i = 2;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (current_ma < 1200) {
                goto aicl_end;
        }

        i = 3;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        }

        i = 4; /*1350*/
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(130);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 2;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 2;
		goto aicl_pre_step;
	} 
	
	i = 5; /* 1500 */
	rc = vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
	msleep(120);

	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
		i = i - 3;
		goto aicl_suspend;
	}

	chg_vol = qpnp_get_prop_charger_voltage_now();
	if (get_client_vote(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable, BOOST_BACK_VOTER) == 0
			&& get_effective_result(chip->pmic_spmi.smb5_chip->chg.usb_icl_votable) <= USBIN_25MA) {
		i = i - 3;
		goto aicl_boost_back;
	}
	if (chg_vol < aicl_point) {
		i = i - 3; //We DO NOT use 1.2A here
                goto aicl_pre_step;
        } else if (current_ma < 1500) {
                i = i - 2;//We use 1.2A here
                goto aicl_end;
        } else if (current_ma < 2000) {
                goto aicl_end;
        }

        i = 6;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(120);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 3;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 3;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 3;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i = i - 3;
                goto aicl_pre_step;
        }

        i = 7;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 2;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 2;
                goto aicl_boost_back;
        }
        if (chg_vol < aicl_point) {
                i =  i - 2;
                goto aicl_pre_step;
        } else if (current_ma < 3000) {
                goto aicl_end;
        }

        i = 8;
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        msleep(90);

        if (get_client_vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER) == 0
                        && get_effective_result(g_smb_chip->usb_icl_votable) < USBIN_25MA) {
                i = i - 1;
                goto aicl_boost_back;
        }
        if (oppo_chg_is_suspend_status() && oppo_chg_is_usb_present() && oppo_chg_is_normal_mode()) {
                i = i - 1;
                goto aicl_suspend;
        }

        chg_vol = qpnp_get_prop_charger_voltage_now();
        if (chg_vol < aicl_point) {
                i = i - 1;
                goto aicl_pre_step;
        } else if (current_ma >= 3000) {
                goto aicl_end;
        }

aicl_pre_step:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_pre_step\n", chg_vol, i, usb_icl[i], aicl_point);
        smbchg_rerun_aicl();
        return rc;
aicl_end:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_end rc = %d\n", chg_vol, i, usb_icl[i], aicl_point,rc);
        smbchg_rerun_aicl();
        return rc;
aicl_boost_back:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_boost_back\n", chg_vol, i, usb_icl[i], aicl_point);
        if (g_smb_chip->wa_flags & BOOST_BACK_WA) {
                vote(g_smb_chip->usb_icl_votable, BOOST_BACK_VOTER, false, 0);
        }
        smbchg_rerun_aicl();
        return rc;
aicl_suspend:
        rc = vote(g_smb_chip->usb_icl_votable, USB_PSY_VOTER, true, usb_icl[i] * 1000);
        chg_debug( "usb input max current limit aicl chg_vol=%d j[%d]=%d sw_aicl_point:%d aicl_suspend\n", chg_vol, i, usb_icl[i], aicl_point);
        oppo_chg_check_clear_suspend();
        smbchg_rerun_aicl();
        return rc;
}


static int smbchg_float_voltage_set(int vfloat_mv)
{
        int rc = 0;
        chg_debug("smbchg_float_voltage_set: vfloat_mv = %d\n", vfloat_mv);
        rc = vote(g_smb_chip->fv_votable, BATT_PROFILE_VOTER, true, vfloat_mv * 1000);
        if (rc < 0) {
                chg_err("Couldn't vote fv_votable[%d], rc=%d\n", vfloat_mv, rc);
        }

        return rc;
}

static int smbchg_term_current_set(int term_current)
{
        int rc = 0;
        u8 val_raw = 0;

        if (term_current < 0 || term_current > 750) {
                term_current = 150;
        }

        val_raw = term_current / 50;
        rc = smblib_masked_write(g_smb_chip, TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG,
                        TCCC_CHARGE_CURRENT_TERMINATION_SETTING_MASK, val_raw);
        if (rc < 0) {
                chg_err("Couldn't write TCCC_CHARGE_CURRENT_TERMINATION_CFG_REG rc=%d\n", rc);
        }

        return rc;
}

static int smbchg_charging_enble(void)
{
        int rc = 0;

        rc = vote(g_smb_chip->chg_disable_votable, DEFAULT_VOTER, false, 0);
        if (rc < 0) {
                chg_err("Couldn't enable charging, rc=%d\n", rc);
        }

        g_smb_chip->pre_current_ma = -1;

        fv_adjust_enable = true;
        fv_adjust_count = 0;

        return rc;
}

static int smbchg_charging_disble(void)
{
        int rc = 0;

        rc = vote(g_smb_chip->chg_disable_votable, DEFAULT_VOTER,
                        true, 0);
        if (rc < 0) {
                chg_err("Couldn't disable charging, rc=%d\n", rc);
        }

        fv_adjust_enable = false;
        fv_adjust_count = 0;
        
        return rc;
}

static int smbchg_get_charge_enable(void)
{
        int rc = 0;
        u8 temp = 0;

        rc = smblib_read(g_smb_chip, CHARGING_ENABLE_CMD_REG, &temp);
        if (rc < 0) {
                chg_err("Couldn't read CHARGING_ENABLE_CMD_REG rc=%d\n", rc);
                return 0;
        }
        rc = temp & CHARGING_ENABLE_CMD_BIT;

        return rc;
}

static int smbchg_usb_suspend_enable(void)
{
        int rc = 0;

        rc = smblib_set_usb_suspend(g_smb_chip, true);
        if (rc < 0) {
                chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

        g_smb_chip->pre_current_ma = -1;

        return rc;
}

static int smbchg_usb_suspend_disable(void)
{
        int rc = 0;
        int boot_mode = get_boot_mode();

        if (boot_mode == MSM_BOOT_MODE__RF || boot_mode == MSM_BOOT_MODE__WLAN) {
                chg_debug("RF/WLAN, suspending...\n");
                rc = smblib_set_usb_suspend(g_smb_chip, true);
                if (rc < 0) {
                        chg_err("Couldn't write enable to USBIN_SUSPEND_BIT rc=%d\n", rc);
                }
                return rc;
        }

        rc = smblib_set_usb_suspend(g_smb_chip, false);
        if (rc < 0) {
                chg_err("Couldn't write disable to USBIN_SUSPEND_BIT rc=%d\n", rc);
        }

        return rc;
}

static int smbchg_set_rechg_vol(int rechg_vol)
{
        return 0;
}

static int smbchg_reset_charger()
{
        return 0;
}

static int smbchg_read_full(void)
{
        int rc = 0;
        u8 stat = 0;

        if (!oppo_chg_is_usb_present()) {
                return 0;
        }

        rc = smblib_read(g_smb_chip, BATTERY_CHARGER_STATUS_1_REG, &stat);
        if (rc < 0) {
                chg_err("Couldn't read BATTERY_CHARGER_STATUS_1 rc=%d\n", rc);
                return 0;
        }
        stat = stat & BATTERY_CHARGER_STATUS_MASK;

        if (stat == TERMINATE_CHARGE || stat == INHIBIT_CHARGE) {
                return 1;
        }

        return 0;
}

static int smbchg_otg_enable(void)
{
        return 0;
}

static int smbchg_otg_disable(void)
{
        return 0;
}

static int oppo_set_chging_term_disable()
{
        return 0;
}

static bool qcom_check_charger_resume()
{
        return true;
}

int smbchg_get_chargerid_volt(void)
{
	int rc, chargerid_volt = 0;
	struct oppo_chg_chip *chip = g_oppo_chip;
	struct smb_charger *chg = NULL;

    if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return 0;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (IS_ERR_OR_NULL(chg->iio.chgid_v_chan)) {
		printk(KERN_ERR "[OPPO_CHG][%s]: chg->iio.chgid_v_chan  is  NULL !\n", __func__);
		return 0;
	}
       
	rc = iio_read_channel_processed(chg->iio.chgid_v_chan, &chargerid_volt);
	if (rc < 0) {
		chg_err("[OPPO_CHG][%s]: iio_read_channel_processed  get error\n", __func__);
		return 0;
	}

	chargerid_volt = chargerid_volt / 1000;
	chg_err("chargerid_volt: %d\n", chargerid_volt);
    
	return chargerid_volt;
}


static int smbchg_chargerid_switch_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
                chg_err("get normalchg_gpio.pinctrl fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_active = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_active");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)) {
                chg_err("get chargerid_switch_active fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_sleep = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_sleep");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
                chg_err("get chargerid_switch_sleep fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.chargerid_switch_default = 
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "chargerid_switch_default");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
                chg_err("get chargerid_switch_default fail\n");
                return -EINVAL;
        }

        if (chip->normalchg_gpio.chargerid_switch_gpio > 0) {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
        }
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.chargerid_switch_default);

        return 0;
}

static void smbchg_set_chargerid_switch_val(int value)
{
        struct oppo_chg_chip *chip = g_oppo_chip;
        if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                chg_err("chargerid_switch_gpio not exist, return\n");
                return;
        }

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_default)) {
                chg_err("pinctrl null, return\n");
                return;
        }

        if (oppo_vooc_get_adapter_update_real_status() == ADAPTER_FW_NEED_UPDATE
                || oppo_vooc_get_btb_temp_over() == true) {
                chg_debug("adapter update or btb_temp_over, return\n");
                return;
        }

        if (value) {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 1);
                pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                                chip->normalchg_gpio.chargerid_switch_default);
        } else {
                gpio_direction_output(chip->normalchg_gpio.chargerid_switch_gpio, 0);
                pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                                chip->normalchg_gpio.chargerid_switch_default);
        }
        chg_debug("set value:%d, gpio_val:%d\n", 
                value, gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio));
}

static int smbchg_get_chargerid_switch_val(void)
{
        struct oppo_chg_chip *chip = g_oppo_chip;
        
        if (chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
                chg_err("chargerid_switch_gpio not exist, return\n");
                return -1;
        }

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_active)
                || IS_ERR_OR_NULL(chip->normalchg_gpio.chargerid_switch_sleep)) {
                chg_err("pinctrl null, return\n");
                return -1;
        }

        return gpio_get_value(chip->normalchg_gpio.chargerid_switch_gpio);
}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
static int oppo_usbid_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
                chg_err("get normalchg_gpio.pinctrl fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.usbid_active =
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
                                "usbid_active");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.usbid_active)) {
                chg_err("get usbid_active fail\n");
                return -EINVAL;
        }

        chip->normalchg_gpio.usbid_sleep =
                        pinctrl_lookup_state(chip->normalchg_gpio.pinctrl,
                                "usbid_sleep");
        if (IS_ERR_OR_NULL(chip->normalchg_gpio.usbid_sleep)) {
                chg_err("get usbid_sleep fail\n");
                return -EINVAL;
        }

        if (chip->normalchg_gpio.usbid_gpio > 0) {
                gpio_direction_output(chip->normalchg_gpio.usbid_gpio, 0);
        }

        pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                chip->normalchg_gpio.usbid_sleep);

        return 0;
}

static void oppo_set_usbid_active(struct oppo_chg_chip *chip)
{
        gpio_direction_input(chip->normalchg_gpio.usbid_gpio);
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.usbid_active);
}

static void oppo_set_usbid_sleep(struct oppo_chg_chip *chip)
{
        gpio_direction_input(chip->normalchg_gpio.usbid_gpio);
        pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.usbid_sleep);
}

static void oppo_usbid_irq_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.usbid_irq = gpio_to_irq(chip->normalchg_gpio.usbid_gpio);
}

static void oppo_usbid_irq_register(struct oppo_chg_chip *chip)
{
        int retval = 0;
        union power_supply_propval ret = {0,};
        oppo_set_usbid_active(chip);

        retval = devm_request_threaded_irq(chip->dev, chip->normalchg_gpio.usbid_irq, NULL,
                        usbid_change_handler, IRQF_TRIGGER_FALLING | IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                        "usbid-change", chip);
        if (retval < 0) {
                chg_err("Unable to request usbid-change irq: %d\n", retval);
        }

        power_supply_get_property(chip->usb_psy, POWER_SUPPLY_PROP_OTG_SWITCH, &ret);
        if (ret.intval == false) {
                oppo_set_usbid_sleep(chip);
        }
}

#endif
static bool oppo_shortc_check_is_gpio(struct oppo_chg_chip *chip)
{
        if (gpio_is_valid(chip->normalchg_gpio.shortc_gpio)) {
                return true;
        }

        return false;
}

static int oppo_shortc_gpio_init(struct oppo_chg_chip *chip)
{
        chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);
        chip->normalchg_gpio.shortc_active = 
                pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, 
                        "shortc_active");

        if (IS_ERR_OR_NULL(chip->normalchg_gpio.shortc_active)) {
                chg_err("get shortc_active fail\n");
                return -EINVAL;
        }       

        pinctrl_select_state(chip->normalchg_gpio.pinctrl,
                chip->normalchg_gpio.shortc_active);
        return 0;
}
#ifdef CONFIG_OPPO_SHORT_HW_CHECK       
static bool oppo_chg_get_shortc_hw_gpio_status(void)
{
        bool shortc_hw_status = 1;
        struct oppo_chg_chip *chip = g_oppo_chip;

        if(oppo_shortc_check_is_gpio(chip) == true) {
                shortc_hw_status = !!(gpio_get_value(chip->normalchg_gpio.shortc_gpio));
        }
        return shortc_hw_status;
}
#else
static bool oppo_chg_get_shortc_hw_gpio_status(void)
{
        bool shortc_hw_status = 1;

        return shortc_hw_status;
}
#endif

static bool smbchg_need_to_check_ibatt()
{
        return true;
}

static int smbchg_get_chg_current_step(void)
{
        return 25;
}

extern void smblib_notify_device_mode(struct smb_charger *chg, bool enable);
static int opchg_get_charger_type(void)
{
        u8 apsd_stat;
        int rc;

        if (!g_oppo_chip) {
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }

        /* reset for fastchg to normal */
        if (g_oppo_chip->charger_type == POWER_SUPPLY_TYPE_UNKNOWN) {
                g_smb_chip->pre_current_ma = -1;
        }

        rc = smblib_read(g_smb_chip, APSD_STATUS_REG, &apsd_stat);
        if (rc < 0) {
                chg_err("Couldn't read APSD_STATUS rc=%d\n", rc);
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }
        chg_debug("APSD_STATUS = 0x%02x, Chg_Type = %d\n", apsd_stat, g_smb_chip->real_charger_type);

        if (!(apsd_stat & APSD_DTC_STATUS_DONE_BIT)) {
                return POWER_SUPPLY_TYPE_UNKNOWN;
        }

        if (g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB
                        || g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP
                        || g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_DCP) {
                oppo_chg_soc_update();
        }
        /* wenbin.liu add for avoid sometime charger exist but type not get */
        if (POWER_SUPPLY_TYPE_UNKNOWN == g_smb_chip->real_charger_type) {
                smblib_update_usb_type(g_smb_chip);
                chg_debug("Type Recovey Call, Type = %d\n", g_smb_chip->real_charger_type);
        }

	if (g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP
		|| g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB) {
		if (g_smb_chip->use_extcon) {
			chg_debug("SDP or CDP, force notify to dwc3\n");
			smblib_notify_device_mode(g_smb_chip, true);
		}
	}

        if (g_smb_chip->real_charger_type == POWER_SUPPLY_TYPE_USB_CDP) {
                return POWER_SUPPLY_TYPE_USB;
        }
        
        return g_smb_chip->real_charger_type;
}


#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Kun.Zhang  PSW.BSP.CHG  2019-04-09  add for charger */
int qpnp_get_prop_charger_voltage_now(void)
{
	int val = 0, rc = 0;
	union power_supply_propval pval = {0, };
	struct smb_charger *chg = NULL;
	struct oppo_chg_chip *chip = g_oppo_chip;
	
	if (!chip)
		return 0;

	//if (!oppo_chg_is_usb_present())
	//	return 0;
	chg = &chip->pmic_spmi.smb5_chip->chg;
	rc = smblib_get_prop_usb_present(chg, &pval);
	if (rc < 0) {
		chg_err("Couldn't get usb presence status rc=%d\n", rc);
		return -ENODATA;
	}

	/* usb not present */
	if (!pval.intval) {
		val = 0;
		return 0;
	}
	if (chg->smb_version != PM8150B_SUBTYPE) {
		if (!chg->iio.usbin_v_chan || PTR_ERR(chg->iio.usbin_v_chan) == -EPROBE_DEFER)
			chg->iio.usbin_v_chan = iio_channel_get(chg->dev, "usbin_v");

		if (IS_ERR(chg->iio.usbin_v_chan))
			return PTR_ERR(chg->iio.usbin_v_chan);

		iio_read_channel_processed(chg->iio.usbin_v_chan, &val);
	} else {
		if (!chg->iio.mid_chan || PTR_ERR(chg->iio.mid_chan) == -EPROBE_DEFER)
			chg->iio.mid_chan = iio_channel_get(chg->dev, "mid_voltage");

		if (IS_ERR(chg->iio.mid_chan))
			return PTR_ERR(chg->iio.mid_chan);

		iio_read_channel_processed(chg->iio.mid_chan, &val);

		if ((val / 1000) < chip->batt_volt) {
			if (oppo_vooc_get_fastchg_started() == true) {
				val = (chip->batt_volt + 300) * 1000 ;
			}
		}
	}

	if (val < 2000 * 1000)
		chg->pre_current_ma = -1;

	return val / 1000;
}
#endif

static bool oppo_chg_is_usb_present(void)
{
        int rc = 0;
        u8 stat = 0;
        bool vbus_rising = false;

        if (!g_oppo_chip || !g_smb_chip) {
                chg_err("Chip not ready\n");
                return false;
        }

        rc = smblib_read(g_smb_chip, USBIN_BASE + INT_RT_STS_OFFSET, &stat);
        if (rc < 0) {
                chg_err("Couldn't read USB_INT_RT_STS, rc=%d\n", rc);
                return false;
        }
        vbus_rising = (bool)(stat & USBIN_PLUGIN_RT_STS_BIT);

        if (vbus_rising == false) {
                g_smb_chip->pre_current_ma = -1;
        }

        return vbus_rising;
}

static int qpnp_get_battery_voltage(void)
{
        return 3800;
}

static int smbchg_get_boot_reason(void)
{
        return 0;
}

static int oppo_chg_get_shutdown_soc(void)
{
        int rc, shutdown_soc;
        union power_supply_propval ret = {0, };
        
        if (!g_oppo_chip || !g_smb_chip) {
                chg_err("chip not ready\n");
                return 0;
        }

        rc = g_smb_chip->bms_psy->desc->get_property(g_smb_chip->bms_psy, POWER_SUPPLY_PROP_RESTORE_SOC, &ret);
        if (rc) {
                chg_err("bms psy doesn't support soc restore rc = %d\n", rc);
                goto restore_soc_err;
        }

        shutdown_soc = ret.intval;
        if (shutdown_soc >= 0 && shutdown_soc <= 100) {
                chg_debug("get restored soc = %d\n", shutdown_soc);
                return shutdown_soc;
        } else {
                chg_err("get restored soc = %d\n", shutdown_soc);
                goto restore_soc_err;
        }

restore_soc_err:
        rc = g_smb_chip->bms_psy->desc->get_property(g_smb_chip->bms_psy, POWER_SUPPLY_PROP_CAPACITY, &ret);
        if (rc) {
                chg_err("get soc error, return default 50, rc = %d\n",rc);
                return 50;
        }
        chg_debug("use QG soc = %d\n", ret.intval);

        return ret.intval;
}

static int oppo_chg_backup_soc(int backup_soc)
{
        return 0;
}

static int smbchg_get_aicl_level_ma(void)
{
        return 0;
}

static int smbchg_force_tlim_en(bool enable)
{
        return 0;
}

static int smbchg_system_temp_level_set(int lvl_sel)
{
        return 0;
}

static int smbchg_set_prop_flash_active(enum skip_reason reason, bool disable)
{
        return 0;
}

static int smbchg_dp_dm(int val)
{
        return 0;
}

static int smbchg_calc_max_flash_current()
{
        return 0;
}

static int oppo_chg_get_fv(struct oppo_chg_chip *chip)
{
	int flv = chip->limits.temp_normal_vfloat_mv;
	int batt_temp = chip->temperature;

	if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
		//default
	} else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
		flv = chip->limits.temp_warm_vfloat_mv;
	} else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
		flv = chip->limits.temp_normal_vfloat_mv;
	} else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
		flv = chip->limits.temp_little_cool_vfloat_mv;
	} else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
		flv = chip->limits.temp_cool_vfloat_mv;
	} else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
		flv = chip->limits.temp_little_cold_vfloat_mv;
	} else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
		flv = chip->limits.temp_cold_vfloat_mv;
	} else {
		//default
	}

	return flv;
}

static int oppo_chg_get_charging_current(struct oppo_chg_chip *chip)
{
        int charging_current = 0;
        int batt_temp = chip->temperature;

        if (batt_temp > chip->limits.hot_bat_decidegc) {//53C
                charging_current = 0;
        } else if (batt_temp >= chip->limits.warm_bat_decidegc) {//45C
                charging_current = chip->limits.temp_warm_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.normal_bat_decidegc) {//16C
                charging_current = chip->limits.temp_normal_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.little_cool_bat_decidegc) {//12C
                charging_current = chip->limits.temp_little_cool_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.cool_bat_decidegc) {//5C
                if (chip->batt_volt > 4180) {
                        charging_current = chip->limits.temp_cool_fastchg_current_ma_low;
                }
                else {
                        charging_current = chip->limits.temp_cool_fastchg_current_ma_high;
                }
        } else if (batt_temp >= chip->limits.little_cold_bat_decidegc) {//0C
                charging_current = chip->limits.temp_little_cold_fastchg_current_ma;
        } else if (batt_temp >= chip->limits.cold_bat_decidegc) {//-3C
                charging_current = chip->limits.temp_cold_fastchg_current_ma;
        } else {
                charging_current = 0;
        }

        return charging_current;
}

#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
/* This function is getting the dynamic aicl result/input limited in mA.
 * If charger was suspended, it must return 0(mA).
 * It meets the requirements in SDM660 platform.
 */
static int oppo_chg_get_dyna_aicl_result(void)
{
        struct power_supply *usb_psy = NULL;
        union power_supply_propval pval = {0, };

        usb_psy = power_supply_get_by_name("usb");
        if (usb_psy) {
                power_supply_get_property(usb_psy,
                                POWER_SUPPLY_PROP_INPUT_CURRENT_SETTLED,
                                &pval);
                return pval.intval / 1000;
        }

        return 1000;
}
#endif


static int get_current_time(unsigned long *now_tm_sec)
{
        struct rtc_time tm;
        struct rtc_device *rtc;
        int rc;

        rtc = rtc_class_open(CONFIG_RTC_HCTOSYS_DEVICE);
        if (rtc == NULL) {
                pr_err("%s: unable to open rtc device (%s)\n",
                        __FILE__, CONFIG_RTC_HCTOSYS_DEVICE);
                return -EINVAL;
        }

        rc = rtc_read_time(rtc, &tm);
        if (rc) {
                pr_err("Error reading rtc device (%s) : %d\n",
                        CONFIG_RTC_HCTOSYS_DEVICE, rc);
                goto close_time;
        }

        rc = rtc_valid_tm(&tm);
        if (rc) {
                pr_err("Invalid RTC time (%s): %d\n",
                        CONFIG_RTC_HCTOSYS_DEVICE, rc);
                goto close_time;
        }
        rtc_tm_to_time(&tm, now_tm_sec);

close_time:
        rtc_class_close(rtc);
        return rc;
}

static unsigned long suspend_tm_sec = 0;
static int smb5_pm_resume(struct device *dev)
{
        int rc = 0;
        signed long resume_tm_sec = 0;
        signed long sleep_time = 0;

        if (!g_oppo_chip || !g_smb_chip) {
                return 0;
                chg_err("Chip not ready\n");
        }

        rc = get_current_time(&resume_tm_sec);
        if (rc || suspend_tm_sec == -1) {
                chg_err("RTC read failed\n");
                sleep_time = 0;
        } else {
                sleep_time = resume_tm_sec - suspend_tm_sec;
        }

        if (sleep_time < 0) {
                sleep_time = 0;
        }

        oppo_chg_soc_update_when_resume(sleep_time);

        return 0;
}

static int smb5_pm_suspend(struct device *dev)
{
        if (!g_oppo_chip || !g_smb_chip) {
                return 0;
                chg_err("Chip not ready\n");
        }

        if (get_current_time(&suspend_tm_sec)) {
                chg_err("RTC read failed\n");
                suspend_tm_sec = -1;
        }

        return 0;
}

static const struct dev_pm_ops smb5_pm_ops = {
        .resume         = smb5_pm_resume,
        .suspend        = smb5_pm_suspend,
};

struct oppo_chg_operations  smb5_chg_ops = {
	.dump_registers = dump_regs,
	.kick_wdt = smbchg_kick_wdt,
	.hardware_init = oppo_chg_hw_init,
	.charging_current_write_fast = smbchg_set_fastchg_current_raw,
	.set_aicl_point = smbchg_set_aicl_point,
	.input_current_write = oppo_chg_set_input_current,
	.float_voltage_write = smbchg_float_voltage_set,
	.term_current_set = smbchg_term_current_set,
	.charging_enable = smbchg_charging_enble,
	.charging_disable = smbchg_charging_disble,
	.get_charging_enable = smbchg_get_charge_enable,
	.charger_suspend = smbchg_usb_suspend_enable,
	.charger_unsuspend = smbchg_usb_suspend_disable,
	.set_rechg_vol = smbchg_set_rechg_vol,
	.reset_charger = smbchg_reset_charger,
	.read_full = smbchg_read_full,
	.otg_enable = smbchg_otg_enable,
	.otg_disable = smbchg_otg_disable,
	.set_charging_term_disable = oppo_set_chging_term_disable,
	.check_charger_resume = qcom_check_charger_resume,
	.get_chargerid_volt = smbchg_get_chargerid_volt,
	.set_chargerid_switch_val = smbchg_set_chargerid_switch_val,
	.get_chargerid_switch_val = smbchg_get_chargerid_switch_val,
	.need_to_check_ibatt = smbchg_need_to_check_ibatt,
	.get_chg_current_step = smbchg_get_chg_current_step,
#ifdef CONFIG_OPPO_CHARGER_MTK
	.get_charger_type = mt_power_supply_type_check,
	.get_charger_volt = battery_meter_get_charger_voltage,
	.check_chrdet_status = pmic_chrdet_status,
	.get_instant_vbatt = battery_meter_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = get_boot_reason,
#ifdef CONFIG_MTK_HAFG_20
	.get_rtc_soc = get_rtc_spare_oppo_fg_value,
	.set_rtc_soc = set_rtc_spare_oppo_fg_value,
#else
	.get_rtc_soc = get_rtc_spare_fg_value,
	.set_rtc_soc = set_rtc_spare_fg_value,
#endif	/* CONFIG_MTK_HAFG_20 */
	.set_power_off = mt_power_off,
	.usb_connect = mt_usb_connect,
	.usb_disconnect = mt_usb_disconnect,
#else
	.get_charger_type = opchg_get_charger_type,
	.get_charger_volt = qpnp_get_prop_charger_voltage_now,
	.check_chrdet_status = oppo_chg_is_usb_present,
	.get_instant_vbatt = qpnp_get_battery_voltage,
	.get_boot_mode = get_boot_mode,
	.get_boot_reason = smbchg_get_boot_reason,
	.get_rtc_soc = oppo_chg_get_shutdown_soc,
	.set_rtc_soc = oppo_chg_backup_soc,
	.get_aicl_ma = smbchg_get_aicl_level_ma,
	.rerun_aicl = smbchg_rerun_aicl,
	.tlim_en = smbchg_force_tlim_en,
	.set_system_temp_level = smbchg_system_temp_level_set,
	.otg_pulse_skip_disable = smbchg_set_prop_flash_active,
	.set_dp_dm = smbchg_dp_dm,
	.calc_flash_current = smbchg_calc_max_flash_current,
#endif	/* CONFIG_OPPO_CHARGER_MTK */
#ifdef CONFIG_OPPO_RTC_DET_SUPPORT
	.check_rtc_reset = rtc_reset_check,
#endif
#ifdef CONFIG_OPPO_SHORT_C_BATT_CHECK
	.get_dyna_aicl_result = oppo_chg_get_dyna_aicl_result,
#endif
	.get_shortc_hw_gpio_status = oppo_chg_get_shortc_hw_gpio_status,
};

static bool oppo_ship_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.ship_gpio))
		return true;

	return false;
}

static int oppo_ship_gpio_init(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb2_chg not ready!\n", __func__);
		return -EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	chip->normalchg_gpio.ship_active =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_active");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_active)) {
		chg_err("get ship_active fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.ship_sleep =
			pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "ship_sleep");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.ship_sleep)) {
		chg_err("get ship_sleep fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.ship_sleep);

	return 0;
}

static bool oppo_usbtemp_check_is_gpio(struct oppo_chg_chip *chip)
{
	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return false;
	}

	if (gpio_is_valid(chip->normalchg_gpio.dischg_gpio))
		return true;

	return false;
}


bool oppo_usbtemp_check_is_support(void)
{
	if(oppo_usbtemp_check_is_gpio(g_oppo_chip) == true)
		return true;
	
	chg_err("dischg return false\n");

	return false;
}

static int oppo_dischg_gpio_init(struct oppo_chg_chip *chip)
{
	if (!chip) {
		chg_err("chip NULL\n");
		return EINVAL;
	}

	chip->normalchg_gpio.pinctrl = devm_pinctrl_get(chip->dev);

	if (IS_ERR_OR_NULL(chip->normalchg_gpio.pinctrl)) {
		chg_err("get dischg_pinctrl fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_enable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_enable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_enable)) {
		chg_err("get dischg_enable fail\n");
		return -EINVAL;
	}

	chip->normalchg_gpio.dischg_disable = pinctrl_lookup_state(chip->normalchg_gpio.pinctrl, "dischg_disable");
	if (IS_ERR_OR_NULL(chip->normalchg_gpio.dischg_disable)) {
		chg_err("get dischg_disable fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chip->normalchg_gpio.pinctrl, chip->normalchg_gpio.dischg_disable);

	return 0;
}

static bool oppo_shipmode_id_check_is_gpio(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	if (gpio_is_valid(chg->shipmode_id_gpio)) {
		printk(KERN_ERR "[OPPO_CHG][%s]:  shipmode_id_gpio true!\n", __func__);
		return true;
	}

	return false;
}

static int oppo_shipmode_id_gpio_init(struct oppo_chg_chip *chip)
{
	struct smb_charger *chg = NULL;

	if (!chip) {
		printk(KERN_ERR "[OPPO_CHG][%s]: smb5_chg not ready!\n", __func__);
		return -EINVAL;
	}
	chg = &chip->pmic_spmi.smb5_chip->chg;

	chg->shipmode_id_pinctrl = devm_pinctrl_get(chip->dev);

	chg->shipmode_id_active =
		pinctrl_lookup_state(chg->shipmode_id_pinctrl, "shipmode_id_active");
	if (IS_ERR_OR_NULL(chg->shipmode_id_active)) {
		chg_err("get shipmode_id_active fail\n");
		return -EINVAL;
	}

	pinctrl_select_state(chg->shipmode_id_pinctrl, chg->shipmode_id_active);

	return 0;
}



static int oppo_chg_parse_custom_dt(struct oppo_chg_chip *chip)
{
	int rc = 0;
	struct device_node *node = chip->dev->of_node;
	struct smb_charger *chg = &chip->pmic_spmi.smb5_chip->chg;
	if (!node) {
			pr_err("device tree node missing\n");
			return -EINVAL;
	}
	
#ifdef CONFIG_PRODUCT_REALME_TRINKET
	/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.chargerid_switch_gpio =
				of_get_named_gpio(node, "qcom,chargerid_switch-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.chargerid_switch_gpio <= 0) {
			chg_err("Couldn't read chargerid_switch-gpio rc = %d, chargerid_switch_gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
		} else {
			if (gpio_is_valid(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio)) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.chargerid_switch_gpio, "charging-switch1-gpio");
				if (rc) {
					chg_err("unable to request chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
				} else {
					smbchg_chargerid_switch_gpio_init(g_oppo_chip);
				}
			}
			chg_err("chargerid_switch_gpio:%d\n", g_oppo_chip->normalchg_gpio.chargerid_switch_gpio);
		}
	}
#endif /*CONFIG_PRODUCT_REALME_TRINKET*/
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* tongfeng.Huang@BSP.CHG.Basic, 2018/05/08,  Add for using gpio as USB vbus short */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.dischg_gpio = of_get_named_gpio(node, "qcom,dischg-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.dischg_gpio <= 0) {
			chg_err("Couldn't read qcom,dischg-gpio rc=%d, qcom,dischg-gpio:%d\n",
				rc, g_oppo_chip->normalchg_gpio.dischg_gpio);
		} else {
			if (oppo_usbtemp_check_is_support() == true) {
				if (gpio_is_valid(g_oppo_chip->normalchg_gpio.dischg_gpio)) {
					rc = gpio_request(g_oppo_chip->normalchg_gpio.dischg_gpio, "dischg-gpio");
					if (rc) {
						chg_err("unable to request dischg-gpio:%d\n", g_oppo_chip->normalchg_gpio.dischg_gpio);
					} else {
						oppo_dischg_gpio_init(g_oppo_chip);
					}
				}
			}
			chg_err("dischg-gpio:%d\n", g_oppo_chip->normalchg_gpio.dischg_gpio);
		}
	}
#endif /*CONFIG_PRODUCT_REALME_TRINKET*/
	
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for using gpio as shipmode stm6620 */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.ship_gpio =
				of_get_named_gpio(node, "qcom,ship-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.ship_gpio <= 0) {
			chg_err("Couldn't read qcom,ship-gpio rc = %d, qcom,ship-gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.ship_gpio);
		} else {
			if (oppo_ship_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.ship_gpio, "ship-gpio");
				if (rc) {
					chg_err("unable to request ship-gpio:%d\n",
							g_oppo_chip->normalchg_gpio.ship_gpio);
				} else {
					oppo_ship_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
		}
	}
#endif /*CONFIG_PRODUCT_REALME_TRINKET*/
	
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for HW shortc */
	if (g_oppo_chip) {
		g_oppo_chip->normalchg_gpio.shortc_gpio =
				of_get_named_gpio(node, "qcom,shortc-gpio", 0);
		if (g_oppo_chip->normalchg_gpio.shortc_gpio <= 0) {
			chg_err("Couldn't read qcom,shortc-gpio rc = %d, qcom,shortc-gpio:%d\n",
					rc, g_oppo_chip->normalchg_gpio.shortc_gpio);
		} else {
			if (oppo_shortc_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(g_oppo_chip->normalchg_gpio.shortc_gpio, "shortc-gpio");
				if (rc) {
					chg_err("unable to request shortc-gpio:%d\n",
							g_oppo_chip->normalchg_gpio.shortc_gpio);
				} else {
					oppo_shortc_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init ship-gpio:%d\n", g_oppo_chip->normalchg_gpio.ship_gpio);
				}
			}
			chg_err("shortc-gpio:%d\n", g_oppo_chip->normalchg_gpio.shortc_gpio);
		}
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2018/03/02, sjc Add for HW shortc */
	if (g_oppo_chip) {
		chg->shipmode_id_gpio =
				of_get_named_gpio(node, "qcom,shipmode-id-gpio", 0);
		if (chg->shipmode_id_gpio <= 0) {
			chg_err("Couldn't read qcom,shipmode-id-gpio rc = %d, qcom,shipmode-id-gpio:%d\n",
					rc, chg->shipmode_id_gpio);
		} else {
			if (oppo_shipmode_id_check_is_gpio(g_oppo_chip) == true) {
				rc = gpio_request(chg->shipmode_id_gpio, "qcom,shipmode-id-gpio");
				if (rc) {
					chg_err("unable to request qcom,shipmode-id-gpio:%d\n",
							chg->shipmode_id_gpio);
				} else {
					oppo_shipmode_id_gpio_init(g_oppo_chip);
					if (rc)
						chg_err("unable to init qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
				}
			}
			chg_err("qcom,shipmode-id-gpio:%d\n", chg->shipmode_id_gpio);
		}
	}
#endif /* CONFIG_PRODUCT_REALME_TRINKET */
	return rc;
}


#endif
static int smb5_probe(struct platform_device *pdev)
{
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        struct oppo_chg_chip *oppo_chip;
        struct power_supply *main_psy = NULL;
        union power_supply_propval pval = {0, };
#endif
	struct smb5 *chip;
	struct smb_charger *chg;
	int rc = 0;
    pr_info("smb5_probe start\n");

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        if (oppo_gauge_check_chip_is_null()) {
                chg_err("gauge chip null, will do after bettery init.\n");
                if (!oppo_chip->platform_fg_flag)
                    return -EPROBE_DEFER;
        }
#endif
	chip = devm_kzalloc(&pdev->dev, sizeof(*chip), GFP_KERNEL);
	if (!chip)
		return -ENOMEM;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        oppo_chip = devm_kzalloc(&pdev->dev, sizeof(*oppo_chip), GFP_KERNEL);
        if (!oppo_chip) {
                return -ENOMEM;
        }

        oppo_chip->pmic_spmi.smb5_chip = chip;
        oppo_chip->chg_ops = &smb5_chg_ops;
        oppo_chip->dev = &pdev->dev;
        g_oppo_chip = oppo_chip;
        g_smb_chip = &oppo_chip->pmic_spmi.smb5_chip->chg;
		if (!g_smb_chip) {
				chg_err("smb5_probe chip not ready\n");
		}
        if (!g_oppo_chip) {
				chg_err("smb5_probe chip not ready1\n");
		}
#endif

	chg = &chip->chg;
	chg->dev = &pdev->dev;
	chg->debug_mask = &__debug_mask;
	chg->pd_disabled = &__pd_disabled;
	chg->weak_chg_icl_ua = &__weak_chg_icl_ua;
	chg->mode = PARALLEL_MASTER;
	chg->irq_info = smb5_irqs;
	chg->die_health = -EINVAL;
	chg->connector_health = -EINVAL;
	chg->otg_present = false;
	chg->main_fcc_max = -EINVAL;
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Jianchao.Shi@BSP.CHG.Basic, 2017/08/10, sjc Add for charging */
            chg->pre_current_ma = -1;
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (of_find_property(oppo_chip->dev->of_node, "qcom,pmi632chg-vadc", NULL)) {
                rc = smblib_get_iio_channel(chg, "usb_in_voltage",
				&chg->iio.chgid_v_chan);
		if (rc < 0){
                chg_err("Couldn't get pmi632 vadc rc=%d\n", rc);
                return rc;
            }
		if (!chg->iio.chgid_v_chan) {
			dev_err(chg->dev, "No chgid_v_chan channel defined");
			return -EINVAL;
		}
        }

        /*if (of_find_property(oppo_chip->dev->of_node, "qcom,pm8953chg-vadc", NULL)) {
                oppo_chip->pmic_spmi.pm8953_vadc_dev = qpnp_get_vadc(oppo_chip->dev, "pm8953chg");
                if (IS_ERR(oppo_chip->pmic_spmi.pm8953_vadc_dev)) {
                        rc = PTR_ERR(oppo_chip->pmic_spmi.pm8953_vadc_dev);
                        oppo_chip->pmic_spmi.pm8953_vadc_dev = NULL;
                        if (rc != -EPROBE_DEFER) {
                                chg_err("Couldn't get pm8953 vadc rc=%d\n", rc);
                        } else {
                                chg_err("Couldn't get pm8953 vadc, try again...\n");
                                return -EPROBE_DEFER;
                        }
                }
        }*/
#endif
	chg->regmap = dev_get_regmap(chg->dev->parent, NULL);
	if (!chg->regmap) {
		pr_err("parent regmap is missing\n");
		return -EINVAL;
	}

	rc = smb5_chg_config_init(chip);
	if (rc < 0) {
		if (rc != -EPROBE_DEFER)
			pr_err("Couldn't setup chg_config rc=%d\n", rc);
		return rc;
	}

	rc = smb5_parse_dt(chip);
	if (rc < 0) {
		pr_err("Couldn't parse device tree rc=%d\n", rc);
		return rc;
	}

	if (alarmtimer_get_rtcdev())
		alarm_init(&chg->lpd_recheck_timer, ALARM_REALTIME,
				smblib_lpd_recheck_timer);
	else
		return -EPROBE_DEFER;

	rc = smblib_init(chg);
	if (rc < 0) {
		pr_err("Smblib_init failed rc=%d\n", rc);
		return rc;
	}

	/* set driver data before resources request it */
	platform_set_drvdata(pdev, chip);

	/* extcon registration */
	chg->extcon = devm_extcon_dev_allocate(chg->dev, smblib_extcon_cable);
	if (IS_ERR(chg->extcon)) {
		rc = PTR_ERR(chg->extcon);
		dev_err(chg->dev, "failed to allocate extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	rc = devm_extcon_dev_register(chg->dev, chg->extcon);
	if (rc < 0) {
		dev_err(chg->dev, "failed to register extcon device rc=%d\n",
				rc);
		goto cleanup;
	}

	/* Support reporting polarity and speed via properties */
	rc = extcon_set_property_capability(chg->extcon,
			EXTCON_USB, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB, EXTCON_PROP_USB_SS);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_TYPEC_POLARITY);
	rc |= extcon_set_property_capability(chg->extcon,
			EXTCON_USB_HOST, EXTCON_PROP_USB_SS);
	if (rc < 0) {
		dev_err(chg->dev,
			"failed to configure extcon capabilities\n");
		goto cleanup;
	}

	rc = smb5_init_hw(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize hardware rc=%d\n", rc);
		goto cleanup;
	}

	/*
	 * VBUS regulator enablement/disablement for host mode is handled
	 * by USB-PD driver only. For micro-USB and non-PD typeC designs,
	 * the VBUS regulator is enabled/disabled by the smb driver itself
	 * before sending extcon notifications.
	 * Hence, register vbus and vconn regulators for PD supported designs
	 * only.
	 */
	if (!chg->pd_not_supported) {
		rc = smb5_init_vbus_regulator(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize vbus regulator rc=%d\n",
				rc);
			goto cleanup;
		}

		rc = smb5_init_vconn_regulator(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize vconn regulator rc=%d\n",
				rc);
			goto cleanup;
		}
	}

	switch (chg->chg_param.smb_version) {
	case PM8150B_SUBTYPE:
	case PM6150_SUBTYPE:
#ifndef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Delete for charging*/
		rc = smb5_init_dc_psy(chip);
		if (rc < 0) {
			pr_err("Couldn't initialize dc psy rc=%d\n", rc);
			goto cleanup;
		}
#endif
		break;
	default:
		break;
	}

       rc = smb5_init_ac_psy(chip);
       if (rc < 0) {
               pr_err("Couldn't initialize ac psy rc=%d\n", rc);
               goto cleanup;
           }

	rc = smb5_init_usb_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_usb_main_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb main psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_usb_port_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize usb pc_port psy rc=%d\n", rc);
		goto cleanup;
	}

	rc = smb5_init_batt_psy(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize batt psy rc=%d\n", rc);
		goto cleanup;
	}
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Jianchao.Shi@BSP.CHG.Basic, 2017/04/11, sjc Add for charging*/
            if (oppo_chg_is_usb_present()) {
                    rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
                                    CHARGING_ENABLE_CMD_BIT, 0);
                    if (rc < 0) {
                            pr_err("Couldn't disable at bootup rc=%d\n", rc);
                    }
                    msleep(100);
                    rc = smblib_masked_write(chg, CHARGING_ENABLE_CMD_REG,
                                    CHARGING_ENABLE_CMD_BIT, CHARGING_ENABLE_CMD_BIT);
                    if (rc < 0) {
                            pr_err("Couldn't enable at bootup rc=%d\n", rc);
                    }
            }
#endif
    
#ifdef CONFIG_PRODUCT_REALME_TRINKET
    /* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
            oppo_chg_parse_custom_dt(oppo_chip);
	     oppo_chg_parse_charger_dt(oppo_chip);
            oppo_chg_init(oppo_chip);
            main_psy = power_supply_get_by_name("main");
            if (main_psy) {
                    pval.intval = 1000 * oppo_chg_get_fv(oppo_chip);
                    chg_debug("init fv = %d\n", pval.intval);
                    power_supply_set_property(main_psy,
                                    POWER_SUPPLY_PROP_VOLTAGE_MAX,
                                    &pval);
                    pval.intval = 1000 * oppo_chg_get_charging_current(oppo_chip);
                    chg_debug("init current = %d\n", pval.intval);
                    power_supply_set_property(main_psy,
                                    POWER_SUPPLY_PROP_CONSTANT_CHARGE_CURRENT_MAX,
                                    &pval);
            }
#endif

	/* Register android dual-role class */
	rc = smb5_init_dual_role_class(chip);
	if (rc < 0) {
		pr_err("Couldn't initialize dual role class, rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb5_determine_initial_status(chip);
	if (rc < 0) {
		pr_err("Couldn't determine initial status rc=%d\n",
			rc);
		goto cleanup;
	}

	rc = smb5_request_interrupts(chip);
	if (rc < 0) {
		pr_err("Couldn't request interrupts rc=%d\n", rc);
		goto cleanup;
	}

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/07/31, sjc Add for using gpio as OTG ID*/
        if (oppo_usbid_check_is_gpio(oppo_chip) == true) {
                oppo_usbid_irq_register(oppo_chip);
        }
#endif
	rc = smb5_post_init(chip);
	if (rc < 0) {
		pr_err("Failed in post init rc=%d\n", rc);
		goto free_irq;
	}

	smb5_create_debugfs(chip);

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2016/12/26, sjc Add for charging*/
        g_oppo_chip->authenticate = oppo_gauge_get_batt_authenticate();
        if(!g_oppo_chip->authenticate) {
                smbchg_charging_disble();
        }
        oppo_chg_wake_update_work();
	oppo_tbatt_power_off_task_init(oppo_chip);
#endif
	rc = smb5_show_charger_status(chip);
	if (rc < 0) {
		pr_err("Failed in getting charger status rc=%d\n", rc);
		goto free_irq;
	}

	device_init_wakeup(chg->dev, true);
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/05/22, sjc Add for dump register */
        init_proc_show_regs_mask();
#endif

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Yichun.Chen  PSW.BSP.CHG  2018-05-25 show voter */
        init_proc_show_voter_mask();
#endif

	if (chg->dcin_uusb_over_gpio_en && gpio_is_valid(chg->micro_usb_gpio))
		smb_micro_usb_irq_handler(chg->micro_usb_irq, chg);

	pr_info("QPNP SMB5 probed successfully\n");

    pr_info("smb5_probe end 1 \n");

	return rc;

free_irq:
	smb5_free_interrupts(chg);
cleanup:
	smblib_deinit(chg);
	platform_set_drvdata(pdev, NULL);
    pr_info("smb5_probe end 2 \n");

	return rc;
}

static int smb5_remove(struct platform_device *pdev)
{
	struct smb5 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

	/* force enable APSD */
	smblib_masked_write(chg, USBIN_OPTIONS_1_CFG_REG,
				BC1P2_SRC_DETECT_BIT, BC1P2_SRC_DETECT_BIT);

	smb5_free_interrupts(chg);
	smblib_deinit(chg);
	platform_set_drvdata(pdev, NULL);
	return 0;
}

static void smb5_shutdown(struct platform_device *pdev)
{
	struct smb5 *chip = platform_get_drvdata(pdev);
	struct smb_charger *chg = &chip->chg;

#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/22, sjc Add for charging*/
        if (g_oppo_chip) {
                smbchg_set_chargerid_switch_val(0);
                msleep(30);
        }
#endif
	/* disable all interrupts */
	smb5_disable_interrupts(chg);

	/* configure power role for UFP */
	if (chg->connector_type == POWER_SUPPLY_CONNECTOR_TYPEC)
		smblib_masked_write(chg, TYPE_C_MODE_CFG_REG,
				TYPEC_POWER_ROLE_CMD_MASK, EN_SNK_ONLY_BIT);

	/* force enable and rerun APSD */
	smblib_apsd_enable(chg, true);
	smblib_hvdcp_exit_config(chg);
}

static const struct of_device_id match_table[] = {
	{ .compatible = "qcom,qpnp-smb5", },
	{ },
};

static struct platform_driver smb5_driver = {
	.driver		= {
		.name		= "qcom,qpnp-smb5",
		.owner		= THIS_MODULE,
		.of_match_table	= match_table,
#ifdef CONFIG_PRODUCT_REALME_TRINKET
/* Jianchao.Shi@BSP.CHG.Basic, 2017/01/25, sjc Add for charging */
                .pm             = &smb5_pm_ops,
#endif
	},
	.probe		= smb5_probe,
	.remove		= smb5_remove,
	.shutdown	= smb5_shutdown,
};
module_platform_driver(smb5_driver);

MODULE_DESCRIPTION("QPNP SMB5 Charger Driver");
MODULE_LICENSE("GPL v2");
