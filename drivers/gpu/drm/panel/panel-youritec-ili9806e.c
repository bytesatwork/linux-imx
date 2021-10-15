// SPDX-License-Identifier: GPL-2.0
/*
 * Ilitek ILI9806E LCD drm_panel driver.
 *
 * Copyright (C) 2022 bytes at work AG - https://www.bytesatwork.io
 *
 * based on panel-raydium-rm67191.c
 *
 * Copyright (C) 2017 NXP
 */

#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>
#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

enum ili9881c_op {
	ILI9881C_SWITCH_PAGE,
	ILI9881C_COMMAND,
};

struct ili9881c_instr {
	enum ili9881c_op	op;

	union arg {
		struct cmd {
			u8	cmd;
			u8	data;
		} cmd;
		u8	page;
		} arg;
};

#define ILI9881C_SWITCH_PAGE_INSTR(_page)	\
	{					\
		.op = ILI9881C_SWITCH_PAGE,	\
		.arg = {			\
			.page = (_page),	\
		},				\
	}

#define ILI9881C_COMMAND_INSTR(_cmd, _data)		\
	{						\
		.op = ILI9881C_COMMAND,		\
		.arg = {				\
			.cmd = {			\
				.cmd = (_cmd),		\
				.data = (_data),	\
			},				\
		},					\
	}

static const struct ili9881c_instr ili9881c_init[] = {
	ILI9881C_SWITCH_PAGE_INSTR(1),
	ILI9881C_COMMAND_INSTR(0x08, 0x10), //Output SDA
	ILI9881C_COMMAND_INSTR(0x20, 0x00), //set DE/VSYNC mode
	ILI9881C_COMMAND_INSTR(0x21, 0x01), //DE = 1 Active
	ILI9881C_COMMAND_INSTR(0x30, 0x01), //Resolution setting 480 X 854
	ILI9881C_COMMAND_INSTR(0x31, 0x00), //Inversion setting 2-dot
	ILI9881C_COMMAND_INSTR(0x40, 0x16), //BT  AVDD,AVDD
	ILI9881C_COMMAND_INSTR(0x41, 0x33),
	ILI9881C_COMMAND_INSTR(0x42, 0x03), //VGL=DDVDH+VCIP -DDVDL,VGH=2DDVDL-VCIP
	ILI9881C_COMMAND_INSTR(0x43, 0x09), //SET VGH clamp level
	ILI9881C_COMMAND_INSTR(0x44, 0x06), //SET VGL clamp level
	ILI9881C_COMMAND_INSTR(0x50, 0x88), //VREG1
	ILI9881C_COMMAND_INSTR(0x51, 0x88), //VREG2
	ILI9881C_COMMAND_INSTR(0x52, 0x00), //Flicker MSB
	ILI9881C_COMMAND_INSTR(0x53, 0x49), //Flicker LSB
	ILI9881C_COMMAND_INSTR(0x55, 0x49), //Flicker
	ILI9881C_COMMAND_INSTR(0x60, 0x07),
	ILI9881C_COMMAND_INSTR(0x61, 0x00),
	ILI9881C_COMMAND_INSTR(0x62, 0x07),
	ILI9881C_COMMAND_INSTR(0x63, 0x00),
	ILI9881C_COMMAND_INSTR(0xA0, 0x00), //Positive Gamma
	ILI9881C_COMMAND_INSTR(0xA1, 0x09),
	ILI9881C_COMMAND_INSTR(0xA2, 0x11),
	ILI9881C_COMMAND_INSTR(0xA3, 0x0B),
	ILI9881C_COMMAND_INSTR(0xA4, 0x05),
	ILI9881C_COMMAND_INSTR(0xA5, 0x08),
	ILI9881C_COMMAND_INSTR(0xA6, 0x06),
	ILI9881C_COMMAND_INSTR(0xA7, 0x04),
	ILI9881C_COMMAND_INSTR(0xA8, 0x09),
	ILI9881C_COMMAND_INSTR(0xA9, 0x0C),
	ILI9881C_COMMAND_INSTR(0xAA, 0x15),
	ILI9881C_COMMAND_INSTR(0xAB, 0x08),
	ILI9881C_COMMAND_INSTR(0xAC, 0x0F),
	ILI9881C_COMMAND_INSTR(0xAD, 0x12),
	ILI9881C_COMMAND_INSTR(0xAE, 0x09),
	ILI9881C_COMMAND_INSTR(0xAF, 0x00),
	ILI9881C_COMMAND_INSTR(0xC0, 0x00), //Negative Gamma
	ILI9881C_COMMAND_INSTR(0xC1, 0x09),
	ILI9881C_COMMAND_INSTR(0xC2, 0x10),
	ILI9881C_COMMAND_INSTR(0xC3, 0x0C),
	ILI9881C_COMMAND_INSTR(0xC4, 0x05),
	ILI9881C_COMMAND_INSTR(0xC5, 0x08),
	ILI9881C_COMMAND_INSTR(0xC6, 0x06),
	ILI9881C_COMMAND_INSTR(0xC7, 0x04),
	ILI9881C_COMMAND_INSTR(0xC8, 0x08),
	ILI9881C_COMMAND_INSTR(0xC9, 0x0C),
	ILI9881C_COMMAND_INSTR(0xCA, 0x14),
	ILI9881C_COMMAND_INSTR(0xCB, 0x08),
	ILI9881C_COMMAND_INSTR(0xCC, 0x0F),
	ILI9881C_COMMAND_INSTR(0xCD, 0x11),
	ILI9881C_COMMAND_INSTR(0xCE, 0x09),
	ILI9881C_COMMAND_INSTR(0xCF, 0x00),
	ILI9881C_SWITCH_PAGE_INSTR(6),
	ILI9881C_COMMAND_INSTR(0x00, 0x20),
	ILI9881C_COMMAND_INSTR(0x01, 0x0A),
	ILI9881C_COMMAND_INSTR(0x02, 0x00),
	ILI9881C_COMMAND_INSTR(0x03, 0x00),
	ILI9881C_COMMAND_INSTR(0x04, 0x01),
	ILI9881C_COMMAND_INSTR(0x05, 0x01),
	ILI9881C_COMMAND_INSTR(0x06, 0x98),
	ILI9881C_COMMAND_INSTR(0x07, 0x06),
	ILI9881C_COMMAND_INSTR(0x08, 0x01),
	ILI9881C_COMMAND_INSTR(0x09, 0x80),
	ILI9881C_COMMAND_INSTR(0x0A, 0x00),
	ILI9881C_COMMAND_INSTR(0x0B, 0x00),
	ILI9881C_COMMAND_INSTR(0x0C, 0x01),
	ILI9881C_COMMAND_INSTR(0x0D, 0x01),
	ILI9881C_COMMAND_INSTR(0x0E, 0x05),
	ILI9881C_COMMAND_INSTR(0x0F, 0x00),
	ILI9881C_COMMAND_INSTR(0x10, 0xF0),
	ILI9881C_COMMAND_INSTR(0x11, 0xF4),
	ILI9881C_COMMAND_INSTR(0x12, 0x01),
	ILI9881C_COMMAND_INSTR(0x13, 0x00),
	ILI9881C_COMMAND_INSTR(0x14, 0x00),
	ILI9881C_COMMAND_INSTR(0x15, 0xC0),
	ILI9881C_COMMAND_INSTR(0x16, 0x08),
	ILI9881C_COMMAND_INSTR(0x17, 0x00),
	ILI9881C_COMMAND_INSTR(0x18, 0x00),
	ILI9881C_COMMAND_INSTR(0x19, 0x00),
	ILI9881C_COMMAND_INSTR(0x1A, 0x00),
	ILI9881C_COMMAND_INSTR(0x1B, 0x00),
	ILI9881C_COMMAND_INSTR(0x1C, 0x00),
	ILI9881C_COMMAND_INSTR(0x1D, 0x00),
	ILI9881C_COMMAND_INSTR(0x20, 0x01),
	ILI9881C_COMMAND_INSTR(0x21, 0x23),
	ILI9881C_COMMAND_INSTR(0x22, 0x45),
	ILI9881C_COMMAND_INSTR(0x23, 0x67),
	ILI9881C_COMMAND_INSTR(0x24, 0x01),
	ILI9881C_COMMAND_INSTR(0x25, 0x23),
	ILI9881C_COMMAND_INSTR(0x26, 0x45),
	ILI9881C_COMMAND_INSTR(0x27, 0x67),
	ILI9881C_COMMAND_INSTR(0x30, 0x11),
	ILI9881C_COMMAND_INSTR(0x31, 0x11),
	ILI9881C_COMMAND_INSTR(0x32, 0x00),
	ILI9881C_COMMAND_INSTR(0x33, 0xEE),
	ILI9881C_COMMAND_INSTR(0x34, 0xFF),
	ILI9881C_COMMAND_INSTR(0x35, 0xBB),
	ILI9881C_COMMAND_INSTR(0x36, 0xAA),
	ILI9881C_COMMAND_INSTR(0x37, 0xDD),
	ILI9881C_COMMAND_INSTR(0x38, 0xCC),
	ILI9881C_COMMAND_INSTR(0x39, 0x66),
	ILI9881C_COMMAND_INSTR(0x3A, 0x77),
	ILI9881C_COMMAND_INSTR(0x3B, 0x22),
	ILI9881C_COMMAND_INSTR(0x3C, 0x22),
	ILI9881C_COMMAND_INSTR(0x3D, 0x22),
	ILI9881C_COMMAND_INSTR(0x3E, 0x22),
	ILI9881C_COMMAND_INSTR(0x3F, 0x22),
	ILI9881C_COMMAND_INSTR(0x40, 0x22),
	ILI9881C_SWITCH_PAGE_INSTR(7),
	ILI9881C_COMMAND_INSTR(0x17, 0x22),
	ILI9881C_COMMAND_INSTR(0x02, 0x77),
	ILI9881C_COMMAND_INSTR(0x26, 0xB2),
	ILI9881C_SWITCH_PAGE_INSTR(0),
};

/*
 * The panel seems to accept some private DCS commands that map
 * directly to registers.
 *
 * It is organised by page, with each page having its own set of
 * registers, and the first page looks like it's holding the standard
 * DCS commands.
 *
 * So before any attempt at sending a command or data, we have to be
 * sure if we're in the right page or not.
 */

static int ili9881c_switch_page(struct mipi_dsi_device *dsi, u8 page)
{
	u8 buf[] = { 0xff, 0xff, 0x98, 0x06 , 0x04, page };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}

static int ili9881c_send_cmd_data(struct mipi_dsi_device *dsi, u8 cmd, u8 data)
{
	u8 buf[2] = { cmd, data };
	int ret;

	ret = mipi_dsi_dcs_write_buffer(dsi, buf, sizeof(buf));
	if (ret < 0)
		return ret;

	return 0;
}


static const u32 ili9806e_bus_formats[] = {
	MEDIA_BUS_FMT_RGB888_1X24,
	MEDIA_BUS_FMT_RGB666_1X18,
	MEDIA_BUS_FMT_RGB565_1X16,
};

struct ili9806e_panel {
	struct drm_panel base;
	struct mipi_dsi_device *dsi;

	struct gpio_desc *reset;

	bool prepared;
	bool enabled;

	struct videomode vm;
	u32 width_mm;
	u32 height_mm;
};

static inline struct ili9806e_panel *to_ili9806e_panel(struct drm_panel *panel)
{
	return container_of(panel, struct ili9806e_panel, base);
}

static int ili9806e_panel_push_cmd_list(struct mipi_dsi_device *dsi)
{
	int i, ret;
	for (i = 0; i < ARRAY_SIZE(ili9881c_init); i++) {
		const struct ili9881c_instr *instr = &ili9881c_init[i];

		if (instr->op == ILI9881C_SWITCH_PAGE)
			ret = ili9881c_switch_page(dsi, instr->arg.page);
		else if (instr->op == ILI9881C_COMMAND)
			ret = ili9881c_send_cmd_data(dsi, instr->arg.cmd.cmd,
							instr->arg.cmd.data);

		if (ret)
			return ret;
	}

	return ret;
};

static int color_format_from_dsi_format(enum mipi_dsi_pixel_format format)
{
	switch (format) {
	case MIPI_DSI_FMT_RGB565:
		return 0x55;
	case MIPI_DSI_FMT_RGB666:
	case MIPI_DSI_FMT_RGB666_PACKED:
		return 0x66;
	case MIPI_DSI_FMT_RGB888:
		return 0x77;
	default:
		return 0x77; /* for backward compatibility */
	}
};

static int ili9806e_panel_prepare(struct drm_panel *panel)
{
	struct ili9806e_panel *ili9806e = to_ili9806e_panel(panel);

	if (ili9806e->prepared)
		return 0;
	printk("mipi prepare reset sequence 1 0 1\n");

	if (ili9806e->reset != NULL) {
	}

	ili9806e->prepared = true;

	return 0;
}

static int ili9806e_panel_unprepare(struct drm_panel *panel)
{
	struct ili9806e_panel *ili9806e = to_ili9806e_panel(panel);
	struct device *dev = &ili9806e->dsi->dev;

	if (!ili9806e->prepared)
		return 0;

	if (ili9806e->enabled) {
		DRM_DEV_ERROR(dev, "Panel still enabled!\n");
		return -EPERM;
	}

	ili9806e->prepared = false;

	return 0;
}

static int ili9806e_panel_enable(struct drm_panel *panel)
{
	struct ili9806e_panel *ili9806e = to_ili9806e_panel(panel);
	struct mipi_dsi_device *dsi = ili9806e->dsi;
	struct device *dev = &dsi->dev;
	int color_format = color_format_from_dsi_format(dsi->format);
	int ret;

	if (ili9806e->enabled)
		return 0;

	if (!ili9806e->prepared) {
		DRM_DEV_ERROR(dev, "Panel not prepared!\n");
		return -EPERM;
	}

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	gpiod_set_value(ili9806e->reset, 0);
	msleep(20);
	gpiod_set_value(ili9806e->reset, 1);
	msleep(100);

	mipi_dsi_dcs_soft_reset(dsi);
	msleep(20);

	ret = ili9806e_panel_push_cmd_list(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to send MCS (%d)\n", ret);
		goto fail;
	}

	/* Set pixel format */
	ret = mipi_dsi_dcs_set_pixel_format(dsi, color_format);
	DRM_DEV_DEBUG_DRIVER(dev, "Interface color format set to 0x%x\n",
				color_format);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set pixel format (%d)\n", ret);
		goto fail;
	}

	/* Exit sleep mode */
	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to exit sleep mode (%d)\n", ret);
		goto fail;
	}

	msleep(125);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display ON (%d)\n", ret);
		goto fail;
	}

	msleep(20);

	ili9806e->enabled = true;

	return 0;

fail:
	if (ili9806e->reset != NULL)
		gpiod_set_value(ili9806e->reset, 0);

	return ret;
}

static int ili9806e_panel_disable(struct drm_panel *panel)
{
	struct ili9806e_panel *ili9806e = to_ili9806e_panel(panel);
	struct mipi_dsi_device *dsi = ili9806e->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	if (!ili9806e->enabled)
		return 0;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to set display OFF (%d)\n", ret);
		return ret;
	}

	msleep(5);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		DRM_DEV_ERROR(dev, "Failed to enter sleep mode (%d)\n", ret);
		return ret;
	}

	msleep(100);

	ili9806e->enabled = false;

	return 0;
}

static int ili9806e_panel_get_modes(struct drm_panel *panel)
{
	struct ili9806e_panel *ili9806e = to_ili9806e_panel(panel);
	struct device *dev = &ili9806e->dsi->dev;
	struct drm_connector *connector = panel->connector;
	struct drm_display_mode *mode;
	u32 *bus_flags = &connector->display_info.bus_flags;
	int ret;

	mode = drm_mode_create(connector->dev);
	if (!mode) {
		DRM_DEV_ERROR(dev, "Failed to create display mode!\n");
		return 0;
	}

	drm_display_mode_from_videomode(&ili9806e->vm, mode);
	mode->width_mm = ili9806e->width_mm;
	mode->height_mm = ili9806e->height_mm;
	connector->display_info.width_mm = ili9806e->width_mm;
	connector->display_info.height_mm = ili9806e->height_mm;
	mode->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;

	if (ili9806e->vm.flags & DISPLAY_FLAGS_DE_HIGH)
		*bus_flags |= DRM_BUS_FLAG_DE_HIGH;
	if (ili9806e->vm.flags & DISPLAY_FLAGS_DE_LOW)
		*bus_flags |= DRM_BUS_FLAG_DE_LOW;
	if (ili9806e->vm.flags & DISPLAY_FLAGS_PIXDATA_NEGEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_NEGEDGE;
	if (ili9806e->vm.flags & DISPLAY_FLAGS_PIXDATA_POSEDGE)
		*bus_flags |= DRM_BUS_FLAG_PIXDATA_POSEDGE;

	ret = drm_display_info_set_bus_formats(&connector->display_info,
			ili9806e_bus_formats, ARRAY_SIZE(ili9806e_bus_formats));
	if (ret)
		return ret;

	drm_mode_probed_add(panel->connector, mode);

	return 1;
}

static const struct drm_panel_funcs ili9806e_panel_funcs = {
	.prepare = ili9806e_panel_prepare,
	.unprepare = ili9806e_panel_unprepare,
	.enable = ili9806e_panel_enable,
	.disable = ili9806e_panel_disable,
	.get_modes = ili9806e_panel_get_modes,
};

/*
 * The clock might range from 66MHz (30Hz refresh rate)
 * to 132MHz (60Hz refresh rate)
 */
static const struct display_timing ili9806e_default_timing = {
	.pixelclock = { 30000000, 30000000, 30000000 },
	.hactive = { 480, 480, 480 },
	.hfront_porch = {100 , 100, 100 },
	.hsync_len = { 10, 10, 10 },
	.hback_porch = { 50, 50, 50 },
	.vactive = { 854, 854, 854 },
	.vfront_porch = { 20, 20, 20},
	.vsync_len = { 4, 4, 4 },
	.vback_porch = { 16, 16, 16 },
};

static int ili9806e_panel_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct device_node *np = dev->of_node;
	struct device_node *timings;
	struct ili9806e_panel *panel;
	int ret;
	u32 video_mode;

	panel = devm_kzalloc(&dsi->dev, sizeof(*panel), GFP_KERNEL);
	if (!panel)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, panel);

	panel->dsi = dsi;

	dsi->lanes = 2;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST |
                          MIPI_DSI_MODE_LPM;
	ret = of_property_read_u32(np, "video-mode", &video_mode);
	if (!ret) {
		switch (video_mode) {
		case 0:
			/* burst mode */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_BURST;
			break;
		case 1:
			/* non-burst mode with sync event */
			break;
		case 2:
			/* non-burst mode with sync pulse */
			dsi->mode_flags |= MIPI_DSI_MODE_VIDEO_SYNC_PULSE;
			break;
		default:
			dev_warn(dev, "invalid video mode %d\n", video_mode);
			break;

		}
	}

	/*
	 * 'display-timings' is optional, so verify if the node is present
	 * before calling of_get_videomode so we won't get console error
	 * messages
	 */
	timings = of_get_child_by_name(np, "display-timings");
	if (timings) {
		of_node_put(timings);
		ret = of_get_videomode(np, &panel->vm, 0);
		if (ret < 0) {
			dev_err(dev, "Failed to get display-timings property (%d)\n", ret);
			return ret;
		}
	} else {
		videomode_from_timing(&ili9806e_default_timing, &panel->vm);
	}

	of_property_read_u32(np, "panel-width-mm", &panel->width_mm);
	of_property_read_u32(np, "panel-height-mm", &panel->height_mm);

	panel->reset = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);

	if (IS_ERR(panel->reset))
		panel->reset = NULL;
	else
		gpiod_set_value(panel->reset, 0);

	drm_panel_init(&panel->base);
	panel->base.funcs = &ili9806e_panel_funcs;
	panel->base.dev = dev;

	ret = drm_panel_add(&panel->base);

	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&panel->base);

	return ret;
}

static int ili9806e_panel_remove(struct mipi_dsi_device *dsi)
{
	struct ili9806e_panel *ili9806e = mipi_dsi_get_drvdata(dsi);
	struct device *dev = &dsi->dev;
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		DRM_DEV_ERROR(dev, "Failed to detach from host (%d)\n",
			ret);

	drm_panel_detach(&ili9806e->base);

	if (ili9806e->base.dev)
		drm_panel_remove(&ili9806e->base);

	return 0;
}

static void ili9806e_panel_shutdown(struct mipi_dsi_device *dsi)
{
	struct ili9806e_panel *ili9806e = mipi_dsi_get_drvdata(dsi);

	ili9806e_panel_disable(&ili9806e->base);
	ili9806e_panel_unprepare(&ili9806e->base);
}

static const struct of_device_id ili9806e_of_match[] = {
	{ .compatible = "youritec,ili9806", },
	{ }
};
MODULE_DEVICE_TABLE(of, ili9806e_of_match);

static struct mipi_dsi_driver ili9806e_panel_driver = {
	.driver = {
		.name = "panel-youritec-ili9806",
		.of_match_table = ili9806e_of_match,
	},
	.probe = ili9806e_panel_probe,
	.remove = ili9806e_panel_remove,
	.shutdown = ili9806e_panel_shutdown,
};
module_mipi_dsi_driver(ili9806e_panel_driver);

MODULE_AUTHOR("Stephan Dünner <stephan.duenner@bytesatwork.ch>");
MODULE_DESCRIPTION("DRM Driver for Youritech MIPI DSI panel with ILI9806 Controller");
MODULE_LICENSE("GPL v2");
