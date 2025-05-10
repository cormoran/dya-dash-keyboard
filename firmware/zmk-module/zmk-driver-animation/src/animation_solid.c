/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_solid

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct animation_solid_config {
    size_t *pixel_map;
    size_t pixel_map_size;
    struct zmk_color_hsl *colors;
    uint8_t num_colors;
    uint16_t duration;
    uint16_t transition_duration;
};

struct animation_solid_data {
    uint32_t counter;
    uint16_t animation_counter;

    struct zmk_color_hsl current_hsl;
    struct zmk_color_rgb current_rgb;
};

static void animation_solid_update_color(const struct device *dev) {
    const struct animation_solid_config *config = dev->config;
    struct animation_solid_data *data           = dev->data;

    const size_t from = data->animation_counter / config->transition_duration;
    const size_t to   = (from + 1) % config->num_colors;

    struct zmk_color_hsl next_hsl;

    zmk_interpolate_hsl(
        &config->colors[from], &config->colors[to], &next_hsl,
        (data->animation_counter % config->transition_duration) /
            (float)config->transition_duration);

    data->current_hsl = next_hsl;
    zmk_hsl_to_rgb(&data->current_hsl, &data->current_rgb);

    data->animation_counter = (data->animation_counter + 1) % config->duration;
}

static void animation_solid_render_frame(const struct device *dev,
                                         struct animation_pixel *pixels,
                                         size_t num_pixels) {
    const struct animation_solid_config *config = dev->config;
    struct animation_solid_data *data           = dev->data;

    uint32_t counter = data->counter;
    if (counter == 0) {
        return;
    }

    for (size_t i = 0; i < config->pixel_map_size; ++i) {
        pixels[config->pixel_map[i]].value = data->current_rgb;
    }

    if (config->num_colors == 1 &&
        data->counter == ANIMATION_DURATION_FOREVER) {
        // optimization to stop render frame if animation is forever
        return;
    }

    if (counter < ANIMATION_DURATION_FOREVER) {
        data->counter = counter - 1;
        zmk_animation_request_frames_if_required(data->counter, false);
    }

    animation_solid_update_color(dev);
}

static void animation_solid_start(const struct device *dev,
                                  uint32_t request_duration_ms) {
    // TODO: support request_duration_ms
    struct animation_solid_data *data = dev->data;
    data->counter                     = request_duration_ms == 0
                                            ? ANIMATION_DURATION_FOREVER
                                            : ANIMATION_DURATION_MS_TO_FRAMES(request_duration_ms);
    data->animation_counter           = 0;
    if (data->counter == ANIMATION_DURATION_FOREVER) {
        // optimization to reduce render frame if animation is forever
        zmk_animation_request_frames(1);
    } else {
        zmk_animation_request_frames_if_required(data->counter, true);
    }
    LOG_INF("Start animation solid");
}

static void animation_solid_stop(const struct device *dev) {
    struct animation_solid_data *data = dev->data;
    data->counter                     = 0;
    LOG_INF("Stop animation solid");
}

static bool animation_solid_is_finished(const struct device *dev) {
    struct animation_solid_data *data = dev->data;
    return data->counter == 0;
}

static int animation_solid_init(const struct device *dev) {
    const struct animation_solid_config *config = dev->config;
    struct animation_solid_data *data           = dev->data;

    data->current_hsl = config->colors[0];

    zmk_hsl_to_rgb(&data->current_hsl, &data->current_rgb);

    return 0;
}

static const struct animation_api animation_solid_api = {
    .on_start     = animation_solid_start,
    .on_stop      = animation_solid_stop,
    .render_frame = animation_solid_render_frame,
    .is_finished  = animation_solid_is_finished,
};

#define ANIMATION_SOLID_DEVICE(idx)                                           \
                                                                              \
    static struct animation_solid_data animation_solid_##idx##_data;          \
                                                                              \
    static size_t animation_ripple_##idx##_pixel_map[] =                      \
        DT_INST_PROP(idx, pixels);                                            \
                                                                              \
    static uint32_t animation_solid_##idx##_colors[] =                        \
        DT_INST_PROP(idx, colors);                                            \
                                                                              \
    static struct animation_solid_config animation_solid_##idx##_config = {   \
        .pixel_map      = &animation_ripple_##idx##_pixel_map[0],             \
        .pixel_map_size = DT_INST_PROP_LEN(idx, pixels),                      \
        .colors     = (struct zmk_color_hsl *)animation_solid_##idx##_colors, \
        .num_colors = DT_INST_PROP_LEN(idx, colors),                          \
        .duration   = DT_INST_PROP(idx, duration) * CONFIG_ZMK_ANIMATION_FPS, \
        .transition_duration =                                                \
            (DT_INST_PROP(idx, duration) * CONFIG_ZMK_ANIMATION_FPS) /        \
            DT_INST_PROP_LEN(idx, colors),                                    \
    };                                                                        \
                                                                              \
    DEVICE_DT_INST_DEFINE(                                                    \
        idx, &animation_solid_init, NULL, &animation_solid_##idx##_data,      \
        &animation_solid_##idx##_config, POST_KERNEL,                         \
        CONFIG_APPLICATION_INIT_PRIORITY, &animation_solid_api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_SOLID_DEVICE);
