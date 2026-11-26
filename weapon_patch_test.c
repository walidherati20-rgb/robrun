/* Mock memory only: signed values, restoration, object reuse and failures. */
#include <stdio.h>
#include "rr_weapon_patch.h"
#include "rr_weapon_paths.h"
static int wrong_float_type, wrong_private_type;
static uint64_t graph_pointer(uint64_t a) {
    if (a == 0x100000 + 0xD0) return 0x110000;
    if (a == 0x110000 + 0x18) return 0x120000;
    if (a == 0x120000 + 0x2B8) return 0x130000;
    if (a == 0x120000 + 0x2C0) return 0x140000;
    if (a == 0x200000 + 0x10) return 0x210000;
    if (a == 0x210000 + 0x228) return 0x220000;
    if (a == 0x220000 + 0x10) return 0x230000;
    if (a == 0x210000 + 0x220) return 0x240000;
    return 0;
}
static int graph_type(uint64_t p, const char* name) {
    const char* type = "";
    switch (p) {
        case 0x100000: type = "BattleEntitiesModel"; break;
        case 0x110000: type = "PlayerEntityModel"; break;
        case 0x120000: type = "PlayerClientModel"; break;
        case 0x130000: type = "RecoilModel"; break;
        case 0x140000: type = "PlayerFakeSpreadModel"; break;
        case 0x200000: type = "BattleCharacterModel"; break;
        case 0x210000: type = wrong_private_type ? "OtherModel" : "BattleCharacterPrivateModel"; break;
        case 0x220000: type = "PrimitiveModel`1"; break;
        case 0x230000: type = wrong_float_type ? "IntData" : "FloatData"; break;
        case 0x240000: type = "WeaponSpreadArgsModel"; break;
    }
    return !strcmp(type, name);
}
static uint64_t klass = 0xABC000;
static float value;
static unsigned char bool_value;
static int refuse, failures;
static int read_mem(uint64_t address, void* out, size_t n) {
    if (address == 0x10000 && n == 8) { memcpy(out, &klass, n); return 1; }
    if (address == 0x10010 && n == 4) { memcpy(out, &value, n); return 1; }
    if (address == 0x10011 && n == 1) { memcpy(out, &bool_value, n); return 1; }
    return 0;
}
static int write_mem(uint64_t address, const void* in, size_t n) {
    if (!refuse && address == 0x10010 && n == 4) { memcpy(&value, in, n); return 1; }
    if (!refuse && address == 0x10011 && n == 1) { memcpy(&bool_value, in, n); return 1; }
    return 0;
}
#define CHECK(c, label) do { if (!(c)) { ++failures; printf("FAIL: %s\n", label); } } while (0)
int main(void) {
    RR_WEAPON_PATH_IO paths_io = { graph_pointer, graph_type };
    RR_WEAPON_PATHS paths = rr_weapon_resolve(0x100000, 0x200000, 1, 1, &paths_io);
    CHECK(paths.recoil == 0x130000 && paths.fake == 0x140000 &&
          paths.spread_data == 0x230000 && paths.spread_args == 0x240000,
          "verified generic float and args paths resolve");
    wrong_float_type = 1;
    paths = rr_weapon_resolve(0x100000, 0x200000, 1, 1, &paths_io);
    CHECK(!paths.spread_data && paths.spread_args && paths.recoil,
          "IntData cannot be written as float spread data");
    wrong_private_type = 1;
    paths = rr_weapon_resolve(0x100000, 0x200000, 0, 1, &paths_io);
    CHECK(!paths.spread_data && !paths.spread_args && paths.fake && !paths.recoil,
          "wrong private type blocks actual spread without enabling recoil");
    paths = rr_weapon_resolve(0x100000, 0x200000, 0, 0, &paths_io);
    CHECK(!paths.recoil && !paths.fake && !paths.spread_data && !paths.spread_args,
          "disabled controls do not resolve write targets");
    RR_WEAPON_FIELD p = {0};
    RR_WEAPON_IO io = {read_mem, write_mem};
    value = -3.5f;
    CHECK(rr_weapon_zero_field(&p, 0x10000, 0x10, &io) && value == 0,
          "signed recoil is zeroed");
    CHECK(rr_weapon_zero_field(&p, 0x10000, 0x10, &io) && p.original == -3.5f,
          "repeated zero keeps original");
    CHECK(rr_weapon_restore_field(&p, &io) && value == -3.5f && !p.active,
          "disabled restores original");
    CHECK(rr_weapon_zero_field(&p, 0x10000, 0x10, &io), "second activation");
    value = 8.0f;
    CHECK(rr_weapon_restore_field(&p, &io) && value == 8.0f,
          "engine-owned newer value is preserved");
    CHECK(rr_weapon_zero_field(&p, 0x10000, 0x10, &io), "third activation");
    klass++;
    CHECK(rr_weapon_restore_field(&p, &io) && !p.active && value == 0,
          "recycled object never receives stale restore");
    value = NAN;
    CHECK(!rr_weapon_zero_field(&p, 0x10000, 0x10, &io), "nonfinite data refused");
    value = 2.0f; refuse = 1;
    CHECK(!rr_weapon_zero_field(&p, 0x10000, 0x10, &io) && p.active,
          "failed write retains restoration record");
    refuse = 0;
    CHECK(rr_weapon_restore_field(&p, &io) && value == 2.0f,
           "failed write leaves original intact");
    RR_WEAPON_BOOL b = {0};
    bool_value = 1;
    CHECK(rr_weapon_zero_bool(&b, 0x10000, 0x11, &io) && !bool_value,
          "boolean spread gate is zeroed");
    CHECK(rr_weapon_zero_bool(&b, 0x10000, 0x11, &io) && b.original == 1,
          "repeated boolean zero preserves original");
    CHECK(rr_weapon_zero_bool(&b, 0, 0x11, &io) && bool_value == 1 && !b.active,
          "disabling restores original boolean");
    bool_value = 2;
    CHECK(!rr_weapon_zero_bool(&b, 0x10000, 0x11, &io),
          "invalid boolean is refused");
    printf("WEAPON PATCH TEST %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures != 0;
}
