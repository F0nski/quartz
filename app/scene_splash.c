/* The boot screen (docs/screens/splash.html, 1 "the crystal"): a cut quartz crystal on the night, its facets lit two
 * by two as the boot goes on, the name, the tagline, a thin bar and the step under way. Drawn once at boot
 * (app_splash, before app_init) and once a step (splash_step, from app_init): no animation, the boot is not slowed.
 * Part of the unity build after p3d.c (app_ui.c): p3d_fill draws into the band. */

#define BOOT_BAND 80                      /* the screen in three bands of 240 x 80 */
#define BOOT_NIGHT RGB(7, 4, 26)
#define BOOT_WIRE RGB(42, 29, 85)
#define BOOT_INK RGB(244, 241, 255)
#define BOOT_MUTE RGB(139, 132, 168)

static const char *const BOOT_STEP[3] = {"PROJECT", "VOICES", "SCENES"};

/* a quad as two triangles, the band's top at oy */
static void boot_quad(const float p[4][2], float oy, uint16_t c)
{
    p3d_fill(p[0][0], p[0][1] - oy, p[1][0], p[1][1] - oy, p[2][0], p[2][1] - oy, c);
    p3d_fill(p[0][0], p[0][1] - oy, p[2][0], p[2][1] - oy, p[3][0], p[3][1] - oy, c);
}

static void boot_center(int32_t y, const felucca_font_t *f, const char *s, uint16_t c)
{
    cv_text((240 - text_w(f, s)) / 2, y, f, s, c);
}

static void boot_band(int n, int32_t oy)
{
    /* the crystal: three body faces and three tip facets, by their tones (lit, base, shade) */
    static const uint16_t BOOT_PINK[3] = {RGB(255, 184, 220), RGB(255, 47, 142), RGB(138, 16, 80)};
    static const uint16_t BOOT_VIO[3] = {RGB(201, 184, 255), RGB(122, 92, 255), RGB(58, 38, 144)};
    static const int8_t BOOT_STARS[8][2] = {{18, 22}, {60, 12}, {120, 6}, {170, 14}, {222, 64}, {34, 70}, {96, 40}, {200, 100}};
    const float cx = 120, top = 26, tip = 58, base = 128, w = 34, l = cx - w, r = cx + w, ml = cx - w * 0.42f,
                mr = cx + w * 0.42f;
    const float face[6][4][2] = {
        {{l, tip}, {ml, tip + 6}, {ml, base + 6}, {l, base}},
        {{ml, tip + 6}, {mr, tip + 6}, {mr, base + 6}, {ml, base + 6}},
        {{mr, tip + 6}, {r, tip}, {r, base}, {mr, base + 6}},
        {{l, tip}, {cx, top}, {cx, top}, {ml, tip + 6}},
        {{ml, tip + 6}, {cx, top}, {cx, top}, {mr, tip + 6}},
        {{mr, tip + 6}, {cx, top}, {cx, top}, {r, tip}},
    };
    static const uint8_t BOOT_PINKS = 0x1A;  /* the faces in pink: the front one and the two front tip facets */
    const float shadow[4][2] = {{l - 6, base + 10}, {r + 6, base + 10}, {r - 2, base + 14}, {l + 2, base + 14}};
    int i, lit = 2 * (n + 1);
    for (i = 0; i < 8; i++)
        cv_rect(BOOT_STARS[i][0], BOOT_STARS[i][1] - oy, 1, 1, BOOT_MUTE);
    for (i = 0; i < 6; i++) {
        const uint16_t *t = BOOT_PINKS >> i & 1 ? BOOT_PINK : BOOT_VIO;
        boot_quad(face[i], (float)oy, i >= lit ? t[2] : i % 3 == 1 ? t[0] : t[1]);
    }
    boot_quad(shadow, (float)oy, BOOT_WIRE);
    boot_center(156 - oy, &FONT_L, "QUARTZ", BOOT_INK);
    boot_center(188 - oy, &FONT_T, "A FOUR-TRACK GROOVEBOX", BOOT_MUTE);
    cv_rect(40, 212 - oy, 160, 2, BOOT_WIRE);
    cv_rect(40, 212 - oy, 160 * (n + 1) / 3, 2, BOOT_PINK[1]);
    boot_center(220 - oy, &FONT_T, BOOT_STEP[n], BOOT_MUTE);
}

/* the boot screen at step n: 0 the project loading, 1 the voices, 2 the screen */
static void splash_step(int n)
{
    int32_t oy;
    for (oy = 0; oy < 240; oy += BOOT_BAND) {
        cv_begin(240, BOOT_BAND, BOOT_NIGHT);
        boot_band(n, oy);
        cv_blit(0, (uint32_t)oy);
    }
}

void app_splash(void)
{
    boot_step = splash_step;
    splash_step(0);
}
