// SPDX-License-Identifier: GPL-2.0+

#include <common.h>
#include <backlight.h>
#include <dm.h>
#include <mipi_dsi.h>
#include <panel.h>
#include <asm/gpio.h>
#include <dm/device_compat.h>
#include <linux/delay.h>
#include <power/regulator.h>

struct ili9881c_panel_priv {
	struct udevice *reg;
	struct udevice *backlight;
	struct gpio_desc reset;
};

static const struct display_timing default_timing = {
	.pixelclock = {.min = 54000000, .typ = 54000000, .max = 54000000,},
	.hactive = {.min = 720, .typ = 720, .max = 720,},
	.hfront_porch = {.min = 20, .typ = 20, .max = 20,},
	.hback_porch = {.min = 20, .typ = 20, .max = 20,},
	.hsync_len = {.min = 2, .typ = 2, .max = 2,},
	.vactive = {.min = 1280, .typ = 1280, .max = 1280,},
	.vfront_porch = {.min = 15, .typ = 15, .max = 15,},
	.vback_porch = {.min = 10, .typ = 10, .max = 10,},
	.vsync_len = {.min = 2, .typ = 2, .max = 2,},
	.flags = 0,
};

static int ili9881c_switch_page(struct udevice *dev, u8 page)
{
	u8 buf[4] = { 0xff, 0x98, 0x81, page };

        struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
        struct mipi_dsi_device *device = plat->device;

        return mipi_dsi_dcs_write_buffer(device, buf, sizeof(buf));
}

static int ili9881c_send_cmd_data(struct udevice *dev, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
        struct mipi_dsi_device *device = plat->device;

        printf("ili9881c_send_cmd_data cmd 0x%02X data 0x%02X\n", cmd,data);

        return mipi_dsi_dcs_write_buffer(device, buf, sizeof(buf));
}

struct ili9881c_instr {
	u8 cmd;
	u8 data;
};

#define LCD_ILI9881C_CMD(CMD, DATA)	{.cmd = CMD, .data = DATA}


static const struct ili9881c_instr ili9881c_init_data_2[] = {
    LCD_ILI9881C_CMD(0xB2, 0x10),
};



static void ili9881c_init_sequence(struct udevice *dev)
{
	int i;
	int ret;
	u8 buf[128] = {0};

        struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
        struct mipi_dsi_device *device = plat->device;

	printf("MIPI DSI LCD ILI9881C setup v2.\n");
	for (i = 0; i < ARRAY_SIZE(ili9881c_init_data_2); i++) {
		const struct ili9881c_instr *instr = &ili9881c_init_data_2[i];

		if (instr->cmd == 0xFF) {
			ret = ili9881c_switch_page(dev, instr->data);
		} else {
			ret = ili9881c_send_cmd_data(dev, instr->cmd, instr->data);
		}
		if (ret < 0){
			printf("MIPI DSI LCD ILI9881C setup failed with cmd: %08X.\n", instr->cmd);
			return;
		}
	}

	ili9881c_switch_page(dev, 0);
	buf[0] = MIPI_DCS_EXIT_SLEEP_MODE;
	buf[1] = 0;
        mipi_dsi_dcs_write_buffer(device, buf, 2);
	mdelay(120);
	buf[0] = MIPI_DCS_SET_DISPLAY_ON;
        mipi_dsi_dcs_write_buffer(device, buf, 2);

	return;
}

static int ili9881c_panel_enable_backlight(struct udevice *dev)
{
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	struct mipi_dsi_device *device = plat->device;
	struct ili9881c_panel_priv *priv = dev_get_priv(dev);
	int ret;

	ret = mipi_dsi_attach(device);
	if (ret < 0)
		return ret;

	ili9881c_init_sequence(dev);

	ret = mipi_dsi_dcs_exit_sleep_mode(device);
	if (ret)
		return ret;

	mdelay(125);

	ret = mipi_dsi_dcs_set_display_on(device);
	if (ret)
		return ret;

	mdelay(125);

	return 0;
}

static int ili9881c_panel_get_display_timing(struct udevice *dev,
                                             struct display_timing *timings)
{
	memcpy(timings, &default_timing, sizeof(*timings));
	return 0;
}

static int ili9881c_panel_ofdata_to_platdata(struct udevice *dev)
{
	struct ili9881c_panel_priv *priv = dev_get_priv(dev);
	int ret;

	if (IS_ENABLED(CONFIG_DM_REGULATOR)) {
		ret =  device_get_supply_regulator(dev, "power-supply",
						   &priv->reg);
		if (ret && ret != -ENOENT) {
			dev_err(dev, "Warning: cannot get power supply\n");
			return ret;
		}
	}

	ret = gpio_request_by_name(dev, "reset-gpios", 0, &priv->reset,
				   GPIOD_IS_OUT);
	if (ret) {
		dev_err(dev, "Warning: cannot get reset GPIO\n");
		if (ret != -ENOENT)
			return ret;
	}

	ret = uclass_get_device_by_phandle(UCLASS_PANEL_BACKLIGHT, dev,
					   "backlight", &priv->backlight);
	if (ret) {
		dev_err(dev, "Cannot get backlight: ret=%d\n", ret);
		return ret;
	}

	return 0;
}

static int ili9881c_panel_probe(struct udevice *dev)
{

	struct ili9881c_panel_priv *priv = dev_get_priv(dev);
	struct mipi_dsi_panel_plat *plat = dev_get_platdata(dev);
	int ret;

	if (IS_ENABLED(CONFIG_DM_REGULATOR) && priv->reg) {
		ret = regulator_set_enable(priv->reg, true);
		if (ret)
			return ret;
	}

	/* reset panel */
        int ii;
        for (ii=0;ii<3;ii++)
        {
        printf("MIPI DSI LCD ILI9881C reset true\n");
	dm_gpio_set_value(&priv->reset, false);
	mdelay(20);
        printf("MIPI DSI LCD ILI9881C reset false\n");
	dm_gpio_set_value(&priv->reset, true);
	mdelay(100);
        }
	mdelay(1000);

	plat->lanes = 2;
	plat->format = MIPI_DSI_FMT_RGB888;
	plat->mode_flags = MIPI_DSI_MODE_VIDEO |
			MIPI_DSI_MODE_VIDEO_SYNC_PULSE |
			MIPI_DSI_MODE_LPM;

	return 0;
}

static const struct panel_ops ili9881c_panel_ops = {
	.enable_backlight = ili9881c_panel_enable_backlight,
	.get_display_timing = ili9881c_panel_get_display_timing,
};

static const struct udevice_id ili9881c_panel_ids[] = {
	{ .compatible = "powertip,ph720128t003-zbc02" },
	{ }
};

U_BOOT_DRIVER(ili9881c_panel) = {
	.name			  = "ili9881c_panel",
	.id			  = UCLASS_PANEL,
	.of_match		  = ili9881c_panel_ids,
	.ops			  = &ili9881c_panel_ops,
	.ofdata_to_platdata	  = ili9881c_panel_ofdata_to_platdata,
	.probe			  = ili9881c_panel_probe,
	.platdata_auto_alloc_size = sizeof(struct mipi_dsi_panel_plat),
	.priv_auto_alloc_size	= sizeof(struct ili9881c_panel_priv),
};
