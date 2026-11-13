/* The SEQ plateau (spec 5b): the page's 16 steps as low-poly blocks on an isometric wire floor. ui.c prepares the
 * view, texts included; the plateau reads neither the project nor ui.c's tables. */
#ifndef QUARTZ_PLATEAU_H
#define QUARTZ_PLATEAU_H
#include <stdint.h>

typedef struct { uint8_t on, vel, extra, len, sel; char name[4]; } q_pcell;   /* len: q_step.len (8 = one step); sel: selected */
typedef struct {
    q_pcell c[16];
    uint16_t live;          /* bit i: cell i is inside LEN and X LEN x Y LEN */
    int8_t pos;             /* the cell playing, -1: none on this page or stopped */
    int8_t cursor;          /* NOTES: the cursor's cell, playing too, else -1 */
    int8_t end;             /* the loop's last cell when the loop ends in the page, else -1 */
    int8_t held, attr;      /* held cell (a held step, a selection's reference, NOTES' cursor with SEL) or -1; the PRESET row 0..8 */
    int8_t npath;           /* cells in path (0: no path shown) */
    uint8_t path[16];
    char top_l[8], top_r[6];         /* "STEP 05" or "17-32"; the playing note or "--" */
    char rows[9][2][10];             /* held: label and value of each attribute, in ATTR_ROW order */
    char held_name[6];
} q_plateau;

void plateau_enter(void);                                  /* shown anew: no hit for the notes before */
void plateau_frame(const q_plateau *p, uint32_t hits, uint32_t ms);
void plateau_band(int32_t oy);

/* GRIDS (spec 5b, 3 bis): the loop's 32 steps as stacks of the three voices, BD at the base */
typedef struct { uint8_t level[3][32], dens[3]; int8_t pos, voice; char top_l[8], top_r[10]; uint8_t x, y, chaos; } q_gview;   /* voice 1..3, 0 none */
void grids_enter(void);                            /* shown anew: no hit pending */
void grids_drop(int voice, uint32_t land_ms);      /* a voice hits at land_ms (a note decided ahead) */
void grids_frame(const q_gview *v, uint32_t ms);
void grids_band(int32_t oy);

#endif
