/*
 * Copyright (C) 2018 Hugh Cole-Baker
 *
 * Hugh Cole-Baker <sigmaris@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 */

#include <linux/module.h>

#include <drm/drmP.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>

struct vga666 {
	struct drm_bridge	bridge;
	struct drm_connector	connector;
};

static inline struct vga666 *
drm_bridge_to_vga666(struct drm_bridge *bridge)
{
	return container_of(bridge, struct vga666, bridge);
}

static int vga666_get_modes(struct drm_connector *connector)
{
	int ret;
	/*
	 * Since there is no DDC connection, use XGA standard modes
	 */
	ret = drm_add_modes_noedid(connector, 1920, 1200);

	/* And prefer a mode pretty much anyone can handle */
	drm_set_preferred_mode(connector, 1024, 768);

	return ret;
}

static const struct drm_connector_helper_funcs vga666_con_helper_funcs = {
	.get_modes	= vga666_get_modes,
};

static enum drm_connector_status
vga666_connector_detect(struct drm_connector *connector, bool force)
{
	return connector_status_connected;
}

static const struct drm_connector_funcs vga666_con_funcs = {
	.detect			= vga666_connector_detect,
	.fill_modes		= drm_helper_probe_single_connector_modes,
	.destroy		= drm_connector_cleanup,
	.reset			= drm_atomic_helper_connector_reset,
	.atomic_duplicate_state	= drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state	= drm_atomic_helper_connector_destroy_state,
};

static int vga666_attach(struct drm_bridge *bridge)
{
	struct vga666 *vga = drm_bridge_to_vga666(bridge);
	u32 bus_format = MEDIA_BUS_FMT_RGB666_1X18;
	int ret;

	if (!bridge->encoder) {
		DRM_ERROR("Missing encoder\n");
		return -ENODEV;
	}

	drm_connector_helper_add(&vga->connector,
				 &vga666_con_helper_funcs);
	ret = drm_connector_init(bridge->dev, &vga->connector,
				 &vga666_con_funcs, DRM_MODE_CONNECTOR_VGA);
	if (ret) {
		DRM_ERROR("Failed to initialize connector\n");
		return ret;
	}
	ret = drm_display_info_set_bus_formats(&vga->connector.display_info,
					       &bus_format, 1);
	if (ret) {
		DRM_ERROR("Failed to set bus format\n");
		return ret;
	}

	drm_mode_connector_attach_encoder(&vga->connector,
					  bridge->encoder);

	return 0;
}

static const struct drm_bridge_funcs vga666_bridge_funcs = {
	.attach		= vga666_attach,
};

static int vga666_probe(struct platform_device *pdev)
{
	struct vga666 *vga;

	vga = devm_kzalloc(&pdev->dev, sizeof(*vga), GFP_KERNEL);
	if (!vga)
		return -ENOMEM;
	platform_set_drvdata(pdev, vga);

	vga->bridge.funcs = &vga666_bridge_funcs;
	vga->bridge.of_node = pdev->dev.of_node;

	drm_bridge_add(&vga->bridge);

	return 0;
}

static int vga666_remove(struct platform_device *pdev)
{
	struct vga666 *vga = platform_get_drvdata(pdev);

	drm_bridge_remove(&vga->bridge);

	return 0;
}

static const struct of_device_id vga666_match[] = {
	{ .compatible = "fenlogic,vga666" },
	{},
};
MODULE_DEVICE_TABLE(of, vga666_match);

static struct platform_driver vga666_driver = {
	.probe	= vga666_probe,
	.remove	= vga666_remove,
	.driver		= {
		.name		= "vga666",
		.of_match_table	= vga666_match,
	},
};
module_platform_driver(vga666_driver);

MODULE_AUTHOR("Hugh Cole-Baker <sigmaris@gmail.com>");
MODULE_DESCRIPTION("VGA666 DPI-to-VGA bridge driver");
MODULE_LICENSE("GPL");
