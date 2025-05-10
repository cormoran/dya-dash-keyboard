/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/types.h>
#include <zephyr/device.h>
#include <zephyr/drivers/led_strip.h>
#include <zmk_driver_animation/color.h>

/**
 * @file
 * #brief Public API for controlling animations.
 *
 * This library abstracts the implementation details
 * for various types of 2D animations.
 */

#define ANIMATION_DURATION_FOREVER UINT32_MAX
#define ANIMATION_DURATION_MS_TO_FRAMES(ms) \
    (ms == ANIMATION_DURATION_FOREVER ? ms  \
                                      : ms * CONFIG_ZMK_ANIMATION_FPS / 1000)

struct animation_pixel {
    const uint8_t position_x;
    const uint8_t position_y;

    struct zmk_color_rgb value;
};

/**
 * @typedef animation_start
 * @brief Callback API for starting an animation.
 *
 * @see animation_start() for argument descriptions.
 */
typedef void (*animation_api_start)(const struct device *dev,
                                    uint32_t request_duration_ms);

/**
 * @typedef animation_stop
 * @brief Callback API for stopping an animation.
 *
 * @see animation_stop() for argument descriptions.
 */
typedef void (*animation_api_stop)(const struct device *dev);

/**
 * @typedef animation_render_frame
 * @brief Callback API for generating the next animation frame.
 *
 * @see animation_render_frame() for argument descriptions.
 */
typedef void (*animation_api_render_frame)(const struct device *dev,
                                           struct animation_pixel *pixels,
                                           size_t num_pixels);

/**
 * @typedef animation_api_is_finished
 * @brief Callback API to check whether the started animation finished or not
 */
typedef bool (*animation_api_is_finished)(const struct device *dev);

struct animation_api {
    animation_api_start on_start;
    animation_api_stop on_stop;
    animation_api_render_frame render_frame;
    animation_api_is_finished is_finished;
};

/**
 * @param request_duration_ms Duration of the animation expected to be played in
 * milliseconds. It's hint for expectation and animation can extend/shorten
 * duration. ANIMATION_DURATION_FOREVER if animation should run forever.
 */
static inline void animation_start(const struct device *dev,
                                   uint32_t request_duration_ms) {
    const struct animation_api *api = (const struct animation_api *)dev->api;

    return api->on_start(dev, request_duration_ms);
}

static inline void animation_stop(const struct device *dev) {
    const struct animation_api *api = (const struct animation_api *)dev->api;

    return api->on_stop(dev);
}

static inline void animation_render_frame(const struct device *dev,
                                          struct animation_pixel *pixels,
                                          size_t num_pixels) {
    const struct animation_api *api = (const struct animation_api *)dev->api;

    return api->render_frame(dev, pixels, num_pixels);
}

static inline bool animation_is_finished(const struct device *dev) {
    const struct animation_api *api = (const struct animation_api *)dev->api;

    return api->is_finished(dev);
}
