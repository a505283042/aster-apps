/* SPDX-License-Identifier: MIT */
#ifndef HIFI_SHAKE_H
#define HIFI_SHAKE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float gravity[3], peak[3], peak_energy;
    uint32_t last_sample, peak_at;
    bool primed, wait_for_rest;
    uint32_t rest_ms;
} shake_t;

/* The accelerometer includes gravity and uses m/s² on all three axes. */
bool shake_update(shake_t *shake, const float accel[3], uint32_t now);

#endif
