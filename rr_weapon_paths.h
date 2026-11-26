/* Weapon paths: local 2026-11-14 dump, corroborated by the supplied post
 * and public offsets dated 2026-11-20. Runtime class checks are mandatory.
 * IPrimitiveModel<float> is PrimitiveModel`1 -> FloatData, NOT a concrete
 * class named WeaponSpreadModel. No generic offset comes from a printed 0. */
#ifndef RR_WEAPON_PATHS_H
#define RR_WEAPON_PATHS_H
#include <stdint.h>
typedef struct {
    uint64_t recoil, fake, spread_data, spread_args;
} RR_WEAPON_PATHS;
typedef struct {
    uint64_t (*pointer)(uint64_t);
    int (*is_type)(uint64_t, const char*);
} RR_WEAPON_PATH_IO;
static uint64_t rr_weapon_child(uint64_t parent, unsigned offset,
                               const char* type, const RR_WEAPON_PATH_IO* io) {
    uint64_t p = parent ? io->pointer(parent + offset) : 0;
    return p && io->is_type(p, type) ? p : 0;
}
static RR_WEAPON_PATHS rr_weapon_resolve(uint64_t battle, uint64_t local,
                                        int recoil, int spread,
                                        const RR_WEAPON_PATH_IO* io) {
    RR_WEAPON_PATHS p = {0};
    if ((recoil || spread) && battle && io->is_type(battle, "BattleEntitiesModel")) {
        uint64_t player = rr_weapon_child(battle, 0xD0, "PlayerEntityModel", io);
        uint64_t client = rr_weapon_child(player, 0x18, "PlayerClientModel", io);
        if (recoil) p.recoil = rr_weapon_child(client, 0x2B8, "RecoilModel", io);
        if (spread) p.fake = rr_weapon_child(client, 0x2C0, "PlayerFakeSpreadModel", io);
    }
    if (spread && local && io->is_type(local, "BattleCharacterModel")) {
        uint64_t priv = rr_weapon_child(local, 0x10, "BattleCharacterPrivateModel", io);
        p.spread_args = rr_weapon_child(priv, 0x220, "WeaponSpreadArgsModel", io);
        uint64_t model = rr_weapon_child(priv, 0x228, "PrimitiveModel`1", io);
        p.spread_data = rr_weapon_child(model, 0x10, "FloatData", io);
    }
    return p;
}
#endif
