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

#define QSIZE 1024                /* packets per port, power of two */
#define QSOFT 384                 /* above this fill only note-offs and Stop get in */
#define MOVE_BUDGET 6             /* packets per block into Move */
#define COMPUTER_BUDGET 16        /* packets per block to USB-C */

typedef struct {
    uint8_t pkt[QSIZE][4];
    uint32_t head, tail;
    uint32_t dropped;
    uint8_t owed[16][128];        /* note-ons queued and not yet answered by a note-off */
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

    int sent_start;               /* the computer was told to start and not yet to stop */

    int focus;

    /* Pattern recall: tracks are loaded into `stage`, then copied into the
     * live engine on the next bar (or at once when stopped). */
    e1_engine_t stage;
    uint32_t stage_mask;
    int commit;                   /* 0 none, 1 on the quantize line, 2 now, 3 after commit_n lines */
    int commit_n, commit_left;    /* mode 3: quantize lines to cross, and how many are still to come */
    int was_running;
    int quant;                    /* index into QUANT_TICKS */

    /* Temp: saved fields of every track while the Temp button is held */
    e1_track_t temp_bak[E1_TRACKS];
    int temp_on;

    /* Web page (Schwung's Tool tab). Track edits from the page come in as
     * "w:tN:key" and bump web_rev so ui.js saves them. Set and pattern names
     * live in ui.js: it hands them over as a flat JSON fragment, and renames
     * from the page wait in webq until ui.js collects them. */
    uint32_t rev, web_rev;
    char names[1600];
    char webq[8][64];
    int wq_head, wq_n;
} e1_inst_t;

static const char *const QUANT_NAMES[] = { "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
static const int QUANT_TICKS[] = { 96, 192, 384, 768, 1536, 3072 };
#define N_QUANT 6

static const host_api_v1_t *g_host = NULL;

/* ------------------------------------------------------------- queues */

static void q_push(queue_t *q, uint8_t b0, uint8_t st, uint8_t d1, uint8_t d2)
{
    /* When the port is slow, new notes and CCs are dropped first. A note-off
     * is queued only for a note-on that got in, and there is always room for
     * it: QSOFT note-ons waiting, their note-offs, and every sounding voice
     * fit in QSIZE. So a full queue thins the music and never hangs a note. */
    uint32_t fill = q->head - q->tail;
    uint8_t type = st & 0xF0, ch = st & 0x0F;
    int is_off = type == 0x80 || (type == 0x90 && d2 == 0);
    if (st < 0xF0 && type == 0x90 && !is_off) {
        if (fill >= QSOFT || q->owed[ch][d1 & 127] == 255) { q->dropped++; return; }
        q->owed[ch][d1 & 127]++;
    } else if (st < 0xF0 && is_off) {
        if (!q->owed[ch][d1 & 127]) return;            /* its note-on never went out */
        q->owed[ch][d1 & 127]--;
    } else if (st != 0xFC && st != 0xFA && fill >= QSOFT) { q->dropped++; return; }
    if (fill >= QSIZE) { q->dropped++; return; }
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
    s->quant = 2;
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
    if (s->sent_start) { realtime_out(s, 0xFC); s->sent_start = 0; }
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

/* ------------------------------------------------------- pattern recall */

static void replace_track(e1_inst_t *s, int i, const e1_track_t *n)
{
    e1_track_silence(&s->e, i, emit, s);
    e1_copy_stored(&s->e.t[i], n);
    e1_resync(&s->e, i);
    e1_track_t *t = &s->e.t[i];
    for (int l = 0; l < E1_CC_LANES; l++) if (t->cc_val[l] >= 0) t->cc_dirty |= 1u << l;
}

static void apply_stage(e1_inst_t *s)
{
    for (int i = 0; i < E1_TRACKS; i++)
        if (s->stage_mask & (1u << i)) replace_track(s, i, &s->stage.t[i]);
    s->stage_mask = 0;
    s->commit = 0;
}

/* Advance to tick_now, applying a queued pattern exactly on the quantize line. */
static void advance_to(e1_inst_t *s, double tick_now)
{
    if (s->commit == 2 || (s->commit == 1 && !s->e.running)) apply_stage(s);
    /* Mode 3 (a pattern chain): counts quantize lines while playing, and
     * starts its count again when the transport stops. */
    if (s->commit == 3 && !s->e.running && s->was_running) s->commit_left = s->commit_n;
    s->was_running = s->e.running;
    if (s->commit == 3 && s->e.running) {
        double q = (double)QUANT_TICKS[s->quant], pos = s->e.tick;
        for (;;) {
            double line = (floor((pos - s->e.origin) / q + 1e-9) + 1.0) * q + s->e.origin;
            if (tick_now < line) break;
            if (s->commit_left <= 1) {
                e1_advance(&s->e, line - 1e-6, emit, s);
                apply_stage(s);
                break;
            }
            s->commit_left--;
            pos = line;
        }
    }
    if (s->commit == 1 && s->e.running) {
        double q = (double)QUANT_TICKS[s->quant];
        double line = (floor((s->e.tick - s->e.origin) / q + 1e-9) + 1.0) * q + s->e.origin;
        if (tick_now >= line) {
            e1_advance(&s->e, line - 1e-6, emit, s);
            apply_stage(s);
        }
    }
    e1_advance(&s->e, tick_now, emit, s);
}

/* ------------------------------------------------------------- render */

static void run_follow(e1_inst_t *s, int frames)
{
    double beat = -1.0;
    int have_beat = g_host && g_host->get_beat_position;
    if (have_beat) beat = g_host->get_beat_position();
    if (!isfinite(beat)) beat = -1.0;
    /* Clock from the computer: Move follows it and reports the beat. If Move
     * reports none, count the MIDI clock ourselves. */
    if (s->clock == CLOCK_COMPUTER && beat < 0.0 && s->ext_running) have_beat = 0;

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

    if (running && s->e.running && tick < s->e.tick - 1.0) e1_stop(&s->e, emit, s);   /* Move looped or jumped back */
    if (running && !s->e.running) e1_join(&s->e, 0.0, tick);   /* bars line up with Move */
    else if (!running && s->e.running) e1_stop(&s->e, emit, s);
    advance_to(s, tick);
}

static void run_internal(e1_inst_t *s, int frames)
{
    if (s->internal_play) {
        if (!s->e.running) {
            s->itick = 0.0;
            s->next_clock_tick = 0.0;
            e1_start(&s->e, 0.0);
        }
        double end = s->itick + (double)frames * s->bpm * E1_PPQN / (60.0 * MOVE_SAMPLE_RATE);
        int want = s->clock_out && computer_wanted(s);
        if (want && !s->sent_start) { realtime_out(s, 0xFA); s->sent_start = 1; }
        if (!want && s->sent_start) { realtime_out(s, 0xFC); s->sent_start = 0; }
        if (want) {
            int n = 0;
            while (s->next_clock_tick <= end && n++ < 8) {
                realtime_out(s, 0xF8);
                s->next_clock_tick += E1_PPQN / 24;
            }
        } else {
            while (s->next_clock_tick <= end) s->next_clock_tick += E1_PPQN / 24;
        }
        s->itick = end;
        advance_to(s, s->itick);
    } else {
        if (s->e.running) {
            e1_stop(&s->e, emit, s);
        }
        if (s->sent_start) { realtime_out(s, 0xFC); s->sent_start = 0; }
        advance_to(s, s->itick);   /* live CC edits still go out */
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
    if (s->sent_start) { realtime_out(s, 0xFC); s->sent_start = 0; }
    s->internal_play = 0;
    s->clock = c;
}

static void e1_set_param(void *inst, const char *key, const char *val);

/* "key=value" lines from dump:g */
static void load_globals(e1_inst_t *s, const char *text)
{
    char line[96];
    const char *p = text;
    /* Sets saved before the song scale existed name none: C minor, no chord track. */
    e1_set(&s->e, -1, "songroot", "C");
    e1_set(&s->e, -1, "songscale", "Minor");
    e1_set(&s->e, -1, "chordtrack", "0");
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n); line[n] = 0;
        char *eq = strchr(line, '=');
        if (eq) {
            *eq = 0;
            if (!strcmp(line, "clock") || !strcmp(line, "bpm") || !strcmp(line, "clockout") ||
                !strcmp(line, "dest") || !strcmp(line, "seed") || !strcmp(line, "quant") ||
                !strcmp(line, "songroot") || !strcmp(line, "songscale") || !strcmp(line, "chordtrack"))
                e1_set_param(s, line, eq + 1);
        }
        p = nl ? nl + 1 : NULL;
    }
}

/* Keys whose change must not leave a note sounding on the old target. */
static int cuts_notes(const char *k)
{
    return !strcmp(k, "type") || !strcmp(k, "mute") || !strcmp(k, "channel") || !strcmp(k, "output") ||
           !strcmp(k, "pool") || !strcmp(k, "pooltoggle");
}

static void e1_set_param(void *inst, const char *key, const char *val)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (!s || !key || !val) return;
    s->rev++;
    if (key[0] == 'w' && key[1] == ':') {
        const char *k = key + 2;
        if (!strcmp(k, "pname") || !strcmp(k, "sname")) {
            if (s->wq_n < 8) {
                snprintf(s->webq[(s->wq_head + s->wq_n) % 8], sizeof s->webq[0], "%s %s", k, val);
                s->wq_n++;
            }
            return;
        }
        const char *r2 = NULL;
        int wt = parse_track_key(k, &r2);
        int song = !strcmp(k, "songroot") || !strcmp(k, "songscale") || !strcmp(k, "chordtrack");
        if (wt < 0 && !song) return;              /* the page reaches tracks and the song settings */
        e1_set_param(inst, k, val);
        s->web_rev++;
        return;
    }
    if (strcmp(key, "uinames") == 0) { snprintf(s->names, sizeof s->names, "%s", val); return; }
    const char *rest = NULL;
    int t = parse_track_key(key, &rest);
    if (t >= 0) {
        if (cuts_notes(rest)) e1_track_silence(&s->e, t, emit, s);
        e1_set(&s->e, t, rest, val);
        return;
    }

    if (strncmp(key, "load:", 5) == 0) {
        if (key[5] == 'g') load_globals(s, val);
        else {
            int n = atoi(key + 5) - 1;
            if (n >= 0 && n < E1_TRACKS) {
                e1_track_silence(&s->e, n, emit, s);
                e1_load_track(&s->e, n, val);
                e1_resync(&s->e, n);
            }
        }
        return;
    }
    /* "aud:N" = "<note> <velocity>": play a pad through track N right now. */
    if (strncmp(key, "aud:", 4) == 0) {
        int n = atoi(key + 4) - 1, note = 0, vel = 0;
        if (n < 0 || n >= E1_TRACKS || sscanf(val, "%d %d", &note, &vel) != 2) return;
        const e1_track_t *tr = &s->e.t[n];
        uint8_t ch = (uint8_t)((tr->v[E1_P_CHANNEL] - 1) & 0x0F);
        note &= 0x7F;
        if (vel > 0) emit(s, e1_ports(&s->e, tr), (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)(vel > 127 ? 127 : vel));
        else emit(s, e1_ports(&s->e, tr), (uint8_t)(0x80 | ch), (uint8_t)note, 0);
        return;
    }
    /* "hit:N" = "1" / "0": one hit of track N by hand, held until "0". */
    if (strncmp(key, "hit:", 4) == 0) {
        int n = atoi(key + 4) - 1;
        if (n >= 0 && n < E1_TRACKS) e1_audition(&s->e, n, atoi(val) != 0, emit, s);
        return;
    }
    if (strcmp(key, "focus") == 0) { int f = atoi(val) - 1; if (f >= 0 && f < E1_TRACKS) s->focus = f; return; }
    if (strcmp(key, "clock") == 0) {
        for (int i = 0; i < 3; i++) if (strcmp(val, CLOCK_NAMES[i]) == 0) { set_clock(s, i); return; }
        int c = atoi(val); if (c >= 0 && c <= 2) set_clock(s, c);
        return;
    }
    if (strcmp(key, "quant") == 0) {
        for (int i = 0; i < N_QUANT; i++) if (strcmp(val, QUANT_NAMES[i]) == 0) { s->quant = i; return; }
        if (val[0] == 'd') { int q = s->quant + atoi(val + 1); s->quant = q < 0 ? 0 : (q >= N_QUANT ? N_QUANT - 1 : q); }
        return;
    }
    if (strcmp(key, "bpm") == 0) {
        float b = (val[0] == 'd') ? s->bpm + (float)atof(val + 1) : (float)atof(val);
        if (!isfinite(b)) return;
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
    if (strcmp(key, "clockout") == 0) { s->clock_out = (atoi(val) != 0) || !strcmp(val, "On"); return; }
    if (strcmp(key, "panic") == 0) { e1_all_notes_off(&s->e, emit, s); return; }
    if (strncmp(key, "perf:", 5) == 0) {
        /* "perf:<move>" = "<mask hex> <value>" */
        char *end = NULL;
        unsigned long mask = strtoul(val, &end, 16);
        int v = (end && *end) ? atoi(end) : 0;
        e1_perf(&s->e, (uint32_t)mask, key + 5, v);
        if (strcmp(key + 5, "solo") == 0)
            for (int i = 0; i < E1_TRACKS; i++) if (s->e.t[i].p_solo_mute) e1_track_silence(&s->e, i, emit, s);
        return;
    }
    if (strncmp(key, "stage:", 6) == 0) {
        int n = atoi(key + 6) - 1;
        if (n >= 0 && n < E1_TRACKS) {
            e1_load_track(&s->stage, n, val);
            s->stage_mask |= 1u << n;
        }
        return;
    }
    if (strcmp(key, "commit") == 0) {
        /* "after N": on the Nth quantize line from here (a pattern chain) */
        if (strncmp(val, "after ", 6) == 0) {
            int n = atoi(val + 6);
            s->commit_n = s->commit_left = n < 1 ? 1 : (n > 256 ? 256 : n);
            s->commit = 3;
            return;
        }
        s->commit = strcmp(val, "now") == 0 ? 2 : (strcmp(val, "cancel") == 0 ? 0 : 1);
        if (!s->commit) s->stage_mask = 0;
        return;
    }
    if (strcmp(key, "temp") == 0) {
        if (strcmp(val, "1") == 0 && !s->temp_on) {
            for (int i = 0; i < E1_TRACKS; i++) e1_copy_stored(&s->temp_bak[i], &s->e.t[i]);
            s->temp_on = 1;
        } else if (strcmp(val, "0") == 0 && s->temp_on) {
            for (int i = 0; i < E1_TRACKS; i++)
                if (memcmp(&s->e.t[i], &s->temp_bak[i], E1_TRACK_STORED_BYTES) != 0) replace_track(s, i, &s->temp_bak[i]);
            s->temp_on = 0;
        } else if (strcmp(val, "keep") == 0) {
            s->temp_on = 0;
        }
        return;
    }
    if (strcmp(key, "dest") == 0) {
        e1_all_notes_off(&s->e, emit, s);          /* close notes on the old port */
        e1_set(&s->e, -1, "dest", val);
        return;
    }
    if (strcmp(key, "seed") == 0) { e1_set(&s->e, -1, "seed", val); return; }
    if (!strcmp(key, "songroot") || !strcmp(key, "songscale") || !strcmp(key, "chordtrack")) { e1_set(&s->e, -1, key, val); return; }
}

/* One compact answer per frame for the UI. Per-track bit sets first, then
 * everything about the focus track. */
static int json_ui(e1_inst_t *s, char *buf, int len)
{
    int pos = 0;
#define PUT(...) do { if (pos < len) { int _n = snprintf(buf + pos, (size_t)(len - pos), __VA_ARGS__); if (_n > 0) pos += _n; } } while (0)
    uint32_t flash = 0, mute = 0, type = 0, pf = 0, rp = 0;
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &s->e.t[i];
        if (t->flash) { flash |= 1u << i; t->flash = 0; }
        if (t->v[E1_P_MUTE]) mute |= 1u << i;
        if (t->v[E1_P_TYPE] == E1_TYPE_CC) type |= 1u << i;
        if (t->p_fill || t->p_ratchet || t->p_speed || t->p_reverse || t->p_freeze || t->p_accent) pf |= 1u << i;
        if (t->p_solo_mute) pf |= 1u << (16 + i);
        for (int k = 0; k < E1_CHAINS; k++) if (t->chain[k].left > 0) rp |= 1u << i;
    }
    const e1_track_t *f = &s->e.t[s->focus];
    int dc = e1_display_cycle(f);
    PUT("{\"r\":%d,\"ck\":%d,\"bpm\":%.1f,\"d\":%d,\"co\":%d,\"qn\":%d,\"f\":%d,\"fl\":%u,\"mu\":%u,\"ty\":%u,\"dr\":%u,",
        s->e.running, s->clock, (double)s->bpm, s->e.dest, s->clock_out, s->quant, s->focus + 1,
        (unsigned)flash, (unsigned)mute, (unsigned)type, (unsigned)(s->q_move.dropped + s->q_comp.dropped));
    PUT("\"ql\":%d,", s->commit == 3 ? s->commit_left : 0);
    PUT("\"pf\":%u,\"tmp\":%d,\"q\":%d,\"wq\":%d,\"wr\":%u,", (unsigned)pf, s->temp_on, s->commit, s->wq_n, (unsigned)s->web_rev);
    PUT("\"rp\":%u,\"rt\":\"%llx\",", (unsigned)rp, (unsigned long long)f->retrig);
    PUT("\"dc\":%d,\"dm\":%d,\"dh\":%d,\"lv\":\"%llx\",", e1_dice_rolled(f), e1_dice_active(&s->e, s->focus), f->dhist_n,
        (unsigned long long)e1_live_mask(&s->e, s->focus, f->edit_mask || s->e.running ? dc : -1, f->p_fill));
    PUT("\"sr\":%d,\"ss\":%d,\"ct\":%d,\"fo\":%u,", s->e.song_root, s->e.song_scale, s->e.chord_track, (unsigned)e1_followers(&s->e));
    PUT("\"cy\":%d,\"pc\":%d,\"ce\":%u,\"em\":%u,\"sn\":%d,", dc, f->cur_cycle, (unsigned)e1_cycles_edited(f),
        (unsigned)f->edit_mask, e1_steps(&s->e, s->focus, f->edit_mask ? dc : -1));
    PUT("\"st\":[");
    for (int i = 0; i < E1_TRACKS; i++) PUT("%s%d", i ? "," : "", s->e.t[i].cur_step);
    PUT("],\"nm\":[");
    for (int i = 0; i < E1_TRACKS; i++) PUT("%s\"%s\"", i ? "," : "", s->e.t[i].name);
    PUT("],\"sm\":\"%llx\",\"se\":\"%llx\",\"sc\":\"%llx\"", (unsigned long long)f->step_mask, (unsigned long long)e1_steps_edited(f),
        (unsigned long long)(f->edit_mask ? e1_steps_edited_in(f, dc) : 0));
    PUT(",\"h\":\"%llx\",\"p\":[", (unsigned long long)e1_hit_mask(&s->e, s->focus, f->edit_mask || s->e.running ? dc : -1, f->p_fill));
    for (int i = 0; i < E1_NP; i++) PUT("%s%d", i ? "," : "", e1_get_num(&s->e, s->focus, i));
    PUT("],\"rn\":[");
    for (int i = 0; i < E1_NP; i++) PUT("%s%d", i ? "," : "", f->rnd[i]);
    PUT("],\"po\":[");
    for (int i = 0; i < f->pool_n; i++) PUT("%s%d", i ? "," : "", f->pool[i]);
    PUT("],\"cv\":[");
    for (int i = 0; i < E1_CC_LANES; i++) PUT("%s%d", i ? "," : "", e1_cc_shown(&s->e, s->focus, i));
    PUT("],\"cn\":[");
    for (int i = 0; i < E1_CC_LANES; i++) PUT("%s%d", i ? "," : "", f->cc_num[i]);
    PUT("],\"cr\":[");
    for (int i = 0; i < E1_CC_LANES; i++) PUT("%s%d", i ? "," : "", f->cc_rnd[i]);
    PUT("]}");
    if (pos >= len) return -1;
    return pos;
}

/* Flat JSON of strings for Schwung's Tool tab: every track's name, type,
 * channel, output, mute and CC numbers, plus the names ui.js handed over. */
static int json_state(e1_inst_t *s, char *buf, int len)
{
    int pos = 0;
    PUT("{\"rev\":\"%u\",\"song_root\":\"%d\",\"song_scale\":\"%d\",\"chord_track\":\"%d\"", (unsigned)s->rev,
        s->e.song_root, s->e.song_scale, s->e.chord_track);
    for (int i = 0; i < E1_TRACKS; i++) {
        const e1_track_t *t = &s->e.t[i];
        char nm[2 * E1_NAME_LEN + 1]; int k = 0;
        for (const char *c = t->name; *c && k < (int)sizeof nm - 2; c++) {
            if (*c == '"' || *c == '\\') nm[k++] = '\\';
            nm[k++] = *c;
        }
        nm[k] = 0;
        PUT(",\"t%d_name\":\"%s\",\"t%d_type\":\"%d\",\"t%d_ch\":\"%d\",\"t%d_out\":\"%d\",\"t%d_mute\":\"%d\",\"t%d_ccn\":\"",
            i + 1, nm, i + 1, t->v[E1_P_TYPE], i + 1, t->v[E1_P_CHANNEL], i + 1, t->v[E1_P_OUTPUT], i + 1, t->v[E1_P_MUTE], i + 1);
        for (int l = 0; l < E1_CC_LANES; l++) PUT("%s%d", l ? "," : "", t->cc_num[l]);
        PUT("\"");
    }
    if (s->names[0]) PUT(",%s", s->names);
    PUT("}");
    return pos < len ? pos : -1;
}
#undef PUT

static int e1_get_param(void *inst, const char *key, char *buf, int len)
{
    e1_inst_t *s = (e1_inst_t *)inst;
    if (!s || !key || !buf || len < 2) return -1;
    if (strcmp(key, "ui") == 0) return json_ui(s, buf, len);
    if (strcmp(key, "state") == 0) return json_state(s, buf, len);
    if (strcmp(key, "webreq") == 0) {
        if (!s->wq_n) { buf[0] = 0; return 0; }
        int n = snprintf(buf, (size_t)len, "%s", s->webq[s->wq_head]);
        s->wq_head = (s->wq_head + 1) % 8; s->wq_n--;
        return n < len ? n : len - 1;
    }
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
            if (n < len) n += snprintf(buf + n, (size_t)(len - n), "clock=%s\nbpm=%.1f\nclockout=%d\nquant=%s\n",
                                       CLOCK_NAMES[s->clock], (double)s->bpm, s->clock_out, QUANT_NAMES[s->quant]);
            return n < len ? n : -1;
        }
        int t = atoi(key + 5) - 1;
        if (t < 0 || t >= E1_TRACKS) return -1;
        int n = e1_dump_track(&s->e, t, buf, len);
        return n >= len - 1 ? -1 : n;             /* -1: it did not fit */
    }
    if (strcmp(key, "clock") == 0) return snprintf(buf, (size_t)len, "%s", CLOCK_NAMES[s->clock]);
    if (strcmp(key, "bpm") == 0) return snprintf(buf, (size_t)len, "%.1f", (double)s->bpm);
    if (strcmp(key, "quant") == 0) return snprintf(buf, (size_t)len, "%s", QUANT_NAMES[s->quant]);
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
