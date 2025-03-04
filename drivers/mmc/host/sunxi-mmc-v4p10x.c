/*
* SUNXI EMMC/SD driver
*
* Copyright (C) 2015 AllWinnertech Ltd.
* Author: lijuan <lijuan@allwinnertech.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License version 2 as
* published by the Free Software Foundation.
*
* This program is distributed "as is" WITHOUT ANY WARRANTY of any
* kind, whether express or implied; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/

#include <linux/clk.h>
#include <linux/reset.h>

#include <linux/gpio.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <linux/scatterlist.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>
#include <linux/reset.h>

#include <linux/of_address.h>
#include <linux/of_gpio.h>
#include <linux/of_platform.h>

#include <linux/mmc/host.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/slot-gpio.h>

#include "sunxi-mmc.h"
#include "sunxi-mmc-v4p10x.h"
#include "sunxi-mmc-export.h"
#include "sunxi-mmc-debug.h"



#define MMC_2MOD_CLK	"sdmmc2mod"
#define MMC_SRCCLK_PLL	"pll_periph"
#define MMC_SRCCLK_HOSC	"hosc"
#define SUNXI_RETRY_CNT_PER_PHA_V4P1X		3

/*dma triger level setting*/
#define SUNXI_DMA_TL_SDMMC_V4P1X	((0x2<<28)|(7<<16)|16)
/*one dma des can transfer data size = 1<<SUNXI_DES_SIZE_SDMMC*/
#define SUNXI_DES_SIZE_SDMMC_V4P1X	(15)

/*reg*/
#define SDXC_REG_SD_NTSR	(0x005C)
/*bit*/
#define	SDXC_2X_TIMING_MODE			(1U<<31)


/*mask*/
#define SDXC_TX_TL_MASK				(0x1f)
#define SDXC_RX_TL_MASK				(0x001F0000)
#define SDXC_STIMING_PH_MASK		(0x00000030)
#define SDXC_DRV_PH_MASK		(0x00000003)

/*shift*/
#define SDXC_STIMING_PH_SHIFT			(4)
#define SDXC_DRV_PH_SHIFT			(0)

enum sunxi_mmc_clk_mode {
	mmc_clk_400k = 0,
	mmc_clk_26M,
	mmc_clk_52M,
	mmc_clk_52M_DDR4,
	mmc_clk_52M_DDR8,
	mmc_clk_104M,
	mmc_clk_208M,
	mmc_clk_104M_DDR,
	mmc_clk_208M_DDR,
	mmc_clk_mod_num,
};

struct sunxi_mmc_clk_dly {
	enum sunxi_mmc_clk_mode cmod;
	char *mod_str;
	u32 drv_ph;
	u32 sam_ph;
};


/*sample delay and output deley setting */

struct sunxi_mmc_spec_regs {
	u32 sd_ntsr;		/*REG_SD_NTSR*/

};

struct sunxi_mmc_ver_priv {
	struct sunxi_mmc_spec_regs bak_spec_regs;
	struct sunxi_mmc_clk_dly mmc_clk_dly[mmc_clk_mod_num];
};


static void sunxi_mmc_set_clk_dly(struct sunxi_mmc_host *host, int clk,
				  int bus_width, int timing)
{
	struct mmc_host *mhost = host->mmc;
	u32 rval = 0;
	enum sunxi_mmc_clk_mode cmod = mmc_clk_400k;
	u32 in_clk_dly[5] = { 0 };
	int ret = 0;
	struct device_node *np = NULL;
	struct sunxi_mmc_clk_dly *mmc_clk_dly =
	    ((struct sunxi_mmc_ver_priv *)host->version_priv_dat)->mmc_clk_dly;

	if (!mhost->parent || !mhost->parent->of_node) {
		dev_err(mmc_dev(host->mmc),
			"no dts to parse clk dly,use default\n");
		return;
	}

	np = mhost->parent->of_node;

	if (clk <= 400 * 1000) {
		cmod = mmc_clk_400k;
	} else if (clk <= 26 * 1000 * 1000) {
		cmod = mmc_clk_26M;
	} else if (clk <= 52 * 1000 * 1000) {
		if ((bus_width == MMC_BUS_WIDTH_4)
		    && sunxi_mmc_ddr_timing(timing)) {
			cmod = mmc_clk_52M_DDR4;
		} else if ((bus_width == MMC_BUS_WIDTH_8)
			   && (timing == MMC_TIMING_MMC_DDR52)) {
			cmod = mmc_clk_52M_DDR8;
		} else {
			cmod = mmc_clk_52M;
		}
	} else if (clk <= 104 * 1000 * 1000) {
		if ((bus_width == MMC_BUS_WIDTH_8)
		    && (timing == MMC_TIMING_MMC_HS400)) {
			cmod = mmc_clk_104M_DDR;
		} else {
			cmod = mmc_clk_104M;
		}
	} else if (clk <= 208 * 1000 * 1000) {
		if ((bus_width == MMC_BUS_WIDTH_8)
		    && (timing == MMC_TIMING_MMC_HS400)) {
			cmod = mmc_clk_208M_DDR;
		} else {
			cmod = mmc_clk_208M;
		}
	} else {
		dev_err(mmc_dev(mhost), "clk %d is out of range\n", clk);
		return;
	}

	ret = of_property_read_u32_array(np, mmc_clk_dly[cmod].mod_str,
					 in_clk_dly, ARRAY_SIZE(in_clk_dly));
	if (ret) {
		dev_dbg(mmc_dev(host->mmc), "failed to get %s used default\n",
			mmc_clk_dly[cmod].mod_str);
	} else {
		mmc_clk_dly[cmod].drv_ph = in_clk_dly[0];
		/*mmc_clk_dly[cmod].sam_dly = in_clk_dly[2];*/
		/*mmc_clk_dly[cmod].ds_dly = in_clk_dly[3];*/
		mmc_clk_dly[cmod].sam_ph = in_clk_dly[3];
		dev_dbg(mmc_dev(host->mmc), "Get %s clk dly ok\n",
		mmc_clk_dly[cmod].mod_str);
	}
	dev_dbg(mmc_dev(host->mmc), "Try set %s clk dly ok\n",
		mmc_clk_dly[cmod].mod_str);
	dev_dbg(mmc_dev(host->mmc), "drv_ph %d\n",
		mmc_clk_dly[cmod].drv_ph);
	dev_dbg(mmc_dev(host->mmc), "sam_ph %d\n",
		mmc_clk_dly[cmod].sam_ph);

/*
*		rval = mmc_readl(host,REG_SAMP_DL);
*		rval &= ~SDXC_SAMP_DL_SW_MASK;
*		rval |= mmc_clk_dly[cmod].sam_dly & SDXC_SAMP_DL_SW_MASK;
*		rval |= SDXC_SAMP_DL_SW_EN;
*		mmc_writel(host,REG_SAMP_DL,rval);

*		rval = mmc_readl(host,REG_DS_DL);
*		rval &= ~SDXC_DS_DL_SW_MASK;
*		rval |= mmc_clk_dly[cmod].ds_dly & SDXC_DS_DL_SW_MASK;
*		rval |= SDXC_DS_DL_SW_EN;
*		mmc_writel(host,REG_DS_DL,rval);
*/

	rval = mmc_readl(host, REG_SD_NTSR);
	rval &= ~SDXC_DRV_PH_MASK;
	rval |=
	    (mmc_clk_dly[cmod].
	     drv_ph << SDXC_DRV_PH_SHIFT) & SDXC_DRV_PH_MASK;
	mmc_writel(host, REG_SD_NTSR, rval);

	rval = mmc_readl(host, REG_SD_NTSR);
	rval &= ~SDXC_STIMING_PH_MASK;
	rval |=
	    (mmc_clk_dly[cmod].
	     sam_ph << SDXC_STIMING_PH_SHIFT) & SDXC_STIMING_PH_MASK;
	mmc_writel(host, REG_SD_NTSR, rval);

	dev_dbg(mmc_dev(host->mmc), "REG_SD_NTSR %08x\n",
		mmc_readl(host, REG_SD_NTSR));

}

static int __sunxi_mmc_do_oclk_onoff(struct sunxi_mmc_host *host, u32 oclk_en,
				     u32 pwr_save, u32 ignore_dat0)
{
	unsigned long expire = jiffies + msecs_to_jiffies(250);
	u32 rval;

	rval = mmc_readl(host, REG_CLKCR);
	rval &= ~(SDXC_CARD_CLOCK_ON | SDXC_LOW_POWER_ON | SDXC_MASK_DATA0);

	if (oclk_en)
		rval |= SDXC_CARD_CLOCK_ON;
	if (pwr_save)
		rval |= SDXC_LOW_POWER_ON;
	if (ignore_dat0)
		rval |= SDXC_MASK_DATA0;

	mmc_writel(host, REG_CLKCR, rval);

	dev_dbg(mmc_dev(host->mmc), "%s REG_CLKCR:%x\n", __func__,
		mmc_readl(host, REG_CLKCR));

	rval = SDXC_START | SDXC_UPCLK_ONLY | SDXC_WAIT_PRE_OVER;
	mmc_writel(host, REG_CMDR, rval);

	do {
		rval = mmc_readl(host, REG_CMDR);
	} while (time_before(jiffies, expire) && (rval & SDXC_START));

	/* clear irq status bits set by the command */
	mmc_writel(host, REG_RINTR,
		mmc_readl(host, REG_RINTR) & ~SDXC_SDIO_INTERRUPT);

	if (rval & SDXC_START) {
		dev_err(mmc_dev(host->mmc), "fatal err update clk timeout\n");
		return -EIO;
	}

	/*only use mask data0 when update clk,clear it when not update clk */
	if (ignore_dat0)
		mmc_writel(host, REG_CLKCR,
			   mmc_readl(host, REG_CLKCR) & ~SDXC_MASK_DATA0);

	return 0;
}

static int sunxi_mmc_oclk_onoff(struct sunxi_mmc_host *host, u32 oclk_en)
{
	struct device_node *np = NULL;
	struct mmc_host *mmc = host->mmc;
	int pwr_save = 0;
	int len = 0;

	if (!mmc->parent || !mmc->parent->of_node) {
		dev_err(mmc_dev(host->mmc),
			"no dts to parse power save mode\n");
		return -EIO;
	}

	np = mmc->parent->of_node;
	if (of_find_property(np, "sunxi-power-save-mode", &len))
		pwr_save = 1;
	return __sunxi_mmc_do_oclk_onoff(host, oclk_en, pwr_save, 1);
}

static int sunxi_mmc_updata_pha_v4p10x(struct sunxi_mmc_host *host,
		struct mmc_command *cmd, struct mmc_data *data)
{
	return sunxi_mmc_oclk_onoff(host, 1);
}

static void sunxi_mmc_2xmod_onoff(struct sunxi_mmc_host *host, u32 newmode_en)
{
	u32 rval = mmc_readl(host, REG_SD_NTSR);

	if (newmode_en)
		rval |= SDXC_2X_TIMING_MODE;
	else
		rval &= ~SDXC_2X_TIMING_MODE;
	mmc_writel(host, REG_SD_NTSR, rval);

	dev_dbg(mmc_dev(host->mmc), "REG_SD_NTSR: 0x%08x ,val %x\n",
		mmc_readl(host, REG_SD_NTSR), rval);
}
static int sunxi_mmc_clk_set_rate_for_sdmmc_v4p10x(
	struct sunxi_mmc_host *host, struct mmc_ios *ios)
{
	u32 mod_clk = 0;
	u32 src_clk = 0;
	u32 rval = 0;
	u32 rval1 = 0;
	s32 err = 0;
	u32 rate = 0;
	u32 clk = 0;
	u32 source_rate = 0;
	u32 sdmmc2mod_rate = 0;
	char *sclk_name = NULL;
	struct clk *sclk = NULL;
	struct clk *mclk2 = NULL;
	struct device *dev = mmc_dev(host->mmc);

	if (ios->clock == 0) {
		__sunxi_mmc_do_oclk_onoff(host, 0, 0, 1);
		return 0;
	}
	if (sunxi_mmc_ddr_timing(ios->timing))
		mod_clk = ios->clock << 1;
	else
		mod_clk = ios->clock;

	if (ios->clock <= 400000) {
		sclk_name = MMC_SRCCLK_HOSC;
		sclk = clk_get(dev, sclk_name);
	} else {
		sclk_name = MMC_SRCCLK_PLL;
		sclk = clk_get(dev, sclk_name);
	}
	if ((sclk == NULL) || IS_ERR(sclk)) {
		dev_err(mmc_dev(host->mmc),
			"Error to get source clock %s %ld\n",
			sclk_name, (long)sclk);
		return -1;
	}
	sunxi_mmc_2xmod_onoff(host, 1);
	mclk2 = clk_get(dev, MMC_2MOD_CLK);

	if (IS_ERR_OR_NULL(mclk2)) {
		dev_err(mmc_dev(host->mmc),
		"Error to get source clock for clk %dHz\n", clk);
		return -1;
	}

	err = clk_set_parent(mclk2, sclk);

	source_rate = clk_get_rate(sclk);
	sdmmc2mod_rate = source_rate/2;
	clk_set_rate(mclk2, sdmmc2mod_rate);

	clk_put(mclk2);
	if (err) {
		clk_put(mclk2);
		return -1;
	}

	rate = clk_round_rate(host->clk_mmc, mod_clk);
	dev_dbg(mmc_dev(host->mmc), "get round rate %d\n", rate);

	clk_disable_unprepare(host->clk_mmc);
	/* sunxi_dump_reg(NULL); */

	err = clk_set_rate(host->clk_mmc, rate);
	if (err) {
		dev_err(mmc_dev(host->mmc),
			"set mclk rate error, rate %dHz\n",
			rate);
		clk_put(sclk);
		return -1;
	}

	rval1 = clk_prepare_enable(host->clk_mmc);
	if (rval1) {
		dev_err(mmc_dev(host->mmc),
			"Enable mmc clk err %d\n", rval1);
		return -1;
	}

	/* sunxi_dump_reg(NULL); */
	src_clk = clk_get_rate(sclk);
	clk_put(sclk);

	dev_dbg(mmc_dev(host->mmc),
		"set round clock %d, soure clk is %d\n",
		rate, src_clk);

	rval = mmc_readl(host, REG_CLKCR);
	rval &= ~0xff;
	if (sunxi_mmc_ddr_timing(ios->timing)) {
		rval |= 1;
		ios->clock = rate >> 1;
		clk = ios->clock;
		dev_dbg(mmc_dev(host->mmc), "card clk%d\n", clk);
	} else {
		ios->clock = rate;
		clk = ios->clock;
	}
	mmc_writel(host, REG_CLKCR, rval);
	sunxi_mmc_set_clk_dly(host, ios->clock, ios->bus_width, ios->timing);
	return sunxi_mmc_oclk_onoff(host, 1);
}

static void sunxi_mmc_save_spec_reg_v4p10x(struct sunxi_mmc_host *host)
{
	struct sunxi_mmc_spec_regs *spec_regs = NULL;

	spec_regs = &((struct sunxi_mmc_ver_priv *)(host->version_priv_dat))
					->bak_spec_regs;

	spec_regs->sd_ntsr = mmc_readl(host, REG_SD_NTSR);
}

static void sunxi_mmc_restore_spec_reg_v4p10x(struct sunxi_mmc_host *host)
{
	struct sunxi_mmc_spec_regs *spec_regs = NULL;

	spec_regs = &((struct sunxi_mmc_ver_priv *)(host->version_priv_dat))
						->bak_spec_regs;

	mmc_writel(host, REG_SD_NTSR, spec_regs->sd_ntsr);
}

static inline void sunxi_mmc_set_dly_raw(
	struct sunxi_mmc_host *host,
			s32 opha, s32 ipha)
{
	u32 rval =  mmc_readl(host, REG_SD_NTSR);

	if (ipha >= 0) {
		rval &= ~SDXC_STIMING_PH_MASK;
		rval |= (ipha << SDXC_STIMING_PH_SHIFT) & SDXC_STIMING_PH_MASK;
	}

	if (opha >= 0) {
		rval &= ~SDXC_DRV_PH_MASK;
		rval |= (opha << SDXC_DRV_PH_SHIFT) & SDXC_DRV_PH_MASK;
	}

	rval &= ~SDXC_2X_TIMING_MODE;
	mmc_writel(host, REG_SD_NTSR, rval);
	rval |= SDXC_2X_TIMING_MODE;
	mmc_writel(host, REG_SD_NTSR, rval);

	dev_info(mmc_dev(host->mmc), "REG_SD_NTSR: 0x%08x\n",
		mmc_readl(host, REG_SD_NTSR));
}

static int sunxi_mmc_judge_retry_v4p10x(
	struct sunxi_mmc_host *host,
	struct mmc_command *cmd, u32 rcnt,
	u32 errno, void *other)
{

	const s32 sunxi_phase[10][2] = {{-1, -1},
		{1, 1}, {0, 0}, {1, 0}, {0, 1},
		{1, 2}, {0, 2} };

	if (rcnt < (SUNXI_RETRY_CNT_PER_PHA_V4P1X*10)) {
		sunxi_mmc_set_dly_raw(host,
			sunxi_phase[rcnt/SUNXI_RETRY_CNT_PER_PHA_V4P1X][0],
			sunxi_phase[rcnt/SUNXI_RETRY_CNT_PER_PHA_V4P1X][1]);
	} else {
		sunxi_mmc_set_dly_raw(host, sunxi_phase[0][0],
							sunxi_phase[0][1]);
		dev_info(mmc_dev(host->mmc), "sunxi v4p10x retry give up\n");
		return -1;
	}
	return 0;
}

static bool sunxi_mmc_hw_busy_v4p10x(struct sunxi_mmc_host *host)
{
	/**if use v4p10x sdmc, all use dat0-gpio check card busy status*/
	if (host->sunxi_mmc_dat0_busy)
		return true;

	return false;
}

static int sunxi_mmc_dat0_busy_v4p10x(struct sunxi_mmc_host *host)
{
	struct device_node *np;
	struct mmc_host *mmc = host->mmc;
	unsigned long config_set;
	unsigned long config_get = 0;
	struct gpio_config gpio_flags;
	int gpio;

	if (!mmc->parent || !mmc->parent->of_node)
		return 0;

	np = mmc->parent->of_node;
	gpio = of_get_named_gpio_flags(np, "dat0-gpios", 0,
					(enum of_gpio_flags *)&gpio_flags);
	if (!gpio_is_valid(gpio))
		pr_err("mmc:failed to get dat0-gpios\n");
	else {
		/***********change gpio func to input*************/
		config_set = pinconf_to_config_packed((enum pin_config_param)SUNXI_PINCFG_TYPE_FUNC, 0);
		pinctrl_gpio_set_config(gpio, config_set);

		/***********get sdcx_dat0 value*************/
		config_get = gpio_get_value(gpio);

		/***********change gpio func to sdcx_dat0*************/
		config_set = pinconf_to_config_packed((enum pin_config_param)SUNXI_PINCFG_TYPE_FUNC, 3);
		pinctrl_gpio_set_config(gpio, config_set);
	}

	return (!config_get);
}

void sunxi_mmc_init_priv_v4p10x(struct sunxi_mmc_host *host,
			       struct platform_device *pdev, int phy_index)
{
	struct sunxi_mmc_ver_priv *ver_priv =
	    devm_kzalloc(&pdev->dev, sizeof(struct sunxi_mmc_ver_priv),
			 GFP_KERNEL);
	host->version_priv_dat = ver_priv;
	ver_priv->mmc_clk_dly[mmc_clk_400k].cmod = mmc_clk_400k;
	ver_priv->mmc_clk_dly[mmc_clk_400k].mod_str = "sunxi-dly-400k";
	ver_priv->mmc_clk_dly[mmc_clk_400k].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_400k].sam_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_26M].cmod = mmc_clk_26M;
	ver_priv->mmc_clk_dly[mmc_clk_26M].mod_str = "sunxi-dly-26M";
	ver_priv->mmc_clk_dly[mmc_clk_26M].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_26M].sam_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M].cmod = mmc_clk_52M,
	ver_priv->mmc_clk_dly[mmc_clk_52M].mod_str = "sunxi-dly-52M";
	ver_priv->mmc_clk_dly[mmc_clk_52M].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M].sam_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR4].cmod = mmc_clk_52M_DDR4;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR4].mod_str =
		"sunxi-dly-52M-ddr4";
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR4].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR4].sam_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR8].cmod = mmc_clk_52M_DDR8;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR8].mod_str =
		"sunxi-dly-52M-ddr8";
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR8].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_52M_DDR8].sam_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_104M].cmod = mmc_clk_104M;
	ver_priv->mmc_clk_dly[mmc_clk_104M].mod_str =
		"sunxi-dly-104M";
	ver_priv->mmc_clk_dly[mmc_clk_104M].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_104M].sam_ph = 00;
	ver_priv->mmc_clk_dly[mmc_clk_208M].cmod = mmc_clk_208M;
	ver_priv->mmc_clk_dly[mmc_clk_208M].mod_str =
		"sunxi-dly-208M";
	ver_priv->mmc_clk_dly[mmc_clk_208M].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_208M].sam_ph = 00;
	ver_priv->mmc_clk_dly[mmc_clk_104M_DDR].cmod = mmc_clk_104M_DDR;
	ver_priv->mmc_clk_dly[mmc_clk_104M_DDR].mod_str =
		"sunxi-dly-104M-ddr";
	ver_priv->mmc_clk_dly[mmc_clk_104M_DDR].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_104M_DDR].sam_ph = 00;
	ver_priv->mmc_clk_dly[mmc_clk_208M_DDR].cmod = mmc_clk_208M_DDR;
	ver_priv->mmc_clk_dly[mmc_clk_208M_DDR].mod_str =
		"sunxi-dly-208M-ddr";
	ver_priv->mmc_clk_dly[mmc_clk_208M_DDR].drv_ph = 01;
	ver_priv->mmc_clk_dly[mmc_clk_208M_DDR].sam_ph = 00;

	host->sunxi_mmc_clk_set_rate = sunxi_mmc_clk_set_rate_for_sdmmc_v4p10x;
	host->dma_tl = SUNXI_DMA_TL_SDMMC_V4P1X;
	host->idma_des_size_bits = SUNXI_DES_SIZE_SDMMC_V4P1X;
	host->sunxi_mmc_thld_ctl = NULL;
	host->sunxi_mmc_save_spec_reg = sunxi_mmc_save_spec_reg_v4p10x;
	host->sunxi_mmc_restore_spec_reg = sunxi_mmc_restore_spec_reg_v4p10x;
	sunxi_mmc_reg_ex_res_inter(host, phy_index);
	host->sunxi_mmc_set_acmda = sunxi_mmc_set_a12a;
	host->phy_index = phy_index;
	host->sunxi_mmc_oclk_en = sunxi_mmc_oclk_onoff;
	host->sunxi_mmc_judge_retry = sunxi_mmc_judge_retry_v4p10x;
	host->sunxi_mmc_updata_pha = sunxi_mmc_updata_pha_v4p10x;
	host->sunxi_mmc_hw_busy = sunxi_mmc_hw_busy_v4p10x;
	host->sunxi_mmc_dat0_busy = sunxi_mmc_dat0_busy_v4p10x;
	/*sunxi_of_parse_clk_dly(host);*/
}
EXPORT_SYMBOL_GPL(sunxi_mmc_init_priv_v4p10x);
