#ifndef RR_CAPTURE_H
#define RR_CAPTURE_H
#include <windows.h>
#ifndef WDA_EXCLUDEFROMCAPTURE
#define WDA_EXCLUDEFROMCAPTURE 0x00000011
#endif

/* Owned top-level windows only. This is capture exclusion, not process hiding.
 * Older Windows/capture paths may ignore it. Query the exact value rather than
 * reporting WDA_MONITOR (black rectangle) as successful exclusion. */
typedef struct RR_CaptureState {
    int requested;
    int status; /* 0 disabled, 1 requested affinity verified, -1 API failed */
    DWORD attempted;
} RR_CaptureState;

static int rr_capture_update(HWND window, int enabled, RR_CaptureState* state) {
    DWORD now = GetTickCount(), actual = 0;
    DWORD wanted = enabled ? WDA_EXCLUDEFROMCAPTURE : WDA_NONE;
    enabled = !!enabled;
    if (state->requested == enabled &&
        (state->status >= 0 || now - state->attempted < 1000)) return state->status;
    state->requested = enabled;
    state->attempted = now;
    if (!SetWindowDisplayAffinity(window, wanted) ||
        !GetWindowDisplayAffinity(window, &actual) || actual != wanted) {
        state->status = -1;
        OutputDebugStringA("RRTool: capture affinity failed or unsupported.\n");
    } else {
        state->status = enabled;
    }
    return state->status;
}
#endif
