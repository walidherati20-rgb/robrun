#ifndef RR_FRAME_PACING_H
#define RR_FRAME_PACING_H
#include <stdint.h>
/* Fractional-millisecond cadence using QPC ticks. Advance the deadline rather
 * than resetting it on every frame, so 1 ms worker sleeps do not force 200 FPS. */
static uint64_t rr_frame_period(uint64_t frequency, unsigned fps) {
    return fps ? (frequency + fps - 1) / fps : frequency;
}
static uint64_t rr_frame_advance(uint64_t now, uint64_t deadline, uint64_t period) {
    if (!deadline || now - deadline >= period) return now + period;
    return deadline + period;
}
#endif
