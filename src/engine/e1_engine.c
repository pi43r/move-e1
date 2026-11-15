/*
 * e1_engine.c — Move E-1 sequencing engine. See e1_engine.h.
 */
#include "e1_engine.h"

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ------------------------------------------------------------------ tables */

const char *const e1_division_names[E1_DIVISIONS] = {
    "1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
    "1/2T", "1/4T", "1/8T", "1/16T", "1/32T",
    "1/4.", "1/8.", "1/16."
};
const int e1_division_ticks[E1_DIVISIONS] = {
    384, 192, 96, 48, 24, 12, 6,
    128, 64, 32, 16, 8,
    144, 72, 36
};

const char *const e1_length_names[E1_LENGTHS] = {
    "Off", "1", "2", "3", "4", "5", "6", "7", "8", "16"
};
const int e1_length_bars[E1_LENGTHS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 16 };

static const char *const TYPE_NAMES[]   = { "Note", "CC" };
static const char *const OUT_NAMES[]    = { "Global", "Move", "Computer", "Both" };
static const char *const DEST_NAMES[]   = { "Move", "Computer", "Both" };
static const char *const NMODE_NAMES[]  = { "Drum", "Melodic" };
static const char *const CCMODE_NAMES[] = { "Pulse", "Change", "Glide" };
static const char *const ONOFF_NAMES[]  = { "Off", "On" };

/* ------------------------------------------------------------- utilities */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

int e1_chance(uint32_t seed, int track, int64_t k)
{
    uint64_t x = ((uint64_t)seed << 32) ^ ((uint64_t)(uint32_t)track * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)k;
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return (int)(x % 100ULL);
}

/* ------------------------------------------------------------- defaults */

void e1_track_defaults(e1_track_t *t, int index)
{
    memset(t, 0, sizeof(*t));
    t->type = E1_TYPE_NOTE;
    t->steps = 16;
    t->pulses = 4;
    t->rotate = 0;
    t->division = 4;          /* 1/16 */
    t->length = 0;            /* free-running, so unequal Steps make polymeters */
    t->cycles = 1;
    t->prob = 100;
    t->fill = 0;
    t->channel = 0;
    t->output = E1_OUT_GLOBAL;
    t->note_mode = E1_NOTE_DRUM;
    t->note = (uint8_t)clampi(36 + index, 0, 127);
    t->velocity = 100;
    t->sustain = 25;
    t->timing = 0;
    t->delay = 0;
    t->muted = 0;
    for (int i = 0; i < E1_CC_LANES; i++) {
        t->cc_val[i] = -1;
        t->cc_num[i] = (uint8_t)(i < 14 ? 70 + i : (i == 14 ? 1 : 74));
        t->cc_sent[i] = -1;
        t->cc_target[i] = -1;
        t->cc_cur[i] = 0.0f;
    }
    t->cc_mode = E1_CC_ON_PULSE;
    t->slew = 0;
    t->next_k = 0;
    t->cur_step = -1;
}

/* The starter project: tracks 1-8 drums into a Move drum rack on channel 1,
 * 9-11 melodic on Move tracks 2-4, 12-16 CC tracks on channels 5-9. */
void e1_init(e1_engine_t *e)
{
    memset(e, 0, sizeof(*e));
    static const uint8_t drum_pulses[8] = { 4, 2, 8, 3, 5, 3, 2, 7 };
    static const uint8_t drum_rot[8]    = { 0, 4, 0, 2, 1, 6, 3, 0 };
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &e->t[i];
        e1_track_defaults(t, i);
        if (i < 8) {
            t->channel = 0;
            t->note = (uint8_t)(36 + i);
            t->pulses = drum_pulses[i];
            t->rotate = drum_rot[i];
            t->muted = (i >= 4);   /* start with a simple four-track groove */
        } else if (i < 11) {
            t->channel = (uint8_t)(i - 8 + 1);   /* channels 2-4 */
            t->note_mode = E1_NOTE_MELODIC;
            t->note = (uint8_t)(i == 8 ? 36 : (i == 9 ? 60 : 72));
            t->pulses = (uint8_t)(i == 8 ? 5 : 3);
            t->steps = (uint8_t)(i == 10 ? 12 : 16);
            t->division = (uint8_t)(i == 9 ? 3 : 4);
            t->sustain = 40;
            t->muted = 1;
        } else {
            t->type = E1_TYPE_CC;
            t->channel = (uint8_t)(i - 11 + 4);  /* channels 5-9 */
            t->pulses = 4;
            t->muted = 1;
        }
    }
    e->dest = E1_DEST_MOVE;
    e->seed = 0xE1u;
    e->running = 0;
    e->tick = 0.0;
}

/* ---------------------------------------------------------------- euclid */

static int euclid_base(int steps, int pulses, int j)
{
    if (steps <= 0 || pulses <= 0) return 0;
    if (pulses >= steps) return 1;
    return ((j * pulses) % steps) < pulses;
}

int e1_hit(const e1_track_t *t, int i, int fill)
{
    int steps = t->steps < 1 ? 1 : t->steps;
    if (i < 0 || i >= steps) return 0;
    int r = t->rotate % steps;
    int j = ((i - r) % steps + steps) % steps;   /* pattern space */
    int pulses = t->pulses + (fill ? t->fill : 0);
    if (pulses > steps) pulses = steps;
    int h = euclid_base(steps, pulses, j);
    if (t->del_mask & (1ULL << j)) h = 0;
    if (t->add_mask & (1ULL << j)) h = 1;
    return h;
}

uint64_t e1_hit_mask(const e1_track_t *t, int fill)
{
    uint64_t m = 0;
    for (int i = 0; i < t->steps && i < 64; i++)
        if (e1_hit(t, i, fill)) m |= (1ULL << i);
    return m;
}

void e1_toggle_step(e1_track_t *t, int i)
{
    int steps = t->steps < 1 ? 1 : t->steps;
    if (i < 0 || i >= steps) return;
    int r = t->rotate % steps;
    int j = ((i - r) % steps + steps) % steps;
    uint64_t bit = 1ULL << j;
    if (e1_hit(t, i, 0)) {
        if (t->add_mask & bit) t->add_mask &= ~bit;
        else t->del_mask |= bit;
    } else {
        if (t->del_mask & bit) t->del_mask &= ~bit;
        else t->add_mask |= bit;
    }
}

int e1_ports(const e1_engine_t *e, const e1_track_t *t)
{
    int o = t->output;
    if (o == E1_OUT_GLOBAL) {
        if (e->dest == E1_DEST_COMPUTER) return E1_PORT_COMPUTER;
        if (e->dest == E1_DEST_BOTH) return E1_PORT_MOVE | E1_PORT_COMPUTER;
        return E1_PORT_MOVE;
    }
    if (o == E1_OUT_COMPUTER) return E1_PORT_COMPUTER;
    if (o == E1_OUT_BOTH) return E1_PORT_MOVE | E1_PORT_COMPUTER;
    return E1_PORT_MOVE;
}

/* ------------------------------------------------------------- transport */

static double base_ticks(const e1_track_t *t)
{
    return (double)e1_division_ticks[t->division < E1_DIVISIONS ? t->division : 4];
}

static double slot_ticks(const e1_track_t *t)
{
    double st = base_ticks(t);
    if (t->p_speed < 0) st *= 2.0;
    else if (t->p_speed > 0) st *= 0.5;
    return st;
}

static double slot_time(const e1_engine_t *e, const e1_track_t *t, int64_t k, double origin)
{
    (void)e;
    double st = slot_ticks(t);
    double tm = origin + (double)k * st + st * (double)t->delay / 100.0;
    if (k & 1) tm += st * (double)t->timing / 100.0;
    return tm;
}

static int64_t mod64(int64_t a, int64_t m) { int64_t r = a % m; return r < 0 ? r + m : r; }

/* Sequence position after Freeze. */
static int64_t effective_s(const e1_track_t *t, int64_t s)
{
    if (t->p_freeze > 0 && s >= t->freeze_s0)
        return t->freeze_s0 + mod64(s - t->freeze_s0, t->p_freeze);
    return s;
}

static int slot_step(const e1_track_t *t, int64_t s)
{
    int steps = t->steps < 1 ? 1 : t->steps;
    int bars = e1_length_bars[t->length < E1_LENGTHS ? t->length : 0];
    int step;
    if (bars > 0) {
        int64_t per = (int64_t)((double)(bars * E1_TICKS_PER_BAR) / base_ticks(t));
        if (per < 1) per = 1;
        step = (int)(mod64(s, per) % steps);
    } else {
        step = (int)mod64(s, steps);
    }
    if (t->p_reverse) step = steps - 1 - step;
    return step;
}

/* Re-anchor a running track after its step length changed (Division, half
 * or double time), so it carries on with the next step instead of jumping
 * to wherever the new grid would put it. */
static void reanchor(e1_engine_t *e, e1_track_t *t)
{
    if (!e->running) return;
    double st = slot_ticks(t);
    int64_t k = (int64_t)ceil((e->tick - e->origin) / st - 1e-9);
    if (k < 0) k = 0;
    t->next_k = k;
    t->step_off = k - (t->last_s + 1);
    t->rat_left = 0;
}

void e1_reanchor(e1_engine_t *e, int track)
{
    if (track >= 0 && track < E1_TRACKS) reanchor(e, &e->t[track]);
}

void e1_start(e1_engine_t *e, double tick)
{
    e1_join(e, tick, tick);
}

void e1_join(e1_engine_t *e, double origin, double now)
{
    e->running = 1;
    e->origin = origin;
    e->tick = now;
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &e->t[i];
        double st = slot_ticks(t);
        int64_t k = (int64_t)ceil((now - origin) / st - 1e-9);
        t->next_k = k < 0 ? 0 : k;
        t->step_off = 0;
        t->last_s = t->next_k - 1;
        t->cur_step = -1;
        t->rat_left = 0;
    }
}

static void note_off(e1_track_t *t, e1_emit_fn emit, void *user)
{
    if (!t->gate_open) return;
    emit(user, t->gate_ports, (uint8_t)(0x80 | (t->gate_ch & 0x0F)), t->gate_note, 0);
    t->gate_open = 0;
}

void e1_all_notes_off(e1_engine_t *e, e1_emit_fn emit, void *user)
{
    for (int i = 0; i < E1_TRACKS; i++) { note_off(&e->t[i], emit, user); e->t[i].rat_left = 0; }
}

void e1_stop(e1_engine_t *e, e1_emit_fn emit, void *user)
{
    e->running = 0;
    e1_all_notes_off(e, emit, user);
    for (int i = 0; i < E1_TRACKS; i++) e->t[i].cur_step = -1;
}

void e1_copy_stored(e1_track_t *dst, const e1_track_t *src)
{
    memcpy(dst, src, E1_TRACK_STORED_BYTES);
}

/* -------------------------------------------------------------- perform */

void e1_perf(e1_engine_t *e, uint32_t mask, const char *move, int value)
{
    if (!move) return;
    if (strcmp(move, "solo") == 0) {
        for (int i = 0; i < E1_TRACKS; i++)
            e->t[i].p_solo_mute = (uint8_t)(value && !(mask & (1u << i)));
        return;
    }
    for (int i = 0; i < E1_TRACKS; i++) {
        if (!(mask & (1u << i))) continue;
        e1_track_t *t = &e->t[i];
        if (strcmp(move, "fill") == 0) t->p_fill = (uint8_t)(value != 0);
        else if (strcmp(move, "ratchet") == 0) { t->p_ratchet = (uint8_t)clampi(value, 0, 4); if (t->p_ratchet < 2) t->rat_left = 0; }
        else if (strcmp(move, "speed") == 0) {
            int8_t sp = (int8_t)clampi(value, -1, 1);
            if (sp != t->p_speed) { t->p_speed = sp; reanchor(e, t); }
        }
        else if (strcmp(move, "reverse") == 0) t->p_reverse = (uint8_t)(value != 0);
        else if (strcmp(move, "freeze") == 0) {
            int n = clampi(value, 0, 64);
            if (n > 0 && t->p_freeze == 0) t->freeze_s0 = t->last_s - n + 1;
            t->p_freeze = (uint8_t)n;
        }
        else if (strcmp(move, "thin") == 0) t->p_thin = (uint8_t)(value != 0);
        else if (strcmp(move, "accent") == 0) t->p_accent = (uint8_t)(value != 0);
        else if (strcmp(move, "restart") == 0) { t->step_off = t->next_k; t->last_s = -1; t->p_freeze = 0; }
        else if (strcmp(move, "rotate") == 0) {
            int steps = t->steps < 1 ? 1 : t->steps;
            t->rotate = (uint8_t)mod64(t->rotate + value, steps);
        }
        else if (strcmp(move, "clear") == 0) {
            t->p_fill = t->p_ratchet = t->p_reverse = t->p_freeze = t->p_thin = t->p_accent = 0;
            t->rat_left = 0;
            if (t->p_speed) { t->p_speed = 0; reanchor(e, t); }
        }
    }
    if (strcmp(move, "clear") == 0) for (int i = 0; i < E1_TRACKS; i++) e->t[i].p_solo_mute = 0;
}

/* --------------------------------------------------------------- advance */

static void cc_send(e1_track_t *t, int lane, int value, int ports, e1_emit_fn emit, void *user)
{
    value = clampi(value, 0, 127);
    emit(user, ports, (uint8_t)(0xB0 | (t->channel & 0x0F)), (uint8_t)(t->cc_num[lane] & 0x7F), (uint8_t)value);
    t->cc_sent[lane] = (int16_t)value;
}

static void note_on(e1_engine_t *e, e1_track_t *t, double when, double len, int vel,
                    e1_emit_fn emit, void *user)
{
    note_off(t, emit, user);
    int ports = e1_ports(e, t);
    uint8_t ch = (uint8_t)(t->channel & 0x0F);
    uint8_t note = t->note & 0x7F;
    emit(user, ports, (uint8_t)(0x90 | ch), note, (uint8_t)clampi(vel, 1, 127));
    t->gate_open = 1;
    t->gate_note = note;
    t->gate_ch = ch;
    t->gate_ports = (uint8_t)ports;
    t->gate_off = when + (len < 1.0 ? 1.0 : len);
    t->flash = 1;
}

static void fire_slot(e1_engine_t *e, int ti, e1_track_t *t, int64_t k, double when,
                      e1_emit_fn emit, void *user)
{
    int64_t s = k - t->step_off;
    t->last_s = s;
    t->rat_left = 0;
    int64_t se = effective_s(t, s);
    int step = slot_step(t, se);
    t->cur_step = step;
    if (!e1_hit(t, step, t->p_fill)) return;
    if (t->prob < 100 && e1_chance(e->seed, ti, se) >= t->prob) return;
    if (t->p_thin && e1_chance(e->seed ^ 0xA5A5A5A5u, ti, se) < 50) return;
    if (t->muted || t->p_solo_mute) return;

    if (t->type == E1_TYPE_CC) {
        int ports = e1_ports(e, t);
        t->flash = 1;
        for (int l = 0; l < E1_CC_LANES; l++) {
            if (t->cc_val[l] < 0) continue;
            if (t->cc_mode == E1_CC_ON_PULSE) cc_send(t, l, t->cc_val[l], ports, emit, user);
            else if (t->cc_mode == E1_CC_GLIDE) t->cc_target[l] = t->cc_val[l];
        }
        return;
    }

    double st = slot_ticks(t);
    double len = st * (double)t->sustain / 50.0;
    int vel = t->p_accent ? 127 : t->velocity;
    if (t->p_ratchet >= 2) {
        double dt = st / (double)t->p_ratchet;
        if (len > dt * 0.8) len = dt * 0.8;
        t->rat_left = (uint8_t)(t->p_ratchet - 1);
        t->rat_dt = dt;
        t->rat_next = when + dt;
        t->rat_len = len;
        t->rat_vel = (uint8_t)clampi(vel, 1, 127);
    }
    note_on(e, t, when, len, vel, emit, user);
}

/* Live CC edits and glides. Bounded per call so a burst of edits cannot
 * flood Move's injection queue. */
static void cc_service(e1_engine_t *e, e1_emit_fn emit, void *user)
{
    int budget = 8;
    for (int n = 0; n < E1_TRACKS && budget > 0; n++) {
        int ti = (int)((e->cc_rr + (uint32_t)n) % E1_TRACKS);
        e1_track_t *t = &e->t[ti];
        if (t->type != E1_TYPE_CC) continue;
        int ports = e1_ports(e, t);
        int silent = t->muted || t->p_solo_mute;
        for (int l = 0; l < E1_CC_LANES && budget > 0; l++) {
            if (t->cc_val[l] < 0) { t->cc_dirty &= ~(1u << l); continue; }
            if (t->cc_dirty & (1u << l)) {
                t->cc_dirty &= ~(1u << l);
                if (t->cc_mode == E1_CC_GLIDE && t->slew > 0) {
                    t->cc_target[l] = t->cc_val[l];
                } else {
                    if (!silent) { cc_send(t, l, t->cc_val[l], ports, emit, user); budget--; }
                    t->cc_cur[l] = (float)t->cc_val[l];
                }
            }
            if (t->cc_mode == E1_CC_GLIDE && t->cc_target[l] >= 0) {
                float tgt = (float)t->cc_target[l];
                float rate = 128.0f / (1.0f + (float)t->slew * 4.0f);   /* units per call */
                float cur = t->cc_cur[l];
                if (cur < tgt) { cur += rate; if (cur > tgt) cur = tgt; }
                else if (cur > tgt) { cur -= rate; if (cur < tgt) cur = tgt; }
                t->cc_cur[l] = cur;
                int iv = (int)lrintf(cur);
                if (iv != t->cc_sent[l] && !silent) { cc_send(t, l, iv, ports, emit, user); budget--; }
                if (cur == tgt) t->cc_target[l] = -1;
            }
        }
    }
    e->cc_rr = (e->cc_rr + 1) % E1_TRACKS;
}

void e1_advance(e1_engine_t *e, double tick_now, e1_emit_fn emit, void *user)
{
    cc_service(e, emit, user);
    if (!e->running) return;
    if (tick_now < e->tick) tick_now = e->tick;   /* never run time backwards */
    e->tick = tick_now;

    for (int ti = 0; ti < E1_TRACKS; ti++) {
        e1_track_t *t = &e->t[ti];
        int guard = 0;
        {
            /* Joined late or the clock jumped: start from "now" instead of
             * replaying every missed step in one block. */
            double st = slot_ticks(t);
            if (tick_now - slot_time(e, t, t->next_k, e->origin) > 2.0 * E1_TICKS_PER_BAR) {
                int64_t k = (int64_t)floor((tick_now - e->origin) / st);
                if (k > t->next_k) { t->step_off += k - t->next_k; t->next_k = k; }
                t->rat_left = 0;
            }
        }
        for (;;) {
            double when = slot_time(e, t, t->next_k, e->origin);
            if (t->rat_left && t->rat_next < when && t->rat_next <= tick_now) {
                if (t->gate_open && t->gate_off <= t->rat_next) note_off(t, emit, user);
                if (!t->muted && !t->p_solo_mute)
                    note_on(e, t, t->rat_next, t->rat_len, t->rat_vel, emit, user);
                t->rat_next += t->rat_dt;
                t->rat_left--;
                continue;
            }
            if (when > tick_now) break;
            if (t->gate_open && t->gate_off <= when) note_off(t, emit, user);
            fire_slot(e, ti, t, t->next_k, when, emit, user);
            t->next_k++;
            if (++guard > 256) {
                double st = slot_ticks(t);
                int64_t k = (int64_t)floor((tick_now - e->origin) / st) + 1;
                if (k > t->next_k) { t->step_off += k - t->next_k; t->next_k = k; }
                break;
            }
        }
        if (t->gate_open && t->gate_off <= tick_now) note_off(t, emit, user);
    }
}

/* ----------------------------------------------------------- parameters */

typedef enum { P_U8, P_I8 } ptype_t;

typedef struct {
    const char *key;
    size_t off;
    ptype_t pt;
    int lo, hi;              /* API range (channel is 1..16 at the API) */
    const char *const *names;/* enum option names, or NULL */
    int api_bias;            /* api = stored + bias */
} pdesc_t;

#define TOFF(f) offsetof(e1_track_t, f)

static const pdesc_t TRACK_PARAMS[] = {
    { "type",     TOFF(type),      P_U8, 0, 1,   TYPE_NAMES,   0 },
    { "steps",    TOFF(steps),     P_U8, 1, 64,  NULL,         0 },
    { "pulses",   TOFF(pulses),    P_U8, 0, 64,  NULL,         0 },
    { "rotate",   TOFF(rotate),    P_U8, 0, 63,  NULL,         0 },
    { "division", TOFF(division),  P_U8, 0, E1_DIVISIONS - 1, e1_division_names, 0 },
    { "length",   TOFF(length),    P_U8, 0, E1_LENGTHS - 1,   e1_length_names,   0 },
    { "cycles",   TOFF(cycles),    P_U8, 1, 16,  NULL,         0 },
    { "prob",     TOFF(prob),      P_U8, 0, 100, NULL,         0 },
    { "fill",     TOFF(fill),      P_U8, 0, 16,  NULL,         0 },
    { "channel",  TOFF(channel),   P_U8, 1, 16,  NULL,         1 },
    { "output",   TOFF(output),    P_U8, 0, 3,   OUT_NAMES,    0 },
    { "notemode", TOFF(note_mode), P_U8, 0, 1,   NMODE_NAMES,  0 },
    { "note",     TOFF(note),      P_U8, 0, 127, NULL,         0 },
    { "vel",      TOFF(velocity),  P_U8, 1, 127, NULL,         0 },
    { "sustain",  TOFF(sustain),   P_U8, 1, 100, NULL,         0 },
    { "timing",   TOFF(timing),    P_I8, -50, 50, NULL,        0 },
    { "delay",    TOFF(delay),     P_I8, -50, 50, NULL,        0 },
    { "mute",     TOFF(muted),     P_U8, 0, 1,   ONOFF_NAMES,  0 },
    { "ccmode",   TOFF(cc_mode),   P_U8, 0, 2,   CCMODE_NAMES, 0 },
    { "slew",     TOFF(slew),      P_U8, 0, 100, NULL,         0 },
};
#define N_TRACK_PARAMS ((int)(sizeof(TRACK_PARAMS) / sizeof(TRACK_PARAMS[0])))

static const pdesc_t *find_param(const char *key)
{
    for (int i = 0; i < N_TRACK_PARAMS; i++)
        if (strcmp(TRACK_PARAMS[i].key, key) == 0) return &TRACK_PARAMS[i];
    return NULL;
}

static int read_stored(const void *base, const pdesc_t *d)
{
    const uint8_t *p = (const uint8_t *)base + d->off;
    return d->pt == P_I8 ? (int)*(const int8_t *)p : (int)*p;
}

static void write_stored(void *base, const pdesc_t *d, int v)
{
    uint8_t *p = (uint8_t *)base + d->off;
    if (d->pt == P_I8) *(int8_t *)p = (int8_t)v;
    else *p = (uint8_t)v;
}

static int enum_count(const pdesc_t *d) { return d->hi - d->lo + 1; }

/* Parse `val` against a current API value. Returns 1 and sets *out. */
static int parse_value(const char *val, int cur, int lo, int hi,
                       const char *const *names, int *out)
{
    if (!val || !*val) return 0;
    if (val[0] == 'd' && (val[1] == '+' || val[1] == '-')) {
        *out = clampi(cur + atoi(val + 1), lo, hi);
        return 1;
    }
    if (names) {
        for (int i = 0; i <= hi - lo; i++)
            if (strcmp(names[i], val) == 0) { *out = lo + i; return 1; }
    }
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val) return 0;
    *out = clampi((int)v, lo, hi);
    return 1;
}

static int parse_lane(const char *key, const char *prefix)
{
    size_t n = strlen(prefix);
    if (strncmp(key, prefix, n) != 0) return -1;
    const char *p = key + n;
    if (!*p) return -1;
    for (const char *q = p; *q; q++) if (*q < '0' || *q > '9') return -1;
    int lane = atoi(p) - 1;
    return (lane >= 0 && lane < E1_CC_LANES) ? lane : -1;
}

static int set_track(e1_engine_t *e, e1_track_t *t, const char *key, const char *val)
{
    int lane;
    if ((lane = parse_lane(key, "cc")) >= 0) {
        int v;
        if (!parse_value(val, t->cc_val[lane], -1, 127, NULL, &v)) return 0;
        t->cc_val[lane] = (int8_t)v;
        if (v >= 0) t->cc_dirty |= (1u << lane);
        return 1;
    }
    if ((lane = parse_lane(key, "ccn")) >= 0) {
        int v;
        if (!parse_value(val, t->cc_num[lane], 0, 127, NULL, &v)) return 0;
        t->cc_num[lane] = (uint8_t)v;
        if (t->cc_val[lane] >= 0) t->cc_dirty |= (1u << lane);
        return 1;
    }
    if (strcmp(key, "toggle") == 0) { e1_toggle_step(t, atoi(val)); return 1; }
    if (strcmp(key, "addmask") == 0) { t->add_mask = strtoull(val, NULL, 16); return 1; }
    if (strcmp(key, "delmask") == 0) { t->del_mask = strtoull(val, NULL, 16); return 1; }
    if (strcmp(key, "clearedits") == 0) { t->add_mask = t->del_mask = 0; return 1; }

    const pdesc_t *d = find_param(key);
    if (!d) return 0;
    int hi = d->hi;
    if (strcmp(key, "pulses") == 0) hi = t->steps;
    if (strcmp(key, "rotate") == 0) hi = t->steps > 0 ? t->steps - 1 : 0;
    int cur = read_stored(t, d) + d->api_bias;
    int v;
    if (!parse_value(val, cur, d->lo, hi, d->names, &v)) return 0;
    int old_div = t->division;
    int old_mute = t->muted;
    write_stored(t, d, v - d->api_bias);

    if (strcmp(key, "steps") == 0) {
        if (t->pulses > t->steps) t->pulses = t->steps;
        if (t->rotate >= t->steps) t->rotate = (uint8_t)(t->steps - 1);
        uint64_t keep = t->steps >= 64 ? ~0ULL : ((1ULL << t->steps) - 1);
        t->add_mask &= keep; t->del_mask &= keep;
    } else if (strcmp(key, "pulses") == 0) {
        t->del_mask = 0;   /* removed generated hits come back, as on the T-1 */
    } else if (strcmp(key, "division") == 0 && old_div != t->division) {
        reanchor(e, t);
    } else if (strcmp(key, "type") == 0) {
        t->gate_open = 0;
    }
    (void)old_mute;
    return 1;
}

static int set_global(e1_engine_t *e, const char *key, const char *val)
{
    int v;
    if (strcmp(key, "dest") == 0) {
        if (!parse_value(val, e->dest, 0, 2, DEST_NAMES, &v)) return 0;
        e->dest = (uint8_t)v; return 1;
    }
    if (strcmp(key, "seed") == 0) { e->seed = (uint32_t)strtoul(val, NULL, 10); return 1; }
    if (strcmp(key, "fillheld") == 0) { e1_perf(e, 0xFFFF, "fill", atoi(val)); return 1; }
    return 0;
}

int e1_set(e1_engine_t *e, int track, const char *key, const char *val)
{
    if (!key || !val) return 0;
    if (track < 0) return set_global(e, key, val);
    if (track >= E1_TRACKS) return 0;
    e1_track_t *t = &e->t[track];
    int was_muted = t->muted;
    int ok = set_track(e, t, key, val);
    (void)was_muted;
    return ok;
}

int e1_get(const e1_engine_t *e, int track, const char *key, char *buf, int len)
{
    if (!key || !buf || len <= 0) return -1;
    if (track < 0) {
        if (strcmp(key, "dest") == 0) return snprintf(buf, (size_t)len, "%s", DEST_NAMES[e->dest % 3]);
        if (strcmp(key, "seed") == 0) return snprintf(buf, (size_t)len, "%u", (unsigned)e->seed);
        return -1;
    }
    if (track >= E1_TRACKS) return -1;
    const e1_track_t *t = &e->t[track];
    int lane;
    if ((lane = parse_lane(key, "cc")) >= 0) return snprintf(buf, (size_t)len, "%d", t->cc_val[lane]);
    if ((lane = parse_lane(key, "ccn")) >= 0) return snprintf(buf, (size_t)len, "%d", t->cc_num[lane]);
    if (strcmp(key, "hits") == 0)
        return snprintf(buf, (size_t)len, "%llx", (unsigned long long)e1_hit_mask(t, 0));
    if (strcmp(key, "addmask") == 0) return snprintf(buf, (size_t)len, "%llx", (unsigned long long)t->add_mask);
    if (strcmp(key, "delmask") == 0) return snprintf(buf, (size_t)len, "%llx", (unsigned long long)t->del_mask);
    const pdesc_t *d = find_param(key);
    if (!d) return -1;
    int v = read_stored(t, d) + d->api_bias;
    if (d->names) {
        int idx = v - d->lo;
        if (idx < 0 || idx >= enum_count(d)) idx = 0;
        return snprintf(buf, (size_t)len, "%s", d->names[idx]);
    }
    return snprintf(buf, (size_t)len, "%d", v);
}

int e1_param_count(void) { return N_TRACK_PARAMS; }
const char *e1_param_key(int i) { return (i >= 0 && i < N_TRACK_PARAMS) ? TRACK_PARAMS[i].key : NULL; }

int e1_get_num(const e1_engine_t *e, int track, int i)
{
    if (track < 0 || track >= E1_TRACKS || i < 0 || i >= N_TRACK_PARAMS) return 0;
    return read_stored(&e->t[track], &TRACK_PARAMS[i]) + TRACK_PARAMS[i].api_bias;
}

/* ---------------------------------------------------------- persistence */

static int appendf(char *buf, int len, int pos, const char *fmt, const char *k, const char *v)
{
    if (pos >= len) return pos;
    int n = snprintf(buf + pos, (size_t)(len - pos), fmt, k, v);
    if (n < 0) return pos;
    return pos + n;
}

int e1_dump_track(const e1_engine_t *e, int track, char *buf, int len)
{
    if (track < 0 || track >= E1_TRACKS || len <= 0) return 0;
    char v[32];
    int pos = 0;
    buf[0] = 0;
    for (int i = 0; i < N_TRACK_PARAMS; i++) {
        e1_get(e, track, TRACK_PARAMS[i].key, v, sizeof v);
        pos = appendf(buf, len, pos, "%s=%s\n", TRACK_PARAMS[i].key, v);
    }
    char k[16];
    for (int l = 0; l < E1_CC_LANES; l++) {
        snprintf(k, sizeof k, "cc%d", l + 1);  e1_get(e, track, k, v, sizeof v);
        pos = appendf(buf, len, pos, "%s=%s\n", k, v);
        snprintf(k, sizeof k, "ccn%d", l + 1); e1_get(e, track, k, v, sizeof v);
        pos = appendf(buf, len, pos, "%s=%s\n", k, v);
    }
    e1_get(e, track, "addmask", v, sizeof v); pos = appendf(buf, len, pos, "%s=%s\n", "addmask", v);
    e1_get(e, track, "delmask", v, sizeof v); pos = appendf(buf, len, pos, "%s=%s\n", "delmask", v);
    return pos < len ? pos : len - 1;
}

int e1_dump_global(const e1_engine_t *e, char *buf, int len)
{
    if (len <= 0) return 0;
    return snprintf(buf, (size_t)len, "dest=%s\nseed=%u\n", DEST_NAMES[e->dest % 3], (unsigned)e->seed);
}

void e1_load_lines(e1_engine_t *e, int track, const char *text)
{
    if (!text) return;
    char line[96];
    const char *p = text;
    /* Steps first, so pulses and rotate are clamped against the right length. */
    for (int pass = 0; pass < 2; pass++) {
        p = text;
        while (*p) {
            const char *nl = strchr(p, '\n');
            size_t n = nl ? (size_t)(nl - p) : strlen(p);
            if (n >= sizeof line) n = sizeof line - 1;
            memcpy(line, p, n); line[n] = 0;
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = 0;
                int is_steps = strcmp(line, "steps") == 0;
                if ((pass == 0) == is_steps) e1_set(e, track, line, eq + 1);
            }
            if (!nl) break;
            p = nl + 1;
        }
    }
    if (track >= 0 && track < E1_TRACKS) e->t[track].cc_dirty = 0;
}
