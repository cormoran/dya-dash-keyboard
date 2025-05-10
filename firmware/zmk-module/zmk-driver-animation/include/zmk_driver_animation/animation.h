/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>

void zmk_animation_request_frames(uint32_t frames);

/**
 * Request some frames depending on the given decremental counter value.
 * It internally requests small frames per pre-defined counter interval.
 * It's useful to avoid requesting too big frames if counter value is very large
 * and animation will be cancled in the middle.
 * @param initial whether this is the first call after animation starts or not.
 *                if true, frame is always requested.
 */
void zmk_animation_request_frames_if_required(uint32_t decremental_counter,
                                              bool initial);
