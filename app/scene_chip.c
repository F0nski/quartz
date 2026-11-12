/* CHIPTUNE's scene (spec 5d1, 6; docs/screens/chiptune.html platforms): a cube robot hops from block to block, a
 * block per note of the chord at its pitch, in the arpeggio's order. Part of the unity build after scene.c
 * (app_ui.c): its sc, PINK3. */

#define CH_FOG RGB(0x0a, 0x06, 0x26)
#define CH_HOP 0.22f                /* a hop's length, s */
#define CH_SQ 64                    /* the moon's and clouds' voxels, drawn in the band */
static const uint16_t CH_CYAN[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t CH_METAL[3] = {RGB(0x2b, 0x30, 0x70), RGB(0x1f, 0x24, 0x58), RGB(0x15, 0x18, 0x40)};
static const uint16_t CH_VIOLET[3] = {RGB(0x3a, 0x2c, 0x90), RGB(0x2e, 0x24, 0x70), RGB(0x21, 0x1a, 0x55)};
static const uint16_t CH_GREEN[3] = {RGB(0x8f, 0xf5, 0xc8), RGB(0x22, 0xd5, 0x8e), RGB(0x0f, 0x6d, 0x48)};
static const int8_t CH_CHORD[11][4] = {{0, 12, -1, -1}, {0, 7, 12, -1}, {0, 5, 7, 12}, {0, 3, 7, 12}, {0, 3, 7, 10}, {0, 3, 10, 14},
                                       {0, 3, 10, 17}, {0, 2, 9, 16}, {0, 4, 11, 14}, {0, 4, 7, 11}, {0, 4, 7, 12}};
static const char *const CH_CHORD_NAME[11] = {"OCT", "5", "SUS4", "m", "m7", "m9", "m11", "69", "M9", "M7", "M"};
static const char *const CH_MODE_NAME[4] = {"UP", "DOWN", "UP-DN", "RAND"};
static struct {
    float t, since, rx, ry, fx, fy, bx[4], by[4], shown_y;   /* shown_y: the robot's height with its arc */
    int n, target, mode, chord, score;
    uint32_t hits, steps;
    int nsq, ngrid;                 /* the voxels' squares (x, y, size) and tones: sb.chip.sq, sqc; the grid: sb.chip.grid */
} ch;

/* the arpeggio's block at step: up, down, up and down, or at random (TIMB) */
static int ch_arp(int n, int mode, uint32_t step)
{
    if (mode == 0)
        return (int)(step % (uint32_t)n);
    if (mode == 1)
        return n - 1 - (int)(step % (uint32_t)n);
    if (mode == 2) {
        int p = (int)(step % (uint32_t)(2 * n - 2));
        return p < n ? p : 2 * n - 2 - p;
    }
    return (int)(p3d_hash(step * 13u + 7u) * (float)n) % n;
}

/* a voxel as a screen square in three tones (the moon, the clouds: flat to the camera, no primitive) */
static void ch_voxel(p3_vec c, float half, const uint16_t t[3])
{
    float x, y, z;
    if (ch.nsq == CH_SQ || !p3d_project(c, &x, &y, &z))
        return;
    float s = 2.0f * half * 210.0f / z;
    sb.chip.sq[ch.nsq][0] = (int16_t)(x - 0.5f * s), sb.chip.sq[ch.nsq][1] = (int16_t)(y - 0.5f * s), sb.chip.sq[ch.nsq][2] = (int16_t)(s + 0.999f);
    for (int k = 0; k < 3; k++)
        sb.chip.sqc[ch.nsq][k] = p3d_fogged(t[k], z);
    ch.nsq++;
}

static void ch_open_quad(const p3_vec q[4], const uint16_t t[3], const uint16_t *edge, float bias)
{
    const p3_vec r[4] = {q[3], q[2], q[1], q[0]};
    p3d_quad3(q, t, edge, bias);
    p3d_quad3(r, t, edge, bias);
}

/* k: HARM (the chord), TIMB (the arpeggio's order), MORPH (the blocks' width: the pulse), DECAY (unused: hops are
   the notes) */
void chip_frame(const q_sview *v, uint32_t ms)
{
    float t = (float)(ms % 3600000u) * 0.001f, z;
    p3_cam cam = {0.08f * fm_sinf(0.3f * t), 0.2f, 3.4f, 1.6f, 210.0f, 96.0f};
    ch.t = t;
    ch.chord = v->k[0] * 11 / 128;
    ch.mode = v->k[1] * 4 / 128;
    ch.n = CH_CHORD[ch.chord][2] < 0 ? 2 : CH_CHORD[ch.chord][3] < 0 ? 3 : 4;
    float W = 0.1f + 0.3f * (1.0f - (float)v->k[2] / 127.0f);
    for (int i = 0; i < ch.n; i++) {
        ch.bx[i] = -1.6f + 3.2f * ((float)i + 0.5f) / (float)ch.n;
        ch.by[i] = 0.1f + (float)CH_CHORD[ch.chord][i] / 17.0f * 1.4f;
    }
    ch.since = (float)(ms - sc.hit_ms[0]) * 0.001f;
    if (sc.fresh || ch.hits > sc.hits[0]) {      /* just shown: on the arpeggio's first block, no hop */
        ch.hits = sc.hits[0];
        ch.steps = 0;
        ch.target = ch_arp(ch.n, ch.mode, 0);
        ch.fx = ch.rx = ch.bx[ch.target], ch.fy = ch.ry = ch.by[ch.target];
        ch.since = 10.0f;
    } else if (sc.hits[0] != ch.hits) {  /* a note: a hop from where it is to the next block */
        ch.steps += sc.hits[0] - ch.hits;
        ch.hits = sc.hits[0];
        ch.fx = ch.rx, ch.fy = ch.shown_y;      /* from where it is shown: the arc's height too */
    }
    ch.target = ch_arp(ch.n, ch.mode, ch.steps);
    ch.score = (int)ch.steps * 10;
    float k = fm_minf(1.0f, ch.since / CH_HOP), arc = 0.5f * fm_sinf(FM_PI * k);
    int land = k >= 1.0f;
    ch.rx = ch.fx + (ch.bx[ch.target] - ch.fx) * k;
    ch.ry = ch.fy + (ch.by[ch.target] - ch.fy) * k;
    float x = ch.rx, y = ch.ry + arc;
    ch.shown_y = y;
    sb_owner = SB_CHIP;
    p3d_begin();
    p3d_camera(&cam, (p3_vec){-0.5f, 0.75f, -0.45f}, CH_FOG, 12.0f);
    ch.nsq = 0;                         /* the voxel moon and the drifting clouds, as squares in the band */
    for (int i = -2; i <= 2; i++)
        for (int j = -2; j <= 2; j++)
            if (i * i + j * j <= 5) {
                static const uint16_t M0[3] = {RGB(0xe4, 0xdd, 0xff), RGB(0xb3, 0xa6, 0xe8), RGB(0x6f, 0x5f, 0xb4)};
                static const uint16_t M1[3] = {RGB(0xb3, 0xa6, 0xe8), RGB(0x8f, 0x80, 0xd0), RGB(0x5e, 0x4f, 0x9a)};
                ch_voxel((p3_vec){-2.2f + 0.16f * (float)i, 2.2f + 0.16f * (float)j, 3.5f}, 0.08f, i + j < 0 ? M0 : M1);
            }
    static const float CLOUD[3][3] = {{-2.4f, 1.8f, 3.0f}, {0.2f, 2.4f, 3.6f}, {2.8f, 1.4f, 2.8f}};
    static const float PUFF[6][2] = {{0, 0}, {1, 0}, {2, 0}, {0.5f, 1}, {1.5f, 1}, {3, 0}};
    for (int c = 0; c < 3; c++) {
        float cx = CLOUD[c][0] + 0.15f * t + 4.0f;
        cx = cx - 8.0f * fm_floorf(cx / 8.0f) - 4.0f;
        int nc = 5 + ((int)(cx * 3.0f + (cx >= 0 ? 0.5f : -0.5f)) & 1);
        for (int p = 0; p < nc; p++)
            ch_voxel((p3_vec){cx + PUFF[p][0] * 0.2f, CLOUD[c][1] + PUFF[p][1] * 0.2f, CLOUD[c][2]}, 0.1f, CH_VIOLET);
    }
    ch.ngrid = 0;                       /* the ground's grid, far below, fully fogged */
    for (int i = 0; i < 21 && ch.ngrid < 40; i++) {
        float gx = -5.0f + 0.5f * (float)i, *g = sb.chip.grid[ch.ngrid];
        if (p3d_project((p3_vec){gx, -0.9f, -1.5f}, &g[0], &g[1], &z) && p3d_project((p3_vec){gx, -0.9f, 6.0f}, &g[2], &g[3], &z))
            ch.ngrid++;
    }
    for (int i = 0; i < 16 && ch.ngrid < 40; i++) {
        float gz = -1.5f + 0.5f * (float)i, *g = sb.chip.grid[ch.ngrid];
        if (p3d_project((p3_vec){-5.0f, -0.9f, gz}, &g[0], &g[1], &z) && p3d_project((p3_vec){5.0f, -0.9f, gz}, &g[2], &g[3], &z))
            ch.ngrid++;
    }
    for (int i = 0; i < ch.n; i++) {    /* the blocks: a violet base, a neon top, joints, a pillar */
        float bx = ch.bx[i], by = ch.by[i];
        int hit = i == ch.target && ch.since < 0.15f;
        const uint16_t wire = hit ? PINK3[0] : RGB(0x7a, 0x5c, 0xff);
        p3d_box3((p3_vec){bx, by - 0.08f, 0}, (p3_vec){W, 0.08f, 0.25f}, hit ? PINK3 : CH_VIOLET, &wire);
        p3d_box3((p3_vec){bx, by + 0.005f, 0}, (p3_vec){W, 0.012f, 0.25f}, hit ? PINK3 : CH_GREEN, 0);
        uint16_t jc = RGB(0x5a, 0x48, 0xa0);
        p3d_line3((p3_vec){bx - 0.5f * W, by - 0.15f, -0.252f}, (p3_vec){bx - 0.5f * W, by - 0.01f, -0.252f}, 1, jc, 0.05f);
        p3d_line3((p3_vec){bx + 0.5f * W, by - 0.15f, -0.252f}, (p3_vec){bx + 0.5f * W, by - 0.01f, -0.252f}, 1, jc, 0.05f);
        p3d_line3((p3_vec){bx - W, by - 0.08f, -0.252f}, (p3_vec){bx + W, by - 0.08f, -0.252f}, 1, jc, 0.05f);
        p3d_line3((p3_vec){bx, by - 0.16f, 0}, (p3_vec){bx, -0.9f, 0}, 1, RGB(0x2a, 0x1f, 0x78), 0.0f);
    }
    {                                   /* its shadow on the block it goes to */
        static const uint16_t SH[3] = {RGB(0x1a, 0x6a, 0x50), RGB(0x1a, 0x6a, 0x50), RGB(0x1a, 0x6a, 0x50)};
        float sy = ch.by[ch.target] + 0.02f;
        const p3_vec q[4] = {{x - 0.12f, sy, -0.12f}, {x - 0.12f, sy, 0.12f}, {x + 0.12f, sy, 0.12f}, {x + 0.12f, sy, -0.12f}};
        ch_open_quad(q, SH, 0, 0.1f);
    }
    float sq = land ? 0.035f * fm_expf(-fm_minf(ch.since - CH_HOP, 2.0f) * 18.0f) : -0.02f * fm_sinf(FM_PI * k);
    float hw = 0.12f + sq, hh = 0.12f - sq;
    const uint16_t white = RGB(0xff, 0xff, 0xff);
    p3d_box3((p3_vec){x, y + hh + 0.06f, 0}, (p3_vec){hw, hh, 0.11f}, CH_CYAN, &white);
    for (int s = -1; s <= 1; s += 2)
        p3d_beam3((p3_vec){x + 0.05f * (float)s, y + 0.07f, 0}, (p3_vec){x + 0.06f * (float)s, y, 0}, 0.02f, CH_METAL, 0);
    static const uint16_t EYE[3] = {RGB(0x05, 0x06, 0x0f), RGB(0x05, 0x06, 0x0f), RGB(0x05, 0x06, 0x0f)};
    p3d_box3((p3_vec){x + 0.05f, y + hh + 0.1f, -0.115f}, (p3_vec){0.02f, 0.025f, 0.003f}, EYE, 0);
    p3d_box3((p3_vec){x - 0.04f, y + hh + 0.1f, -0.115f}, (p3_vec){0.02f, 0.025f, 0.003f}, EYE, 0);
    p3d_line3((p3_vec){x, y + 2.0f * hh + 0.06f, 0}, (p3_vec){x + 0.04f, y + 2.0f * hh + 0.2f, 0}, 1, CH_METAL[1], 0.0f);
    p3d_box3((p3_vec){x + 0.04f, y + 2.0f * hh + 0.22f, 0}, (p3_vec){0.022f, 0.022f, 0.022f}, ch.since < 0.15f ? PINK3 : CH_CYAN, 0);
    if (!land)                          /* a trail of small cubes behind, in the air */
        for (int j = 1; j <= 4; j++) {
            static const uint16_t TR[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x5f, 0xe0, 0xff), RGB(0x5f, 0xe0, 0xff)};
            float kk = fm_maxf(0.0f, k - 0.08f * (float)j);
            p3_vec p = {ch.fx + (ch.bx[ch.target] - ch.fx) * kk, ch.fy + (ch.by[ch.target] - ch.fy) * kk + 0.5f * fm_sinf(FM_PI * kk) + 0.12f, 0};
            p3d_box3(p, (p3_vec){0.025f, 0.025f, 0.025f}, TR, 0);
        }
    if (ch.since < 0.4f) {              /* a pink coin spinning up */
        float cy = y + 0.6f + 1.2f * ch.since, cw = 0.07f * fm_fabsf(fm_cosf(18.0f * ch.since)) + 0.01f;
        const p3_vec q[4] = {{x, cy, -0.05f}, {x + cw, cy + 0.08f, -0.05f}, {x, cy + 0.16f, -0.05f}, {x - cw, cy + 0.08f, -0.05f}};
        ch_open_quad(q, PINK3, &PINK3[0], 1.0f);
    }
}

void chip_band(int32_t oy)
{
    float t = ch.t;
    int i, k;
    char b[16];
    cv_rect(0, oy, 240, 120, RGB(0x05, 0x03, 0x1a));
    cv_rect(0, oy + 120, 240, 46, CH_FOG);
    for (k = 0; k < 26; k++)            /* twinkling stars */
        if (fm_sinf(2.0f * t + 1.7f * (float)k) > -0.5f) {
            int32_t x = (int32_t)(p3d_hash((uint32_t)k * 3u + 4000u) * 240.0f), y = 4 + (int32_t)(p3d_hash((uint32_t)k * 5u + 4100u) * 110.0f);
            cv_rect(x, oy + y, k % 4 ? 1 : 2, k % 4 ? 1 : 2, k % 4 ? RGB(0x6c, 0x5f, 0xa0) : RGB(0xff, 0xff, 0xff));
        }
    for (i = 0; i < ch.ngrid; i++) {
        const float *g = sb.chip.grid[i];
        cv_line((int32_t)g[0], oy + (int32_t)g[1], (int32_t)g[2], oy + (int32_t)g[3], p3d_mix(RGB(0x22, 0x18, 0x6a), CH_FOG, 0.85f));
    }
    for (i = 0; i < ch.nsq; i++) {      /* voxels: the base tone, the lit top row, the shaded right column */
        int32_t x = sb.chip.sq[i][0], y = oy + sb.chip.sq[i][1], s = sb.chip.sq[i][2];
        cv_rect(x, y, s, s, sb.chip.sqc[i][1]);
        cv_rect(x, y, s, 1, sb.chip.sqc[i][0]);
        cv_rect(x + s - 1, y + 1, 1, s - 1, sb.chip.sqc[i][2]);
    }
    p3d_draw(oy);
    for (i = 0, k = ch.score; i < 5; i++, k /= 10)   /* the score, five digits, top left; the chord and mode, top right */
        b[4 - i] = (char)('0' + k % 10);
    b[5] = 0;
    cv_text(6, oy + 4, &FONT_S, b, RGB(0xff, 0xd2, 0x5a));
    {
        const char *cn = CH_CHORD_NAME[ch.chord], *mn = CH_MODE_NAME[ch.mode];
        int n = 0;
        while (*cn) b[n++] = *cn++;
        b[n++] = ' ', b[n++] = ' ';
        while (*mn) b[n++] = *mn++;
        b[n] = 0;
        cv_text(234 - text_w(&FONT_S, b), oy + 4, &FONT_S, b, RGB(0xc9, 0xc3, 0xe4));
    }
}
