/* The projects in flash: a q_project's bytes cut into Q_CHUNKS store objects (pooled, app.h), each behind a header
 * naming the save it belongs to. Loading wants every chunk, of one save: a project is never half old, half new.
 * store_project picks which of the Q_PROJECTS a load or a save is; the gens count the saves of all of them.
 * Part of the unity build (app.c). */
#include "audio.h"

#define CHUNK_DATA (PLAT_STORE_MAX - 8u)             /* payload per chunk, after the header */
#define CHUNK_MAGIC 0x515Au                          /* "QZ" */
typedef struct { uint16_t magic; uint8_t version, index; uint32_t gen; } chunk_hdr;
_Static_assert(sizeof(chunk_hdr) == 8, "chunk header");
_Static_assert(sizeof(q_project) <= Q_CHUNKS * CHUNK_DATA, "q_project must fit its chunks");

uint8_t chunk_buf[PLAT_STORE_MAX] __attribute__((aligned(4)));   /* one chunk: no q_project on the stack; lent to a
                                                                    restore (app.h APP_BACKUP_BUF) */
_Static_assert(APP_BACKUP_VERSION == Q_VERSION, "a backup's version is the project's layout");
_Static_assert(sizeof (uint16_t[])APP_BACKUP_OBJS == Q_PROJECTS * Q_CHUNKS * sizeof(uint16_t), "a backup holds every chunk");
static uint32_t save_gen;                            /* the highest chunk 0's gen read or written: a save goes past it */
int store_project;                                   /* the project loaded and saved, 0..Q_PROJECTS - 1 */

static uint32_t chunk_obj(int i) { return OBJ_CHUNK0 + (uint32_t)(store_project * Q_CHUNKS + i); }

static uint32_t chunk_len(int i)                     /* payload bytes of chunk i */
{
    uint32_t off = (uint32_t)i * CHUNK_DATA;
    return sizeof(q_project) - off < CHUNK_DATA ? (uint32_t)sizeof(q_project) - off : CHUNK_DATA;
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* each LK_* lock's value range; a negative lo is a signed field, stored as int8_t */
static const int8_t lk_range[LK_N][2] = {
    [LK_HARM] = {0, 127}, [LK_TIMB] = {0, 127}, [LK_MORPH] = {0, 127}, [LK_DECAY] = {0, 127}, [LK_TUNE] = {-24, 24},
    [LK_GLIDE] = {0, 127}, [LK_ATK] = {0, 127}, [LK_DEC] = {0, 127}, [LK_SUS] = {0, 127}, [LK_REL] = {0, 127},
    [LK_FTYPE] = {0, FT_BP}, [LK_CUT] = {0, 127}, [LK_RES] = {0, 127}, [LK_ENVAMT] = {-64, 63},
    [LK_SHAPE] = {0, LS_SH}, [LK_RATE] = {0, 75}, [LK_DEPTH] = {-64, 63}, [LK_DEST] = {0, LD_LEVEL},
    [LK_AMODE] = {0, AM_ORDER}, [LK_AOCT] = {1, 4}, [LK_ADIV] = {0, DIV_N - 1}, [LK_AGATE] = {5, 100},
    [LK_I1] = {0, 127}, [LK_I2] = {0, 127}, [LK_I3] = {0, 127}, [LK_IMIX] = {0, 127}, [LK_DSEND] = {0, 127},
    [LK_RSEND] = {0, 127}, [LK_PAN] = {-64, 63}, [LK_LEVEL] = {0, 127},
    [LK_SHAPE2] = {0, LS_SH}, [LK_RATE2] = {0, 75}, [LK_DEPTH2] = {-64, 63}, [LK_DEST2] = {0, LD_LEVEL},
    [LK_PTUNE] = {-24, 24}, [LK_PHARM] = {0, 127}, [LK_PTIMB] = {0, 127}, [LK_PDECAY] = {0, 127} };

/* version 18 (0.9), loaded and brought to this one: the same layout, its punch-in locks right after LK_DEST2, before
   the pads' locks */
#define V_MIGRATED 18
#define V18_PUNCH_LO (LK_DEST2 + 1)
static void migrate_18(q_project *p)
{
    for (int t = 0; t < Q_TRACKS; t++)
        for (int n = 0; n < Q_PATTERNS; n++)
            for (int k = 0; k < Q_LOCKS; k++) {
                q_lock *l = &p->pat[t][n].locks[k];
                if (l->param >= V18_PUNCH_LO && l->param <= V18_PUNCH_LO + 1)
                    l->param = (uint8_t)(l->param - V18_PUNCH_LO + LK_PUNCH_LO);
            }
    p->version = Q_VERSION;
}

/* a valid-CRC object can still hold out-of-range fields: the render indexes and divides with them */
static void clamp_project(q_project *p)
{
    p->tempo = (uint16_t)clampi(p->tempo, 40, 240);
    p->swing = (uint8_t)clampi(p->swing, 50, 75);
    p->tonic = (uint8_t)clampi(p->tonic, 0, 11);
    p->mode = (uint8_t)clampi(p->mode, 0, 10);
    p->quant = (uint8_t)clampi(p->quant, 0, 2);
    p->click = (uint8_t)clampi(p->click, 0, 3);
    p->keys = (uint8_t)clampi(p->keys, 0, KEYS_ALL);
    p->nsong = (uint8_t)clampi(p->nsong, 0, Q_SONG);
    p->dtime = (uint8_t)clampi(p->dtime, 0, DIV_N - 1);
    p->dfb = (uint8_t)clampi(p->dfb, 0, 127); p->rsize = (uint8_t)clampi(p->rsize, 0, 127);
    p->rtone = (uint8_t)clampi(p->rtone, 0, 127); p->comp = (uint8_t)clampi(p->comp, 0, 127);
    p->duck = (uint8_t)clampi(p->duck, 0, 127);
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
        s->atk = (uint8_t)clampi(s->atk, 0, 127); s->dec = (uint8_t)clampi(s->dec, 0, 127);
        s->sus = (uint8_t)clampi(s->sus, 0, 127); s->rel = (uint8_t)clampi(s->rel, 0, 127);
        s->ftype = (uint8_t)clampi(s->ftype, 0, FT_BP);
        s->cutoff = (uint8_t)clampi(s->cutoff, 0, 127); s->res = (uint8_t)clampi(s->res, 0, 127);
        s->envamt = (int8_t)clampi(s->envamt, -64, 63);
        s->lshape = (uint8_t)clampi(s->lshape, 0, LS_SH); s->ltrig = (uint8_t)clampi(s->ltrig, 0, 1);
        s->lrate = (uint8_t)clampi(s->lrate, 0, 75); s->ldepth = (int8_t)clampi(s->ldepth, -64, 63);
        s->ldest = (uint8_t)clampi(s->ldest, 0, LD_LEVEL); s->amode = (uint8_t)clampi(s->amode, 0, AM_ORDER);
        s->l2shape = (uint8_t)clampi(s->l2shape, 0, LS_SH); s->l2trig = (uint8_t)clampi(s->l2trig, 0, 1);
        s->l2rate = (uint8_t)clampi(s->l2rate, 0, 75); s->l2depth = (int8_t)clampi(s->l2depth, -64, 63);
        s->l2dest = (uint8_t)clampi(s->l2dest, 0, LD_LEVEL);
        s->aoct = (uint8_t)clampi(s->aoct, 1, 4); s->adiv = (uint8_t)clampi(s->adiv, 0, DIV_N - 1);
        s->agate = (uint8_t)clampi(s->agate, 5, 100);
        s->itype = (uint8_t)clampi(s->itype, 0, IT_N - 1);
        s->i1 = (uint8_t)clampi(s->i1, 0, 127); s->i2 = (uint8_t)clampi(s->i2, 0, 127);
        s->i3 = (uint8_t)clampi(s->i3, 0, 127); s->imix = (uint8_t)clampi(s->imix, 0, 127);
        s->dsend = (uint8_t)clampi(s->dsend, 0, 127); s->rsend = (uint8_t)clampi(s->rsend, 0, 127);
        s->pan = (int8_t)clampi(s->pan, -64, 63);
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
            g->len = (uint8_t)clampi(g->len, 1, GRIDS_LEN);
            for (int j = 0; j < 3; j++) g->dens[j] = (uint8_t)clampi(g->dens[j], 0, 127);
            for (int j = 0; j < 3; j++) g->pad[j] = (uint8_t)clampi(g->pad[j], 0, Q_PADS - 1);
            m->tout = (uint8_t)clampi(m->tout, 1, 3); m->len = (uint8_t)clampi(m->len, 1, 16);
            m->mode = (uint8_t)clampi(m->mode, 0, 6); m->range = (uint8_t)clampi(m->range, 1, 4);
            m->gate = (uint8_t)clampi(m->gate, 0, 5);
            m->bias = (uint8_t)clampi(m->bias, 0, 127); m->jitter = (uint8_t)clampi(m->jitter, 0, 127);
            m->dejavu = (uint8_t)clampi(m->dejavu, 0, 127); m->spread = (uint8_t)clampi(m->spread, 0, 127);
            m->xbias = (uint8_t)clampi(m->xbias, 0, 127); m->steps = (uint8_t)clampi(m->steps, 0, 127);
            m->note = (uint8_t)clampi(m->note, 1, 127);
            q_life *l = &q->life;
            l->poly = (uint8_t)clampi(l->poly, 1, 4); l->note = (uint8_t)clampi(l->note, 1, 127);
            l->map = (uint8_t)clampi(l->map, 0, 1); l->seed = (uint8_t)clampi(l->seed, 0, 127);
            l->dens = (uint8_t)clampi(l->dens, 0, 127); l->autos = (uint8_t)clampi(l->autos, 0, 1);
            l->gen = (uint8_t)clampi(l->gen, 0, 3); l->mut = (uint8_t)clampi(l->mut, 0, 127);
            q_orbits *ob = &q->orbits;
            ob->note = (uint8_t)clampi(ob->note, 1, 127); ob->moons = (uint8_t)clampi(ob->moons, 1, 4);
            for (int m = 0; m < 4; m++) {
                ob->per[m] = (uint8_t)clampi(ob->per[m], 1, 32); ob->pha[m] = (uint8_t)clampi(ob->pha[m], 0, 31);
                ob->val[m] = (uint8_t)clampi(ob->val[m], 0, 15);
            }
            q_cipher *cy = &q->cipher;
            cy->formula = (uint8_t)clampi(cy->formula, 0, CIPHER_N - 1);
            cy->a = (uint8_t)clampi(cy->a, 0, 15); cy->b = (uint8_t)clampi(cy->b, 0, 15); cy->c = (uint8_t)clampi(cy->c, 0, 15);
            cy->dens = (uint8_t)clampi(cy->dens, 0, 127); cy->len = (uint8_t)clampi(cy->len, 0, 64);
            cy->start = (uint8_t)clampi(cy->start, 0, 127); cy->speed = (uint8_t)clampi(cy->speed, 1, 255);
            cy->note = (uint8_t)clampi(cy->note, 1, 127); cy->range = (uint8_t)clampi(cy->range, 1, 4);
            q_mimic *mi = &q->mimic;
            mi->n = (uint8_t)clampi(mi->n, 0, MIMIC_EV); mi->order = (uint8_t)clampi(mi->order, 1, 2);
            mi->drift = (uint8_t)clampi(mi->drift, 0, 127); mi->loop = (uint8_t)clampi(mi->loop, 0, 16);
            for (int e = 0; e < mi->n; e++) {   /* (the slots past n: never read) */
                if (!mi->ev[e][0] || mi->ev[e][0] > 127) mi->ev[e][0] = 60;
                mi->ev[e][1] = (uint8_t)clampi(mi->ev[e][1], 0, 7);
            }
            q_tonnetz *tz = &q->tonnetz;
            tz->note = (uint8_t)clampi(tz->note, 1, 127); tz->len = (uint8_t)clampi(tz->len, 1, 8);
            tz->rate = (uint8_t)clampi(tz->rate, 0, 4); tz->strum = (uint8_t)clampi(tz->strum, 0, 7);
            tz->voice = (uint8_t)clampi(tz->voice, 0, 1); tz->chaos = (uint8_t)clampi(tz->chaos, 0, 127);
            tz->scale = (uint8_t)clampi(tz->scale, 0, 1); tz->ret = (uint8_t)clampi(tz->ret, 0, 1);
            q_chords *ch = &q->chords;
            ch->note = (uint8_t)clampi(ch->note, 1, 127); ch->len = (uint8_t)clampi(ch->len, 1, 8);
            ch->rate = (uint8_t)clampi(ch->rate, 0, 4); ch->strum = (uint8_t)clampi(ch->strum, 0, 7);
            ch->voice = (uint8_t)clampi(ch->voice, 0, 1);
            for (int c = 0; c < 8; c++) ch->ch[c] = CH_MAKE(clampi(CH_DEG(ch->ch[c]), 0, 6), CH_SIZE(ch->ch[c]));
            q_lanes *ln = &q->lanes;
            static const uint8_t LMAX[L_N] = { 1, 15, 4, 4, 3 };
            ln->note = (uint8_t)clampi(ln->note, 1, 127);
            for (int j = 0; j < L_N; j++) {
                for (int c = 0; c < 16; c++)         /* (a length, a nibble, is always 1..16) */
                    lanes_set(ln, j, c, clampi(lanes_get(ln, j, c), 0, LMAX[j]));
            }
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
                q->locks[k].param = (uint8_t)clampi(q->locks[k].param, 0, LK_PUNCH_HI);
                if (q->locks[k].param >= LK_N)
                    continue;               /* a punch-in mask: any byte */
                const int8_t *r = lk_range[q->locks[k].param];
                q->locks[k].value = (uint8_t)clampi(r[0] < 0 ? (int8_t)q->locks[k].value : q->locks[k].value, r[0], r[1]);
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
        if (plat_store_size(chunk_obj(i)) != (int)(sizeof h + n)) return -1;   /* (plat_store_load cuts a longer one) */
        if (plat_store_load(chunk_obj(i), chunk_buf, sizeof h + n) != (int)(sizeof h + n)) return -1;
        __builtin_memcpy(&h, chunk_buf, sizeof h);
        if (i == 0 && (gen = h.gen) > save_gen)   /* written first: a failed load still saves past it */
            save_gen = gen;
        if (h.magic != CHUNK_MAGIC || (h.version != Q_VERSION && h.version != V_MIGRATED) || h.index != i || h.gen != gen) return -1;
        __builtin_memcpy((uint8_t *)p + (uint32_t)i * CHUNK_DATA, chunk_buf + sizeof h, n);
    }
    return 0;
}

/* the project saved last, 0 when none is of this version (or V_MIGRATED); and save_gen past every project's */
int store_last(void)
{
    int n, last = 0;
    uint32_t best = 0;
    chunk_hdr h;
    for (n = 0; n < Q_PROJECTS; n++) {
        if (plat_store_load(OBJ_CHUNK0 + (uint32_t)(n * Q_CHUNKS), chunk_buf, sizeof h) != (int)sizeof h)
            continue;
        __builtin_memcpy(&h, chunk_buf, sizeof h);
        if (h.magic != CHUNK_MAGIC || (h.version != Q_VERSION && h.version != V_MIGRATED))
            continue;
        if (h.gen > best)
            best = h.gen, last = n;
        if (h.gen > save_gen)
            save_gen = h.gen;
    }
    return last;
}

int store_load(q_project *p)
{
    if (load_chunks(p) == 0 && (p->version == Q_VERSION || p->version == V_MIGRATED)) {
        if (p->version == V_MIGRATED)
            migrate_18(p);
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
        int r = plat_store_save(chunk_obj(i), chunk_buf, sizeof h + n);
        if (r) return r;
    }
    return 0;
}
