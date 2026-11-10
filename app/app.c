/* QUARTZ: a black screen with the name; the render is audio.c. Part of the unity build (device/unity.c,
 * host/plat_host.c), after the platform: gfx.c is in scope. The app's own .c files are included here. */
#include "project.h"
#include "audio.h"
#include "seq.c"
#include "voices.c"
#include "audio.c"

static uint8_t booted;
static q_project project;

void app_init(void)
{
    project_defaults(&project);
    audio_setup(&project);
    booted = 1;
}

void app_panic(void) {}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 32, C_BLACK);
    cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
    cv_blit(0, 0);
}

void app_frame(void) {}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "booted"))
        *got = booted;
    else
        return 0;
    return 1;
}
#endif
