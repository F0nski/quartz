/* QUARTZ: the interface is ui.c, the render audio.c. Part of the unity build (device/unity.c,
 * host/plat_host.c), after the platform: gfx.c is in scope. The app's own .c files are included here. */
#include "project.h"
#include "audio.h"
#include "gen.c"
#include "seq.c"
#include "voices.c"
#include "mod.c"
#include "audio.c"
#include "store.c"
static uint8_t booted;
void (*boot_step)(int n);
static q_project project PLAT_POOL;   /* too big for the core's RAM: the 336 KB pool */

void app_init(void)
{
    store_project = store_last();
    store_load(&project);
    if (boot_step)
        boot_step(1);
    audio_setup(&project);
    if (boot_step)
        boot_step(2);
    booted = 1;
}

void app_panic(void) { audio_mute(); }

void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
/* "update_refused": what device/main_fm1.c does for an update that commits nothing */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)b, (void)c, (void)d;
    if (!strcmp(cmd, "splash")) {      /* "splash N": the boot screen at step N, which the simulator never shows */
        app_splash();
        boot_step(atoi(a));
        return 1;
    }
    if (strcmp(cmd, "update_refused"))
        return 0;
    app_panic();
    app_ui_init();
    return 1;
}
#endif

#ifndef APP_LATE                    /* the simulator: here; the device: after the platform's code (app.h) */
#include "app_ui.c"
#endif
