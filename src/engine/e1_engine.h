/*
 * e1_engine.h — Move E-1 sequencing engine (second generation).
 *
 * Pure C, no host calls, no allocation, no file I/O.
 *
 * Time is counted in ticks at E1_PPQN (96 per quarter note). The host
 * wrapper turns its clock into a rising tick count and calls e1_advance()
 * once per audio block; the engine emits every event whose time has come.
 *
 * A track's position is always derived from the transport time, never
 * accumulated, so a track cannot drift: change Steps or Division while
 * playing and the track is exactly where it would have been had it always
 * been set that way.
 *
 * Each track has up to 16 cycles. One cycle is one pass through the steps.
 * Every lockable parameter has a per-cycle offset from the track's base
 * value, so cycles differ by what was edited in them and still follow when
 * the base value moves.
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
#define E1_CYCLES        16
#define E1_POOL          8      /* notes in a track's pitch pool */
#define E1_VOICES        12     /* sounding notes tracked per track */
#define E1_CHAINS        3      /* repeat chains in flight per track */
#define E1_NAME_LEN      13
#define E1_SEDITS        16     /* steps per track that can carry their own values */
#define E1_PPQN          96
#define E1_TICKS_PER_BAR (E1_PPQN * 4)
#define E1_REPEATS_INF   49     /* Repeats value that means "until the next pulse" */

enum { E1_TYPE_NOTE = 0, E1_TYPE_CC = 1 };
enum { E1_PORT_MOVE = 1, E1_PORT_COMPUTER = 2 };
enum { E1_DEST_MOVE = 0, E1_DEST_COMPUTER = 1, E1_DEST_BOTH = 2 };
enum { E1_OUT_GLOBAL = 0, E1_OUT_MOVE = 1, E1_OUT_COMPUTER = 2, E1_OUT_BOTH = 3 };
enum { E1_CC_ON_PULSE = 0, E1_CC_ON_CHANGE = 1, E1_CC_GLIDE = 2 };

/* Track parameters. All live in e1_track_t.v[] as API values. */
enum {
    E1_P_TYPE = 0, E1_P_STEPS, E1_P_PULSES, E1_P_ROTATE, E1_P_DIVISION, E1_P_LENGTH,
    E1_P_CYCLES, E1_P_VELOCITY, E1_P_PROB, E1_P_SUSTAIN, E1_P_REPEATS, E1_P_TIME,
    E1_P_RAMP, E1_P_PACE, E1_P_FILL, E1_P_DELAY, E1_P_PITCH, E1_P_HARMONY,
    E1_P_VOICING, E1_P_STYLE, E1_P_RANGE, E1_P_PHRASE, E1_P_SCALE, E1_P_ROOT,
    E1_P_ACCENT, E1_P_GROOVE, E1_P_TIMING, E1_P_SWINGGRID, E1_P_RANDOM, E1_P_RATE,
    E1_P_CHANNEL, E1_P_OUTPUT, E1_P_MUTE, E1_P_DIR, E1_P_TAIL, E1_P_PHRASERATE,
    E1_P_CCMODE, E1_P_SLEW, E1_P_LOOP,
    E1_P_DENSITY, E1_P_DICETYPE, E1_P_AUTOROLL, E1_P_DICELEN,
    E1_NP
};

#define E1_DIVISIONS 15
extern const char *const e1_division_names[E1_DIVISIONS];
extern const int e1_division_ticks[E1_DIVISIONS];
#define E1_LENGTHS 10
extern const int e1_length_bars[E1_LENGTHS];   /* 0 = off (free-running) */
#define E1_LOOPS 5
extern const int e1_loop_bars[E1_LOOPS];       /* Loop lock: 0 = free, else bars */

/* Dice: five lanes of 16 steps, each made from its own seed. A lane that
 * was never rolled has seed 0. Notes plays when Style is Dice, Accents when
 * Groove is Dice, Octaves when Phrase is Dice, Lengths when DiceLen is On;
 * Rhythm is the order in which Density takes hits away. */
enum { E1_DICE_NOTES = 0, E1_DICE_ACCENTS, E1_DICE_LENGTHS, E1_DICE_OCTAVES, E1_DICE_RHYTHM, E1_DICE_LAYERS };
#define E1_STYLE_DICE   6
#define E1_GROOVE_DICE  8
#define E1_PHRASE_DICE  8
#define E1_DICE_HISTORY 4

typedef struct { uint32_t seed[E1_DICE_LAYERS]; uint8_t style, groove, phrase, dlen; } e1_dice_snap_t;

typedef struct { uint8_t on, note, ch, ports; double off; } e1_voice_t;

typedef struct {
    int     left;          /* repeats still to play; 0 = idle */
    int     k;             /* repeats played so far */
    double  next;          /* engine tick of the next repeat */
    double  interval;      /* engine ticks to the one after */
    double  pace;          /* interval multiplier per repeat */
    double  len;           /* note length in engine ticks */
    int     vel, ramp;     /* start velocity and change per repeat */
    int     cyc, step, pidx;
    int     dpos;          /* dice lane step at the pulse */
    int     shift;         /* phrase shift in scale degrees at the pulse */
    int     ridx;          /* random step at the pulse */
    int     prob;          /* per-note drop chance, 0..100 */
    uint32_t id;
} e1_chain_t;

typedef struct {
    /* ---- stored: saved with a pattern ---- */
    int16_t  v[E1_NP];                         /* base values */
    int16_t  cyc[E1_CYCLES][E1_NP];            /* per-cycle offsets, 0 = no edit */
    int8_t   rnd[E1_NP];                       /* random amount, -100..100 */
    uint8_t  sed_step[E1_SEDITS];              /* step + 1 of each edited step, 0 = free */
    int16_t  sed[E1_SEDITS][E1_NP];            /* per-step offsets */
    uint64_t add_mask, del_mask;               /* hand edits, all cycles */
    uint64_t cadd[E1_CYCLES], cdel[E1_CYCLES]; /* hand edits in one cycle */
    uint64_t retrig;                           /* steps that restart random, groove, phrase and style */
    uint32_t rseed;                            /* the track's own dice; 0 = the set's seed alone */
    uint32_t dseed[E1_DICE_LAYERS];            /* dice lanes; 0 = never rolled */
    uint8_t  dwas[3];                          /* style, groove, phrase + 1 from before their dice */
    uint8_t  pool[E1_POOL];                    /* MIDI notes, ascending */
    uint8_t  pool_n;
    int16_t  cc_val[E1_CC_LANES];              /* -1 = lane off */
    uint8_t  cc_num[E1_CC_LANES];
    int16_t  cc_cyc[E1_CYCLES][E1_CC_LANES];
    int8_t   cc_rnd[E1_CC_LANES];
    char     name[E1_NAME_LEN];

    /* ---- runtime: never saved ---- */
    int64_t  cursor;       /* track time; slots starting before it are done */
    uint8_t  fired;        /* slots ahead of the cursor that already sounded */
    double   toff;         /* engine ticks after origin where track time 0 is */
    int      cur_step;     /* step last played, -1 before start */
    int      cur_cycle;
    uint16_t edit_mask;    /* cycles picked for editing; they loop */
    uint64_t step_mask;    /* steps picked for editing */
    uint8_t  flash;        /* set when the track sounded, UI clears */
    e1_voice_t voice[E1_VOICES];
    e1_chain_t chain[E1_CHAINS];
    float    cc_cur[E1_CC_LANES];
    int16_t  cc_sent[E1_CC_LANES];
    int16_t  cc_target[E1_CC_LANES];
    uint32_t cc_dirty;

    /* performance moves */
    uint8_t  p_fill, p_ratchet, p_reverse, p_accent, p_solo_mute;
    int8_t   p_speed;      /* -1 half time, 0, +1 double time */
    uint8_t  p_freeze;     /* 0 off, else loop the last N steps */
    int64_t  fz_anchor;    /* track time where the frozen loop restarts */
    int64_t  fz_span, fz_dt, fz_j;
    int64_t  rt_T;         /* track time of the last retrigger step played */
    uint8_t  aud_n, aud_ch, aud_ports, aud_note[E1_POOL];   /* notes held by an audition */
    int64_t  ar_idx;       /* auto roll period last seen */
    uint8_t  dhist_n;      /* earlier rolls kept for Back */
    e1_dice_snap_t dhist[E1_DICE_HISTORY];
} e1_track_t;

#define E1_TRACK_STORED_BYTES offsetof(e1_track_t, cursor)

typedef struct {
    e1_track_t t[E1_TRACKS];
    uint8_t  dest;         /* E1_DEST_* */
    uint32_t seed;
    int      running;
    double   tick;         /* last tick advanced to */
    double   origin;       /* tick at which track time 0 fell */
    uint32_t cc_rr;
    uint32_t nonce;        /* stirs one-off random picks */
} e1_engine_t;

/* ports (E1_PORT_* bits), status, data1, data2 */
typedef void (*e1_emit_fn)(void *user, int ports, uint8_t st, uint8_t d1, uint8_t d2);

void e1_init(e1_engine_t *e);                      /* the starter set */
void e1_track_defaults(e1_track_t *t, int index);  /* an empty track */

void e1_start(e1_engine_t *e, double tick);
/* Track time 0 at `origin`, continue from `now` without replaying. */
void e1_join(e1_engine_t *e, double origin, double now);
void e1_stop(e1_engine_t *e, e1_emit_fn emit, void *user);
void e1_advance(e1_engine_t *e, double tick_now, e1_emit_fn emit, void *user);

void e1_all_notes_off(e1_engine_t *e, e1_emit_fn emit, void *user);
void e1_track_silence(e1_engine_t *e, int track, e1_emit_fn emit, void *user);
/* Play one hit of a track by hand, also while stopped. on = 1 sounds its
 * notes (or sends a CC track's lane values), on = 0 lets the notes go. */
void e1_audition(e1_engine_t *e, int track, int on, e1_emit_fn emit, void *user);

/* Performance moves on every track whose bit is set in `mask`:
 *   "fill", "ratchet" (2..4, 0 off), "speed" (-1, 0, 1), "reverse",
 *   "freeze" (steps to loop, 0 off), "accent", "solo" (1: tracks outside
 *   the mask fall silent, 0 off), "restart", "rotate" (a saved edit),
 *   "stoprepeats" (cut repeat chains in flight), "randstart" (1: jump to a
 *   random step, 0: back in line with the bar), "clear" (every move off). */
void e1_perf(e1_engine_t *e, uint32_t mask, const char *move, int value);

/* Copy only the saved part of a track; position and sounding notes stay. */
void e1_copy_stored(e1_track_t *dst, const e1_track_t *src);
/* Call after replacing a playing track's saved part. */
void e1_resync(e1_engine_t *e, int track);

/* Hits of one cycle as a bit mask, bit i = step i. */
uint64_t e1_hit_mask(const e1_engine_t *e, int track, int cycle, int fill);
/* The hits Density leaves sounding. */
uint64_t e1_live_mask(const e1_engine_t *e, int track, int cycle, int fill);
/* Bit per E1_DICE_* layer: rolled at least once / playing now. */
int  e1_dice_rolled(const e1_track_t *t);
int  e1_dice_active(const e1_engine_t *e, int track);
int  e1_steps(const e1_engine_t *e, int track, int cycle);
/* The cycle the UI should show: the one being edited, else the one playing. */
int  e1_display_cycle(const e1_track_t *t);
/* Bit c set when cycle c differs from the base. */
uint32_t e1_cycles_edited(const e1_track_t *t);
uint64_t e1_steps_edited(const e1_track_t *t);   /* steps that carry their own values */

int  e1_ports(const e1_engine_t *e, const e1_track_t *t);

/* Parameters by text.
 *   track  0..15, or -1 for the global keys "dest" and "seed".
 *   val    a number, an option name, or a relative step "d+1" / "d-3".
 * Track keys:
 *   <param>          base value, or the picked cycles while edit_mask is set
 *   r.<param>        random amount (-100..100)
 *   c<N>.<param>     cycle N's offset from base (used by save files)
 *   cc<L>, ccn<L>    CC lane value (-1 off) and CC number; r.cc<L>, c<N>.cc<L>
 *   pool             comma list of MIDI notes; pooltoggle = one note
 *   toggle           flip a step by hand; clearedits; addmask/delmask (hex)
 *   c<N>.add/del     hand edits in one cycle (hex)
 *   edit             hex mask of cycles to edit (0 leaves cycle edit)
 *   clearcycle       N; copycycle "A B"; clearparam <param>; clearrandom
 *   retrig           hex mask of retrigger steps; retrigtoggle = one step
 *   rseed            the track's own random seed (a new one re-rolls it)
 *   roll             notes | accents | lengths | octaves | rhythm | all: a new lane
 *   rollback         the roll before this one; cleardice <layer | all>
 *   dice             the five lane seeds, comma list (used by save files)
 *   name
 * Returns 1 when the key was recognised. */
int  e1_set(e1_engine_t *e, int track, const char *key, const char *val);
int  e1_get(const e1_engine_t *e, int track, const char *key, char *buf, int len);

int  e1_param_count(void);
const char *e1_param_key(int i);
/* Value to show for parameter i: the edited cycle's value while editing. */
int  e1_get_num(const e1_engine_t *e, int track, int i);

/* Save text: "key=value" lines. Loading resets the track first. */
int  e1_dump_track(const e1_engine_t *e, int track, char *buf, int len);
int  e1_dump_global(const e1_engine_t *e, char *buf, int len);
void e1_load_track(e1_engine_t *e, int track, const char *text);
void e1_load_global(e1_engine_t *e, const char *text);

/* Deterministic random in [0, 99]. */
int  e1_chance(uint32_t seed, int track, int64_t k);

#ifdef __cplusplus
}
#endif
#endif
