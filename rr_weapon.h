/* External recoil mouse-input test and spread data control; no hook. */
#ifndef RR_WEAPON_H
#define RR_WEAPON_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int rr_weapon_start(void);
void rr_weapon_stop(void);
void rr_weapon_publish(uint64_t battle, uint64_t local, int recoil, int spread,
                       int pause_reason);
/* Recoil: 0 off, 5-8 paused, 9 waiting for chord, 10 mouse input issued
 * (effect unverified), 11 SendInput failed.
 * Spread: 0 off, 1 data writes verified, 2 model missing, 3 read/write refused,
 * 4 visual-only spread path (shot spread model unavailable),
 * 5 menu open, 6 game not focused, 7 invalid scene, 8 stale feed.
 * Successful writes are not proof of server-side effect. */
int rr_weapon_recoil_status(void);
int rr_weapon_spread_status(void);
#ifdef __cplusplus
}
#endif
#endif
