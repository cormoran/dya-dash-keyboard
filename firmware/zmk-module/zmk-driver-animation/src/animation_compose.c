/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_compose

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk_driver_animation/color.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct animation_compose_config {
    struct device **animations;
    uint32_t *durations;
    uint8_t num_animations;
    bool parallel;
};

struct animation_compose_data {
    bool running;
    uint8_t current_index;
    struct k_mutex mutex;
};

void render_frame_for_parallel(const struct device *dev,
                               struct animation_pixel *pixels,
                               size_t num_pixels) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    bool still_running                            = false;
    for (int i = 0; i < config->num_animations; ++i) {
        if (!animation_is_finished(config->animations[i])) {
            animation_render_frame(config->animations[i], pixels, num_pixels);
            // animation can finish by this rendering
            still_running = !animation_is_finished(config->animations[i]);
        }
    }
    if (!still_running) {
        // finished
        int rc = k_mutex_lock(&data->mutex, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("Failed to lock mutex: %d", rc);
            return;
        }
        data->running = false;
        rc            = k_mutex_unlock(&data->mutex);
        if (rc != 0) {
            LOG_ERR("Failed to unlock mutex: %d", rc);
            return;
        }
        return;
    }
}

void render_frame_for_sequential(const struct device *dev,
                                 struct animation_pixel *pixels,
                                 size_t num_pixels) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    int current                                   = data->current_index;
    animation_render_frame(config->animations[current], pixels, num_pixels);
    if (animation_is_finished(config->animations[current])) {
        int rc = k_mutex_lock(&data->mutex, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("Failed to lock mutex: %d", rc);
            return;
        }
        if (data->running && data->current_index == current) {
            int next = current + 1;
            if (next >= config->num_animations) {
                data->running       = false;
                data->current_index = 0;
                LOG_DBG("all animations finished");
            } else {
                LOG_DBG("start next animation[%d]", next);
                data->current_index = next;
                // TODO: consider request_duration_ms (how?)
                animation_start(config->animations[next],
                                config->durations[next]);
                // request frame to give chance to switch animation even if next
                // animation is not started
                zmk_animation_request_frames(1);
            }
        } else {
            LOG_DBG("detect concurrent update");
        }
        rc = k_mutex_unlock(&data->mutex);
        if (rc != 0) {
            LOG_ERR("Failed to unlock mutex: %d", rc);
            return;
        }
    }
}

static void animation_compose_render_frame(const struct device *dev,
                                           struct animation_pixel *pixels,
                                           size_t num_pixels) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    if (!data->running) {
        LOG_INF("animation compose not running");
        return;
    }
    if (config->parallel) {
        render_frame_for_parallel(dev, pixels, num_pixels);
    } else {
        render_frame_for_sequential(dev, pixels, num_pixels);
    }
}

static void animation_compose_start(const struct device *dev,
                                    uint32_t request_duration_ms) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    if (data->running) {
        LOG_INF("animation compose already running");
        return;
    }
    int rc = k_mutex_lock(&data->mutex, K_FOREVER);
    if (rc != 0) {
        LOG_ERR("Failed to lock mutex: %d", rc);
        return;
    }
    if (!data->running) {
        data->current_index = 0;
        data->running       = true;
        LOG_DBG("Start animation compose");
        int ub = config->parallel ? config->num_animations : 1;

        for (int i = 0; i < ub; ++i) {
            uint32_t duration = config->durations[i];
            // request_duration_ms is not respected for sequential animation
            if (request_duration_ms > 0 && duration > request_duration_ms) {
                duration = request_duration_ms;
            }
            animation_start(config->animations[i], duration);
        }
        zmk_animation_request_frames(1);
    } else {
        LOG_INF("animation compose already running (after taking lock)");
    }
    rc = k_mutex_unlock(&data->mutex);
    if (rc != 0) {
        LOG_ERR("Failed to unlock mutex: %d", rc);
        return;
    }
}

static void animation_compose_stop(const struct device *dev) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    if (!data->running) {
        return;
    }
    int rc = k_mutex_lock(&data->mutex, K_FOREVER);
    if (rc != 0) {
        LOG_ERR("Failed to lock mutex: %d", rc);
        return;
    }
    if (data->running) {  // check again after taking the lock
        if (config->parallel) {
            for (int i = 0; i < config->num_animations; ++i) {
                // Let's always stop for simplicity. The animation might be
                // already stopped.
                animation_stop(config->animations[i]);
            }
        } else {
            animation_stop(config->animations[data->current_index]);
        }
    }
    data->current_index = 0;
    data->running       = false;
    rc                  = k_mutex_unlock(&data->mutex);
    if (rc != 0) {
        LOG_ERR("Failed to unlock mutex: %d", rc);
        return;
    }
    LOG_DBG("Stop animation compose");
}

static bool animation_compose_is_finished(const struct device *dev) {
    struct animation_compose_data *data = dev->data;
    return !data->running;
}

static int animation_compose_init(const struct device *dev) {
    const struct animation_compose_config *config = dev->config;
    struct animation_compose_data *data           = dev->data;
    int rc                                        = k_mutex_init(&data->mutex);
    if (rc != 0) {
        LOG_ERR("Failed to initialize mutex: %d", rc);
        return rc;
    }
    return 0;
}

static const struct animation_api animation_compose_api = {
    .on_start     = animation_compose_start,
    .on_stop      = animation_compose_stop,
    .render_frame = animation_compose_render_frame,
    .is_finished  = animation_compose_is_finished,
};

#define PHANDLE_TO_DEVICE(node_id, prop, idx) \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#define ANIMATION_COMPOSE_DEVICE(idx)                                         \
                                                                              \
    static struct animation_compose_data animation_compose_##idx##_data;      \
                                                                              \
    static const struct device *animation_compose_##idx##_animations[] = {    \
        DT_INST_FOREACH_PROP_ELEM(idx, animations, PHANDLE_TO_DEVICE)};       \
    static const uint32_t animation_compose_##idx##_durations[] =             \
        DT_INST_PROP(idx, durations_ms);                                      \
                                                                              \
    static struct animation_compose_config animation_compose_##idx##_config = \
        {                                                                     \
            .animations     = &animation_compose_##idx##_animations[0],       \
            .durations      = &animation_compose_##idx##_durations[0],        \
            .num_animations = DT_INST_PROP_LEN(idx, animations),              \
            .parallel       = DT_INST_PROP(idx, parallel),                    \
    };                                                                        \
                                                                              \
    DEVICE_DT_INST_DEFINE(                                                    \
        idx, &animation_compose_init, NULL, &animation_compose_##idx##_data,  \
        &animation_compose_##idx##_config, POST_KERNEL,                       \
        CONFIG_APPLICATION_INIT_PRIORITY, &animation_compose_api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_COMPOSE_DEVICE);
