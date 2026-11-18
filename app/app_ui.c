/* QUARTZ's screen: the 3D renderer, the scenes, the SEQ plateau, the interface, and the platform's calls into them. Part of
 * the unity build: on the device after the platform's own code (app.h APP_LATE), so that as it grows the render's code,
 * the audio interrupt's and the DSP's keep their place in the flash, and how they fit the caches (docs/cpu.md); in the
 * simulator at the end of app.c. Nothing before calls into it: clang places a function where it is first named. */
#include "p3d.c"
#include "scene_splash.c"
#include "scene.c"
#include "scene_va.c"
#include "scene_wave.c"
#include "scene_garden.c"
#include "scene_chip.c"
#include "scene_choir.c"
#include "scene_rope.c"
#include "scene_dance.c"
#include "scene_home.c"
#include "scene_sound.c"
#include "scene_glo.c"
#include "scene_life.c"
#include "scene_lanes.c"
#include "scene_chords.c"
#include "scene_tonnetz.c"
#include "scene_orbits.c"
#include "scene_mimic.c"
#include "plateau.c"
#include "ui.c"

void app_ui_init(void)
{
    audio_unmute();                 /* the app resumed: a refused update (audio.h) */
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui_init(&project);
}

/* The CPU probe, for tools/device/probe.py: while a screen reader listens (its HELLO, every 2 s), once a second on
 * channel 16: CC 102 core 1 %, 103 core 2 %, 104-106 LOST BLOCKS (7 bits each, low first), 107 the mutes, 108 the
 * voices sounding, 109-120 each track's model, source (its pattern's) and poly, 121-123 the voices stolen, 124-126
 * those that cut another track's held note. Silent
 * otherwise: nothing goes to a DAW. In the screen's code: the audio's code keeps its place in the flash */
static void cpu_probe(void)
{
    static uint32_t last;
    uint32_t now = plat_ms(), v[25], n = 0;
    ann_tick(now);                  /* (the platform does not call it: a reader gone quiet would keep the probe on) */
    if (!ann_on() || now - last < 1000u)
        return;
    last = now;
    q_load l = audio_load();
    v[n++] = l.core1_pct > 127 ? 127 : l.core1_pct;
    v[n++] = l.core2_pct > 127 ? 127 : l.core2_pct;
    for (int k = 0; k < 3; k++) v[n++] = l.xruns >> 7 * k & 127u;
    v[n++] = project.mute & 127u;
    v[n++] = l.active;
    for (int t = 0; t < Q_TRACKS; t++) {
        v[n++] = project.sound[t].model;
        v[n++] = project.pat[t][project.pattern[t]].src;
        v[n++] = project.sound[t].poly;
    }
    for (int k = 0; k < 3; k++) v[n++] = l.steals >> 7 * k & 127u;
    for (int k = 0; k < 3; k++) v[n++] = l.cuts >> 7 * k & 127u;
    for (uint32_t i = 0; i < n; i++)
        plat_midi_send(0x0Bu | 0xBFu << 8 | (102u + i) << 16 | v[i] << 24, TO_USB);
}

void app_frame(void)
{
    audio_prepare();                /* after boot and after a model or poly change: the engines, outside the render */
    ui_frame();
    cpu_probe();
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
