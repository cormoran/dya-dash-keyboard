/*
 * Copyright (c) 2020 The ZMK Contributors
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_ext_power_transient

#include <stdio.h>
#include <zephyr/device.h>
#include <zephyr/pm/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <drivers/ext_power.h>

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

struct ext_power_transient_config {
    const struct gpio_dt_spec *control;
    const size_t control_gpios_count;
};

struct ext_power_transient_data {
    bool status;
};

static int ext_power_transient_enable(const struct device *dev) {
    struct ext_power_transient_data *data           = dev->data;
    const struct ext_power_transient_config *config = dev->config;

    for (int i = 0; i < config->control_gpios_count; i++) {
        const struct gpio_dt_spec *gpio = &config->control[i];
        if (gpio_pin_set_dt(gpio, 1)) {
            LOG_WRN("Failed to set ext-power control pin %d", i);
            return -EIO;
        }
    }
    data->status = true;
    return 0;
}

static int ext_power_transient_disable(const struct device *dev) {
    struct ext_power_transient_data *data           = dev->data;
    const struct ext_power_transient_config *config = dev->config;

    for (int i = 0; i < config->control_gpios_count; i++) {
        const struct gpio_dt_spec *gpio = &config->control[i];
        if (gpio_pin_set_dt(gpio, 0)) {
            LOG_WRN("Failed to clear ext-power control pin %d", i);
            return -EIO;
        }
    }
    data->status = false;
    return 0;
}

static int ext_power_transient_get(const struct device *dev) {
    struct ext_power_transient_data *data = dev->data;
    return data->status;
}

static int ext_power_transient_init(const struct device *dev) {
    const struct ext_power_transient_config *config = dev->config;

    for (int i = 0; i < config->control_gpios_count; i++) {
        const struct gpio_dt_spec *gpio = &config->control[i];
        if (gpio_pin_configure_dt(gpio, GPIO_OUTPUT_INACTIVE)) {
            LOG_ERR("Failed to configure ext-power control pin %d", i);
            return -EIO;
        }
    }
    return 0;
}

#ifdef CONFIG_PM_DEVICE
static int ext_power_transient_pm_action(const struct device *dev,
                                         enum pm_device_action action) {
    switch (action) {
        case PM_DEVICE_ACTION_RESUME:
            // Not automatically enabled!
            return 0;
        case PM_DEVICE_ACTION_SUSPEND:
            ext_power_transient_disable(dev);
            return 0;
        default:
            return -ENOTSUP;
    }
}
#endif /* CONFIG_PM_DEVICE */

#define ZMK_EXT_POWER_INIT_PRIORITY 81
#define EXT_POWER_TRANSIENT_DEVICE(idx)                                       \
                                                                              \
    static const struct gpio_dt_spec                                          \
        ext_power_##idx##_control_gpios[DT_INST_PROP_LEN(idx,                 \
                                                         control_gpios)] = {  \
            DT_INST_FOREACH_PROP_ELEM_SEP(idx, control_gpios,                 \
                                          GPIO_DT_SPEC_GET_BY_IDX, (, ))};    \
                                                                              \
    static const struct ext_power_transient_config ext_power_##idx##_config = \
        {                                                                     \
            .control             = ext_power_##idx##_control_gpios,           \
            .control_gpios_count = DT_INST_PROP_LEN(idx, control_gpios),      \
    };                                                                        \
                                                                              \
    static struct ext_power_transient_data ext_power_##idx##_data = {         \
        .status = false,                                                      \
    };                                                                        \
                                                                              \
    static const struct ext_power_api ext_power_##idx##_api = {               \
        .enable  = ext_power_transient_enable,                                \
        .disable = ext_power_transient_disable,                               \
        .get     = ext_power_transient_get,                                   \
    };                                                                        \
                                                                              \
    PM_DEVICE_DT_INST_DEFINE(idx, ext_power_transient_pm_action);             \
    DEVICE_DT_INST_DEFINE(                                                    \
        idx, ext_power_transient_init, PM_DEVICE_DT_INST_GET(idx),            \
        &ext_power_##idx##_data, &ext_power_##idx##_config, POST_KERNEL,      \
        ZMK_EXT_POWER_INIT_PRIORITY, &ext_power_##idx##_api);

DT_INST_FOREACH_STATUS_OKAY(EXT_POWER_TRANSIENT_DEVICE);

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
