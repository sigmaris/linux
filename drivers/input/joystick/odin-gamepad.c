// SPDX-License-Identifier: GPL-2.0
/*
 * AYN Odin ADC joysticks and GPIO buttons driver.
 * Copyright (c) 2022 Teguh Sobirin <teguh@sobir.in>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/gpio_keys.h>
#include <linux/iio/consumer.h>
#include <linux/iio/types.h>
#include <linux/property.h>
#include <linux/of_gpio.h>
#include <linux/delay.h>

struct odin_button_config {
	const char *const name;
	const int code;
	const bool recenter_combo;
};

static const struct odin_button_config gpio_buttons[] = {
	{ .name = "north-btn",   .code = BTN_NORTH, },
	{ .name = "east-btn",    .code = BTN_EAST, },
	{ .name = "south-btn",   .code = BTN_SOUTH, },
	{ .name = "west-btn",    .code = BTN_WEST, },
	{ .name = "dpad-up",     .code = BTN_DPAD_UP, },
	{ .name = "dpad-down",   .code = BTN_DPAD_DOWN, },
	{ .name = "dpad-left",   .code = BTN_DPAD_LEFT, },
	{ .name = "dpad-right",  .code = BTN_DPAD_RIGHT, },
	{ .name = "l1-btn",      .code = BTN_TL, },
	{ .name = "r1-btn",      .code = BTN_TR, },
	{ .name = "rear-l-btn",  .code = BTN_TL2,    .recenter_combo = true, },
	{ .name = "rear-r-btn",  .code = BTN_TR2,    .recenter_combo = true, },
	{ .name = "thumb-l-btn", .code = BTN_THUMBL, },
	{ .name = "thumb-r-rtn", .code = BTN_THUMBR, },
	{ .name = "start-btn",   .code = BTN_START,  .recenter_combo = true, },
	{ .name = "select-btn",  .code = BTN_SELECT, .recenter_combo = true, },
	{ .name = "home-btn",    .code = BTN_MODE, },
};
// TODO: static assert num buttons is <= num_bits(long)

struct odin_axis_config {
	const char *const name;
	const int report_type;
	const bool is_trigger;
};

static const struct odin_axis_config adc_axes[] = {
	{ .name = "x-axis",     .report_type = ABS_X, },
	{ .name = "y-axis",     .report_type = ABS_Y, },
	{ .name = "rx-axis",    .report_type = ABS_RX, },
	{ .name = "ry-axis",    .report_type = ABS_RY, },
	{ .name = "r2-trigger", .report_type = ABS_HAT2X, .is_trigger = true, },
	{ .name = "l2-trigger", .report_type = ABS_HAT2Y, .is_trigger = true, },
};

struct odin_axis {
	const struct odin_axis_config *config;
	struct iio_channel *channel;
	u32 range[2];
	u32 rest_pos;
	bool invert;
	u32 fuzz;
	u32 flat;
};

struct odin_button {
	const struct odin_button_config *config;
	struct gpio_desc *gpiod;
};

struct odin_gamepad {
	struct device *dev;
	struct input_dev *input;

	struct odin_axis *axes;
	struct odin_button *btns;

	// Bitmap of buttons in recenter combo
	unsigned long recenter_combo;
	// Which buttons in combo are pressed now
	unsigned long combo_btns_pressed;
};

static void odin_gamepad_poll(struct input_dev *input)
{
	struct odin_gamepad *gamepad = input_get_drvdata(input);
	int i, ret, value;
	bool recenter = false;

	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++) {
		struct odin_button *btn = &gamepad->btns[i];

		value = gpiod_get_value_cansleep(btn->gpiod);
		input_event(input, EV_KEY, btn->config->code, value);
		if (btn->config->recenter_combo) {
			// Check if any of the recenter buttons were just pressed:
			if (value)
				recenter |= !__test_and_set_bit(i, &gamepad->combo_btns_pressed);
			else
				__clear_bit(i, &gamepad->combo_btns_pressed);
		}
	}

	// Only recenter if all combo buttons are pressed (and one was just pressed)
	recenter &= (gamepad->combo_btns_pressed == gamepad->recenter_combo);

	for (i = 0; i < ARRAY_SIZE(adc_axes); i++) {
		struct odin_axis *axis = &gamepad->axes[i];

		ret = iio_read_channel_processed(axis->channel, &value);
		if (unlikely(ret < 0)) {
			continue;
		}

		if (recenter)
			axis->rest_pos = value;

		value = value - axis->rest_pos;
		if (axis->invert)
			value = -value;

		input_report_abs(input, axis->config->report_type, value);
	}

	if (recenter)
		dev_info(gamepad->dev, "Recentered axes\n");

	input_sync(input);
}

static int gamepad_setup_one_axis(struct odin_gamepad *gamepad, struct odin_axis *axis,
				  struct fwnode_handle *fw_node)
{
	int ret, range;

	axis->channel = devm_iio_channel_get(gamepad->dev, axis->config->name);
	if (IS_ERR(axis->channel))
		return dev_err_probe(gamepad->dev, PTR_ERR(axis->channel),
				     "failed to get ADC channel for %s\n", axis->config->name);

	ret = fwnode_property_read_u32(fw_node, "abs-range", &range);
	if (ret < 0)
		return dev_err_probe(gamepad->dev, ret, "missing range for %s\n",
				     axis->config->name);

	axis->invert = fwnode_property_read_bool(fw_node, "inverted");

	ret = iio_read_channel_processed(axis->channel, &axis->rest_pos);
	if (ret < 0) {
		dev_err_ratelimited(gamepad->dev, "failed to read ADC channel %s\n",
				    axis->config->name);
		return ret;
	}

	dev_info(gamepad->dev, "%s: rest_pos=%u invert=%d\n",
		 axis->config->name, axis->rest_pos, axis->invert);

	ret = fwnode_property_read_u32(fw_node, "abs-fuzz", &axis->fuzz);
	if (ret < 0)
		axis->fuzz = 0;

	ret = fwnode_property_read_u32(fw_node, "abs-flat", &axis->flat);
	if (ret < 0)
		axis->flat = 0;

	input_set_abs_params(gamepad->input, axis->config->report_type,
			     axis->config->is_trigger ? 0 : -(range / 2),
			     axis->config->is_trigger ? range : (range / 2),
			     axis->fuzz, axis->flat);
	input_set_capability(gamepad->input, EV_ABS, axis->config->report_type);

	return 0;
}

static int odin_gamepad_setup_axes(struct odin_gamepad *gamepad)
{
	int i, ret;
	dev_info(gamepad->dev, "%s: alloc axes\n", __func__);
	gamepad->axes = devm_kzalloc(gamepad->dev, ARRAY_SIZE(adc_axes) *
				     sizeof(struct odin_axis), GFP_KERNEL);
	if (!gamepad->axes)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(adc_axes); i++) {
		dev_info(gamepad->dev, "%s: work on axis %d\n", __func__, i);
		struct odin_axis *axis = &gamepad->axes[i];
		axis->config = &adc_axes[i];

		dev_info(gamepad->dev, "%s: get child %s\n", __func__, axis->config->name);
		struct fwnode_handle *child = device_get_named_child_node(gamepad->dev,
									  axis->config->name);
		if (!child)
			return dev_err_probe(gamepad->dev, -ENXIO, "No %s node found\n",
					     axis->config->name);

		dev_info(gamepad->dev, "%s: setup one axis %s\n", __func__, axis->config->name);
		ret = gamepad_setup_one_axis(gamepad, axis, child);
		fwnode_handle_put(child);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static int odin_gamepad_setup_buttons(struct odin_gamepad *gamepad)
{
	int i;

	gamepad->btns = devm_kzalloc(gamepad->dev, ARRAY_SIZE(gpio_buttons) *
				     sizeof(struct odin_button), GFP_KERNEL);
	if (!gamepad->btns)
		return -ENOMEM;

	for (i = 0; i < ARRAY_SIZE(gpio_buttons); i++) {
		struct odin_button *btn = &gamepad->btns[i];
		btn->config = &gpio_buttons[i];
		btn->gpiod = devm_gpiod_get(gamepad->dev, btn->config->name, GPIOD_IN);
		// TODO: test missing GPIOs in devicetree
		if (IS_ERR(btn->gpiod))
			return dev_err_probe(gamepad->dev, PTR_ERR(btn->gpiod),
					    "failed to get GPIO for %s\n",
					    btn->config->name);
		
		input_set_capability(gamepad->input, EV_KEY, btn->config->code);

		// If this button is part of recenter combo, record that:
		if (btn->config->recenter_combo)
			__set_bit(i, &gamepad->recenter_combo);
	}

	return 0;
}

static int odin_gamepad_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct odin_gamepad *gamepad;
	struct input_dev *input;
	int error;

	gamepad = devm_kzalloc(dev, sizeof(struct odin_gamepad), GFP_KERNEL);
	if (!gamepad) {
		dev_err(dev, "gamepad devm_kzmalloc error!\n");
		return -ENOMEM;
	}
	gamepad->dev = dev;

	input = devm_input_allocate_device(dev);
	if (!input) {
		dev_err(dev, "failed to allocate input device\n");
		return -ENOMEM;
	}
	gamepad->input = input;

	input->id.bustype = BUS_HOST;
	input->name = "AYN Odin Gamepad";
	input_set_drvdata(input, gamepad);

	error = odin_gamepad_setup_axes(gamepad);
	if (error)
		return error;

	error = odin_gamepad_setup_buttons(gamepad);
	if (error)
		return error;

	dev_info(dev, "%s: setup polling\n", __func__);
	input_setup_polling(input, odin_gamepad_poll);
	input_set_poll_interval(input, 10); // TODO: configurable

	dev_info(dev, "%s: register input_dev\n", __func__);
	error = input_register_device(input);
	if (error) {
		dev_err(dev, "Unable to register input device\n");
		return error;
	}

	dev_info(dev, "%s: success\n", __func__);
	return 0;
}

static const struct of_device_id odin_gamepad_of_match[] = {
	{ .compatible = "ayntec,odin-gamepad", },
	{ /* sentinel */ },
};
MODULE_DEVICE_TABLE(of, odin_gamepad_of_match);

static struct platform_driver odin_gamepad_driver = {
	.probe = odin_gamepad_probe,
	.driver = {
		.name = "odin-gamepad",
		.of_match_table = odin_gamepad_of_match,
	},
};
module_platform_driver(odin_gamepad_driver);

MODULE_DESCRIPTION("AYN Odin ADC joysticks and GPIO buttons driver");
MODULE_AUTHOR("Teguh Sobirin <teguh@sobir.in>");
MODULE_LICENSE("GPL");
