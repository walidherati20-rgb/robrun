/* Standalone owned-window test: no game, driver, hooks or input. */
#include "rr_capture.h"
#include <stdio.h>

int main(void) {
    RR_CaptureState state = {-1, 0, 0}, invalid = {-1, 0, 0};
    HWND window = CreateWindowExW(0, L"STATIC", L"Capture affinity test",
        WS_POPUP, 0, 0, 32, 32, NULL, NULL, GetModuleHandleW(NULL), NULL);
    DWORD actual = 0;
    int failures = 0;
    if (!window) return 2;
#define CHECK(condition, label) do { int ok = !!(condition); \
    printf("[%s] %s\n", ok ? "PASS" : "FAIL", label); failures += !ok; } while (0)
    CHECK(rr_capture_update(window, 0, &state) == 0 &&
        GetWindowDisplayAffinity(window, &actual) && actual == WDA_NONE, "off restores normal capture");
    CHECK(rr_capture_update(window, 1, &state) == 1 &&
        GetWindowDisplayAffinity(window, &actual) && actual == WDA_EXCLUDEFROMCAPTURE,
        "on requests and verifies exact exclusion value");
    CHECK(rr_capture_update(window, 1, &state) == 1, "unchanged setting stays enabled");
    CHECK(rr_capture_update(window, 0, &state) == 0 &&
        GetWindowDisplayAffinity(window, &actual) && actual == WDA_NONE, "disable restores capture");
    CHECK(rr_capture_update(NULL, 1, &invalid) == -1, "invalid window reports failure");
    CHECK(rr_capture_update(window, 0, &invalid) == 0, "changed request recovers from failure");
    DestroyWindow(window);
    printf("No game process accessed. Discord not tested.\n");
    return failures ? 1 : 0;
}
