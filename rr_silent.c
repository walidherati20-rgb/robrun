/* rr_silent.c - Silent aim writer thread.
 *
 * The frame thread publishes the eye and the target point once per snapshot.
 * This thread walks the local character's look models, normalises the
 * eye->point direction and pins every copy the shot path can read:
 *
 *   LookDirectionModel._value             +0x10
 *   ShotInterpolationModel._lookDirection +0x1C
 *   LastLookDirectionModel.Direction      +0x18
 *
 * The look tick rebuilds _value from the mouse every frame, so a write that
 * lands between ticks is lost by the time ShotArgsSystem reads it. Polling at
 * 4 kHz means the direction is re-stated within 250 us of being overwritten and
 * stays in place for the rest of the frame. A copy that already points at the
 * target costs one read and no write.
 *
 * No restore is needed: the game's own look tick republishes the mouse
 * direction on the next frame, so releasing the key snaps back by itself.
 */
#include <windows.h>
#include <math.h>
#include <string.h>

#include "rr_silent.h"
#include "rr_mem.h"
#include "rr_unity.h"

/* BattleCharacterModel fields (dump 20261107 / verified by class name at run
 * time) and the class each one must resolve to. */
#define RR_OFF_CHAR_LOOK      0x70u   /* BattleCharacterModel.LookDirection      */
#define RR_OFF_CHAR_LASTLOOK  0x88u   /* BattleCharacterModel.LastLookDirection  */
#define RR_OFF_CHAR_SHOTINT   0x108u  /* BattleCharacterModel.ShotInterpolation  */
#define RR_OFF_LOOK_VALUE     0x10u   /* LookDirectionModel._value               */
#define RR_OFF_SHOT_LOOKDIR   0x1Cu   /* ShotInterpolationModel._lookDirection   */
#define RR_OFF_LAST_DIR       0x18u   /* LastLookDirectionModel.Direction        */

#define SA_CHAIN_MS           250u    /* pointers only move on respawn          */
#define SA_BEAT_MAX_MS        500u    /* stop writing if the feed died          */
#define SA_FAIL_MAX           40

typedef struct {
    uint64_t local;
    uint64_t input;
    uint64_t slot[RR_SA_SLOTS];   /* look, shotint, lastlook */
    DWORD    at;
    int      input_ok;
} SA_CHAIN;

static volatile LONG   g_sa_stop;
static volatile LONG   g_sa_active;
static volatile LONG   g_sa_status;
static volatile LONG   g_sa_seq;
static volatile LONG   g_sa_writes;
static volatile LONG   g_sa_input_writes;
static volatile LONG   g_sa_polls;
static volatile LONG   g_sa_failures;
static volatile LONG   g_sa_skips;
static volatile LONG   g_sa_mode;
static volatile LONG64 g_sa_local;
static volatile LONG64 g_sa_input;
static volatile DWORD  g_sa_beat;
static volatile float  g_sa_eye[3];
static volatile float  g_sa_pt[3];
static HANDLE          g_sa_thread;

int rr_silent_status(void)   { return (int)InterlockedCompareExchange(&g_sa_status, 0, 0); }
int rr_silent_writes(void)   { return (int)InterlockedCompareExchange(&g_sa_writes, 0, 0); }
int rr_silent_failures(void) { return (int)InterlockedCompareExchange(&g_sa_failures, 0, 0); }
int rr_silent_polls(void)    { return (int)InterlockedCompareExchange(&g_sa_polls, 0, 0); }
int rr_silent_skips(void)    { return (int)InterlockedCompareExchange(&g_sa_skips, 0, 0); }
int rr_silent_input_writes(void) { return (int)InterlockedCompareExchange(&g_sa_input_writes, 0, 0); }

void rr_silent_mode(int mode) {
    if (mode < RR_SA_MODE_LOOK || mode > RR_SA_MODE_BOTH) mode = RR_SA_MODE_LOOK;
    InterlockedExchange(&g_sa_mode, mode);
}

void rr_silent_idle(int status) {
    InterlockedExchange(&g_sa_status, status);
    InterlockedExchange(&g_sa_active, 0);
}

void rr_silent_aim(uint64_t local_character, uint64_t input_model,
                   const RR_V3* eye, const RR_V3* point) {
    if ((!local_character && !input_model) || !eye || !point) {
        rr_silent_idle(2);
        return;
    }
    InterlockedIncrement(&g_sa_seq);                 /* odd: update in progress */
    g_sa_eye[0] = eye->x;   g_sa_eye[1] = eye->y;   g_sa_eye[2] = eye->z;
    g_sa_pt[0]  = point->x; g_sa_pt[1]  = point->y; g_sa_pt[2]  = point->z;
    InterlockedExchange64(&g_sa_local, (LONG64)local_character);
    InterlockedExchange64(&g_sa_input, (LONG64)input_model);
    InterlockedExchange((volatile LONG*)&g_sa_beat, (LONG)GetTickCount());
    InterlockedExchange(&g_sa_status, 1);
    InterlockedExchange(&g_sa_active, 1);
    InterlockedIncrement(&g_sa_seq);                 /* even: consistent */
}

/* The posted external method writes BattleInputModel, so the pair has to be
 * plausible before we touch it: class name first, then the live radians. */
static int sa_input_ok(uint64_t input) {
    if (!input) return 0;
    if (u_is(input, "BattleInputModel")) return 1;
    float p[2];
    if (!rr_read(input + RR_OFF_INPUT_LOOK, p, sizeof p)) return 0;
    return isfinite(p[0]) && isfinite(p[1]) &&
           fabsf(p[0]) <= 3.15f && fabsf(p[1]) <= 1.58f;
}

/* Pointers only move on respawn, so the chain is re-read four times a second.
 * Every piece is validated by class name: a patch that moves a field leaves
 * that slot zero and the others keep working. */
static int sa_chain(SA_CHAIN* c) {
    const uint64_t local = (uint64_t)InterlockedCompareExchange64(&g_sa_local, 0, 0);
    const uint64_t input = (uint64_t)InterlockedCompareExchange64(&g_sa_input, 0, 0);
    if (!local && !input) return 0;
    const DWORD now = timeGetTime();
    if (local == c->local && input == c->input && now - c->at <= SA_CHAIN_MS) return 1;

    c->local = local;
    c->input = input;
    c->at = now;
    c->slot[RR_SA_LOOK] = c->slot[RR_SA_SHOTINT] = c->slot[RR_SA_LASTLOOK] = 0;
    c->input_ok = sa_input_ok(input);

    if (!local) return 1;
    const uint64_t look = rr_u64(local + RR_OFF_CHAR_LOOK);
    if (u_is(look, "LookDirectionModel")) c->slot[RR_SA_LOOK] = look;

    const uint64_t si = rr_u64(local + RR_OFF_CHAR_SHOTINT);
    if (u_is(si, "ShotInterpolationModel")) c->slot[RR_SA_SHOTINT] = si;

    const uint64_t ll = rr_u64(local + RR_OFF_CHAR_LASTLOOK);
    if (u_is(ll, "LastLookDirectionModel")) c->slot[RR_SA_LASTLOOK] = ll;
    return 1;
}

/* Character copies: pin the unit direction into each live slot. */
static int silent_write_look(SA_CHAIN* c, const RR_V3* dir) {
    static const struct { int slot; uint32_t off; } map[RR_SA_SLOTS] = {
        { RR_SA_LOOK,     RR_OFF_LOOK_VALUE },
        { RR_SA_SHOTINT,  RR_OFF_SHOT_LOOKDIR },
        { RR_SA_LASTLOOK, RR_OFF_LAST_DIR }
    };
    int any = 0, wrote = 0, failed = 0;
    for (int i = 0; i < RR_SA_SLOTS; ++i) {
        const uint64_t addr = c->slot[map[i].slot];
        if (!addr) continue;
        any = 1;
        float cur[3];
        if (rr_read(addr + map[i].off, cur, sizeof cur) && !rr_sa_needs_write(cur, dir)) {
            InterlockedIncrement(&g_sa_skips);
            continue;
        }
        if (rr_write(addr + map[i].off, dir, sizeof *dir)) wrote = 1;
        else ++failed;
    }
    if (wrote) InterlockedIncrement(&g_sa_writes);
    if (failed) InterlockedIncrement(&g_sa_failures);
    return any ? (failed ? 3 : 1) : 0;
}

/* Input copy: the posted external method. Radians, yaw = atan2(dx, dz),
 * pitch = -atan2(dy, horizontal), and ShotLookOffset cleared so the shot uses
 * the pair verbatim. */
static int silent_write_input(uint64_t input, const RR_V3* eye, const RR_V3* pt) {
    if (!input) return 0;
    float yaw = 0.0f, pitch = 0.0f;
    if (!rr_sa_input_angles(eye, pt, &yaw, &pitch)) return 0;

    float cur[2];
    if (rr_read(input + RR_OFF_INPUT_LOOK, cur, sizeof cur) &&
        fabsf(cur[0] - yaw) < 0.0005f && fabsf(cur[1] - pitch) < 0.0005f) {
        InterlockedIncrement(&g_sa_skips);
        return 1;
    }

    struct { float yaw, pitch; } pair = { yaw, pitch };
    int32_t off = rr_i32(input + RR_OFF_INPUT_SHOT_OFFSET);
    if (off != 0) {
        int32_t zero = 0;
        rr_write(input + RR_OFF_INPUT_SHOT_OFFSET, &zero, sizeof zero);
    }
    unsigned char changed = 1;
    int ok = rr_write(input + RR_OFF_INPUT_LOOK, &pair, sizeof pair) &&
             rr_write(input + RR_OFF_INPUT_CHANGED, &changed, 1);
    if (ok) InterlockedIncrement(&g_sa_input_writes);
    else InterlockedIncrement(&g_sa_failures);
    return ok ? 1 : 3;
}

/* One pass for the selected mode(s). Returns 0 when nothing resolved,
 * otherwise 1, with bit 1 set when a write was refused. */
static int silent_write(SA_CHAIN* c, const RR_V3* dir,
                        const RR_V3* eye, const RR_V3* pt) {
    const int mode = (int)InterlockedCompareExchange(&g_sa_mode, 0, 0);
    int any = 0, failed = 0;
    if (mode != RR_SA_MODE_INPUT) {
        int r = silent_write_look(c, dir);
        if (r) any = 1;
        if (r & 2) failed = 1;
    }
    if (mode != RR_SA_MODE_LOOK && c->input_ok) {
        int r = silent_write_input(c->input, eye, pt);
        if (r) any = 1;
        if (r & 2) failed = 1;
    }
    return any ? (failed ? 3 : 1) : 0;
}

static DWORD WINAPI silent_writer(LPVOID unused) {
    (void)unused;
    timeBeginPeriod(1);
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    const LONGLONG slot = freq.QuadPart / RR_SILENT_POLL_HZ;
    SA_CHAIN chain;
    memset(&chain, 0, sizeof chain);
    int consecutive_failures = 0;

    for (;;) {
        if (InterlockedCompareExchange(&g_sa_stop, 0, 0)) break;
        if (!InterlockedCompareExchange(&g_sa_active, 0, 0)) {
            Sleep(10);              /* off: no polling at all */
            continue;
        }
        const DWORD now = GetTickCount();
        /* The frame thread crashed or stopped publishing: never keep writing
         * to an address nobody validates anymore. */
        if (now - (DWORD)InterlockedCompareExchange((volatile LONG*)&g_sa_beat, 0, 0) > SA_BEAT_MAX_MS) {
            InterlockedExchange(&g_sa_status, 0);
            InterlockedExchange(&g_sa_active, 0);
            continue;
        }

        LARGE_INTEGER start;
        QueryPerformanceCounter(&start);

        LONG s0 = InterlockedCompareExchange(&g_sa_seq, 0, 0);
        if (!(s0 & 1)) {
            RR_V3 eye  = { g_sa_eye[0], g_sa_eye[1], g_sa_eye[2] };
            RR_V3 pt   = { g_sa_pt[0],  g_sa_pt[1],  g_sa_pt[2]  };
            LONG s1 = InterlockedCompareExchange(&g_sa_seq, 0, 0);
            RR_V3 delta = { pt.x - eye.x, pt.y - eye.y, pt.z - eye.z };
            RR_V3 dir;
            if (s0 == s1 && rr_dir_unit(&delta, &dir)) {
                InterlockedIncrement(&g_sa_polls);
                int chain_ok = sa_chain(&chain);
                int live = chain_ok ? silent_write(&chain, &dir, &eye, &pt) : 0;
                if (!live) {
                    InterlockedExchange(&g_sa_status, 4);
                } else {
                    InterlockedExchange(&g_sa_status, 1);
                    if (live & 2) ++consecutive_failures;
                    else consecutive_failures = 0;
                }
            }
        }

        if (consecutive_failures > SA_FAIL_MAX) {
            InterlockedExchange(&g_sa_status, 3);
            InterlockedExchange(&g_sa_active, 0);
        }

        /* Yield the rest of the slot: Sleep(0) hands the CPU to the game and
         * never spins hot, but the deadline keeps the poll at 4 kHz when the
         * scheduler has room. Slots lost to a long frame are simply skipped. */
        for (;;) {
            LARGE_INTEGER nowq;
            QueryPerformanceCounter(&nowq);
            if (nowq.QuadPart - start.QuadPart >= slot) break;
            Sleep(0);
        }
    }
    InterlockedExchange(&g_sa_active, 0);
    InterlockedExchange(&g_sa_status, 0);
    timeEndPeriod(1);
    return 0;
}

int rr_silent_start(void) {
    if (g_sa_thread) return 1;
    InterlockedExchange(&g_sa_stop, 0);
    g_sa_thread = CreateThread(NULL, 0, silent_writer, NULL, 0, NULL);
    return g_sa_thread != NULL;
}

void rr_silent_stop(void) {
    InterlockedExchange(&g_sa_active, 0);
    InterlockedExchange(&g_sa_stop, 1);
    if (g_sa_thread) {
        WaitForSingleObject(g_sa_thread, 200);
        CloseHandle(g_sa_thread);
        g_sa_thread = NULL;
    }
}
