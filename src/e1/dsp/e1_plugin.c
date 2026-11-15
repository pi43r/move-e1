/*
 * e1_plugin.c — DSP side of the E-1 overtake tool (Schwung plugin API v2).
 *
 * Owns the clock, runs the shared engine once per audio block and sends its
 * events to Move (by channel, as if from a USB-A keyboard) and/or to the
 * computer over USB-C. The UI (ui.js) only edits parameters and reads one
 * compact snapshot ("ui") per frame.
 *
 * Every entry point here runs on the SPI audio callback: no allocation, no
 * file I/O, no logging. Saving and loading happen in ui.js, which reads
 * "dump:N" and writes "load:N".
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "plugin_api_v1.h"
#include "../../engine/e1_engine.h"

enum { CLOCK_MOVE = 0, CLOCK_INTERNAL = 1, CLOCK_COMPUTER = 2 };
static const char *const CLOCK_NAMES[] = { "Move", "Internal", "Computer" };

#define QSIZE 512                 /* packets per port, power of two */
#define MOVE_BUDGET 4             /* packets per block into Move */
#define COMPUTER_BUDGET 16        /* packets per block to USB-C */

typedef struct {
    uint8_t pkt[QSIZE][4];
    uint32_t head, tail;
    uint32_t dropped;
} queue_t;

typedef struct {
    e1_engine_t e;
    queue_t q_move, q_comp;

    int clock;                    /* CLOCK_* */
    float bpm;                    /* internal tempo */
    int internal_play;
    double itick;                 /* internal clock position, ticks */
    int clock_out;                /* send MIDI clock to the computer in Internal */
    double next_clock_tick;

    /* MIDI clock fallback when the host has no beat position */
    int ext_running;
    double ext_ticks;
    uint32_t ext_samples_since;

    int focus;
} e1_inst_t;

static const host_api_v1_t *g_host = NULL;

/* ------------------------------------------------------------- queues */

static void q_push(queue_t *q, uint8_t b0, uint8_t st, uint8_t d1, uint8_t d2)
{
    if (q->head - q->tail >= QSIZE) { q->dropped++; return; }
    uint8_t *p = q->pkt[q->head & (QSIZE - 1)];
    p[0] = b0; p[1] = st; p[2] = d1; p[3] = d2;
    q->head++;
}

static void q_flush(queue_t *q, int (*send)(const uint8_t *, int), int budget)
{
    if (!send) { q->tail = q->head; return; }
    while (budget-- > 0 && q->tail != q->head) {
        if (send(q->pkt[q->tail & (QSIZE - 1)], 4) <= 0) break;   /* full: retry next block */
        q->tail++;
    }
}

/* Channel messages travel as cable 2: Move routes them by channel to its
 * tracks and Schwung slots; on the external path they reach the USB-C
 * "Standalone Port" the computer sees. */
static void emit(void *user, int ports, uint8_t st, uint8_t d1, uint8_t d2)
{
    e1_inst_t *s = (e1_inst_t *)user;
    uint8_t b0 = (uint8_t)(0x20 | ((st >> 4) & 0x0F));
    if (ports & E1_PORT_MOVE) q_push(&s->q_move, b0, st, d1, d2);
    if (ports & E1_PORT_COMPUTER) q_push(&s->q_comp, b0, st, d1, d2);
}

static void realtime_out(e1_inst_t *s, uint8_t st)
{
    q_push(&s->q_comp, 0x2F, st, 0, 0);
}

static int computer_wanted(const e1_inst_t *s)
{
    if (s->e.dest != E1_DEST_MOVE) return 1;
    for (int i = 0; i < E1_TRACKS; i++)
        if (e1_ports(&s->e, &s->e.t[i]) & E1_PORT_COMPUTER) return 1;
    return 0;
}

/* ------------------------------------------------------------ lifecycle */

static void *e1_create(const char *module_dir, const char *json_defaults)
{
    (void)module_dir; (void)json_defaults;
    e1_inst_t *s = (e1_inst_t *)calloc(1, sizeof(e1_inst_t));
    if (!s) return NULL;
    e1_init(&s->e);
    s->clock = CLOCK_MOVE;
    s->bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
    if (!(s->bpm >= 20.0f && s->bpm <= 300.0f)) s->bpm = 120.0f;
    s->clock_out = 1;
    return s;
}

static void stop_all(e1_inst_t *s)
{
    if (s->e.running) e1_stop(&s->e, emit, s);
    else e1_all_notes_off(&s->e, emit, s);
}

static void e1_destroy(void *inst)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (!s) return;
    stop_all(s);
    if (s->clock == CLOCK_INTERNAL && s->internal_play && s->clock_out) realtime_out(s, 0xFC);
    /* Best effort: whatever fits now. */
    if (g_host) {
        q_flush(&s->q_move, g_host->midi_inject_to_move, 64);
        q_flush(&s->q_comp, g_host->midi_send_external, 64);
    }
    free(s);
}

/* ------------------------------------------------------------- MIDI in */

static void e1_on_midi(void *inst, const uint8_t *msg, int len, int source)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    (void)source;
    if (!s || !msg || len < 1) return;
    /* Only Move's transport matters here; it is the fallback for hosts
     * without get_beat_position. */
    switch (msg[0]) {
    case 0xFA: s->ext_running = 1; s->ext_ticks = 0; s->ext_samples_since = 0; break;
    case 0xFB: s->ext_running = 1; break;
    case 0xFC: s->ext_running = 0; break;
    case 0xF8: if (s->ext_running) { s->ext_ticks += E1_PPQN / 24; s->ext_samples_since = 0; } break;
    default: break;
    }
}

/* ------------------------------------------------------------- render */

static void run_follow(e1_inst_t *s, int frames)
{
    double beat = -1.0;
    int have_beat = g_host && g_host->get_beat_position;
    if (have_beat) beat = g_host->get_beat_position();

    double tick;
    int running;
    if (have_beat) {
        running = beat >= 0.0;
        tick = beat * E1_PPQN;
    } else {
        running = s->ext_running;
        s->ext_samples_since += (uint32_t)frames;
        float bpm = (g_host && g_host->get_bpm) ? g_host->get_bpm() : 120.0f;
        double interp = (double)s->ext_samples_since * bpm * E1_PPQN / (60.0 * MOVE_SAMPLE_RATE);
        if (interp > E1_PPQN / 24) interp = E1_PPQN / 24;
        tick = s->ext_ticks + interp;
    }

    if (running && !s->e.running) e1_join(&s->e, 0.0, tick);   /* bars line up with Move */
    else if (!running && s->e.running) e1_stop(&s->e, emit, s);
    e1_advance(&s->e, tick, emit, s);
}

static void run_internal(e1_inst_t *s, int frames)
{
    if (s->internal_play) {
        if (!s->e.running) {
            s->itick = 0.0;
            s->next_clock_tick = 0.0;
            e1_start(&s->e, 0.0);
            if (s->clock_out && computer_wanted(s)) realtime_out(s, 0xFA);
        }
        double end = s->itick + (double)frames * s->bpm * E1_PPQN / (60.0 * MOVE_SAMPLE_RATE);
        if (s->clock_out && computer_wanted(s)) {
            int n = 0;
            while (s->next_clock_tick <= end && n++ < 8) {
                realtime_out(s, 0xF8);
                s->next_clock_tick += E1_PPQN / 24;
            }
        } else {
            while (s->next_clock_tick <= end) s->next_clock_tick += E1_PPQN / 24;
        }
        s->itick = end;
        e1_advance(&s->e, s->itick, emit, s);
    } else {
        if (s->e.running) {
            e1_stop(&s->e, emit, s);
            if (s->clock_out && computer_wanted(s)) realtime_out(s, 0xFC);
        }
        e1_advance(&s->e, s->itick, emit, s);   /* live CC edits still go out */
    }
}

static void e1_render(void *inst, int16_t *out, int frames)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (out && frames > 0) memset(out, 0, sizeof(int16_t) * (size_t)frames * 2);
    if (!s) return;

    if (s->clock == CLOCK_INTERNAL) run_internal(s, frames);
    else run_follow(s, frames);

    if (g_host) {
        q_flush(&s->q_comp, g_host->midi_send_external, COMPUTER_BUDGET);
        q_flush(&s->q_move, g_host->midi_inject_to_move, MOVE_BUDGET);
    }
}

/* ------------------------------------------------------------- params */

static int parse_track_key(const char *key, const char **rest)
{
    /* "t1:steps" .. "t16:steps" */
    if (key[0] != 't' || key[1] < '0' || key[1] > '9') return -1;
    char *end = NULL;
    long n = strtol(key + 1, &end, 10);
    if (!end || *end != ':' || n < 1 || n > E1_TRACKS) return -1;
    *rest = end + 1;
    return (int)n - 1;
}

static void set_clock(e1_inst_t *s, int c)
{
    if (c == s->clock) return;
    stop_all(s);
    if (s->clock == CLOCK_INTERNAL && s->internal_play && s->clock_out) realtime_out(s, 0xFC);
    s->internal_play = 0;
    s->clock = c;
}

static void e1_set_param(void *inst, const char *key, const char *val);

/* "key=value" lines from dump:g */
static void load_globals(e1_inst_t *s, const char *text)
{
    char line[96];
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            if (!strcmp(line, "clock") || !strcmp(line, "bpm") || !strcmp(line, "clockout") ||
                !strcmp(line, "dest") || !strcmp(line, "seed"))
                e1_set_param(s, line, eq + 1);
        }
        p = nl ? nl + 1 : NULL;
    }
}

static void e1_set_param(void *inst, const char *key, const char *val)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (!s || !key || !val) return;
    const char *rest = NULL;
    int t = parse_track_key(key, &rest);
    if (t >= 0) { e1_set(&s->e, t, rest, val); return; }

    if (strncmp(key, "load:", 5) == 0) {
        if (key[5] == 'g') load_globals(s, val);
        else {
            int n = atoi(key + 5) - 1;
            if (n >= 0 && n < E1_TRACKS) {
                if (s->e.t[n].gate_open) e1_all_notes_off(&s->e, emit, s);
                e1_load_lines(&s->e, n, val);
            }
        }
        return;
    }
    if (strcmp(key, "focus") == 0) { int f = atoi(val) - 1; if (f >= 0 && f < E1_TRACKS) s->focus = f; return; }
    if (strcmp(key, "clock") == 0) {
        for (int i = 0; i < 3; i++) if (strcmp(val, CLOCK_NAMES[i]) == 0) { set_clock(s, i); return; }
        int c = atoi(val); if (c >= 0 && c <= 2) set_clock(s, c);
        return;
    }
    if (strcmp(key, "bpm") == 0) {
        float b = (val[0] == 'd') ? s->bpm + (float)atof(val + 1) : (float)atof(val);
        if (b < 20.0f) b = 20.0f;
        if (b > 300.0f) b = 300.0f;
        s->bpm = b;
        return;
    }
    if (strcmp(key, "play") == 0) {
        if (s->clock != CLOCK_INTERNAL) return;   /* Move owns the transport */
        if (strcmp(val, "toggle") == 0) s->internal_play = !s->internal_play;
        else s->internal_play = atoi(val) != 0;
        return;
    }
    if (strcmp(key, "clockout") == 0) { s->clock_out = atoi(val) != 0; return; }
    if (strcmp(key, "panic") == 0) { e1_all_notes_off(&s->e, emit, s); return; }
    if (strcmp(key, "fill") == 0) { s->e.fill_held = (uint8_t)(atoi(val) != 0); return; }
    if (strcmp(key, "dest") == 0) {
        e1_all_notes_off(&s->e, emit, s);          /* close notes on the old port */
        e1_set(&s->e, -1, "dest", val);
        return;
    }
    if (strcmp(key, "seed") == 0) { e1_set(&s->e, -1, "seed", val); return; }
}

static int json_ui(e1_inst_t *s, char *buf, int len)
{
    int pos = 0;
#define PUT(...) do { if (pos < len) { int _n = snprintf(buf + pos, (size_t)(len - pos), __VA_ARGS__); if (_n > 0) pos += _n; } } while (0)
    uint32_t flash = 0, mute = 0, type = 0;
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &s->e.t[i];
        if (t->flash) { flash |= 1u << i; t->flash = 0; }
        if (t->muted) mute |= 1u << i;
        if (t->type == E1_TYPE_CC) type |= 1u << i;
    }
    double tick = s->clock == CLOCK_INTERNAL ? s->itick : s->e.tick;
    PUT("{\"r\":%d,\"ck\":%d,\"bpm\":%.1f,\"d\":%d,\"co\":%d,\"f\":%d,\"fl\":%u,\"mu\":%u,\"ty\":%u,\"tk\":%.0f,\"dr\":%u,",
        s->e.running, s->clock, (double)s->bpm, s->e.dest, s->clock_out, s->focus + 1,
        (unsigned)flash, (unsigned)mute, (unsigned)type, tick,
        (unsigned)(s->q_move.dropped + s->q_comp.dropped));
    PUT("\"st\":[");
    for (int i = 0; i < E1_TRACKS; i++) PUT("%s%d", i ? "," : "", s->e.t[i].cur_step);
    const e1_track_t *f = &s->e.t[s->focus];
    PUT("],\"h\":\"%llx\",\"p\":[", (unsigned long long)e1_hit_mask(f, s->e.fill_held));
    for (int i = 0; i < e1_param_count(); i++) PUT("%s%d", i ? "," : "", e1_get_num(&s->e, s->focus, i));
    PUT("],\"cv\":[");
    for (int i = 0; i < E1_CC_LANES; i++) PUT("%s%d", i ? "," : "", f->cc_val[i]);
    PUT("],\"cn\":[");
    for (int i = 0; i < E1_CC_LANES; i++) PUT("%s%d", i ? "," : "", f->cc_num[i]);
    PUT("]}");
#undef PUT
    if (pos >= len) return -1;
    return pos;
}

static int e1_get_param(void *inst, const char *key, char *buf, int len)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (!s || !key || !buf || len < 2) return -1;
    if (strcmp(key, "ui") == 0) return json_ui(s, buf, len);
    if (strcmp(key, "module_id") == 0) return snprintf(buf, (size_t)len, "e1");
    if (strcmp(key, "pkeys") == 0) {
        int pos = 0;
        for (int i = 0; i < e1_param_count() && pos < len; i++) {
            int n = snprintf(buf + pos, (size_t)(len - pos), "%s%s", i ? "," : "", e1_param_key(i));
            if (n > 0) pos += n;
        }
        return pos < len ? pos : -1;
    }
    if (strncmp(key, "dump:", 5) == 0) {
        if (key[5] == 'g') {
            int n = e1_dump_global(&s->e, buf, len);
            if (n < len) n += snprintf(buf + n, (size_t)(len - n), "clock=%s\nbpm=%.1f\nclockout=%d\n",
                                       CLOCK_NAMES[s->clock], (double)s->bpm, s->clock_out);
            return n < len ? n : -1;
        }
        int t = atoi(key + 5) - 1;
        if (t < 0 || t >= E1_TRACKS) return -1;
        return e1_dump_track(&s->e, t, buf, len);
    }
    if (strcmp(key, "clock") == 0) return snprintf(buf, (size_t)len, "%s", CLOCK_NAMES[s->clock]);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, (size_t)len, "%.1f", (double)s->bpm);
    const char *rest = NULL;
    int t = parse_track_key(key, &rest);
    if (t >= 0) return e1_get(&s->e, t, rest, buf, len);
    return e1_get(&s->e, -1, key, buf, len);
}

static int e1_get_error(void *inst, char *buf, int len) { (void)inst; (void)buf; (void)len; return 0; }

static plugin_api_v2_t g_api = {
    .api_version      = MOVE_PLUGIN_API_VERSION_2,
    .create_instance  = e1_create,
    .destroy_instance = e1_destroy,
    .on_midi          = e1_on_midi,
    .set_param        = e1_set_param,
    .get_param        = e1_get_param,
    .get_error        = e1_get_error,
    .render_block     = e1_render,
};

__attribute__((visibility("default")))
plugin_api_v2_t *move_plugin_init_v2(const host_api_v1_t *host)
{
    g_host = host;
    return &g_api;
}
