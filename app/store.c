/* The project in flash: the q_project's bytes cut into Q_CHUNKS store objects (pooled, app.h), each behind a header
 * naming the save it belongs to. Loading wants every chunk, of one save: a project is never half old, half new.
 * Part of the unity build (app.c). */
#include "audio.h"

#define CHUNK_DATA (PLAT_STORE_MAX - 8u)             /* payload per chunk, after the header */
#define CHUNK_MAGIC 0x515Au                          /* "QZ" */
typedef struct { uint16_t magic; uint8_t version, index; uint32_t gen; } chunk_hdr;
_Static_assert(sizeof(chunk_hdr) == 8, "chunk header");
_Static_assert(sizeof(q_project) <= Q_CHUNKS * CHUNK_DATA, "q_project must fit its chunks");

static uint8_t chunk_buf[PLAT_STORE_MAX] __attribute__((aligned(4)));   /* one chunk: no q_project on the stack */
static uint32_t save_gen;                            /* chunk 0's last read or written: the highest gen */

static uint32_t chunk_len(int i)                     /* payload bytes of chunk i */
{
    uint32_t off = (uint32_t)i * CHUNK_DATA;
    return sizeof(q_project) - off < CHUNK_DATA ? (uint32_t)sizeof(q_project) - off : CHUNK_DATA;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a valid-CRC object can still hold out-of-range fields: the render indexes and divides with them */
static void clamp_project(q_project *p)
{
    p->tempo = (uint16_t)clampi(p->tempo, 40, 240);
    p->swing = (uint8_t)clampi(p->swing, 50, 75);
    p->tonic = (uint8_t)clampi(p->tonic, 0, 11);
    p->mode = (uint8_t)clampi(p->mode, 0, 10);
    p->quant = (uint8_t)clampi(p->quant, 0, 2);
    p->click = (uint8_t)clampi(p->click, 0, 3);
    p->nsong = (uint8_t)clampi(p->nsong, 0, Q_SONG);
    for (int i = 0; i < Q_SCENES; i++)
        for (int t = 0; t < Q_TRACKS; t++)
            p->scene[i].pat[t] = (uint8_t)clampi(p->scene[i].pat[t], 0, Q_PATTERNS - 1);
    for (int i = 0; i < Q_SONG; i++) {
        p->song[i].scene = (uint8_t)clampi(p->song[i].scene, 0, Q_SCENES - 1);
        p->song[i].reps = (uint8_t)clampi(p->song[i].reps, 1, 16);
    }
    for (int i = 0; i < Q_TRACKS; i++) {
        q_sound *s = &p->sound[i];
        s->model = (uint8_t)clampi(s->model, 0, M_KIT);
        s->poly = (uint8_t)clampi(s->poly, 1, 4);
        s->tune = (int8_t)clampi(s->tune, -24, 24);
        s->glide = (uint8_t)clampi(s->glide, 0, 127);
        s->level = (uint8_t)clampi(s->level, 0, 127);
        s->harm = (uint8_t)clampi(s->harm, 0, 127);
        s->timb = (uint8_t)clampi(s->timb, 0, 127);
        s->morph = (uint8_t)clampi(s->morph, 0, 127);
        s->decay = (uint8_t)clampi(s->decay, 0, 127);
        p->pattern[i] = (uint8_t)clampi(p->pattern[i], 0, Q_PATTERNS - 1);
        for (int k = 0; k < Q_PADS; k++) {
            q_pad *pd = &p->kit[i][k];
            pd->engine = (uint8_t)clampi(pd->engine, 0, 2);
            pd->tune = (int8_t)clampi(pd->tune, -24, 24);
            pd->harm = (uint8_t)clampi(pd->harm, 0, 127);
            pd->timb = (uint8_t)clampi(pd->timb, 0, 127);
            pd->decay = (uint8_t)clampi(pd->decay, 0, 127);
            pd->level = (uint8_t)clampi(pd->level, 0, 127);
        }
        for (int n = 0; n < Q_PATTERNS; n++) {
            q_pattern *q = &p->pat[i][n];
            q->len = (uint8_t)clampi(q->len, 1, Q_STEPS);
            q->div = (uint8_t)clampi(q->div, 0, DIV_N - 1);
            q->nlocks = (uint8_t)clampi(q->nlocks, 0, Q_LOCKS);
            q->path = (uint8_t)clampi(q->path, 0, PATH_N - 1);
            q->xlen = (uint8_t)clampi(q->xlen, 1, 4);
            q->ylen = (uint8_t)clampi(q->ylen, 1, 4);
            q->ydiv = (uint8_t)clampi(q->ydiv, 0, DIV_N - 1);
            q->src = (uint8_t)clampi(q->src, 0, SRC_N - 1);
            q_grids *g = &q->grids;
            q_marbles *m = &q->marbles;
            g->x = (uint8_t)clampi(g->x, 0, 127); g->y = (uint8_t)clampi(g->y, 0, 127);
            g->chaos = (uint8_t)clampi(g->chaos, 0, 127); g->note = (uint8_t)clampi(g->note, 1, 127);
            g->voice = (uint8_t)clampi(g->voice, 1, 3);
            for (int j = 0; j < 3; j++) g->dens[j] = (uint8_t)clampi(g->dens[j], 0, 127);
            m->tout = (uint8_t)clampi(m->tout, 1, 3); m->len = (uint8_t)clampi(m->len, 1, 16);
            m->mode = (uint8_t)clampi(m->mode, 0, 6); m->range = (uint8_t)clampi(m->range, 1, 4);
            m->bias = (uint8_t)clampi(m->bias, 0, 127); m->jitter = (uint8_t)clampi(m->jitter, 0, 127);
            m->dejavu = (uint8_t)clampi(m->dejavu, 0, 127); m->spread = (uint8_t)clampi(m->spread, 0, 127);
            m->xbias = (uint8_t)clampi(m->xbias, 0, 127); m->steps = (uint8_t)clampi(m->steps, 0, 127);
            m->note = (uint8_t)clampi(m->note, 1, 127);
            for (int k = 0; k < Q_STEPS; k++) {
                q_step *st = &q->steps[k];
                for (int j = 0; j < 4; j++) st->note[j] = (uint8_t)clampi(st->note[j], 0, 127);
                st->vel = (uint8_t)clampi(st->vel, 1, 127);
                st->len = (uint8_t)clampi(st->len, 1, 64);
                st->prob = (uint8_t)clampi(st->prob, 0, 20);
                st->micro = (int8_t)clampi(st->micro, -11, 11);
            }
            for (int k = 0; k < Q_LOCKS; k++) {
                q->locks[k].step = (uint8_t)clampi(q->locks[k].step, 0, Q_STEPS - 1);
                q->locks[k].param = (uint8_t)clampi(q->locks[k].param, 0, LK_N - 1);
            }
        }
    }
}

/* chunks straight into *p: a failure leaves it half written, so the caller falls back to the defaults */
static int load_chunks(q_project *p)
{
    uint32_t gen = 0;
    for (int i = 0; i < Q_CHUNKS; i++) {
        chunk_hdr h;
        uint32_t n = chunk_len(i);
        if (plat_store_size(OBJ_CHUNK0 + i) != (int)(sizeof h + n)) return -1;   /* (plat_store_load cuts a longer one) */
        if (plat_store_load(OBJ_CHUNK0 + i, chunk_buf, sizeof h + n) != (int)(sizeof h + n)) return -1;
        __builtin_memcpy(&h, chunk_buf, sizeof h);
        if (i == 0) save_gen = gen = h.gen;      /* written first: a failed load still saves past it */
        if (h.magic != CHUNK_MAGIC || h.version != Q_VERSION || h.index != i || h.gen != gen) return -1;
        __builtin_memcpy((uint8_t *)p + (uint32_t)i * CHUNK_DATA, chunk_buf + sizeof h, n);
    }
    return 0;
}

int store_load(q_project *p)
{
    if (load_chunks(p) == 0 && p->version == Q_VERSION) {
        clamp_project(p);
        return 0;
    }
    project_defaults(p);
    return -1;
}

/* a flash write stalls the render: not while playing */
int store_save(const q_project *p)
{
    if (audio_playing()) return -1;
    save_gen++;
    for (int i = 0; i < Q_CHUNKS; i++) {
        chunk_hdr h = { CHUNK_MAGIC, Q_VERSION, (uint8_t)i, save_gen };
        uint32_t n = chunk_len(i);
        __builtin_memcpy(chunk_buf, &h, sizeof h);
        __builtin_memcpy(chunk_buf + sizeof h, (const uint8_t *)p + (uint32_t)i * CHUNK_DATA, n);
        int r = plat_store_save(OBJ_CHUNK0 + i, chunk_buf, sizeof h + n);
        if (r) return r;
    }
    return 0;
}
