/* rr_reveal_patch.h - Bounded save/write/restore book for reveal timers.
 *
 * The caller owns identity validation and synchronization. Every entry is
 * tied to an owner object and its class word so a recycled heap object cannot
 * receive a stale restore value. Values are plain little-endian floats.
 */
#ifndef RR_REVEAL_PATCH_H
#define RR_REVEAL_PATCH_H
#include <stdint.h>
#include <string.h>
#include <math.h>

#define RR_REVEAL_MAX_PATCHES 520

typedef struct {
    uint64_t address;      /* float address                                  */
    uint64_t owner;        /* object containing the float                    */
    uint64_t owner_klass;  /* class word observed when the value was saved   */
    uint64_t root;         /* validated InventoryRevealViewDescriptions      */
    uint64_t root_klass;   /* class word of root                             */
    uint32_t original;     /* bits to restore                                */
    uint32_t replacement;  /* bits written while enabled                     */
    int owner_offset;      /* field offset inside owner                      */
    int active;
} RR_REVEAL_PATCH;

typedef struct {
    RR_REVEAL_PATCH fields[RR_REVEAL_MAX_PATCHES];
    unsigned skipped;
} RR_REVEAL_BOOK;

typedef struct {
    int (*read)(uint64_t, void*, size_t);
    int (*write)(uint64_t, const void*, size_t);
    int (*valid)(const RR_REVEAL_PATCH*);
} RR_REVEAL_IO;

static uint32_t rr_reveal_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, 4);
    return bits;
}

/* Finite and within the widest duration this game uses. Zero is valid: it is
 * the value that lets the state machine skip a phase. */
static int rr_reveal_duration(uint32_t bits) {
    float value;
    memcpy(&value, &bits, 4);
    return isfinite(value) && value >= 0.0f && value <= 120.0f;
}

static int rr_reveal_pending(const RR_REVEAL_BOOK* book) {
    int n = 0;
    for (int i = 0; i < RR_REVEAL_MAX_PATCHES; ++i) n += !!book->fields[i].active;
    return n;
}

static int rr_reveal_same_identity(const RR_REVEAL_PATCH* left,
                                   const RR_REVEAL_PATCH* right) {
    return left->address == right->address &&
           left->owner == right->owner &&
           left->owner_klass == right->owner_klass &&
           left->root == right->root &&
           left->root_klass == right->root_klass &&
           left->owner_offset == right->owner_offset;
}

/* Save the current value once and write replacement. Re-applies only while
 * the identity still matches. A changed but valid duration is re-baselined so
 * a reloaded description is shortened again instead of being left alone. */
static int rr_reveal_apply(RR_REVEAL_BOOK* book, const RR_REVEAL_PATCH* identity,
                           float value, const RR_REVEAL_IO* io) {
    uint32_t current = 0, replacement = rr_reveal_bits(value);
    int slot = -1;
    if (!rr_reveal_duration(replacement) || !io->valid(identity) ||
        !io->read(identity->address, &current, 4) || !rr_reveal_duration(current))
        return 0;
    for (int i = 0; i < RR_REVEAL_MAX_PATCHES; ++i) {
        RR_REVEAL_PATCH* p = &book->fields[i];
        if (p->active && p->address == identity->address) {
            if (!rr_reveal_same_identity(p, identity)) return 0;
            if (current == p->replacement) return 1;
            if (current != p->original) p->original = current; /* reloaded value */
            slot = i;
            break;
        }
        if (!p->active && slot < 0) slot = i;
    }
    if (slot < 0) return 0;
    RR_REVEAL_PATCH* p = &book->fields[slot];
    if (!p->active) {
        *p = *identity;
        p->original = current;
        p->replacement = replacement;
    }
    if (!io->write(p->address, &p->replacement, 4)) {
        /* The write may have landed even though the transport reported an
         * error. Only keep a restore entry when the readback agrees. */
        p->active = 0;
        return 0;
    }
    if (!io->read(p->address, &current, 4) || current != p->replacement) {
        p->active = 0;
        return 0;
    }
    p->active = 1;
    return 1;
}

static void rr_reveal_prune(RR_REVEAL_BOOK* book, const RR_REVEAL_IO* io) {
    for (int i = 0; i < RR_REVEAL_MAX_PATCHES; ++i) {
        if (book->fields[i].active && !io->valid(&book->fields[i])) {
            book->fields[i].active = 0;
            ++book->skipped;
        }
    }
}

/* Restore every entry whose value is still the one this book wrote. An
 * unrelated value is left untouched and counted as skipped. */
static int rr_reveal_restore(RR_REVEAL_BOOK* book, const RR_REVEAL_IO* io) {
    for (int i = 0; i < RR_REVEAL_MAX_PATCHES; ++i) {
        RR_REVEAL_PATCH* p = &book->fields[i];
        uint32_t current = 0;
        if (!p->active) continue;
        if (!io->valid(p)) {
            p->active = 0;
            ++book->skipped;
            continue;
        }
        if (!io->read(p->address, &current, 4)) continue;
        if (current == p->original) {
            p->active = 0;
            continue;
        }
        if (current != p->replacement) {
            p->active = 0;
            ++book->skipped;
            continue;
        }
        if (io->write(p->address, &p->original, 4) &&
            io->read(p->address, &current, 4) && current == p->original)
            p->active = 0;
    }
    return rr_reveal_pending(book);
}

#endif
