/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_animation_layer_status

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk_driver_animation/drivers/animation_layer_status.h>
#include <dt-bindings/zmk_driver_animation/animation_layer_status.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static const struct device *animation_layer_status =
    DEVICE_DT_GET(DT_NODELABEL(animation_layer_status));

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    if (!device_is_ready(animation_layer_status)) {
        LOG_ERR("Animation control device not ready");
        return -ENODEV;
    }
    switch (binding->param1) {
        case ANIMATION_LAYER_STATUS_CMD_FOR_PERIPHERAL:
            zmk_animation_layer_status_set_status(binding->param2);
            return 0;
        default:
            LOG_ERR("Unknown command: %d", binding->param1);
    }
    return -ENOTSUP;
}

static int behavior_animation_layer_status_init(const struct device *dev) {
    return 0;
}

static const struct behavior_driver_api behavior_animation_layer_status_api = {
    .binding_pressed  = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
    .locality         = BEHAVIOR_LOCALITY_GLOBAL,
};

BEHAVIOR_DT_INST_DEFINE(0, behavior_animation_layer_status_init, NULL, NULL,
                        NULL, POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
                        &behavior_animation_layer_status_api);

#endif
