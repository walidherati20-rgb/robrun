/* Read-only StateModel chains supplied by the user, corroborated by the
 * local 2026-11-14 dump. Unix milliseconds, never GetTickCount().
 * Knockout phase is not required: the timestamp is the active signal. */
#ifndef RR_TARGET_STATE_H
#define RR_TARGET_STATE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
typedef struct {
    int dead_known, dead, down_known, down;
    int64_t end_ts;
} RR_TARGET_STATE;
typedef struct {
    uint64_t (*pointer)(uint64_t);
    int (*is_type)(uint64_t, const char*);
    int (*read)(uint64_t, void*, size_t);
    int (*read_string)(uint64_t, char*, size_t);
} RR_TARGET_STATE_IO;
static uint64_t rr_state_data(uint64_t object, const char* data_type,
                              const RR_TARGET_STATE_IO* io) {
    if (io->is_type(object, data_type)) return object;
    if (!io->is_type(object, "PrimitiveModel`1")) return 0;
    uint64_t data = io->pointer(object + 0x10);
    return data && io->is_type(data, data_type) ? data : 0;
}
static RR_TARGET_STATE rr_target_state(uint64_t character, int64_t unix_ms,
                                        const RR_TARGET_STATE_IO* io) {
    RR_TARGET_STATE result = {0};
    if (!character || !io || !io->pointer || !io->is_type || !io->read ||
        !io->read_string || !io->is_type(character, "BattleCharacterModel")) return result;
    uint64_t state = io->pointer(character + 0x1E0);
    if (!state || !io->is_type(state, "BattleCharacterStateModel")) return result;
    uint64_t model = io->pointer(state + 0x10);
    if (model && io->is_type(model, "PrimitiveModel`1")) {
        uint64_t data = io->pointer(model + 0x10);
        unsigned char value;
        if (data && io->is_type(data, "BoolData") &&
            io->read(data + 0x18, &value, 1) && value <= 1) {
            result.dead_known = 1; result.dead = value;
        }
    }
    uint64_t knockout = io->pointer(state + 0x30);
    if (knockout && io->is_type(knockout, "KnockoutModel")) {
        /* Accept a validated direct LongData or a primitive wrapper around it. */
        uint64_t data = rr_state_data(io->pointer(knockout + 0x10), "LongData", io);
        int64_t end_ts;
        if (data && io->is_type(data, "LongData") &&
            io->read(data + 0x18, &end_ts, 8) && end_ts >= 0 && unix_ms > 0) {
            result.end_ts = end_ts;
            result.down_known = 1;
            result.down = end_ts > unix_ms && end_ts - unix_ms > 1000;
        }
    }
    return result;
}
#endif
