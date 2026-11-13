/* LANES' rows of blocks (spec LANES section 6; docs/screens/lanes.html, direction C3 "Focus", F2): the five lanes small
 * on top, their polymetry at a glance; the lane PRESET edits big below, as the white keys 1-8 and 9-16, its values
 * written; a dark wire floor under it slides a notch a step. The overview is drawn straight into the band (no
 * primitives: the big blocks and the floor keep the budget). Part of the unity build after scene_life.c (app_ui.c):
 * KBLUE, PINK3, MB_WHITE, glo_text. */

#define LN_X0 34.0f                 /* the overview: cell c from LN_X0 + LN_CW c, row r's base at LN_Y0 + rh r */
#define LN_CW 12.5f
#define LN_Y0 14
#define LN_BX 6.0f                  /* the edited lane: cell c from LN_BX + LN_BW (c mod 8), its rows' bases */
#define LN_BW 29.0f
#define LN_FLASH_MS 300u
static const float LN_BASE[2] = {120.0f, 162.0f};
static const uint16_t LN_OFF[3] = {RGB(0x3a, 0x2f, 0x63), RGB(0x2a, 0x21, 0x48), RGB(0x16, 0x0f, 0x30)};
#define LN_WALL RGB(0x9a, 0x16, 0x59)
#define LN_FLOOR RGB(0x0c, 0x08, 0x1e)        /* barely off the black: a backdrop, not a grid to read */
#define LN_GROUND RGB(0x24, 0x18, 0x50)
#define LN_UI RGB(0x8b, 0x84, 0xa8)
#define LN_DIM RGB(0x2a, 0x21, 0x48)
static const uint8_t LN_MAX[L_N] = {1, 15, 4, 4, 3};
static const char *const LN_NAME[L_N] = {"TRIG", "NOTE", "OCT", "DUR", "VEL"};

static struct {
    int seen;
    uint32_t n, step_t, ms;
} ln;

void lanes_enter(void) { ln.seen = 0; }

/* a value's height 0..1: OCT from the centre */
static float ln_h(int l, int v) { return l == L_OCT ? (float)(v > 2 ? v - 2 : 2 - v) * 0.5f : (float)v / (float)LN_MAX[l]; }

/* the lanes shown, top first: a KIT has no OCT */
static int ln_rows(const q_laneview *v, int *rows)
{
    int n = 0;
    for (int l = 0; l < L_N; l++)
        if (!(v->kit && l == L_OCT)) rows[n++] = l;
    return n;
}

static const uint16_t *ln_tones(const q_laneview *v, int l, int c, int big)
{
    if (v->head[l] == c) return PINK3;
    if (l == v->sel && v->flash == c && ln.ms - v->flash_ms < LN_FLASH_MS) return MB_WHITE;
    return l == v->sel || big ? KBLUE : LN_OFF;
}

static void ln_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, float z, uint16_t c)
{
    p3d_tri(x0, y0, x1, y1, x2, y2, z, c);
    p3d_tri(x0, y0, x2, y2, x3, y3, z, c);
}

void lanes_frame(const q_laneview *v, uint32_t ms)
{
    if (!ln.seen || v->n != ln.n)       /* a step went by: the floor slides from here; arriving, it stands */
        ln.n = v->n, ln.step_t = ln.seen ? ms : ms - 100000u;
    ln.seen = 1;
    ln.ms = ms;
    p3d_begin();
    /* the edited lane, big: front, top and side facets; a TRIG off a slab */
    int l = v->sel, len = lanes_len(&v->lanes, l);
    for (int c = 0; c < 16; c++) {
        float x = LN_BX + LN_BW * (float)(c % 8), y = LN_BASE[c / 8], w = 24.0f, d = 4.0f;
        if (c >= len) {
            ln_quad(x, y - 2.0f, x + w, y - 2.0f, x + w, y, x, y, 0.0f, LN_OFF[2]);
            continue;
        }
        const uint16_t *t = ln_tones(v, l, c, 1);
        int val = lanes_get(&v->lanes, l, c);
        if (l == L_TRIG && !val) {
            ln_quad(x, y - 3.0f, x + w, y - 3.0f, x + w, y, x, y, 0.0f, t[2]);
            continue;
        }
        float h = l == L_TRIG ? 22.0f : 6.0f + ln_h(l, val) * 20.0f;
        ln_quad(x, y - h, x + w, y - h, x + w, y, x, y, 0.0f, t[1]);
        ln_quad(x, y - h, x + w, y - h, x + w + d, y - h - d, x + d, y - h - d, 0.0f, t[0]);
        ln_quad(x + w, y - h, x + w + d, y - h - d, x + w + d, y - d, x + w, y, 0.0f, t[2]);
    }
    if (len < 16) {                     /* the loop's wall after the cell LEN */
        float x = LN_BX + LN_BW * (float)(len % 8) - 3.0f, y = LN_BASE[len / 8];
        if (len % 8 == 0) x = LN_BX + LN_BW * 8.0f - 3.0f, y = LN_BASE[len / 8 - 1];
        ln_quad(x, y, x + 2.0f, y, x + 6.0f, y - 30.0f, x + 4.0f, y - 30.0f, 0.0f, LN_WALL);
    }
    /* the floor, sent last (dropped first) and far (drawn first: the lowest depth): a notch a step */
    float f = v->step_ms ? (float)(ms - ln.step_t) / (float)v->step_ms : 1.0f;
    if (f > 1.0f) f = 1.0f;
    for (int i = -5; i <= 5; i++)
        p3d_line(120.0f + 20.0f * (float)i, 78.0f, 120.0f + 88.0f * (float)i, 166.0f, -10.0f, 1, LN_FLOOR);
    for (int j = 0; j < 4; j++) {
        float u = ((float)j + f) / 4.0f, y = 80.0f + u * u * 86.0f;
        p3d_line(0.0f, y, 240.0f, y, -10.0f, 1, LN_FLOOR);
    }
}

void lanes_band(const q_laneview *v, int32_t oy)
{
    static const char *const OCTS[5] = {"-2", "-1", "0", "+1", "+2"}, *const DURS[5] = {"1/4", "1/2", "1", "2", "4"};
    static const char *const VELS[4] = {"pp", "mf", "f", "ff"};
    int rows[L_N], nr = ln_rows(v, rows), rh = nr == 5 ? 13 : 16;
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    p3d_draw(oy);
    /* the overview: a front and a top strip per block, a wall after LEN */
    for (int r = 0; r < nr; r++) {
        int l = rows[r], len = lanes_len(&v->lanes, l);
        int32_t y0 = oy + LN_Y0 + rh * r;
        glo_text(4, y0 - 4, &FONT_XS, LN_NAME[l], l == v->sel ? PINK3[1] : LN_UI, 2);
        for (int c = 0; c < 16; c++) {
            int32_t x = (int32_t)(LN_X0 + LN_CW * (float)c);
            if (c >= len) {
                cv_rect(x + 1, y0, 8, 1, LN_GROUND);
                continue;
            }
            const uint16_t *t = ln_tones(v, l, c, 0);
            int val = lanes_get(&v->lanes, l, c), cur = v->head[l] == c;
            if (l == L_TRIG && !val) {
                cv_rect(x, y0 - 2, 10, 2, t[cur ? 1 : 2]);
                continue;
            }
            int32_t h = l == L_TRIG ? 7 : 2 + (int32_t)(ln_h(l, val) * 7.0f + 0.5f);
            cv_rect(x, y0 - h, 10, h, t[cur || l == v->sel ? 1 : 2]);
            cv_rect(x + 1, y0 - h - 2, 10, 2, t[0]);
        }
        cv_rect((int32_t)(LN_X0 + LN_CW * (float)len) - 2, y0 - 11, 2, 11, LN_WALL);
    }
    /* the edited lane: its title, its values above the blocks, the keys' numbers at their feet */
    int l = v->sel, len = lanes_len(&v->lanes, l);
    cv_rect(0, oy + 71, 240, 1, LN_DIM);
    char s[7] = {'L', 'E', 'N', ' ', (char)(len >= 10 ? '1' : '0' + len), (char)(len >= 10 ? '0' + len - 10 : 0), 0};
    glo_text(4, oy + 82, &FONT_S, "EDIT", PINK3[1], 2);
    glo_text(4 + text_w(&FONT_S, "EDIT "), oy + 82, &FONT_S, LN_NAME[l], PINK3[1], 2);
    glo_text(236, oy + 82, &FONT_S, s, LN_UI, 1);
    for (int c = 0; c < 16; c++) {
        int32_t x = (int32_t)(LN_BX + LN_BW * (float)(c % 8)) + 12, y = oy + (int32_t)LN_BASE[c / 8];
        char num[3] = {(char)(c >= 9 ? '1' : '0' + c + 1), (char)(c >= 9 ? '0' + c - 9 : 0), 0};
        if (c >= len) {
            glo_text(x, y - 6, &FONT_XS, num, LN_DIM, 0);
            continue;
        }
        int val = lanes_get(&v->lanes, l, c), cur = v->head[l] == c;
        float h = l == L_TRIG ? (val ? 22.0f : 3.0f) : 6.0f + ln_h(l, val) * 20.0f;
        glo_text(x, y - 4, &FONT_XS, num, cur ? MB_WHITE[0] : KBLUE[0], 0);
        if (l == L_TRIG) continue;
        const char *t = l == L_NOTE ? v->name[val] : l == L_OCT ? OCTS[val] : l == L_DUR ? DURS[val] : VELS[val];
        glo_text(x, y - (int32_t)h - 10, &FONT_XS, t, cur ? MB_WHITE[0] : LN_UI, 0);
    }
}
