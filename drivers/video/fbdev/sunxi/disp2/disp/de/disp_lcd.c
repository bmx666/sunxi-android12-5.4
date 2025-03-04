/*
 * Allwinner SoCs display driver.
 *
 * Copyright (C) 2016 Allwinner.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/reset.h>
#include "disp_lcd.h"
#include "../dev_disp.h"

extern void lcd_set_panel_funs(void);
#define DISP_LCD_MODE_DIRTY_MASK 0x80000000

struct disp_lcd_private_data {
	struct disp_lcd_flow open_flow;
	struct disp_lcd_flow close_flow;
	struct disp_panel_para panel_info;
	struct panel_extend_para panel_extend_info;
	struct panel_extend_para panel_extend_info_set;
	u32    panel_extend_dirty;
	struct disp_lcd_cfg lcd_cfg;
	struct disp_lcd_panel_fun lcd_panel_fun;
	bool enabling;
	bool disabling;
	bool bl_enabled;
	u32 irq_no;
	u32 irq_no_dsi;
	u32 irq_no_edp;
	u32 enabled;
	u32 power_enabled;
	u32 bl_need_enabled;
	u32 frame_per_sec;
	u32 usec_per_line;
	u32 judge_line;
	u32 tri_finish_fail;
	s32 color_temperature;
	u32 color_inverse;
	/*0:reset all module, 1:reset panel only*/
	u32 esd_reset_level;
	struct {
		uintptr_t dev;
		u32 channel;
		u32 polarity;
		u32 period_ns;
		u32 duty_ns;
		u32 enabled;
	} pwm_info;

	struct clk *clk_tcon_lcd;
	struct clk *clk_bus_tcon_lcd;
	struct clk *clk_lvds;
	struct clk *clk_edp;
	struct clk *clk_parent;

	struct reset_control *rst;
	struct reset_control *rst_bus_lvds;
	struct reset_control *rst_bus_tcon_lcd;
#if defined(SUPPORT_DSI)
	struct clk *clk_mipi_dsi[CLK_NUM_PER_DSI];
	struct clk *clk_bus_mipi_dsi[CLK_NUM_PER_DSI];
	struct reset_control *rst_bus_mipi_dsi;
#endif

	/*0:no reset process;1:reset request;2:resetting*/
	atomic_t lcd_resetting;
	struct work_struct reflush_work;
	struct disp_lcd_esd_info esd_inf;
	struct disp_device_config config;
};
static spinlock_t lcd_data_lock;

static struct disp_device *lcds;
static struct disp_lcd_private_data *lcd_private;

struct disp_device *disp_get_lcd(u32 disp)
{
	u32 num_screens;

	num_screens = bsp_disp_feat_get_num_screens();
	if (disp >= num_screens
	    || !bsp_disp_feat_is_supported_output_types(disp,
							DISP_OUTPUT_TYPE_LCD)) {
		DE_INF("disp %d not support lcd output\n", disp);
		return NULL;
	}

	return &lcds[disp];
}
static struct disp_lcd_private_data *disp_lcd_get_priv(struct disp_device *lcd)
{
	if (lcd == NULL) {
		DE_WRN("param is NULL!\n");
		return NULL;
	}

	return (struct disp_lcd_private_data *)lcd->priv_data;
}

static s32 disp_lcd_is_used(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	s32 ret = 0;

	if ((lcd == NULL) || (lcdp == NULL))
		ret = 0;
	else
		ret = (s32)lcdp->lcd_cfg.lcd_used;

	return ret;
}

static s32 lcd_parse_panel_para(u32 disp, struct disp_panel_para *info)
{
	s32 ret = 0;
	char primary_key[25];
	s32 value = 0;

	sprintf(primary_key, "lcd%d", disp);
	memset(info, 0, sizeof(struct disp_panel_para));

	ret = disp_sys_script_get_item(primary_key, "lcd_x", &value, 1);
	if (ret == 1)
		info->lcd_x = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_y", &value, 1);
	if (ret == 1)
		info->lcd_y = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_width", &value, 1);
	if (ret == 1)
		info->lcd_width = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_height", &value, 1);
	if (ret == 1)
		info->lcd_height = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_dclk_freq", &value, 1);
	if (ret == 1)
		info->lcd_dclk_freq = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_pwm_used", &value, 1);
	if (ret == 1)
		info->lcd_pwm_used = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_pwm_ch", &value, 1);
	if (ret == 1)
		info->lcd_pwm_ch = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_pwm_freq", &value, 1);
	if (ret == 1)
		info->lcd_pwm_freq = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_pwm_pol", &value, 1);
	if (ret == 1)
		info->lcd_pwm_pol = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_if", &value, 1);
	if (ret == 1)
		info->lcd_if = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_hbp", &value, 1);
	if (ret == 1)
		info->lcd_hbp = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_ht", &value, 1);
	if (ret == 1)
		info->lcd_ht = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_vbp", &value, 1);
	if (ret == 1)
		info->lcd_vbp = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_vt", &value, 1);
	if (ret == 1)
		info->lcd_vt = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_gsensor_detect", &value, 1);
	if (ret == 1)
		info->lcd_gsensor_detect = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_hv_if", &value, 1);
	if (ret == 1)
		info->lcd_hv_if = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_vspw", &value, 1);
	if (ret == 1)
		info->lcd_vspw = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_hspw", &value, 1);
	if (ret == 1)
		info->lcd_hspw = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_lvds_if", &value, 1);
	if (ret == 1)
		info->lcd_lvds_if = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_lvds_mode", &value, 1);
	if (ret == 1)
		info->lcd_lvds_mode = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_lvds_colordepth", &value,
				     1);
	if (ret == 1)
		info->lcd_lvds_colordepth = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_lvds_io_polarity",
				     &value, 1);
	if (ret == 1)
		info->lcd_lvds_io_polarity = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_cpu_if", &value, 1);
	if (ret == 1)
		info->lcd_cpu_if = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_cpu_te", &value, 1);
	if (ret == 1)
		info->lcd_cpu_te = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_cpu_mode", &value, 1);
	if (ret == 1)
		info->lcd_cpu_mode = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_frm", &value, 1);
	if (ret == 1)
		info->lcd_frm = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_dsi_if", &value, 1);
	if (ret == 1)
		info->lcd_dsi_if = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_dsi_lane", &value, 1);
	if (ret == 1)
		info->lcd_dsi_lane = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_dsi_format", &value, 1);
	if (ret == 1)
		info->lcd_dsi_format = value;


	ret = disp_sys_script_get_item(primary_key, "lcd_dsi_eotp", &value, 1);
	if (ret == 1)
		info->lcd_dsi_eotp = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_dsi_te", &value, 1);
	if (ret == 1)
		info->lcd_dsi_te = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_dsi_port_num",
				       &value, 1);
	if (ret == 1)
		info->lcd_dsi_port_num = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_tcon_mode", &value, 1);
	if (ret == 1)
		info->lcd_tcon_mode = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_slave_tcon_num",
				       &value, 1);
	if (ret == 1)
		info->lcd_slave_tcon_num = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_slave_stop_pos",
				       &value, 1);
	if (ret == 1)
		info->lcd_slave_stop_pos = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_sync_pixel_num",
				       &value, 1);
	if (ret == 1)
		info->lcd_sync_pixel_num = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_sync_line_num",
				       &value, 1);
	if (ret == 1)
		info->lcd_sync_line_num = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_tcon_en_odd_even_div",
				       &value, 1);
	if (ret == 1)
		info->lcd_tcon_en_odd_even = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_fsync_en", &value, 1);
	if (ret == 1)
		info->lcd_fsync_en = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_fsync_act_time", &value, 1);
	if (ret == 1)
		info->lcd_fsync_act_time = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_fsync_dis_time", &value, 1);
	if (ret == 1)
		info->lcd_fsync_dis_time = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_fsync_pol", &value,
				     1);
	if (ret == 1)
		info->lcd_fsync_pol = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_clk_phase", &value,
				     1);
	if (ret == 1)
		info->lcd_hv_clk_phase = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_sync_polarity",
				     &value, 1);
	if (ret == 1)
		info->lcd_hv_sync_polarity = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_data_polarity",
				     &value, 1);
	if (ret == 1)
		info->lcd_hv_data_polarity = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_srgb_seq", &value, 1);
	if (ret == 1)
		info->lcd_hv_srgb_seq = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_rb_swap", &value, 1);
	if (ret == 1)
		info->lcd_rb_swap = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_syuv_seq", &value, 1);
	if (ret == 1)
		info->lcd_hv_syuv_seq = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_hv_syuv_fdly", &value,
				     1);
	if (ret == 1)
		info->lcd_hv_syuv_fdly = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_gamma_en", &value, 1);
	if (ret == 1)
		info->lcd_gamma_en = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_cmap_en", &value, 1);
	if (ret == 1)
		info->lcd_cmap_en = value;

	ret = disp_sys_script_get_item(primary_key, "lcd_xtal_freq", &value, 1);
	if (ret == 1)
		info->lcd_xtal_freq = value;

	ret =
	    disp_sys_script_get_item(primary_key, "lcd_size",
				     (int *)info->lcd_size, 2);
	ret =
	    disp_sys_script_get_item(primary_key, "lcd_model_name",
				     (int *)info->lcd_model_name, 2);
	ret =
	    disp_sys_script_get_item(primary_key, "lcd_driver_name",
				     (int *)info->lcd_driver_name, 2);

	return 0;
}

static void lcd_get_sys_config(u32 disp, struct disp_lcd_cfg *lcd_cfg)
{
	struct disp_gpio_info *gpio_info;
	int value = 1;
	char primary_key[20], sub_name[25];
	int i = 0;
	int ret;

	sprintf(primary_key, "lcd%d", disp);
	/* lcd_used */
	ret = disp_sys_script_get_item(primary_key, "lcd_used", &value, 1);
	if (ret == 1)
		lcd_cfg->lcd_used = value;

	if (lcd_cfg->lcd_used == 0)
		return;

	/* lcd_bl_en */
	lcd_cfg->lcd_bl_en_used = 0;
	gpio_info = &(lcd_cfg->lcd_bl_en);
	ret = disp_sys_script_get_item(primary_key, "lcd_bl_en", (int *)gpio_info, 3);
	if (!ret)
		lcd_cfg->lcd_bl_en_used = 1;

	sprintf(sub_name, "lcd_bl_en_power");
	ret = disp_sys_script_get_item(primary_key, "lcd_bl_en_power", (int *)lcd_cfg->lcd_bl_en_power, 2);
	if (ret)
		return;

	/* lcd_power */
	for (i = 0; i < LCD_POWER_NUM; i++) {
		if (i == 0)
			sprintf(sub_name, "lcd_power");
		else
			sprintf(sub_name, "lcd_power%d", i);
		lcd_cfg->lcd_power_used[i] = 0;
		ret =
		    disp_sys_script_get_item(primary_key, sub_name,
					     (int *)(lcd_cfg->lcd_power[i]), 2);
		if (ret == 2)
			/* str */
			lcd_cfg->lcd_power_used[i] = 1;
	}

	/* lcd_gpio */
	for (i = 0; i < 6; i++) {
		sprintf(sub_name, "lcd_gpio_%d", i);

		gpio_info = &(lcd_cfg->lcd_gpio[i]);
		ret = disp_sys_script_get_item(primary_key, sub_name, (int *)gpio_info, 3);
		if (!ret)
			lcd_cfg->lcd_gpio_used[i] = 1;
	}

	/* lcd_gpio_scl,lcd_gpio_sda */
	gpio_info = &(lcd_cfg->lcd_gpio[LCD_GPIO_SCL]);
	ret = disp_sys_script_get_item(primary_key, "lcd_gpio_scl", (int *)gpio_info, 3);
	if (ret == 3)
		lcd_cfg->lcd_gpio_used[LCD_GPIO_SCL] = 1;

	gpio_info = &(lcd_cfg->lcd_gpio[LCD_GPIO_SDA]);
	ret = disp_sys_script_get_item(primary_key, "lcd_gpio_sda", (int *)gpio_info, 3);
	if (ret == 3)
		lcd_cfg->lcd_gpio_used[LCD_GPIO_SDA] = 1;

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		sprintf(sub_name, "lcd_gpio_power%d", i);

		ret =
		    disp_sys_script_get_item(primary_key, sub_name,
					     (int *)lcd_cfg->lcd_gpio_power[i],
					     2);
	}

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		if (i == 0)
			sprintf(sub_name, "lcd_pin_power");
		else
			sprintf(sub_name, "lcd_pin_power%d", i);
		ret =
		    disp_sys_script_get_item(primary_key, sub_name,
					     (int *)lcd_cfg->lcd_pin_power[i],
					     2);
	}

/* backlight adjust */
	for (i = 0; i < 101; i++) {
		sprintf(sub_name, "lcd_bl_%d_percent", i);
		lcd_cfg->backlight_curve_adjust[i] = 0;

		if (i == 100)
			lcd_cfg->backlight_curve_adjust[i] = 255;

		ret =
		    disp_sys_script_get_item(primary_key, sub_name, &value, 1);
		if (ret == 1) {
			value = (value > 100) ? 100 : value;
			value = value * 255 / 100;
			lcd_cfg->backlight_curve_adjust[i] = value;
		}
	}

	sprintf(sub_name, "lcd_backlight");
	ret = disp_sys_script_get_item(primary_key, sub_name, &value, 1);
	if (ret == 1) {
		value = (value > 256) ? 256 : value;
		lcd_cfg->backlight_bright = value;
	} else {
		lcd_cfg->backlight_bright = 197;
	}

}

static s32 lcd_clk_init(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	DE_INF("lcd %d clk init\n", lcd->disp);

	lcdp->clk_parent = clk_get_parent(lcdp->clk_tcon_lcd);

	return DIS_SUCCESS;
}

static s32 lcd_clk_exit(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	if (lcdp->clk_parent)
		clk_put(lcdp->clk_parent);

	return DIS_SUCCESS;
}

static int lcd_regulator_get_wrap(struct disp_lcd_cfg *lcd_cfg)
{
	int i;
	struct device *dev = g_disp_drv.dev;
	struct regulator *regulator;

	if (strlen(lcd_cfg->lcd_bl_en_power)) {
		regulator = regulator_get(dev, lcd_cfg->lcd_bl_en_power);
		if (!regulator)
			DE_WRN("regulator_get for %s failed\n", lcd_cfg->lcd_bl_en_power);
		else
			lcd_cfg->bl_regulator = regulator;
	}

	for (i = 0; i < LCD_POWER_NUM; i++) {
		if (strlen(lcd_cfg->lcd_power[i])) {
			regulator = regulator_get(dev, lcd_cfg->lcd_power[i]);
			if (!regulator) {
				DE_WRN("regulator_get for %s failed\n", lcd_cfg->lcd_power[i]);
				continue;
			}
			lcd_cfg->regulator[i] = regulator;
		}
	}

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		if (strlen(lcd_cfg->lcd_gpio_power[i])) {
			regulator = regulator_get(dev, lcd_cfg->lcd_gpio_power[i]);
			if (!regulator) {
				DE_WRN("regulator_get for %s failed\n", lcd_cfg->lcd_gpio_power[i]);
				continue;
			}
			lcd_cfg->gpio_regulator[i] = regulator;
		}
	}

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		if (strlen(lcd_cfg->lcd_pin_power[i])) {
			regulator = regulator_get(dev, lcd_cfg->lcd_pin_power[i]);
			if (!regulator) {
				DE_WRN("regulator_get for %s failed\n", lcd_cfg->lcd_pin_power[i]);
				continue;
			}
			lcd_cfg->pin_regulator[i] = regulator;
		}
	}

	return 0;
}

static void lcd_regulator_put_wrap(struct disp_lcd_cfg *lcd_cfg)
{
	int i;

	if (lcd_cfg->bl_regulator)
		regulator_put(lcd_cfg->bl_regulator);

	for (i = 0; i < LCD_POWER_NUM; i++) {
		if (lcd_cfg->regulator[i])
			regulator_put(lcd_cfg->regulator[i]);
	}

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		if (lcd_cfg->gpio_regulator[i])
			regulator_put(lcd_cfg->gpio_regulator[i]);
	}

	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		if (lcd_cfg->pin_regulator[i])
			regulator_put(lcd_cfg->pin_regulator[i]);
	}
}

/**
 * @name   :cal_real_frame_period
 * @brief  :set lcd->timings.frame_period (nsec)
 *              ,lcd->timings.start_delay (line)
 *              and lcd->timings.dclk_rate_set(hz)
 * @param  :lcd:pointer of disp_device
 * @return :0 if success, other fail
 */
static s32 cal_real_frame_period(struct disp_device *lcd)
{
	s32 ret = -1;
	struct disp_lcd_private_data *lcdp;
	struct lcd_clk_info clk_info;
	unsigned long long temp = 0;

	if (!lcd) {
		DE_WRN("NULL hdl!\n");
		goto OUT;
	}

	lcdp = disp_lcd_get_priv(lcd);

	if (!lcdp) {
		DE_WRN("NULL hdl!\n");
		goto OUT;
	}

	memset(&clk_info, 0, sizeof(struct lcd_clk_info));
	disp_al_lcd_get_clk_info(lcd->hwdev_index, &clk_info,
				 &lcdp->panel_info);

	if (!lcdp->clk_tcon_lcd) {
		DE_WRN("clk is NULL!\n");
		goto OUT;
	}

#if defined(SUPPORT_DSI)
	if (lcdp->panel_info.lcd_if == LCD_IF_DSI)
		lcd->timings.dclk_rate_set = clk_get_rate(lcdp->clk_mipi_dsi[0]) / clk_info.dsi_div;
#endif

	if (lcdp->panel_info.lcd_if != LCD_IF_DSI)
		lcd->timings.dclk_rate_set = clk_get_rate(lcdp->clk_tcon_lcd) / clk_info.tcon_div;

	if (lcd->timings.dclk_rate_set == 0) {
		DE_WRN("lcd dclk_rate_set is 0!\n");
		ret = -2;
		goto OUT;
	}

	temp = ONE_SEC * lcdp->panel_info.lcd_ht * lcdp->panel_info.lcd_vt;

	do_div(temp, lcd->timings.dclk_rate_set);

	lcd->timings.frame_period = temp;

	lcd->timings.start_delay =
	    disp_al_lcd_get_start_delay(lcd->hwdev_index, &lcdp->panel_info);

	DE_INF("lcd frame period:%llu\n", lcd->timings.frame_period);

	ret = 0;
OUT:
	return ret;
}

static s32 lcd_clk_config(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct lcd_clk_info clk_info;
	unsigned long pll_rate = 297000000, lcd_rate = 33000000;
	unsigned long dclk_rate = 33000000, dsi_rate = 0;	/* hz */
	unsigned long pll_rate_set = 297000000, lcd_rate_set = 33000000;
	unsigned long dclk_rate_set = 33000000, dsi_rate_set = 0;	/* hz */
#if defined(SUPPORT_DSI)
	u32 i = 0, j = 0;
	u32 dsi_num = 0;
#endif
	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}
	memset(&clk_info, 0, sizeof(struct lcd_clk_info));
	disp_al_lcd_get_clk_info(lcd->hwdev_index, &clk_info,
				 &lcdp->panel_info);
	dclk_rate = lcdp->panel_info.lcd_dclk_freq * 1000000;	/* Mhz -> hz */
	if (lcdp->panel_info.lcd_if == LCD_IF_DSI) {
		lcd_rate = dclk_rate * clk_info.dsi_div;
		pll_rate = lcd_rate * clk_info.lcd_div;
	} else {
		lcd_rate = dclk_rate * clk_info.tcon_div;
		pll_rate = lcd_rate * clk_info.lcd_div;
	}
	dsi_rate = pll_rate / clk_info.dsi_div;

	if (lcdp->clk_parent) {
		clk_set_rate(lcdp->clk_parent, pll_rate);
		pll_rate_set = clk_get_rate(lcdp->clk_parent);
	}

	if (clk_info.lcd_div)
		lcd_rate_set = pll_rate_set / clk_info.lcd_div;
	else
		lcd_rate_set = pll_rate_set;

	clk_set_rate(lcdp->clk_tcon_lcd, lcd_rate_set);
	lcd_rate_set = clk_get_rate(lcdp->clk_tcon_lcd);
#if defined(SUPPORT_DSI)
	if (lcdp->panel_info.lcd_if == LCD_IF_DSI) {
		if (lcdp->panel_info.lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE)
			dsi_rate_set = pll_rate_set;
		else
			dsi_rate_set = pll_rate_set / clk_info.dsi_div;
		dsi_rate_set =
		    (clk_info.dsi_rate == 0) ? dsi_rate_set : clk_info.dsi_rate;
		dsi_num = (lcdp->panel_info.lcd_tcon_mode == DISP_TCON_DUAL_DSI)
			      ? 2
			      : 1;
		dsi_rate_set /= dsi_num;
		/* total number of dsi clk for current disp device*/
		dsi_num *= CLK_NUM_PER_DSI;
		/*In the case of CLK_NUM_PER_DSI equal to 2*/
		/*even index mean hs clk which need to be seted*/
		/*odd index mean lp clk which no need to be seted*/
		for (i = dsi_num * lcd->disp, j = 0; i < CLK_DSI_NUM;
		     i += CLK_NUM_PER_DSI) {
			if (lcdp->clk_mipi_dsi[i]) {
				clk_set_rate(lcdp->clk_mipi_dsi[i], dsi_rate_set);
				DE_INF(
				    "clk_set_rate:dsi's %d th clk with %ld\n",
				    i, dsi_rate_set);
				j += CLK_NUM_PER_DSI;
			}
			if (j == dsi_num)
				break;
		}
		/*FIXME, dsi clk0 = dsi clk1(rate)*/
		/*disp_sys_clk_set_rate(lcdp->clk_dsi[1], dsi_rate_set);*/
	}
#endif

	dclk_rate_set = lcd_rate_set / clk_info.tcon_div;
	if ((pll_rate_set != pll_rate) || (lcd_rate_set != lcd_rate)
	    || (dclk_rate_set != dclk_rate)) {
		DE_WRN
		    ("disp %d, clk: pll(%ld),clk(%ld),dclk(%ld) dsi_rate(%ld)\n     clk real:pll(%ld),clk(%ld),dclk(%ld) dsi_rate(%ld)\n",
		     lcd->disp, pll_rate, lcd_rate, dclk_rate, dsi_rate,
		     pll_rate_set, lcd_rate_set, dclk_rate_set, dsi_rate_set);
	}

	return 0;
}

static s32 lcd_clk_enable(struct disp_device *lcd)
{
#if (defined SUPPORT_COMBO_DPHY) || (defined SUPPORT_DSI)
	int enable_mipi = 0;
	u32 i = 0;
#endif
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int ret = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}
	lcd_clk_config(lcd);

	ret = reset_control_deassert(lcdp->rst_bus_tcon_lcd);
	if (ret) {
		DE_WRN("%s: reset_control_deassert for rst_bus_tcon_lcd failed\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(lcdp->clk_tcon_lcd);
	if (ret) {
		DE_WRN("%s: clk_prepare_enable for clk_tcon_lcd failed\n", __func__);
		return ret;
	}

	ret = clk_prepare_enable(lcdp->clk_bus_tcon_lcd);
	if (ret) {
		DE_WRN("%s: clk_prepare_enable for clk_bus_tcon_lcd failed\n", __func__);
		return ret;
	}

	if (lcdp->panel_info.lcd_if == LCD_IF_LVDS) {
		ret = reset_control_deassert(lcdp->rst_bus_lvds);
		if (ret) {
			DE_WRN("%s: reset_control_deassert for rst_lvds failed\n", __func__);
			return ret;
		}
		ret = clk_prepare_enable(lcdp->clk_lvds);
		if (ret) {
			DE_WRN("%s: clk_prepare_enable for clk_lvds failed\n", __func__);
			return ret;
		}
#ifdef SUPPORT_COMBO_DPHY
		enable_mipi = 1;
#endif
	} else if (lcdp->panel_info.lcd_if == LCD_IF_DSI) {
#if defined(SUPPORT_DSI)
		enable_mipi = 1;
#endif
	} else if (lcdp->panel_info.lcd_if == LCD_IF_EDP) {
		ret = clk_prepare_enable(lcdp->clk_edp);
		if (ret) {
			DE_WRN("%s: clk_prepare_enable for clk_edp failed\n", __func__);
			return ret;
		}
	}

#if defined(SUPPORT_DSI)
	if (enable_mipi) {
		for (i = 0; i < CLK_DSI_NUM; i++) {
			ret = reset_control_deassert(lcdp->rst_bus_mipi_dsi);
			if (ret) {
				DE_WRN("%s: reset_control_deassert for rst_bus_mipi_dsi[%d] failed\n", __func__, i);
				return ret;
			}

			ret = clk_prepare_enable(lcdp->clk_mipi_dsi[i]);
			if (ret) {
				DE_WRN("%s: clk_prepare_enable for clk_mipi_dsi[%d] failed\n", __func__, lcd->hwdev_index);
				return ret;
			}

			ret = clk_prepare_enable(lcdp->clk_bus_mipi_dsi[i]);
			if (ret) {
				DE_WRN("%s: clk_prepare_enable for clk_bus_mipi_dsi[%d] failed\n", __func__, lcd->hwdev_index);
				return ret;
			}
		}
	}
#endif

	return ret;
}

static s32 lcd_clk_disable(struct disp_device *lcd)
{
	int ret;
#if defined(SUPPORT_DSI)
	int disable_mipi = 0;
	u32 i = 0;
#endif

	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	if (lcdp->panel_info.lcd_if == LCD_IF_LVDS) {
		clk_disable(lcdp->clk_lvds);
#ifdef SUPPORT_COMBO_DPHY
		if (lcd->hwdev_index == 0)
			clk_disable(lcdp->clk_mipi_dsi[0]);

		ret = reset_control_assert(lcdp->rst_bus_lvds);
		if (ret) {
			DE_WRN("%s: reset_control_assert for rst_bus_lvds failed\n", __func__);
			return ret;
		}
#if defined(SUPPORT_DSI)
		disable_mipi = 1;
#endif
#endif
#if defined(SUPPORT_DSI)
	} else if (lcdp->panel_info.lcd_if == LCD_IF_DSI) {
		disable_mipi = 1;
#endif
	} else if (lcdp->panel_info.lcd_if == LCD_IF_EDP) {
		clk_disable_unprepare(lcdp->clk_edp);
	}

#if defined(SUPPORT_DSI)
	if (disable_mipi) {
		for (i = 0; i < CLK_NUM_PER_DSI; i++) {
			clk_disable_unprepare(lcdp->clk_bus_mipi_dsi[i]);
			clk_disable_unprepare(lcdp->clk_mipi_dsi[i]);
		}

		ret = reset_control_assert(lcdp->rst_bus_mipi_dsi);
		if (ret) {
			DE_WRN("%s: reset_control_assert for rst_bus_mipi_dsi failed\n", __func__);
			return ret;
		}
	}
#endif

	clk_disable_unprepare(lcdp->clk_bus_tcon_lcd);
	clk_disable_unprepare(lcdp->clk_tcon_lcd);

	ret = reset_control_assert(lcdp->rst);
	if (ret) {
		DE_WRN("%s: reset_control_assert for rst failed\n", __func__);
		return ret;
	}

	return 0;
}

static int lcd_calc_judge_line(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	if (lcdp->usec_per_line == 0) {
		struct disp_panel_para *panel_info = &lcdp->panel_info;
		/*
		 * usec_per_line = 1 / fps / vt * 1000000
		 *               = 1 / (dclk * 1000000 / vt / ht) / vt * 1000000
		 *               = ht / dclk(Mhz)
		 */
		lcdp->usec_per_line = panel_info->lcd_ht
		    / panel_info->lcd_dclk_freq;
	}

	if (lcdp->judge_line == 0) {
		int start_delay = disp_al_lcd_get_start_delay(lcd->hwdev_index,
							      &lcdp->
							      panel_info);
		int usec_start_delay = start_delay * lcdp->usec_per_line;
		int usec_judge_point;

		if (usec_start_delay <= 200)
			usec_judge_point = usec_start_delay * 3 / 7;
		else if (usec_start_delay <= 400)
			usec_judge_point = usec_start_delay / 2;
		else
			usec_judge_point = 200;
		lcdp->judge_line = usec_judge_point / lcdp->usec_per_line;
	}

	return 0;
}

#ifdef EINK_FLUSH_TIME_TEST
struct timeval lcd_start, lcd_mid, lcd_mid1, lcd_mid2, lcd_end, t5_b, t5_e;
struct timeval pin_b, pin_e, po_b, po_e, tocn_b, tcon_e;
unsigned int lcd_t1 = 0, lcd_t2 = 0, lcd_t3 = 0, lcd_t4 = 0, lcd_t5 = 0;
unsigned int lcd_pin, lcd_po, lcd_tcon;
#endif

extern int sunxi_get_soc_chipid_str(char *serial);

static s32 disp_lcd_speed_limit(struct disp_panel_para *panel, u32 *min_dclk,
				u32 *max_dclk)
{
#if defined(CONFIG_ARCH_SUN50IW10)
	char id[17] = "";
	unsigned long value = 0;
	unsigned int qa_val = 0;
	unsigned int ic_ver = 0, display_cfg_flag = 0;
	void *qa_addr = NULL, *ic_ver_addr = NULL, *disp_cfg_addr = NULL;

	qa_addr = ioremap(0x0300621c, 4);
	ic_ver_addr = ioremap(0x03000024, 4);
	disp_cfg_addr = ioremap(0x03006218, 4);
#endif

	/*init unlimit*/
	*min_dclk = 0;
	*max_dclk = 9999;

#if defined(CONFIG_ARCH_SUN50IW10)
	if (!qa_addr || !ic_ver_addr || !disp_cfg_addr) {
		DE_WRN("ioremap fail!%p %p %p\n", qa_addr, ic_ver_addr,
		       disp_cfg_addr);
		goto OUT;
	}
	qa_val = readl(qa_addr);
	qa_val = (qa_val >> 28) & 0x00000003;
	ic_ver = readl(ic_ver_addr) & 0x00000007;
	display_cfg_flag = (readl(disp_cfg_addr) >> 12) & 0x00000001;
	sunxi_get_soc_chipid_str(id);

	if (qa_val >= 2 && panel->lcd_if == LCD_IF_DSI) {
		/*bad IC  not support DSI*/
		*min_dclk = 9999;
		*max_dclk = 0;
		goto OUT;
	}

	if (!kstrtoul(id, 16, &value)) {

		switch (value) {
		case 0x3c00: /*A53*/
			{
				if (panel->lcd_if == LCD_IF_DSI) {
					if (qa_val == 0)
						*min_dclk = 40; /*1024*600@60*/
					/* normal qa and  B/C ic */
					if (qa_val == 1 && ic_ver == 0) {
						*min_dclk = 64; /*1280*720@60*/
					}
					/*ic version after e*/
					if (ic_ver >= 4)
						*min_dclk = 0;
				}

				*max_dclk = 200; /*1280*720@60*/
			} break;
		case 0x0400: /*A100*/
		{
			if (panel->lcd_if == LCD_IF_DSI) {
				if (qa_val == 0)
					*min_dclk = 40; /*1024*600@60*/
				/* normal qa and  B/C ic */
				if (qa_val == 1 && ic_ver == 0) {
					*min_dclk = 64; /*1280*720@60*/
				}
				/*ic version after e*/
				if (ic_ver >= 4)
					*min_dclk = 0;
			}

			*max_dclk = 200; /*1280*720@60*/
		} break;
		case 0x1400: /*A133*/
		{
			if (panel->lcd_if == LCD_IF_DSI) {
				if ((qa_val == 0 && ic_ver == 0) ||
				    (qa_val == 1 && ic_ver == 3)) {
					/*D 01 or B/C 00*/
					*min_dclk = 40;
				} else if (qa_val == 1 && ic_ver == 0) {
					/*B/C 01*/
					*min_dclk = 64; /*1280*720@60*/
				}
				/*ic version after e*/
				if (ic_ver >= 4)
					*min_dclk = 0;
			}
			*max_dclk = 200; /*1920*1200@60*/
		} break;
		case 0x1000: /*R818/MR813*/
		case 0x2000: {
			/*not bad ic*/
			*max_dclk = 200; /*1920*1200@60*/
		} break;
		case 0x0800: { /*T509*/
			if (panel->lcd_if == LCD_IF_DSI) {
				if (qa_val == 0 && ic_ver == 0)
					*min_dclk = 40;/*1024*600@60*/
				else
					*min_dclk = 0;
			}
			*max_dclk = 200; /*1920*1200@60*/
		} break;
		case 0x4000: { /*not support*/
			*min_dclk = 9999;
			*max_dclk = 0;
		}
		break;
		default:
			break;
		}
	}
#endif

#if defined(CONFIG_ARCH_SUN50IW10)
OUT:
	iounmap(qa_addr);
	iounmap(ic_ver_addr);
	iounmap(disp_cfg_addr);
#endif

	/*unlimit */
	return 0;
}

static s32 disp_lcd_tcon_enable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	u32 min_dclk = 0, max_dclk = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	disp_lcd_speed_limit(&lcdp->panel_info, &min_dclk, &max_dclk);

	if (lcdp->panel_info.lcd_dclk_freq < min_dclk ||
	    lcdp->panel_info.lcd_dclk_freq > max_dclk) {
		return 0;
	}

	return disp_al_lcd_enable(lcd->hwdev_index, &lcdp->panel_info);
}

s32 disp_lcd_tcon_disable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	return disp_al_lcd_disable(lcd->hwdev_index, &lcdp->panel_info);
}

static s32 disp_lcd_pin_cfg(struct disp_device *lcd, u32 bon)
{
	int i, ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	char dev_name[25];

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	DE_INF("lcd %d pin config, state %s, %d\n", lcd->disp,
	       (bon) ? "on" : "off", bon);

	/* io-pad */
	if (bon == 1) {
		for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
			ret = disp_sys_power_enable(lcdp->lcd_cfg.pin_regulator[i]);
			if (ret)
				return DIS_FAIL;
		}
	}

	sprintf(dev_name, "lcd%d", lcd->disp);
	disp_sys_pin_set_state(dev_name,
			       (bon == 1) ?
			       DISP_PIN_STATE_ACTIVE : DISP_PIN_STATE_SLEEP);

	disp_al_lcd_io_cfg(lcd->hwdev_index, bon, &lcdp->panel_info);

	if (bon)
		return DIS_SUCCESS;

	for (i = LCD_GPIO_REGU_NUM - 1; i >= 0; i--) {
		ret = disp_sys_power_disable(lcdp->lcd_cfg.pin_regulator[i]);
		if (ret)
			return DIS_FAIL;
	}

	return DIS_SUCCESS;
}

#if defined(CONFIG_FPGA_V4_PLATFORM) && defined(SUPPORT_EINK)
static s32 disp_lcd_pwm_enable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	unsigned long reg = 0;
	unsigned long val = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_used(lcd) && lcdp->pwm_info.dev) {
		reg = ((unsigned long)0xf1c20878);
		val = readl(reg);
		val = val & 0xfff0ffff;
		val = val | 0x10000;
		writel(val, reg);
		reg = ((unsigned long)0xf1c2087C);
		val = readl(reg);
		val = val & 0xefffffff;
		val = val | 0x10000000;
		writel(val, reg);
		return 0;
	}
	DE_WRN("pwm device hdl is NULL\n");
	return DIS_FAIL;
}
static s32 disp_lcd_pwm_disable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long reg = 0;
	unsigned long val = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_used(lcd) && lcdp->pwm_info.dev) {
		reg = ((unsigned long)0xf1c20878);
		val = readl(reg);
		val = val & 0xfff0ffff;
		val = val | 0x10000;
		writel(val, reg);
		reg = ((unsigned long)0xf1c2087C);
		val = readl(reg);
		val = val & 0xefffffff;
		val = val | 0x00000000;
		writel(val, reg);
		return 0;
	}
	DE_WRN("pwm device hdl is NULL\n");
	return DIS_FAIL;
}
#else
static s32 disp_lcd_pwm_enable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	if (lcdp->panel_info.lcd_pwm_used && !lcdp->pwm_info.dev) {
		lcdp->pwm_info.dev =
		    disp_sys_pwm_request(lcdp->panel_info.lcd_pwm_ch);
	}

	if (disp_lcd_is_used(lcd) && lcdp->pwm_info.dev)
		return disp_sys_pwm_enable(lcdp->pwm_info.dev);
	DE_WRN("pwm device hdl is NULL\n");

	return DIS_FAIL;
}

static s32 disp_lcd_pwm_disable(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	s32 ret = -1;
	struct pwm_device *pwm_dev;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	if (disp_lcd_is_used(lcd) && lcdp->pwm_info.dev) {
		ret = disp_sys_pwm_disable(lcdp->pwm_info.dev);
		pwm_dev = (struct pwm_device *)lcdp->pwm_info.dev;
		/*following is for reset pwm state purpose*/
		disp_sys_pwm_config(lcdp->pwm_info.dev,
				    pwm_dev->state.duty_cycle - 1,
				    pwm_dev->state.period);
		disp_sys_pwm_set_polarity(lcdp->pwm_info.dev,
					  !lcdp->pwm_info.polarity);

		disp_sys_pwm_free(lcdp->pwm_info.dev);
		lcdp->pwm_info.dev = 0;

		return ret;
	}
	DE_WRN("pwm device hdl is NULL\n");

	return DIS_FAIL;
}
#endif

static s32 disp_lcd_backlight_enable(struct disp_device *lcd)
{
	int ret;
	struct disp_gpio_info gpio_info;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	if (lcdp->bl_enabled) {
		spin_unlock_irqrestore(&lcd_data_lock, flags);
		return -EBUSY;
	}

	lcdp->bl_need_enabled = 1;
	lcdp->bl_enabled = true;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	if (disp_lcd_is_used(lcd)) {
		unsigned bl;

		if (lcdp->lcd_cfg.lcd_bl_en_used) {
			/* io-pad */
			ret = disp_sys_power_enable(lcdp->lcd_cfg.bl_regulator);
			if (ret)
				return DIS_FAIL;

			memcpy(&gpio_info, &(lcdp->lcd_cfg.lcd_bl_en),
			       sizeof(struct disp_gpio_info));

			disp_sys_gpio_request(&gpio_info);
		}
		bl = disp_lcd_get_bright(lcd);
		disp_lcd_set_bright(lcd, bl);
	}

	return 0;
}

static s32 disp_lcd_backlight_disable(struct disp_device *lcd)
{
	int ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	if (!lcdp->bl_enabled) {
		spin_unlock_irqrestore(&lcd_data_lock, flags);
		return -EBUSY;
	}

	lcdp->bl_enabled = false;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	if (!disp_lcd_is_used(lcd))
		return DIS_SUCCESS;

	if (!lcdp->lcd_cfg.lcd_bl_en_used)
		return DIS_SUCCESS;

	ret = disp_sys_power_disable(lcdp->lcd_cfg.bl_regulator);
	if (ret)
		return DIS_FAIL;

	return DIS_SUCCESS;
}

#if defined(CONFIG_FPGA_V4_PLATFORM) && defined(SUPPORT_EINK)
static s32 disp_lcd_power_enable(struct disp_device *lcd, u32 power_id)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long reg = 0;
	unsigned long val = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_used(lcd)) {
		reg = ((unsigned long)0xf1c20878);
		val = readl(reg);
		val = val & 0x000fffff;
		val = val | 0x11100000;
		writel(val, reg);
		reg = ((unsigned long)0xf1c2087C);
		val = readl(reg);
		val = val & 0x1fffffff;
		val = val | 0xe0000000;
		writel(val, reg);
		return 0;
	}
	return DIS_FAIL;
}
static s32 disp_lcd_power_disable(struct disp_device *lcd, u32 power_id)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long reg = 0;
	unsigned long val = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_used(lcd)) {
		reg = ((unsigned long)0xf1c20878);
		val = readl(reg);
		val = val & 0x000fffff;
		val = val | 0x77700000;
		writel(val, reg);
		return 0;
	}
	return DIS_FAIL;
}
#else
static s32 disp_lcd_power_enable(struct disp_device *lcd, u32 power_id)
{
	int ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if (!disp_lcd_is_used(lcd))
		return DIS_SUCCESS;

	if (!lcdp->lcd_cfg.regulator[power_id])
		return DIS_SUCCESS;

	ret = disp_sys_power_enable(lcdp->lcd_cfg.regulator[power_id]);
	if (ret)
		return DIS_FAIL;

	return DIS_SUCCESS;
}

static s32 disp_lcd_power_disable(struct disp_device *lcd, u32 power_id)
{
	int ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	ret = disp_sys_power_disable(lcdp->lcd_cfg.regulator[power_id]);
	if (ret)
		return DIS_FAIL;

	return DIS_SUCCESS;
}
#endif

static s32 disp_lcd_bright_get_adjust_value(struct disp_device *lcd, u32 bright)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	bright = (bright > 255) ? 255 : bright;
	return lcdp->panel_extend_info.lcd_bright_curve_tbl[bright];
}

static s32 disp_lcd_bright_curve_init(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	u32 i = 0, j = 0;
	u32 items = 0;
	u32 lcd_bright_curve_tbl[101][2];

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	for (i = 0; i < 101; i++) {
		if (lcdp->lcd_cfg.backlight_curve_adjust[i] == 0) {
			if (i == 0) {
				lcd_bright_curve_tbl[items][0] = 0;
				lcd_bright_curve_tbl[items][1] = 0;
				items++;
			}
		} else {
			lcd_bright_curve_tbl[items][0] = 255 * i / 100;
			lcd_bright_curve_tbl[items][1] =
			    lcdp->lcd_cfg.backlight_curve_adjust[i];
			items++;
		}
	}

	for (i = 0; i < items - 1; i++) {
		u32 num =
		    lcd_bright_curve_tbl[i + 1][0] - lcd_bright_curve_tbl[i][0];

		for (j = 0; j < num; j++) {
			u32 value = 0;

			value =
			    lcd_bright_curve_tbl[i][1] +
			    ((lcd_bright_curve_tbl[i + 1][1] -
			      lcd_bright_curve_tbl[i][1]) * j) / num;
			lcdp->panel_extend_info.
			    lcd_bright_curve_tbl[lcd_bright_curve_tbl[i][0] +
						 j] = value;
		}
	}
	lcdp->panel_extend_info.lcd_bright_curve_tbl[255] =
	    lcd_bright_curve_tbl[items - 1][1];

	return 0;
}

s32 disp_lcd_set_bright(struct disp_device *lcd, u32 bright)
{
	u32 duty_ns;
	__u64 backlight_bright = bright;
	__u64 backlight_dimming;
	__u64 period_ns;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;
	bool bright_update = false;
	struct disp_manager *mgr = NULL;
	struct disp_smbl *smbl = NULL;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	smbl = mgr->smbl;

	spin_lock_irqsave(&lcd_data_lock, flags);
	backlight_bright = (backlight_bright > 255) ? 255 : backlight_bright;
	if (lcdp->lcd_cfg.backlight_bright != backlight_bright) {
		bright_update = true;
		lcdp->lcd_cfg.backlight_bright = backlight_bright;
	}
	spin_unlock_irqrestore(&lcd_data_lock, flags);
	if (bright_update && smbl)
		smbl->update_backlight(smbl, backlight_bright);

	if (lcdp->pwm_info.dev) {
		if (backlight_bright != 0)
			backlight_bright += 1;
		backlight_bright =
		    disp_lcd_bright_get_adjust_value(lcd, backlight_bright);

		lcdp->lcd_cfg.backlight_dimming =
		    (lcdp->lcd_cfg.backlight_dimming ==
		     0) ? 256 : lcdp->lcd_cfg.backlight_dimming;
		backlight_dimming = lcdp->lcd_cfg.backlight_dimming;
		period_ns = lcdp->pwm_info.period_ns;
		duty_ns =
		    (backlight_bright * backlight_dimming * period_ns / 256 +
		     128) / 256;
		lcdp->pwm_info.duty_ns = duty_ns;
		disp_sys_pwm_config(lcdp->pwm_info.dev, duty_ns, period_ns);
	}

	if (lcdp->lcd_panel_fun.set_bright && lcdp->enabled) {
		lcdp->lcd_panel_fun.set_bright(lcd->disp,
					       disp_lcd_bright_get_adjust_value
					       (lcd, bright));
	}

	return DIS_SUCCESS;
}

s32 disp_lcd_get_bright(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	return lcdp->lcd_cfg.backlight_bright;
}

static s32 disp_lcd_set_bright_dimming(struct disp_device *lcd, u32 dimming)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	u32 bl = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	dimming = dimming > 256 ? 256 : dimming;
	lcdp->lcd_cfg.backlight_dimming = dimming;
	bl = disp_lcd_get_bright(lcd);
	disp_lcd_set_bright(lcd, bl);

	return DIS_SUCCESS;
}

static s32 disp_lcd_get_panel_info(struct disp_device *lcd,
				   struct disp_panel_para *info)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	memcpy(info, (struct disp_panel_para *) (&(lcdp->panel_info)),
	       sizeof(struct disp_panel_para));
	return 0;
}

#if defined(__LINUX_PLAT__)
static irqreturn_t disp_lcd_event_proc(int irq, void *parg)
#else
static irqreturn_t disp_lcd_event_proc(void *parg)
#endif
{
	struct disp_device *lcd = (struct disp_device *)parg;
	struct disp_lcd_private_data *lcdp = NULL;
	struct disp_manager *mgr = NULL;
#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)
	struct disp_eink_manager *eink_manager = NULL;
#endif
	u32 hwdev_index;
	u32 irq_flag = 0;
	unsigned int panel_extend_dirty;
	unsigned long flags;

	if (lcd == NULL)
		return DISP_IRQ_RETURN;

	hwdev_index = lcd->hwdev_index;
	lcdp = disp_lcd_get_priv(lcd);

	if (lcdp == NULL)
		return DISP_IRQ_RETURN;

#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)
	eink_manager = disp_get_eink_manager(0);
	if (eink_manager == NULL)
		return DISP_IRQ_RETURN;
#endif

	if (disp_al_lcd_query_irq
	    (hwdev_index, LCD_IRQ_TCON0_VBLK, &lcdp->panel_info)) {
#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)

		eink_display_one_frame(eink_manager);
#else
		int cur_line =
		    disp_al_lcd_get_cur_line(hwdev_index, &lcdp->panel_info);
		int start_delay =
		    disp_al_lcd_get_start_delay(hwdev_index, &lcdp->panel_info);
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
		if (lcdp->lcd_panel_fun.esd_check &&
		    lcdp->lcd_panel_fun.reset_panel) {
			++lcdp->esd_inf.cnt;
			if (cur_line < 2 &&
			    !atomic_read(&lcdp->lcd_resetting) &&
			    lcdp->esd_inf.cnt >= lcdp->esd_inf.freq) {
				if (!lcdp->esd_inf.esd_check_func_pos ||
				    lcdp->lcd_panel_fun.esd_check(lcd->disp)) {
					/*request reset*/
					atomic_set(&lcdp->lcd_resetting, 1);
					schedule_work(&lcdp->reflush_work);
				}
				lcdp->esd_inf.cnt = 0;
			}
		}
#endif

		mgr = lcd->manager;
		if (mgr == NULL)
			return DISP_IRQ_RETURN;

		if (cur_line <= (start_delay - lcdp->judge_line))
			sync_event_proc(mgr->disp, false);
		else
			sync_event_proc(mgr->disp, true);
#endif
	} else {
		irq_flag = disp_al_lcd_query_irq(hwdev_index, LCD_IRQ_TCON0_CNTR,
						&lcdp->panel_info);
		irq_flag |=
		    disp_al_lcd_query_irq(hwdev_index, LCD_IRQ_TCON0_TRIF,
					  &lcdp->panel_info);

		if (irq_flag == 0)
			goto exit;

		if (disp_al_lcd_tri_busy(hwdev_index, &lcdp->panel_info)) {
			/* if lcd is still busy when tri/cnt irq coming,
			 * take it as failture, record failture times,
			 * when it reach 2 times, clear counter
			 */
			lcdp->tri_finish_fail++;
			lcdp->tri_finish_fail = (lcdp->tri_finish_fail == 2) ?
			    0 : lcdp->tri_finish_fail;
		} else
			lcdp->tri_finish_fail = 0;

		mgr = lcd->manager;
		if (mgr == NULL)
			return DISP_IRQ_RETURN;

#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
		if (lcdp->lcd_panel_fun.esd_check &&
		    lcdp->lcd_panel_fun.reset_panel) {
			++lcdp->esd_inf.cnt;
			if (!atomic_read(&lcdp->lcd_resetting) &&
			    lcdp->esd_inf.cnt >= lcdp->esd_inf.freq) {
				if (!lcdp->esd_inf.esd_check_func_pos ||
				    lcdp->lcd_panel_fun.esd_check(lcd->disp)) {
					/*request reset*/
					atomic_set(&lcdp->lcd_resetting, 1);
					schedule_work(&lcdp->reflush_work);
				}
				lcdp->esd_inf.cnt = 0;
			}
		}
#endif

		if (lcdp->tri_finish_fail == 0) {
			sync_event_proc(mgr->disp, false);
			disp_al_lcd_tri_start(hwdev_index, &lcdp->panel_info);
		} else
			sync_event_proc(mgr->disp, true);
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	panel_extend_dirty = lcdp->panel_extend_dirty;
	lcdp->panel_extend_dirty = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);
	if (panel_extend_dirty == 1)
		disp_al_lcd_cfg_ext(lcd->disp, &lcdp->panel_extend_info_set);

exit:
	return DISP_IRQ_RETURN;
}

#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
static void disp_lcd_reflush_work(struct work_struct *work)
{
	struct disp_lcd_private_data *lcdp =
	    container_of(work, struct disp_lcd_private_data, reflush_work);
	struct disp_device *lcd = disp_device_get_from_priv((void *)lcdp);
	unsigned long flags;

	if (!lcdp || !lcd) {
		DE_WRN("lcdp is null\n");
		return;
	}

	/*lcd is not enabled or is enabling*/
	if (disp_lcd_is_enabled(lcd) == 0 || lcdp->enabling == 1)
		return;

	/*lcd is resetting*/
	if (atomic_read(&lcdp->lcd_resetting) == 2)
		return;

	if (!lcdp->esd_inf.esd_check_func_pos)
		if (lcdp->lcd_panel_fun.esd_check)
			if (!lcdp->lcd_panel_fun.esd_check(lcd->disp)) {
				atomic_set(&lcdp->lcd_resetting, 0);
				return; /*everything is just fine*/
			}

	if (lcdp->esd_inf.level == 1) {
		spin_lock_irqsave(&lcd_data_lock, flags);
		lcdp->enabled = 0;
		lcdp->enabling = 1;
		spin_unlock_irqrestore(&lcd_data_lock, flags);

		atomic_set(&lcdp->lcd_resetting, 2);

		disp_lcd_tcon_disable(lcd);
		disp_al_lcd_cfg(lcd->hwdev_index, &lcdp->panel_info,
				&lcdp->panel_extend_info_set);
	} else
		atomic_set(&lcdp->lcd_resetting, 2);


	++lcdp->esd_inf.rst_cnt;
	if (lcdp->lcd_panel_fun.reset_panel)
		lcdp->lcd_panel_fun.reset_panel(lcd->disp);

	if (lcdp->esd_inf.level == 1) {
		disp_lcd_tcon_enable(lcd);

		spin_lock_irqsave(&lcd_data_lock, flags);
		lcdp->enabled = 1;
		lcdp->enabling = 0;
		spin_unlock_irqrestore(&lcd_data_lock, flags);
	}

	disp_delay_ms(300);

	/*lcd reset finish*/
	atomic_set(&lcdp->lcd_resetting, 0);
}
#endif

static s32 disp_lcd_cal_fps(struct disp_device *lcd)
{
	s32 ret = -1;
	struct disp_lcd_private_data *lcdp = NULL;
	struct disp_panel_para *panel_info = NULL;

	if (!lcd)
		 goto OUT;
	lcdp = disp_lcd_get_priv(lcd);
	if (!lcdp)
		goto OUT;
	panel_info = &lcdp->panel_info;


	lcdp->frame_per_sec =
	    DIV_ROUND_CLOSEST(panel_info->lcd_dclk_freq * 1000000 *
				  (panel_info->lcd_interlace + 1),
			      panel_info->lcd_ht * panel_info->lcd_vt);

	ret = 0;
OUT:
	return ret;
}

/* lcd enable except for backlight */
static s32 disp_lcd_fake_enable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i, ret;
	struct disp_manager *mgr = NULL;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	DE_INF("lcd %d\n", lcd->disp);
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_enabled(lcd) == 1)
		return 0;

	disp_lcd_cal_fps(lcd);
	if (mgr->enable)
		mgr->enable(mgr);

#if defined(SUPPORT_DSI) && defined(DSI_VERSION_40)
	if ((lcdp->panel_info.lcd_if == LCD_IF_DSI) &&
	    (lcdp->irq_no_dsi != 0)) {
		if (lcdp->panel_info.lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
			disp_sys_register_irq(lcdp->irq_no, 0,
					      disp_lcd_event_proc, (void *)lcd,
					      0, 0);
			disp_sys_enable_irq(lcdp->irq_no);
		} else {
			disp_sys_register_irq(lcdp->irq_no_dsi, 0,
					      disp_lcd_event_proc, (void *)lcd,
					      0, 0);
			disp_sys_enable_irq(lcdp->irq_no_dsi);
		}
	} else
#endif
	{
		disp_sys_register_irq(lcdp->irq_no, 0, disp_lcd_event_proc,
				      (void *)lcd, 0, 0);
		disp_sys_enable_irq(lcdp->irq_no);
	}
	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabling = 1;
	lcdp->bl_need_enabled = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);
	disp_lcd_gpio_init(lcd);
	lcd_clk_enable(lcd);
	ret = cal_real_frame_period(lcd);
	if (ret)
		DE_WRN("cal_real_frame_period fail:%d\n", ret);
	if (lcdp->panel_info.lcd_pwm_used && !lcdp->pwm_info.dev)
		lcdp->pwm_info.dev =
			disp_sys_pwm_request(lcdp->panel_info.lcd_pwm_ch);
	disp_sys_pwm_config(lcdp->pwm_info.dev, lcdp->pwm_info.duty_ns,
			    lcdp->pwm_info.period_ns);
	disp_sys_pwm_set_polarity(lcdp->pwm_info.dev, lcdp->pwm_info.polarity);
	disp_al_lcd_cfg(lcd->hwdev_index, &lcdp->panel_info,
		&lcdp->panel_extend_info_set);
	lcdp->open_flow.func_num = 0;
	if (lcdp->lcd_panel_fun.cfg_open_flow)
		lcdp->lcd_panel_fun.cfg_open_flow(lcd->disp);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_open_flow is NULL\n", lcd->disp);

	for (i = 0; i < lcdp->open_flow.func_num - 1; i++) {
		if (lcdp->open_flow.func[i].func) {
			lcdp->open_flow.func[i].func(lcd->disp);
			DE_INF("open flow:step %d finish, to delay %d\n", i,
			       lcdp->open_flow.func[i].delay);
			if (lcdp->open_flow.func[i].delay != 0)
				disp_delay_ms(lcdp->open_flow.func[i].delay);
		}
	}
	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 1;
	lcdp->enabling = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
	atomic_set(&lcdp->lcd_resetting, 0);
#endif

	return 0;
}

static int skip_open_backlight;
#ifndef MODULE
static int __init early_bootreason(char *str)
{
	skip_open_backlight = 0;
	if (!str) {
		skip_open_backlight = 0;
		goto OUT;
	}

	if (parse_option_str("irq", str) == true) {
		skip_open_backlight = 1;
		pr_warn("get irq bootreason:%s\n", str);
	} else {
		skip_open_backlight = 0;
	}

OUT:
	return 0;
}
early_param("bootreason", early_bootreason);
#endif

#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)
static s32 disp_lcd_enable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct disp_manager *mgr = NULL;
	int ret;
	int i;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	flush_work(&lcd->close_eink_panel_work);
	DE_INF("lcd %d\n", lcd->disp);
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_enabled(lcd) == 1)
		return 0;

	disp_sys_register_irq(lcdp->irq_no, 0, disp_lcd_event_proc, (void *)lcd,
			      0, 0);
	disp_sys_enable_irq(lcdp->irq_no);

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabling = 1;
	lcdp->bl_need_enabled = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);
	if (lcdp->lcd_panel_fun.cfg_panel_info)
		lcdp->lcd_panel_fun.cfg_panel_info(&lcdp->panel_extend_info);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_panel_info is NULL\n", lcd->disp);

	disp_lcd_gpio_init(lcd);
	ret = lcd_clk_enable(lcd);
	if (ret != 0)
		return DIS_FAIL;
	ret = cal_real_frame_period(lcd);
	if (ret)
		DE_WRN("cal_real_frame_period fail:%d\n", ret);

	disp_al_lcd_cfg(lcd->hwdev_index, &lcdp->panel_info,
			&lcdp->panel_extend_info);/* init tcon_lcd regs */
	lcdp->open_flow.func_num = 0;
	if (lcdp->lcd_panel_fun.cfg_open_flow)
		lcdp->lcd_panel_fun.cfg_open_flow(lcd->disp);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_open_flow is NULL\n", lcd->disp);

	for (i = 0; i < lcdp->open_flow.func_num; i++) {
		if (lcdp->open_flow.func[i].func) {
			lcdp->open_flow.func[i].func(lcd->disp);
			DE_INF("open flow:step %d finish, to delay %d\n", i,
			       lcdp->open_flow.func[i].delay);
			if (lcdp->open_flow.func[i].delay != 0)
				disp_delay_ms(lcdp->open_flow.func[i].delay);
		}
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 1;
	lcdp->enabling = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return 0;
}

static s32 disp_lcd_disable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct disp_manager *mgr = NULL;
	int i;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	DE_INF("lcd %d\n", lcd->disp);
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_enabled(lcd) == 0)
		return 0;

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	lcdp->bl_need_enabled = 0;
	lcdp->close_flow.func_num = 0;
	if (lcdp->lcd_panel_fun.cfg_close_flow)
		lcdp->lcd_panel_fun.cfg_close_flow(lcd->disp);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_close_flow is NULL\n", lcd->disp);

	for (i = 0; i < lcdp->close_flow.func_num; i++) {
		if (lcdp->close_flow.func[i].func) {
			lcdp->close_flow.func[i].func(lcd->disp);
			DE_INF("close flow:step %d finish, to delay %d\n", i,
			       lcdp->close_flow.func[i].delay);
			if (lcdp->close_flow.func[i].delay != 0)
				disp_delay_ms(lcdp->close_flow.func[i].delay);
		}
	}

	lcd_clk_disable(lcd);
	disp_lcd_gpio_exit(lcd);

	disp_sys_disable_irq(lcdp->irq_no);
	disp_sys_unregister_irq(lcdp->irq_no, disp_lcd_event_proc, (void *)lcd);

	return 0;
}

#else
static s32 disp_lcd_enable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i;
	struct disp_manager *mgr = NULL;
	unsigned bl;
	int ret;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	__inf("lcd %d\n", lcd->disp);
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_enabled(lcd) == 1)
		return 0;

	disp_lcd_cal_fps(lcd);
	if (mgr->enable)
		mgr->enable(mgr);

#if defined(SUPPORT_DSI) && defined(DSI_VERSION_40)
	if ((lcdp->panel_info.lcd_if == LCD_IF_DSI)
	    && (lcdp->irq_no_dsi != 0)) {
		if (lcdp->panel_info.lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
			disp_sys_register_irq(lcdp->irq_no, 0, disp_lcd_event_proc,
					      (void *)lcd, 0, 0);
			disp_sys_enable_irq(lcdp->irq_no);
		} else {
			disp_sys_register_irq(lcdp->irq_no_dsi, 0, disp_lcd_event_proc,
					      (void *)lcd, 0, 0);
			disp_sys_enable_irq(lcdp->irq_no_dsi);
		}
	} else
#endif
	{
		disp_sys_register_irq(lcdp->irq_no, 0, disp_lcd_event_proc,
				      (void *)lcd, 0, 0);
		disp_sys_enable_irq(lcdp->irq_no);
	}
	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabling = 1;
	lcdp->bl_need_enabled = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	lcdp->panel_extend_info.lcd_gamma_en = lcdp->panel_info.lcd_gamma_en;
	disp_lcd_gpio_init(lcd);
	ret = lcd_clk_enable(lcd);
	if (ret != 0)
		return DIS_FAIL;
	ret = cal_real_frame_period(lcd);
	if (ret)
		DE_WRN("cal_real_frame_period fail:%d\n", ret);

	if (lcdp->panel_info.lcd_pwm_used && !lcdp->pwm_info.dev)
		lcdp->pwm_info.dev =
			disp_sys_pwm_request(lcdp->panel_info.lcd_pwm_ch);

	disp_sys_pwm_config(lcdp->pwm_info.dev, lcdp->pwm_info.duty_ns,
			    lcdp->pwm_info.period_ns);
	disp_sys_pwm_set_polarity(lcdp->pwm_info.dev, lcdp->pwm_info.polarity);
	disp_al_lcd_cfg(lcd->hwdev_index, &lcdp->panel_info,
		&lcdp->panel_extend_info_set);
	lcd_calc_judge_line(lcd);
	lcdp->open_flow.func_num = 0;
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
	if (lcdp->lcd_panel_fun.set_esd_info) {
		lcdp->lcd_panel_fun.set_esd_info(&lcdp->esd_inf);
	} else {
		/*default value*/
		lcdp->esd_inf.level = 0;
		lcdp->esd_inf.freq = 60;
		lcdp->esd_inf.esd_check_func_pos = 0;
		lcdp->esd_inf.cnt = 0;
	}
#endif
	if (lcdp->lcd_panel_fun.cfg_open_flow)
		lcdp->lcd_panel_fun.cfg_open_flow(lcd->disp);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_open_flow is NULL\n", lcd->disp);

	if (lcdp->panel_info.lcd_gsensor_detect == 0)
		skip_open_backlight = 0;

	for (i = 0; i < lcdp->open_flow.func_num - skip_open_backlight; i++) {
		if (lcdp->open_flow.func[i].func) {
			lcdp->open_flow.func[i].func(lcd->disp);
			DE_INF("open flow:step %d finish, to delay %d\n", i,
			       lcdp->open_flow.func[i].delay);
			if (lcdp->open_flow.func[i].delay != 0)
				disp_delay_ms(lcdp->open_flow.func[i].delay);
		}
	}
	skip_open_backlight = 0; /*only skip one time*/

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 1;
	lcdp->enabling = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);
	bl = disp_lcd_get_bright(lcd);
	disp_lcd_set_bright(lcd, bl);
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
	atomic_set(&lcdp->lcd_resetting, 0);
#endif

	return 0;
}

static s32 disp_lcd_disable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct disp_manager *mgr = NULL;
	int i;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}
	DE_INF("lcd %d\n", lcd->disp);
	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	if (disp_lcd_is_enabled(lcd) == 0)
		return 0;

#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
	atomic_set(&lcdp->lcd_resetting, 2);
#endif
	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 0;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	lcdp->bl_need_enabled = 0;
	lcdp->close_flow.func_num = 0;
	if (lcdp->lcd_panel_fun.cfg_close_flow)
		lcdp->lcd_panel_fun.cfg_close_flow(lcd->disp);
	else
		DE_WRN("lcd_panel_fun[%d].cfg_close_flow is NULL\n", lcd->disp);

	for (i = 0; i < lcdp->close_flow.func_num; i++) {
		if (lcdp->close_flow.func[i].func) {
			lcdp->close_flow.func[i].func(lcd->disp);
			DE_INF("close flow:step %d finish, to delay %d\n", i,
			       lcdp->close_flow.func[i].delay);
			if (lcdp->close_flow.func[i].delay != 0)
				disp_delay_ms(lcdp->close_flow.func[i].delay);
		}
	}

	disp_lcd_gpio_exit(lcd);

	lcd_clk_disable(lcd);
#if defined(SUPPORT_DSI) && defined(DSI_VERSION_40)
	if ((lcdp->panel_info.lcd_if == LCD_IF_DSI) &&
	    (lcdp->irq_no_dsi != 0)) {
		if (lcdp->panel_info.lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
			disp_sys_disable_irq(lcdp->irq_no);
			disp_sys_unregister_irq(
			    lcdp->irq_no, disp_lcd_event_proc, (void *)lcd);
		} else {
			disp_sys_disable_irq(lcdp->irq_no_dsi);
			disp_sys_unregister_irq(
			    lcdp->irq_no_dsi, disp_lcd_event_proc, (void *)lcd);
		}
	} else
#endif
	{
		disp_sys_disable_irq(lcdp->irq_no);
		disp_sys_unregister_irq(lcdp->irq_no, disp_lcd_event_proc,
					(void *)lcd);
	}

	if (mgr->disable)
		mgr->disable(mgr);

	return 0;
}
#endif
static s32 disp_lcd_sw_enable(struct disp_device *lcd)
{
	unsigned long flags;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i, ret;
	struct disp_manager *mgr = NULL;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	mgr = lcd->manager;
	if (mgr == NULL) {
		DE_WRN("mgr is NULL!\n");
		return DIS_FAIL;
	}
	disp_lcd_cal_fps(lcd);
	lcd_calc_judge_line(lcd);
	if (mgr->sw_enable)
		mgr->sw_enable(mgr);

#if !defined(CONFIG_COMMON_CLK_ENABLE_SYNCBOOT)
	if (lcd_clk_enable(lcd) != 0)
		return DIS_FAIL;
#endif
	ret = cal_real_frame_period(lcd);
	if (ret)
		DE_WRN("cal_real_frame_period fail:%d\n", ret);

	/* init lcd power */
	for (i = 0; i < LCD_POWER_NUM; i++) {
		ret = disp_sys_power_enable(lcdp->lcd_cfg.regulator[i]);
		if (ret)
			return DIS_FAIL;
	}

	/* init gpio */
	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		ret = disp_sys_power_enable(lcdp->lcd_cfg.gpio_regulator[i]);
		if (ret)
			return DIS_FAIL;
	}

	/* init lcd pin */
	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		ret = disp_sys_power_enable(lcdp->lcd_cfg.pin_regulator[i]);
		if (ret)
			return DIS_FAIL;
	}

	/* init bl */
	if (lcdp->lcd_cfg.lcd_bl_en_used) {
		ret = disp_sys_power_enable(lcdp->lcd_cfg.bl_regulator);
		if (ret)
			return DIS_FAIL;
		disp_sys_gpio_request(&lcdp->lcd_cfg.lcd_bl_en);
	}

	if (lcdp->panel_info.lcd_pwm_used) {
		if (!lcdp->pwm_info.dev) {
			lcdp->pwm_info.dev = disp_sys_pwm_request(
				lcdp->panel_info.lcd_pwm_ch);
		}
		if (lcdp->pwm_info.dev) {
			disp_sys_pwm_config(lcdp->pwm_info.dev,
					    lcdp->pwm_info.duty_ns,
					    lcdp->pwm_info.period_ns);
			disp_sys_pwm_set_polarity(lcdp->pwm_info.dev,
						  lcdp->pwm_info.polarity);
			disp_sys_pwm_enable(lcdp->pwm_info.dev);
		}
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->enabled = 1;
	lcdp->enabling = 0;
	lcdp->bl_need_enabled = 1;
	lcdp->bl_enabled = true;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
	if (lcdp->lcd_panel_fun.set_esd_info) {
		lcdp->lcd_panel_fun.set_esd_info(&lcdp->esd_inf);
	} else {
		/*default value*/
		lcdp->esd_inf.level = 0;
		lcdp->esd_inf.freq = 60;
		lcdp->esd_inf.esd_check_func_pos = 0;
		lcdp->esd_inf.cnt = 0;
	}
#endif
	disp_al_lcd_disable_irq(lcd->hwdev_index, LCD_IRQ_TCON0_VBLK,
				&lcdp->panel_info);
#if defined(SUPPORT_DSI) && defined(DSI_VERSION_40)
	if ((lcdp->panel_info.lcd_if == LCD_IF_DSI) &&
	    (lcdp->irq_no_dsi != 0)) {
		if (lcdp->panel_info.lcd_dsi_if == LCD_DSI_IF_COMMAND_MODE) {
			disp_sys_register_irq(lcdp->irq_no, 0,
					      disp_lcd_event_proc, (void *)lcd,
					      0, 0);
			disp_sys_enable_irq(lcdp->irq_no);
		} else {
			disp_sys_register_irq(lcdp->irq_no_dsi, 0,
					      disp_lcd_event_proc, (void *)lcd,
					      0, 0);
			disp_sys_enable_irq(lcdp->irq_no_dsi);
		}
	} else
#endif
	{
		disp_sys_register_irq(lcdp->irq_no, 0, disp_lcd_event_proc,
				      (void *)lcd, 0, 0);
		disp_sys_enable_irq(lcdp->irq_no);
	}
	disp_al_lcd_enable_irq(lcd->hwdev_index, LCD_IRQ_TCON0_VBLK,
			       &lcdp->panel_info);

	return 0;
}

s32 disp_lcd_is_enabled(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	return (s32)lcdp->enabled;
}

/**
 * disp_lcd_check_if_enabled - check lcd if be enabled status
 *
 * this function only be used by bsp_disp_sync_with_hw to check
 * the device enabled status when driver init
 */
s32 disp_lcd_check_if_enabled(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int ret = 1;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

#if !defined(CONFIG_COMMON_CLK_ENABLE_SYNCBOOT)
	if (lcdp->clk_tcon_lcd &&
	   (__clk_is_enabled(lcdp->clk_tcon_lcd) == 0))
		ret = 0;
#endif

	return ret;
}

static s32 disp_lcd_set_open_func(struct disp_device *lcd, LCD_FUNC func,
				  u32 delay)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	if (func) {
		lcdp->open_flow.func[lcdp->open_flow.func_num].func = func;
		lcdp->open_flow.func[lcdp->open_flow.func_num].delay = delay;
		lcdp->open_flow.func_num++;
	}

	return DIS_SUCCESS;
}

static s32 disp_lcd_set_close_func(struct disp_device *lcd, LCD_FUNC func,
				   u32 delay)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	if (func) {
		lcdp->close_flow.func[lcdp->close_flow.func_num].func = func;
		lcdp->close_flow.func[lcdp->close_flow.func_num].delay = delay;
		lcdp->close_flow.func_num++;
	}

	return DIS_SUCCESS;
}

static s32 disp_lcd_set_panel_funs(struct disp_device *lcd, char *name,
				   struct disp_lcd_panel_fun *lcd_cfg)
{
	s32 ret = -1;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}


	DE_INF("lcd %d, driver_name %s,  panel_name %s\n", lcd->disp, lcdp->panel_info.lcd_driver_name,
	       name);
	if (!strcmp(lcdp->panel_info.lcd_driver_name, name)) {
		memset(&lcdp->lcd_panel_fun,
		       0,
		       sizeof(struct disp_lcd_panel_fun));
		lcdp->lcd_panel_fun.cfg_panel_info = lcd_cfg->cfg_panel_info;
		lcdp->lcd_panel_fun.cfg_open_flow = lcd_cfg->cfg_open_flow;
		lcdp->lcd_panel_fun.cfg_close_flow = lcd_cfg->cfg_close_flow;
		lcdp->lcd_panel_fun.esd_check = lcd_cfg->esd_check;
		lcdp->lcd_panel_fun.reset_panel = lcd_cfg->reset_panel;
		lcdp->lcd_panel_fun.set_esd_info = lcd_cfg->set_esd_info;
		lcdp->lcd_panel_fun.lcd_user_defined_func =
		    lcd_cfg->lcd_user_defined_func;
		lcdp->lcd_panel_fun.set_bright = lcd_cfg->set_bright;
		if (lcdp->lcd_panel_fun.cfg_panel_info) {
			lcdp->lcd_panel_fun.cfg_panel_info(&lcdp->panel_extend_info);
			memcpy(&lcdp->panel_extend_info_set,
				&lcdp->panel_extend_info, sizeof(struct panel_extend_para));
		} else {
			DE_WRN("lcd_panel_fun[%d].cfg_panel_info is NULL\n", lcd->disp);
		}

		ret = 0;
	}

	return ret;
}

s32 disp_lcd_gpio_init(struct disp_device *lcd)
{
	int ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	/* io-pad */
	for (i = 0; i < LCD_GPIO_REGU_NUM; i++) {
		ret = disp_sys_power_enable(lcdp->lcd_cfg.gpio_regulator[i]);
		if (ret)
			return DIS_FAIL;
	}


	return 0;
}

s32 disp_lcd_gpio_exit(struct disp_device *lcd)
{
	int ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}


	/* io-pad */
	for (i = LCD_GPIO_REGU_NUM - 1; i >= 0; i--) {
		ret = disp_sys_power_disable(lcdp->lcd_cfg.gpio_regulator[i]);
		if (ret)
			return DIS_FAIL;
	}

	return DIS_SUCCESS;
}

/* direction: input(0), output(1) */
s32 disp_lcd_gpio_set_direction(struct disp_device *lcd, u32 io_index,
				u32 direction)
{
	int gpio, value, ret;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	char gpio_name[20];
	struct device_node *node = g_disp_drv.node;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	sprintf(gpio_name, "lcd_gpio_%d", io_index);

	gpio = of_get_named_gpio(node, gpio_name, 0);
	if (!gpio_is_valid(gpio)) {
		DE_WRN("of_get_named_gpio_flags for %s failed\n", gpio_name);
		return DIS_FAIL;
	}

	if (direction) {
		value = __gpio_get_value(gpio);
		ret = gpio_direction_output(gpio, value);
		if (ret) {
			DE_WRN("gpio_direction_output for %s failed\n", gpio_name);
			return DIS_FAIL;
		}
	} else {
		ret = gpio_direction_input(gpio);
		if (ret) {
			DE_WRN("gpio_direction_input for %s failed\n", gpio_name);
			return DIS_FAIL;
		}
	}

	return DIS_SUCCESS;
}

s32 disp_lcd_gpio_get_value(struct disp_device *lcd, u32 io_index)
{
	int gpio;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	char gpio_name[20];
	struct device_node *node = g_disp_drv.node;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	sprintf(gpio_name, "lcd_gpio_%d", io_index);

	gpio = of_get_named_gpio(node, gpio_name, 0);
	if (!gpio_is_valid(gpio)) {
		DE_WRN("of_get_named_gpio_flags for %s failed\n", gpio_name);
		return DIS_FAIL;
	}

	return __gpio_get_value(gpio);
}

s32 disp_lcd_gpio_set_value(struct disp_device *lcd, u32 io_index, u32 data)
{
	int gpio;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	char gpio_name[20];

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	if (io_index >= LCD_GPIO_NUM) {
		DE_WRN("gpio num out of range\n");
		return DIS_FAIL;
	}
	sprintf(gpio_name, "lcd_gpio_%d", io_index);

	gpio = lcdp->lcd_cfg.lcd_gpio[io_index].gpio;
	if (!gpio_is_valid(gpio)) {
		DE_WRN("of_get_named_gpio_flags for %s failed\n", gpio_name);
		return DIS_FAIL;
	}

	__gpio_set_value(gpio, data);

	return DIS_SUCCESS;
}

static s32 disp_lcd_get_dimensions(struct disp_device *lcd, u32 *width,
				   u32 *height)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	*width = lcdp->panel_info.lcd_width;
	*height = lcdp->panel_info.lcd_height;
	return 0;
}

static s32 disp_lcd_get_status(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	return disp_al_lcd_get_status(lcd->hwdev_index, &lcdp->panel_info);
}

static bool disp_lcd_is_in_safe_period(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int start_delay;
	int cur_line;
	bool ret = true;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		goto exit;
	}

	start_delay =
	    disp_al_lcd_get_start_delay(lcd->hwdev_index, &lcdp->panel_info);
	cur_line =
	    disp_al_lcd_get_cur_line(lcd->hwdev_index, &lcdp->panel_info);
	if (cur_line >= start_delay)
		ret = false;

exit:
	return ret;
}

static s32 disp_lcd_update_gamma_tbl_set(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int i;
	unsigned int *gamma, *gamma_set;
	unsigned int r, g, b;
	s32 color_temperature;
	u32 color_inverse;

	color_temperature = lcdp->color_temperature;
	color_inverse = lcdp->color_inverse;
	memcpy(&lcdp->panel_extend_info_set, &lcdp->panel_extend_info,
		sizeof(struct panel_extend_para));
	gamma = lcdp->panel_extend_info.lcd_gamma_tbl;
	gamma_set = lcdp->panel_extend_info_set.lcd_gamma_tbl;
	if (color_temperature > 0) {
		/* warm color */
		for (i = 0; i < 256; i++) {
			r = (gamma[i] >> 16) & 0xff;
			g = (gamma[i] >> 8) & 0xff;
			b = gamma[i] & 0xff;

			g = g * (512 - color_temperature) / 512;
			b = b * (256 - color_temperature) / 256;
			r = r << 16;

			g = g << 8;
			gamma_set[i] = r | g | b;
		}
	} else if (color_temperature < 0) {
		/* cool color */
		for (i = 0; i < 256; i++) {
			r = (gamma[i] >> 16) & 0xff;
			g = (gamma[i] >> 8) & 0xff;
			b = gamma[i] & 0xff;

			r = r * (256 + color_temperature) / 256;
			g = g * (512 + color_temperature) / 512;

			r = r << 16;
			g = g << 8;

			gamma_set[i] = r | g | b;
		}
	}
	if (color_inverse == 1) {
		for (i = 0; i < 256; i++)
			gamma_set[i] = 0xffffffff -  gamma_set[i];
	}
	if (color_inverse != 0)
		lcdp->panel_extend_info_set.lcd_gamma_en = 1;
	if (color_temperature != 0)
		lcdp->panel_extend_info_set.lcd_gamma_en = 1;

	return 0;
}


static s32 disp_lcd_set_gamma_tbl(struct disp_device *lcd,
			unsigned int *gamma_table, unsigned int size)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((lcd == NULL) || (lcdp == NULL)
	    || (gamma_table == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	size = (size > LCD_GAMMA_TABLE_SIZE) ?
	    LCD_GAMMA_TABLE_SIZE : size;
	spin_lock_irqsave(&lcd_data_lock, flags);
	memcpy(lcdp->panel_extend_info.lcd_gamma_tbl, gamma_table, size);
	disp_lcd_update_gamma_tbl_set(lcd);
	lcdp->panel_extend_dirty = 1;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return 0;
}
static s32 disp_lcd_get_gamma_tbl(struct disp_device *lcd,
			unsigned int *gamma_table, unsigned int size)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((lcd == NULL) || (lcdp == NULL)
	    || (gamma_table == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	size = (size > LCD_GAMMA_TABLE_SIZE) ?
	    LCD_GAMMA_TABLE_SIZE : size;
	spin_lock_irqsave(&lcd_data_lock, flags);
	memcpy(gamma_table, lcdp->panel_extend_info.lcd_gamma_tbl, size);
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return 0;
}

static s32 disp_lcd_enable_gamma(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	if (lcdp->panel_extend_info.lcd_gamma_en == 0) {
		lcdp->panel_extend_info.lcd_gamma_en = 1;
		disp_lcd_update_gamma_tbl_set(lcd);
		lcdp->panel_extend_dirty = 1;
	}
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return 0;
}

static s32 disp_lcd_disable_gamma(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	int ret;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	if (lcdp->panel_extend_info.lcd_gamma_en == 1) {
		lcdp->panel_extend_info.lcd_gamma_en = 0;
		ret = disp_al_lcd_cfg_ext(lcd->disp,
					  &lcdp->panel_extend_info);
	} else {
		ret = 0;
	}

	return ret;
}

static s32 disp_lcd_get_fps(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	return lcdp->frame_per_sec;
}

static s32 disp_lcd_set_color_temperature(struct disp_device *lcd,
	s32 color_temperature)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;

	if ((NULL == lcd) || (NULL == lcdp)) {
		DE_WRN("NULL hdl!\n");
		return -1;
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	lcdp->color_temperature = color_temperature;
	disp_lcd_update_gamma_tbl_set(lcd);
	lcdp->panel_extend_dirty = 1;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return 0;
}

static s32 disp_lcd_get_color_temperature(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	unsigned long flags;
	s32 color_temperature = 0;

	if ((NULL == lcd) || (NULL == lcdp)) {
		DE_WRN("NULL hdl!\n");
		return 0;
	}

	spin_lock_irqsave(&lcd_data_lock, flags);
	color_temperature = lcdp->color_temperature;
	spin_unlock_irqrestore(&lcd_data_lock, flags);

	return color_temperature;
}

static s32 disp_lcd_init(struct disp_device *lcd, int lcd_index)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct disp_gpio_info gpio_info;
	int i;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	lcd_get_sys_config(lcd_index, &lcdp->lcd_cfg);

	lcd_regulator_get_wrap(&lcdp->lcd_cfg);

	if (disp_lcd_is_used(lcd)) {
		struct disp_video_timings *timmings;
		struct disp_panel_para *panel_info;

		lcd_parse_panel_para(lcd_index, &lcdp->panel_info);
		lcdp->panel_extend_info.lcd_cmap_en = lcdp->panel_info.lcd_cmap_en;
		lcdp->panel_extend_info.lcd_gamma_en = lcdp->panel_info.lcd_gamma_en;
		if (lcdp->panel_extend_info.lcd_gamma_en ||
		    lcdp->panel_extend_info.lcd_cmap_en)
			lcdp->panel_extend_dirty = 1;
		if (lcdp->panel_info.lcd_if == LCD_IF_DSI &&
		    lcdp->panel_info.lcd_tcon_mode == DISP_TCON_DUAL_DSI &&
		    lcdp->panel_info.lcd_dsi_port_num ==
			DISP_LCD_DSI_SINGLE_PORT) {
			lcdp->panel_info.lcd_ht *= 2;
			lcdp->panel_info.lcd_hspw *= 2;
			lcdp->panel_info.lcd_x *= 2;
			lcdp->panel_info.lcd_hbp *= 2;
			lcdp->panel_info.lcd_dclk_freq *= 2;
		}
		timmings = &lcd->timings;
		panel_info = &lcdp->panel_info;
		timmings->pixel_clk = panel_info->lcd_dclk_freq * 1000;
		timmings->x_res = panel_info->lcd_x;
		timmings->y_res = panel_info->lcd_y;
		timmings->hor_total_time = panel_info->lcd_ht;
		timmings->hor_sync_time = panel_info->lcd_hspw;
		timmings->hor_back_porch =
		    panel_info->lcd_hbp - panel_info->lcd_hspw;
		timmings->hor_front_porch =
		    panel_info->lcd_ht - panel_info->lcd_hbp -
		    panel_info->lcd_x;
		timmings->ver_total_time = panel_info->lcd_vt;
		timmings->ver_sync_time = panel_info->lcd_vspw;
		timmings->ver_back_porch =
		    panel_info->lcd_vbp - panel_info->lcd_vspw;
		timmings->ver_front_porch =
		    panel_info->lcd_vt - panel_info->lcd_vbp -
		    panel_info->lcd_y;
	}
	disp_lcd_bright_curve_init(lcd);

	if (disp_lcd_is_used(lcd)) {
		__u64 backlight_bright;
		__u64 period_ns, duty_ns;

		if (lcdp->panel_info.lcd_pwm_used) {
			lcdp->pwm_info.channel = lcdp->panel_info.lcd_pwm_ch;
			lcdp->pwm_info.polarity = lcdp->panel_info.lcd_pwm_pol;
			lcdp->pwm_info.dev = disp_sys_pwm_request(
								  lcdp->panel_info.lcd_pwm_ch);

			if (lcdp->panel_info.lcd_pwm_freq != 0) {
				period_ns =
				    1000 * 1000 * 1000 /
				    lcdp->panel_info.lcd_pwm_freq;
			} else {
				DE_WRN("lcd%d.lcd_pwm_freq is ZERO\n",
				       lcd->disp);
				/* default 1khz */
				period_ns = 1000 * 1000 * 1000 / 1000;
			}

			backlight_bright = lcdp->lcd_cfg.backlight_bright;

			duty_ns = (backlight_bright * period_ns) / 256;
			lcdp->pwm_info.duty_ns = duty_ns;
			lcdp->pwm_info.period_ns = period_ns;
			disp_sys_pwm_config(lcdp->pwm_info.dev, duty_ns, period_ns);
		}
		lcd_clk_init(lcd);
		for (i = 0; i < 256; i++) {
			lcdp->panel_extend_info.lcd_gamma_tbl[i] =
				(i << 24) | (i << 16) | (i << 8) | (i);
		}
		for (i = 0; i < LCD_GPIO_NUM; i++) {
			if (!lcdp->lcd_cfg.lcd_gpio_used[i])
				continue;

			memcpy(&gpio_info, &(lcdp->lcd_cfg.lcd_gpio[i]),
			       sizeof(struct disp_gpio_info));
			disp_sys_gpio_request(&gpio_info);
		}
	}

	/* lcd_panel_parameter_check(lcd->disp, lcd); */
	return 0;
}

#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)
static void disp_close_eink_panel_task(struct work_struct *work)
{				/* (unsigned long parg) */
	struct disp_device *plcd = NULL;

	plcd = disp_device_find(0, DISP_OUTPUT_TYPE_LCD);
	plcd->disable(plcd);
	display_finish_flag = 1;
}
#endif

static disp_config_update_t disp_lcd_check_config_dirty(struct disp_device *lcd,
					struct disp_device_config *config)
{
	disp_config_update_t ret = DISP_NOT_UPDATE;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		goto exit;
	}

	if (lcdp->enabled == 0 ||
	    ((config->reserve1 & DISP_LCD_MODE_DIRTY_MASK) &&
	     config->reserve1 != lcdp->config.reserve1))
		ret = DISP_NORMAL_UPDATE;

exit:
	return ret;
}

static s32 disp_lcd_set_static_config(struct disp_device *lcd,
			       struct disp_device_config *config)
{
	int ret = -1;
	struct disp_lcd_private_data *lcdp = NULL;

	if (!lcd || !config) {
		DE_WRN("NULL hdl!\n");
		goto OUT;
	}

	lcdp = disp_lcd_get_priv(lcd);
	if (!lcdp) {
		DE_WRN("NULL lcdp!\n");
		goto OUT;
	}

	if ((config->reserve1 & DISP_LCD_MODE_DIRTY_MASK) &&
	    config->reserve1 != lcdp->config.reserve1) {
		ret = disp_lcd_init(lcd, config->reserve1 & 0xf);
		lcd_set_panel_funs();
		lcdp->config.reserve1 = config->reserve1;
	} else
		ret = 0;

OUT:
	return ret;
}

static s32 disp_lcd_get_static_config(struct disp_device *lcd,
					struct disp_device_config *config)
{
	int ret = 0;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		ret = -1;
		goto exit;
	}

	config->type = lcd->type;
	config->format = DISP_CSC_TYPE_RGB;
exit:
	return ret;
}

static s32 disp_lcd_exit(struct disp_device *lcd)
{
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(lcd);
	struct disp_gpio_info gpio_info;
	s32 i = 0;

	if ((lcd == NULL) || (lcdp == NULL)) {
		DE_WRN("NULL hdl!\n");
		return DIS_FAIL;
	}

	lcd_regulator_put_wrap(&lcdp->lcd_cfg);

	for (i = 0; i < LCD_GPIO_NUM; i++) {
		memcpy(&gpio_info, &(lcdp->lcd_cfg.lcd_gpio[i]), sizeof(struct disp_gpio_info));
		gpio_info.value = 0;
		disp_sys_gpio_release(&gpio_info);
	}
	lcd_clk_exit(lcd);

	return 0;
}

#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
static s32 disp_lcd_get_esd_info(struct disp_device *dispdev,
		 struct disp_lcd_esd_info *p_esd_info)
{
	s32 ret = -1;
	struct disp_lcd_private_data *lcdp = NULL;

	if (!dispdev || !p_esd_info)
		goto OUT;
	 lcdp = disp_lcd_get_priv(dispdev);
	if (!lcdp)
		goto OUT;

	memcpy(p_esd_info, &lcdp->esd_inf, sizeof(struct disp_lcd_esd_info));
	ret = 0;
OUT:
	return ret;
}
#endif

static u32 disp_lcd_usec_before_vblank(struct disp_device *dispdev)
{
	int cur_line;
	int start_delay;
	u32 usec = 0;
	struct disp_lcd_private_data *lcdp = disp_lcd_get_priv(dispdev);

	if (!dispdev || !lcdp) {
		DE_WRN("NULL hdl!\n");
		goto exit;
	}

	start_delay = disp_al_lcd_get_start_delay(dispdev->hwdev_index,
						  &lcdp->panel_info);
	cur_line =
	    disp_al_lcd_get_cur_line(dispdev->hwdev_index, &lcdp->panel_info);
	if (cur_line > (start_delay - lcdp->judge_line)) {
		usec =
		    (lcdp->panel_info.lcd_vt - cur_line + 1) * lcdp->usec_per_line;
	}

exit:
	return usec;
}

s32 disp_init_lcd(struct disp_bsp_init_para *para)
{
	u32 num_devices;
	u32 disp = 0;
	struct disp_device *lcd;
	struct disp_lcd_private_data *lcdp;
	u32 hwdev_index = 0;
	u32 num_devices_support_lcd = 0;
#if defined(SUPPORT_DSI)
	u32 i = 0, index_base;
#endif
	char primary_key[20];
	int ret = 0, value = 1;
#if defined(CONFIG_ARCH_SUN8IW17P1)
	s32 use_dsi_flag = 0;
#endif

	DE_INF("disp_init_lcd\n");

	spin_lock_init(&lcd_data_lock);
	num_devices = bsp_disp_feat_get_num_devices();
	for (hwdev_index = 0; hwdev_index < num_devices; hwdev_index++) {
		if (bsp_disp_feat_is_supported_output_types
		    (hwdev_index, DISP_OUTPUT_TYPE_LCD))
			num_devices_support_lcd++;
	}
	lcds =
	    kmalloc_array(num_devices_support_lcd, sizeof(struct disp_device),
			  GFP_KERNEL | __GFP_ZERO);
	if (lcds == NULL) {
		DE_WRN("malloc memory(%d bytes) fail!\n",
		       (unsigned int)sizeof(struct disp_device) *
		       num_devices_support_lcd);
		goto malloc_err;
	}
	lcd_private =
	    (struct disp_lcd_private_data *)
	    kmalloc(sizeof(struct disp_lcd_private_data)
		    * num_devices_support_lcd, GFP_KERNEL | __GFP_ZERO);
	if (lcd_private == NULL) {
		DE_WRN("malloc memory(%d bytes) fail!\n",
		       (unsigned int)sizeof(struct disp_lcd_private_data) *
		       num_devices_support_lcd);
		goto malloc_err;
	}

	disp = 0;
	for (hwdev_index = 0; hwdev_index < num_devices; hwdev_index++) {
		if (!bsp_disp_feat_is_supported_output_types
		    (hwdev_index, DISP_OUTPUT_TYPE_LCD)) {
			continue;
		}

		sprintf(primary_key, "lcd%d", disp);
		ret = disp_sys_script_get_item(primary_key, "lcd_used", &value,
					       1);
		if (ret != 1 || value != 1)
			continue;
		lcd = &lcds[disp];
		lcdp = &lcd_private[disp];
		lcd->priv_data = (void *)lcdp;

		sprintf(lcd->name, "lcd%d", disp);
		lcd->disp = disp;
#if defined(CONFIG_ARCH_SUN8IW17P1)
		value = 0;
		ret = disp_sys_script_get_item(primary_key, "lcd_if", &value,
					       1);
		if (value == 4) {
			lcd->hwdev_index = 1;
			use_dsi_flag = 1;
		} else
			lcd->hwdev_index = (use_dsi_flag == 1) ? 0 : hwdev_index;
#else
		lcd->hwdev_index = hwdev_index;
#endif
		lcd->type = DISP_OUTPUT_TYPE_LCD;
		lcdp->irq_no = para->irq_no[DISP_MOD_LCD0 + lcd->hwdev_index];
		lcdp->clk_tcon_lcd = para->clk_tcon[lcd->hwdev_index];
		lcdp->clk_bus_tcon_lcd = para->clk_bus_tcon[lcd->hwdev_index];
		lcdp->clk_lvds = para->clk_tcon[lcd->hwdev_index];
		lcdp->rst_bus_tcon_lcd = para->rst_bus_tcon[lcd->hwdev_index];
		lcdp->rst_bus_lvds = para->rst_bus_lvds[lcd->hwdev_index];
#if defined(SUPPORT_DSI)
		lcdp->irq_no_dsi = para->irq_no[DISP_MOD_DSI0 + disp];

		index_base = CLK_NUM_PER_DSI * lcd->hwdev_index;
		for (i = 0; i < CLK_NUM_PER_DSI; i++) {
			lcdp->clk_mipi_dsi[i] = para->clk_mipi_dsi[index_base + i];
			lcdp->clk_bus_mipi_dsi[i] = para->clk_bus_mipi_dsi[index_base + i];
		}
		DE_INF("total number of clk in dsi:%d\n", CLK_DSI_NUM);

		lcdp->rst_bus_mipi_dsi = para->rst_bus_mipi_dsi[lcd->hwdev_index];
#endif
		DE_INF("lcd %d, irq_no=%d, irq_no_dsi=%d\n", disp, lcdp->irq_no,
		       lcdp->irq_no_dsi);

		lcd->set_manager = disp_device_set_manager;
		lcd->unset_manager = disp_device_unset_manager;
		lcd->get_resolution = disp_device_get_resolution;
		lcd->get_timings = disp_device_get_timings;
		lcd->enable = disp_lcd_enable;
		lcd->sw_enable = disp_lcd_sw_enable;
		lcd->fake_enable = disp_lcd_fake_enable;
		lcd->disable = disp_lcd_disable;
		lcd->is_enabled = disp_lcd_is_enabled;
		lcd->check_if_enabled = disp_lcd_check_if_enabled;
		lcd->set_bright = disp_lcd_set_bright;
		lcd->get_bright = disp_lcd_get_bright;
		lcd->set_bright_dimming = disp_lcd_set_bright_dimming;
		lcd->get_panel_info = disp_lcd_get_panel_info;
		lcd->set_static_config = disp_lcd_set_static_config;
		lcd->get_static_config = disp_lcd_get_static_config;
		lcd->check_config_dirty = disp_lcd_check_config_dirty;

		lcd->set_panel_func = disp_lcd_set_panel_funs;
		lcd->set_open_func = disp_lcd_set_open_func;
		lcd->set_close_func = disp_lcd_set_close_func;
		lcd->backlight_enable = disp_lcd_backlight_enable;
		lcd->backlight_disable = disp_lcd_backlight_disable;
		lcd->pwm_enable = disp_lcd_pwm_enable;
		lcd->pwm_disable = disp_lcd_pwm_disable;
		lcd->power_enable = disp_lcd_power_enable;
		lcd->power_disable = disp_lcd_power_disable;
		lcd->pin_cfg = disp_lcd_pin_cfg;
		lcd->tcon_enable = disp_lcd_tcon_enable;
		lcd->tcon_disable = disp_lcd_tcon_disable;
		lcd->gpio_set_value = disp_lcd_gpio_set_value;
		lcd->gpio_set_direction = disp_lcd_gpio_set_direction;
		lcd->get_dimensions = disp_lcd_get_dimensions;
		lcd->get_status = disp_lcd_get_status;
		lcd->is_in_safe_period = disp_lcd_is_in_safe_period;
		lcd->usec_before_vblank = disp_lcd_usec_before_vblank;
		lcd->set_gamma_tbl = disp_lcd_set_gamma_tbl;
		lcd->get_gamma_tbl = disp_lcd_get_gamma_tbl;
		lcd->enable_gamma = disp_lcd_enable_gamma;
		lcd->disable_gamma = disp_lcd_disable_gamma;
		lcd->get_fps = disp_lcd_get_fps;
		lcd->set_color_temperature = disp_lcd_set_color_temperature;
		lcd->get_color_temperature = disp_lcd_get_color_temperature;
		lcd->show_builtin_patten = disp_device_show_builtin_patten;
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
		lcd->get_esd_info = disp_lcd_get_esd_info;
#endif

		/*lcd->init = disp_lcd_init;*/
		lcd->exit = disp_lcd_exit;

#if defined(SUPPORT_EINK) && defined(CONFIG_EINK_PANEL_USED)
		INIT_WORK(&lcd->close_eink_panel_work,
			  disp_close_eink_panel_task);
#endif
		disp_lcd_init(lcd, lcd->disp);
		disp_device_register(lcd);
#if defined(CONFIG_DISP2_LCD_ESD_DETECT)
		INIT_WORK(&lcdp->reflush_work, disp_lcd_reflush_work);
		atomic_set(&lcdp->lcd_resetting, 0);
#endif

		disp++;
	}

	return 0;

malloc_err:
	kfree(lcds);
	kfree(lcd_private);
	lcds = NULL;
	lcd_private = NULL;

	return -1;
}

s32 disp_exit_lcd(void)
{
	u32 num_devices;
	u32 disp = 0;
	struct disp_device *lcd;
	u32 hwdev_index = 0;

	if (!lcds)
		return 0;

	num_devices = bsp_disp_feat_get_num_devices();

	disp = 0;
	for (hwdev_index = 0; hwdev_index < num_devices; hwdev_index++) {
		if (!bsp_disp_feat_is_supported_output_types
		    (hwdev_index, DISP_OUTPUT_TYPE_LCD)) {
			continue;
		}
		lcd = &lcds[disp];
		disp_device_unregister(lcd);
		lcd->exit(lcd);
		disp++;
	}

	kfree(lcds);
	kfree(lcd_private);
	lcds = NULL;
	lcd_private = NULL;

	return 0;
}
