/* QUARTZ: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define Q_CHUNKS 11                      /* store objects holding a project (store.c) */
#define Q_PROJECTS 4                     /* the projects kept: project n's chunks are objects OBJ_CHUNK0 + n * Q_CHUNKS.. */

#define APP_NAME "QUARTZ"
#define APP_SPLASH_RGB RGB(80, 160, 255)
#define APP_REDRAW                       /* app_redraw: the screen mirror lost a part */
#ifndef PLAT_HOST
#define APP_LATE "app_ui.c"             /* the screen's code after the platform's (patches/fm1-base-app-late.patch): as it
                                           grows, the audio's code keeps its place in the flash and in the caches */
#endif
void app_redraw(void);
#define APP_SPLASH                       /* the boot screen: scene_splash.c */
void app_splash(void);
extern void (*boot_step)(int n);        /* the boot screen at step n (0..2), from app_init. Set by app_splash: app.c
                                            names no screen function, which would place the screen's code (and move the
                                            platform's audio code) before the platform's (docs/cpu.md) */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_CANVAS_PX (240u * 83u)       /* the widest strip drawn: a scene band (ui.c BAND_H); the pool keeps the rest */

/* the store, each object at its flash offset (A/B sector pair). The numbering is fm1-base's template's: the panel
 * calibration record must stay readable across apps (storage rejects a record whose type is not its object number).
 * OBJ_SETTINGS is reserved and unused by QUARTZ. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PROJECT, OBJ_CHUNK0, OBJ_NOBJ = OBJ_CHUNK0 + Q_PROJECTS * Q_CHUNKS };
/* OBJ_PROJECT is the v1 project's object, reserved and no longer read. The projects' chunks are pooled (device/storage.c):
 * 53 sectors from 0x99000 (up to 0xCE000, below OBJ_PANEL's pair at 0xDC000) for 44 objects, so a save always finds a
 * sector that holds no current copy. Project 1's objects are the single project's of before: it is kept. The map's other
 * entries are the pool's, unused. */
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0x97000u}
#define APP_STORE_POOL
#define APP_STORE_POOL_BASE 0x99000u
#define APP_STORE_POOL_N 53
#define APP_STORE_POOL_OBJ0 OBJ_CHUNK0
#define APP_STORE_POOL_OBJN (Q_PROJECTS * Q_CHUNKS)
#define APP_STORE_POOL_OLD 0

/* backup and restore over USB (fm1.toml backup = true; fm1-base device/backup.c): the projects' chunks, as last saved.
 * The version is Q_VERSION's (store.c checks): a backup of another layout is refused, not loaded as a new project */
#define Q_PROJECT_OBJS(o) o, o + 1, o + 2, o + 3, o + 4, o + 5, o + 6, o + 7, o + 8, o + 9, o + 10   /* a project's chunks */
#define APP_BACKUP_OBJS {Q_PROJECT_OBJS(OBJ_CHUNK0), Q_PROJECT_OBJS(OBJ_CHUNK0 + Q_CHUNKS), \
                         Q_PROJECT_OBJS(OBJ_CHUNK0 + 2 * Q_CHUNKS), Q_PROJECT_OBJS(OBJ_CHUNK0 + 3 * Q_CHUNKS)}
#define APP_BACKUP_MAGIC 0x5A545251u     /* "QRTZ" */
#define APP_BACKUP_VERSION 17
#define APP_BACKUP_WHAT "replaces the 4 projects"
#define APP_BACKUP_BUF chunk_buf         /* store.c's: the restore takes none of the pool */
int app_backup_playing(void);
void app_backup_flush(void);
void app_backup_hold(int on);
void app_backup_restored(void);

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
