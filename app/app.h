/* QUARTZ: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define Q_CHUNKS 9                       /* store objects holding the project (store.c) */

#define APP_NAME "QUARTZ"
#define APP_SPLASH_RGB RGB(80, 160, 255)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 83u)       /* the widest strip drawn: a scene band (ui.c BAND_H); the pool keeps the rest */

/* the store, each object at its flash offset (A/B sector pair). The numbering is fm1-base's template's: the panel
 * calibration record must stay readable across apps (storage rejects a record whose type is not its object number).
 * OBJ_SETTINGS is reserved and unused by QUARTZ. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PROJECT, OBJ_CHUNK0, OBJ_NOBJ = OBJ_CHUNK0 + Q_CHUNKS };
/* OBJ_PROJECT is the v1 project's object, reserved and no longer read. The project's chunks are pooled (device/storage.c):
 * 20 sectors from 0x99000 for 9 objects, so a save always finds a sector that holds no current copy. The map's other
 * entries are the pool's, unused. */
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0x97000u}
#define APP_STORE_POOL
#define APP_STORE_POOL_BASE 0x99000u
#define APP_STORE_POOL_N 20
#define APP_STORE_POOL_OBJ0 OBJ_CHUNK0
#define APP_STORE_POOL_OBJN Q_CHUNKS
#define APP_STORE_POOL_OLD 0

void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);
void app_ui_init(void);
void app_frame(void);
void app_input_only(void);
void app_say(const char *a, const char *b);

/* the host simulator's checks (host/plat_host.c "expect"): booted */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
#define APP_HOST_CMD                     /* update_refused */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d);
