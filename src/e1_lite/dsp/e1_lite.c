/*
 * e1_lite.c — E-1 Lite: one E-1 track as a Schwung MIDI FX.
 *
 * Put it in a chain slot in front of any synth. It follows Move's transport
 * and plays one Euclidean track into that slot. Same engine and the same
 * parameter keys as the E-1 tool, so a track sounds the same in both.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */
#include <stdlib.h>
#include <string.h>

#include "plugin_api_v1.h"
#include "midi_fx_api_v1.h"
#include "../../engine/e1_engine.h"

#define PENDING 64

typedef struct {
    e1_engine_t e;
    uint8_t pend[PENDING][3];
    int npend;
} lite_t;

static const host_api_v1_t *g_host = NULL;

static void emit(void *user, int ports, uint8_t st, uint8_t d1, uint8_t d2)
{
    (void)ports;
    lite_t *s = (lite_t *)user;
    if (s->npend >= PENDING) return;
    s->pend[s->npend][0] = st; s->pend[s->npend][1] = d1; s->pend[s->npend][2] = d2;
    s->npend++;
}

static void *lite_create(const char *dir, const char *cfg)
{
    (void)dir; (void)cfg;
    lite_t *s = (lite_t *)calloc(1, sizeof(lite_t));
    if (!s) return NULL;
    e1_init(&s->e);
    /* One track, unmuted, on the slot's own channel. */
    e1_track_defaults(&s->e.t[0], 0);
    s->e.t[0].pulses = 4;
    for (int i = 1; i < E1_TRACKS; i++) s->e.t[i].muted = 1, s->e.t[i].pulses = 0;
    return s;
}

static void lite_destroy(void *inst) { free(inst); }

/* Incoming MIDI passes through untouched. */
static int lite_process(void *inst, const uint8_t *in, int len, uint8_t out[][3], int lens[], int max)
{
    (void)inst;
    if (max < 1 || len < 1) return 0;
    out[0][0] = in[0]; out[0][1] = len > 1 ? in[1] : 0; out[0][2] = len > 2 ? in[2] : 0;
    lens[0] = len > 3 ? 3 : len;
    return 1;
}

static int lite_tick(void *inst, int frames, int sr, uint8_t out[][3], int lens[], int max)
{
    (void)frames; (void)sr;
    lite_t *s = (lite_t *)inst;
    if (!s) return 0;
    double beat = (g_host && g_host->get_beat_position) ? g_host->get_beat_position() : -1.0;
    if (beat >= 0.0) {
        double tick = beat * E1_PPQN;
        if (!s->e.running) e1_join(&s->e, 0.0, tick);
        e1_advance(&s->e, tick, emit, s);
    } else if (s->e.running) {
        e1_stop(&s->e, emit, s);
    } else {
        e1_advance(&s->e, s->e.tick, emit, s);
    }
    int n = s->npend < max ? s->npend : max;
    for (int i = 0; i < n; i++) {
        memcpy(out[i], s->pend[i], 3);
        lens[i] = 3;
    }
    /* Anything over the limit goes out next block. */
    memmove(s->pend, s->pend + n, (size_t)(s->npend - n) * 3);
    s->npend -= n;
    return n;
}

static void lite_set(void *inst, const char *key, const char *val)
{
    lite_t *s = (lite_t *)inst;
    if (!s || !key || !val) return;
    if (strcmp(key, "state") == 0) { e1_load_lines(&s->e, 0, val); return; }
    if (strcmp(key, "type") == 0 || strcmp(key, "mute") == 0) {
        if (s->e.t[0].gate_open) e1_all_notes_off(&s->e, emit, s);
    }
    if (strcmp(key, "seed") == 0) { e1_set(&s->e, -1, key, val); return; }
    e1_set(&s->e, 0, key, val);
}

static int lite_get(void *inst, const char *key, char *buf, int len)
{
    lite_t *s = (lite_t *)inst;
    if (!s || !key || !buf || len < 2) return -1;
    if (strcmp(key, "state") == 0) return e1_dump_track(&s->e, 0, buf, len);
    if (strcmp(key, "seed") == 0) return e1_get(&s->e, -1, key, buf, len);
    return e1_get(&s->e, 0, key, buf, len);
}

static midi_fx_api_v1_t g_api = {
    .api_version = MIDI_FX_API_VERSION,
    .create_instance = lite_create,
    .destroy_instance = lite_destroy,
    .process_midi = lite_process,
    .tick = lite_tick,
    .set_param = lite_set,
    .get_param = lite_get,
};

__attribute__((visibility("default")))
midi_fx_api_v1_t *move_midi_fx_init(const struct host_api_v1 *host)
{
    g_host = host;
    return &g_api;
}
