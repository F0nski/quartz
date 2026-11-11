/* The SEQ plateau (spec 5b): the page's 16 steps as low-poly blocks on an isometric wire floor. ui.c prepares the
 * view, texts included; the plateau reads neither the project nor ui.c's tables. */
#ifndef QUARTZ_PLATEAU_H
#define QUARTZ_PLATEAU_H
#include <stdint.h>

typedef struct { uint8_t on, vel, extra, len; char name[4]; } q_pcell;   /* len: q_step.len (4 = one step) */
typedef struct {
    q_pcell c[16];
    uint16_t live;          /* bit i: cell i is inside LEN and X LEN x Y LEN */
    int8_t pos;             /* the cell playing, -1: none on this page or stopped */
    int8_t cursor;          /* REC stopped: the write cursor's cell, else -1 */
    int8_t end;             /* the loop's last cell when the loop ends in the page, else -1 */
    int8_t held, attr;      /* held cell or -1; held: the PRESET row 0..8 */
    int8_t npath;           /* cells in path (0: no path shown) */
    uint8_t path[16];
    char top_l[8], top_r[6];         /* "STEP 05" or "17-32"; the playing note or "--" */
    char rows[9][2][10];             /* held: label and value of each attribute, in ATTR_ROW order */
    char held_name[6];
} q_plateau;

void plateau_enter(void);                                  /* shown anew: no hit for the notes before */
void plateau_frame(const q_plateau *p, uint32_t hits, uint32_t ms);
void plateau_band(int32_t oy);

#endif
