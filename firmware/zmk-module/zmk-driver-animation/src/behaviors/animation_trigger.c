/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_animation_trigger

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk_driver_animation/drivers/animation_control.h>
#include <dt-bindings/zmk_driver_animation/animation_trigger.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define PHANDLE_TO_DEVICE(node_id, prop, idx) \
    DEVICE_DT_GET(DT_PHANDLE_BY_IDX(node_id, prop, idx)),

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct trigger_status {
    bool triggered;
    uint8_t index;
    uint8_t num_pressed;
    uint32_t remaining_duration_ms;
};
// TODO: directly mapping animation index to [i] is simpler
static struct trigger_status
    trigger_statuses[CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM] = {};
static struct k_mutex mutex;
static struct k_work_delayable animation_stop_work;
static uint32_t last_work_time = 0;

static uint8_t find_trigger_status_index(uint8_t index) {
    for (int i = 0; i < CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM; i++) {
        if (trigger_statuses[i].triggered &&
            trigger_statuses[i].index == index) {
            return i;
        }
    }
    return CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM;
}

static uint8_t find_empty_index() {
    for (int i = 0; i < CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM; i++) {
        if (!trigger_statuses[i].triggered) {
            return i;
        }
    }
    return CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM;
}

static void animation_stop_work_handler(struct k_work *work) {
    LOG_DBG("");
    int64_t now            = k_uptime_get();
    uint32_t elapsed       = now - last_work_time;
    uint32_t min_remaining = 0;
    for (int i = 0; i < CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM; i++) {
        if (!trigger_statuses[i].triggered) {
            continue;
        }
        int rc = k_mutex_lock(&mutex, K_FOREVER);
        if (rc != 0) {
            LOG_ERR("Failed to lock mutex: %d", rc);
            return;
        }
        struct trigger_status s = trigger_statuses[i];  // copy
        if (!s.triggered) {
            k_mutex_unlock(&mutex);
            continue;
        }
        if (s.remaining_duration_ms > elapsed) {
            s.remaining_duration_ms -= elapsed;
            if (min_remaining < s.remaining_duration_ms) {
                min_remaining = s.remaining_duration_ms;
            }
            trigger_statuses[i] = s;
            k_mutex_unlock(&mutex);
            continue;
        }
        if (s.num_pressed > 0) {
            s.remaining_duration_ms =
                CONFIG_ZMK_ANIMATION_TRIGGER_EXTEND_MS_ON_HOLD;
            if (min_remaining < s.remaining_duration_ms) {
                min_remaining = s.remaining_duration_ms;
            }
            trigger_statuses[i] = s;
            k_mutex_unlock(&mutex);
            continue;
        }
        animation_control_stop_by_index0(s.index);
        struct trigger_status reset = {};
        trigger_statuses[i]         = reset;
        k_mutex_unlock(&mutex);
    }
    last_work_time = now;
    if (min_remaining > 0) {
        LOG_DBG("reschedule %d", min_remaining);
        int rc = k_work_schedule(&animation_stop_work, K_MSEC(min_remaining));
        if (rc < 0) {
            LOG_ERR("Failed to schedule work: %d", rc);
        }
    } else {
        LOG_DBG("Skip rescheduling work");
    }
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
        case ANIMATION_TRIGGER_CMD_TRIGGER:
            int rc = k_mutex_lock(&mutex, K_FOREVER);
            if (rc != 0) {
                LOG_ERR("Failed to lock mutex: %d", rc);
                return rc;
            }
            uint8_t animation_index = binding->param2;
            uint8_t index = find_trigger_status_index(animation_index);
            if (index < CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM) {
                LOG_INF("Animation %d already triggered", animation_index);
                trigger_statuses[index].num_pressed++;
                k_mutex_unlock(&mutex);
                return ZMK_BEHAVIOR_OPAQUE;
            }
            index = find_empty_index();
            if (index >= CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM) {
                LOG_ERR("No empty space for animation %d", animation_index);
                k_mutex_unlock(&mutex);
                return -ENOTSUP;
            }
            trigger_statuses[index].triggered   = true;
            trigger_statuses[index].index       = animation_index;
            trigger_statuses[index].num_pressed = 1;
            trigger_statuses[index].remaining_duration_ms =
                CONFIG_ZMK_ANIMATION_TRIGGER_MIN_DURATION_MS;
            k_mutex_unlock(&mutex);
            rc = animation_control_play_now_by_index0(
                binding->param2, true,
                CONFIG_ZMK_ANIMATION_TRIGGER_MAX_DURATION_MS);
            if (rc != 0) {
                LOG_ERR("Failed to play animation %d: %d", binding->param2, rc);
                return rc;
            }
            rc = k_work_schedule(
                &animation_stop_work,
                K_MSEC(CONFIG_ZMK_ANIMATION_TRIGGER_MIN_DURATION_MS));
            if (rc < 0) {
                LOG_ERR("Failed to schedule work: %d", rc);
                return rc;
            }
            return ZMK_BEHAVIOR_OPAQUE;
            break;
        default:
            LOG_ERR("Unknown command: %d", binding->param1);
            return -ENOTSUP;
    }
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    switch (binding->param1) {
        case ANIMATION_TRIGGER_CMD_TRIGGER:
            uint8_t animation_index = binding->param2;
            int rc                  = k_mutex_lock(&mutex, K_FOREVER);
            if (rc != 0) {
                LOG_ERR("Failed to lock mutex: %d", rc);
                return rc;
            }
            uint8_t index = find_trigger_status_index(animation_index);
            if (index < CONFIG_ZMK_ANIMATION_TRIGGER_MAX_PARALELISM) {
                LOG_DBG("Animation %d released", animation_index);
                uint8_t np = trigger_statuses[index].num_pressed;
                trigger_statuses[index].num_pressed = np > 0 ? np - 1 : 0;
            } else {
                LOG_INF("Animation %d looks already stopped", animation_index);
            }
            k_mutex_unlock(&mutex);
            return ZMK_BEHAVIOR_OPAQUE;
        default:
            LOG_ERR("Unknown command: %d", binding->param1);
    }
    return -ENOTSUP;
}

static int behavior_animation_trigger_init(const struct device *dev) {
    k_mutex_init(&mutex);
    k_work_init_delayable(&animation_stop_work, animation_stop_work_handler);
    return 0;
}

static const struct behavior_driver_api behavior_animation_trigger_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality         = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_animation_trigger_init, NULL, NULL, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_animation_trigger_api);

#endif
