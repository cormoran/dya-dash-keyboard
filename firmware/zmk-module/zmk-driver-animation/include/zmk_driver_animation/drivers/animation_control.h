/*
 * Copyright (c) 2025 cormoran
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zmk_driver_animation/drivers/animation.h>

/**
 * Target power source to update the setting.
 * animation control supports setting per power source mode for battery power
 * saving.
 */
enum animation_control_power_source {
    ANIMATION_CONTROL_POWER_SOURCE_USB,
    ANIMATION_CONTROL_POWER_SOURCE_BATTERY,
    ANIMATION_CONTROL_POWER_SOURCE_CURRENT,
};

/**
 * Submit ad-hoc animation to be played by the controller.
 * The animation is played after all queued ad-hoc animations.
 * @param animation device which implements animation_api.
 * @param cancelable if true, the animation stops when another ad-hoc animation
 * is enqueued by animation_control_enqueue_animation(). Otherwise, the next
 * animation waits until this animation finishes. Stopped animation never
 * resumes even after finishing next animation.
 * Even if false, the animation can be stopped by
 * animation_control_api_play_now().
 * @param duration_ms duration of the animation in milliseconds. The animation
 * stops after this duration. If 0, the animation is played until it finishes or
 * canceled.
 *
 * @return 0 on success, negative error code on failure.
 */
typedef int (*animation_control_api_enqueue_animation)(
    const struct device *dev, const struct device *animation, bool cancelable,
    uint32_t duration_ms);

/**
 * Play the animation immediately. Currently playing animation stops regardless
 * of its cancelability. Stopped ad-hoc animation never resumes even after
 * finishing next animation.
 * @param cancelable if true, the animation stops when another ad-hoc animation
 * is enqueued by animation_control_enqueue_animation(). Otherwise, the enqueued
 * animation waits until this animation finishes. Stopped animation never
 * resumes even after finishing next animation.
 * Even if false, the animation can be stopped by
 * animation_control_api_play_now().
 * @param duration_ms duration of the animation in milliseconds. The animation
 * stops after this duration. If 0, the animation is played until it finishes or
 * canceled.
 * Caller can stop animation by calling animation_stop(animation) to cancel it.
 * It's ensured that the driver calls aniamtion_start(animation) before
 * returning from this function.
 */
typedef int (*animation_control_api_play_now)(const struct device *dev,
                                              const struct device *animation,
                                              bool cancelable,
                                              uint32_t duration_ms);

/**
 * Enable/disable animation (for all power source).
 */
typedef void (*animation_control_api_set_enabled)(const struct device *dev,
                                                  bool enabled);

/**
 * Change animation to i + offset where i is currently selected animation index.
 * @param offset index offset
 *                1: next animation or first animation if reaches to last
 *               -1: previous animation or last animation if reaches to first
 * @param target_power_source the battery state to set the next animation for
 *   e.g. if ANIMATION_CONTROL_POWER_SOURCE_USB is specified, animation
 * for usb powered status changes to next. Animation for battery powered status
 * is kept current one. if ANIMATION_CONTROL_POWER_SOURCE_CURRENT is specified,
 * decide target status to change by current power source.
 */
typedef void (*animation_control_api_set_next_animation)(
    const struct device *dev, int index_offset,
    enum animation_control_power_source target_power_source);

/**
 * Change animation to index i.
 * @param index animation index (modulo by number of available animations)
 * @param target_power_source the battery state to set the animation for
 *        see animation_control_api_set_next_animation
 */
typedef void (*animation_control_api_set_animation)(
    const struct device *dev, int index,
    enum animation_control_power_source target_power_source);

/**
 * Change brightness of the animation pixels.
 * @param brightness_to_increment amount of brightness to increment. Negative
 * value means decrement
 */
typedef void (*animation_control_api_change_brightness)(
    const struct device *dev, int brightness_to_increment,
    enum animation_control_power_source target_power_source);

typedef int (*animation_control_api_enqueue_by_index)(const struct device *dev,
                                                      uint8_t index,
                                                      bool cancelable,
                                                      uint32_t duration_ms);

typedef int (*animation_control_api_play_now_by_index)(const struct device *dev,
                                                       uint8_t index,
                                                       bool cancelable,
                                                       uint32_t duration_ms);
typedef int (*animation_control_api_stop_by_index)(const struct device *dev,
                                                   uint8_t index);

struct animation_control_api {
    // animation control behaves as animation_api
    struct animation_api animation_api_base;
    animation_control_api_enqueue_animation enqueue_animation;
    animation_control_api_play_now play_now;
    animation_control_api_set_enabled set_enabled;
    animation_control_api_set_next_animation set_next_animation;
    animation_control_api_set_animation set_animation;
    animation_control_api_change_brightness change_brightness;
    animation_control_api_play_now_by_index play_now_by_index;
    animation_control_api_enqueue_by_index enqueue_by_index;
    animation_control_api_stop_by_index stop_by_index;
};

static inline int animation_control_enqueue_animation(
    const struct device *dev, const struct device *animation, bool cancelable,
    uint32_t duration_ms) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->enqueue_animation(dev, animation, cancelable, duration_ms);
}

static inline int animation_control_play_now(const struct device *dev,
                                             const struct device *animation,
                                             bool cancelable,
                                             uint32_t duration_ms) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->enqueue_animation(dev, animation, cancelable, duration_ms);
}

static inline void animation_control_set_enabled(const struct device *dev,
                                                 bool enabled) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->set_enabled(dev, enabled);
}

static inline void animation_control_set_next_animation(
    const struct device *dev, int index_offset,
    enum animation_control_power_source target_power_source) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->set_next_animation(dev, index_offset, target_power_source);
}

static inline void animation_control_set_animation(
    const struct device *dev, int index,
    enum animation_control_power_source target_power_source) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->set_animation(dev, index, target_power_source);
}

static inline void animation_control_change_brightness(
    const struct device *dev, int brightness_to_increment,
    enum animation_control_power_source target_power_source) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->change_brightness(dev, brightness_to_increment,
                                  target_power_source);
}

static inline int animation_control_enqueue_animation_by_index(
    const struct device *dev, uint8_t index, bool cancelable,
    uint32_t duration_ms) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->enqueue_by_index(dev, index, cancelable, duration_ms);
}

static inline int animation_control_play_now_by_index(const struct device *dev,
                                                      uint8_t index,
                                                      bool cancelable,
                                                      uint32_t duration_ms) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->play_now_by_index(dev, index, cancelable, duration_ms);
}

static inline int animation_control_stop_by_index(const struct device *dev,
                                                  uint8_t index) {
    const struct animation_control_api *api =
        (const struct animation_control_api *)dev->api;
    return api->stop_by_index(dev, index);
}

// workaround to use from behavior without adding compile time dependency
#if DT_HAS_CHOSEN(zmk_animation_control)

int animation_control_enqueue_animation0(const struct device *animation,
                                         bool cancelable, uint32_t duration_ms);
int animation_control_play_now0(const struct device *animation, bool cancelable,
                                uint32_t duration_ms);
void animation_control_set_enabled0(bool enabled);
void animation_control_set_next_animation0(
    int index_offset, enum animation_control_power_source target_power_source);

void animation_control_set_animation0(
    int index, enum animation_control_power_source target_power_source);
void animation_control_change_brightness0(
    int brightness_to_increment,
    enum animation_control_power_source target_power_source);

int animation_control_enqueue_animation_by_index0(uint8_t index,
                                                  bool cancelable,
                                                  uint32_t duration_ms);
int animation_control_play_now_by_index0(uint8_t index, bool cancelable,
                                         uint32_t duration_ms);
int animation_control_stop_by_index0(uint8_t index);
#endif
