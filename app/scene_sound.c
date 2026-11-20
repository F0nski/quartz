/* The sound pages (spec 5f; docs/screens/sound.html): what the knobs set, drawn as low-poly ribbons and blocks in an
 * oblique projection (no vanishing point), each knob's element in its tones, a note's reaction in pink. p3d's 2D
 * primitives, all at one depth: the sort is stable, so they are drawn in the order they are made. Part of the unity
 * build after scene_home.c (app_ui.c): sc, PINK3, MB_NAME, the knobs' tones HM_BLUE .. HM_ORANGE, HM_MODEL. */
#include "project.h"

#define SP_DX 12.0f                 /* a row's depth on screen */
#define SP_DY -9.0f
#define SP_X0 16.0f                 /* the plot, over the floor */
#define SP_X1 216.0f
#define SP_YT 32.0f
#define SP_YB 142.0f
#define SP_FX(x) (14.0f + 192.0f * (x))     /* FILTER's plot: x and the response 0..1 */
#define SP_FY(m) (146.0f - 70.0f * (m))
#define SP_GRID RGB(0x1d, 0x18, 0x38)
#define SP_WIRE RGB(0x2e, 0x28, 0x50)
#define SP_MAXP 128
#define SP_BAR RGB(0x06, 0x04, 0x1a)    /* the knobs' index */
static const uint16_t SP_GRAY[3] = {RGB(0x6f, 0x68, 0x90), RGB(0x4a, 0x44, 0x66), RGB(0x2c, 0x28, 0x44)};
static const uint16_t SP_RAIL[3] = {RGB(0x2a, 0x24, 0x40), RGB(0x1a, 0x15, 0x30), RGB(0x10, 0x0c, 0x20)};
static const char *const SP_DEST[7] = {"HARM", "TIMB", "MORPH", "PITCH", "CUTOFF", "RES", "LEVEL"};
static const char *const SP_FTYPE[3] = {"LOW-PASS", "HIGH-PASS", "BAND-PASS"};    /* ui.c's order */
static struct {
    q_pview v;
    uint32_t hits, hit_ms;
    float tn, ph;                   /* since the last note (s); the LFO's last phase */
    int cyc;                        /* the LFO's cycles seen: the diamond walks the cycles shown */
    int ast, snd, steps;            /* ARP: the step played last (-1 none yet), the note sounding, the steps */
    float lx, ly;                   /* the played step's name, centred there (lab "": none) */
    char lab[6];                    /* up to "C#-1" */
    float px[SP_MAXP], py[SP_MAXP]; /* the polyline being built */
    const uint16_t *tone[SP_MAXP];  /* its segments' tones */
    int n;
} sp;

static void sp_tri(float x0, float y0, float x1, float y1, float x2, float y2, uint16_t c)   /* a flat one: nothing */
{
    if (fm_fabsf((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0)) > 0.01f)
        p3d_tri(x0, y0, x1, y1, x2, y2, 0.0f, c);
}

static void sp_quad(float x0, float y0, float x1, float y1, float x2, float y2, float x3, float y3, uint16_t c)
{
    sp_tri(x0, y0, x1, y1, x2, y2, c);
    sp_tri(x0, y0, x2, y2, x3, y3, c);
}

static void sp_diamond(float x, float y, float r, uint16_t c)
{
    sp_quad(x, y - r, x + r, y, x, y + r, x - r, y, c);
}

/* appends to the polyline; a point in line with the last two replaces the last (a straight run: one segment) */
static void sp_pt(float x, float y)
{
    int n = sp.n;
    if (n >= 2 && fm_fabsf((sp.px[n - 1] - sp.px[n - 2]) * (y - sp.py[n - 1]) - (sp.py[n - 1] - sp.py[n - 2]) * (x - sp.px[n - 1])) < 0.5f)
        n--;
    sp.px[n] = x, sp.py[n] = y;
    sp.n = n + 1;
}

/* a ribbon over the polyline x, y (row 0) pushed back k rows, d rows deep; segment i in t[i] (light, base, shadow):
   its front, from the curve to base, in the shadow; its top light rising, base flat, base darkened falling; its back
   edge a light line. A segment crossing base (a ribbon around an axis) has its front cut there. */
static void sp_ribbon(const float *x, const float *y, int n, float base, float k, float d, const uint16_t *const *t)
{
    float ox = SP_DX * k, oy = SP_DY * k, dx = SP_DX * d, dy = SP_DY * d;
    base += oy;
    for (int i = 0; i < n - 1; i++) {
        float ax = x[i] + ox, ay = y[i] + oy, bx = x[i + 1] + ox, by = y[i + 1] + oy;
        const uint16_t *c = t[i];
        if ((ay - base) * (by - base) < 0.0f) {
            float mx = ax + (bx - ax) * (base - ay) / (by - ay);
            sp_tri(ax, ay, mx, base, ax, base, c[2]);
            sp_tri(mx, base, bx, by, bx, base, c[2]);
        } else {
            sp_quad(ax, ay, bx, by, bx, base, ax, base, c[2]);
        }
        sp_quad(ax, ay, bx, by, bx + dx, by + dy, ax + dx, ay + dy,
                by < ay - 0.5f ? c[0] : by > ay + 0.5f ? p3d_mix(c[1], 0, 0.25f) : c[1]);
        p3d_line(ax + dx, ay + dy, bx + dx, by + dy, 0.0f, 1, c[0]);
    }
}

/* a block x .. x + w, yt .. yb pushed back k rows, d rows deep: front base, top light, right side shadow */
static void sp_box(float x, float w, float yt, float yb, const uint16_t *t, float k, float d)
{
    float dx = SP_DX * d, dy = SP_DY * d;
    x += SP_DX * k, yt += SP_DY * k, yb += SP_DY * k;
    sp_quad(x, yt, x + w, yt, x + w, yb, x, yb, t[1]);
    sp_quad(x, yt, x + w, yt, x + w + dx, yt + dy, x + dx, yt + dy, t[0]);
    sp_quad(x + w, yt, x + w + dx, yt + dy, x + w + dx, yb + dy, x + w, yb, t[2]);
}

/* the same block in wire, a row deep: its front's three sides and its top */
static void sp_wbox(float x, float w, float yt, float yb, uint16_t c, float k)
{
    x += SP_DX * k, yt += SP_DY * k, yb += SP_DY * k;
    p3d_line(x, yb, x, yt, 0.0f, 1, c);
    p3d_line(x, yt, x + w, yt, 0.0f, 1, c);
    p3d_line(x + w, yt, x + w, yb, 0.0f, 1, c);
    p3d_line(x, yt, x + SP_DX, yt + SP_DY, 0.0f, 1, c);
    p3d_line(x + SP_DX, yt + SP_DY, x + w + SP_DX, yt + SP_DY, 0.0f, 1, c);
    p3d_line(x + w + SP_DX, yt + SP_DY, x + w, yt, 0.0f, 1, c);
}

/* the wire floor, drawn in the band under the primitives: rows + 1 lines x0 .. x1 going back, cols + 1 across */
static void sp_floor(int32_t oy, float x0, float x1, float yb, int rows, int cols)
{
    for (int j = 0; j <= rows; j++)
        cv_line((int32_t)(x0 + SP_DX * (float)j), oy + (int32_t)(yb + SP_DY * (float)j), (int32_t)(x1 + SP_DX * (float)j),
                oy + (int32_t)(yb + SP_DY * (float)j), SP_GRID);
    for (int i = 0; cols && i <= cols; i++) {
        float x = x0 + (x1 - x0) * (float)i / (float)cols;
        cv_line((int32_t)x, oy + (int32_t)yb, (int32_t)(x + SP_DX * (float)rows), oy + (int32_t)(yb + SP_DY * (float)rows), SP_GRID);
    }
}

/* ADSR: the envelope over 2.3 s, a segment a knob; the hold fixed at 0.45 s; the note's segment pink */
static void sp_adsr(const q_pview *v)
{
    float a = 0.05f + 0.55f * (float)v->k[0] / 127.0f, d = 0.05f + 0.55f * (float)v->k[1] / 127.0f, s = (float)v->k[2] / 127.0f;
    float h = 0.45f, r = 0.05f + 0.8f * (float)v->k[3] / 127.0f, tn = sp.tn;
    const float q[5] = {0.0f, a, a + d, a + d + h, a + d + h + r}, l[5] = {0.0f, 1.0f, s, s, 0.0f};
    const uint16_t *t[4] = {HM_BLUE, HM_GREEN, HM_WHITE, HM_ORANGE};
    float x[5], y[5];
    for (int i = 0; i < 5; i++)
        x[i] = SP_X0 + (SP_X1 - SP_X0) * q[i] / 2.3f, y[i] = SP_YB - (SP_YB - SP_YT) * l[i];
    for (int i = 0; i < 4; i++)
        if (tn >= q[i] && tn < q[i + 1])
            t[i] = PINK3;
    sp_ribbon(x, y, 5, SP_YB, 0.0f, 1.0f, t);
}

/* the filter's response at x (0..1, the CUTOFF knob's scale: 119.5 semitones from 20 Hz) for its type (ui.c's: LP, HP,
   BP), cutoff xc and resonance q (0..1): mod.c's SVF, Q 0.5 .. 30, as 0 at -36 dB, 1 at +6 dB (0 dB: 6/7) */
static float sp_resp(int type, float xc, float q, float x)
{
    float r = fm_exp2f((x - xc) * (119.5f / 12.0f)), Q = 0.5f * fm_powf(60.0f, q), a = 1.0f - r * r;
    float d = fm_sqrtf(a * a + r * r / (Q * Q)), g = (type == 0 ? 1.0f : type == 1 ? r * r : r) / d;
    return fm_clampf((6.0206f * fm_log2f(fm_maxf(g, 1e-4f)) + 36.0f) / 42.0f, 0.0f, 1.6f);
}

/* a band over the polyline x, y, pushed back k rows, d rows deep: only its top, as sp_ribbon's (no front: the curve floats
   over the floor), its back edge a light line */
static void sp_band(const float *x, const float *y, int n, float k, float d, const uint16_t *const *t)
{
    float ox = SP_DX * k, oy = SP_DY * k, dx = SP_DX * d, dy = SP_DY * d;
    for (int i = 0; i < n - 1; i++) {
        float ax = x[i] + ox, ay = y[i] + oy, bx = x[i + 1] + ox, by = y[i + 1] + oy;
        const uint16_t *c = t[i];
        sp_quad(ax, ay, bx, by, bx + dx, by + dy, ax + dx, ay + dy,
                by < ay - 0.5f ? c[0] : by > ay + 0.5f ? p3d_mix(c[1], 0, 0.25f) : c[1]);
        p3d_line(ax, ay, bx, by, 0.0f, 1, c[2]);
        p3d_line(ax + dx, ay + dy, bx + dx, by + dy, 0.0f, 1, c[0]);
    }
}

/* FILTER: the response floating over a log-frequency floor, on stilts; the cutoff's pillar, the resonance's crystal on
   the peak. ENV AMT on CUTOFF: the response the envelope reaches, in wire, and its arrow; a note: the curve comes back
   from there in 0.9 s, pink. ENV AMT elsewhere (ENV DEST): its gauge at the right, up or down, and the destination's name
   (sound_band) */
static void sp_filter(const q_pview *v)
{
    int type = v->k[0], cutdest = v->edest == LD_CUT;
    float cut = (float)v->k[1] / 127.0f, q = (float)v->k[2] / 127.0f, amt = (float)v->k[3] / 63.0f, tn = sp.tn;
    float env = tn < 0.9f ? (1.0f - tn / 0.9f) * (1.0f - tn / 0.9f) : 0.0f;
    float cut2 = fm_clampf(cut + amt * (96.0f / 119.5f), 0.0f, 1.0f), cc = cutdest ? cut + (cut2 - cut) * env : cut;
    const uint16_t *tone = tn < 0.4f ? PINK3 : HM_GREEN;
    if (cutdest && v->k[3]) {           /* where the envelope takes it: in wire, two rows back */
        float px = 0.0f, py = 0.0f;
        for (int i = 0; i <= 40; i++) {
            float x = SP_FX((float)i / 40.0f) + 2.0f * SP_DX, y = SP_FY(sp_resp(type, cut2, q, (float)i / 40.0f)) + 2.0f * SP_DY;
            if (i)
                p3d_line(px, py, x, y, 0.0f, 1, HM_ORANGE[2]);
            px = x, py = y;
        }
    }
    sp.n = 0;
    for (int i = 0; i <= 48; i++)
        sp_pt(SP_FX((float)i / 48.0f), SP_FY(sp_resp(type, cc, q, (float)i / 48.0f)));
    for (int i = 0; i < 16; i++) {      /* the stilts, every 1/16 */
        float x = ((float)i + 0.5f) / 16.0f, y = SP_FY(sp_resp(type, cc, q, x));
        if (y < SP_FY(0) - 2.0f)
            p3d_line(SP_FX(x) + SP_DX * 0.5f, y + SP_DY * 0.5f, SP_FX(x) + SP_DX * 0.5f, SP_FY(0) + SP_DY * 0.5f, 0.0f, 1, SP_WIRE);
    }
    for (int i = 0; i < sp.n; i++)
        sp.tone[i] = tone;
    sp_band(sp.px, sp.py, sp.n, 0.0f, 1.0f, sp.tone);
    float tc = SP_FY(sp_resp(type, cc, q, cc));
    sp_box(SP_FX(cc) - 2.0f, 4.0f, tc + 3.0f, SP_FY(0), tone, 0.3f, 0.4f);   /* the cutoff's pillar, under the curve */
    if (cutdest && v->k[3]) {           /* the arrow on the floor, cutoff to target */
        float y = SP_FY(0) + 6.0f;
        p3d_line(SP_FX(cut), y, SP_FX(cut2), y, 0.0f, 1, HM_ORANGE[1]);
        sp_diamond(SP_FX(cut2), y, 3.0f, HM_ORANGE[1]);
    }
    if (q > 0.08f) {                    /* the crystal: its lit half, then its shaded one */
        float x = SP_FX(cc) + SP_DX * 0.5f, y = tc + SP_DY * 0.5f - 3.0f, r = 3.0f + 4.0f * q;
        sp_quad(x, y - 2.2f * r, x + r, y - r, x, y, x - r, y - r, HM_WHITE[0]);
        sp_tri(x, y - 2.2f * r, x + r, y - r, x, y, HM_WHITE[1]);
    }
    if (!cutdest) {                     /* ENV AMT's gauge: up or down from the middle, pink on a note */
        float ym = SP_FY(0.55f), h = 40.0f * amt * (tn < 0.9f ? 0.5f + 0.5f * env : 1.0f);
        if (h > 1.0f || h < -1.0f)
            sp_box(212.0f, 9.0f, h > 0.0f ? ym - h : ym, h > 0.0f ? ym : ym - h, tn < 0.4f ? PINK3 : HM_ORANGE, 0.0f, 0.5f);
        p3d_line(206.0f, ym, 228.0f, ym, 0.0f, 1, HM_ORANGE[2]);
    }
}

/* the LFO's shapes (SIN, TRI, SAW, SQR, S&H) at p cycles, -1..1 */
static float sp_wave(int shape, float p)
{
    float f = p - fm_floorf(p);
    switch (shape) {
    case 0: return fm_sinf(FM_TWO_PI * f);
    case 1: return f < 0.25f ? 4.0f * f : f < 0.75f ? 2.0f - 4.0f * f : 4.0f * f - 4.0f;   /* mod.c's lfo_value */
    case 2: return 2.0f * f - 1.0f;
    case 3: return f < 0.5f ? 1.0f : -1.0f;
    default: return 2.0f * p3d_hash((uint32_t)(int32_t)fm_floorf(p) + 7300u) - 1.0f;
    }
}

/* LFO: the wave's ribbon, RATE's cycles shown, DEPTH its amplitude; a diamond on the LFO's phase, pink on a note */
static void sp_lfo(const q_pview *v)
{
    static const int CYC[5] = {1, 2, 3, 4, 6};
    int shape = v->k[0], cyc = CYC[v->k[1] < LRATE_SYNC ? v->k[1] * 5 / LRATE_SYNC : (v->k[1] - LRATE_SYNC) * 5 / 64];
    float ym = 0.5f * (SP_YT + SP_YB), a = 0.5f * (SP_YB - SP_YT) * (float)v->k[2] / 64.0f, q = 0.0f;
    sp.n = 0;
    for (int i = 0; i <= 48; i++) {     /* 48: the cycles and half cycles fall on samples, the steps upright there */
        float x = SP_X0 + (SP_X1 - SP_X0) * (float)i / 48.0f, p = (float)(i * cyc) / 48.0f;
        float y = ym - a * sp_wave(shape, p), yl = ym - a * sp_wave(shape, p - 0.001f);
        if (i == 48)
            y = yl;
        else if (i > 0 && fm_fabsf(yl - y) > 0.5f)
            sp_pt(x, yl);
        sp_pt(x, y);
    }
    for (int i = 0; i < sp.n; i++)
        sp.tone[i] = HM_BLUE;
    sp_ribbon(sp.px, sp.py, sp.n, SP_YB, 0.0f, 1.0f, sp.tone);
    if (v->lfo_ph >= 0.0f) {            /* no voice: still at the wave's start */
        if (v->lfo_ph < sp.ph - 0.5f)
            sp.cyc++;
        sp.ph = v->lfo_ph;
        q = ((float)(sp.cyc % cyc) + v->lfo_ph) / (float)cyc;
    }
    sp_diamond(SP_X0 + (SP_X1 - SP_X0) * q + 0.5f * SP_DX, ym - a * sp_wave(shape, q * (float)cyc) + 0.5f * SP_DY - 3.0f, 4.0f,
               sp.tn < 0.15f ? PINK3[1] : RGB(0xf4, 0xf1, 0xff));
}

/* ARP: the steps in the MODE's order over OCT octaves (mod.c's arp_step: by pitch, by arrival for ORDER; RAND a fixed
   shuffle), a block a step: height the note, width GATE, every other octave darker; 12 at most, the page of 12 with the
   step played. The step played: the next one sounding's note, pink, its name above. OFF: the notes in one pile.
   Nothing held: the tonic's chord in wire */
static void sp_arp(const q_pview *v)
{
    const uint16_t dark[3] = {HM_BLUE[0], p3d_mix(HM_BLUE[1], 0, 0.2f), p3d_mix(HM_BLUE[2], 0, 0.2f)};
    const uint8_t *h = v->nheld ? v->held : v->chord;
    int nh = v->nheld ? v->nheld : 3, mode = v->k[0], oct = mode ? v->k[1] : 1, L = 0, P, at = -1, ord[8];
    uint8_t ln[32], lo[32], idx[64];
    float gate = (float)v->k[3] / 100.0f, low = 127.0f, top = 0.0f;
    sp.lab[0] = 0;
    if (v->kit)
        return;
    for (int j = 0; j < nh; j++) {
        int k = j;
        if (mode != AM_ORDER) while (k > 0 && h[ord[k - 1]] > h[j]) { ord[k] = ord[k - 1]; k--; }
        ord[k] = j;
    }
    for (int o = 0; o < oct; o++)
        for (int m = 0; m < nh; m++)
            if (h[ord[m]] + 12 * o <= 127)
                ln[L] = (uint8_t)(h[ord[m]] + 12 * o), lo[L++] = (uint8_t)o;
    for (int i = 0; i < L; i++)
        low = fm_minf(low, (float)ln[i]), top = fm_maxf(top, (float)ln[i]);
    top = fm_maxf(12.0f, top - low + 5.0f);
#define SP_H(n) (SP_YB - 14.0f - (SP_YB - SP_YT - 20.0f) * ((float)(n) - low) / top)
    if (mode == AM_OFF) {
        for (int m = 0; m < nh; m++)
            if (v->nheld)
                sp_box(100.0f, 26.0f, SP_H(ln[m]), SP_H(ln[m]) + 10.0f, sp.tn < 0.3f ? PINK3 : HM_BLUE, 0.0f, 1.0f);
            else
                sp_wbox(100.0f, 26.0f, SP_H(ln[m]), SP_H(ln[m]) + 10.0f, SP_WIRE, 0.0f);
        sp.steps = 0;
        return;
    }
    P = mode == AM_UPDN && L > 1 ? 2 * L - 2 : L;
    for (int i = 0; i < P; i++)
        idx[i] = (uint8_t)(mode == AM_DOWN ? L - 1 - i : i < L ? i : 2 * L - 2 - i);
    if (mode == AM_RAND)
        for (int i = L - 1; i > 0; i--) {
            int j = (int)fm_minf((float)i, p3d_hash(7700u + (uint32_t)i) * (float)(i + 1));
            uint8_t t = idx[i];
            idx[i] = idx[j], idx[j] = t;
        }
    if (v->nheld && v->sounding >= 0) {     /* the step: the one before if still its note, else the next with it */
        at = sp.ast < P ? sp.ast : -1;
        if (at < 0 || v->sounding != sp.snd || ln[idx[at]] != v->sounding)
            for (int i = 1; i <= P; i++)
                if (ln[idx[(at + i) % P]] == v->sounding) {
                    at = (at + i) % P;
                    break;
                }
        if (at < 0 || ln[idx[at]] != v->sounding)
            at = -1;
        else
            sp.ast = at;
    }
    sp.snd = v->sounding, sp.steps = P;
    int n = P < 12 ? P : 12, first = v->nheld && sp.ast >= 0 && sp.ast < P ? sp.ast / 12 * 12 : 0;
    float w = (SP_X1 - SP_X0 - SP_DX) / (float)n, bw = fm_maxf(3.0f, w * gate - 2.0f);
    for (int i = 0; i < n && first + i < P; i++) {
        int s = idx[first + i], nt = ln[s];
        float x = SP_X0 + w * (float)i;
        if (!v->nheld) {
            sp_wbox(x, bw, SP_H(nt), SP_YB, SP_WIRE, 0.0f);
            continue;
        }
        sp_box(x, bw, SP_H(nt), SP_YB, first + i == at ? PINK3 : lo[s] & 1 ? dark : HM_BLUE, 0.0f, 1.0f);
        if (first + i == at) {
            char *b = sp.lab;
            int o = nt / 12 - 1;
            for (const char *c = MB_NAME[nt % 12]; *c;)
                *b++ = *c++;
            if (o < 0)
                *b++ = '-', o = 1;
            *b++ = (char)('0' + o), *b = 0;
            sp.lx = x + 0.5f * w * gate + 0.5f * SP_DX, sp.ly = SP_H(nt) + SP_DY - 8.0f;
        }
    }
#undef SP_H
}

/* INSERT: two cycles of a sine in gray wire (the input); the output's ribbon around the axis, shaped by the type
   (ui.c's INS_NAME order, the mock-up's formulas), MIX blending the input in; a note: a pink wave of 8 segments runs
   along it in 1 s. 54 samples, not the mock-up's 72: 5 primitives a segment, 270 at most, and the wire's 18. The
   bitcrush's holds and the smoothing's radius are scaled to keep their widths. */
static void sp_insert(const q_pview *v, float t)
{
    enum { N = 54 };
    float a = (float)v->k[0] / 127.0f, b = (float)v->k[1] / 127.0f, c = (float)v->k[2] / 127.0f, m = (float)v->k[3] / 127.0f;
    float y0[N + 1], y[N + 1], sm = 0.0f, ym = 0.5f * (SP_YT + SP_YB) + 4.0f, A = 0.5f * (SP_YB - SP_YT) - 6.0f;
    float wv = sp.tn < 1.0f ? sp.tn * (float)N : -10.0f;
#define SP_S(q) fm_sinf(2.0f * FM_TWO_PI * (q))     /* the input: two cycles over 0..1 */
#define SP_XQ(q) (SP_X0 + (SP_X1 - SP_X0) * (q))
    for (int i = 0; i <= N; i++) {
        float q = (float)i / (float)N, x = SP_S(q), o;
        switch (v->itype) {
        case 1: { float g = 1.0f + 9.0f * a; o = fm_tanhf(x * g) / fm_tanhf(g); sm = 1.0f - b; break; }
        case 2: {
            float u = (x * (1.0f + 3.0f * a) + b - 0.5f + 1.0f) * 0.25f;
            o = 4.0f * fm_fabsf(u - fm_floorf(u) - 0.5f) - 1.0f, sm = 1.0f - c;
            break;
        }
        case 3: {
            float l = fm_floorf((1.0f - a) * 7.0f + 0.5f) + 1.0f;
            int hold = (int)(b * 6.0f + 0.5f) + 1;
            o = fm_floorf(SP_S((float)(i / hold * hold) / (float)N) * l + 0.5f) / l;
            break;
        }
        case 4: o = 0.5f * (x + SP_S(q + 0.06f * b * fm_sinf(t * (1.0f + 6.0f * a)))); break;
        case 5: o = (x + c * SP_S(q + 0.25f * b * fm_sinf(t * (0.5f + 3.0f * a)))) / (1.0f + c); break;
        case 6: o = x * fm_sinf(FM_TWO_PI * q * (3.0f + 14.0f * a)); break;
        case 7: {
            float p = 0.04f + 0.2f * a, g = 1.0f;
            o = 0.0f;
            for (int k = 0; k < 5; k++, g *= b)
                o += g * SP_S(q - (float)k * p);
            o /= 1.0f + 2.0f * b, sm = c;
            break;
        }
        default: o = x, m = 0.0f;
        }
        y0[i] = o;
    }
    int r = (int)(sm * 4.5f + 0.5f);        /* TONE, DAMP: a moving average, the ends over what there is */
    for (int i = 0; i <= N; i++) {
        float s = 0.0f;
        int n = 0;
        for (int j = i - r; j <= i + r; j++)
            if (j >= 0 && j <= N)
                s += y0[j], n++;
        y[i] = SP_S((float)i / (float)N) * (1.0f - m) + s / (float)n * m;
    }
    for (int i = 0; i < 18; i++)
        p3d_line(SP_XQ((float)i / 18), ym - A * SP_S((float)i / 18), SP_XQ((float)(i + 1) / 18), ym - A * SP_S((float)(i + 1) / 18),
                 0.0f, 1, SP_GRAY[0]);
    sp.n = 0;                               /* merged within a tone only: the wave stays 8 segments */
    for (int i = 0; i <= N; i++) {
        float x = SP_XQ((float)i / N), yy = ym - A * y[i];
        const uint16_t *tn = fm_fabsf((float)i - 0.5f - wv) < 4.0f ? PINK3 : v->itype ? HM_BLUE : SP_GRAY;
        if (sp.n >= 2 && sp.tone[sp.n - 2] == tn)
            sp_pt(x, yy);
        else
            sp.px[sp.n] = x, sp.py[sp.n++] = yy;
        if (i)
            sp.tone[sp.n - 2] = tn;
    }
    sp_ribbon(sp.px, sp.py, sp.n, ym, 0.0f, 1.0f, sp.tone);
#undef SP_S
#undef SP_XQ
}

/* a hexagonal knob in relief at x, y: 11 ticks lit up to val (0..1; bipolar: from the middle), a dark index */
static void sp_knob(float x, float y, float val, const uint16_t *t, int bip)
{
    float hx[6], hy[6], r = 16.0f;
    for (int i = 0; i < 6; i++)
        hx[i] = x + r * fm_cosf(FM_PI / 6.0f + (float)i * FM_PI / 3.0f), hy[i] = y + 0.8f * r * fm_sinf(FM_PI / 6.0f + (float)i * FM_PI / 3.0f);
    sp_quad(hx[0], hy[0], hx[1], hy[1], hx[1], hy[1] + 5.0f, hx[0], hy[0] + 5.0f, t[2]);
    sp_quad(hx[1], hy[1], hx[2], hy[2], hx[2], hy[2] + 5.0f, hx[1], hy[1] + 5.0f, t[2]);
    sp_quad(hx[0], hy[0], hx[1], hy[1], hx[2], hy[2], hx[3], hy[3], t[1]);
    sp_quad(hx[0], hy[0], hx[3], hy[3], hx[4], hy[4], hx[5], hy[5], t[1]);
    sp_quad(hx[3], hy[3], hx[4], hy[4], hx[5], hy[5], x, y, t[0]);
    for (int i = 0; i <= 10; i++) {
        float a = FM_PI * (0.75f + 0.15f * (float)i), f = 0.1f * (float)i, ca = fm_cosf(a), sa = fm_sinf(a);
        int on = bip ? (val >= 0.5f ? f >= 0.5f && f <= val : f <= 0.5f && f >= val) : f <= val;
        p3d_line(x + (r + 4.0f) * ca, y + 0.8f * (r + 4.0f) * sa, x + (r + 9.0f) * ca, y + 0.8f * (r + 9.0f) * sa, 0.0f, 2,
                 on ? t[1] : SP_WIRE);
    }
    float a = FM_PI * (0.75f + 1.5f * val);
    p3d_line(x, y, x + 0.8f * r * fm_cosf(a), y + 0.64f * r * fm_sinf(a), 0.0f, 2, SP_BAR);
}

/* MIX: a console strip: DELAY, REVERB and PAN's knobs, LEVEL's fader; a note: the L/R meter up to LEVEL, shared by
   PAN, falling back in 0.9 s */
static void sp_mix(const q_pview *v)
{
    float lvl = (float)v->k[3] / 127.0f, pan = (float)v->k[2] / 64.0f, cy = 150.0f - 122.0f * lvl;
    float env = fm_maxf(0.0f, 1.0f - sp.tn / 0.9f) * lvl, g[2] = {fm_minf(1.0f, 1.0f - pan) * env, fm_minf(1.0f, 1.0f + pan) * env};
    sp_knob(44.0f, 52.0f, (float)v->k[0] / 127.0f, HM_BLUE, 0);
    sp_knob(108.0f, 52.0f, (float)v->k[1] / 127.0f, HM_GREEN, 0);
    sp_knob(76.0f, 116.0f, (float)(v->k[2] + 64) / 127.0f, HM_WHITE, 1);
    sp_box(160.0f, 4.0f, 28.0f, 150.0f, SP_RAIL, 0.0f, 0.4f);
    for (int i = 0; i <= 8; i++)
        p3d_line(152.0f, 28.0f + 122.0f * (float)i / 8.0f, 157.0f, 28.0f + 122.0f * (float)i / 8.0f, 0.0f, 1, SP_WIRE);
    sp_box(151.0f, 22.0f, cy - 5.0f, cy + 5.0f, HM_ORANGE, 0.0f, 0.6f);
    p3d_line(151.0f, cy, 173.0f, cy, 0.0f, 1, SP_BAR);
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 14; i++) {
            float y = 146.0f - 8.6f * (float)i;
            sp_box(196.0f + 14.0f * (float)j, 9.0f, y - 6.0f, y, (float)i < g[j] * 14.0f ? PINK3 : SP_RAIL, 0.0f, 0.3f);
        }
}

void sound_frame(const q_pview *v, uint32_t ms)
{
    sc.fresh = !sc.seen;                /* (no scene_frame here: its own first frame since scene_enter) */
    sc.seen = 1;
    if (sc.fresh)
        sp.ast = -1;
    if (sc.fresh || v->hits < sp.hits)  /* the notes before the page showed: none */
        sp.hits = v->hits, sp.hit_ms = ms - 100000u;
    else if (v->hits != sp.hits)
        sp.hits = v->hits, sp.hit_ms = ms;
    sp.tn = (float)(ms - sp.hit_ms) * 0.001f;
    sp.v = *v;
    p3d_begin();
    switch (v->page) {
    case SP_ADSR: sp_adsr(v); break;
    case SP_FILTER: sp_filter(v); break;
    case SP_LFO: sp_lfo(v); break;
    case SP_ARP: sp_arp(v); break;
    case SP_INSERT: sp_insert(v, (float)(ms % 60000u) * 0.001f); break;   /* (the chorus' and phaser's sweep) */
    case SP_MIX: sp_mix(v); break;
    }
}

/* small text right-aligned at the top, by the model's name */
static int32_t sp_right(int32_t oy, const char *s, uint16_t c)
{
    int32_t x = 230 - text_w(&FONT_S, s);
    cv_text(x, oy + 4, &FONT_S, s, c);
    return x;
}

void sound_band(int32_t oy)
{
    const q_pview *v = &sp.v;
    cv_rect(0, oy, 240, 166, C_BLACK);
    if (v->page == SP_FILTER)
        sp_floor(oy, SP_FX(0), SP_FX(1), SP_FY(0), 3, 0);
    else if (v->page != SP_MIX)
        sp_floor(oy, SP_X0, SP_X1, SP_YB, 2, 8);
    p3d_draw(oy);
    cv_text(10, oy + 8, &FONT_M, HM_MODEL[v->model], C_WHITE);
    switch (v->page) {
    case SP_ADSR:
        if (v->own_env)                 /* its own envelope shapes the level */
            sp_right(oy, "FILTER ONLY", C_AMB);
        break;
    case SP_FILTER:
        sp_right(oy, SP_FTYPE[v->k[0]], HM_BLUE[1]);
        if (v->edest != LD_CUT) {       /* ENV DEST: ENV, an arrow, the destination */
            int32_t x = 230 - text_w(&FONT_S, SP_DEST[v->edest]), y = oy + 30;
            cv_text(x, oy + 22, &FONT_S, SP_DEST[v->edest], HM_ORANGE[1]);
            cv_line(x - 12, y, x - 5, y, HM_ORANGE[1]);
            cv_line(x - 8, y - 3, x - 5, y, HM_ORANGE[1]);
            cv_line(x - 8, y + 3, x - 5, y, HM_ORANGE[1]);
            cv_text(x - 14 - text_w(&FONT_S, "ENV"), oy + 22, &FONT_S, "ENV", HM_ORANGE[1]);
        }
        break;
    case SP_LFO: {                      /* an arrow, then the destination */
        int32_t x = sp_right(oy, SP_DEST[v->k[3]], HM_ORANGE[1]) - 12, y = oy + 12;
        cv_line(x, y, x + 7, y, HM_ORANGE[1]);
        cv_line(x + 4, y - 3, x + 7, y, HM_ORANGE[1]);
        cv_line(x + 4, y + 3, x + 7, y, HM_ORANGE[1]);
        break;
    }
    case SP_ARP:
        if (v->kit) {
            cv_text(10, oy + 60, &FONT_M, "NO ARP ON KIT", C_GRAY);
        } else if (!sp.steps) {
            sp_right(oy, "OFF", HM_BLUE[1]);
        } else {
            char b[10] = {(char)('0' + sp.steps / 10), (char)('0' + sp.steps % 10), ' ', 'S', 'T', 'E', 'P', 'S', 0};
            sp_right(oy, b + (sp.steps < 10), C_GRAY);
        }
        if (sp.lab[0])
            cv_text((int32_t)sp.lx - text_w(&FONT_S, sp.lab) / 2, oy + (int32_t)sp.ly - 11, &FONT_S, sp.lab, PINK3[0]);
        break;
    case SP_MIX: {                      /* the knobs' and the meter's names */
        static const char *const NAME[5] = {"DELAY", "REVERB", "PAN", "L", "R"};
        static const int16_t X[5] = {44, 108, 76, 201, 215}, Y[5] = {80, 80, 144, 158, 158};
        const uint16_t c[5] = {HM_BLUE[1], HM_GREEN[1], HM_WHITE[1], C_GRAY, C_GRAY};
        for (int i = 0; i < 5; i++)
            cv_text(X[i] - text_w(&FONT_S, NAME[i]) / 2, oy + Y[i] - 11, &FONT_S, NAME[i], c[i]);
        break;
    }
    }
}
