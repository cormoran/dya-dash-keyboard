/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_layer_status

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>
#include <zmk_driver_animation/drivers/animation_control.h>
#include <zmk_driver_animation/drivers/animation_layer_status.h>
#include <dt-bindings/zmk_driver_animation/animation_layer_status.h>

#define IS_CENTRAL \
    (!IS_ENABLED(CONFIG_ZMK_SPLIT) || IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))
#define IS_SPLIT_PERIPHERAL \
    (IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL))

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_animation_control)
#error "A zmk,animation_control chosen node must be declared"
#endif

static const struct device *animation_control =
    DEVICE_DT_GET(DT_CHOSEN(zmk_animation_control));

struct animation_layer_status_config {
    size_t *pixel_map;
    size_t pixel_map_size;
    struct zmk_color_hsl *default_color;
    uint8_t layer_offset;
    uint32_t extend_duration;
    struct zmk_color_hsl *colors;
    uint8_t colors_size;
};

struct animation_layer_status_data {
    bool running;
    uint32_t counter;
    uint32_t layer_status;
    uint64_t last_set;
};

#if IS_CENTRAL
static void refresh_layer_status_central(const struct device *dev) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    uint32_t prev                                      = data->layer_status;
    // workaround for ZMK's buggy behavior...
    zmk_keymap_layer_id_t default_layer = zmk_keymap_layer_default();
    data->layer_status = zmk_keymap_layer_state() | BIT(default_layer);
    if (prev != data->layer_status && data->running) {
        LOG_DBG("Layer status changed: %d", data->layer_status);
        if (data->counter < config->extend_duration) {
            data->counter = config->extend_duration;
            zmk_animation_request_frames_if_required(data->counter, true);
        }
    }
}
#endif

#if IS_SPLIT_PERIPHERAL
static void refresh_layer_status_peripheral(const struct device *dev,
                                            uint32_t layer_status) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    uint32_t prev                                      = data->layer_status;
    data->layer_status                                 = layer_status;
    data->last_set                                     = k_uptime_get();
    if (prev != 0 && prev != data->layer_status && data->running) {
        LOG_DBG("Layer status changed: %d", data->layer_status);
        if (data->counter < config->extend_duration) {
            data->counter = config->extend_duration;
            zmk_animation_request_frames_if_required(data->counter, true);
        }
    }
}
#endif

#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
static void sync_layer_status_to_peripheral(const struct device *dev) {
    struct animation_layer_status_data *data = dev->data;
    struct zmk_behavior_binding binding      = {
             .behavior_dev = "animls",
             .param1       = ANIMATION_LAYER_STATUS_CMD_FOR_PERIPHERAL,
             .param2       = data->layer_status,
    };
    struct zmk_behavior_binding_event event = {
        .layer     = 0,
        .position  = 0,
        .timestamp = k_uptime_get(),
        .source    = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
    };
    zmk_behavior_invoke_binding(&binding, event, false);
}
#endif

static void animation_layer_status_render_frame(const struct device *dev,
                                                struct animation_pixel *pixels,
                                                size_t num_pixels) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    uint32_t counter                                   = data->counter;
    if (counter == 0) {
        return;
    }
    struct zmk_color_rgb black = {};
    struct zmk_color_rgb default_rgb;
    struct zmk_color_hsl default_hsl = {};
    zmk_hsl_to_rgb(config->default_color, &default_rgb);
    for (size_t i = 0; i < config->pixel_map_size; i++) {
        uint8_t idx = i + config->layer_offset;
        if (data->layer_status & (1 << idx)) {
            if (idx < config->colors_size &&
                (config->colors[idx].h != 0 || config->colors[idx].s != 0 ||
                 config->colors[idx].l != 0)) {
                struct zmk_color_rgb rgb;
                zmk_hsl_to_rgb(&config->colors[idx], &rgb);
                pixels[config->pixel_map[i]].value = rgb;
            } else {
                pixels[config->pixel_map[i]].value = default_rgb;
            }
        } else {
            pixels[config->pixel_map[i]].value = black;
        }
    }
    data->counter = counter - 1;
    zmk_animation_request_frames_if_required(data->counter, false);
    if (data->counter == 0) {
        animation_stop(dev);
    }
}

static void animation_layer_status_start(const struct device *dev,
                                         uint32_t request_duration_ms) {
    // TODO: support request_duration_ms
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    LOG_INF("Start animation layer status");
#if IS_CENTRAL
    // execute before setting counter to avoid duration extension
    refresh_layer_status_central(dev);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    sync_layer_status_to_peripheral(dev);
#else
    // status is synced to peripheral via behavior
    // behavior calls zmk_animation_layer_status_set_status()
    // animls behavior from central can be processed before peripheral reaches
    // here. skip resetting status if it is set recently not to reset latest
    // status.
    if (k_uptime_get() - data->last_set > 1000) {
        zmk_animation_layer_status_set_status(0);
    }
#endif
    data->counter = request_duration_ms == 0
                        ? ANIMATION_DURATION_FOREVER
                        : ANIMATION_DURATION_MS_TO_FRAMES(request_duration_ms);
    data->running = true;
    zmk_animation_request_frames_if_required(data->counter, true);
}

static void animation_layer_status_stop(const struct device *dev) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    LOG_INF("Stop animation layer status");
    data->layer_status = 0;
    data->running      = false;
    data->counter      = 0;
}

static bool animation_layer_status_is_finished(const struct device *dev) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    return !data->running;
}

static int animation_layer_status_init(const struct device *dev) { return 0; }

static const struct animation_api animation_layer_status_api = {
    .on_start     = animation_layer_status_start,
    .on_stop      = animation_layer_status_stop,
    .render_frame = animation_layer_status_render_frame,
    .is_finished  = animation_layer_status_is_finished,
};

static struct animation_layer_status_data animation_layer_status_data = {};

static size_t animation_layer_status_pixel_map[] = DT_INST_PROP(0, pixels);

static uint32_t animation_layer_status_default_color =
    DT_INST_PROP(0, default_color);
static uint32_t animation_layer_status_colors[] = DT_INST_PROP(0, colors);

static struct animation_layer_status_config animation_layer_status_config = {
    .pixel_map      = &animation_layer_status_pixel_map[0],
    .pixel_map_size = DT_INST_PROP_LEN(0, pixels),
    .default_color  = &animation_layer_status_default_color,
    .colors         = &animation_layer_status_colors[0],
    .colors_size    = DT_INST_PROP_LEN(0, colors),
    .layer_offset   = DT_INST_PROP(0, layer_offset),
    .extend_duration =
        DT_INST_PROP(0, extend_duration_seconds) * CONFIG_ZMK_ANIMATION_FPS,
};

void zmk_animation_layer_status_set_status(uint32_t layer_status) {
    LOG_DBG("set status: %d", layer_status);
#if IS_SPLIT_PERIPHERAL
    refresh_layer_status_peripheral(DEVICE_DT_GET(DT_DRV_INST(0)),
                                    layer_status);
#endif
}

DEVICE_DT_INST_DEFINE(0, &animation_layer_status_init, NULL,
                      &animation_layer_status_data,
                      &animation_layer_status_config, POST_KERNEL,
                      CONFIG_APPLICATION_INIT_PRIORITY,
                      &animation_layer_status_api);

// #if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#if IS_CENTRAL
static void on_layer_status_change(const struct device *dev,
                                   const struct zmk_layer_state_changed *ev) {
    const struct animation_layer_status_config *config = dev->config;
    struct animation_layer_status_data *data           = dev->data;
    if (data->running) {
#if IS_CENTRAL
        refresh_layer_status_central(dev);
#endif
#if IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
        sync_layer_status_to_peripheral(dev);
#endif
    }
}

static int event_listener(const zmk_event_t *eh) {
    const struct zmk_layer_state_changed *ev = as_zmk_layer_state_changed(eh);
    if (ev) {
        on_layer_status_change(DEVICE_DT_GET(DT_DRV_INST(0)), ev);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(animation_layer_status, event_listener);
ZMK_SUBSCRIPTION(animation_layer_status, zmk_layer_state_changed);

#endif
