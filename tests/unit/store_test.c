#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "seq.c"
#include "../../app/app.h"
#include "store.c"

/* doubles: one in-memory object and the playing state */
static uint8_t mem[PLAT_STORE_MAX];
static int mem_len = -1;
static int saves;
static int playing;
int audio_playing(void) { return playing; }
int plat_store_size(uint32_t obj) { assert(obj == OBJ_PROJECT); return mem_len; }
int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    assert(obj == OBJ_PROJECT);
    if (mem_len < 0 || (uint32_t)mem_len > max) return -1;
    memcpy(dst, mem, (size_t)mem_len);
    return mem_len;
}
int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    assert(obj == OBJ_PROJECT && len <= sizeof mem);
    memcpy(mem, src, len);
    mem_len = (int)len;
    saves++;
    return 0;
}

static void roundtrip(void)
{
    q_project a, b;
    project_defaults(&a);
    a.tempo = 97;
    a.sound[2].model = 13;
    a.pat[0].steps[5].on = 1;
    a.pat[0].steps[5].note = 67;
    assert(store_save(&a) == 0);
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == 0);
    assert(!memcmp(&a, &b, sizeof a));
}

static void store_rejects_bad_object(void)
{
    q_project a, d, b;
    project_defaults(&d);
    project_defaults(&a);
    a.tempo = 97;
    store_save(&a);
    int good = mem_len;

    mem_len = -1;                                   /* absent */
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == -1 && !memcmp(&b, &d, sizeof d));

    mem_len = good - 10;                            /* truncated */
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == -1 && !memcmp(&b, &d, sizeof d));

    mem_len = good;                                 /* version 2 */
    mem[0] = 2;
    memset(&b, 0xAA, sizeof b);
    assert(store_load(&b) == -1 && !memcmp(&b, &d, sizeof d));
}

static void no_save_while_playing(void)
{
    q_project a;
    project_defaults(&a);
    playing = 1;
    saves = 0;
    assert(store_save(&a) == -1);
    assert(saves == 0);
    playing = 0;
}

int main(void)
{
    roundtrip();
    store_rejects_bad_object();
    no_save_while_playing();
    puts("store ok");
    return 0;
}
