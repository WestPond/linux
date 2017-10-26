/*
 * LEDs driver for Systech SL500
 *
 * Copyright (C) 2011 Westpond Tech.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/leds.h>
#include <linux/gpio.h>

#define LED_STATUS_GREEN	(0x40 + 6)		/* AT91_PIN_PC6 */
#define LED_STATUS_RED		(0x40 + 7)		/* AT91_PIN_PC7 */
#define LED_USB1_GREEN		(0x40 + 13)		/* AT91_PIN_PC13 */
#define LED_USB1_RED		(0x40 + 15)		/* AT91_PIN_PC15 */
#define LED_USB2_GREEN		(0x40 + 11)		/* AT91_PIN_PC11 */
#define LED_USB2_RED		(0x40 + 12)		/* AT91_PIN_PC12 */
#define LED_MODE_A			(0x40 + 30)		/* AT91_PIN_PC30 */
#define LED_MODE_B			(0x40 + 21)		/* AT91_PIN_PC21 */
#define LED_MODE_C			(0x40 + 20)		/* AT91_PIN_PC20 */
#define LED_SIGNAL_1		(0x00 + 27)		/* AT91_PIN_PA27 */
#define LED_SIGNAL_2		(0x00 + 26)		/* AT91_PIN_PA26 */
#define LED_SIGNAL_3		(0x00 + 25)		/* AT91_PIN_PA25 */
#define LED_SIGNAL_4		(0x00 + 24)		/* AT91_PIN_PA24 */

static void sl500_status_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	if(value==0) {
		gpio_set_value(LED_STATUS_RED, 0);
		gpio_set_value(LED_STATUS_GREEN, 0);
	} else if (value==1) {
		gpio_set_value(LED_STATUS_RED, 1);
		gpio_set_value(LED_STATUS_GREEN, 0);
	} else if (value==2) {
		gpio_set_value(LED_STATUS_RED, 1);
		gpio_set_value(LED_STATUS_GREEN, 1);
	} else {
		gpio_set_value(LED_STATUS_RED, 0);
		gpio_set_value(LED_STATUS_GREEN, 1);
	}
}

static void sl500_usb_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	if(value==0) {
		gpio_set_value(LED_USB1_RED, 0);
		gpio_set_value(LED_USB1_GREEN, 0);
	} else if (value==1) {
		gpio_set_value(LED_USB1_RED, 1);
		gpio_set_value(LED_USB1_GREEN, 0);
	} else if (value==2) {
		gpio_set_value(LED_USB1_RED, 1);
		gpio_set_value(LED_USB1_GREEN, 1);
	} else {
		gpio_set_value(LED_USB1_RED, 0);
		gpio_set_value(LED_USB1_GREEN, 1);
	}
}

static void sl500_lock_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	gpio_set_value(LED_MODE_A, (value>0?0:1));
}

static void sl500_data_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	gpio_set_value(LED_MODE_B, (value>0?0:1));
}

static void sl500_network_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	gpio_set_value(LED_MODE_C, (value>0?0:1));
}

static void sl500_signal_led_set(struct led_classdev *led_cdev,
		enum led_brightness value)
{
	gpio_set_value(LED_SIGNAL_1, (value>0?0:1));
	gpio_set_value(LED_SIGNAL_2, (value>1?0:1));
	gpio_set_value(LED_SIGNAL_3, (value>2?0:1));
	gpio_set_value(LED_SIGNAL_4, (value>3?0:1));
}

static struct led_classdev sl500_leds[] = {
	{
		.name		= "status",
		.brightness_set = sl500_status_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
	{
		.name		= "usb",
		.brightness_set = sl500_usb_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
	{
		.name		= "lock",
		.brightness_set = sl500_lock_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
	{
		.name		= "data",
		.brightness_set = sl500_data_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
	{
		.name		= "network",
		.brightness_set = sl500_network_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
	{
		.name		= "signal",
		.brightness_set = sl500_signal_led_set,
		.flags		= LED_CORE_SUSPENDRESUME,
	},
};

static struct gpio sl500_led_gpio[] = {
	{
		.gpio = LED_STATUS_GREEN,
		.flags = GPIOF_OUT_INIT_LOW,
		.label = "Sl500_LEDS_STATUS_GREEN",
	},
	{
		.gpio = LED_STATUS_RED,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_STATUS_RED",
	},
	{
		.gpio = LED_USB1_GREEN,
		.flags = GPIOF_OUT_INIT_LOW,
		.label = "SL500_LEDS_USB_GREEN",
	},
	{
		.gpio = LED_USB1_RED,
		.flags = GPIOF_OUT_INIT_LOW,
		.label = "SL500_LEDS_USB_RED",
	},
	{
		.gpio = LED_MODE_A,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_MODE_A",
	},
	{
		.gpio = LED_MODE_B,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_MODE_B",
	},
	{
		.gpio = LED_MODE_C,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_MODE_C",
	},
	{
		.gpio = LED_SIGNAL_1,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_SIGNAL_1",
	},
	{
		.gpio = LED_SIGNAL_2,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_SIGNAL_2",
	},
	{
		.gpio = LED_SIGNAL_3,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_SIGNAL_3",
	},
	{
		.gpio = LED_SIGNAL_4,
		.flags = GPIOF_OUT_INIT_HIGH,
		.label = "SL500_LEDS_SIGNAL_4",
	},
};

static int sl500_led_probe(struct platform_device *pdev)
{
	int i, ret;

	ret = gpio_request_array(sl500_led_gpio, ARRAY_SIZE(sl500_led_gpio));
	if(ret<0)
		return ret;

	for (i = 0; i < ARRAY_SIZE(sl500_leds); i++) {
		ret = led_classdev_register(&pdev->dev, &sl500_leds[i]);
		if (ret < 0)
			goto fail;
	}

	return 0;

fail:
	while (--i >= 0)
		led_classdev_unregister(&sl500_leds[i]);
	gpio_free_array(sl500_led_gpio, ARRAY_SIZE(sl500_led_gpio));
	return ret;
}

static int sl500_led_remove(struct platform_device *pdev)
{
	int i;

	gpio_free_array(sl500_led_gpio, ARRAY_SIZE(sl500_led_gpio));

	for (i = 0; i < ARRAY_SIZE(sl500_leds); i++)
		led_classdev_unregister(&sl500_leds[i]);

	return 0;
}

static const struct of_device_id of_sl500_leds_match[] = {
	{ .compatible = "systech,sl500-leds", },
	{},
};

static struct platform_driver sl500_led_driver = {
	.probe		= sl500_led_probe,
	.remove		= sl500_led_remove,
	.driver		= {
		.name		= "sl500-leds",
		.owner		= THIS_MODULE,
		.of_match_table	= of_match_ptr(of_sl500_leds_match),
	},
};

module_platform_driver(sl500_led_driver);

MODULE_AUTHOR("Nathan Ford <nford@westpond.com>");
MODULE_DESCRIPTION("SL500 LED driver");
MODULE_LICENSE("GPL");
