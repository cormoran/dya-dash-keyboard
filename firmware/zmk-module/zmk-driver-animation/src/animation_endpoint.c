/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_endpoint

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/conn.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/endpoint_changed.h>
#include <zmk/events/ble_active_profile_changed.h>
#include <zmk/events/split_peripheral_status_changed.h>
#include <zmk/split/bluetooth/peripheral.h>
#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>
#include <zmk_driver_animation/drivers/animation_control.h>

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

enum ble_connection_status {
    BLE_STATUS_OPEN,
    BLE_STATUS_DISCONNECTED,
    BLE_STATUS_CONNECTED
};

struct animation_endpoint_config {
    size_t *pixel_map;
    size_t pixel_map_size;
    uint32_t duration_seconds_on_endpoint_change;
    uint32_t not_connected_duration;
    uint32_t blink_duration;
    uint32_t extend_duration;
    uint32_t event_handling_start_seconds;
    struct zmk_color_hsl *color_open;
    struct zmk_color_hsl *color_disconnected;
    struct zmk_color_hsl *color_connected;
    struct zmk_color_hsl *color_usb;
};
struct animation_endpoint_data {
    bool running;
    uint32_t counter;
    uint32_t blink_counter;
#if IS_CENTRAL
    int active_index;
    enum ble_connection_status active_profile_status;
#elif IS_SPLIT_PERIPHERAL
    enum ble_connection_status central_status;
#endif
};

void refresh_ble_connection_status(const struct device *dev) {
    const struct animation_endpoint_config *config = dev->config;
    struct animation_endpoint_data *data           = dev->data;
    if (!data->running) {
        return;
    }
    bool is_connected = false;
#if IS_CENTRAL
    data->active_index = zmk_ble_active_profile_index();
    if (zmk_ble_active_profile_is_open()) {
        data->active_profile_status = BLE_STATUS_OPEN;
    } else if (zmk_ble_active_profile_is_connected()) {
        data->active_profile_status = BLE_STATUS_CONNECTED;
        is_connected                = true;
    } else {
        data->active_profile_status = BLE_STATUS_DISCONNECTED;
    }
#elif IS_SPLIT_PERIPHERAL
    if (!zmk_split_bt_peripheral_is_bonded()) {
        data->central_status = BLE_STATUS_OPEN;
    } else if (zmk_split_bt_peripheral_is_connected()) {
        data->central_status = BLE_STATUS_CONNECTED;
        is_connected         = true;
    } else {
        data->central_status = BLE_STATUS_DISCONNECTED;
    }
#endif
    if (!is_connected && data->counter < config->not_connected_duration) {
        data->counter = config->not_connected_duration;
        zmk_animation_request_frames_if_required(data->counter, true);
    }
    if (data->counter < config->extend_duration) {
        data->counter = config->extend_duration;
        zmk_animation_request_frames_if_required(data->counter, true);
    }
}

#if IS_CENTRAL
static void update_pixels_central(const struct device *dev,
                                  struct animation_pixel *pixels,
                                  size_t num_pixels) {
    const struct animation_endpoint_config *config = dev->config;
    struct animation_endpoint_data *data           = dev->data;

    bool is_usb_selected =
        zmk_endpoints_selected().transport == ZMK_TRANSPORT_USB;

    for (size_t i = 0; i < config->pixel_map_size; i++) {
        struct zmk_color_hsl color;
        struct zmk_color_rgb rgb = {.r = 0, .g = 0, .b = 0};
        if (i != data->active_index || i >= ZMK_BLE_PROFILE_COUNT) {
            if (is_usb_selected) {
                color = *config->color_usb;
                zmk_hsl_to_rgb(&color, &rgb);
                pixels[config->pixel_map[i]].value = rgb;
            }
            pixels[config->pixel_map[i]].value = rgb;
            continue;
        }
        bool blink = true;
        switch (data->active_profile_status) {
            case BLE_STATUS_OPEN:
                color = *config->color_open;
                break;
            case BLE_STATUS_CONNECTED:
                color = *config->color_connected;
                blink = false;
                break;
            case BLE_STATUS_DISCONNECTED:
                color = *config->color_disconnected;
                break;
            default:
                LOG_ERR("Unkonwn ble status %d", data->active_profile_status);
                continue;
        }
        if (blink) {
            uint32_t highest_point = config->blink_duration / 2;
            uint32_t point = data->blink_counter % config->blink_duration;
            // 0% when point = 0
            // 100% when point = config->blink_duration / 2
            // 0% when point = config->blink_duration (=0)
            float ratio = (float)(point < highest_point
                                      ? point
                                      : config->blink_duration - point) /
                          highest_point;
            color.l = ratio * color.l;
        }

        zmk_hsl_to_rgb(&color, &rgb);
        pixels[config->pixel_map[i]].value = rgb;
    }
}

#elif IS_SPLIT_PERIPHERAL

static void update_pixels_peripheral(const struct device *dev,
                                     struct animation_pixel *pixels,
                                     size_t num_pixels) {
    const struct animation_endpoint_config *config = dev->config;
    struct animation_endpoint_data *data           = dev->data;

    struct zmk_color_hsl color;
    bool animate = true;
    switch (data->central_status) {
        case BLE_STATUS_OPEN:
            color = *config->color_open;
            break;
        case BLE_STATUS_CONNECTED:
            color   = *config->color_connected;
            animate = false;
            break;
        case BLE_STATUS_DISCONNECTED:
            color = *config->color_disconnected;
            break;
        default:
            LOG_ERR("Unkonwn ble status %d", data->central_status);
    }

    uint32_t highest_point =
        data->blink_counter %
        (config->blink_duration * 2 - 1);  // [0, blink_duration*2-2]
    if (highest_point >= config->blink_duration) {
        // [0, blink_duration-1]
        // blink_druation -> blink_duration - 1
        // blink_duration*2-2 -> 1
        highest_point = config->blink_duration * 2 - highest_point - 1;
    }
    uint32_t unit =
        config->pixel_map_size == 1
            ? 1
            : (config->blink_duration / (config->pixel_map_size - 1));
    for (int i = 0; i < config->pixel_map_size; i++) {
        struct zmk_color_hsl color2 = {
            .h = 0,
            .s = 0,
            .l = 0,
        };
        if (!animate) {
            color2 = color;
        } else {
            uint32_t point = i * unit;  // 0 ~ config->blink_duration
            // gap: 0~ config->blink_duration - 1
            uint32_t gap = point < highest_point ? highest_point - point
                                                 : point - highest_point;
            // enable in range [hiest_point - unit, highest_point + unit]
            float ratio = gap > unit ? 0 : (1.0 - (float)gap / unit);
            color2      = color;
            color2.l    = ratio * color2.l;
        }
        // else if (i * unit < highest_point &&
        //            highest_point <= (i + 1) * unit) {
        //     color2 = color;
        //     int32_t p =
        //         (int32_t)(highest_point % unit) * 2 - unit;  // -unit ~ unit
        //     float ratio = (float)(unit - (p > 0 ? p : -p)) / unit;
        //     color2.l    = ratio * color2.l;
        // }
        struct zmk_color_rgb rgb;
        zmk_hsl_to_rgb(&color2, &rgb);
        pixels[config->pixel_map[i]].value = rgb;
    }
}

#endif

static void animation_endpoint_render_frame(const struct device *dev,
                                            struct animation_pixel *pixels,
                                            size_t num_pixels) {
    struct animation_endpoint_data *data = dev->data;
    uint32_t counter                     = data->counter;
    if (counter == 0) {
        return;
    }
#if IS_CENTRAL
    update_pixels_central(dev, pixels, num_pixels);
#elif IS_SPLIT_PERIPHERAL
    update_pixels_peripheral(dev, pixels, num_pixels);
#endif
    data->blink_counter++;
    data->counter = counter - 1;
    zmk_animation_request_frames_if_required(data->counter, false);
    if (data->counter == 0) {
        LOG_INF("Stop animation endpoint status by counter");
        animation_stop(dev);
    }
}

static void animation_endpoint_start(const struct device *dev,
                                     uint32_t request_duration_ms) {
    const struct animation_endpoint_config *config = dev->config;
    struct animation_endpoint_data *data           = dev->data;

    data->counter = request_duration_ms == 0
                        ? ANIMATION_DURATION_FOREVER
                        : ANIMATION_DURATION_MS_TO_FRAMES(request_duration_ms);
    if (!data->running) {
        data->blink_counter = 0;
        data->running       = true;
    }

    refresh_ble_connection_status(dev);

    zmk_animation_request_frames_if_required(data->counter, true);
    LOG_INF("Start animation endpoint status");
}

static void animation_endpoint_stop(const struct device *dev) {
    struct animation_endpoint_data *data = dev->data;

    data->running = false;
    data->counter = 0;
    LOG_INF("Stop animation endpoint status");
}

static bool animation_endpoint_is_finished(const struct device *dev) {
    struct animation_endpoint_data *data = dev->data;
    return !data->running;
}

void on_endpoint_status_change(const struct device *dev) {
    const struct animation_endpoint_config *config = dev->config;
    struct animation_endpoint_data *data           = dev->data;
    if (!data->running && config->duration_seconds_on_endpoint_change > 0) {
        if (k_uptime_get() > config->event_handling_start_seconds * 1000) {
            animation_control_enqueue_animation(
                animation_control, dev, false,
                config->duration_seconds_on_endpoint_change * 1000);
        }
        return;
    }
    refresh_ble_connection_status(dev);
}

static int animation_endpoint_init(const struct device *dev) { return 0; }

static const struct animation_api animation_endpoint_api = {
    .on_start     = animation_endpoint_start,
    .on_stop      = animation_endpoint_stop,
    .render_frame = animation_endpoint_render_frame,
    .is_finished  = animation_endpoint_is_finished,
};

#define ANIMATION_ENDPOINT_DEVICE(idx)                                         \
                                                                               \
    static struct animation_endpoint_data animation_endpoint_##idx##_data =    \
        {};                                                                    \
                                                                               \
    static size_t animation_endpoint_##idx##_pixel_map[] =                     \
        DT_INST_PROP(idx, pixels);                                             \
                                                                               \
    static uint32_t animation_endpoint_##idx##_color_open =                    \
        DT_INST_PROP(idx, color_open);                                         \
    static uint32_t animation_endpoint_##idx##_color_disconnected =            \
        DT_INST_PROP(idx, color_disconnected);                                 \
    static uint32_t animation_endpoint_##idx##_color_connected =               \
        DT_INST_PROP(idx, color_connected);                                    \
    static uint32_t animation_endpoint_##idx##_color_usb =                     \
        DT_INST_PROP(idx, color_usb);                                          \
                                                                               \
    static struct animation_endpoint_config                                    \
        animation_endpoint_##idx##_config = {                                  \
            .pixel_map      = &animation_endpoint_##idx##_pixel_map[0],        \
            .pixel_map_size = DT_INST_PROP_LEN(idx, pixels),                   \
            .duration_seconds_on_endpoint_change =                             \
                DT_INST_PROP(idx, duration_seconds_on_endpoint_change),        \
            .not_connected_duration =                                          \
                DT_INST_PROP(idx, not_connected_duration_seconds) *            \
                CONFIG_ZMK_ANIMATION_FPS,                                      \
            .blink_duration = DT_INST_PROP(idx, blink_duration_seconds) *      \
                              CONFIG_ZMK_ANIMATION_FPS,                        \
            .extend_duration = DT_INST_PROP(idx, extend_duration_seconds) *    \
                               CONFIG_ZMK_ANIMATION_FPS,                       \
            .event_handling_start_seconds =                                    \
                DT_INST_PROP(idx, event_handling_start_seconds),               \
            .color_open = (struct zmk_color_hsl                                \
                               *)&animation_endpoint_##idx##_color_open,       \
            .color_disconnected =                                              \
                (struct zmk_color_hsl                                          \
                     *)&animation_endpoint_##idx##_color_disconnected,         \
            .color_connected =                                                 \
                (struct zmk_color_hsl                                          \
                     *)&animation_endpoint_##idx##_color_connected,            \
            .color_usb =                                                       \
                (struct zmk_color_hsl *)&animation_endpoint_##idx##_color_usb, \
    };                                                                         \
                                                                               \
    DEVICE_DT_INST_DEFINE(                                                     \
        idx, &animation_endpoint_init, NULL, &animation_endpoint_##idx##_data, \
        &animation_endpoint_##idx##_config, POST_KERNEL,                       \
        CONFIG_APPLICATION_INIT_PRIORITY, &animation_endpoint_api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_ENDPOINT_DEVICE);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#define DEVICE_ADDR(idx) DEVICE_DT_GET(DT_DRV_INST(idx)),

static const struct device *animation_endpoint_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(DEVICE_ADDR)};

static const size_t animation_endpoint_size =
    sizeof(animation_endpoint_devices) / sizeof(struct device *);

static int event_listener(const zmk_event_t *eh) {
    for (int i = 0; i < animation_endpoint_size; i++) {
        on_endpoint_status_change(animation_endpoint_devices[i]);
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(animation_endpoint, event_listener);
#if IS_CENTRAL
ZMK_SUBSCRIPTION(animation_endpoint, zmk_ble_active_profile_changed);
ZMK_SUBSCRIPTION(animation_endpoint, zmk_endpoint_changed);
#elif IS_SPLIT_PERIPHERAL
ZMK_SUBSCRIPTION(animation_endpoint, zmk_split_peripheral_status_changed);
#endif
#endif
