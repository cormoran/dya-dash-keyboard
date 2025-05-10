/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_led_stripnize

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define INDEX_R 0
#define INDEX_G 0
#define INDEX_B 0

struct led_stripnize_config {
  const struct device *led;
  const uint8_t *rgb_index;
};

static int led_set_value(const struct device *dev, uint8_t index, bool value) {
  const struct led_stripnize_config *config = dev->config;
  if (value) {
    return led_on(config->led, config->rgb_index[index]);
  } else {
    return led_off(config->led, config->rgb_index[index]);
  }
}

static int led_stripnize_update_rgb(const struct device *dev,
                                    struct led_rgb *pixels, size_t num_pixels) {
  const struct led_stripnize_config *config = dev->config;
  if (num_pixels > 0) {
    LOG_DBG("Updating RGB: R=%d, G=%d, B=%d", pixels[0].r, pixels[0].g,
            pixels[0].b);
    led_set_value(dev, INDEX_R, pixels[0].r > 0);
    led_set_value(dev, INDEX_G, pixels[0].g > 0);
    led_set_value(dev, INDEX_B, pixels[0].b > 0);
  }
  return 0;
}

static int led_stripnize_update_channels(const struct device *dev,
                                         uint8_t *channels,
                                         size_t num_channels) {
  if (num_channels > 0) {
    led_set_value(dev, INDEX_R, channels[0] > 0);
    led_set_value(dev, INDEX_G, channels[0] > 0);
    led_set_value(dev, INDEX_B, channels[0] > 0);
  }
  return 0;
}

static int led_stripnize_init(const struct device *dev) { return 0; }

static const struct led_strip_driver_api api = {
    .update_rgb = led_stripnize_update_rgb,
    .update_channels = led_stripnize_update_channels,
};

#define LED_STRIPNIZE_DEVICE(idx)                                              \
  static const uint8_t led_stripnize_##idx##_rgb_index[] = {                   \
      DT_INST_PROP(idx, red_index),                                            \
      DT_INST_PROP(idx, green_index),                                          \
      DT_INST_PROP(idx, blue_index),                                           \
  };                                                                           \
  static const struct led_stripnize_config led_stripnize_##idx##_config = {    \
      .led = DEVICE_DT_GET(DT_INST_PHANDLE(idx, led)),                         \
      .rgb_index = led_stripnize_##idx##_rgb_index,                            \
  };                                                                           \
                                                                               \
  DEVICE_DT_INST_DEFINE(idx, led_stripnize_init, NULL, NULL,                   \
                        &led_stripnize_##idx##_config, POST_KERNEL,            \
                        CONFIG_APPLICATION_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(LED_STRIPNIZE_DEVICE)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
