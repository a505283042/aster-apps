/* SPDX-License-Identifier: MIT */
#include "shake.h"

enum { REVERSAL_MIN_MS = 45, REVERSAL_MAX_MS = 420, SAMPLE_GAP_MS = 160 };

bool shake_update(shake_t *s, const float accel[3], uint32_t now)
{
    float magnitude = 0;
    for (unsigned i = 0; i < 3; i++) magnitude += accel[i] * accel[i];
    if (!(magnitude <= 70.f * 70.f)) return false;
    if (!s->primed || now - s->last_sample > SAMPLE_GAP_MS) {
        for (unsigned i = 0; i < 3; i++) s->gravity[i] = accel[i];
        s->primed = true;
        s->last_sample = now;
        s->peak_energy = 0;
        return false;
    }
    s->last_sample = now;

    float impulse[3], energy = 0, dot = 0;
    for (unsigned i = 0; i < 3; i++) {
        impulse[i] = accel[i] - s->gravity[i];
        energy += impulse[i] * impulse[i];
        dot += impulse[i] * s->peak[i];
        s->gravity[i] += impulse[i] * .16f;
    }

    uint32_t since_peak = now - s->peak_at;
    if (since_peak > REVERSAL_MAX_MS) s->peak_energy = 0;
    /* Turning the device normally changes the gravity vector without a large
     * change in its magnitude. Require a strong acceleration in both directions. */
    if (energy < 11.f * 11.f || (magnitude > 6.f * 6.f && magnitude < 13.f * 13.f))
        return false;
    if (s->peak_energy && since_peak >= REVERSAL_MIN_MS && dot < 0 &&
        dot * dot >= energy * s->peak_energy * .20f) {
        s->peak_energy = 0;
        return true;
    }
    if (!s->peak_energy || (since_peak < REVERSAL_MIN_MS && dot > 0 && energy > s->peak_energy)) {
        for (unsigned i = 0; i < 3; i++) s->peak[i] = impulse[i];
        s->peak_energy = energy;
        s->peak_at = now;
    }
    return false;
}
