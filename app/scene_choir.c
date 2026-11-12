/* The chord engines' scene (spec 5d2, 2; docs/screens/chords.html choir): a choir of androids on a stage, an android
 * per voice on a step as high as its note, the chord as a staircase; a conductor beats time. CHORDS, STRING MACH. Part
 * of the unity build after scene.c (app_ui.c): its sc, sb, stars, PINK3. */

#define CR_FOG RGB(0x0d, 0x08, 0x24)
#define CR_CZ (-0.55f)              /* the conductor's depth */
static const int8_t CR_CHORD[11][4] = {{0, 12, -1, -1}, {0, 7, 12, -1}, {0, 5, 7, 12}, {0, 3, 7, 12}, {0, 3, 7, 10}, {0, 3, 10, 14},
                                       {0, 3, 10, 17}, {0, 2, 9, 16}, {0, 4, 11, 14}, {0, 4, 7, 11}, {0, 4, 7, 12}};
static const char *const CR_NAME[11] = {"OCT", "5", "SUS4", "m", "m7", "m9", "m11", "69", "M9", "M7", "M"};
static const uint16_t CR_BOT[4][3] = {{RGB(0x8f, 0xb4, 0xff), RGB(0x3a, 0x6d, 0xff), RGB(0x1c, 0x35, 0x90)},
                                      {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)},
                                      {RGB(0xff, 0xff, 0xff), RGB(0xc9, 0xc3, 0xe4), RGB(0x6f, 0x68, 0x90)},
                                      {RGB(0xff, 0xc1, 0x96), RGB(0xff, 0x7a, 0x2e), RGB(0xa3, 0x41, 0x0c)}};
static const uint16_t CR_DIM[3] = {RGB(0x3b, 0x31, 0x70), RGB(0x2e, 0x26, 0x58), RGB(0x1f, 0x19, 0x46)};
static const uint16_t CR_WARM[3] = {RGB(0xff, 0xf2, 0xb0), RGB(0xff, 0xd2, 0x5a), RGB(0xb8, 0x86, 0x2a)};
static const uint16_t CR_EYE[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t CR_COAT[3] = {RGB(0x3b, 0x35, 0x66), RGB(0x2a, 0x24, 0x50), RGB(0x18, 0x14, 0x3a)};
static struct {
    float t, since, beat, beat_from, h[4], top[4][2];
    int n, chord, hot, nseg, ntop[4];
    uint32_t hits, steps;
} cr;

/* an open flat face, seen from either side */
static void cr_face(const p3_vec q[4], const uint16_t t[3])
{
    const p3_vec r[4] = {q[3], q[2], q[1], q[0]};
    p3d_quad3(q, t, 0, 0.0f);
    p3d_quad3(r, t, 0, 0.0f);
}

/* a small flat panel facing the camera (-z), centred at c, half sizes hx, hy */
static void cr_panel(p3_vec c, float hx, float hy, const uint16_t t[3])
{
    const p3_vec q[4] = {{c.x - hx, c.y - hy, c.z}, {c.x + hx, c.y - hy, c.z}, {c.x + hx, c.y + hy, c.z}, {c.x - hx, c.y + hy, c.z}};
    cr_face(q, t);
}

static void cr_seg(p3_vec a, p3_vec b, uint16_t c, float bias)   /* a backdrop line, drawn in the band */
{
    float xa, ya, za, xb, yb, zb;
    if (cr.nseg == 64 || !p3d_project(a, &xa, &ya, &za) || !p3d_project(b, &xb, &yb, &zb))
        return;
    int16_t *s = sb.choir.seg[cr.nseg];
    s[0] = (int16_t)xa, s[1] = (int16_t)ya, s[2] = (int16_t)xb, s[3] = (int16_t)yb;
    sb.choir.segc[cr.nseg++] = p3d_fogged(c, 0.5f * (za + zb) - bias);
}

/* k: HARM (the chord), TIMB (its inversion; STRING MACH: the choir's sway), MORPH (the heads' shape), DECAY (the pink) */
void choir_frame(const q_sview *v, uint32_t ms)
{
    static const p3_cam CAM = {0.0f, 0.2f, 3.5f, 1.5f, 215.0f, 100.0f};
    float t = (float)(ms % 3600000u) * 0.001f, k[4], z;
    int notes[4];
    for (int i = 0; i < 4; i++)
        k[i] = (float)v->k[i] / 127.0f;
    cr.t = t;
    cr.since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    float heat = cr.since < 3.0f ? fm_expf(-cr.since / (0.05f + 0.4f * k[3])) : 0.0f;
    cr.hot = heat > 0.4f;
    cr.chord = v->k[0] * 11 / 128;
    int inv = v->k[1] * 4 / 128;
    cr.n = CR_CHORD[cr.chord][2] < 0 ? 2 : CR_CHORD[cr.chord][3] < 0 ? 3 : 4;
    for (int i = 0; i < cr.n; i++)
        notes[i] = CR_CHORD[cr.chord][i];
    for (int j = 0; j < inv; j++)       /* the inversion: the lowest notes an octave up, then in order again */
        notes[j % cr.n] += 12;
    for (int i = 1; i < cr.n; i++)
        for (int j = i; j > 0 && notes[j - 1] > notes[j]; j--) {
            int x = notes[j];
            notes[j] = notes[j - 1], notes[j - 1] = x;
        }
    /* the conductor's beat: down in 0.12 s from where it was, up in 0.3 s */
    if (sc.fresh) {
        cr.hits = sc.hits[0], cr.steps = 0, cr.beat_from = 0.0f;
    } else if (sc.hits[0] != cr.hits) {
        cr.steps += sc.hits[0] - cr.hits;
        cr.hits = sc.hits[0];
        cr.beat_from = cr.beat;
    }
    cr.beat = cr.since < 0.12f ? cr.beat_from + (1.0f - cr.beat_from) * cr.since / 0.12f
                               : fm_maxf(0.0f, 1.0f - (cr.since - 0.12f) / 0.3f);
    sb_owner = SB_CHOIR;
    p3d_begin();
    p3d_camera(&CAM, (p3_vec){-0.5f, 0.75f, -0.45f}, CR_FOG, 12.0f);
    cr.nseg = 0;                        /* the backdrop: the organ, the stage's grid, two spots */
    for (int p = 0; p < 13; p++) {
        float x = -1.8f + 0.3f * (float)p, h = 1.0f + 0.7f * (1.0f - fm_fabsf((float)(p - 6)) / 6.0f), x0, y0, x1, y1;
        float *pp = sb.choir.pipe[p];
        pp[3] = 0.0f;
        if (p3d_project((p3_vec){x, 0.6f, 1.2f}, &x0, &y0, &z) && p3d_project((p3_vec){x, 0.6f + h, 1.2f}, &x1, &y1, &z))
            pp[0] = x0, pp[1] = y0, pp[2] = y1, pp[3] = 0.12f * 215.0f / z;
        cr_seg((p3_vec){x, 0.6f, 1.12f}, (p3_vec){x, 0.6f + h, 1.12f}, p & 1 ? RGB(0x7a, 0x5c, 0xff) : RGB(0x3f, 0xa8, 0xff), 0.0f);
    }
    for (float x = -2.6f; x <= 2.61f; x += 0.4f)
        cr_seg((p3_vec){x, 0.002f, -0.9f}, (p3_vec){x, 0.002f, 1.1f}, RGB(0x2a, 0x1f, 0x78), -50.0f);
    for (float zz = -0.9f; zz <= 1.11f; zz += 0.4f)
        cr_seg((p3_vec){-2.6f, 0.002f, zz}, (p3_vec){2.6f, 0.002f, zz}, RGB(0x2a, 0x1f, 0x78), -50.0f);
    for (int s = 0; s < 2; s++)
        for (int q = 0; q < 8; q++) {
            float cx = s ? 0.7f : -0.6f, a0 = (float)q * (FM_TWO_PI / 8.0f), a1 = a0 + FM_TWO_PI / 8.0f;
            cr_seg((p3_vec){cx + 0.7f * fm_cosf(a0), 0.004f, 0.3f + 0.4f * fm_sinf(a0)},
                   (p3_vec){cx + 0.7f * fm_cosf(a1), 0.004f, 0.3f + 0.4f * fm_sinf(a1)}, RGB(0x7a, 0x5c, 0xff), -0.4f);
        }
    static const uint16_t STAGE[3] = {RGB(0x1a, 0x14, 0x40), RGB(0x14, 0x0f, 0x36), RGB(0x0e, 0x0a, 0x28)};
    p3d_box3((p3_vec){0, -0.1f, 0.1f}, (p3_vec){2.6f, 0.1f, 1.0f}, STAGE, 0);
    p3d_line3((p3_vec){-2.6f, 0.003f, -0.9f}, (p3_vec){2.6f, 0.003f, -0.9f}, 2, RGB(0x5f, 0xe0, 0xff), 1.0f);
    static const uint16_t RISER[3] = {RGB(0x2b, 0x30, 0x70), RGB(0x1f, 0x24, 0x58), RGB(0x15, 0x18, 0x40)};
    const uint16_t rwire = RGB(0x3f, 0xa8, 0xff);
    float sw = v->model == 6 ? 0.3f + 0.7f * k[1] : 0.0f;
    for (int i = 0; i < cr.n; i++) {    /* the voices: a step as high as the note, an android on it */
        static const float BW[3] = {0.15f, 0.12f, 0.19f}, BH[3] = {0.2f, 0.26f, 0.17f};
        float x = -1.1f + 2.2f * (float)i / (float)(cr.n - 1), h = 0.06f + (float)notes[i] / 24.0f, y = h, zc = 0.45f;
        float bx = x + sw * 0.05f * fm_sinf(2.0f * t + 1.5f * (float)i), bw = BW[i % 3], bh = BH[i % 3], by = y + 0.16f + bh;
        const uint16_t *body = CR_BOT[i % 4];
        cr.h[i] = h;
        p3d_box3((p3_vec){x, h * 0.5f, zc}, (p3_vec){0.28f, h * 0.5f, 0.28f}, RISER, &rwire);
        for (int s = -1; s <= 1; s += 2)
            p3d_beam3((p3_vec){x + 0.07f * (float)s, y, zc}, (p3_vec){bx + 0.07f * (float)s, y + 0.16f, zc}, 0.03f, CR_DIM, 0);
        p3d_box3((p3_vec){bx, by, zc}, (p3_vec){bw, bh, 0.12f}, body, 0);
        cr_panel((p3_vec){bx, by + 0.3f * bh, zc - 0.125f}, 0.04f, 0.03f, cr.hot ? PINK3 : CR_WARM);
        for (int s = -1; s <= 1; s += 2)
            p3d_line3((p3_vec){bx + bw * (float)s, by + 0.05f, zc}, (p3_vec){bx + 0.08f * (float)s, by - 0.02f, zc - 0.2f}, 2, body[1], 0.0f);
        {
            static const uint16_t PAPER[3] = {RGB(0xf4, 0xf1, 0xff), RGB(0xf4, 0xf1, 0xff), RGB(0xf4, 0xf1, 0xff)};
            const p3_vec q[4] = {{bx - 0.12f, by - 0.08f, zc - 0.22f}, {bx + 0.12f, by - 0.08f, zc - 0.22f},
                                 {bx + 0.12f, by + 0.06f, zc - 0.26f}, {bx - 0.12f, by + 0.06f, zc - 0.26f}};
            cr_face(q, PAPER);
        }
        float hw = 0.12f + 0.06f * k[2], hh = 0.12f - 0.035f * k[2], hy = by + bh + hh + 0.03f;
        p3d_box3((p3_vec){bx, hy, zc}, (p3_vec){hw, hh, 0.11f}, CR_BOT[2], 0);
        float blink = fm_sinf(1.3f * t + 2.1f * (float)i) > 0.97f ? 0.004f : 0.025f;
        cr_panel((p3_vec){bx - 0.45f * hw, hy + 0.03f, zc - 0.115f}, 0.03f, blink, CR_EYE);
        cr_panel((p3_vec){bx + 0.45f * hw, hy + 0.03f, zc - 0.115f}, 0.03f, blink, CR_EYE);
        {
            static const uint16_t INK[3] = {RGB(0x06, 0x03, 0x1a), RGB(0x06, 0x03, 0x1a), RGB(0x06, 0x03, 0x1a)};
            float open = cr.hot ? 0.055f : 0.012f + 0.008f * fm_sinf(6.0f * t + (float)i);
            cr_panel((p3_vec){bx, hy - 0.055f, zc - 0.115f}, 0.05f, open, cr.hot ? PINK3 : INK);
        }
        p3d_line3((p3_vec){bx, hy + hh, zc}, (p3_vec){bx, hy + hh + 0.13f, zc}, 1, CR_DIM[1], 0.0f);
        p3d_box3((p3_vec){bx, hy + hh + 0.15f, zc}, (p3_vec){0.028f, 0.028f, 0.028f}, cr.hot ? PINK3 : CR_WARM, 0);
        cr.ntop[i] = p3d_project((p3_vec){bx, hy + 0.2f, zc}, &cr.top[i][0], &cr.top[i][1], &z);
    }
    /* the conductor, from behind, on his podium */
    static const uint16_t POD[3] = {RGB(0x6b, 0x5a, 0xa8), RGB(0x4a, 0x3a, 0x85), RGB(0x2e, 0x24, 0x60)};
    p3d_box3((p3_vec){0, 0.07f, CR_CZ}, (p3_vec){0.22f, 0.07f, 0.18f}, POD, 0);
    for (int s = -1; s <= 1; s += 2) {
        p3d_beam3((p3_vec){0.05f * (float)s, 0.14f, CR_CZ}, (p3_vec){0.05f * (float)s, 0.32f, CR_CZ}, 0.03f, CR_COAT, 0);
        p3d_beam3((p3_vec){0.05f * (float)s, 0.36f, CR_CZ - 0.07f}, (p3_vec){0.07f * (float)s, 0.2f, CR_CZ - 0.09f}, 0.025f, CR_COAT, 0);
    }
    p3d_box3((p3_vec){0, 0.45f, CR_CZ}, (p3_vec){0.1f, 0.14f, 0.07f}, CR_COAT, 0);
    p3d_box3((p3_vec){0, 0.67f, CR_CZ}, (p3_vec){0.075f, 0.075f, 0.07f}, CR_BOT[2], 0);
    p3_vec rh = {0.24f, 0.74f - 0.24f * cr.beat, CR_CZ + 0.05f}, lh = {-0.22f, 0.55f + 0.05f * fm_sinf(2.0f * t), CR_CZ + 0.05f};
    p3d_beam3((p3_vec){0.1f, 0.55f, CR_CZ}, rh, 0.022f, CR_COAT, 0);
    p3d_beam3((p3_vec){-0.1f, 0.55f, CR_CZ}, lh, 0.022f, CR_COAT, 0);
    p3d_line3(rh, (p3_vec){rh.x + 0.1f, rh.y + 0.22f - 0.3f * cr.beat, rh.z + 0.12f}, 1, cr.hot ? PINK3[1] : CR_BOT[2][0], 0.0f);
}

void choir_band(int32_t oy)
{
    float t = cr.t;
    int i, k;
    cv_rect(0, oy, 240, 120, RGB(0x05, 0x03, 0x1a));
    cv_rect(0, oy + 120, 240, 46, RGB(0x0a, 0x06, 0x24));
    stars(oy, t, 30, 3, 56, 1.0f);
    for (i = 0; i < 13; i++) {          /* the organ's pipes, as silhouettes */
        const float *p = sb.choir.pipe[i];
        if (p[3] > 0.0f)
            cv_rect((int32_t)(p[0] - 0.5f * p[3]), oy + (int32_t)p[2], (int32_t)(p[3] + 0.999f), (int32_t)(p[1] - p[2]), RGB(0x1f, 0x1a, 0x4a));
    }
    for (i = 0; i < cr.nseg; i++)
        cv_line(sb.choir.seg[i][0], oy + sb.choir.seg[i][1], sb.choir.seg[i][2], oy + sb.choir.seg[i][3], sb.choir.segc[i]);
    p3d_draw(oy);
    if (cr.since < 0.45f)               /* notes flying off every other singer */
        for (i = 0; i < cr.n; i++)
            if (cr.ntop[i] && !((i + (int)cr.steps) & 1)) {
                float sx = cr.top[i][0], yy = cr.top[i][1] - 50.0f * cr.since + (float)oy;
                p3d_fill(sx + 8, yy, sx + 11, yy - 2, sx + 14, yy, PINK3[1]);
                p3d_fill(sx + 8, yy, sx + 14, yy, sx + 11, yy + 2, PINK3[1]);
                cv_line((int32_t)(sx + 14), (int32_t)yy, (int32_t)(sx + 14), (int32_t)(yy - 9), PINK3[1]);
            }
    for (k = 0; k < 5; k++) {           /* the hologram curtains, pulsing */
        float a = 0.5f + 0.5f * fm_sinf(2.0f * t + (float)k);
        uint16_t c = p3d_mix(RGB(0x7a, 0x5c, 0xff), RGB(0x1a, 0x14, 0x40), 0.3f + 0.5f * a);
        cv_rect(2 + 6 * k, oy, 2, 140, c);
        cv_rect(236 - 6 * k, oy, 2, 140, c);
    }
    cv_rect(0, oy, 240, 2, RGB(0x3a, 0x2a, 0x8a));
    const char *name = CR_NAME[cr.chord];
    cv_text(222 - text_w(&FONT_S, name), oy + 10, &FONT_S, name, RGB(0xc9, 0xc3, 0xe4));
}
