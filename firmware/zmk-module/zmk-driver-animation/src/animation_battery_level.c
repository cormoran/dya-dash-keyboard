/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_battery_status

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/battery.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>
#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>
#include <zmk_driver_animation/drivers/animation_control.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if !DT_HAS_CHOSEN(zmk_animation_control)
#error "A zmk,animation_control chosen node must be declared"
#endif

#define REFRESH_INTERVAL CONFIG_ZMK_ANIMATION_FPS

static const struct device *animation_control =
    DEVICE_DT_GET(DT_CHOSEN(zmk_animation_control));

struct animation_battery_status_config {
    size_t *pixel_map;
    size_t pixel_map_size;
    uint32_t animation_duration;
    uint8_t low_alert_start_threshold;
    uint8_t low_alert_stop_threshold;
    uint32_t low_alert_interval_millis;
    uint32_t low_alert_duration_ms;
    struct zmk_color_hsl *color_high;
    struct zmk_color_hsl *color_middle;
    struct zmk_color_hsl *color_low;
};
struct animation_battery_status_data {
    bool running;
    uint32_t counter;
    uint64_t last_alert_time;
};

static void animation_battery_status_render_frame(
    const struct device *dev, struct animation_pixel *pixels,
    size_t num_pixels) {
    const struct animation_battery_status_config *config = dev->config;
    struct animation_battery_status_data *data           = dev->data;

    uint32_t counter = data->counter;
    if (counter == 0) {
        return;
    }

    uint8_t battery_level = zmk_battery_state_of_charge();
    uint8_t unit          = 100 / (config->pixel_map_size * 3);

    // highest brightness point (0~config->animation_duration)
    uint32_t highest_point = data->counter % config->animation_duration;
    for (int i = 0; i < config->pixel_map_size; i++) {
        uint32_t point =
            i * config->animation_duration / config->pixel_map_size;
        uint32_t gap = point < highest_point ? highest_point - point
                                             : point - highest_point;
        if (gap > config->animation_duration / 2)
            gap = config->animation_duration - gap;
        // gap = 0~config->animation_duration/2
        float ratio = 1.0 - (float)gap / (config->animation_duration / 2);
        struct zmk_color_hsl color = {.h = 0, .s = 0, .l = 0};
        if (battery_level <= (i * 3) * unit) {
            // default
            // <= is to treat 0% as default
        } else if (battery_level < (i * 3 + 1) * unit) {
            color = *config->color_low;
        } else if (battery_level < (i * 3 + 2) * unit) {
            color = *config->color_middle;
        } else {
            color = *config->color_high;
        }
        color.l = color.l * (0.5 + ratio / 2);  // 0.5~1.0
        struct zmk_color_rgb rgb;
        zmk_hsl_to_rgb(&color, &rgb);
        pixels[config->pixel_map[i]].value = rgb;
    }
    data->counter = counter - 1;
    zmk_animation_request_frames_if_required(data->counter, false);
    if (data->counter == 0) {
        animation_stop(dev);
    }
}

static void animation_battery_status_start(const struct device *dev,
                                           uint32_t request_duration_ms) {
    const struct animation_battery_status_config *config = dev->config;
    struct animation_battery_status_data *data           = dev->data;
    LOG_INF("Start animation battery status");
    data->last_alert_time = k_uptime_get();
    data->counter         = request_duration_ms == 0
                                ? ANIMATION_DURATION_FOREVER
                                : ANIMATION_DURATION_MS_TO_FRAMES(request_duration_ms);
    data->running         = true;

    zmk_animation_request_frames_if_required(data->counter, true);
}

static void animation_battery_status_stop(const struct device *dev) {
    const struct animation_battery_status_config *config = dev->config;
    struct animation_battery_status_data *data           = dev->data;
    LOG_INF("Stop animation battery status");
    data->last_alert_time = k_uptime_get();
    data->running         = false;
    data->counter         = 0;
}

static bool animation_battery_status_is_finished(const struct device *dev) {
    const struct animation_battery_status_config *config = dev->config;
    struct animation_battery_status_data *data           = dev->data;
    return !data->running;
}

void on_battery_status_change(const struct device *dev) {
    const struct animation_battery_status_config *config = dev->config;
    struct animation_battery_status_data *data           = dev->data;
    if (!data->running) {
        uint8_t level = zmk_battery_state_of_charge();
        if (config->low_alert_stop_threshold < level &&
            level < config->low_alert_start_threshold &&
            k_uptime_get() - data->last_alert_time >
                config->low_alert_interval_millis) {
            animation_control_enqueue_animation(animation_control, dev, false,
                                                config->low_alert_duration_ms);
        }
        return;
    }
}

static int animation_battery_status_init(const struct device *dev) { return 0; }

static const struct animation_api animation_battery_status_api = {
    .on_start     = animation_battery_status_start,
    .on_stop      = animation_battery_status_stop,
    .render_frame = animation_battery_status_render_frame,
    .is_finished  = animation_battery_status_is_finished,
};

#define ANIMATION_BATTERY_STATUS_DEVICE(idx)                                   \
                                                                               \
    static struct animation_battery_status_data                                \
        animation_battery_status_##idx##_data = {};                            \
                                                                               \
    static size_t animation_battery_status_##idx##_pixel_map[] =               \
        DT_INST_PROP(idx, pixels);                                             \
                                                                               \
    static uint32_t animation_battery_status_##idx##_color_high =              \
        DT_INST_PROP(idx, color_high);                                         \
    static uint32_t animation_battery_status_##idx##_color_middle =            \
        DT_INST_PROP(idx, color_middle);                                       \
    static uint32_t animation_battery_status_##idx##_color_low =               \
        DT_INST_PROP(idx, color_low);                                          \
                                                                               \
    static struct animation_battery_status_config                              \
        animation_battery_status_##idx##_config = {                            \
            .pixel_map      = &animation_battery_status_##idx##_pixel_map[0],  \
            .pixel_map_size = DT_INST_PROP_LEN(idx, pixels),                   \
            .animation_duration =                                              \
                DT_INST_PROP(idx, animation_duration_seconds) *                \
                CONFIG_ZMK_ANIMATION_FPS,                                      \
            .color_high   = &animation_battery_status_##idx##_color_high,      \
            .color_middle = &animation_battery_status_##idx##_color_middle,    \
            .color_low    = &animation_battery_status_##idx##_color_low,       \
            .low_alert_start_threshold =                                       \
                DT_INST_PROP(idx, low_alert_start_threshold),                  \
            .low_alert_stop_threshold =                                        \
                DT_INST_PROP(idx, low_alert_stop_threshold),                   \
            .low_alert_interval_millis =                                       \
                DT_INST_PROP(idx, low_alert_interval_seconds) * 1000,          \
            .low_alert_duration_ms = DT_INST_PROP(idx, low_alert_duration_ms), \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(idx, &animation_battery_status_init, NULL,           \
                          &animation_battery_status_##idx##_data,              \
                          &animation_battery_status_##idx##_config,            \
                          POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY,       \
                          &animation_battery_status_api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_BATTERY_STATUS_DEVICE);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define DEVICE_ADDR(idx) DEVICE_DT_GET(DT_DRV_INST(idx)),

static const struct device *animation_battery_status_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(DEVICE_ADDR)};

static const size_t animation_battery_status_size =
    sizeof(animation_battery_status_devices) / sizeof(struct device *);

static int event_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev =
        as_zmk_battery_state_changed(eh);
    if (ev) {
        for (int i = 0; i < animation_battery_status_size; i++) {
            on_battery_status_change(animation_battery_status_devices[i]);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(animation_battery_status, event_listener);
ZMK_SUBSCRIPTION(animation_battery_status, zmk_battery_state_changed);

#endif
