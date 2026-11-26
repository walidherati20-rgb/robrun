/* Small float save/zero/restore book. Caller validates ownership chains.
 * Rejects changed class identities and preserves newer engine values.
 * Same-class address reuse cannot be detected from a class word alone. */
#ifndef RR_WEAPON_PATCH_H
#define RR_WEAPON_PATCH_H
#include <stdint.h>
#include <stddef.h>
#include <math.h>
#include <string.h>
typedef struct {
    uint64_t owner, klass, address;
    float original;
    int active;
} RR_WEAPON_FIELD;
typedef struct {
    uint64_t owner, klass, address;
    unsigned char original;
    int active;
} RR_WEAPON_BOOL;
typedef struct {
    int (*read)(uint64_t, void*, size_t);
    int (*write)(uint64_t, const void*, size_t);
} RR_WEAPON_IO;
static int rr_weapon_restore_field(RR_WEAPON_FIELD* p, const RR_WEAPON_IO* io) {
    uint64_t klass;
    float live;
    if (!p->active) return 1;
    if (!io->read(p->owner, &klass, 8)) return 0;
    if (klass != p->klass) { p->active = 0; return 1; }
    if (!io->read(p->address, &live, 4)) return 0;
    if (live != 0.0f) { p->active = 0; return 1; }
    if (!io->write(p->address, &p->original, 4)) return 0;
    if (!io->read(p->address, &live, 4) ||
        memcmp(&live, &p->original, 4)) return 0;
    p->active = 0;
    return 1;
}
static int rr_weapon_zero_field(RR_WEAPON_FIELD* p, uint64_t owner,
                               unsigned offset, const RR_WEAPON_IO* io) {
    uint64_t klass;
    float live, zero = 0.0f;
    if (!owner) return rr_weapon_restore_field(p, io);
    if (!io->read(owner, &klass, 8) || !klass) return 0;
    if (p->active && (p->owner != owner || p->klass != klass ||
                      p->address != owner + offset))
        if (!rr_weapon_restore_field(p, io)) return 0;
    if (!io->read(owner + offset, &live, 4) ||
        !isfinite(live) || fabsf(live) > 10000.0f) return 0;
    if (!p->active || live != 0.0f) p->original = live;
    p->owner = owner; p->klass = klass; p->address = owner + offset;
    /* Save before writing: a reported failure may still have landed. */
    p->active = 1;
    if (live != 0.0f && !io->write(p->address, &zero, 4)) return 0;
    return io->read(p->address, &live, 4) && live == 0.0f;
}
static int rr_weapon_restore_bool(RR_WEAPON_BOOL* p, const RR_WEAPON_IO* io) {
    uint64_t klass;
    unsigned char live;
    if (!p->active) return 1;
    if (!io->read(p->owner, &klass, 8)) return 0;
    if (klass != p->klass) { p->active = 0; return 1; }
    if (!io->read(p->address, &live, 1)) return 0;
    if (live != 0) { p->active = 0; return 1; }
    if (!io->write(p->address, &p->original, 1)) return 0;
    if (!io->read(p->address, &live, 1) || live != p->original) return 0;
    p->active = 0;
    return 1;
}
static int rr_weapon_zero_bool(RR_WEAPON_BOOL* p, uint64_t owner,
                               unsigned offset, const RR_WEAPON_IO* io) {
    uint64_t klass;
    unsigned char live, zero = 0;
    if (!owner) return rr_weapon_restore_bool(p, io);
    if (!io->read(owner, &klass, 8) || !klass) return 0;
    if (p->active && (p->owner != owner || p->klass != klass ||
                      p->address != owner + offset))
        if (!rr_weapon_restore_bool(p, io)) return 0;
    if (!io->read(owner + offset, &live, 1) || live > 1) return 0;
    if (!p->active || live) p->original = live;
    p->owner = owner; p->klass = klass; p->address = owner + offset;
    p->active = 1;
    if (live && !io->write(p->address, &zero, 1)) return 0;
    return io->read(p->address, &live, 1) && live == 0;
}
#endif
