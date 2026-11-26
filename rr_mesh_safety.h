/* Pure geometry/ancestor checks for live mesh reads. No old poses or guesses. */
#ifndef RR_MESH_SAFETY_H
#define RR_MESH_SAFETY_H
#include <math.h>
static int rr_mesh_span_covers(const int* parents, int first, int count,
                               const int* seeds, int seed_count) {
    if (!parents || !seeds || first < 0 || count <= 0 || count > 2048) return 0;
    for (int s = 0; s < seed_count; ++s) {
        int index = seeds[s], depth = 0;
        if (index == -1) continue; /* absent optional joint */
        while (index != -1) {
            if (++depth > 128 || index < first || index - first >= count) return 0;
            index = parents[index - first];
        }
    }
    return 1;
}
/* A seated mesh is checked around its live measured root, not around a
 * potentially lagging replicated standing origin. Keep a bounded association
 * to the model and require a coherent, human-sized head/spine/root chain. */
static int rr_mesh_anchors_valid(const float points[5][3],
                                 const float model[3], int seated) {
    if (!points || !model) return 0;
    float displacement = 0;
    for (int a = 0; a < 3; ++a) {
        if (!isfinite(model[a]) || !isfinite(points[4][a])) return 0;
        float d = points[4][a] - model[a];
        displacement += d*d;
    }
    if (seated && displacement > 36.0f) return 0;
    for (int p = 0; p < 5; ++p) {
        float span = 0;
        for (int a = 0; a < 3; ++a) {
            float v = points[p][a];
            if (!isfinite(v) || fabsf(v) > 500000.0f) return 0;
            float d = v - points[4][a]; span += d*d;
            if (!seated && fabsf(v-model[a]) > (a == 1 ? 3.0f : 2.0f)) return 0;
        }
        if (span > 9.0f) return 0;
    }
    return 1;
}
#endif
