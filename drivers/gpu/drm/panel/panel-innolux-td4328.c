// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2024 FIXME
// Generated with linux-mdss-dsi-panel-driver-generator from vendor device tree:
//   Copyright (c) 2013, The Linux Foundation. All rights reserved. (FIXME)

#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regulator/consumer.h>

#include <drm/drm_mipi_dsi.h>
#include <drm/drm_modes.h>
#include <drm/drm_panel.h>
#include <drm/drm_probe_helper.h>

struct innolux_td4328 {
	struct drm_panel panel;
	struct mipi_dsi_device *dsi;
	struct regulator_bulk_data supplies[3];
	struct gpio_desc *reset_gpio;
	struct gpio_desc *enable_gpio;
	enum drm_panel_orientation orientation;
};

static inline struct innolux_td4328 *to_innolux_td4328(struct drm_panel *panel)
{
	return container_of(panel, struct innolux_td4328, panel);
}

static void innolux_td4328_reset(struct innolux_td4328 *ctx)
{
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(ctx->reset_gpio, 0);
	msleep(80);
}

static int write_te = 0;
static bool sseq_1 = false;
static bool sseq_2 = false;
static bool sseq_3 = false;
static bool sseq_4 = false;
static bool sseq_5 = false;

module_param(write_te, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(write_te, "How to write SET_TEAR_ON: 0 = use proper function, 1 = MIPI_DSI_DCS_SHORT_WRITE with no param, 2 = MIPI_DSI_DCS_SHORT_WRITE_PARAM with 0x00");
module_param(sseq_1, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(sseq_1, "Use the init sequence from the published source instead of from the Android dump");
module_param(sseq_2, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(sseq_2, "Use the init sequence from the published source instead of from the Android dump");
module_param(sseq_3, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(sseq_3, "Use the init sequence from the published source instead of from the Android dump");
module_param(sseq_4, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(sseq_4, "Use the init sequence from the published source instead of from the Android dump");
module_param(sseq_5, bool, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
MODULE_PARM_DESC(sseq_5, "Use the init sequence from the published source instead of from the Android dump");

static int innolux_td4328_enable(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };
	struct device *dev = &ctx->dsi->dev;

	ctx->dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	/* 
	Original init sequence from https://github.com/AYN-Tech/odin_android_kernel/blob/master/arch/arm64/boot/dts/qcom/dsi-panel-innolux-td4328-1080p-cmd.dtsi#L61
	  |  |  |  |  |     |    P
	  |  |  |  |  |     |    A
	D |  |  |  |  |     |    Y
	T |L |  |  |W |  D  |    L
	Y |A |  |A |A |  L  |    O
	P |S |V |C |I |  E  |    A
	E |T |C |K |T |  N  |    D
	// 29 = MIPI_DSI_GENERIC_LONG_WRITE
	29 01 00 00 00 00 02 B0 00
	29 01 00 00 00 00 18 C2 
	                     01 F7 80 08 68 08
	                     0C 10 00 08 70 00
	                     00 00 00 00 00 00
	                     03 83 00 00 00
	29 01 00 00 00 00 02 D6 01
	29 01 00 00 00 00 02 B0 03
	// 39 = MIPI_DSI_DCS_LONG_WRITE
	39 01 00 00 00 00 05 2A 00 00 04 37
	39 01 00 00 00 00 05 2B 00 00 07 7F
	// 05 = MIPI_DSI_DCS_SHORT_WRITE
	05 01 00 00 00 00 02 35 00
	05 01 00 00 96 00 01 11
	05 01 00 00 32 00 01 29

	Android DT-dump init sequence:
	  |  |  |  |  |     |    P
	  |  |  |  |  |     |    A
	D |  |  |  |  |     |    Y
	T |L |  |  |W |  D  |    L
	Y |A |  |A |A |  L  |    O
	P |S |V |C |I |  E  |    A
	E |T |C |K |T |  N  |    D
	// 29 = MIPI_DSI_GENERIC_LONG_WRITE
	29 01 00 00 00 00 02 b0 00
	29 01 00 00 00 00 18 c2
	                     01 f7 80 04 68 08
	                     09 10 00 08 30 00
	                     00 00 00 00 00 00
	                     02 80 00 00 00
	29 01 00 00 00 00 02 d6 01
	29 01 00 00 00 00 02 b0 03
	// 39 = MIPI_DSI_DCS_LONG_WRITE
	39 01 00 00 00 00 05 2a 00 00 04 37
	39 01 00 00 00 00 05 2b 00 00 07 7f
	// 05 = MIPI_DSI_DCS_SHORT_WRITE
	05 01 00 00 00 00 02 35 00
	05 01 00 00 96 00 01 11
	05 01 00 00 32 00 01 29
	*/
	u8 init_payload[0x18] = {
		0xc2, 0x01, 0xf7, 0x80,
		sseq_1 ? 0x08 : 0x04, // Unknown effect
		0x68, 0x08,
		sseq_2 ? 0x0c : 0x09, // 0x0c causes wrong colors, RGB becomes YCM
		0x10, 0x00, 0x08,
		sseq_3 ? 0x70 : 0x30, // Unknown effect
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		sseq_4 ? 0x03 : 0x02, // 0x03 makes framerate and init reliability consistent
		sseq_5 ? 0x83 : 0x80, // Unknown effect
		0x00, 0x00, 0x00
	};
	dev_info(dev, "init_payload = %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
		init_payload[0],
		init_payload[1],
		init_payload[2],
		init_payload[3],
		init_payload[4],
		init_payload[5],
		init_payload[6],
		init_payload[7],
		init_payload[8],
		init_payload[9],
		init_payload[10],
		init_payload[11],
		init_payload[12],
		init_payload[13],
		init_payload[14],
		init_payload[15],
		init_payload[16],
		init_payload[17],
		init_payload[18],
		init_payload[19],
		init_payload[20],
		init_payload[21],
		init_payload[22],
		init_payload[23]
	);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x00);
	mipi_dsi_generic_write_multi(&dsi_ctx, init_payload, ARRAY_SIZE(init_payload));
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xd6, 0x01);
	mipi_dsi_generic_write_seq_multi(&dsi_ctx, 0xb0, 0x03);

	mipi_dsi_dcs_set_column_address_multi(&dsi_ctx, 0, 1080 - 1);
	mipi_dsi_dcs_set_page_address_multi(&dsi_ctx, 0, 1920 - 1);
	switch (write_te) {
		case 1:
			dev_info(dev, "Set tear on with MIPI_DSI_DCS_SHORT_WRITE\n");
			u8 d1[1] = { 0x35 };
			mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, d1, 1);
			break;
		case 2:
			dev_info(dev, "Set tear on with MIPI_DSI_DCS_SHORT_WRITE_PARAM\n");
			u8 d2[2] = { 0x35, 0x00 };
			mipi_dsi_dcs_write_buffer_multi(&dsi_ctx, d2, 2);
			break;
		default:
			dev_info(dev, "Set tear on with function\n");
			mipi_dsi_dcs_set_tear_on_multi(&dsi_ctx, MIPI_DSI_DCS_TEAR_MODE_VBLANK);
			break;
	}
	mipi_dsi_dcs_exit_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 150);
	mipi_dsi_dcs_set_display_on_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 50);

	return dsi_ctx.accum_err;
}

static int innolux_td4328_disable(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	struct mipi_dsi_multi_context dsi_ctx = { .dsi = ctx->dsi };

	ctx->dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	mipi_dsi_dcs_set_display_off_multi(&dsi_ctx);
	mipi_dsi_usleep_range(&dsi_ctx, 5000, 6000);
	mipi_dsi_dcs_enter_sleep_mode_multi(&dsi_ctx);
	mipi_dsi_msleep(&dsi_ctx, 83);

	return dsi_ctx.accum_err;
}

static int innolux_td4328_prepare(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	gpiod_set_value_cansleep(ctx->enable_gpio, 1);

	innolux_td4328_reset(ctx);

	return 0;
}

static int innolux_td4328_unprepare(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);

	gpiod_set_value_cansleep(ctx->enable_gpio, 0);
	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	return 0;
}

static const struct drm_display_mode innolux_td4328_mode = {
	.clock = (1080 + 60 + 10 + 60) * (1920 + 20 + 8 + 20) * 60 / 1000,
	.hdisplay = 1080,
	.hsync_start = 1080 + 60,
	.hsync_end = 1080 + 60 + 10,
	.htotal = 1080 + 60 + 10 + 60,
	.vdisplay = 1920,
	.vsync_start = 1920 + 20,
	.vsync_end = 1920 + 20 + 8,
	.vtotal = 1920 + 20 + 8 + 20,
	.width_mm = 75,
	.height_mm = 132,
	.type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED,
};

static int innolux_td4328_get_modes(struct drm_panel *panel,
				    struct drm_connector *connector)
{
	/*
	 * TODO: Remove once all drm drivers call
	 * drm_connector_set_orientation_from_panel()
	 */
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	drm_connector_set_panel_orientation(connector, ctx->orientation);

	return drm_connector_helper_get_modes_fixed(connector, &innolux_td4328_mode);
}

static enum drm_panel_orientation innolux_td4328_get_orientation(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);

	return ctx->orientation;
}

static const struct drm_panel_funcs innolux_td4328_panel_funcs = {
	.prepare = innolux_td4328_prepare,
	.enable = innolux_td4328_enable,
	.disable = innolux_td4328_disable,
	.unprepare = innolux_td4328_unprepare,
	.get_modes = innolux_td4328_get_modes,
	.get_orientation = innolux_td4328_get_orientation,
};

static int innolux_td4328_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct innolux_td4328 *ctx;
	int ret;

	ctx = devm_kzalloc(dev, sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	ctx->supplies[0].supply = "vddio";
	ctx->supplies[1].supply = "vddpos";
	ctx->supplies[2].supply = "vddneg";
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(ctx->supplies),
				      ctx->supplies);
	if (ret < 0)
		return dev_err_probe(dev, ret, "Failed to get regulators\n");

	ret = regulator_set_voltage(ctx->supplies[1].consumer,
				    5500000, 5500000);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request 5.5v for vddpos\n");
	ret = regulator_set_voltage(ctx->supplies[2].consumer,
				    5500000, 5500000);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request 5.5v for vddneg\n");

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

	ctx->enable_gpio = devm_gpiod_get(dev, "enable", GPIOD_ASIS);
	if (IS_ERR(ctx->enable_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->enable_gpio),
				     "Failed to get enable-gpios\n");

	ret = of_drm_get_panel_orientation(dev->of_node, &ctx->orientation);
	if (ret < 0) {
		dev_err(dev, "Failed to get orientation %d\n", ret);
		return ret;
	}

	ctx->dsi = dsi;
	mipi_dsi_set_drvdata(dsi, ctx);

	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_NO_EOT_PACKET |
			  MIPI_DSI_CLOCK_NON_CONTINUOUS;

	drm_panel_init(&ctx->panel, dev, &innolux_td4328_panel_funcs,
		       DRM_MODE_CONNECTOR_DSI);
	ctx->panel.prepare_prev_first = true;

	ret = drm_panel_of_backlight(&ctx->panel);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to get backlight\n");

	drm_panel_add(&ctx->panel);

	ret = mipi_dsi_attach(dsi);
	if (ret < 0) {
		drm_panel_remove(&ctx->panel);
		return dev_err_probe(dev, ret, "Failed to attach to DSI host\n");
	}

	return 0;
}

static void innolux_td4328_remove(struct mipi_dsi_device *dsi)
{
	struct innolux_td4328 *ctx = mipi_dsi_get_drvdata(dsi);
	int ret;

	ret = mipi_dsi_detach(dsi);
	if (ret < 0)
		dev_err(&dsi->dev, "Failed to detach from DSI host: %d\n", ret);

	drm_panel_remove(&ctx->panel);
}

static const struct of_device_id innolux_td4328_of_match[] = {
	{ .compatible = "innolux,td4328" }, // FIXME
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, innolux_td4328_of_match);

static struct mipi_dsi_driver innolux_td4328_driver = {
	.probe = innolux_td4328_probe,
	.remove = innolux_td4328_remove,
	.driver = {
		.name = "panel-innolux-td4328",
		.of_match_table = innolux_td4328_of_match,
	},
};
module_mipi_dsi_driver(innolux_td4328_driver);

MODULE_AUTHOR("linux-mdss-dsi-panel-driver-generator <fix@me>"); // FIXME
MODULE_DESCRIPTION("DRM driver for TD4328 cmd mode dsi panel without DSC");
MODULE_LICENSE("GPL");
