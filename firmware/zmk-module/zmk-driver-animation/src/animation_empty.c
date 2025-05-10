/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_empty

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static void animation_empty_render_frame(const struct device *dev,
                                         struct animation_pixel *pixels,
                                         size_t num_pixels) {
    for (int i = 0; i < num_pixels; i++) {
        pixels[i].value.r = 0;
        pixels[i].value.g = 0;
        pixels[i].value.b = 0;
    }
}

static void animation_empty_start(const struct device *dev,
                                  uint32_t request_duration_ms) {
    // request_duration_ms is not supported and runs forever
    zmk_animation_request_frames(1);
}

static void animation_empty_stop(const struct device *dev) {}

static bool animation_empty_is_finished(const struct device *dev) {
    return false;  // never finishes
}

static int animation_empty_init(const struct device *dev) {
    // workaround to make this device not ready
    return -ENXIO;
}

static const struct animation_api animation_empty_api = {
    .on_start     = animation_empty_start,
    .on_stop      = animation_empty_stop,
    .render_frame = animation_empty_render_frame,
    .is_finished  = animation_empty_is_finished,
};

#define ANIMATION_EMPTY_DEVICE(idx)                                      \
                                                                         \
    DEVICE_DT_INST_DEFINE(idx, &animation_empty_init, NULL, NULL, NULL,  \
                          POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY, \
                          &animation_empty_api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_EMPTY_DEVICE);
