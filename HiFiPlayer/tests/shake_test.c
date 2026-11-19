/* SPDX-License-Identifier: MIT */
#include "shake.h"
#include <assert.h>

int main(void)
{
    const float stationary[] = {0,0,9.81f}, tilt[] = {9.81f,0,0};
    const float left[] = {-17.f,0,9.81f}, right[] = {17.f,0,9.81f};
    const float weak[] = {5.f,0,9.81f};
    shake_t s = {0};
    assert(!shake_update(&s, stationary, 0));
    assert(!shake_update(&s, tilt, 30));
    assert(!shake_update(&s, stationary, 60));
    assert(!shake_update(&s, weak, 100));
    assert(!shake_update(&s, left, 130));
    assert(!shake_update(&s, left, 165));
    assert(!shake_update(&s, stationary, 198));
    assert(shake_update(&s, right, 232));
    assert(!shake_update(&s, left, 260));
    assert(!shake_update(&s, right, 590)); /* A gap starts a fresh baseline. */
    return 0;
}
