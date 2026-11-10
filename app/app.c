/* QUARTZ: the interface is ui.c, the render audio.c. Part of the unity build (device/unity.c,
 * host/plat_host.c), after the platform: gfx.c is in scope. The app's own .c files are included here. */
#include "project.h"
#include "audio.h"
#include "seq.c"
#include "voices.c"
#include "audio.c"
#include "store.c"
#include "ui.c"

static uint8_t booted;
static q_project project;

void app_init(void)
{
    store_load(&project);
    audio_setup(&project);
    booted = 1;
}

void app_panic(void) { audio_mute(); }

void app_ui_init(void)
{
    audio_unmute();                 /* the app resumed: a refused update (audio.h) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init(&project);
}

void app_frame(void) { ui_frame(); }
void app_input_only(void) { ui_input(); }
void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
/* "update_refused": what device/main_fm1.c does for an update that commits nothing */
int app_host_cmd(const char *cmd, const char *a, const char *b, const char *c, const char *d)
{
    (void)a, (void)b, (void)c, (void)d;
    if (strcmp(cmd, "update_refused"))
        return 0;
    app_panic();
    app_ui_init();
    return 1;
}

int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "booted"))
        *got = booted;
    else
        return ui_expect(what, got);
    return 1;
}
#endif
