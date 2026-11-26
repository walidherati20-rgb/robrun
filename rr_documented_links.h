#ifndef RR_DOCUMENTED_LINKS_H
#define RR_DOCUMENTED_LINKS_H
#include <stdint.h>
/* Candidate layouts supplied by the user, not blindly trusted offsets.
 * Accept only class-checked, ownership-checked, stable read-only chains. */
typedef uint64_t (*RR_LINK_READ)(uint64_t);
typedef int (*RR_LINK_TYPE)(uint64_t, const char*);
typedef struct {
    uint64_t game, model, battle_model, battle, world, camera, views, aim;
    unsigned game_model_offset;
} RR_DOCUMENTED_LINKS;
static int rr_documented_scene(uint64_t game, RR_LINK_READ read, RR_LINK_TYPE type,
                               RR_DOCUMENTED_LINKS* output) {
    RR_DOCUMENTED_LINKS next = {0};
    if (!output || !read || !type || !type(game, "Game")) return 0;
    uint64_t current = read(game + 0xD0), legacy = read(game + 0xD8);
    int current_valid = type(current, "GameModel"), legacy_valid = type(legacy, "GameModel");
    if (current_valid && legacy_valid && current != legacy) return 0;
    next.game_model_offset = current_valid ? 0xD0 : 0xD8;
    next.game = game; next.model = current_valid ? current : legacy;
    if (!current_valid && !legacy_valid) return 0;
    next.battle_model = read(next.model + 0x10);
    if (!type(next.battle_model, "BattleModel")) return 0;
    next.battle = read(next.battle_model + 0x90);
    if (!type(next.battle, "BattleEntitiesModel")) return 0;
    next.world = read(next.battle + 0x60);
    if (!type(next.world, "WorldEntitiesModel")) return 0;
    uint64_t camera = read(next.battle_model + 0x358);
    if (type(camera, "BattleCameraModel")) next.camera = camera;

    /* Prior supplied chain: StartGameProcessor -> ScreenManager -> active
     * BattleScreenUpdater -> BattleUpdater. Optional and never required. */
    uint64_t processor = read(game + 0x98);
    if (type(processor, "StartGameProcessor")) {
        uint64_t screen = read(processor + 0x20);
        uint64_t active = type(screen, "ScreenManager") ? read(screen + 0x50) : 0;
        uint64_t updater = type(active, "BattleScreenUpdater") ? read(active + 0x18) : 0;
        if (type(updater, "BattleUpdater")) {
            uint64_t views = read(updater + 0x90);
            if (type(views, "BattleEntitiesUpdater") && read(views + 0x18) == next.battle &&
                read(views + 0x70) == next.battle_model) next.views = views;
            uint64_t aim = read(updater + 0x1B8);
            if (type(aim, "BattleAimTargetUpdater") && read(aim + 0x30) == next.battle &&
                type(read(aim + 0x48), "CinemachineBrain")) next.aim = aim;
        }
    }
    if (read(game + next.game_model_offset) != next.model ||
        read(next.model + 0x10) != next.battle_model ||
        read(next.battle_model + 0x90) != next.battle ||
        read(next.battle + 0x60) != next.world) return 0;
    *output = next;
    return 1;
}
#endif
