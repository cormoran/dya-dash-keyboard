/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk_driver_animation/drivers/animation_control.h>
#include <dt-bindings/zmk_driver_animation/animation_control.h>

#define DT_DRV_COMPAT zmk_behavior_animation_control

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    // log binding
    LOG_INF("binding: %d %d", binding->param1, binding->param2);
    switch (binding->param1) {
        case ANIMATION_CONTROL_CMD_ENABLE:
            animation_control_set_enabled0(binding->param2);
            return 0;
        case ANIMATION_CONTROL_CMD_SHIFT:
            animation_control_set_next_animation0(
                binding->param2, ANIMATION_CONTROL_POWER_SOURCE_CURRENT);
            return 0;
        case ANIMATION_CONTROL_CMD_SELECT:
            animation_control_set_animation0(
                binding->param2, ANIMATION_CONTROL_POWER_SOURCE_CURRENT);
            return 0;
        case ANIMATION_CONTROL_CMD_BRIGHT:
            animation_control_change_brightness0(
                binding->param2, ANIMATION_CONTROL_POWER_SOURCE_CURRENT);
            return 0;
        default:
            LOG_ERR("Unknown command: %d", binding->param1);
    }
    return -ENOTSUP;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int behavior_animation_init(const struct device *dev) { return 0; }

static const struct behavior_driver_api behavior_animation_driver_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality         = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_animation_init, NULL, NULL, NULL,
                        POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_animation_driver_api);
