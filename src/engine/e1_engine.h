/*
 * e1_engine.h — Move E-1 sequencing engine.
 *
 * Pure C, no host calls, no allocation, no file I/O. Everything the two
 * Schwung modules (the E-1 overtake tool and the E-1 Lite MIDI FX) do
 * musically lives here, so the two cannot drift apart.
 *
 * Time is counted in ticks at E1_PPQN (96 per quarter note). The host
 * wrapper converts its clock (Move's beat position, an internal sample
 * counter, MIDI clock) into a monotonically rising tick count and calls
 * e1_advance() once per audio block; the engine emits every event whose
 * time has been reached through the emit callback.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */
#ifndef E1_ENGINE_H
#define E1_ENGINE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define E1_TRACKS        16
#define E1_MAX_STEPS     64
#define E1_CC_LANES      16
#define E1_PPQN          96
#define E1_TICKS_PER_BAR (E1_PPQN * 4)

/* Track types */
enum { E1_TYPE_NOTE = 0, E1_TYPE_CC = 1 };

/* Port bits */
enum { E1_PORT_MOVE = 1, E1_PORT_COMPUTER = 2 };

/* Global destination (where tracks with output = "Global" play) */
enum { E1_DEST_MOVE = 0, E1_DEST_COMPUTER = 1, E1_DEST_BOTH = 2 };

/* Per-track output override */
enum { E1_OUT_GLOBAL = 0, E1_OUT_MOVE = 1, E1_OUT_COMPUTER = 2, E1_OUT_BOTH = 3 };

/* CC send modes */
enum { E1_CC_ON_PULSE = 0, E1_CC_ON_CHANGE = 1, E1_CC_GLIDE = 2 };

/* Note modes */
enum { E1_NOTE_DRUM = 0, E1_NOTE_MELODIC = 1 };

#define E1_DIVISIONS 15
extern const char *const e1_division_names[E1_DIVISIONS];
extern const int e1_division_ticks[E1_DIVISIONS];

#define E1_LENGTHS 10
extern const char *const e1_length_names[E1_LENGTHS];
extern const int e1_length_bars[E1_LENGTHS];   /* 0 = off (free-running) */

typedef struct {
    /* --- stored (saved with the project) --- */
    uint8_t type;          /* E1_TYPE_* */
    uint8_t steps;         /* 1..64 */
    uint8_t pulses;        /* 0..steps */
    uint8_t rotate;        /* 0..steps-1 */
    uint8_t division;      /* index into e1_division_ticks */
    uint8_t length;        /* index into e1_length_bars */
    uint8_t cycles;        /* 1..16 (stored; cycles play in a later phase) */
    uint8_t prob;          /* 0..100 % */
    uint8_t fill;          /* extra pulses while a fill is held, 0..16 */
    uint64_t add_mask;     /* steps added by hand */
    uint64_t del_mask;     /* generated steps removed by hand */

    uint8_t channel;       /* 0..15 */
    uint8_t output;        /* E1_OUT_* */
    uint8_t note_mode;     /* E1_NOTE_* */
    uint8_t note;          /* 0..127 */
    uint8_t velocity;      /* 1..127 */
    uint8_t sustain;       /* 1..100; 50 = one full step, as on the T-1 */
    int8_t  timing;        /* -50..50, % of a step, moves every second step (swing) */
    int8_t  delay;         /* -50..50, % of a step, moves the whole track */
    uint8_t muted;

    int8_t  cc_val[E1_CC_LANES];   /* -1 = lane off, else 0..127 */
    uint8_t cc_num[E1_CC_LANES];   /* 0..127 */
    uint8_t cc_mode;               /* E1_CC_* */
    uint8_t slew;                  /* 0..100 */

    /* --- runtime (not saved) --- */
    int64_t next_k;        /* next step slot to evaluate */
    int     cur_step;      /* step index last played, -1 before start */
    uint8_t gate_open;     /* a note is sounding */
    uint8_t gate_note;
    uint8_t gate_ch;
    uint8_t gate_ports;
    double  gate_off;      /* tick at which the sounding note ends */
    float   cc_cur[E1_CC_LANES];   /* glide position */
    int16_t cc_sent[E1_CC_LANES];  /* last value sent, -1 = never */
    int8_t  cc_target[E1_CC_LANES];
    uint32_t cc_dirty;             /* lanes edited since last send (on-change) */
    uint8_t flash;                 /* set when the track fired, UI clears */

    /* sequence position: s = k - step_off counts played steps */
    int64_t step_off;
    int64_t last_s;

    /* performance moves (runtime, never saved) */
    uint8_t p_fill;        /* add fill pulses */
    uint8_t p_ratchet;     /* 0/1 off, 2..4 hits per step */
    int8_t  p_speed;       /* -1 half time, 0, +1 double time */
    uint8_t p_reverse;
    uint8_t p_freeze;      /* 0 off, else loop the last N steps */
    int64_t freeze_s0;
    uint8_t p_thin;        /* drop about half the hits */
    uint8_t p_accent;      /* velocity 127 */
    uint8_t p_solo_mute;   /* silenced by someone else's solo */

    /* ratchet in flight */
    uint8_t rat_left;
    uint8_t rat_vel;
    double  rat_next, rat_dt, rat_len;
} e1_track_t;

/* Stored (saved) fields end where the runtime block starts. */
#define E1_TRACK_STORED_BYTES offsetof(e1_track_t, next_k)

typedef struct {
    e1_track_t t[E1_TRACKS];
    uint8_t dest;          /* E1_DEST_* */
    uint32_t seed;
    uint8_t fill_held;     /* unused, kept for layout; fills are per track */
    int running;
    double tick;           /* last tick advanced to */
    double origin;         /* tick at which step slot 0 played */
    uint32_t cc_rr;        /* round-robin cursor for the CC budget */
} e1_engine_t;

/* status, data1, data2, ports (E1_PORT_* bits) */
typedef void (*e1_emit_fn)(void *user, int ports, uint8_t st, uint8_t d1, uint8_t d2);

void e1_init(e1_engine_t *e);
void e1_track_defaults(e1_track_t *t, int index);

/* Transport. start() rewinds every track to step 0 at `tick`. */
void e1_start(e1_engine_t *e, double tick);
/* Start with step 0 at `origin` but continue from `now` (joining a transport
 * that is already running) without replaying the steps in between. */
void e1_join(e1_engine_t *e, double origin, double now);
void e1_stop(e1_engine_t *e, e1_emit_fn emit, void *user);

/* Run every event with time <= tick_now. Call once per block. */
void e1_advance(e1_engine_t *e, double tick_now, e1_emit_fn emit, void *user);

/* Performance moves on every track whose bit is set in `mask`.
 *   move: "fill", "ratchet" (2..4, 0 off), "speed" (-1, 0, 1), "reverse",
 *         "freeze" (steps to loop, 0 off), "thin", "accent", "solo" (1 on,
 *         0 off: tracks outside mask fall silent), "restart" (next step is
 *         step 1), "rotate" (adds value to Rotate, a saved edit), "clear"
 *         (every move off). */
void e1_perf(e1_engine_t *e, uint32_t mask, const char *move, int value);

/* Copy only the saved fields of a track (pattern recall, Temp restore).
 * Runtime state such as position and sounding notes is kept. */
void e1_copy_stored(e1_track_t *dst, const e1_track_t *src);

/* Keep a running track's step continuity after its Division changed under it. */
void e1_reanchor(e1_engine_t *e, int track);

/* Close any sounding notes (used on stop, mute, unload). */
void e1_all_notes_off(e1_engine_t *e, e1_emit_fn emit, void *user);

/* Euclidean hit test for step `i` (0-based), honouring rotate, manual
 * edits and, when `fill` is set, the track's fill pulses. */
int  e1_hit(const e1_track_t *t, int i, int fill);
uint64_t e1_hit_mask(const e1_track_t *t, int fill);

/* Toggle a hit by hand (step row). */
void e1_toggle_step(e1_track_t *t, int i);

/* Effective port bits for a track given the global destination. */
int  e1_ports(const e1_engine_t *e, const e1_track_t *t);

/* Parameters.
 *   track  0..15, or -1 for global keys.
 *   val    an absolute value ("12"), an enum name ("1/16"), or a relative
 *          step ("d+1" / "d-3") which the engine clamps. Plain negative
 *          numbers stay absolute, so timing "-20" means -20.
 * Returns 1 when the key was recognised. */
int  e1_set(e1_engine_t *e, int track, const char *key, const char *val);
int  e1_get(const e1_engine_t *e, int track, const char *key, char *buf, int len);

/* Track parameters by index, as numbers (enums as their index). Used for
 * the compact UI snapshot. */
int  e1_param_count(void);
const char *e1_param_key(int i);
int  e1_get_num(const e1_engine_t *e, int track, int i);

/* Persistence: one track or the globals, as "key=value\n" lines. */
int  e1_dump_track(const e1_engine_t *e, int track, char *buf, int len);
int  e1_dump_global(const e1_engine_t *e, char *buf, int len);
void e1_load_lines(e1_engine_t *e, int track, const char *text);

/* Deterministic per-event random in [0, 99]. */
int  e1_chance(uint32_t seed, int track, int64_t k);

#ifdef __cplusplus
}
#endif
#endif
