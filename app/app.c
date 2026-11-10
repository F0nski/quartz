/* QUARTZ: a black screen with the name, and silence. Part of the unity build (device/unity.c, host/plat_host.c),
 * after the platform: gfx.c is in scope. */
static uint8_t booted;

void app_init(void) { booted = 1; }

void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    uint32_t i;
    (void)master_q12;
    for (i = 0; i < 2u * n; i++)
        lr[i] = 0;
}

void app_panic(void) {}

void app_ui_init(void)
{
    lcd_fill(0, 0, 240, 240, C_BLACK);
    cv_begin(240, 32, C_BLACK);
    cv_text(10, 6, &FONT_B, APP_NAME, C_WHITE);
    cv_blit(0, 0);
}

void app_frame(void) {}
void app_input_only(void) {}
void app_say(const char *a, const char *b) { (void)a; (void)b; }

#ifdef PLAT_HOST
int app_expect(const char *what, int *got)
{
    if (!strcmp(what, "booted"))
        *got = booted;
    else
        return 0;
    return 1;
}
#endif
