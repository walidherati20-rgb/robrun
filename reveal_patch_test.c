/* reveal_patch_test.c - Offline tests for the reveal save/write/restore book.
 * No driver, game process or executable page is touched.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "rr_reveal_patch.h"

static unsigned char g_memory[0x4000];
static int g_writes;
static int g_write_fail;
static int g_write_ignored;
static int g_valid = 1;

static int mock_read(uint64_t address, void* out, size_t size) {
    if (address + size > sizeof(g_memory)) return 0;
    memcpy(out, g_memory + address, size);
    return 1;
}
static int mock_write(uint64_t address, const void* in, size_t size) {
    if (address + size > sizeof(g_memory)) return 0;
    ++g_writes;
    if (g_write_fail) return 0;
    if (!g_write_ignored) memcpy(g_memory + address, in, size);
    return 1;
}
static int mock_valid(const RR_REVEAL_PATCH* patch) {
    (void)patch;
    return g_valid;
}
static const RR_REVEAL_IO g_io = { mock_read, mock_write, mock_valid };

static int g_failures;
#define CHECK(condition, message) do { \
    if (!(condition)) { printf("FAIL: %s\n", message); ++g_failures; } \
} while (0)

static float load_float(uint64_t address) {
    float value;
    memcpy(&value, g_memory + address, 4);
    return value;
}
static void store_float(uint64_t address, float value) {
    memcpy(g_memory + address, &value, 4);
}

static RR_REVEAL_PATCH identity(uint64_t address, float ignored) {
    RR_REVEAL_PATCH patch;
    (void)ignored;
    memset(&patch, 0, sizeof(patch));
    patch.address = address;
    patch.owner = 0x10;
    patch.owner_klass = 0x1000;
    patch.root = 0x10;
    patch.root_klass = 0x1000;
    patch.owner_offset = (int)(address - patch.owner);
    return patch;
}

static void reset(void) {
    memset(g_memory, 0, sizeof(g_memory));
    g_writes = 0;
    g_write_fail = 0;
    g_write_ignored = 0;
    g_valid = 1;
}

static void test_apply_and_restore(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x100, 0.25f);
    RR_REVEAL_PATCH patch = identity(0x100, 0);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "apply succeeds");
    CHECK(load_float(0x100) == 0.0f, "replacement written");
    CHECK(rr_reveal_pending(&book) == 1, "one active entry");

    int writes = g_writes;
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "second apply is idempotent");
    CHECK(g_writes == writes, "idempotent apply performs no write");

    CHECK(rr_reveal_restore(&book, &g_io) == 0, "restore clears the book");
    CHECK(load_float(0x100) == 0.25f, "original restored exactly");
}

static void test_reload_rebaseline(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x200, 0.4f);
    RR_REVEAL_PATCH patch = identity(0x200, 0);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "apply before reload");
    store_float(0x200, 0.75f); /* game reloaded the description */
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "apply after reload");
    CHECK(load_float(0x200) == 0.0f, "reloaded value shortened again");
    CHECK(rr_reveal_restore(&book, &g_io) == 0, "restore reloaded value");
    CHECK(load_float(0x200) == 0.75f, "newer original restored, not the old one");
}

static void test_restore_does_not_clobber(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x300, 0.4f);
    RR_REVEAL_PATCH patch = identity(0x300, 0);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "apply for clobber test");
    store_float(0x300, 0.9f); /* another writer owns the field now */
    int writes = g_writes;
    CHECK(rr_reveal_restore(&book, &g_io) == 0, "restore clears entry");
    CHECK(load_float(0x300) == 0.9f, "unrelated value preserved");
    CHECK(g_writes == writes, "no write to an unrelated value");
    CHECK(book.skipped == 1, "unrelated value counted as skipped");
}

static void test_invalid_identity(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x400, 0.4f);
    RR_REVEAL_PATCH patch = identity(0x400, 0);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "valid apply");
    g_valid = 0;
    rr_reveal_prune(&book, &g_io);
    CHECK(rr_reveal_pending(&book) == 0, "invalid identity pruned");
    CHECK(book.skipped == 1, "prune counted as skipped");
    CHECK(rr_reveal_restore(&book, &g_io) == 0, "restore sees no pending entry");
    CHECK(load_float(0x400) == 0.0f, "pruned entry is not written after invalidation");
}

static void test_failed_write(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x500, 0.4f);
    RR_REVEAL_PATCH patch = identity(0x500, 0);
    g_write_fail = 1;
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 0, "failed write rejected");
    CHECK(rr_reveal_pending(&book) == 0, "failed write leaves no restore entry");
    CHECK(load_float(0x500) == 0.4f, "failed write leaves original intact");

    reset();
    store_float(0x500, 0.4f);
    g_write_ignored = 1;
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 0, "ignored write rejected by readback");
    CHECK(rr_reveal_pending(&book) == 0, "ignored write leaves no restore entry");
}

static void test_duration_limits(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    store_float(0x600, 0.4f);
    RR_REVEAL_PATCH patch = identity(0x600, 0);
    CHECK(rr_reveal_apply(&book, &patch, -1.0f, &g_io) == 0, "negative replacement rejected");
    CHECK(rr_reveal_apply(&book, &patch, 121.0f, &g_io) == 0, "large replacement rejected");
    CHECK(rr_reveal_apply(&book, &patch, NAN, &g_io) == 0, "NaN replacement rejected");
    store_float(0x600, -5.0f);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 0, "invalid current value rejected");
    CHECK(rr_reveal_pending(&book) == 0, "rejected values leave no entry");
}

static void test_capacity(void) {
    RR_REVEAL_BOOK book;
    memset(&book, 0, sizeof(book));
    reset();
    for (int i = 0; i < RR_REVEAL_MAX_PATCHES; ++i) {
        uint64_t address = 0x800 + (uint64_t)i * 8;
        store_float(address, 0.5f);
        RR_REVEAL_PATCH patch = identity(address, 0);
        CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 1, "capacity fill apply");
    }
    uint64_t extra = 0x800 + (uint64_t)RR_REVEAL_MAX_PATCHES * 8;
    store_float(extra, 0.5f);
    RR_REVEAL_PATCH patch = identity(extra, 0);
    CHECK(rr_reveal_apply(&book, &patch, 0.0f, &g_io) == 0, "capacity limit enforced");
    CHECK(rr_reveal_pending(&book) == RR_REVEAL_MAX_PATCHES, "book stays bounded");
}

int main(void) {
    test_apply_and_restore();
    test_reload_rebaseline();
    test_restore_does_not_clobber();
    test_invalid_identity();
    test_failed_write();
    test_duration_limits();
    test_capacity();
    if (g_failures) {
        printf("reveal_patch_test: %d assertion(s) failed\n", g_failures);
        return 1;
    }
    printf("reveal_patch_test: all assertions passed\n");
    return 0;
}
