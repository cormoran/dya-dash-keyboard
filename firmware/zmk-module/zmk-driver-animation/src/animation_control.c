/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_animation_control

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/types.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/usb.h>
#include <zmk_driver_animation/animation.h>
#include <zmk_driver_animation/drivers/animation.h>
#include <zmk_driver_animation/drivers/animation_control.h>
#include <drivers/ext_power.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PHANDLE_TO_DEVICE(node_id, prop, idx) \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

struct animation_queue_record {
    const struct device *animation;
    bool cancelable;
    uint32_t duration_ms;
};

struct animation_queue {
    struct k_msgq que;
    char *que_buffer;
    size_t que_buffer_size;
};

struct animation_control_work_context {
    // TODO: move to data or state
    const struct device *animation;
    struct k_work_delayable save_work;
    struct k_work_delayable init_animation_work;
};

struct animation_control_config {
    const struct device **powered_animations;
    const size_t powered_animations_size;
    const struct device **battery_animations;
    const size_t battery_animations_size;
    const struct device **behavior_animations;
    const size_t behavior_animations_size;
    const struct device *init_animation;
    const uint32_t init_animation_duration_ms;
    const uint32_t init_animation_delay_ms;
    const struct device *activation_animation;
    const uint32_t activation_animation_duration_ms;
    const struct device *ext_power;
    const uint8_t brightness_steps;
    const uint8_t max_brightness;
    const struct animation_control_work_context *work;
    const struct settings_handler *settings_handler;
    const struct animation_queue *que;
};

struct animation_control_save_data {
    bool active;
    uint8_t powered_brightness;
    uint8_t battery_brightness;
    uint8_t current_powered_animation;
    uint8_t current_battery_animation;
};

struct animation_control_data {
    struct animation_control_save_data s;
    // `running` is true if animation started
    // if false, `running_animation.animation` is ensured to be null
    // even if true, `running_animation.animation` can be null
    // this value should be mutated only in start/stop method
    bool running;
    struct animation_queue_record running_animation;
    // mutex for updating `running_animation`
    struct k_mutex mutex;
    // internal flag to notify to check next animation if current animation is
    // cancelable
    bool change_animation_if_cancelable;
    // power status cache
    bool last_powered;
    bool playing_adhoc_animation;
};

static int animation_control_load_settings(const struct device *dev,
                                           const char *name, size_t len,
                                           settings_read_cb read_cb,
                                           void *cb_arg) {
#if IS_ENABLED(CONFIG_SETTINGS)
    const char *next;
    int rc;

    if (settings_name_steq(name, "state", &next) && !next) {
        if (len != sizeof(struct animation_control_save_data)) {
            LOG_WRN(
                "animation_control_load_settings: unexpected data size %d != "
                "%d",
                len, sizeof(struct animation_control_save_data));
            return -EINVAL;
        }
        struct animation_control_data *data = dev->data;
        rc                                  = read_cb(cb_arg, &data->s,
                                                      sizeof(struct animation_control_save_data));
        if (rc >= 0) {
            LOG_WRN(
                "animation_control_load_settings: failed to load setting: %d",
                rc);
            return 0;
        }

        return rc;
    }

    return -ENOENT;
#else
    return 0;
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
}

#if IS_ENABLED(CONFIG_SETTINGS)
static void animation_control_save_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct animation_control_work_context *ctx =
        CONTAINER_OF(dwork, struct animation_control_work_context, save_work);
    const struct device *dev = ctx->animation;

    char path[40];
    snprintf(path, 40, "%s/state", dev->name);
    struct animation_control_data *data = dev->data;
    settings_save_one(path, &data->s,
                      sizeof(struct animation_control_save_data));
};

static int animation_control_save_settings(const struct device *dev) {
    const struct animation_control_config *config    = dev->config;
    const struct animation_control_work_context *ctx = config->work;

    k_work_cancel_delayable(&ctx->save_work);

    return k_work_reschedule(&ctx->save_work,
                             K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
}
#endif /* IS_ENABLED(CONFIG_SETTINGS) */

bool is_powered() {
    switch (zmk_usb_get_conn_state()) {
        case ZMK_USB_CONN_HID:
        case ZMK_USB_CONN_POWERED:
            return true;
        default:
            return false;
    }
}

const struct device *get_animation_for_current_power_state(
    const struct device *dev) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (data->last_powered) {
        return config->powered_animations[data->s.current_powered_animation];
    } else {
        return config->battery_animations[data->s.current_battery_animation];
    }
}

int lock_for_updating_animation(struct animation_control_data *data) {
    int rc = k_mutex_lock(&data->mutex, K_FOREVER);
    if (rc != 0) {
        LOG_ERR("Failed to lock mutex: %d", rc);
    }
    return rc;
}

int unlock_after_updating_animation(struct animation_control_data *data) {
    int rc = k_mutex_unlock(&data->mutex);
    if (rc != 0) {
        LOG_ERR("Failed to unlock mutex: %d", rc);
    }
    return rc;
}

int set_power(const struct animation_control_config *config, bool enable) {
    if (device_is_ready(config->ext_power)) {
        int rc = enable ? ext_power_enable(config->ext_power)
                        : ext_power_disable(config->ext_power);
        if (rc != 0) {
            LOG_ERR("Unable to update power: %d", rc);
            return rc;
        }
        LOG_INF("LED Power %s", enable ? "ON" : "OFF");
    }
    return 0;
}

/**
 * Change animation if next animation exists or next_animation_optional given.
 * Next animation is decided in below order:
 * 1. next_animation_optional if not null
 * 2. animation from queue if exists
 * 3. animation for current power state
 *
 * If no playable animation exists, LED power stops to save battery.
 */
void change_animation(
    const struct device *dev,
    const struct animation_queue_record *next_animation_optional) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    if (!data->s.active) {
        LOG_WRN("animation %s is not active", dev->name);
        return;
    }
    if (!data->running) {
        LOG_WRN("animation %s is not running %d", dev->name, data->running);
        return;
    }
    data->change_animation_if_cancelable = false;
    data->last_powered                   = is_powered();

    struct animation_queue_record current = data->running_animation;
    struct animation_queue_record next    = {};
    // decide next animation
    if (next_animation_optional != NULL) {
        next = *next_animation_optional;
        if (device_is_ready(next.animation)) {
            LOG_DBG("Got animation %s from param", next.animation->name);
        } else {
            // give chance to change animation in next cycle
            zmk_animation_request_frames(1);
        }
        data->playing_adhoc_animation = true;
    } else if (k_msgq_num_used_get(&config->que->que) > 0) {
        int rc = k_msgq_get(&config->que->que, &next, K_NO_WAIT);
        if (rc != 0) {
            LOG_ERR("Failed to get next animation from queue: %d", rc);
            return;
        }
        if (device_is_ready(next.animation)) {
            LOG_DBG("Got animation %s from queue", next.animation->name);
        } else {
            // give chance to change animation in next cycle
            zmk_animation_request_frames(1);
        }
        data->playing_adhoc_animation = true;
    } else {
        next.animation   = get_animation_for_current_power_state(dev);
        next.cancelable  = true;
        next.duration_ms = ANIMATION_DURATION_FOREVER;
        if (current.animation == next.animation &&
            !data->playing_adhoc_animation) {
            LOG_DBG("Animation not updated");
            return;  // no update
        }
        LOG_DBG("Got animation %s for power state %d", next.animation->name,
                data->last_powered);
        // no request animation frame here to stop render animation
        data->playing_adhoc_animation = false;
    }
    // Set next animation
    {  // Lock section
        if (lock_for_updating_animation(data) != 0) {
            return;
        }
        // check the latest status after taking lock
        if (data->running) {
            current = data->running_animation;
            if (current.animation) {
                animation_stop(current.animation);
            }
            if (!device_is_ready(next.animation)) {  // including NULL
                LOG_WRN("next animation %s is empty",
                        data->running_animation.animation->name);
                set_power(config, false);
                struct animation_queue_record empty = {
                    .cancelable = true,
                };
                data->running_animation = empty;
                // keep data->running true
                // no request animation frame here to stop render animation
            } else {
                data->running_animation = next;
                set_power(config, true);
                animation_start(data->running_animation.animation,
                                data->running_animation.duration_ms);
                // give chance to change animation in next cycle even if
                // animation didn't start
                zmk_animation_request_frames(1);
            }
        } else {
            LOG_WRN(
                "animation %s is not running (after taking "
                "lock)",
                dev->name);
        }
        if (unlock_after_updating_animation(data) != 0) {
            return;
        }
    }
}

static bool animation_control_api_impl_is_finished(const struct device *dev) {
    const struct animation_control_data *data = dev->data;
    return !data->running;
}

static void animation_control_api_impl_start(const struct device *dev) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    if (!data->s.active) {
        LOG_INF("animation %s is not active", dev->name);
        return;
    }
    if (data->running) {
        LOG_WRN("animation %s already running", dev->name);
        return;
    }
    LOG_DBG("Start animation control %s", dev->name);
    data->running = true;
    // power is set in change_animation
    change_animation(dev, NULL);
}

static void animation_control_api_impl_stop(const struct device *dev) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    // data->active can be already false here
    if (!data->running) {
        LOG_WRN("stop: animation %s is not running", dev->name);
        return;
    }
    {
        if (lock_for_updating_animation(data) != 0) {
            return;
        }
        if (data->running) {
            struct animation_queue_record current = data->running_animation;
            if (current.animation) {
                animation_stop(current.animation);
            }
            struct animation_queue_record empty = {
                .cancelable = true,
            };
            data->running_animation = empty;
            set_power(config, false);
            data->running = false;
            LOG_DBG("Stop animation control %s", dev->name);
        } else {
            LOG_WRN("concurrent update detected");  // safe anyway
        }
        if (unlock_after_updating_animation(data) != 0) {
            return;
        }
    }
}

static void animation_control_api_impl_render_frame(
    const struct device *dev, struct animation_pixel *pixels,
    size_t num_pixels) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    if (!data->s.active) {
        LOG_INF("animation %s inactive, skipped render", dev->name);
        return;
    }
    if (!data->running) {
        LOG_INF("animation %s not running, skipped render", dev->name);
        return;
    }
    // take data snapshot for lockfree thread safety
    struct animation_queue_record current = data->running_animation;
    if (current.animation) {
        animation_render_frame(current.animation, pixels, num_pixels);

        uint8_t brightness = data->last_powered ? data->s.powered_brightness
                                                : data->s.battery_brightness;

        if (brightness < config->brightness_steps) {
            float fbrightness =
                (float)brightness / (float)config->brightness_steps;
            float fmax_brightness =
                (float)config->max_brightness / (float)UINT8_MAX;
            float multiplier = fbrightness * fmax_brightness;
            for (size_t i = 0; i < num_pixels; ++i) {
                pixels[i].value.r *= multiplier;
                pixels[i].value.g *= multiplier;
                pixels[i].value.b *= multiplier;
            }
        }
    }
    const bool animation_finished =
        current.animation ? animation_is_finished(current.animation) : true;
    const bool should_cancel =
        current.cancelable && (data->change_animation_if_cancelable ||
                               k_msgq_num_used_get(&config->que->que) > 0);
    if (animation_finished || should_cancel) {
        LOG_DBG("change animation by %s",
                animation_finished ? "finished" : "cancelable");
        change_animation(dev, NULL);
    }
}

static int animation_control_api_impl_enqueue_animation(
    const struct device *dev, const struct device *animation, bool cancelable,
    uint32_t duration_ms) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (!data->s.active) {
        LOG_WRN("animation %s inactive, skipped %s", dev->name,
                animation->name);
        return 0;
    }
    if (!data->running) {
        LOG_WRN("animation %s not running, skipped %s", dev->name,
                animation->name);
        return 0;
    }
    const struct animation_queue_record record = {
        .animation   = animation,
        .cancelable  = cancelable,
        .duration_ms = duration_ms,
    };
    int res = k_msgq_put(&config->que->que, &record, K_NO_WAIT);
    if (res != 0) {
        LOG_ERR("Failed to put animation %s in queue: %d", animation->name,
                res);
        return res;
    }
    LOG_DBG("Animation %s enqueued", animation->name);
    zmk_animation_request_frames(1);  // force trigger change animation
    return 0;
}

static int animation_control_api_impl_play_now(const struct device *dev,
                                               const struct device *animation,
                                               bool cancelable,
                                               uint32_t duration_ms) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (!data->s.active) {
        LOG_WRN("animation %s inactive, skipped %s", dev->name,
                animation->name);
        return 0;
    }
    if (!data->running) {
        LOG_WRN("animation %s not running, skipped %s", dev->name,
                animation->name);
        return 0;
    }
    struct animation_queue_record record = {
        .animation   = animation,
        .cancelable  = cancelable,
        .duration_ms = duration_ms,
    };
    change_animation(dev, &record);
    return 0;
}

static void animation_control_api_impl_set_enabled(const struct device *dev,
                                                   bool enabled) {
    struct animation_control_data *data = dev->data;
    if (data->s.active == enabled) {
        return;
    }
    data->s.active = enabled;
    if (data->s.active) {
        animation_start(dev, ANIMATION_DURATION_FOREVER);
    } else {
        animation_stop(dev);
    }
#if IS_ENABLED(CONFIG_SETTINGS)
    animation_control_save_settings(dev);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
}

bool select_powered(enum animation_control_power_source source) {
    return source == ANIMATION_CONTROL_POWER_SOURCE_USB       ? true
           : source == ANIMATION_CONTROL_POWER_SOURCE_BATTERY ? false
           : source == ANIMATION_CONTROL_POWER_SOURCE_CURRENT ? is_powered()
                                                              : false;
}

static void animation_control_api_impl_set_next_animation(
    const struct device *dev, int index_offset,
    enum animation_control_power_source power_source) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;

    if (index_offset == 0) {
        return;  // no change
    }
    bool powered               = select_powered(power_source);
    uint8_t *current_animation = powered ? &data->s.current_powered_animation
                                         : &data->s.current_battery_animation;
    size_t num_animations      = powered ? config->powered_animations_size
                                         : config->battery_animations_size;

    while (index_offset < 0) index_offset += (int)num_animations;
    index_offset = index_offset % num_animations;
    uint8_t next_animation =
        (*current_animation + index_offset) % num_animations;
    LOG_DBG("animation: change index %d -> %d", *current_animation,
            next_animation);
    *current_animation                   = next_animation;
    data->change_animation_if_cancelable = true;
    zmk_animation_request_frames(1);
#if IS_ENABLED(CONFIG_SETTINGS)
    animation_control_save_settings(dev);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
}

static void animation_control_api_impl_set_animation(
    const struct device *dev, size_t index,
    enum animation_control_power_source power_source) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;

    bool powered              = select_powered(power_source);
    size_t *current_animation = powered ? &data->s.current_powered_animation
                                        : &data->s.current_battery_animation;
    size_t num_animations     = powered ? config->powered_animations_size
                                        : config->battery_animations_size;

    index = index % num_animations;
    if (*current_animation != index) {
        *current_animation                   = index;
        data->change_animation_if_cancelable = true;
        zmk_animation_request_frames(1);
#if IS_ENABLED(CONFIG_SETTINGS)
        animation_control_save_settings(dev);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
    }
}

static void animation_control_api_impl_change_brightness(
    const struct device *dev, int brightness_offset,
    enum animation_control_power_source power_source) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;

    bool powered = select_powered(power_source);
    uint8_t *brightness_ref =
        powered ? &data->s.powered_brightness : &data->s.battery_brightness;
    // calc next brightness
    int current_brightness = *brightness_ref;
    int next_brightness    = current_brightness + brightness_offset;
    if (next_brightness < 0) next_brightness = 0;
    if (next_brightness > config->brightness_steps)
        next_brightness = config->brightness_steps;
    // reflect to brightness
    if (current_brightness != next_brightness) {
        *brightness_ref = next_brightness;
        LOG_DBG("animation: change brightness %d->%d", current_brightness,
                next_brightness);
        if (next_brightness == 0) {
            animation_stop(dev);
        } else if (current_brightness == 0) {
            animation_start(dev, ANIMATION_DURATION_FOREVER);
        }
#if IS_ENABLED(CONFIG_SETTINGS)
        animation_control_save_settings(dev);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
    }
    return;
}

static int animation_control_api_impl_enqueue_by_index(const struct device *dev,
                                                       uint8_t index,
                                                       bool cancelable,
                                                       uint32_t duration_ms) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (index >= config->behavior_animations_size) {
        LOG_ERR("animation %s index out of range %d", dev->name, index);
        return -EINVAL;
    }
    const struct device *animation = config->behavior_animations[index];
    return animation_control_api_impl_enqueue_animation(
        dev, animation, cancelable, duration_ms);
}

static int animation_control_api_impl_play_now_by_index(
    const struct device *dev, uint8_t index, bool cancelable,
    uint32_t duration_ms) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (index >= config->behavior_animations_size) {
        LOG_ERR("animation %s index out of range %d", dev->name, index);
        return -EINVAL;
    }
    const struct device *animation = config->behavior_animations[index];
    return animation_control_api_impl_play_now(dev, animation, cancelable,
                                               duration_ms);
}

static int animation_control_api_impl_stop_by_index(const struct device *dev,
                                                    uint8_t index) {
    const struct animation_control_config *config = dev->config;
    const struct animation_control_data *data     = dev->data;
    if (index >= config->behavior_animations_size) {
        LOG_ERR("animation %s index out of range %d", dev->name, index);
        return -EINVAL;
    }
    const struct device *animation = config->behavior_animations[index];
    animation_stop(animation);
    // expects the animation returns is_finished true and change animation
    // happens
    zmk_animation_request_frames(1);
    return 0;
}

static void animation_control_on_usb_conn_state_changed(
    const struct device *dev, const struct zmk_usb_conn_state_changed *event) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    if (data->running) {
        // enqueue empty animation to trigger change animation
        // change animation is not called here not to stop current animation
        animation_control_enqueue_animation(dev, NULL, true, 1);
    }
}

static void animation_control_on_activity_state_changed(
    const struct device *dev, const struct zmk_activity_state_changed *event) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    if (event->state == ZMK_ACTIVITY_ACTIVE) {
        if (config->activation_animation != NULL) {
            uint32_t duration_ms =
                config->activation_animation_duration_ms > 0
                    ? config->activation_animation_duration_ms
                    : ANIMATION_DURATION_FOREVER;
            animation_control_enqueue_animation(
                dev, config->activation_animation, false, duration_ms);
        }
    }
}

static void enqueue_initial_animation_work(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct animation_control_work_context *ctx = CONTAINER_OF(
        dwork, struct animation_control_work_context, init_animation_work);
    const struct device *dev                      = ctx->animation;
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;
    uint32_t duration_ms = config->init_animation_duration_ms > 0
                               ? config->init_animation_duration_ms
                               : ANIMATION_DURATION_FOREVER;
    animation_control_enqueue_animation(dev, config->init_animation, false,
                                        duration_ms);
}

static int animation_control_init(const struct device *dev) {
    const struct animation_control_config *config = dev->config;
    struct animation_control_data *data           = dev->data;

#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();

    settings_register(config->settings_handler);

    k_work_init_delayable(&config->work->save_work,
                          animation_control_save_work);

    settings_load_subtree(dev->name);
#endif /* IS_ENABLED(CONFIG_SETTINGS) */
    if (config->ext_power) {
        if (!device_is_ready(config->ext_power)) {
            LOG_ERR("External power device \"%s\" is not ready",
                    config->ext_power->name);
            return -ENODEV;
        }
    }
    k_msgq_init(&config->que->que, config->que->que_buffer,
                sizeof(struct animation_queue_record),
                config->que->que_buffer_size);
    k_mutex_init(&data->mutex);
    if (config->init_animation != NULL) {
        k_work_init_delayable(&config->work->init_animation_work,
                              enqueue_initial_animation_work);
        k_work_schedule(&config->work->init_animation_work,
                        K_MSEC(config->init_animation_delay_ms));
    }
    LOG_INF("Animation control %s initialized", dev->name);
    return 0;
}

static const struct animation_control_api api = {
    .animation_api_base =
        {
            .on_start     = animation_control_api_impl_start,
            .on_stop      = animation_control_api_impl_stop,
            .render_frame = animation_control_api_impl_render_frame,
            .is_finished  = animation_control_api_impl_is_finished,
        },
    .enqueue_animation  = animation_control_api_impl_enqueue_animation,
    .play_now           = animation_control_api_impl_play_now,
    .set_enabled        = animation_control_api_impl_set_enabled,
    .set_next_animation = animation_control_api_impl_set_next_animation,
    .set_animation      = animation_control_api_impl_set_animation,
    .change_brightness  = animation_control_api_impl_change_brightness,
    .enqueue_by_index   = animation_control_api_impl_enqueue_by_index,
    .play_now_by_index  = animation_control_api_impl_play_now_by_index,
    .stop_by_index      = animation_control_api_impl_stop_by_index,
};

#define ANIMATION_CONTROL_DEVICE(idx)                                        \
                                                                             \
    static const struct device                                               \
        *animation_control_##idx##_powered_animations[] = {                  \
            DT_INST_FOREACH_PROP_ELEM(idx, powered_animations,               \
                                      PHANDLE_TO_DEVICE)};                   \
    static const struct device                                               \
        *animation_control_##idx##_battery_animations[] = {                  \
            DT_INST_FOREACH_PROP_ELEM(idx, battery_animations,               \
                                      PHANDLE_TO_DEVICE)};                   \
                                                                             \
    static const struct device                                               \
        *animation_control_##idx##_behavior_animations[] = {                 \
            DT_INST_FOREACH_PROP_ELEM(idx, behavior_animations,              \
                                      PHANDLE_TO_DEVICE)};                   \
                                                                             \
    static struct animation_control_work_context                             \
        animation_control_##idx##_work = {                                   \
            .animation = DEVICE_DT_GET(DT_DRV_INST(idx)),                    \
    };                                                                       \
                                                                             \
    static int animation_control_##idx##_load_settings(                      \
        const char *name, size_t len, settings_read_cb read_cb,              \
        void *cb_arg) {                                                      \
        const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(idx));          \
                                                                             \
        return animation_control_load_settings(dev, name, len, read_cb,      \
                                               cb_arg);                      \
    }                                                                        \
                                                                             \
    static struct settings_handler                                           \
        animation_control_##idx##_settings_handler = {                       \
            .name  = DT_INST_PROP(idx, label),                               \
            .h_set = animation_control_##idx##_load_settings,                \
    };                                                                       \
                                                                             \
    static char animation_control_##idx##_queue_buffer                       \
        [DT_INST_PROP(idx, queue_size) *                                     \
         sizeof(struct animation_queue_record)];                             \
    static struct animation_queue animation_control_##idx##_queue = {        \
        .que_buffer      = animation_control_##idx##_queue_buffer,           \
        .que_buffer_size = DT_INST_PROP(idx, queue_size),                    \
    };                                                                       \
                                                                             \
    static const struct animation_control_config                             \
        animation_control_##idx##_config = {                                 \
            .powered_animations =                                            \
                animation_control_##idx##_powered_animations,                \
            .powered_animations_size =                                       \
                DT_INST_PROP_LEN(idx, powered_animations),                   \
            .battery_animations =                                            \
                animation_control_##idx##_battery_animations,                \
            .battery_animations_size =                                       \
                DT_INST_PROP_LEN(idx, battery_animations),                   \
            .behavior_animations =                                           \
                animation_control_##idx##_behavior_animations,               \
            .behavior_animations_size =                                      \
                DT_INST_PROP_LEN(idx, behavior_animations),                  \
            .init_animation =                                                \
                DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(idx, init_animation)), \
            .init_animation_duration_ms =                                    \
                DT_INST_PROP(idx, init_animation_duration_ms),               \
            .init_animation_delay_ms =                                       \
                DT_INST_PROP(idx, init_animation_delay_ms),                  \
            .activation_animation = DEVICE_DT_GET_OR_NULL(                   \
                DT_INST_PHANDLE(idx, activation_animation)),                 \
            .activation_animation_duration_ms =                              \
                DT_INST_PROP(idx, activation_animation_duration_ms),         \
            .brightness_steps = DT_INST_PROP(idx, brightness_steps) - 1,     \
            .max_brightness   = DT_INST_PROP(idx, max_brightness),           \
            .work             = &animation_control_##idx##_work,             \
            .settings_handler = &animation_control_##idx##_settings_handler, \
            .que              = &animation_control_##idx##_queue,            \
            .ext_power =                                                     \
                DEVICE_DT_GET_OR_NULL(DT_INST_PHANDLE(idx, ext_power)),      \
    };                                                                       \
                                                                             \
    static struct animation_control_data animation_control_##idx##_data = {  \
        .s =                                                                 \
            {                                                                \
                .active                    = true,                           \
                .powered_brightness        = 1,                              \
                .battery_brightness        = 1,                              \
                .current_powered_animation = 0,                              \
                .current_battery_animation = 0,                              \
            },                                                               \
    };                                                                       \
                                                                             \
    DEVICE_DT_INST_DEFINE(idx, &animation_control_init, NULL,                \
                          &animation_control_##idx##_data,                   \
                          &animation_control_##idx##_config, POST_KERNEL,    \
                          CONFIG_APPLICATION_INIT_PRIORITY, &api);

DT_INST_FOREACH_STATUS_OKAY(ANIMATION_CONTROL_DEVICE);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/*
 * Define callback handler for all animation control devices.
 */

#define DEVICE_ADDR(idx) DEVICE_DT_GET(DT_DRV_INST(idx)),

static const struct device *animation_control_devices[] = {
    DT_INST_FOREACH_STATUS_OKAY(DEVICE_ADDR)};

static const size_t control_animations_size =
    sizeof(animation_control_devices) / sizeof(animation_control_devices[0]);

static int event_listener(const zmk_event_t *eh) {
    if (as_zmk_usb_conn_state_changed(eh)) {
        for (size_t i = 0; i < control_animations_size; i++) {
            animation_control_on_usb_conn_state_changed(
                animation_control_devices[i],
                as_zmk_usb_conn_state_changed(eh));
        }
    } else if (as_zmk_activity_state_changed(eh)) {
        for (size_t i = 0; i < control_animations_size; i++) {
            animation_control_on_activity_state_changed(
                animation_control_devices[i],
                as_zmk_activity_state_changed(eh));
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(animation_control, event_listener);
ZMK_SUBSCRIPTION(animation_control, zmk_usb_conn_state_changed);
ZMK_SUBSCRIPTION(animation_control, zmk_activity_state_changed);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */

#if DT_HAS_CHOSEN(zmk_animation_control)

/*
 * Define global API for behavior. Global instance is required for behavior,
 * since behaviors need to be initialized before device.
 */

struct device *dev0 = DEVICE_DT_GET(DT_CHOSEN(zmk_animation_control));

bool is_dev0_ready() {
    if (!device_is_ready(dev0)) {
        LOG_ERR("Animation control device %s not ready", dev0->name);
        return false;
    }
    return true;
}

int animation_control_enqueue_animation0(const struct device *animation,
                                         bool cancelable,
                                         uint32_t duration_ms) {
    if (!is_dev0_ready()) {
        return -ENODEV;
    }
    return animation_control_enqueue_animation(dev0, animation, cancelable,
                                               duration_ms);
}
int animation_control_play_now0(const struct device *animation, bool cancelable,
                                uint32_t duration_ms) {
    if (!is_dev0_ready()) {
        return -ENODEV;
    }
    return animation_control_play_now(dev0, animation, cancelable, duration_ms);
}
void animation_control_set_enabled0(bool enabled) {
    if (!is_dev0_ready()) {
        return;
    }
    animation_control_set_enabled(dev0, enabled);
}
void animation_control_set_next_animation0(
    int index_offset, enum animation_control_power_source target_power_source) {
    if (!is_dev0_ready()) {
        return;
    }
    animation_control_set_next_animation(dev0, index_offset,
                                         target_power_source);
}

void animation_control_set_animation0(
    int index, enum animation_control_power_source target_power_source) {
    if (!is_dev0_ready()) {
        return;
    }
    animation_control_set_animation(dev0, index, target_power_source);
}
void animation_control_change_brightness0(
    int brightness_to_increment,
    enum animation_control_power_source target_power_source) {
    if (!is_dev0_ready()) {
        return;
    }
    animation_control_change_brightness(dev0, brightness_to_increment,
                                        target_power_source);
}

int animation_control_enqueue_animation_by_index0(uint8_t index,
                                                  bool cancelable,
                                                  uint32_t duration_ms) {
    if (!is_dev0_ready()) {
        return -ENODEV;
    }
    // TODO: define API?
    const struct animation_control_config *config = dev0->config;
    struct animation_control_data *data           = dev0->data;
    if (index >= config->behavior_animations_size) {
        return -EINVAL;
    }
    return animation_control_enqueue_animation(
        dev0, config->behavior_animations[index], cancelable, duration_ms);
}

int animation_control_play_now_by_index0(uint8_t index, bool cancelable,
                                         uint32_t duration_ms) {
    if (!is_dev0_ready()) {
        return -ENODEV;
    }
    return animation_control_play_now_by_index(dev0, index, cancelable,
                                               duration_ms);
}

int animation_control_stop_by_index0(uint8_t index) {
    if (!is_dev0_ready()) {
        return -ENODEV;
    }
    return animation_control_stop_by_index(dev0, index);
}
#endif
