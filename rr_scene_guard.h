#ifndef RR_SCENE_GUARD_H
#define RR_SCENE_GUARD_H
#include <stdint.h>
/* Reader-owned scene identity. Never accept a fallback/old camera as a new
 * scene; two consecutive coherent samples are required after a transition. */
typedef struct {
    uint64_t world, camera;
    unsigned epoch, coherent_samples;
} RR_SCENE_GUARD;
static int rr_scene_observe(RR_SCENE_GUARD* state, uint64_t world, uint64_t camera, int coherent) {
    if (world != state->world || camera != state->camera) {
        state->world = world; state->camera = camera;
        if (++state->epoch == 0) ++state->epoch;
        state->coherent_samples = 0;
    }
    if (!world || !camera || !coherent) { state->coherent_samples = 0; return 0; }
    if (state->coherent_samples < 2) ++state->coherent_samples;
    return state->coherent_samples >= 2;
}
#endif
