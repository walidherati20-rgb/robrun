/* Mock-only tests: Unix timestamps, wrapper hops and fail-closed reads. */
#include <stdio.h>
#include <string.h>
#include "rr_target_state.h"
static int failures, wrong_state, wrong_long, read_fail, end_wrapper, wrong_phase;
static unsigned char dead_byte;
static int64_t end_ts;
static const char* phase_id = "knocked_out";
static uint64_t pointer(uint64_t a) {
    if (a == 0x10000 + 0x1E0) return 0x20000;
    if (a == 0x20000 + 0x10) return 0x30000;
    if (a == 0x30000 + 0x10) return 0x40000;
    if (a == 0x20000 + 0x30) return 0x50000;
    if (a == 0x10000 + 0x298) return 0x50000;
    if (a == 0x50000 + 0x10) return end_wrapper ? 0x61000 : 0x60000;
    if (a == 0x61000 + 0x10) return 0x60000;
    if (a == 0x50000 + 0x18) return 0x70000;
    if (a == 0x70000 + 0x18) return 0x80000;
    if (a == 0x80000 + 0x10) return 0x90000;
    return 0;
}
static int is_type(uint64_t p, const char* wanted) {
    const char* actual = "";
    switch (p) {
        case 0x10000: actual = "BattleCharacterModel"; break;
        case 0x20000: actual = wrong_state ? "OtherState" : "BattleCharacterStateModel"; break;
        case 0x30000: actual = "PrimitiveModel`1"; break;
        case 0x40000: actual = "BoolData"; break;
        case 0x50000: actual = "KnockoutModel"; break;
        case 0x60000: actual = wrong_long ? "FloatData" : "LongData"; break;
        case 0x61000: actual = "PrimitiveModel`1"; break;
        case 0x70000: actual = "PhaseData"; break;
        case 0x80000: actual = wrong_phase ? "OtherDescription" : "KnockoutPhaseDescription"; break;
        case 0x90000: actual = "String"; break;
    }
    return !strcmp(actual, wanted);
}
static int read_string(uint64_t a, char* out, size_t n) {
    if (read_fail || a != 0x90000 || strlen(phase_id) + 1 > n) return 0;
    strcpy_s(out, n, phase_id);
    return (int)strlen(out);
}
static int read_data(uint64_t a, void* out, size_t n) {
    if (read_fail) return 0;
    if (a == 0x40000 + 0x18 && n == 1) { memcpy(out, &dead_byte, n); return 1; }
    if (a == 0x60000 + 0x18 && n == 8) { memcpy(out, &end_ts, n); return 1; }
    return 0;
}
#define CHECK(c, label) do { if (!(c)) { ++failures; printf("FAIL: %s\n", label); } } while (0)
int main(void) {
    RR_TARGET_STATE_IO io = {pointer, is_type, read_data, read_string};
    const int64_t now = 1795260000000LL;
    RR_TARGET_STATE s;
    end_ts = now + 1001;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.dead_known && !s.dead && s.down_known && s.down,
           "living knocked character identified by Unix timestamp");
    phase_id = "delay_after_revive";
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && s.down, "phase string does not override future end timestamp");
    phase_id = "knocked_out";
    end_wrapper = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && s.down, "wrapped LongData also resolves");
    end_wrapper = 0;
    wrong_phase = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && s.down, "foreign phase description does not block timestamp");
    wrong_phase = 0;
    end_ts = now + 1000;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && !s.down, "strict one-second threshold");
    end_ts = 0;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && !s.down, "cleared knockout becomes targetable");
    end_ts = 1767225600000LL;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down_known && !s.down, "stale timestamp is targetable");
    dead_byte = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.dead_known && s.dead && !s.down, "death flag independent of knockout");
    dead_byte = 2; end_ts = -1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(!s.dead_known && !s.down_known, "invalid boolean and timestamp rejected");
    dead_byte = 0; end_ts = INT64_MAX;
    s = rr_target_state(0x10000, now, &io);
    CHECK(s.down, "positive timestamp comparison does not overflow");
    wrong_long = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(!s.down_known && s.dead_known, "wrong timestamp type cannot imply knock");
    wrong_long = 0; read_fail = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(!s.down_known && !s.dead_known, "failed reads never invent a state");
    read_fail = 0; wrong_state = 1;
    s = rr_target_state(0x10000, now, &io);
    CHECK(!s.down_known && !s.dead_known, "foreign state model rejected");
    wrong_state = 0;
    s = rr_target_state(0x10000, 0, &io);
    CHECK(!s.down_known, "missing Unix clock does not interpret an epoch timestamp");
    printf("TARGET STATE TEST %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures != 0;
}
