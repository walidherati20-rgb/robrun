/* menu_bridge.h - Shared integer settings, limits, colors and menu telemetry.
 * BR_SETTINGS is the schema used for declarations, defaults and validation.
 */
#ifndef MENU_BRIDGE_H
#define MENU_BRIDGE_H

#ifdef __cplusplus
extern "C" {
#endif

#define BR_MAGIC 0x57444D31u
#define BR_VERSION 20u
#define BR_COLOR_COUNT 15
#define BR_MAX_FRIENDS 320

/* Shared schema for the WebView, defaults and on-disk settings.
 * Each entry is a field, default, minimum and maximum. */
#define BR_SETTINGS(X) \
    X(enabled, 1, 0, 1) \
    X(streamer_mode, 0, 0, 1) \
    X(show_box, 1, 0, 1) \
    X(show_skeleton, 1, 0, 1) \
    X(show_name, 1, 0, 1) \
    X(show_armor, 1, 0, 1) \
    X(show_distance, 1, 0, 1) \
    X(show_weapon, 1, 0, 1) \
    X(show_head, 1, 0, 1) \
    X(show_self, 0, 0, 1) \
    X(show_clan, 1, 0, 1) \
    X(show_police, 1, 0, 1) \
    X(cham_on, 0, 0, 1) \
    X(max_distance, 1000000, 0, 1000000) \
    X(skel_distance, 1000000, 0, 1000000) \
    X(line_w, 1, 1, 4) \
    X(radar_on, 1, 0, 1) \
    X(radar_range, 150, 25, 500) \
    X(aa_on, 0, 0, 1) \
    X(aa_show_fov, 1, 0, 1) \
    X(aa_fov_px, 150, 30, 600) \
    X(aa_key, 0x12, 1, 254) \
    X(aa_smooth, 0, 0, 10) \
    X(aa_aim_cal, 100, 10, 2000) \
    X(aa_dist, 0, 0, 1000) \
    X(aa_target_clan, 0, 0, 1) \
    X(aa_target_police, 0, 0, 1) \
    X(aa_ignore_knocked, 1, 0, 1) \
    X(aa_ignore_civilian, 0, 0, 1) \
    X(aa_bone, 0, 0, 9) \
    X(aa_los_on, 0, 0, 1) \
    X(aa_closest_first, 0, 0, 1) \
    X(sa_on, 0, 0, 1) \
    X(sa_key, 0x06, 1, 254) \
    X(sa_fov_px, 300, 30, 900) \
    X(sa_mode, 0, 0, 2) \
    X(no_recoil, 0, 0, 1) \
    X(no_spread, 0, 0, 1) \
    X(show_loot, 0, 0, 1) \
    X(show_loot_nodes, 0, 0, 1) \
    X(loot_tier_none, 1, 0, 1) \
    X(loot_tier_common, 1, 0, 1) \
    X(loot_tier_uncommon, 1, 0, 1) \
    X(loot_tier_rare, 1, 0, 1) \
    X(loot_tier_epic, 1, 0, 1) \
    X(loot_tier_legendary, 1, 0, 1) \
    X(loot_tier_exotic, 1, 0, 1) \
    X(vehicle_chams, 0, 0, 1) \
    X(show_containers, 0, 0, 1) \
    X(show_doors, 0, 0, 1) \
    X(show_dropped_loot, 0, 0, 1) \
    X(show_vehicle_loot, 0, 0, 1) \
    X(radar_world, 0, 0, 1) \
    X(show_crouch, 0, 0, 1) \
    X(show_health, 0, 0, 1) \
    X(show_vehicle_markers, 0, 0, 1) \
    X(show_vehicle_occupants, 0, 0, 1)

typedef struct BR_Config {
    unsigned int magic, version;
#define BR_DECLARE(field, initial, minimum, maximum) int field;
    BR_SETTINGS(BR_DECLARE)
#undef BR_DECLARE
    struct { unsigned char rgb[3]; } color[BR_COLOR_COUNT];

    int menu_open;
    int telem_targets;
    int telem_game_pid;
    int telem_cham_n;
    int telem_los_state; /* 0 off, 1 loading/stale, 2 ready */
    int telem_capture_menu; /* 0 off, 1 affinity verified, -1 failed */
    int telem_capture_overlay;
    int telem_overlay_fps; /* successful presents, not game or monitor FPS */
    unsigned int telem_fps_at;
    int telem_startup_ms, telem_startup_fast;
    int instant_inventory_reveal; /* session only; never auto-enable from a saved profile */
    int telem_reveal_state, telem_reveal_fields, telem_reveal_skipped;
    /* Session-only exclusion, bound to both entity ID and object identity. */
    struct { int id; unsigned long long address; } friends[BR_MAX_FRIENDS];

#ifdef RR_PORT_DIAGNOSTICS
    int show_vehicles;
#endif
} BR_Config;

static int br_friend_excluded(const BR_Config* config, int id, unsigned long long address) {
    if (!address) return 0;
    for (int i = 0; i < BR_MAX_FRIENDS; ++i)
        if (config->friends[i].address == address && config->friends[i].id == id) return 1;
    return 0;
}
static void br_friends_clear(BR_Config* config) {
    for (int i = 0; i < BR_MAX_FRIENDS; ++i) config->friends[i].address = 0;
}
static int br_friend_set(BR_Config* config, int id, unsigned long long address, int excluded) {
    int free_slot = -1;
    if (id < 0 || (excluded && !address)) return 0;
    for (int i = 0; i < BR_MAX_FRIENDS; ++i) {
        if (config->friends[i].address && config->friends[i].id == id) {
            config->friends[i].address = excluded ? address : 0; return 1;
        }
        if (!config->friends[i].address && free_slot < 0) free_slot = i;
    }
    if (!excluded) return 1;
    if (free_slot < 0) return 0;
    config->friends[free_slot].id = id;
    config->friends[free_slot].address = address;
    return 1;
}
typedef struct BR_PlayerEntry {
    int id, distance, crosshair;
    unsigned long long address;
} BR_PlayerEntry;
int br_player_list(BR_PlayerEntry* output, int capacity);
int br_exclude_friend(int id, int excluded);

/* Unrecognized values use the same no-rarity bucket as the loot renderer. */
static int br_loot_rarity_visible(const BR_Config* config, int rarity) {
    switch (rarity) {
        case 1: return config->loot_tier_common;
        case 2: return config->loot_tier_uncommon;
        case 3: return config->loot_tier_rare;
        case 4: return config->loot_tier_epic;
        case 5: return config->loot_tier_legendary;
        case 6: return config->loot_tier_exotic;
        default: return config->loot_tier_none;
    }
}

BR_Config* br_get(void);
int br_config_set(BR_Config* config, const char* key, int value);
int br_config_save(void);
int br_config_load(void);

int br_start_menu(void);
void br_stop_menu(void);
void br_set_menu_visible(int visible);
void br_request_toggle(void);

#ifdef __cplusplus
}
#endif
#endif
