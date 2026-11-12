/* QUARTZ's screen: the 3D renderer, the scenes, the SEQ plateau, the interface, and the platform's calls into them. Part of
 * the unity build: on the device after the platform's own code (app.h APP_LATE), so that as it grows the render's code,
 * the audio interrupt's and the DSP's keep their place in the flash, and how they fit the caches (docs/cpu.md); in the
 * simulator at the end of app.c. Nothing before calls into it: clang places a function where it is first named. */
#include "p3d.c"
#include "scene.c"
#include "plateau.c"
#include "ui.c"

void app_ui_init(void)
{
    audio_unmute();                 /* the app resumed: a refused update (audio.h) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init(&project);
}

void app_frame(void)
{
    audio_prepare();                /* after boot and after a model or poly change: the engines, outside the render */
    ui_frame();
}
void app_input_only(void) { ui_input(); }
void app_redraw(void) { ui_redraw(); }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "booted"))
        *got = booted;
    else
        return ui_expect(what, got);
    return 1;
}
#endif
