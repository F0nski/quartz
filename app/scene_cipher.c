/* CIPHER's phase space (spec 2026-11-19 CIPHER, the scene; docs/screens/cipher.html, B): each step of the loop a point
 * (v[n], v[n+1], v[n+2]) in a wire cube turning slowly about the vertical, nearly isometric; a pink trail joins the
 * last 6 steps to the one playing. The formula with its values and t on top, the step's byte as 8 bits on the left,
 * what they play on the right (the rhythm against DENS, the accent, the note). Part of the unity build after
 * scene_mimic.c (app_ui.c): ORANGE, MB_WHITE, PINK3, glo_text. */

#define CYV_CX 120.0f               /* the cube's centre in the scene area */
#define CYV_CY 104.0f
#define CYV_S 37.0f                 /* half its edge, in pixels */
#define CYV_TRAIL 6
static const uint16_t CYV_YEL[3] = {RGB(0xff, 0xf6, 0xb0), RGB(0xf2, 0xe1, 0x4c), RGB(0x8a, 0x7a, 0x10)};
static const uint16_t CYV_CYAN[3] = {RGB(0xc8, 0xf4, 0xff), RGB(0x3e, 0xe6, 0xff), RGB(0x1d, 0x7f, 0xa8)};
static const uint16_t CYV_MUTE[3] = {RGB(0x9a, 0x94, 0xd0), RGB(0x60, 0x5a, 0xa0), RGB(0x3a, 0x35, 0x6a)};
#define CYV_BACK RGB(0x22, 0x1f, 0x4a)
#define CYV_FRONT RGB(0x4a, 0x47, 0x96)
#define CYV_OFF RGB(0x1d, 0x1a, 0x38)
#define CYV_LABEL RGB(0xa2, 0x9c, 0xba)
#define CYV_DIM RGB(0x4a, 0x46, 0x70)

/* the formulas as shown, in PRESET's order: A B C their values; D A*4+2, E B*4+3, F A*17+1, G B*17+2, H B*16+1,
   I C*16+15, J B&7 (so the hex constant is written in lower case) */
static const char *const CYV_TEXT[CIPHER_N] = {
    "t*(t>>A|t>>B)", "t&t>>A", "t*(D&t>>B)", "(t&t>>A)|t>>B", "t*(t>>A&t>>B)", "t*A&t>>B", "t*A|t>>B",
    "t*(0xca98>>(t>>A&14)&15)|t>>B",
    "(t*A&t>>B)|(t*3&t>>C)", "t*((t>>A|t>>B)&63&t>>C)", "(t>>A|t|t>>(t>>B))*C+(t>>11&7)", "t*(t^t+(t>>A|1)^(t-1280^t)>>B)",
    "t*A&t>>B|t>>C", "(t&t>>A)-(t>>B)", "t>>A^t<<J", "(t*(t>>A&E))>>(t>>C&3)",
    "t*((t>>A|t>>B)&25&t>>C)", "(t&t>>A)*(t>>B|t>>C)", "t*(t>>A&t>>B&123&t>>C)", "t*((t>>A|t>>B)&82&t>>C)",
    "(t>>A)*(t>>B)", "t*9&t>>A|t*5&t>>B|t*3&t>>C", "t*(t>>A&15)>>(t>>B&3)", "t*(t>>A)&t>>B",
    "t|t%F|t%G", "t*(t>>A)^t>>B", "(t^t>>A)*(t>>B)", "(t*t>>A)&t>>B", "t*(t>>A)%H", "t>>A^t*B",
    "(t*(t>>A)|t>>B)^t>>C", "t*(t>>A^t>>B)&I"};

static struct { uint32_t ms; } cyv;

void cipher_enter(void) { cyv.ms = 0; }

static char *cyv_num(uint32_t v, char *b)       /* v in decimal; the end */
{
    char t[10];
    int n = 0;
    do t[n++] = (char)('0' + v % 10u), v /= 10u; while (v);
    while (n) *b++ = t[--n];
    *b = 0;
    return b;
}

static void cyv_formula(const q_cipher *c, char *o, char *end)
{
    for (const char *s = CYV_TEXT[c->formula % CIPHER_N]; *s && o < end - 6; s++) {
        uint32_t x;
        switch (*s) {
        case 'A': x = c->a; break;
        case 'B': x = c->b; break;
        case 'C': x = c->c; break;
        case 'D': x = c->a * 4u + 2u; break;
        case 'E': x = c->b * 4u + 3u; break;
        case 'F': x = c->a * 17u + 1u; break;
        case 'G': x = c->b * 17u + 2u; break;
        case 'H': x = c->b * 16u + 1u; break;
        case 'I': x = c->c * 16u + 15u; break;
        case 'J': x = c->b & 7u; break;
        default: *o++ = *s; continue;
        }
        o = cyv_num(x, o);
    }
    *o = 0;
}

/* x, y, z in -1..1: turned about the vertical by rot, tilted towards us; the screen's x, y and the depth (more: farther) */
static void cyv_at(float x, float y, float z, float rot, float *sx, float *sy, float *d)
{
    const float ct = 0.8525245f, st = 0.5226872f;   /* cos and sin of the tilt, 0.55 rad */
    float c = fm_cosf(rot), s = fm_sinf(rot), x1 = x * c + z * s, z1 = -x * s + z * c;
    *sx = CYV_CX + x1 * CYV_S, *sy = CYV_CY - (y * ct + z1 * st) * CYV_S, *d = -y * st + z1 * ct;
}

static void cyv_gem(float x, float y, float r, const uint16_t *t, float z)
{
    p3d_tri(x, y - r, x + r * 0.75f, y, x, y + r, z, t[1]);
    p3d_tri(x, y - r, x, y + r, x - r * 0.75f, y, z, t[2]);
}

/* the steps the points show: the loop (LEN), or in FREE the 48 steps of the window the step is in */
static uint32_t cyv_len(const q_cipher *c) { return c->len ? c->len : 48u; }

void cipher_frame(const q_ciview *v, uint32_t ms)
{
    const q_cipher *c = &v->c;
    uint32_t L = cyv_len(c), base = v->cur - v->cur % L, i;
    /* the cube sways about its isometric view rather than turning round: face on, it would read as a flat square */
    float rot = 0.785f + 0.42f * fm_sinf((float)(ms % 24000u) * (6.2831853f / 24000.0f)), ex[8], ey[8], ed[8], far = -9.0f;
    int thr = (c->dens * 8 + 63) / 127;
    cyv.ms = ms;
    p3d_begin();
    for (i = 0; i < 8; i++) {                   /* the cube's corners; the farthest one's three edges go under */
        cyv_at(i & 1 ? 1.0f : -1.0f, i & 2 ? 1.0f : -1.0f, i & 4 ? 1.0f : -1.0f, rot, &ex[i], &ey[i], &ed[i]);
        if (ed[i] > far) far = ed[i];
    }
    for (i = 0; i < 8; i++)
        for (uint32_t b = 1; b < 8; b <<= 1)
            if (!(i & b)) {
                int back = ed[i] == far || ed[i | b] == far;
                p3d_line(ex[i], ey[i], ex[i | b], ey[i | b], back ? -10.0f : 30.0f, 1, back ? CYV_BACK : CYV_FRONT);
            }
    float tx[CYV_TRAIL + 1], ty[CYV_TRAIL + 1];
    int nt = 0;
    for (i = 0; i < L; i++) {                   /* the points: nearer ones over farther ones */
        uint32_t s = base + i;
        int b0 = gen_cipher_byte(c, s), b1 = gen_cipher_byte(c, s + 1), b2 = gen_cipher_byte(c, s + 2), on = b0 >> 5 < thr;
        float x, y, d;
        cyv_at((float)b0 / 127.5f - 1.0f, (float)b1 / 127.5f - 1.0f, (float)b2 / 127.5f - 1.0f, rot, &x, &y, &d);
        int cur = s == v->cur, hot = cur && on && v->playing;
        cyv_gem(x, y, cur ? 6.5f : 3.6f, hot ? PINK3 : on ? CYV_CYAN : CYV_MUTE, 10.0f - d);
        if (cur) {                              /* the step playing, framed */
            float r = 10.5f;
            uint16_t f = hot ? PINK3[1] : MB_WHITE[1];
            p3d_line(x, y - r, x + r, y, 20.0f, 1, f), p3d_line(x + r, y, x, y + r, 20.0f, 1, f);
            p3d_line(x, y + r, x - r, y, 20.0f, 1, f), p3d_line(x - r, y, x, y - r, 20.0f, 1, f);
        }
        if (s + CYV_TRAIL >= v->cur && s <= v->cur && v->playing)
            tx[nt] = x, ty[nt] = y, nt++;
    }
    for (int k = 1; k < nt; k++)                /* the trail to the step playing, brighter towards it */
        p3d_line(tx[k - 1], ty[k - 1], tx[k], ty[k], 15.0f, 1, k > nt - 3 ? PINK3[1] : PINK3[2]);
}

void cipher_band(const q_ciview *v, int32_t oy)
{
    const q_cipher *c = &v->c;
    char b[48];
    int val = gen_cipher_byte(c, v->cur), thr = (c->dens * 8 + 63) / 127, on = v->playing && val >> 5 < thr, i;
    cv_rect(0, oy, 240, 166, RGB(0, 0, 0));
    p3d_draw(oy);
    cyv_formula(c, b, b + sizeof b);            /* the formula and t, on top */
    const felucca_font_t *f = text_w(&FONT_S, b) <= 232 ? &FONT_S : &FONT_XS;
    glo_text(120, oy + 9, f, b, KGREEN[1], 0);
    b[0] = 't', b[1] = ' ', b[2] = '=', b[3] = ' ';
    cyv_num((uint32_t)c->start * 256u + v->cur * c->speed, b + 4);
    glo_text(120, oy + 22, &FONT_XS, b, CYV_LABEL, 0);
    for (i = 7; i >= 0; i--) {                  /* the byte's bits, bit 7 on top: the rhythm, the accent, the note */
        int32_t y = oy + 40 + (7 - i) * 11, lit = val >> i & 1;
        const uint16_t *t = i >= 5 ? ORANGE : i == 4 ? CYV_YEL : CYV_CYAN;
        uint16_t col = lit ? t[on ? 1 : 2] : CYV_OFF;
        p3d_fill(10.0f, (float)y, 26.0f, (float)y, 29.0f, (float)(y + 8), col);
        p3d_fill(10.0f, (float)y, 29.0f, (float)(y + 8), 13.0f, (float)(y + 8), col);
        glo_text(33, y + 4, &FONT_XS, lit ? "1" : "0", lit ? C_WHITE : CYV_DIM, 2);
    }
    b[0] = 'v', b[1] = ' ';
    cyv_num((uint32_t)val, b + 2);
    glo_text(10, oy + 136, &FONT_XS, b, C_WHITE, 2);
    int32_t x = 214;                            /* the rhythm level against DENS's line, the accent, the note */
    glo_text(x, oy + 36, &FONT_XS, "RHY", CYV_LABEL, 2);
    for (i = 0; i < 8; i++)
        cv_rect(x, oy + 44 + (7 - i) * 7, 16, 5, i == val >> 5 ? (on ? PINK3[1] : ORANGE[1]) : i < thr ? ORANGE[2] : CYV_OFF);
    cv_rect(x - 3, oy + 43 + (8 - thr) * 7, 22, 1, ORANGE[1]);
    glo_text(x, oy + 108, &FONT_XS, "ACC", CYV_LABEL, 2);
    uint16_t a = val >> 4 & 1 ? CYV_YEL[1] : CYV_OFF;
    p3d_fill((float)(x + 8), (float)(oy + 114), (float)(x + 14), (float)(oy + 120), (float)(x + 2), (float)(oy + 120), a);
    p3d_fill((float)(x + 14), (float)(oy + 120), (float)(x + 8), (float)(oy + 126), (float)(x + 2), (float)(oy + 120), a);
    glo_text(x, oy + 136, &FONT_XS, v->kit ? "PAD" : "NOTE", CYV_LABEL, 2);
    glo_text(x, oy + 149, &FONT_XS, on ? v->name : "--", on ? PINK3[1] : CYV_DIM, 2);
}
