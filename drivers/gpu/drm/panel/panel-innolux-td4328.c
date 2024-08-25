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
	enum drm_panel_orientation orientation;
	bool prepared;
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

static int innolux_td4328_on(struct innolux_td4328 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags |= MIPI_DSI_MODE_LPM;

	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xc2,
				   0x01, 0xf7, 0x80, 0x04, 0x68, 0x08, 0x09,
				   0x10, 0x00, 0x08, 0x30, 0x00, 0x00, 0x00,
				   0x00, 0x00, 0x00, 0x00, 0x02, 0x80, 0x00,
				   0x00, 0x00);
	mipi_dsi_generic_write_seq(dsi, 0xd6, 0x01);
	mipi_dsi_generic_write_seq(dsi, 0xb0, 0x03);

	ret = mipi_dsi_dcs_set_column_address(dsi, 0x0000, 0x0437);
	if (ret < 0) {
		dev_err(dev, "Failed to set column address: %d\n", ret);
		return ret;
	}

	ret = mipi_dsi_dcs_set_page_address(dsi, 0x0000, 0x077f);
	if (ret < 0) {
		dev_err(dev, "Failed to set page address: %d\n", ret);
		return ret;
	}

	mipi_dsi_dcs_write_seq(dsi, 0x35, 0x00);

	ret = mipi_dsi_dcs_exit_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to exit sleep mode: %d\n", ret);
		return ret;
	}
	msleep(150);

	ret = mipi_dsi_dcs_set_display_on(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display on: %d\n", ret);
		return ret;
	}
	msleep(50);

	return 0;
}

static int innolux_td4328_off(struct innolux_td4328 *ctx)
{
	struct mipi_dsi_device *dsi = ctx->dsi;
	struct device *dev = &dsi->dev;
	int ret;

	dsi->mode_flags &= ~MIPI_DSI_MODE_LPM;

	ret = mipi_dsi_dcs_set_display_off(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to set display off: %d\n", ret);
		return ret;
	}
	usleep_range(5000, 6000);

	ret = mipi_dsi_dcs_enter_sleep_mode(dsi);
	if (ret < 0) {
		dev_err(dev, "Failed to enter sleep mode: %d\n", ret);
		return ret;
	}
	msleep(83);

	return 0;
}

static int innolux_td4328_prepare(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (ctx->prepared)
		return 0;

	ret = regulator_bulk_enable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
	if (ret < 0) {
		dev_err(dev, "Failed to enable regulators: %d\n", ret);
		return ret;
	}

	innolux_td4328_reset(ctx);

	ret = innolux_td4328_on(ctx);
	if (ret < 0) {
		dev_err(dev, "Failed to initialize panel: %d\n", ret);
		gpiod_set_value_cansleep(ctx->reset_gpio, 1);
		regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);
		return ret;
	}

	ctx->prepared = true;
	return 0;
}

static int innolux_td4328_unprepare(struct drm_panel *panel)
{
	struct innolux_td4328 *ctx = to_innolux_td4328(panel);
	struct device *dev = &ctx->dsi->dev;
	int ret;

	if (!ctx->prepared)
		return 0;

	ret = innolux_td4328_off(ctx);
	if (ret < 0)
		dev_err(dev, "Failed to un-initialize panel: %d\n", ret);

	gpiod_set_value_cansleep(ctx->reset_gpio, 1);
	regulator_bulk_disable(ARRAY_SIZE(ctx->supplies), ctx->supplies);

	ctx->prepared = false;
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

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio))
		return dev_err_probe(dev, PTR_ERR(ctx->reset_gpio),
				     "Failed to get reset-gpios\n");

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
