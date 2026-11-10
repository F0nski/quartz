/* QUARTZ: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "QUARTZ"
#define APP_SPLASH_RGB RGB(80, 160, 255)
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */

/* the store, each object at its flash offset (A/B sector pair). The numbering is fm1-base's template's: the panel
 * calibration record must stay readable across apps (storage rejects a record whose type is not its object number).
 * OBJ_SETTINGS is reserved and unused by QUARTZ. */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_PROJECT, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u, 0x97000u}

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
