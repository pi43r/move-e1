/*
 * e1_engine.c — Move E-1 sequencing engine. See e1_engine.h.
 */
#include "e1_engine.h"

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
const int e1_length_bars[E1_LENGTHS] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 16 };
const int e1_loop_bars[E1_LOOPS] = { 0, 1, 2, 4, 8 };

static const char *const LENGTH_NAMES[] = { "Off", "1", "2", "3", "4", "5", "6", "7", "8", "16" };
static const char *const TYPE_NAMES[]   = { "Note", "CC" };
static const char *const OUT_NAMES[]    = { "Global", "Move", "Computer", "Both" };
static const char *const DEST_NAMES[]   = { "Move", "Computer", "Both" };
static const char *const CCMODE_NAMES[] = { "Pulse", "Change", "Glide" };
static const char *const ONOFF_NAMES[]  = { "Off", "On" };
static const char *const STYLE_NAMES[]  = { "PolyFix", "PolyRamp", "PolyClimb", "MonoFix", "MonoRamp", "MonoClimb", "Dice" };
static const char *const PHRASE_NAMES[] = { "Cad1", "Cad2", "Cad3", "Cad4", "Saw", "Tri", "Sine", "Pulse", "Dice" };
/* "Song" is value -1 in both lists: the track follows the set. */
static const char *const SCALE_NAMES[]  = { "Song", "Chrom", "Major", "Minor", "Penta", "Hira", "Iwato", "Tetra", "Dorian",
                                            "Phryg", "Lydian", "Mixo", "Locri", "HarMin", "MelMin", "Blues", "MinPen", "Whole" };
static const char *const ROOT_NAMES[]   = { "Song", "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const FOLLOW_NAMES[] = { "Off", "Bass", "Chord", "Arp" };
static const char *const GROOVE_NAMES[] = { "Agogo", "Congas", "Timbal", "Bongo", "Saw", "Tri", "Sine", "Pulse", "Dice" };
static const char *const GRID_NAMES[]   = { "x1", "x2", "x4" };
static const char *const DIR_NAMES[]    = { "Forward", "Backward", "Pendulum", "Random" };
static const char *const SOURCE_NAMES[] = { "Random", "Drift", "RampUp", "RampDn", "Tri", "Square" };
static const char *const TAIL_NAMES[]   = { "Choke", "Tail" };
static const char *const PRATE_NAMES[]  = { "x1", "x2", "/2" };
static const char *const LOOP_NAMES[]   = { "Free", "1", "2", "4", "8" };
static const char *const AUTO_NAMES[]   = { "Off", "1", "2", "4", "8" };
static const char *const DICE_NAMES[E1_DICE_LAYERS] = { "notes", "accents", "lengths", "octaves", "rhythm" };

static const int8_t SCALE_DEG[E1_SCALES][12] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
    { 0, 2, 4, 5, 7, 9, 11 },
    { 0, 2, 3, 5, 7, 8, 10 },
    { 0, 2, 4, 7, 9 },
    { 0, 2, 3, 7, 8 },
    { 0, 1, 5, 6, 10 },
    { 0, 4, 7, 10 },
    { 0, 2, 3, 5, 7, 9, 10 },
    { 0, 1, 3, 5, 7, 8, 10 },      /* Phrygian */
    { 0, 2, 4, 6, 7, 9, 11 },      /* Lydian */
    { 0, 2, 4, 5, 7, 9, 10 },      /* Mixolydian */
    { 0, 1, 3, 5, 6, 8, 10 },      /* Locrian */
    { 0, 2, 3, 5, 7, 8, 11 },      /* Harmonic minor */
    { 0, 2, 3, 5, 7, 9, 11 },      /* Melodic minor */
    { 0, 3, 5, 6, 7, 10 },         /* Blues */
    { 0, 3, 5, 7, 10 },            /* Minor pentatonic */
    { 0, 2, 4, 6, 8, 10 },         /* Whole tone */
};
static const int8_t SCALE_LEN[E1_SCALES] = { 12, 7, 7, 5, 5, 5, 4, 7, 7, 7, 7, 7, 7, 7, 6, 5, 6 };

/* Velocity shapes, one value per step of a 16-step bar, in tenths (-10..10). */
static const int8_t GROOVE_SHAPE[4][16] = {
    { 10, -4, 0, -6,  6, -4, 2, -6,  10, -4, 0, -6,  6, -2, 8, -4 },   /* Agogo */
    { 8, -6, -2, 6,  -4, 4, 10, -6,  6, -6, -2, 8,  -4, 2, 10, -2 },   /* Congas */
    { 10, 0, -8, 4,  0, -8, 8, 0,  -8, 6, 0, -6,  10, -4, 4, -8 },     /* Timbales */
    { 6, 2, -6, 10,  -6, 2, 6, -8,  4, 10, -6, 2,  -4, 8, -8, 2 },     /* Bongo */
};

/* Phrase cadences: eight segments per phrase, as tenths of Range. */
static const int8_t CADENCE[4][8] = {
    { 0, 0, 5, 5, 10, 10, 5, 5 },
    { 0, 5, 0, 10, 0, 5, 10, 5 },
    { 0, 0, 0, 10, 0, 0, 5, 10 },
    { 10, 7, 5, 2, 0, 2, 5, 7 },
};

/* ------------------------------------------------------------ parameters */

enum { F_CYC = 1, F_RC = 2, F_RR = 4, F_STEP = 8 };   /* lockable; random per cycle; random per rate step */

typedef struct {
    const char *key;
    int16_t lo, hi, def;
    const char *const *names;
    uint8_t flags;
    uint8_t span;          /* how far a random amount of 100 can move it */
} pdesc_t;

static const pdesc_t P[E1_NP] = {
    [E1_P_TYPE]       = { "type",       0, 1, 0,     TYPE_NAMES,   0, 0 },
    [E1_P_STEPS]      = { "steps",      1, 64, 16,   NULL,         F_CYC | F_RC, 8 },
    [E1_P_PULSES]     = { "pulses",     0, 64, 4,    NULL,         F_CYC | F_RC, 6 },
    [E1_P_ROTATE]     = { "rotate",     0, 63, 0,    NULL,         F_CYC | F_RC, 8 },
    [E1_P_DIVISION]   = { "division",   0, E1_DIVISIONS - 1, 4, e1_division_names, F_CYC, 0 },
    [E1_P_LENGTH]     = { "length",     0, E1_LENGTHS - 1, 0,   LENGTH_NAMES, 0, 0 },
    [E1_P_CYCLES]     = { "cycles",     1, 16, 4,    NULL,         0, 0 },
    [E1_P_VELOCITY]   = { "velocity",   1, 127, 100, NULL,         F_STEP | F_CYC | F_RR, 64 },
    [E1_P_PROB]       = { "prob",       -100, 100, 0, NULL,        F_STEP | F_CYC | F_RR, 100 },
    [E1_P_SUSTAIN]    = { "sustain",    1, 100, 25,  NULL,         F_STEP | F_CYC | F_RR, 50 },
    [E1_P_REPEATS]    = { "repeats",    0, E1_REPEATS_INF, 0, NULL, F_STEP | F_CYC | F_RR, 6 },
    [E1_P_TIME]       = { "time",       0, E1_DIVISIONS - 1, 5, e1_division_names, F_STEP | F_CYC, 0 },
    [E1_P_RAMP]       = { "ramp",       -100, 100, 0, NULL,        F_STEP | F_CYC, 0 },
    [E1_P_PACE]       = { "pace",       -100, 100, 0, NULL,        F_STEP | F_CYC, 0 },
    [E1_P_FILL]       = { "fill",       0, 16, 2,    NULL,         F_CYC, 0 },
    [E1_P_DELAY]      = { "delay",      -50, 50, 0,  NULL,         0, 0 },
    [E1_P_PITCH]      = { "pitch",      -36, 36, 0,  NULL,         F_STEP | F_CYC | F_RC, 7 },
    [E1_P_HARMONY]    = { "harmony",    -16, 16, 0,  NULL,         F_STEP | F_CYC | F_RC, 4 },
    [E1_P_VOICING]    = { "voicing",    -16, 16, 0,  NULL,         F_STEP | F_CYC | F_RR, 4 },
    [E1_P_STYLE]      = { "style",      0, E1_STYLE_DICE, 0, STYLE_NAMES,  F_STEP | F_CYC, 0 },
    [E1_P_RANGE]      = { "range",      -16, 16, 0,  NULL,         F_STEP | F_CYC | F_RR, 7 },
    [E1_P_PHRASE]     = { "phrase",     0, E1_PHRASE_DICE, 0, PHRASE_NAMES, F_STEP | F_CYC, 0 },
    [E1_P_SCALE]      = { "scale",      E1_SONG, E1_SCALES - 1, 0, SCALE_NAMES, F_STEP | F_CYC, 0 },
    [E1_P_ROOT]       = { "root",       E1_SONG, 11, 0, ROOT_NAMES,   F_STEP | F_CYC, 0 },
    [E1_P_ACCENT]     = { "accent",     -100, 100, 0, NULL,        F_STEP | F_CYC | F_RR, 100 },
    [E1_P_GROOVE]     = { "groove",     0, E1_GROOVE_DICE, 0, GROOVE_NAMES, F_STEP | F_CYC, 0 },
    [E1_P_TIMING]     = { "timing",     -50, 50, 0,  NULL,         F_CYC | F_RR, 50 },
    [E1_P_SWINGGRID]  = { "swinggrid",  0, 2, 0,     GRID_NAMES,   0, 0 },
    [E1_P_RANDOM]     = { "random",     0, 100, 100, NULL,         0, 0 },
    [E1_P_RATE]       = { "rate",       0, E1_DIVISIONS - 1, 4, e1_division_names, 0, 0 },
    [E1_P_CHANNEL]    = { "channel",    1, 16, 1,    NULL,         0, 0 },
    [E1_P_OUTPUT]     = { "output",     0, 3, 0,     OUT_NAMES,    0, 0 },
    [E1_P_MUTE]       = { "mute",       0, 1, 0,     ONOFF_NAMES,  0, 0 },
    [E1_P_DIR]        = { "dir",        0, 3, 0,     DIR_NAMES,    F_CYC, 0 },
    [E1_P_TAIL]       = { "tail",       0, 1, 0,     TAIL_NAMES,   0, 0 },
    [E1_P_PHRASERATE] = { "phraserate", 0, 2, 0,     PRATE_NAMES,  0, 0 },
    [E1_P_CCMODE]     = { "ccmode",     0, 2, 0,     CCMODE_NAMES, 0, 0 },
    [E1_P_SLEW]       = { "slew",       0, 100, 0,   NULL,         0, 0 },
    [E1_P_LOOP]       = { "loop",       0, E1_LOOPS - 1, 0, LOOP_NAMES, 0, 0 },
    [E1_P_DENSITY]    = { "density",    0, 16, 16,   NULL,         F_CYC, 0 },
    [E1_P_DICETYPE]   = { "dicetype",   1, 16, 8,    NULL,         0, 0 },
    [E1_P_AUTOROLL]   = { "autoroll",   0, E1_LOOPS - 1, 0, AUTO_NAMES, 0, 0 },
    [E1_P_DICELEN]    = { "dicelen",    0, 1, 0,     ONOFF_NAMES,  0, 0 },
    [E1_P_FOLLOW]     = { "follow",     0, 3, 0,     FOLLOW_NAMES, 0, 0 },
    [E1_P_SPLIT]      = { "split",      0, E1_TRACKS, 0, NULL,     0, 0 },
    [E1_P_SOURCE]     = { "source",     0, E1_SOURCES - 1, 0, SOURCE_NAMES, 0, 0 },
    [E1_P_FIT]        = { "fit",        0, 1, 0,     ONOFF_NAMES,  0, 0 },
    [E1_P_WARP]       = { "warp",       -100, 100, 0, NULL,        F_CYC, 0 },
    [E1_P_JITTER]     = { "jitter",     0, 100, 0,   NULL,         0, 0 },
};

int e1_param_count(void) { return E1_NP; }
const char *e1_param_key(int i) { return (i >= 0 && i < E1_NP) ? P[i].key : ""; }

static int find_param(const char *key)
{
    for (int i = 0; i < E1_NP; i++) if (strcmp(P[i].key, key) == 0) return i;
    return -1;
}

/* ------------------------------------------------------------- utilities */

static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint64_t mix(uint64_t x)
{
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x;
}

int e1_chance(uint32_t seed, int track, int64_t k)
{
    return (int)(mix(((uint64_t)seed << 32) ^ ((uint64_t)(uint32_t)track * 0x9E3779B97F4A7C15ULL) ^ (uint64_t)k) % 100ULL);
}

/* The seed a track draws from: the set's, stirred by the track's own. */
static uint32_t tseed(const e1_engine_t *e, const e1_track_t *t)
{
    return e->seed + t->rseed * 0x9E3779B1u;
}

static int lowest_bit(uint32_t m) { for (int i = 0; i < 16; i++) if (m & (1u << i)) return i; return -1; }

/* The value parameter p has in cycle c. `ridx` is the random step for
 * rate-driven random, or -1 to leave that out (used for display). */
static int sed_find(const e1_track_t *t, int step)
{
    if (step < 0) return -1;
    for (int i = 0; i < E1_SEDITS; i++) if (t->sed_step[i] == step + 1) return i;
    return -1;
}

static int sed_get(e1_track_t *t, int step)        /* find or claim a row */
{
    int i = sed_find(t, step);
    if (i >= 0) return i;
    for (i = 0; i < E1_SEDITS; i++) if (!t->sed_step[i]) {
        memset(t->sed[i], 0, sizeof t->sed[i]);
        t->sed_step[i] = (uint8_t)(step + 1);
        return i;
    }
    return -1;
}

uint64_t e1_steps_edited(const e1_track_t *t)
{
    uint64_t m = 0;
    for (int i = 0; i < E1_SEDITS; i++) {
        if (!t->sed_step[i]) continue;
        for (int p = 0; p < E1_NP; p++) if (t->sed[i][p]) { m |= 1ULL << (t->sed_step[i] - 1); break; }
    }
    return m;
}

/* ---------------------------------------------------------- shapes */

static int64_t shape_period(const e1_engine_t *e, int ti);

/* 0..1 for Source `src` at `pos` in a period of P. Drift is a smooth walk
 * through four random points per period; looped, the fourth leads back to
 * the first so the walk comes round. */
static double shape_u(uint32_t seed, int salt, int src, double pos, double P, int wrap)
{
    if (P <= 0) P = 1;
    double ph = fmod(pos, P) / P;
    if (ph < 0) ph += 1.0;
    switch (src) {
    case E1_SRC_DRIFT: {
        double x = pos / P * 4.0;
        int64_t i = (int64_t)floor(x);
        double f = x - (double)i;
        int64_t i0 = wrap ? ((i % 4) + 4) % 4 : i, i1 = wrap ? (i0 + 1) % 4 : i + 1;
        double a = (double)(mix(((uint64_t)seed << 32) ^ ((uint64_t)salt << 20) ^ 0xD71F7000ULL ^ (uint64_t)i0) % 1001ULL) / 1000.0;
        double b = (double)(mix(((uint64_t)seed << 32) ^ ((uint64_t)salt << 20) ^ 0xD71F7000ULL ^ (uint64_t)i1) % 1001ULL) / 1000.0;
        f = f * f * (3.0 - 2.0 * f);
        return a + (b - a) * f;
    }
    case E1_SRC_RAMPUP: return ph;
    case E1_SRC_RAMPDN: return 1.0 - ph;
    case E1_SRC_TRI:    return 1.0 - fabs(2.0 * ph - 1.0);
    case E1_SRC_SQUARE: return ph < 0.5 ? 1.0 : 0.0;
    default: return 0.0;
    }
}

/* A shape's value for a random amount. Per-cycle parameters (`per_cycle`)
 * follow it across the cycles, idx being the cycle; the rest at Rate, idx
 * being the random step, over the shape period. */
static double shape_at(const e1_engine_t *e, int ti, int salt, int idx, int per_cycle)
{
    const e1_track_t *t = &e->t[ti];
    if (per_cycle) {
        int nc = clampi(t->v[E1_P_CYCLES], 1, E1_CYCLES);
        return shape_u(tseed(e, t), salt, t->v[E1_P_SOURCE], (double)idx, (double)nc, 1);
    }
    double pos = (double)idx * (double)e1_division_ticks[clampi(t->v[E1_P_RATE], 0, E1_DIVISIONS - 1)];
    return shape_u(tseed(e, t), salt, t->v[E1_P_SOURCE], pos, (double)shape_period(e, ti), t->v[E1_P_LOOP] > 0);
}

/* As eff(), plus the step's own offset when the step carries one. */
static int eff_s(const e1_engine_t *e, int ti, int p, int c, int ridx, int step);

static int eff(const e1_engine_t *e, int ti, int p, int c, int ridx) { return eff_s(e, ti, p, c, ridx, -1); }

static int eff_s(const e1_engine_t *e, int ti, int p, int c, int ridx, int step)
{
    const e1_track_t *t = &e->t[ti];
    const pdesc_t *d = &P[p];
    int v = t->v[p];
    if ((d->flags & F_CYC) && c >= 0 && c < E1_CYCLES) v += t->cyc[c][p];
    if ((d->flags & F_STEP) && step >= 0) { int r = sed_find(t, step); if (r >= 0) v += t->sed[r][p]; }
    if (t->rnd[p] && t->v[E1_P_RANDOM] > 0 && t->v[E1_P_SOURCE] != E1_SRC_RANDOM) {
        int idx = (d->flags & F_RC) ? c : ((d->flags & F_RR) ? ridx : -1);
        if (idx >= 0)
            v += (int)lrint((double)t->rnd[p] / 100.0 * (double)d->span * shape_at(e, ti, ti * 64 + p, idx, (d->flags & F_RC) != 0));
    } else if (t->rnd[p] && t->v[E1_P_RANDOM] > 0) {
        int idx = (d->flags & F_RC) ? c : ((d->flags & F_RR) ? ridx : -1);
        if (idx >= 0) {
            uint64_t h = mix(((uint64_t)tseed(e, t) << 32) ^ ((uint64_t)(ti * 64 + p) << 16) ^ (uint64_t)idx);
            if ((int)(h % 100ULL) < t->v[E1_P_RANDOM]) {
                double u = (double)((h >> 8) % 1001ULL) / 1000.0;
                v += (int)lrint((double)t->rnd[p] / 100.0 * (double)d->span * u);
            }
        }
    }
    return clampi(v, d->lo, d->hi);
}

/* What a track's Scale and Root mean once Song is looked up. */
static int rscale(const e1_engine_t *e, int v) { return v < 0 ? clampi(e->song_scale, 0, E1_SCALES - 1) : clampi(v, 0, E1_SCALES - 1); }
static int rroot(const e1_engine_t *e, int v) { return v < 0 ? e->song_root % 12 : clampi(v, 0, 11); }

static int div_ticks(int idx) { return e1_division_ticks[clampi(idx, 0, E1_DIVISIONS - 1)]; }

/* ------------------------------------------------------------- defaults */

static const char *const DEFAULT_NAMES[E1_TRACKS] = {
    "KICK", "SNARE", "HAT", "CLAP", "PERC", "TOM", "RIM", "SHAKE",
    "BASS", "CHORD", "LEAD", "CC 1", "CC 2", "CC 3", "CC 4", "CC 5"
};

static void runtime_reset(e1_track_t *t)
{
    memset((char *)t + E1_TRACK_STORED_BYTES, 0, sizeof(*t) - E1_TRACK_STORED_BYTES);
    t->cur_step = -1;
    for (int i = 0; i < E1_CC_LANES; i++) { t->cc_sent[i] = -1; t->cc_target[i] = -1; }
}

void e1_track_defaults(e1_track_t *t, int index)
{
    memset(t, 0, sizeof(*t));
    for (int p = 0; p < E1_NP; p++) t->v[p] = P[p].def;
    t->pool[0] = (uint8_t)clampi(36 + index, 0, 127);
    t->pool_n = 1;
    for (int i = 0; i < E1_CC_LANES; i++) {
        t->cc_val[i] = -1;
        t->cc_num[i] = (uint8_t)(i < 14 ? 70 + i : (i == 14 ? 1 : 74));
    }
    snprintf(t->name, sizeof t->name, "%s", DEFAULT_NAMES[index >= 0 && index < E1_TRACKS ? index : 0]);
    runtime_reset(t);
}

/* The starter set: tracks 1-8 drums into a Move drum rack on channel 1,
 * 9-11 melodic on channels 2-4, 12-16 CC tracks on channels 5-9. */
void e1_init(e1_engine_t *e)
{
    memset(e, 0, sizeof(*e));
    static const uint8_t drum_pulses[8] = { 4, 2, 8, 3, 5, 3, 2, 7 };
    static const uint8_t drum_rot[8]    = { 0, 4, 0, 2, 1, 6, 3, 0 };
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &e->t[i];
        e1_track_defaults(t, i);
        if (i < 8) {
            t->v[E1_P_PULSES] = drum_pulses[i];
            t->v[E1_P_ROTATE] = drum_rot[i];
            t->v[E1_P_MUTE] = (i >= 4);
        } else if (i < 11) {
            t->v[E1_P_CHANNEL] = (int16_t)(i - 8 + 2);
            t->v[E1_P_SCALE] = E1_SONG;           /* the set's scale and root */
            t->v[E1_P_ROOT] = E1_SONG;
            t->v[E1_P_SUSTAIN] = 40;
            t->v[E1_P_MUTE] = 1;
            if (i == 8) { t->pool[0] = 36; t->pool_n = 1; t->v[E1_P_PULSES] = 5; }
            if (i == 9) {
                t->pool[0] = 60; t->pool[1] = 63; t->pool[2] = 67; t->pool_n = 3;
                t->v[E1_P_PULSES] = 3; t->v[E1_P_DIVISION] = 3; t->v[E1_P_SUSTAIN] = 60;
            }
            if (i == 10) {
                t->pool[0] = 72; t->pool[1] = 75; t->pool[2] = 79; t->pool_n = 3;
                t->v[E1_P_STEPS] = 12; t->v[E1_P_PULSES] = 7; t->v[E1_P_STYLE] = 4;   /* mono ramp */
                t->v[E1_P_VOICING] = 2;
            }
        } else {
            t->v[E1_P_TYPE] = E1_TYPE_CC;
            t->v[E1_P_CHANNEL] = (int16_t)(i - 11 + 5);
            t->v[E1_P_MUTE] = 1;
        }
    }
    e->dest = E1_DEST_MOVE;
    e->seed = 0xE1u;
    e->song_root = 0;
    e->song_scale = 2;                            /* C minor */
}

/* ---------------------------------------------------------------- euclid */

static int euclid_base(int steps, int pulses, int j)
{
    if (steps <= 0 || pulses <= 0) return 0;
    if (pulses >= steps) return 1;
    return ((j * pulses) % steps) < pulses;
}

/* Pattern-space index of step i: hand edits follow the pattern when it rotates. */
static int pattern_index(int i, int rotate, int steps)
{
    return ((i - rotate % steps) % steps + steps) % steps;
}

static int hit(const e1_engine_t *e, int ti, int c, int i, int fill)
{
    const e1_track_t *t = &e->t[ti];
    int steps = eff(e, ti, E1_P_STEPS, c, -1);
    if (i < 0 || i >= steps) return 0;
    int j = pattern_index(i, eff(e, ti, E1_P_ROTATE, c, -1), steps);
    int pulses = eff(e, ti, E1_P_PULSES, c, -1) + (fill ? eff(e, ti, E1_P_FILL, c, -1) : 0);
    if (pulses > steps) pulses = steps;
    int h = euclid_base(steps, pulses, j);
    uint64_t bit = 1ULL << j;
    if (t->del_mask & bit) h = 0;
    if (t->add_mask & bit) h = 1;
    if (c >= 0 && c < E1_CYCLES) {
        if (t->cdel[c] & bit) h = 0;
        if (t->cadd[c] & bit) h = 1;
    }
    return h;
}

uint64_t e1_hit_mask(const e1_engine_t *e, int track, int cycle, int fill)
{
    if (track < 0 || track >= E1_TRACKS) return 0;
    int steps = eff(e, track, E1_P_STEPS, cycle, -1);
    uint64_t m = 0;
    for (int i = 0; i < steps && i < 64; i++) if (hit(e, track, cycle, i, fill)) m |= 1ULL << i;
    return m;
}

/* ----------------------------------------------------------------- dice */

static uint64_t dice_h(uint32_t seed, int layer, int i)
{
    return mix(((uint64_t)seed << 32) ^ ((uint64_t)(0xD1CE00 + layer) << 8) ^ (uint64_t)(i & 0xFF));
}

/* How long a hit holds out when Density thins the track: higher stays
 * longer. Unrolled, the strong beats stay longest; rolled, the dice say. */
static uint32_t density_rank(const e1_track_t *t, int step)
{
    uint32_t seed = t->dseed[E1_DICE_RHYTHM];
    uint32_t r = (uint32_t)(dice_h(seed, E1_DICE_RHYTHM, step) & 0xFFFFF);
    if (seed) return r;
    int w = step == 0 ? 6 : !(step & 15) ? 5 : !(step & 7) ? 4 : !(step & 3) ? 3 : !(step & 1) ? 2 : 1;
    return ((uint32_t)w << 20) | r;
}

/* Does the hit on `step` survive Density? The same hits go every time
 * round, so a thinned pattern is still a pattern. */
static int dense(const e1_engine_t *e, int ti, int c, int step, int fill)
{
    const e1_track_t *t = &e->t[ti];
    int d = eff(e, ti, E1_P_DENSITY, c, -1);
    if (d >= 16) return 1;
    if (d <= 0) return 0;
    int steps = eff(e, ti, E1_P_STEPS, c, -1);
    uint32_t mine = density_rank(t, step);
    int n = 0, above = 0;
    for (int i = 0; i < steps && i < 64; i++) {
        if (!hit(e, ti, c, i, fill)) continue;
        n++;
        uint32_t r = density_rank(t, i);
        if (i != step && (r > mine || (r == mine && i < step))) above++;
    }
    int keep = (n * d + 15) / 16;
    return above < keep;
}

uint64_t e1_live_mask(const e1_engine_t *e, int track, int cycle, int fill)
{
    uint64_t m = e1_hit_mask(e, track, cycle, fill);
    if (!m || eff(e, track, E1_P_DENSITY, cycle, -1) >= 16) return m;
    for (int i = 0; i < 64; i++) if (((m >> i) & 1) && !dense(e, track, cycle, i, fill)) m &= ~(1ULL << i);
    return m;
}

/* Notes lane: where in the track's notes step i lands (0..len-1) and an
 * octave jump. Type steers it: low is wide, any note and octave jumps; high
 * is tight, close to the first note with notes said twice. The same seed
 * gives related lines across Type, so turning it morphs the line. */
static int dice_note(uint32_t seed, int type, int i, int len, int *oct)
{
    double w = (double)(clampi(type, 1, 16) - 1) / 15.0;
    double frac = 0.0;
    int o = 0;
    i &= 15;
    for (int j = 0; j <= i; j++) {
        uint64_t h = dice_h(seed, E1_DICE_NOTES, j);
        double u1 = (double)(h & 0xFFFF) / 65536.0, u2 = (double)((h >> 16) & 0xFFFF) / 65536.0;
        double u3 = (double)((h >> 32) & 0xFFFF) / 65536.0;
        if (j > 0 && u3 < 0.55 * w) continue;                  /* say the last note again */
        frac = pow(u1, 1.0 + 5.0 * w);
        o = u2 < 0.25 * (1.0 - w) ? 1 : (u2 > 1.0 - 0.12 * (1.0 - w) ? -1 : 0);
    }
    *oct = o;
    return clampi((int)(frac * (double)len), 0, len - 1);
}

static int dice_accent(uint32_t seed, int i)       /* tenths: strong, plain, ghost */
{
    int r = (int)(dice_h(seed, E1_DICE_ACCENTS, i & 15) % 100ULL);
    return r < 25 ? 10 : (r < 70 ? 0 : -8);
}
static int dice_length(uint32_t seed, int i)       /* 0 short, 1 medium, 2 long, 3 tied */
{
    int r = (int)(dice_h(seed, E1_DICE_LENGTHS, i & 15) % 100ULL);
    return r < 35 ? 0 : (r < 70 ? 1 : (r < 88 ? 2 : 3));
}
static int dice_octave(uint32_t seed, int i)       /* octaves to jump: 0, 1 or 2 */
{
    int r = (int)(dice_h(seed, E1_DICE_OCTAVES, i & 15) % 100ULL);
    return r < 55 ? 0 : (r < 88 ? 1 : 2);
}

int e1_dice_rolled(const e1_track_t *t)
{
    int m = 0;
    for (int l = 0; l < E1_DICE_LAYERS; l++) if (t->dseed[l]) m |= 1 << l;
    return m;
}

int e1_dice_active(const e1_engine_t *e, int track)
{
    if (track < 0 || track >= E1_TRACKS) return 0;
    const e1_track_t *t = &e->t[track];
    int c = e1_display_cycle(t), m = 0;
    if (eff(e, track, E1_P_STYLE, c, -1) == E1_STYLE_DICE) m |= 1 << E1_DICE_NOTES;
    if (eff(e, track, E1_P_GROOVE, c, -1) == E1_GROOVE_DICE) m |= 1 << E1_DICE_ACCENTS;
    if (t->v[E1_P_DICELEN]) m |= 1 << E1_DICE_LENGTHS;
    if (eff(e, track, E1_P_PHRASE, c, -1) == E1_PHRASE_DICE) m |= 1 << E1_DICE_OCTAVES;
    if (t->dseed[E1_DICE_RHYTHM]) m |= 1 << E1_DICE_RHYTHM;
    return m;
}

static void dice_push(e1_track_t *t)
{
    if (t->dhist_n >= E1_DICE_HISTORY) {
        memmove(&t->dhist[0], &t->dhist[1], sizeof t->dhist[0] * (E1_DICE_HISTORY - 1));
        t->dhist_n = E1_DICE_HISTORY - 1;
    }
    e1_dice_snap_t *h = &t->dhist[t->dhist_n++];
    memcpy(h->seed, t->dseed, sizeof h->seed);
    h->style = (uint8_t)t->v[E1_P_STYLE]; h->groove = (uint8_t)t->v[E1_P_GROOVE];
    h->phrase = (uint8_t)t->v[E1_P_PHRASE]; h->dlen = (uint8_t)t->v[E1_P_DICELEN];
}

static int dice_back(e1_track_t *t)
{
    if (!t->dhist_n) return 0;
    const e1_dice_snap_t *h = &t->dhist[--t->dhist_n];
    memcpy(t->dseed, h->seed, sizeof h->seed);
    t->v[E1_P_STYLE] = h->style; t->v[E1_P_GROOVE] = h->groove;
    t->v[E1_P_PHRASE] = h->phrase; t->v[E1_P_DICELEN] = h->dlen;
    return 1;
}

static const int8_t DICE_PAR[3] = { E1_P_STYLE, E1_P_GROOVE, E1_P_PHRASE };
static const int8_t DICE_ENT[3] = { E1_STYLE_DICE, E1_GROOVE_DICE, E1_PHRASE_DICE };
static int dice_list(int layer)
{
    return layer == E1_DICE_NOTES ? 0 : (layer == E1_DICE_ACCENTS ? 1 : (layer == E1_DICE_OCTAVES ? 2 : -1));
}

/* Put a layer's Dice entry up, remembering what it replaces. */
static void dice_select(const e1_engine_t *e, e1_track_t *t, int layer)
{
    int k = dice_list(layer);
    if (layer == E1_DICE_LENGTHS) t->v[E1_P_DICELEN] = 1;
    /* An amount of zero would leave the new lane unheard. */
    if (layer == E1_DICE_ACCENTS && t->v[E1_P_ACCENT] == 0) t->v[E1_P_ACCENT] = 60;
    if (layer == E1_DICE_OCTAVES && t->v[E1_P_RANGE] == 0)
        t->v[E1_P_RANGE] = (int16_t)clampi(SCALE_LEN[rscale(e, t->v[E1_P_SCALE])], 1, 16);
    if (k < 0 || t->v[DICE_PAR[k]] == DICE_ENT[k]) return;
    t->dwas[k] = (uint8_t)(t->v[DICE_PAR[k]] + 1);
    t->v[DICE_PAR[k]] = DICE_ENT[k];
}

static void dice_clear(e1_track_t *t, int layer)
{
    int k = dice_list(layer);
    t->dseed[layer] = 0;
    if (layer == E1_DICE_LENGTHS) t->v[E1_P_DICELEN] = 0;
    if (k < 0) return;
    if (t->v[DICE_PAR[k]] == DICE_ENT[k])
        t->v[DICE_PAR[k]] = (int16_t)(t->dwas[k] ? clampi(t->dwas[k] - 1, 0, DICE_ENT[k] - 1) : 0);
    t->dwas[k] = 0;
}

static uint32_t new_seed(e1_engine_t *e, int ti, int layer)
{
    uint64_t h = mix(((uint64_t)e->seed << 32) ^ ((uint64_t)(++e->nonce) << 12) ^ (uint64_t)(ti * 8 + layer) ^
                     ((uint64_t)(int64_t)(e->tick * 16.0) << 20));
    uint32_t s = (uint32_t)(h >> 16) & 0x7FFFFFFFu;
    return s ? s : 1u;
}

static int dice_layer(const char *name)
{
    for (int l = 0; l < E1_DICE_LAYERS; l++) if (strcmp(name, DICE_NAMES[l]) == 0) return l;
    return strcmp(name, "all") == 0 ? E1_DICE_LAYERS : -1;
}

int e1_steps(const e1_engine_t *e, int track, int cycle)
{
    if (track < 0 || track >= E1_TRACKS) return 16;
    return eff(e, track, E1_P_STEPS, cycle, -1);
}

int e1_display_cycle(const e1_track_t *t)
{
    if (t->edit_mask) return lowest_bit(t->edit_mask);
    return clampi(t->cur_cycle, 0, E1_CYCLES - 1);
}

uint32_t e1_cycles_edited(const e1_track_t *t)
{
    uint32_t m = 0;
    for (int c = 0; c < E1_CYCLES; c++) {
        int ed = (t->cadd[c] || t->cdel[c]);
        for (int p = 0; p < E1_NP && !ed; p++) if (t->cyc[c][p]) ed = 1;
        for (int l = 0; l < E1_CC_LANES && !ed; l++) if (t->cc_cyc[c][l]) ed = 1;
        if (ed) m |= 1u << c;
    }
    return m;
}

static void toggle_step(e1_engine_t *e, int ti, int i)
{
    e1_track_t *t = &e->t[ti];
    if (t->edit_mask) {
        for (int c = 0; c < E1_CYCLES; c++) {
            if (!(t->edit_mask & (1u << c))) continue;
            int steps = eff(e, ti, E1_P_STEPS, c, -1);
            if (i < 0 || i >= steps) continue;
            uint64_t bit = 1ULL << pattern_index(i, eff(e, ti, E1_P_ROTATE, c, -1), steps);
            int want = !hit(e, ti, c, i, 0);
            t->cadd[c] &= ~bit; t->cdel[c] &= ~bit;
            if (hit(e, ti, c, i, 0) != want) { if (want) t->cadd[c] |= bit; else t->cdel[c] |= bit; }
        }
        return;
    }
    int steps = eff(e, ti, E1_P_STEPS, -1, -1);
    if (i < 0 || i >= steps) return;
    uint64_t bit = 1ULL << pattern_index(i, eff(e, ti, E1_P_ROTATE, -1, -1), steps);
    int want = !hit(e, ti, -1, i, 0);
    t->add_mask &= ~bit; t->del_mask &= ~bit;
    if (hit(e, ti, -1, i, 0) != want) { if (want) t->add_mask |= bit; else t->del_mask |= bit; }
}

int e1_ports(const e1_engine_t *e, const e1_track_t *t)
{
    int o = t->v[E1_P_OUTPUT];
    if (o == E1_OUT_GLOBAL) {
        if (e->dest == E1_DEST_COMPUTER) return E1_PORT_COMPUTER;
        if (e->dest == E1_DEST_BOTH) return E1_PORT_MOVE | E1_PORT_COMPUTER;
        return E1_PORT_MOVE;
    }
    if (o == E1_OUT_COMPUTER) return E1_PORT_COMPUTER;
    if (o == E1_OUT_BOTH) return E1_PORT_MOVE | E1_PORT_COMPUTER;
    return E1_PORT_MOVE;
}

/* ----------------------------------------------------------------- time */

static double speed_f(const e1_track_t *t) { return t->p_speed > 0 ? 2.0 : (t->p_speed < 0 ? 0.5 : 1.0); }

/* Track time: ticks since the track's own zero, scaled by half/double time.
 * Because it is a function of the transport, letting go of a speed move
 * lands the track back in sync. */
static double track_time(const e1_engine_t *e, const e1_track_t *t, double tick)
{
    return (tick - e->origin - t->toff) * speed_f(t);
}
static double engine_time(const e1_engine_t *e, const e1_track_t *t, double T)
{
    return e->origin + t->toff + T / speed_f(t);
}

typedef struct {
    int c;            /* cycle */
    int step;         /* slot in the cycle, in time order (Dir and Jitter pick the step it plays) */
    int steps;
    int dur;          /* ticks until the next slot */
    int64_t start;    /* track time */
    int64_t cstart;   /* track time where this pass through the cycle began */
    int64_t D;        /* the cycle's length in ticks */
    int64_t g;        /* slots since the round (or the Length) began */
    int warp;
} slot_t;

/* Fit: each cycle lasts the Length (one bar while Length is Off), its steps
 * spread evenly over it. Length then no longer cuts the round short. */
static int64_t fit_ticks(const e1_track_t *t)
{
    int bars = e1_length_bars[clampi(t->v[E1_P_LENGTH], 0, E1_LENGTHS - 1)];
    return (int64_t)(bars ? bars : 1) * E1_TICKS_PER_BAR;
}

static int64_t cycle_ticks(const e1_engine_t *e, int ti, int c, int steps)
{
    const e1_track_t *t = &e->t[ti];
    if (t->v[E1_P_FIT]) return fit_ticks(t) < steps ? steps : fit_ticks(t);
    return (int64_t)steps * div_ticks(eff(e, ti, E1_P_DIVISION, c, -1));
}

/* Warp bends where the steps fall: x -> x^p with p = 3^(-warp/100), so
 * positive warp starts slow and rushes, negative starts fast and slows.
 * The cycle's ends stay put. One tick per step is kept out of the bend so
 * every step lasts at least one tick. */
static double warp_pow(int warp) { return pow(3.0, -(double)warp / 100.0); }

static int64_t step_edge(int64_t D, int n, int warp, int k)
{
    if (k <= 0) return 0;
    if (k >= n) return D;
    double x = (double)k / (double)n;
    if (warp) x = pow(x, warp_pow(warp));
    return (int64_t)floor(x * (double)(D - n) + 0.5) + k;
}

/* The slot index whose span holds r (0 <= r < D). */
static int step_find(int64_t D, int n, int warp, int64_t r)
{
    double y = (double)r / (double)D;
    if (warp) y = pow(y, 1.0 / warp_pow(warp));
    int k = clampi((int)(y * (double)n), 0, n - 1);
    while (k > 0 && step_edge(D, n, warp, k) > r) k--;
    while (k < n - 1 && step_edge(D, n, warp, k + 1) <= r) k++;
    return k;
}

static int cycle_list(const e1_track_t *t, int *list)
{
    int n = 0;
    if (t->edit_mask) { for (int c = 0; c < E1_CYCLES; c++) if (t->edit_mask & (1u << c)) list[n++] = c; }
    else { int nc = clampi(t->v[E1_P_CYCLES], 1, E1_CYCLES); for (int c = 0; c < nc; c++) list[n++] = c; }
    return n;
}

/* Length in ticks as it cuts the round: 0 = no cut. */
static int64_t length_cut(const e1_track_t *t)
{
    if (t->v[E1_P_FIT]) return 0;
    return (int64_t)e1_length_bars[clampi(t->v[E1_P_LENGTH], 0, E1_LENGTHS - 1)] * E1_TICKS_PER_BAR;
}

/* The period shapes run over, in ticks. See E1_SRC_*. */
static int64_t shape_period(const e1_engine_t *e, int ti)
{
    const e1_track_t *t = &e->t[ti];
    int bars = e1_loop_bars[clampi(t->v[E1_P_LOOP], 0, E1_LOOPS - 1)];
    if (bars) return (int64_t)bars * E1_TICKS_PER_BAR;
    int list[E1_CYCLES], n = cycle_list(t, list);
    int64_t round = 0;
    for (int i = 0; i < n; i++) round += cycle_ticks(e, ti, list[i], eff(e, ti, E1_P_STEPS, list[i], -1));
    int64_t Lt = length_cut(t);
    if (Lt > 0 && Lt < round) round = Lt;
    return round > 0 ? round : E1_TICKS_PER_BAR;
}

/* The slot that contains track time T. One round is every active cycle
 * played once; Length cuts the round short and starts again. */
static void slot_at(const e1_engine_t *e, int ti, int64_t T, slot_t *s)
{
    const e1_track_t *t = &e->t[ti];
    if (T < 0) T = 0;
    int64_t Lt = length_cut(t);
    int64_t base = 0, Tm = T;
    if (Lt > 0) { base = (T / Lt) * Lt; Tm = T - base; }

    int list[E1_CYCLES], n = cycle_list(t, list);
    int st[E1_CYCLES], wp[E1_CYCLES];
    int64_t Dc[E1_CYCLES], round = 0, nslots = 0;
    for (int i = 0; i < n; i++) {
        st[i] = eff(e, ti, E1_P_STEPS, list[i], -1);
        Dc[i] = cycle_ticks(e, ti, list[i], st[i]);
        wp[i] = eff(e, ti, E1_P_WARP, list[i], -1);
        round += Dc[i];
        nslots += st[i];
    }
    int64_t r = Tm % round, acc = Tm - r, g = (Tm / round) * nslots;
    int i = 0;
    while (i < n - 1 && r >= Dc[i]) { r -= Dc[i]; acc += Dc[i]; g += st[i]; i++; }
    if (r >= Dc[i]) r = Dc[i] - 1;
    int k = step_find(Dc[i], st[i], wp[i], r);
    int64_t e0 = step_edge(Dc[i], st[i], wp[i], k), e1 = step_edge(Dc[i], st[i], wp[i], k + 1);
    s->c = list[i]; s->steps = st[i]; s->step = k;
    s->D = Dc[i]; s->warp = wp[i];
    s->cstart = base + acc;
    s->start = base + acc + e0;
    s->dur = (int)(e1 - e0);
    s->g = g + k;
    if (Lt > 0 && s->start + s->dur > base + Lt) s->dur = (int)(base + Lt - s->start);
    if (s->dur < 1) s->dur = 1;
}

static int64_t ceil64(double x) { return (int64_t)ceil(x - 1e-9); }

static void resync(e1_engine_t *e, e1_track_t *t)
{
    double Tn = track_time(e, t, e->tick);
    t->cursor = Tn <= 0 ? 0 : ceil64(Tn);
    t->fired = 0;
    t->fz_dt = 0;
}

void e1_resync(e1_engine_t *e, int track)
{
    if (track >= 0 && track < E1_TRACKS) resync(e, &e->t[track]);
}

void e1_start(e1_engine_t *e, double tick) { e1_join(e, tick, tick); }

void e1_join(e1_engine_t *e, double origin, double now)
{
    e->running = 1;
    e->origin = origin;
    e->tick = now;
    for (int i = 0; i < E1_TRACKS; i++) {
        e1_track_t *t = &e->t[i];
        t->toff = 0;
        t->cur_step = -1;
        t->cur_cycle = 0;
        t->rt_T = 0;
        t->ar_idx = 0;
        t->split_n = 0;
        for (int c = 0; c < E1_CHAINS; c++) t->chain[c].left = 0;
        resync(e, t);
    }
}

/* --------------------------------------------------------------- voices */

static void voices_off(e1_track_t *t, e1_emit_fn emit, void *user)
{
    for (int i = 0; i < E1_VOICES; i++) {
        e1_voice_t *v = &t->voice[i];
        if (!v->on) continue;
        emit(user, v->ports, (uint8_t)(0x80 | (v->ch & 0x0F)), v->note, 0);
        v->on = 0;
    }
}

static void chains_off(e1_track_t *t) { for (int c = 0; c < E1_CHAINS; c++) t->chain[c].left = 0; }

static void aud_off(e1_track_t *t, e1_emit_fn emit, void *user)
{
    for (int i = 0; i < t->aud_n; i++) emit(user, t->aud_ports, (uint8_t)(0x80 | (t->aud_ch & 0x0F)), t->aud_note[i], 0);
    t->aud_n = 0;
}

void e1_track_silence(e1_engine_t *e, int track, e1_emit_fn emit, void *user)
{
    if (track < 0 || track >= E1_TRACKS) return;
    voices_off(&e->t[track], emit, user);
    chains_off(&e->t[track]);
    aud_off(&e->t[track], emit, user);
}

void e1_all_notes_off(e1_engine_t *e, e1_emit_fn emit, void *user)
{
    for (int i = 0; i < E1_TRACKS; i++) e1_track_silence(e, i, emit, user);
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

/* `to` is the track whose channel and output the note goes to: the track
 * itself, or its Split partner. The voice stays with the track that played. */
static void note_on(e1_engine_t *e, e1_track_t *t, const e1_track_t *to, int note, int vel, double when, double len,
                    e1_emit_fn emit, void *user)
{
    int ports = e1_ports(e, to);
    uint8_t ch = (uint8_t)((to->v[E1_P_CHANNEL] - 1) & 0x0F);
    note = clampi(note, 0, 127);
    e1_voice_t *slot = NULL;
    for (int i = 0; i < E1_VOICES; i++) {
        e1_voice_t *v = &t->voice[i];
        if (v->on && v->note == note && v->ch == ch) {      /* retrigger: close it first */
            emit(user, v->ports, (uint8_t)(0x80 | ch), v->note, 0);
            v->on = 0;
        }
        if (!v->on && !slot) slot = v;
    }
    if (!slot) {                                            /* steal the one ending soonest */
        slot = &t->voice[0];
        for (int i = 1; i < E1_VOICES; i++) if (t->voice[i].off < slot->off) slot = &t->voice[i];
        emit(user, slot->ports, (uint8_t)(0x80 | (slot->ch & 0x0F)), slot->note, 0);
    }
    emit(user, ports, (uint8_t)(0x90 | ch), (uint8_t)note, (uint8_t)clampi(vel, 1, 127));
    slot->on = 1; slot->note = (uint8_t)note; slot->ch = ch; slot->ports = (uint8_t)ports;
    slot->off = when + (len < 1.0 ? 1.0 : len);
    t->flash = 1;
}

/* Split: every second hit of the track goes to its partner's channel and
 * output. Returns the track this hit plays through. */
static const e1_track_t *split_to(e1_engine_t *e, e1_track_t *t)
{
    int p = t->v[E1_P_SPLIT] - 1;
    if (p < 0 || p >= E1_TRACKS || &e->t[p] == t) return t;
    return (t->split_n++ & 1) ? &e->t[p] : t;
}

/* ---------------------------------------------------------------- pitch */

static int shift_in_scale(int note, int d, int scale, int root)
{
    const int8_t *deg = SCALE_DEG[scale];
    int n = SCALE_LEN[scale];
    int rel = note - root;
    int oct = (rel >= 0) ? rel / 12 : -((-rel + 11) / 12);
    int pc = rel - oct * 12;
    int k = 0;
    for (int i = 0; i < n; i++) if (deg[i] <= pc) k = i;      /* snap down into the scale */
    int idx = oct * n + k + d;
    int o2 = (idx >= 0) ? idx / n : -((-idx + n - 1) / n);
    int k2 = idx - o2 * n;
    return root + o2 * 12 + deg[k2];
}

static void sort_notes(int *a, int n)
{
    for (int i = 1; i < n; i++) {
        int x = a[i], j = i - 1;
        while (j >= 0 && a[j] > x) { a[j + 1] = a[j]; j--; }
        a[j + 1] = x;
    }
}

/* Phrase position 0..1 mapped to tenths of Range. */
static int phrase_tenths(int phrase, double ph)
{
    if (phrase < 4) return CADENCE[phrase][clampi((int)(ph * 8.0), 0, 7)];
    switch (phrase) {
    case 4: return (int)(ph * 10.0);
    case 5: return (int)lrint((1.0 - fabs(2.0 * ph - 1.0)) * 10.0);
    case 6: return (int)lrint((0.5 - 0.5 * cos(6.283185307179586 * ph)) * 10.0);
    default: return ph < 0.5 ? 0 : 10;
    }
}

/* A pool moved by Pitch, Harmony and a phrase shift, in the scale, sorted. */
static void pool_shift(const int *src, int n, int pitch, int harm, int shift, int scale, int root, int *L)
{
    int ha = harm < 0 ? -harm : harm, hs = harm < 0 ? -1 : 1;
    for (int j = 0; j < n; j++) {
        int extra = (ha > j) ? (ha - j + n - 1) / n : 0;     /* Harmony moves one note per click */
        L[j] = shift_in_scale(src[j], pitch + shift + hs * extra, scale, root);
    }
    sort_notes(L, n);
}

/* The chord: the chord track's pool as its Pitch and Harmony leave it in
 * the cycle (and on the step) it plays now. Voicing and Style are how the
 * chord track itself plays the chord, so they stay out. */
int e1_chord(const e1_engine_t *e, int *out)
{
    int ci = (int)e->chord_track - 1;
    if (ci < 0 || ci >= E1_TRACKS) return 0;
    const e1_track_t *t = &e->t[ci];
    int n = t->pool_n > E1_POOL ? E1_POOL : t->pool_n;
    if (t->v[E1_P_TYPE] != E1_TYPE_NOTE || n <= 0) return 0;
    int c = e->running ? clampi(t->cur_cycle, 0, E1_CYCLES - 1) : e1_display_cycle(t);
    int step = e->running ? t->cur_step : -1;
    int src[E1_POOL];
    for (int j = 0; j < n; j++) src[j] = t->pool[j];
    pool_shift(src, n, eff_s(e, ci, E1_P_PITCH, c, -1, step), eff_s(e, ci, E1_P_HARMONY, c, -1, step), 0,
               rscale(e, eff_s(e, ci, E1_P_SCALE, c, -1, step)), rroot(e, eff_s(e, ci, E1_P_ROOT, c, -1, step)), out);
    for (int j = 0; j < n; j++) out[j] = clampi(out[j], 0, 127);
    return n;
}

/* The notes a track starts from: its own pool, or what its Follow role
 * takes from the chord, moved to the octave of the track's own lowest note.
 * *role is the role in force, Off when the track plays its own pool. */
static int source_pool(const e1_engine_t *e, int ti, int *src, int *role)
{
    const e1_track_t *t = &e->t[ti];
    int n = t->pool_n > E1_POOL ? E1_POOL : t->pool_n;
    for (int j = 0; j < n; j++) src[j] = t->pool[j];
    *role = E1_FOLLOW_OFF;
    int f = t->v[E1_P_FOLLOW];
    if (f == E1_FOLLOW_OFF || ti == (int)e->chord_track - 1) return n;
    int ch[E1_POOL], cn = e1_chord(e, ch);
    if (cn <= 0) return n;
    *role = f;
    int oct = n > 0 ? (t->pool[0] / 12 - ch[0] / 12) * 12 : 0;
    if (f == E1_FOLLOW_BASS) cn = 1;                         /* the chord's lowest note */
    for (int j = 0; j < cn; j++) {
        src[j] = ch[j] + oct;
        while (src[j] > 127) src[j] -= 12;
        while (src[j] < 0) src[j] += 12;
    }
    return cn;
}

uint32_t e1_followers(const e1_engine_t *e)
{
    uint32_t m = 0;
    int ch[E1_POOL];
    if (e1_chord(e, ch) <= 0) return 0;
    for (int i = 0; i < E1_TRACKS; i++)
        if (e->t[i].v[E1_P_TYPE] == E1_TYPE_NOTE && e->t[i].v[E1_P_FOLLOW] && i != (int)e->chord_track - 1) m |= 1u << i;
    return m;
}

/* The notes one pulse plays. pidx counts pulses since the cycle began (and
 * goes on counting through repeats), which is what moves mono styles and
 * Climb along. Returns the note count, at most E1_POOL. */
static int build_notes(const e1_engine_t *e, int ti, int c, int step, int pidx, int dpos, int shift, int ridx, int down, int *out)
{
    const e1_track_t *t = &e->t[ti];
    int src[E1_POOL], role;
    int n = source_pool(e, ti, src, &role);
    if (n <= 0) return 0;
    int scale = rscale(e, eff_s(e, ti, E1_P_SCALE, c, ridx, step)), root = rroot(e, eff_s(e, ti, E1_P_ROOT, c, ridx, step));
    int pitch = eff_s(e, ti, E1_P_PITCH, c, ridx, step), harm = eff_s(e, ti, E1_P_HARMONY, c, ridx, step);
    int voicing = eff_s(e, ti, E1_P_VOICING, c, ridx, step), style = eff_s(e, ti, E1_P_STYLE, c, ridx, step);
    if (role == E1_FOLLOW_ARP && style < 3) style += 3;      /* an arp plays one chord tone at a time */

    int L[E1_POOL + 16], len = n;
    pool_shift(src, n, pitch, harm, shift, scale, root, L);

    int dice = style == E1_STYLE_DICE;
    int poly = style < 3, kind = dice ? 1 : style % 3;       /* Dice takes Voicing as Ramp does */
    if (dice && len == 1) {                                   /* one note: play around it in the scale */
        L[1] = shift_in_scale(L[0], 2, scale, root);
        L[2] = shift_in_scale(L[0], 4, scale, root);
        len = n = 3;
    }
    int va = voicing < 0 ? -voicing : voicing, vs = voicing < 0 ? -1 : 1;
    if (kind == 1) {                                          /* Ramp: add notes like an arpeggiator range */
        for (int k = 0; k < va && len < E1_POOL + 16; k++) {
            int src = vs > 0 ? L[k % n] : L[n - 1 - (k % n)];
            L[len++] = src + vs * 12 * (1 + k / n);
        }
        sort_notes(L, len);
    } else {
        int inv = (kind == 2) ? (pidx % (va + 1)) : va;       /* Climb walks through the inversions */
        for (int k = 0; k < inv; k++) {
            if (vs > 0) L[0] += 12; else L[len - 1] -= 12;
            sort_notes(L, len);
        }
    }
    for (int j = 0; j < len; j++) { while (L[j] > 127) L[j] -= 12; while (L[j] < 0) L[j] += 12; }

    if (poly) {
        int m = len > E1_POOL ? E1_POOL : len;
        for (int j = 0; j < m; j++) out[j] = L[j];
        return m;
    }
    int idx = pidx % len, oct = 0;
    if (dice) idx = dice_note(t->dseed[E1_DICE_NOTES], t->v[E1_P_DICETYPE], dpos, len, &oct);
    int note = L[down ? len - 1 - idx : idx] + 12 * oct;
    while (note > 127) note -= 12;
    while (note < 0) note += 12;
    out[0] = note;
    return 1;
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
        else if (strcmp(move, "ratchet") == 0) t->p_ratchet = (uint8_t)clampi(value, 0, 4);
        else if (strcmp(move, "speed") == 0) {
            int8_t sp = (int8_t)clampi(value, -1, 1);
            if (sp != t->p_speed) { t->p_speed = sp; t->p_freeze = 0; resync(e, t); }
        }
        else if (strcmp(move, "reverse") == 0) t->p_reverse = (uint8_t)(value != 0);
        else if (strcmp(move, "freeze") == 0) {
            t->p_freeze = (uint8_t)clampi(value, 0, 64);
            resync(e, t);
        }
        else if (strcmp(move, "accent") == 0) t->p_accent = (uint8_t)(value != 0);
        else if (strcmp(move, "restart") == 0) {
            /* Step 1 falls on the track's next step line, so it stays on the grid. */
            slot_t rs;
            double Tn = track_time(e, t, e->tick);
            slot_at(e, i, (int64_t)floor(Tn < 0 ? 0 : Tn), &rs);
            double d = (double)rs.dur / speed_f(t);
            double rel = e->tick - e->origin - t->toff;
            t->toff += ceil(rel / d - 1e-9) * d;
            t->p_freeze = 0;
            t->cursor = 0; t->fired = 0; t->fz_dt = 0;
        }
        else if (strcmp(move, "rotate") == 0) {
            int steps = eff(e, i, E1_P_STEPS, -1, -1);
            t->v[E1_P_ROTATE] = (int16_t)(((t->v[E1_P_ROTATE] + value % steps) % steps + steps) % steps);
        }
        else if (strcmp(move, "stoprepeats") == 0) chains_off(t);
        else if (strcmp(move, "randstart") == 0) {
            if (!value) { t->toff = 0; t->p_freeze = 0; resync(e, t); continue; }
            /* Jump ahead a random number of whole steps, so the grid holds. */
            slot_t rs;
            double Tn = track_time(e, t, e->tick);
            slot_at(e, i, (int64_t)floor(Tn < 0 ? 0 : Tn), &rs);
            int64_t Lt = (int64_t)e1_length_bars[clampi(t->v[E1_P_LENGTH], 0, E1_LENGTHS - 1)] * E1_TICKS_PER_BAR;
            int n = Lt > 0 ? (int)(Lt / rs.dur) : rs.steps;
            if (n < 2) n = rs.steps;
            if (n < 2) continue;
            uint64_t h = mix(((uint64_t)e->seed << 32) ^ ((uint64_t)(++e->nonce) << 8) ^ (uint64_t)i ^ (uint64_t)(int64_t)e->tick);
            t->toff -= (double)(1 + (int)(h % (uint64_t)(n - 1))) * (double)rs.dur / speed_f(t);
            t->p_freeze = 0;
            resync(e, t);
        }
        else if (strcmp(move, "clear") == 0) {
            t->p_fill = t->p_ratchet = t->p_reverse = t->p_accent = 0;
            if (t->p_speed || t->p_freeze) { t->p_speed = 0; t->p_freeze = 0; resync(e, t); }
        }
    }
    if (strcmp(move, "clear") == 0) for (int i = 0; i < E1_TRACKS; i++) e->t[i].p_solo_mute = 0;
}

/* --------------------------------------------------------------- firing */

static void cc_send(e1_track_t *t, int lane, int value, int ports, e1_emit_fn emit, void *user)
{
    value = clampi(value, 0, 127);
    emit(user, ports, (uint8_t)(0xB0 | ((t->v[E1_P_CHANNEL] - 1) & 0x0F)), (uint8_t)(t->cc_num[lane] & 0x7F), (uint8_t)value);
    t->cc_sent[lane] = (int16_t)value;
}

static int cc_eff(const e1_engine_t *e, int ti, int lane, int c, int ridx)
{
    const e1_track_t *t = &e->t[ti];
    int v = t->cc_val[lane];
    if (v < 0) return -1;
    if (c >= 0 && c < E1_CYCLES) v += t->cc_cyc[c][lane];
    if (t->cc_rnd[lane] && t->v[E1_P_RANDOM] > 0 && ridx >= 0 && t->v[E1_P_SOURCE] != E1_SRC_RANDOM) {
        v += (int)lrint((double)t->cc_rnd[lane] / 100.0 * 127.0 * shape_at(e, ti, ti * 64 + 48 + lane, ridx, 0));
    } else if (t->cc_rnd[lane] && t->v[E1_P_RANDOM] > 0 && ridx >= 0) {
        uint64_t h = mix(((uint64_t)tseed(e, t) << 32) ^ ((uint64_t)(ti * 64 + 48 + lane) << 16) ^ (uint64_t)ridx);
        if ((int)(h % 100ULL) < t->v[E1_P_RANDOM])
            v += (int)lrint((double)t->cc_rnd[lane] / 100.0 * 127.0 * ((double)((h >> 8) % 1001ULL) / 1000.0));
    }
    return clampi(v, 0, 127);
}

static int groove_tenths(const e1_track_t *t, int groove, int step)
{
    int i = step & 15;
    if (groove >= E1_GROOVE_DICE) return dice_accent(t->dseed[E1_DICE_ACCENTS], i);
    if (groove < 4) return GROOVE_SHAPE[groove][i];
    switch (groove) {
    case 4: return -10 + (20 * i) / 15;
    case 5: return i < 8 ? -10 + (20 * i) / 8 : 10 - (20 * (i - 8)) / 8;
    case 6: return (int)lrint(10.0 * sin(6.283185307179586 * (double)i / 16.0));
    default: return (i & 4) ? -10 : 10;
    }
}

/* The step a slot is played as, and whether it is a retrigger step. */
static int is_retrig(const e1_track_t *t, int step) { return step >= 0 && step < 64 && ((t->retrig >> step) & 1); }

static int64_t loop_ticks(const e1_track_t *t)
{
    return (int64_t)e1_loop_bars[clampi(t->v[E1_P_LOOP], 0, E1_LOOPS - 1)] * E1_TICKS_PER_BAR;
}
/* Track time as Loop lock sees it: wrapped to the loop, so draws come round. */
static int64_t loop_wrap(const e1_track_t *t, int64_t T)
{
    int64_t L = loop_ticks(t);
    return L ? ((T % L) + L) % L : T;
}

/* Jitter: on each slot of this pass through the cycle, with Jitter's chance,
 * the playhead skips a step ahead, plays the step again or steps back, and
 * goes on from there. The walk starts fresh with each pass (and at each
 * Loop start), and its draws follow Loop lock. Returns the offset. */
static int jitter_offset(const e1_engine_t *e, int ti, const slot_t *s)
{
    const e1_track_t *t = &e->t[ti];
    int J = t->v[E1_P_JITTER];
    if (J <= 0) return 0;
    int64_t L = loop_ticks(t), from = L ? (s->start / L) * L : 0;
    uint32_t seed = tseed(e, t);
    int o = 0;
    for (int j = 1; j <= s->step; j++) {
        int64_t at = s->cstart + step_edge(s->D, s->steps, s->warp, j);
        if (at < from) continue;
        uint64_t h = mix(((uint64_t)seed << 32) ^ ((uint64_t)(ti + 1) << 56) ^ 0x7177E000000ULL ^ (uint64_t)loop_wrap(t, at));
        if ((int)(h % 100ULL) >= J) continue;
        int kind = (int)((h >> 12) % 3ULL);
        o += kind == 0 ? 1 : (kind == 1 ? -1 : -2);
    }
    return o;
}

/* The step a slot plays: Dir, then Jitter, then the Reverse move on top.
 * *rev is set when the playhead is running backwards there. */
static int play_step_x(const e1_engine_t *e, int ti, const slot_t *s, int jit, int *rev)
{
    const e1_track_t *t = &e->t[ti];
    int n = s->steps, k = s->step, pos, back = 0;
    int o = jit ? jitter_offset(e, ti, s) : 0;
    int dir = eff(e, ti, E1_P_DIR, s->c, -1);
    if (dir == E1_DIR_PEND && n > 1) {
        int64_t m = 2 * (int64_t)n - 2, q = ((s->g + o) % m + m) % m;
        pos = (int)(q < n ? q : m - q);
        back = q >= n;
    } else if (dir == E1_DIR_RAND && n > 1) {
        uint64_t h = mix(((uint64_t)tseed(e, t) << 32) ^ ((uint64_t)(ti + 1) << 56) ^ 0xD12E000000ULL ^ (uint64_t)loop_wrap(t, s->start));
        pos = (int)(h % (uint64_t)n);
    } else {
        pos = ((k + o) % n + n) % n;
        if (dir == E1_DIR_BACK) { pos = n - 1 - pos; back = 1; }
    }
    if (t->p_reverse) { pos = n - 1 - pos; back = !back; }
    if (rev) *rev = back;
    return pos;
}

/* Time as the free-running parts count it: from the last retrigger step,
 * and wrapped to the loop when Loop is set, so the same draws come round. */
static int64_t free_time(const e1_engine_t *e, int ti, const slot_t *s)
{
    const e1_track_t *t = &e->t[ti];
    int64_t r = s->start;
    if (t->retrig) r = is_retrig(t, play_step_x(e, ti, s, 0, NULL)) ? 0 : r - t->rt_T;
    if (r < 0) r = -r;
    int bars = e1_loop_bars[clampi(t->v[E1_P_LOOP], 0, E1_LOOPS - 1)];
    if (bars) r %= (int64_t)bars * E1_TICKS_PER_BAR;
    return r;
}

static int random_step(const e1_engine_t *e, int ti, const slot_t *s)
{
    return (int)((free_time(e, ti, s) / div_ticks(e->t[ti].v[E1_P_RATE])) & 0x3FFFFFFF);
}

/* Auto roll: every N bars the lanes that were rolled by hand get new dice. */
static void auto_roll(e1_track_t *t, const slot_t *s)
{
    int bars = e1_loop_bars[clampi(t->v[E1_P_AUTOROLL], 0, E1_LOOPS - 1)];
    if (!bars || t->p_freeze) return;
    int64_t idx = s->start / ((int64_t)bars * E1_TICKS_PER_BAR);
    if (idx == t->ar_idx) return;
    t->ar_idx = idx;
    if (!e1_dice_rolled(t)) return;
    dice_push(t);
    for (int l = 0; l < E1_DICE_LAYERS; l++) {
        if (!t->dseed[l]) continue;
        uint32_t n = (uint32_t)(mix(((uint64_t)t->dseed[l] << 20) ^ (uint64_t)idx ^ ((uint64_t)l << 60)) >> 16) & 0x7FFFFFFFu;
        t->dseed[l] = n ? n : 1u;
    }
}

static void fire(e1_engine_t *e, int ti, const slot_t *s, double when, e1_emit_fn emit, void *user)
{
    e1_track_t *t = &e->t[ti];
    int c = s->c;
    int rev = 0;
    int step = play_step_x(e, ti, s, 1, &rev);
    int down = rev ^ (t->p_reverse != 0);        /* Dir runs backwards here: mono styles walk down */
    t->cur_step = step;
    t->cur_cycle = c;
    if (t->retrig && is_retrig(t, step)) t->rt_T = s->start;
    auto_roll(t, s);
    if (!hit(e, ti, c, step, t->p_fill)) return;
    if (!dense(e, ti, c, step, t->p_fill)) return;

    /* The nearest retrigger step at or before this one in the cycle: style
     * and groove count from it. */
    int rb = -1;
    if (t->retrig) for (int i = 0; i < s->steps && i < 64; i++) {
        int upto = rev ? (i >= step) : (i <= step);
        if (upto && is_retrig(t, i) && (rb < 0 || !rev)) rb = i;
    }

    int dpos = rb < 0 ? step : abs(step - rb);   /* where the dice lanes are */
    uint32_t seed = tseed(e, t);
    int ridx = random_step(e, ti, s);
    /* With Loop set the chance draws come round with the loop too. */
    int64_t idT = t->v[E1_P_LOOP] > 0 ? free_time(e, ti, s) : s->start;
    int64_t id = (int64_t)((uint64_t)idT * 2654435761ULL + (uint64_t)ti);
    int prob = eff_s(e, ti, E1_P_PROB, c, ridx, step);
    if (prob < 0 && e1_chance(seed, ti, id) < -prob) return;   /* the whole pulse and its repeats */
    if (t->v[E1_P_MUTE] || t->p_solo_mute) return;

    if (t->v[E1_P_TYPE] == E1_TYPE_CC) {
        int ports = e1_ports(e, t);
        t->flash = 1;
        for (int l = 0; l < E1_CC_LANES; l++) {
            int v = cc_eff(e, ti, l, c, ridx);
            if (v < 0) continue;
            int mode = t->v[E1_P_CCMODE];
            if (mode == E1_CC_ON_PULSE) cc_send(t, l, v, ports, emit, user);
            else if (mode == E1_CC_ON_CHANGE) { if (v != t->cc_sent[l]) cc_send(t, l, v, ports, emit, user); }
            else t->cc_target[l] = (int16_t)v;
        }
        return;
    }

    /* pulses so far in this cycle, in play order */
    int pidx = 0;
    for (int i = 0; i < s->steps; i++) {
        int before = rev ? (i > step) : (i < step);
        if (rb >= 0 && (rev ? (i > rb) : (i < rb))) continue;
        if (before && hit(e, ti, c, i, t->p_fill)) pidx++;
    }

    static const int PRATE_TICKS[3] = { 4 * E1_TICKS_PER_BAR, 2 * E1_TICKS_PER_BAR, 8 * E1_TICKS_PER_BAR };
    int ptk = PRATE_TICKS[clampi(t->v[E1_P_PHRASERATE], 0, 2)];
    int64_t pT = t->retrig ? (is_retrig(t, step) ? 0 : s->start - t->rt_T) : s->start;   /* the phrase restarts on a retrigger */
    if (pT < 0) pT = -pT;
    double ph = (double)(pT % ptk) / (double)ptk;
    int range = eff_s(e, ti, E1_P_RANGE, c, ridx, step);
    int phrase = eff_s(e, ti, E1_P_PHRASE, c, ridx, step);
    int shift;
    if (phrase >= E1_PHRASE_DICE) {              /* whole octaves, as many as Range holds */
        int sl = SCALE_LEN[rscale(e, eff_s(e, ti, E1_P_SCALE, c, ridx, step))];
        int room = abs(range) / sl, o = dice_octave(t->dseed[E1_DICE_OCTAVES], dpos);
        if (range && room < 1) room = 1;
        if (o > room) o = room;
        shift = (range < 0 ? -o : o) * sl;
    } else shift = (int)lrint((double)range * (double)phrase_tenths(phrase, ph) / 10.0);

    double sp = speed_f(t);
    int vel = eff_s(e, ti, E1_P_VELOCITY, c, ridx, step);
    int accent = eff_s(e, ti, E1_P_ACCENT, c, ridx, step);
    if (accent) vel += accent * 6 * groove_tenths(t, eff_s(e, ti, E1_P_GROOVE, c, ridx, step), dpos) / 100;
    if (t->p_accent) vel = 127;
    vel = clampi(vel, 1, 127);

    double len = (double)s->dur * (double)eff_s(e, ti, E1_P_SUSTAIN, c, ridx, step) / 50.0 / sp;
    if (t->v[E1_P_DICELEN]) {                    /* short, medium, long, or tied to the next hit */
        int k = dice_length(t->dseed[E1_DICE_LENGTHS], dpos);
        if (k == 0) len *= 0.4;
        else if (k == 2) len *= 2.2;
        else if (k == 3) {
            int gap = 1;
            while (gap < s->steps && !hit(e, ti, c, (step + (rev ? -gap : gap) + s->steps * 2) % s->steps, t->p_fill)) gap++;
            double tie = (double)s->dur * (double)gap / sp;
            if (tie > len) len = tie;
        }
    }
    int repeats = eff_s(e, ti, E1_P_REPEATS, c, ridx, step);
    double interval = (double)div_ticks(eff_s(e, ti, E1_P_TIME, c, ridx, step)) / sp;
    double pace = 1.0 + (double)eff_s(e, ti, E1_P_PACE, c, ridx, step) / 200.0;
    int ramp = eff_s(e, ti, E1_P_RAMP, c, ridx, step);
    if (t->p_ratchet >= 2) {                     /* a held ratchet replaces the track's repeats */
        repeats = t->p_ratchet - 1;
        interval = (double)s->dur / sp / (double)t->p_ratchet;
        pace = 1.0; ramp = 0;
    }
    if (repeats > 0 && len > interval * 0.9) len = interval * 0.9;

    int tail = t->v[E1_P_TAIL];
    if (!tail) { chains_off(t); voices_off(t, emit, user); }   /* a new pulse cuts what came before */
    else for (int k = 0; k < E1_CHAINS; k++)                    /* "until the next pulse" ends here */
        if (t->chain[k].left > 1000) t->chain[k].left = 0;

    int notes[E1_POOL];
    int n = build_notes(e, ti, c, step, pidx, dpos, shift, ridx, down, notes);
    int drop = prob > 0 ? prob : 0;
    const e1_track_t *to = split_to(e, t);
    for (int j = 0; j < n; j++) {
        if (drop && e1_chance(seed, ti, (int64_t)((uint64_t)id * 31u + (uint64_t)j + 1u)) < drop) continue;
        note_on(e, t, to, notes[j], vel, when, len, emit, user);
    }

    if (repeats > 0) {
        e1_chain_t *ch = &t->chain[0];
        for (int k = 0; k < E1_CHAINS; k++) {
            if (!t->chain[k].left) { ch = &t->chain[k]; break; }
            if (t->chain[k].next > ch->next) ch = &t->chain[k];   /* else replace the one furthest along */
        }
        ch->left = repeats >= E1_REPEATS_INF ? 100000 : repeats;
        ch->k = 0;
        ch->interval = interval < 2.0 ? 2.0 : interval;
        ch->next = when + ch->interval;
        ch->pace = pace;
        ch->len = len;
        ch->vel = vel; ch->ramp = ramp;
        ch->cyc = c; ch->step = step; ch->pidx = pidx; ch->dpos = dpos; ch->shift = shift; ch->ridx = ridx; ch->down = down;
        ch->prob = drop;
        ch->id = (uint32_t)id;
    }
}

static void expire(e1_track_t *t, double now, e1_emit_fn emit, void *user)
{
    for (int i = 0; i < E1_VOICES; i++) {
        e1_voice_t *v = &t->voice[i];
        if (v->on && v->off <= now) {
            emit(user, v->ports, (uint8_t)(0x80 | (v->ch & 0x0F)), v->note, 0);
            v->on = 0;
        }
    }
}

static void run_chains(e1_engine_t *e, int ti, double now, e1_emit_fn emit, void *user)
{
    e1_track_t *t = &e->t[ti];
    for (int c = 0; c < E1_CHAINS; c++) {
        e1_chain_t *ch = &t->chain[c];
        int guard = 0;
        while (ch->left > 0 && ch->next <= now && guard++ < 64) {
            ch->k++;
            ch->left--;
            if (!(t->v[E1_P_MUTE] || t->p_solo_mute)) {
                int notes[E1_POOL];
                int n = build_notes(e, ti, ch->cyc, ch->step, ch->pidx + ch->k, ch->dpos + ch->k, ch->shift, ch->ridx, ch->down, notes);
                int vel = clampi(ch->vel + ch->ramp * 32 * ch->k / 100, 1, 127);
                double len = ch->len;
                if (len > ch->interval * 0.9) len = ch->interval * 0.9;
                const e1_track_t *to = split_to(e, t);
                for (int j = 0; j < n; j++) {
                    if (ch->prob && e1_chance(tseed(e, t), ti, (int64_t)ch->id * 131 + ch->k * 17 + j) < ch->prob) continue;
                    note_on(e, t, to, notes[j], vel, ch->next, len, emit, user);
                }
            }
            ch->interval *= ch->pace;
            if (ch->interval < 2.0) ch->interval = 2.0;
            if (ch->interval > 4.0 * E1_TICKS_PER_BAR) ch->left = 0;
            ch->next += ch->interval;
            if (ch->k > 512) ch->left = 0;
        }
    }
}

/* Live CC edits and glides. Bounded per call so a burst of edits cannot
 * flood Move's injection queue. */
static void cc_service(e1_engine_t *e, e1_emit_fn emit, void *user)
{
    int budget = 8;
    for (int n = 0; n < E1_TRACKS && budget > 0; n++) {
        int ti = (int)((e->cc_rr + (uint32_t)n) % E1_TRACKS);
        e1_track_t *t = &e->t[ti];
        if (t->v[E1_P_TYPE] != E1_TYPE_CC) continue;
        int ports = e1_ports(e, t);
        int silent = t->v[E1_P_MUTE] || t->p_solo_mute;
        int glide = t->v[E1_P_CCMODE] == E1_CC_GLIDE;
        int slew = t->v[E1_P_SLEW];
        int dc = e1_display_cycle(t);
        for (int l = 0; l < E1_CC_LANES && budget > 0; l++) {
            if (t->cc_val[l] < 0) { t->cc_dirty &= ~(1u << l); continue; }
            if (t->cc_dirty & (1u << l)) {
                t->cc_dirty &= ~(1u << l);
                int v = cc_eff(e, ti, l, dc, -1);
                if (glide && slew > 0) t->cc_target[l] = (int16_t)v;
                else {
                    if (!silent) { cc_send(t, l, v, ports, emit, user); budget--; }
                    t->cc_cur[l] = (float)v;
                }
            }
            if (glide && t->cc_target[l] >= 0) {
                float tgt = (float)t->cc_target[l];
                float rate = 128.0f / (1.0f + (float)slew * 4.0f);   /* units per call */
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

/* When a slot sounds, in track time: Delay moves the whole track, Timing
 * moves every second step (or pair, or four) of the swing grid. */
static double slot_offset(const e1_engine_t *e, int ti, const slot_t *s)
{
    const e1_track_t *t = &e->t[ti];
    double off = (double)s->dur * (double)t->v[E1_P_DELAY] / 100.0;
    int timing = eff(e, ti, E1_P_TIMING, s->c, random_step(e, ti, s));
    if (timing) {
        int g = 1 << clampi(t->v[E1_P_SWINGGRID], 0, 2);
        if ((s->step / g) & 1) off += (double)s->dur * (double)g * (double)timing / 100.0;
    }
    return off;
}

static void advance_track(e1_engine_t *e, int ti, double now, e1_emit_fn emit, void *user)
{
    e1_track_t *t = &e->t[ti];
    double Tn = track_time(e, t, now);
    slot_t s;

    if (t->p_freeze) {
        if (t->fz_dt == 0) {                      /* just engaged: loop the steps before now */
            slot_at(e, ti, (int64_t)floor(Tn < 0 ? 0 : Tn), &s);
            t->fz_dt = s.dur;
            t->fz_anchor = s.start + s.dur;
            t->fz_span = (int64_t)t->p_freeze * t->fz_dt;
            t->fz_j = 0;
        }
        for (int guard = 0; guard < 64; guard++) {
            int64_t V = t->fz_anchor + t->fz_j * t->fz_dt;
            if ((double)V > Tn) break;
            int64_t src = t->fz_anchor - t->fz_span + (t->fz_j * t->fz_dt) % t->fz_span;
            while (src < 0) src += t->fz_span;
            slot_at(e, ti, src, &s);
            double when = engine_time(e, t, (double)V);
            run_chains(e, ti, when - 1e-6, emit, user);
            expire(t, when, emit, user);
            fire(e, ti, &s, when, emit, user);
            t->fz_j++;
        }
        return;
    }

    /* Joined late or the clock jumped: carry on from now, don't replay. */
    if (Tn - (double)t->cursor > 2.0 * E1_TICKS_PER_BAR * speed_f(t)) { t->cursor = ceil64(Tn); t->fired = 0; }

    /* Timing and Delay can move a step past its neighbours, so look a few
     * slots ahead and always play the earliest one that is due. `fired`
     * marks slots ahead of the cursor that have already sounded. */
    for (int guard = 0; guard < 256; guard++) {
        int64_t st = t->cursor, first = -1;
        double best = 0; int bj = -1; slot_t bs = { 0 };
        for (int j = 0; j < 8; j++) {
            slot_at(e, ti, st, &s);
            if (s.start < st) {                   /* inside a slot that is already done */
                int64_t ns = s.start + s.dur;
                slot_at(e, ti, ns, &s);
                if (s.start < ns) { s.start = ns; }
            }
            if (j == 0) first = s.start;
            if ((double)s.start - 4.0 * (double)s.dur > Tn) break;   /* nothing later can be due */
            if (!((t->fired >> j) & 1)) {
                double fireT = (double)s.start + slot_offset(e, ti, &s);
                if (fireT <= Tn && (bj < 0 || fireT < best)) { best = fireT; bj = j; bs = s; }
            }
            st = s.start + s.dur;
        }
        if (t->fired & 1) { t->cursor = first + 1; t->fired >>= 1; continue; }
        if (bj < 0) break;
        double when = engine_time(e, t, best);
        run_chains(e, ti, when - 1e-6, emit, user);   /* echoes due before this pulse play first */
        expire(t, when, emit, user);
        fire(e, ti, &bs, when, emit, user);
        t->fired |= (uint8_t)(1u << bj);
    }
}

void e1_advance(e1_engine_t *e, double tick_now, e1_emit_fn emit, void *user)
{
    cc_service(e, emit, user);
    if (!e->running || !isfinite(tick_now)) return;
    if (tick_now < e->tick) tick_now = e->tick;   /* never run time backwards */
    e->tick = tick_now;
    /* The chord track goes first, so tracks that follow it hear a chord
     * change on the same tick. */
    int ci = (int)e->chord_track - 1;
    if (ci >= 0 && ci < E1_TRACKS) advance_track(e, ci, tick_now, emit, user);
    for (int ti = 0; ti < E1_TRACKS; ti++) {
        e1_track_t *t = &e->t[ti];
        if (ti != ci) advance_track(e, ti, tick_now, emit, user);
        run_chains(e, ti, tick_now, emit, user);
        expire(t, tick_now, emit, user);
    }
}

/* ------------------------------------------------------------- audition */

void e1_audition(e1_engine_t *e, int track, int on, e1_emit_fn emit, void *user)
{
    if (track < 0 || track >= E1_TRACKS) return;
    e1_track_t *t = &e->t[track];
    aud_off(t, emit, user);
    if (!on) return;
    int c = e1_display_cycle(t);
    int ports = e1_ports(e, t);
    t->flash = 1;
    if (t->v[E1_P_TYPE] == E1_TYPE_CC) {
        for (int l = 0; l < E1_CC_LANES; l++) {
            int v = cc_eff(e, track, l, c, -1);
            if (v >= 0) cc_send(t, l, v, ports, emit, user);
        }
        return;
    }
    int notes[E1_POOL];
    int n = build_notes(e, track, c, -1, 0, 0, 0, -1, 0, notes);
    int vel = clampi(eff(e, track, E1_P_VELOCITY, c, -1), 1, 127);
    uint8_t ch = (uint8_t)((t->v[E1_P_CHANNEL] - 1) & 0x0F);
    t->aud_ch = ch; t->aud_ports = (uint8_t)ports;
    for (int j = 0; j < n; j++) {
        uint8_t note = (uint8_t)clampi(notes[j], 0, 127);
        emit(user, ports, (uint8_t)(0x90 | ch), note, (uint8_t)vel);
        t->aud_note[t->aud_n++] = note;
    }
}

/* ------------------------------------------------------------- set / get */

/* Parse `val` against a current value. Returns 1 and sets *out. */
static int parse_value(const char *val, int cur, int lo, int hi, const char *const *names, int *out)
{
    if (!val || !*val) return 0;
    if (val[0] == 'd' && (val[1] == '+' || val[1] == '-')) {
        long d = strtol(val + 1, NULL, 10);
        if (d > 100000) d = 100000;
        if (d < -100000) d = -100000;
        *out = clampi(cur + (int)d, lo, hi);
        return 1;
    }
    if (names) {
        for (int i = 0; i <= hi - lo; i++)
            if (strcmp(names[i], val) == 0) { *out = lo + i; return 1; }
    }
    char *end = NULL;
    long v = strtol(val, &end, 10);
    if (end == val) return 0;
    if (v > hi) v = hi;
    if (v < lo) v = lo;
    *out = (int)v;
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

static void pool_sort(e1_track_t *t)
{
    for (int i = 1; i < t->pool_n; i++) {
        uint8_t x = t->pool[i]; int j = i - 1;
        while (j >= 0 && t->pool[j] > x) { t->pool[j + 1] = t->pool[j]; j--; }
        t->pool[j + 1] = x;
    }
}

static void pool_toggle(e1_track_t *t, int note)
{
    note = clampi(note, 0, 127);
    for (int i = 0; i < t->pool_n; i++) {
        if (t->pool[i] == note) {
            for (int j = i; j < t->pool_n - 1; j++) t->pool[j] = t->pool[j + 1];
            t->pool_n--;
            t->pool[t->pool_n] = 0;
            return;
        }
    }
    if (t->pool_n >= E1_POOL) return;
    t->pool[t->pool_n++] = (uint8_t)note;
    pool_sort(t);
}

static void pool_parse(e1_track_t *t, const char *val)
{
    t->pool_n = 0;
    memset(t->pool, 0, sizeof t->pool);
    const char *p = val;
    while (*p && t->pool_n < E1_POOL) {
        char *end = NULL;
        long v = strtol(p, &end, 10);
        if (end == p) break;
        t->pool[t->pool_n++] = (uint8_t)clampi((int)v, 0, 127);
        p = (*end == ',') ? end + 1 : end;
    }
    pool_sort(t);
}

/* raw = 1 when loading a save file: cycle edit must not redirect the write. */
static int set_track(e1_engine_t *e, int ti, const char *key, const char *val, int raw)
{
    e1_track_t *t = &e->t[ti];
    int lane, p;
    uint16_t em = raw ? 0 : t->edit_mask;

    /* c<N>.<key> */
    if (key[0] == 'c' && key[1] >= '0' && key[1] <= '9') {
        const char *dot = strchr(key, '.');
        if (dot) {
            int c = atoi(key + 1) - 1;
            const char *k = dot + 1;
            if (c < 0 || c >= E1_CYCLES) return 0;
            if (strcmp(k, "add") == 0) { t->cadd[c] = strtoull(val, NULL, 16); return 1; }
            if (strcmp(k, "del") == 0) { t->cdel[c] = strtoull(val, NULL, 16); return 1; }
            if ((lane = parse_lane(k, "cc")) >= 0) { t->cc_cyc[c][lane] = (int16_t)clampi(atoi(val), -127, 127); return 1; }
            if ((p = find_param(k)) >= 0 && (P[p].flags & F_CYC)) {
                t->cyc[c][p] = (int16_t)clampi(atoi(val), P[p].lo - P[p].hi, P[p].hi - P[p].lo);
                return 1;
            }
            return 0;
        }
    }
    /* r.<key> */
    if (key[0] == 'r' && key[1] == '.') {
        const char *k = key + 2;
        int v;
        if ((lane = parse_lane(k, "cc")) >= 0) {
            if (!parse_value(val, t->cc_rnd[lane], -100, 100, NULL, &v)) return 0;
            t->cc_rnd[lane] = (int8_t)v; return 1;
        }
        if ((p = find_param(k)) >= 0 && (P[p].flags & (F_RC | F_RR))) {
            if (!parse_value(val, t->rnd[p], -100, 100, NULL, &v)) return 0;
            t->rnd[p] = (int8_t)v; return 1;
        }
        return 0;
    }
    if ((lane = parse_lane(key, "cc")) >= 0) {
        int v;
        if (em && t->cc_val[lane] >= 0) {
            for (int c = 0; c < E1_CYCLES; c++) {
                if (!(em & (1u << c))) continue;
                int cur = clampi(t->cc_val[lane] + t->cc_cyc[c][lane], 0, 127);
                if (!parse_value(val, cur, 0, 127, NULL, &v)) return 0;
                t->cc_cyc[c][lane] = (int16_t)(v - t->cc_val[lane]);
            }
        } else {
            if (!parse_value(val, t->cc_val[lane], -1, 127, NULL, &v)) return 0;
            t->cc_val[lane] = (int16_t)v;
        }
        if (t->cc_val[lane] >= 0) t->cc_dirty |= (1u << lane);
        return 1;
    }
    if ((lane = parse_lane(key, "ccn")) >= 0) {
        int v;
        if (!parse_value(val, t->cc_num[lane], 0, 127, NULL, &v)) return 0;
        t->cc_num[lane] = (uint8_t)v;
        if (t->cc_val[lane] >= 0) t->cc_dirty |= (1u << lane);
        return 1;
    }
    if (strcmp(key, "toggle") == 0) { toggle_step(e, ti, atoi(val)); return 1; }
    if (strcmp(key, "addmask") == 0) { t->add_mask = strtoull(val, NULL, 16); return 1; }
    if (strcmp(key, "delmask") == 0) { t->del_mask = strtoull(val, NULL, 16); return 1; }
    if (strcmp(key, "retrig") == 0) { t->retrig = strtoull(val, NULL, 16); return 1; }
    if (strcmp(key, "retrigtoggle") == 0) {
        int st = atoi(val);
        if (st < 0 || st >= 64) return 0;
        t->retrig ^= 1ULL << st;
        return 1;
    }
    if (strcmp(key, "rseed") == 0) { t->rseed = (uint32_t)strtoul(val, NULL, 10); return 1; }
    if (strcmp(key, "dice") == 0) {
        const char *q = val;
        for (int l = 0; l < E1_DICE_LAYERS; l++) {
            char *end = NULL;
            t->dseed[l] = (uint32_t)strtoul(q, &end, 10);
            q = (end && *end == ',') ? end + 1 : (end ? end : q);
        }
        return 1;
    }
    if (strcmp(key, "dicewas") == 0) {
        int a = 0, b = 0, c2 = 0;
        sscanf(val, "%d,%d,%d", &a, &b, &c2);
        t->dwas[0] = (uint8_t)clampi(a, 0, E1_STYLE_DICE);
        t->dwas[1] = (uint8_t)clampi(b, 0, E1_GROOVE_DICE);
        t->dwas[2] = (uint8_t)clampi(c2, 0, E1_PHRASE_DICE);
        return 1;
    }
    if (strcmp(key, "roll") == 0 || strcmp(key, "cleardice") == 0) {
        int l = dice_layer(val), roll = key[0] == 'r';
        if (l < 0) return 0;
        int cc = t->v[E1_P_TYPE] == E1_TYPE_CC;          /* a CC track has a rhythm and nothing else */
        dice_push(t);
        for (int k = 0; k < E1_DICE_LAYERS; k++) {
            if (l != E1_DICE_LAYERS && l != k) continue;
            if (cc && k != E1_DICE_RHYTHM) continue;
            if (!roll) { dice_clear(t, k); continue; }
            t->dseed[k] = new_seed(e, ti, k);
            dice_select(e, t, k);
        }
        /* Rhythm alone on a full track: thin it a little so the roll is heard. */
        if (roll && l == E1_DICE_RHYTHM && t->v[E1_P_DENSITY] >= 16) t->v[E1_P_DENSITY] = 12;
        return 1;
    }
    if (strcmp(key, "rollback") == 0) return dice_back(t);
    if (strcmp(key, "clearedits") == 0) {
        if (em) { for (int c = 0; c < E1_CYCLES; c++) if (em & (1u << c)) t->cadd[c] = t->cdel[c] = 0; }
        else t->add_mask = t->del_mask = 0;
        return 1;
    }
    if (strcmp(key, "pool") == 0) { pool_parse(t, val); return 1; }
    if (strcmp(key, "pooltoggle") == 0) { pool_toggle(t, atoi(val)); return 1; }
    if (strcmp(key, "name") == 0) {
        snprintf(t->name, sizeof t->name, "%s", val);
        for (char *q = t->name; *q; q++) if ((unsigned char)*q < 0x20 || *q == 0x7F || *q == '=' || *q == '"' || *q == '\\') *q = ' ';
        return 1;
    }
    /* s<N>.<key>: one step's offset, as saved */
    if (key[0] == 's' && key[1] >= '0' && key[1] <= '9') {
        const char *dot = strchr(key, '.');
        if (dot) {
            int st = atoi(key + 1) - 1, r;
            if (st < 0 || st >= 64 || (p = find_param(dot + 1)) < 0 || !(P[p].flags & F_STEP)) return 0;
            if ((r = sed_get(t, st)) < 0) return 0;
            t->sed[r][p] = (int16_t)clampi(atoi(val), P[p].lo - P[p].hi, P[p].hi - P[p].lo);
            return 1;
        }
    }
    if (strcmp(key, "stepedit") == 0) {
        t->step_mask = strtoull(val, NULL, 16);
        return 1;
    }
    if (strcmp(key, "clearstep") == 0) {
        int st = atoi(val) - 1;
        if (st < 0) { memset(t->sed_step, 0, sizeof t->sed_step); t->step_mask = 0; return 1; }   /* 0 = every step */
        int r = sed_find(t, st);
        if (r >= 0) t->sed_step[r] = 0;
        return 1;
    }
    if (strcmp(key, "edit") == 0) {
        t->edit_mask = (uint16_t)strtoul(val, NULL, 16);
        return 1;
    }
    if (strcmp(key, "clearcycle") == 0) {
        int c = atoi(val) - 1;
        if (c < 0 || c >= E1_CYCLES) return 0;
        memset(t->cyc[c], 0, sizeof t->cyc[c]);
        memset(t->cc_cyc[c], 0, sizeof t->cc_cyc[c]);
        t->cadd[c] = t->cdel[c] = 0;
        return 1;
    }
    if (strcmp(key, "copycycle") == 0) {
        int a = 0, b = 0;
        if (sscanf(val, "%d %d", &a, &b) != 2) return 0;
        a--; b--;
        if (a < 0 || b < 0 || a >= E1_CYCLES || b >= E1_CYCLES) return 0;
        memcpy(t->cyc[b], t->cyc[a], sizeof t->cyc[a]);
        memcpy(t->cc_cyc[b], t->cc_cyc[a], sizeof t->cc_cyc[a]);
        t->cadd[b] = t->cadd[a]; t->cdel[b] = t->cdel[a];
        return 1;
    }
    if (strcmp(key, "clearparam") == 0) {
        if ((lane = parse_lane(val, "cc")) >= 0) { for (int c = 0; c < E1_CYCLES; c++) t->cc_cyc[c][lane] = 0; t->cc_rnd[lane] = 0; return 1; }
        if ((p = find_param(val)) < 0) return 0;
        for (int c = 0; c < E1_CYCLES; c++) t->cyc[c][p] = 0;
        for (int i = 0; i < E1_SEDITS; i++) t->sed[i][p] = 0;
        t->rnd[p] = 0;
        return 1;
    }
    if (strcmp(key, "clearrandom") == 0) {
        memset(t->rnd, 0, sizeof t->rnd);
        memset(t->cc_rnd, 0, sizeof t->cc_rnd);
        return 1;
    }

    if ((p = find_param(key)) < 0) return 0;
    if (p == E1_P_DIR && val) {                  /* saves from before Dir had four ways */
        if (strcmp(val, "Up") == 0) val = "Forward";
        else if (strcmp(val, "Down") == 0) val = "Backward";
    }
    const pdesc_t *d = &P[p];
    int v;
    if (!raw && t->step_mask && (d->flags & F_STEP)) {
        int ok = 0;
        for (int st = 0; st < 64; st++) {
            if (!(t->step_mask & (1ULL << st))) continue;
            int r = sed_get(t, st);
            if (r < 0) continue;                 /* every row is taken */
            int cur = clampi(t->v[p] + t->sed[r][p], d->lo, d->hi);
            if (!parse_value(val, cur, d->lo, d->hi, d->names, &v)) return 0;
            t->sed[r][p] = (int16_t)(v - t->v[p]);
            ok = 1;
        }
        return ok;
    }
    if (em && (d->flags & F_CYC)) {
        for (int c = 0; c < E1_CYCLES; c++) {
            if (!(em & (1u << c))) continue;
            int cur = clampi(t->v[p] + t->cyc[c][p], d->lo, d->hi);
            if (!parse_value(val, cur, d->lo, d->hi, d->names, &v)) return 0;
            t->cyc[c][p] = (int16_t)(v - t->v[p]);
        }
        return 1;
    }
    if (!parse_value(val, t->v[p], d->lo, d->hi, d->names, &v)) return 0;
    int old = t->v[p];
    t->v[p] = (int16_t)v;
    if (raw) return 1;
    if (p == E1_P_PULSES && v != old) t->del_mask = 0;      /* removed hits come back, as on the T-1 */
    if (p == E1_P_CYCLES && t->cur_cycle >= v) t->cur_cycle = 0;
    return 1;
}

static int set_global(e1_engine_t *e, const char *key, const char *val)
{
    if (strcmp(key, "dest") == 0) {
        int v;
        if (!parse_value(val, e->dest, 0, 2, DEST_NAMES, &v)) return 0;
        e->dest = (uint8_t)v;
        return 1;
    }
    if (strcmp(key, "seed") == 0) { e->seed = (uint32_t)strtoul(val, NULL, 10); return 1; }
    int v;
    if (strcmp(key, "songroot") == 0) {
        if (!parse_value(val, e->song_root, 0, 11, ROOT_NAMES + 1, &v)) return 0;
        e->song_root = (uint8_t)v;
        return 1;
    }
    if (strcmp(key, "songscale") == 0) {
        if (!parse_value(val, e->song_scale, 0, E1_SCALES - 1, SCALE_NAMES + 1, &v)) return 0;
        e->song_scale = (uint8_t)v;
        return 1;
    }
    if (strcmp(key, "chordtrack") == 0) {
        if (!parse_value(val, e->chord_track, 0, E1_TRACKS, NULL, &v)) return 0;
        e->chord_track = (uint8_t)v;
        return 1;
    }
    return 0;
}

int e1_set(e1_engine_t *e, int track, const char *key, const char *val)
{
    if (!key || !val) return 0;
    if (track < 0) return set_global(e, key, val);
    if (track >= E1_TRACKS) return 0;
    return set_track(e, track, key, val, 0);
}

static int shown(const e1_engine_t *e, int ti, int p)
{
    const e1_track_t *t = &e->t[ti];
    int c = t->edit_mask ? lowest_bit(t->edit_mask) : -1;
    int v = t->v[p];
    if (t->step_mask && (P[p].flags & F_STEP)) {
        int st = 0; while (st < 63 && !(t->step_mask & (1ULL << st))) st++;
        int r = sed_find(t, st);
        if (r >= 0) v += t->sed[r][p];
    } else if (c >= 0 && (P[p].flags & F_CYC)) v += t->cyc[c][p];
    return clampi(v, P[p].lo, P[p].hi);
}

int e1_get_num(const e1_engine_t *e, int track, int i)
{
    if (track < 0 || track >= E1_TRACKS || i < 0 || i >= E1_NP) return 0;
    return shown(e, track, i);
}

int e1_get(const e1_engine_t *e, int track, const char *key, char *buf, int len)
{
    if (!key || !buf || len <= 0) return -1;
    if (track < 0) {
        if (strcmp(key, "dest") == 0) return snprintf(buf, (size_t)len, "%s", DEST_NAMES[e->dest % 3]);
        if (strcmp(key, "seed") == 0) return snprintf(buf, (size_t)len, "%u", (unsigned)e->seed);
        if (strcmp(key, "songroot") == 0) return snprintf(buf, (size_t)len, "%s", ROOT_NAMES[1 + e->song_root % 12]);
        if (strcmp(key, "songscale") == 0) return snprintf(buf, (size_t)len, "%s", SCALE_NAMES[1 + rscale(e, e->song_scale)]);
        if (strcmp(key, "chordtrack") == 0) return snprintf(buf, (size_t)len, "%d", e->chord_track);
        return -1;
    }
    if (track >= E1_TRACKS) return -1;
    const e1_track_t *t = &e->t[track];
    int lane, p;
    if (key[0] == 'r' && key[1] == '.') {
        if ((lane = parse_lane(key + 2, "cc")) >= 0) return snprintf(buf, (size_t)len, "%d", t->cc_rnd[lane]);
        if ((p = find_param(key + 2)) >= 0) return snprintf(buf, (size_t)len, "%d", t->rnd[p]);
        return -1;
    }
    if ((lane = parse_lane(key, "cc")) >= 0) {
        int c = t->edit_mask ? lowest_bit(t->edit_mask) : -1;
        int v = t->cc_val[lane];
        if (v >= 0 && c >= 0) v = clampi(v + t->cc_cyc[c][lane], 0, 127);
        return snprintf(buf, (size_t)len, "%d", v);
    }
    if ((lane = parse_lane(key, "ccn")) >= 0) return snprintf(buf, (size_t)len, "%d", t->cc_num[lane]);
    if (strcmp(key, "name") == 0) return snprintf(buf, (size_t)len, "%s", t->name);
    if (strcmp(key, "retrig") == 0) return snprintf(buf, (size_t)len, "%llx", (unsigned long long)t->retrig);
    if (strcmp(key, "rseed") == 0) return snprintf(buf, (size_t)len, "%u", (unsigned)t->rseed);
    if (strcmp(key, "dice") == 0)
        return snprintf(buf, (size_t)len, "%u,%u,%u,%u,%u", (unsigned)t->dseed[0], (unsigned)t->dseed[1],
                        (unsigned)t->dseed[2], (unsigned)t->dseed[3], (unsigned)t->dseed[4]);
    if (strcmp(key, "pool") == 0) {
        int pos = 0;
        buf[0] = 0;
        for (int i = 0; i < t->pool_n && pos < len - 5; i++)
            pos += snprintf(buf + pos, (size_t)(len - pos), "%s%d", i ? "," : "", t->pool[i]);
        return pos;
    }
    if ((p = find_param(key)) < 0) return -1;
    int v = shown(e, track, p);
    if (P[p].names) return snprintf(buf, (size_t)len, "%s", P[p].names[v - P[p].lo]);
    return snprintf(buf, (size_t)len, "%d", v);
}

/* ------------------------------------------------------------ save text */

static int appendf(char *buf, int len, int pos, const char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
#include <stdarg.h>
static int appendf(char *buf, int len, int pos, const char *fmt, ...)
{
    if (pos >= len - 1) return pos;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, (size_t)(len - pos), fmt, ap);
    va_end(ap);
    if (n < 0) return pos;
    return pos + n >= len ? len - 1 : pos + n;
}

int e1_dump_track(const e1_engine_t *e, int track, char *buf, int len)
{
    if (track < 0 || track >= E1_TRACKS || len <= 0) return 0;
    const e1_track_t *t = &e->t[track];
    int pos = 0;
    buf[0] = 0;
    pos = appendf(buf, len, pos, "name=%s\n", t->name);
    for (int p = 0; p < E1_NP; p++) {
        if (P[p].names) pos = appendf(buf, len, pos, "%s=%s\n", P[p].key, P[p].names[clampi(t->v[p], P[p].lo, P[p].hi) - P[p].lo]);
        else pos = appendf(buf, len, pos, "%s=%d\n", P[p].key, t->v[p]);
        if (t->rnd[p]) pos = appendf(buf, len, pos, "r.%s=%d\n", P[p].key, t->rnd[p]);
    }
    pos = appendf(buf, len, pos, "pool=");
    for (int i = 0; i < t->pool_n; i++) pos = appendf(buf, len, pos, "%s%d", i ? "," : "", t->pool[i]);
    pos = appendf(buf, len, pos, "\n");
    if (t->add_mask) pos = appendf(buf, len, pos, "addmask=%llx\n", (unsigned long long)t->add_mask);
    if (t->del_mask) pos = appendf(buf, len, pos, "delmask=%llx\n", (unsigned long long)t->del_mask);
    if (t->retrig) pos = appendf(buf, len, pos, "retrig=%llx\n", (unsigned long long)t->retrig);
    if (t->rseed) pos = appendf(buf, len, pos, "rseed=%u\n", (unsigned)t->rseed);
    if (e1_dice_rolled(t))
        pos = appendf(buf, len, pos, "dice=%u,%u,%u,%u,%u\n", (unsigned)t->dseed[0], (unsigned)t->dseed[1],
                      (unsigned)t->dseed[2], (unsigned)t->dseed[3], (unsigned)t->dseed[4]);
    if (t->dwas[0] || t->dwas[1] || t->dwas[2])
        pos = appendf(buf, len, pos, "dicewas=%d,%d,%d\n", t->dwas[0], t->dwas[1], t->dwas[2]);
    for (int l = 0; l < E1_CC_LANES; l++) {
        pos = appendf(buf, len, pos, "ccn%d=%d\n", l + 1, t->cc_num[l]);
        if (t->cc_val[l] >= 0) pos = appendf(buf, len, pos, "cc%d=%d\n", l + 1, t->cc_val[l]);
        if (t->cc_rnd[l]) pos = appendf(buf, len, pos, "r.cc%d=%d\n", l + 1, t->cc_rnd[l]);
    }
    for (int i = 0; i < E1_SEDITS; i++) {
        if (!t->sed_step[i]) continue;
        for (int p = 0; p < E1_NP; p++)
            if (t->sed[i][p]) pos = appendf(buf, len, pos, "s%d.%s=%d\n", t->sed_step[i], P[p].key, t->sed[i][p]);
    }
    for (int c = 0; c < E1_CYCLES; c++) {
        for (int p = 0; p < E1_NP; p++)
            if (t->cyc[c][p]) pos = appendf(buf, len, pos, "c%d.%s=%d\n", c + 1, P[p].key, t->cyc[c][p]);
        for (int l = 0; l < E1_CC_LANES; l++)
            if (t->cc_cyc[c][l]) pos = appendf(buf, len, pos, "c%d.cc%d=%d\n", c + 1, l + 1, t->cc_cyc[c][l]);
        if (t->cadd[c]) pos = appendf(buf, len, pos, "c%d.add=%llx\n", c + 1, (unsigned long long)t->cadd[c]);
        if (t->cdel[c]) pos = appendf(buf, len, pos, "c%d.del=%llx\n", c + 1, (unsigned long long)t->cdel[c]);
    }
    return pos;
}

int e1_dump_global(const e1_engine_t *e, char *buf, int len)
{
    if (len <= 0) return 0;
    return snprintf(buf, (size_t)len, "dest=%s\nseed=%u\nsongroot=%s\nsongscale=%s\nchordtrack=%d\n", DEST_NAMES[e->dest % 3],
                    (unsigned)e->seed, ROOT_NAMES[1 + e->song_root % 12], SCALE_NAMES[1 + rscale(e, e->song_scale)], e->chord_track);
}

typedef int (*line_fn)(e1_engine_t *e, int track, const char *key, const char *val);

static void each_line(e1_engine_t *e, int track, const char *text, line_fn fn)
{
    char line[160];
    const char *p = text;
    while (p && *p) {
        const char *nl = strchr(p, '\n');
        size_t n = nl ? (size_t)(nl - p) : strlen(p);
        if (n >= sizeof line) n = sizeof line - 1;
        memcpy(line, p, n);
        line[n] = 0;
        if (n && line[n - 1] == '\r') line[n - 1] = 0;
        char *eq = strchr(line, '=');
        if (eq) { *eq = 0; fn(e, track, line, eq + 1); }
        p = nl ? nl + 1 : NULL;
    }
}

static int load_line(e1_engine_t *e, int track, const char *key, const char *val)
{
    return set_track(e, track, key, val, 1);
}
static int load_gline(e1_engine_t *e, int track, const char *key, const char *val)
{
    (void)track;
    return set_global(e, key, val);
}

void e1_load_track(e1_engine_t *e, int track, const char *text)
{
    if (track < 0 || track >= E1_TRACKS || !text) return;
    /* Reset the saved part only; a playing track keeps its place. */
    e1_track_t fresh;
    e1_track_defaults(&fresh, track);
    e1_copy_stored(&e->t[track], &fresh);
    each_line(e, track, text, load_line);
    for (int l = 0; l < E1_CC_LANES; l++) if (e->t[track].cc_val[l] >= 0) e->t[track].cc_dirty |= 1u << l;
}

void e1_load_global(e1_engine_t *e, const char *text)
{
    if (text) each_line(e, -1, text, load_gline);
}
