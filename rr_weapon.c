/* External weapon controls. Recoil uses mouse input, not data writes;
 * spread data fields may be cosmetic or server overridden. */
#include <windows.h>
#include <stdio.h>
#include "rr_mem.h"
#include "rr_unity.h"
#include "rr_weapon.h"
#include "rr_weapon_patch.h"
#include "rr_weapon_paths.h"

static HANDLE g_thread;
static SRWLOCK g_lock = SRWLOCK_INIT;
static uint64_t g_battle, g_local;
static DWORD g_beat;
static int g_recoil, g_spread, g_pause;
static volatile LONG g_stop, g_spread_status;
extern void rr_recoil_publish(int on, int pause);
extern int rr_recoil_status(void);
extern int rr_recoil_start(void);
extern void rr_recoil_stop(void);
static const RR_WEAPON_IO g_io = { rr_read, rr_write };
/* These callbacks preserve the resolver's fail-closed behavior, while
 * recording the first bad read/type for periodic diagnostics. Worker only. */
static int g_trace;
static uint64_t g_bad_address, g_bad_object;
static char g_expected[160], g_observed[160];
static uint64_t trace_pointer(uint64_t address) {
    uint64_t p = 0;
    int ok = rr_read(address, &p, sizeof p);
    if (g_trace && !g_expected[0] && (!ok || !PL(p))) {
        g_bad_address = address; g_bad_object = p;
        strcpy_s(g_expected, sizeof g_expected, "managed pointer");
        strcpy_s(g_observed, sizeof g_observed, ok ? "null/invalid pointer" : "read failed");
    }
    return ok ? p : 0;
}
static int trace_type(uint64_t p, const char* type) {
    int ok = u_is(p, type);
    if (g_trace && !ok && !g_expected[0]) {
        g_bad_object = p;
        strcpy_s(g_expected, sizeof g_expected, type);
        if (!u_class_name(u_klass(p), g_observed, sizeof g_observed))
            strcpy_s(g_observed, sizeof g_observed, "unreadable class");
    }
    return ok;
}
static const RR_WEAPON_PATH_IO g_path_io = { trace_pointer, trace_type };

static void diagnostic_log(uint64_t battle, uint64_t local, int requested_r,
                           int requested_s, int pause, RR_WEAPON_PATHS paths,
                           unsigned failed, unsigned changed) {
    char path[MAX_PATH];
    DWORD len = GetModuleFileNameA(NULL, path, MAX_PATH);
    if (!len || len >= MAX_PATH) return;
    char* slash = strrchr(path, '\\');
    if (!slash) return;
    slash[1] = 0;
    if (strcat_s(path, sizeof path, "weapon-diagnostic.log")) return;
    FILE* f = fopen(path, "a+");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    /* Bound output; never truncate the user's previous diagnosis. */
    if (ftell(f) < 2 * 1024 * 1024)
        fprintf(f, "21h ms=%lu target_pid=%u requested=%d/%d pause=%d states=%d/%d "
                "battle=%llX local=%llX recoil=%llX fake=%llX data=%llX args=%llX "
                "failed_mask=%X nonzero_before_mask=%X bad_field=%llX bad_object=%llX "
                "expected=\"%s\" observed=\"%s\"\n",
                (unsigned long)GetTickCount(), g_rr_pid, requested_r, requested_s,
                pause, rr_weapon_recoil_status(), rr_weapon_spread_status(),
                (unsigned long long)battle, (unsigned long long)local,
                (unsigned long long)paths.recoil, (unsigned long long)paths.fake,
                (unsigned long long)paths.spread_data, (unsigned long long)paths.spread_args,
                failed, changed, (unsigned long long)g_bad_address,
                (unsigned long long)g_bad_object, g_expected, g_observed);
    fclose(f);
}

int rr_weapon_recoil_status(void) {
    return rr_recoil_status();
}
int rr_weapon_spread_status(void) {
    return (int)InterlockedCompareExchange(&g_spread_status, 0, 0);
}
void rr_weapon_publish(uint64_t battle, uint64_t local, int recoil, int spread,
                       int pause_reason) {
    AcquireSRWLockExclusive(&g_lock);
    g_battle = battle; g_local = local;
    g_recoil = !!recoil; g_spread = !!spread; g_beat = GetTickCount();
    g_pause = pause_reason;
    ReleaseSRWLockExclusive(&g_lock);
    rr_recoil_publish(recoil, pause_reason);
}
static DWORD WINAPI worker(LPVOID unused) {
    RR_WEAPON_FIELD fields[9] = {0};
    RR_WEAPON_BOOL has_spread = {0};
    DWORD last_log = 0;
    int logged_once = 0;
    (void)unused;
    for (;;) {
        uint64_t battle, local;
        int recoil, spread, pause;
        DWORD beat;
        AcquireSRWLockShared(&g_lock);
        battle = g_battle; local = g_local;
        recoil = g_recoil; spread = g_spread; beat = g_beat;
        pause = g_pause;
        ReleaseSRWLockShared(&g_lock);
        int stop = (int)InterlockedCompareExchange(&g_stop, 0, 0);
        int requested_r = recoil, requested_s = spread;
        if (GetTickCount() - beat > 100) pause = 8;
        if (stop || pause) recoil = spread = 0;
        g_trace = !logged_once || GetTickCount() - last_log >= 5000;
        g_bad_address = g_bad_object = 0;
        g_expected[0] = g_observed[0] = 0;

        RR_WEAPON_PATHS paths = rr_weapon_resolve(battle, local, 0, spread, &g_path_io);
        /* FloatData.Value plus the live shot spread/ray offsets. Do not touch
         * random seeds, shared weapon descriptions, or StateSpread objects. */
        const uint64_t owners[9] = {
            0, 0, paths.fake, paths.fake, paths.fake,
            paths.spread_data, paths.spread_args, paths.spread_args, paths.spread_args
        };
        const unsigned offsets[9] = {0x10, 0x14, 0x10, 0x14, 0x18, 0x18, 0x14, 0x20, 0x24};
        int spread_ok = 1;
        unsigned failed = 0, changed = 0;
        for (int i = 2; i < 9; ++i) {
            if (g_trace && owners[i]) {
                float before;
                if (rr_read(owners[i] + offsets[i], &before, sizeof before) &&
                    isfinite(before) && before != 0.0f) changed |= 1u << i;
            }
            int ok = rr_weapon_zero_field(&fields[i], owners[i], offsets[i], &g_io);
            if (!ok) failed |= 1u << i;
            spread_ok &= ok;
        }
        /* The managed shot-args model also carries a boolean HasSpread
         * gate. Its effect on projectiles is unverified. Fail closed on
         * unknown byte values, and restore when disabled/paused. */
        if (g_trace && paths.spread_args) {
            unsigned char before;
            if (rr_read(paths.spread_args + 0x10, &before, 1) && before == 1)
                changed |= 1u << 9;
        }
        if (!rr_weapon_zero_bool(&has_spread, paths.spread_args, 0x10, &g_io)) {
            failed |= 1u << 9;
            spread_ok = 0;
        }
        InterlockedExchange(&g_spread_status, stop || !requested_s ? 0 : pause ? pause :
                            !spread_ok ? 3 : paths.spread_data && paths.spread_args ? 1 :
                            paths.fake ? 4 : 2);
        if (g_trace || stop) {
            diagnostic_log(battle, local, requested_r, requested_s, pause,
                           paths, failed, changed);
            last_log = GetTickCount(); logged_once = 1;
        }
        if (stop) break;
        Sleep(spread ? 1 : 10);
    }
    return 0;
}
int rr_weapon_start(void) {
    if (g_thread) return 1;
    if (!rr_recoil_start()) return 0;
    InterlockedExchange(&g_stop, 0);
    g_thread = CreateThread(NULL, 0, worker, NULL, 0, NULL);
    return g_thread != NULL;
}
void rr_weapon_stop(void) {
    rr_recoil_stop();
    InterlockedExchange(&g_stop, 1);
    if (g_thread) {
        WaitForSingleObject(g_thread, INFINITE);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
}
