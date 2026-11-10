#ifndef QUARTZ_UI_H
#define QUARTZ_UI_H
#include "project.h"

/* The controls, the lights and the screen (ui.c). Main loop only: it edits the project the render reads. */
void ui_init(q_project *p);     /* app_ui_init: at boot and when fm1-base resumes the app; the screen drawn whole */
void ui_input(void);            /* every pass: buttons, keys, encoders */
void ui_frame(void);            /* every frame: the input, the lights, what changed on the screen */
int ui_expect(const char *what, int *got);   /* the host simulator's checks (app_expect) */

#endif
