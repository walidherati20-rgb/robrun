/* rr_silent.h - Silent aim: point the shot ray at the target, not the camera.
 *
 * The shot path builds its ray from the local character's look direction, not
 * from BattleInputModel. Three copies can be read on the way to the packet:
 *
 *   BattleCharacterModel.LookDirection        (+0x70) -> LookDirectionModel._value       (+0x10)
 *   BattleCharacterModel.ShotInterpolation    (+0x108) -> ShotInterpolationModel._lookDirection (+0x1C)
 *   BattleCharacterModel.LastLookDirection    (+0x88) -> LastLookDirectionModel.Direction (+0x18)
 *
 * The look tick rewrites _value from the mouse every frame, so a single write
 * per frame loses on the frame the shot is built. rr_silent.c therefore pins
 * all three copies from a 4 kHz poll and re-states the direction the moment the
 * game overwrites it.
 *
 * Every object is validated by class name at run time (u_is), so an offset that
 * moved in a patch degrades to "chain not resolved" instead of a blind write.
 */
#ifndef RR_SILENT_H
#define RR_SILENT_H

#include <math.h>
#include "rr_game.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RR_SILENT_DEG 57.29577951308232f
#define RR_SILENT_RAD 0.017453292519943295f

/* Poll rate of the pin thread. The game looks once per frame (4.2 ms at
 * 240 FPS), so 250 us keeps the mouse direction exposed for a fraction of a
 * frame; the loop yields with Sleep(0) and never spins hot. */
#define RR_SILENT_POLL_HZ 4000

/* A copy that already points this close to the target is left alone. */
#define RR_SILENT_DOT 0.9999f

/* Direction writers, in the order the shot path reads them. */
enum {
    RR_SA_LOOK = 0,
    RR_SA_SHOTINT = 1,
    RR_SA_LASTLOOK = 2,
    RR_SA_SLOTS = 3
};

/* Which model the writer feeds.
 * 0 = character look models only (pinned vector copies),
 * 1 = BattleInputModel angles only (posted external method),
 * 2 = both. */
enum { RR_SA_MODE_LOOK = 0, RR_SA_MODE_INPUT = 1, RR_SA_MODE_BOTH = 2 };

/* BattleInputModel fields, from the dump (BattleModel+0x50). */
#define RR_OFF_INPUT_LOOK         0x18u  /* LookPair { float yaw, pitch } */
#define RR_OFF_INPUT_CHANGED      0x32u  /* IsLookDirectionChanged (bool) */
#define RR_OFF_INPUT_SHOT_OFFSET  0x38u  /* ShotLookOffset (int32, cleared) */

/* Input pair in radians, yaw = atan2(dx, dz), pitch = -atan2(dy, horizontal),
 * clamped to +-89 degrees. Returns 0 when the geometry is degenerate. */
static __inline int rr_sa_input_angles(const RR_V3* eye, const RR_V3* point,
                                       float* yaw, float* pitch) {
    if (!eye || !point || !yaw || !pitch) return 0;
    const float dx = point->x - eye->x;
    const float dy = point->y - eye->y;
    const float dz = point->z - eye->z;
    const float horizontal = sqrtf(dx * dx + dz * dz);
    if (!(horizontal > 0.001f) || !isfinite(horizontal)) return 0;
    const float limit = 89.0f * RR_SILENT_RAD;
    float y = atan2f(dx, dz);
    float p = -atan2f(dy, horizontal);
    if (!isfinite(y) || !isfinite(p)) return 0;
    if (p > limit) p = limit;
    if (p < -limit) p = -limit;
    *yaw = y;
    *pitch = p;
    return 1;
}

/* Normalise in-place copy. Returns 0 for a zero, sub-epsilon or non-finite
 * vector, which is the "no target" case the callers treat as hold. */
static __inline int rr_dir_unit(const RR_V3* in, RR_V3* out) {
    if (!in || !out) return 0;
    float len2 = in->x * in->x + in->y * in->y + in->z * in->z;
    if (!(len2 > 1e-8f) || !isfinite(len2)) return 0;
    float inv = 1.0f / sqrtf(len2);
    out->x = in->x * inv;
    out->y = in->y * inv;
    out->z = in->z * inv;
    return isfinite(out->x) && isfinite(out->y) && isfinite(out->z);
}

/* 1 when the live copy has drifted from the target and must be rewritten. */
static __inline int rr_sa_needs_write(const float cur[3], const RR_V3* want) {
    if (!cur || !want) return 1;
    if (!isfinite(cur[0]) || !isfinite(cur[1]) || !isfinite(cur[2])) return 1;
    float dot = cur[0] * want->x + cur[1] * want->y + cur[2] * want->z;
    return !(dot > RR_SILENT_DOT);
}

/* Sign convention helpers. Kept because the rotation input is still the only
 * place the game publishes the live view angles, and the menu shows them. The
 * silent writer itself works on vectors and needs none of this. */
/* bit 0 flips the yaw sign, bit 1 flips the pitch sign. */
enum { RR_LOOK_HYP_0 = 0, RR_LOOK_HYP_1 = 1, RR_LOOK_HYP_2 = 2, RR_LOOK_HYP_3 = 3 };

/* Forward vector for a yaw/pitch pair under one sign hypothesis. */
static __inline RR_V3 rr_look_forward(int hyp, float yaw_deg, float pitch_deg) {
    float y = yaw_deg * RR_SILENT_RAD, p = pitch_deg * RR_SILENT_RAD;
    float sy = sinf(y), cy = cosf(y), sp = sinf(p), cp = cosf(p);
    RR_V3 v;
    v.x = ((hyp & 1) ? -sy : sy) * cp;
    v.y = (hyp & 2) ? sp : -sp;
    v.z = cy * cp;
    return v;
}

/* Inverse of rr_look_forward for a unit direction. Extra components are
 * ignored; yaw is wrapped to (-180, 180]. */
static __inline int rr_look_angles(int hyp, const RR_V3* dir, float* yaw_deg, float* pitch_deg) {
    if (!dir || !yaw_deg || !pitch_deg) return 0;
    float x = dir->x, y = dir->y, z = dir->z;
    float len = sqrtf(x * x + y * y + z * z);
    if (!(len > 1e-4f) || !isfinite(len)) return 0;
    x /= len; y /= len; z /= len;
    float pitch = asinf(fmaxf(-1.0f, fminf(1.0f, y))) * RR_SILENT_DEG;
    if (!(hyp & 2)) pitch = -pitch;
    float yaw = atan2f((hyp & 1) ? -x : x, z) * RR_SILENT_DEG;
    while (yaw > 180.0f) yaw -= 360.0f;
    while (yaw <= -180.0f) yaw += 360.0f;
    *yaw_deg = yaw;
    *pitch_deg = pitch;
    return isfinite(yaw) && isfinite(pitch);
}

/* Rotate (0, 0, 1) by a Unity quaternion stored as x, y, z, w. */
static __inline RR_V3 rr_quat_forward(const float q[4]) {
    float x = q[0], y = q[1], z = q[2], w = q[3];
    RR_V3 v;
    v.x = 2.0f * (x * z + w * y);
    v.y = 2.0f * (y * z - w * x);
    v.z = 1.0f - 2.0f * (x * x + y * y);
    return v;
}

/* Pick the hypothesis whose forward best matches the live rotation. Returns
 * the hypothesis index, or -1 when the inputs are unusable (zero quaternion,
 * non-finite values, or no candidate close enough). *score receives the best
 * dot product. */
static __inline int rr_look_calibrate(float yaw_deg, float pitch_deg, const RR_V3* fwd, float* score) {
    if (!fwd) return -1;
    if (score) *score = -1.0f;
    float flen = sqrtf(fwd->x * fwd->x + fwd->y * fwd->y + fwd->z * fwd->z);
    if (!(flen > 1e-4f) || !isfinite(flen) || !isfinite(yaw_deg) || !isfinite(pitch_deg))
        return -1;
    if (fabsf(pitch_deg) > 89.5f) return -1; /* near the pole yaw is ambiguous */
    int best = -1;
    float best_dot = -2.0f;
    for (int hyp = 0; hyp < 4; ++hyp) {
        RR_V3 f = rr_look_forward(hyp, yaw_deg, pitch_deg);
        float dot = (f.x * fwd->x + f.y * fwd->y + f.z * fwd->z) / flen;
        if (dot > best_dot) { best_dot = dot; best = hyp; }
    }
    if (score) *score = best_dot;
    return best_dot >= 0.9f ? best : -1;
}

/* Writer thread and hand-off.
 * status: 0 off, 1 aiming, 2 held without a target, 3 writes refused,
 *         4 target but the look models did not validate. */
int  rr_silent_start(void);
void rr_silent_stop(void);
/* Publish a live target: local_character is BattleCharacterModel*, input_model
 * is BattleModel+0x50 (BattleInputModel*, may be 0), eye is the camera the shot
 * leaves from, point is the joint the ray should hit. */
void rr_silent_aim(uint64_t local_character, uint64_t input_model,
                   const RR_V3* eye, const RR_V3* point);
/* See RR_SA_MODE_*. Clamped to 0..2. */
void rr_silent_mode(int mode);
void rr_silent_idle(int status);
int  rr_silent_status(void);
int  rr_silent_writes(void);
int  rr_silent_input_writes(void);
int  rr_silent_failures(void);
int  rr_silent_polls(void);
int  rr_silent_skips(void);

#ifdef __cplusplus
}
#endif
#endif
