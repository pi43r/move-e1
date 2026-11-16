/* Host unit tests for the E-1 engine (second generation API): `make test`
 *
 * Runs under ASan/UBSan. A few probes for undefined behaviour run in a child
 * process with UBSan set to halt, so a sanitizer report counts as a failure.
 */
#define _GNU_SOURCE
#include "../src/engine/e1_engine.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <limits.h>
#include <unistd.h>
#include <sys/wait.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s -- ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------ capture */

#define MAXEV 120000
typedef struct { double at; int ports; uint8_t st, d1, d2; } ev_t;
typedef struct {
    int n, lost, neg;
    int on[16][128];            /* note-on count per channel/note */
    int held[16], maxheld[16];  /* notes sounding per channel */
    ev_t ev[MAXEV];
} cap_t;

static double g_now;
static void emit(void *u, int ports, uint8_t st, uint8_t d1, uint8_t d2)
{
    cap_t *c = (cap_t *)u;
    int ty = st & 0xF0, ch = st & 0x0F;
    if (ty == 0x90 && d2) {
        c->on[ch][d1 & 127]++; c->held[ch]++;
        if (c->held[ch] > c->maxheld[ch]) c->maxheld[ch] = c->held[ch];
    } else if (ty == 0x80 || ty == 0x90) {
        if (c->on[ch][d1 & 127] <= 0) c->neg++;
        else { c->on[ch][d1 & 127]--; c->held[ch]--; }
    }
    if (c->n < MAXEV) { ev_t *e = &c->ev[c->n++]; e->at = g_now; e->ports = ports; e->st = st; e->d1 = d1; e->d2 = d2; }
    else c->lost++;
}
static cap_t *cap_new(void) { cap_t *c = calloc(1, sizeof *c); if (!c) abort(); return c; }
static void cap_clear(cap_t *c) { memset(c, 0, sizeof *c); }
static int sounding(const cap_t *c) { int n = 0; for (int ch = 0; ch < 16; ch++) for (int k = 0; k < 128; k++) n += c->on[ch][k]; return n; }
static int balanced(const cap_t *c) { return sounding(c) == 0 && c->neg == 0; }

typedef struct { double at; int ch, note, vel; } non_t;
/* note-ons on channel ch (0-based, -1 any) */
static int ons(const cap_t *c, int ch, non_t *out, int max)
{
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        const ev_t *e = &c->ev[i];
        if ((e->st & 0xF0) != 0x90 || !e->d2) continue;
        if (ch >= 0 && (e->st & 0x0F) != ch) continue;
        if (n < max) { out[n].at = e->at; out[n].ch = e->st & 0x0F; out[n].note = e->d1; out[n].vel = e->d2; }
        n++;
    }
    return n;
}
static int count_ons(const cap_t *c, int ch, double from, double to)
{
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        const ev_t *e = &c->ev[i];
        if ((e->st & 0xF0) == 0x90 && e->d2 && (ch < 0 || (e->st & 0x0F) == ch) && e->at >= from && e->at < to) n++;
    }
    return n;
}
static int count_cc(const cap_t *c, int ch, int num, int val)
{
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        const ev_t *e = &c->ev[i];
        if ((e->st & 0xF0) == 0xB0 && (e->st & 0x0F) == ch && (num < 0 || e->d1 == num) && (val < 0 || e->d2 == val)) n++;
    }
    return n;
}

/* Note-ons grouped by time. */
typedef struct { double at; int n; int note[24]; int vel[24]; } grp_t;
static int groups(const cap_t *c, int ch, grp_t *g, int max)
{
    int n = 0;
    for (int i = 0; i < c->n; i++) {
        const ev_t *e = &c->ev[i];
        if ((e->st & 0xF0) != 0x90 || !e->d2 || (e->st & 0x0F) != ch) continue;
        if (n == 0 || g[n - 1].at != e->at) { if (n >= max) break; g[n].at = e->at; g[n].n = 0; n++; }
        grp_t *q = &g[n - 1];
        if (q->n < 24) { q->note[q->n] = e->d1; q->vel[q->n] = e->d2; q->n++; }
    }
    for (int k = 0; k < n; k++)       /* sort notes in each group */
        for (int i = 1; i < g[k].n; i++)
            for (int j = i; j > 0 && g[k].note[j - 1] > g[k].note[j]; j--) {
                int x = g[k].note[j]; g[k].note[j] = g[k].note[j - 1]; g[k].note[j - 1] = x;
            }
    return n;
}
static const char *notes_str(const grp_t *g)
{
    static char b[8][160]; static int r = 0;
    char *s = b[r++ & 7]; int pos = 0; s[0] = 0;
    for (int i = 0; i < g->n; i++) pos += snprintf(s + pos, sizeof b[0] - (size_t)pos, "%s%d", i ? "," : "", g->note[i]);
    return s;
}

/* ------------------------------------------------------------- driving */

static void run(e1_engine_t *e, cap_t *c, double from, double to, double step)
{
    for (long i = 0;; i++) {
        double t = from + (double)i * step;
        if (t > to + 1e-9) break;
        g_now = t;
        e1_advance(e, t, emit, c);
    }
}
static e1_engine_t *eng_new(void) { e1_engine_t *e = calloc(1, sizeof *e); if (!e) abort(); return e; }

/* Starter set with every track muted. */
static void setup(e1_engine_t *e) { e1_init(e); for (int i = 0; i < E1_TRACKS; i++) e->t[i].v[E1_P_MUTE] = 1; }
/* An empty default track on channel ti+1, unmuted. */
static void fresh(e1_engine_t *e, int ti) { e1_track_defaults(&e->t[ti], ti); e->t[ti].v[E1_P_CHANNEL] = (int16_t)(ti + 1); }

#define S(e, t, k, v) CHECKM(e1_set((e), (t), (k), (v)) == 1, "set %s=%s", (k), (v))

static int popcount64(uint64_t m) { return __builtin_popcountll(m); }

/* first group's notes after playing track ti for `bars` bars */
static int play_groups(e1_engine_t *e, cap_t *c, int ti, int bars, grp_t *g, int max)
{
    cap_clear(c);
    e1_start(e, 0);
    run(e, c, 0, bars * E1_TICKS_PER_BAR - 1, 1);
    e1_stop(e, emit, c);
    return groups(c, e->t[ti].v[E1_P_CHANNEL] - 1, g, max);
}

/* ------------------------------------------------------------- euclid */

static void test_euclid(void)
{
    e1_engine_t *e = eng_new(); setup(e); fresh(e, 0);
    CHECK(e1_hit_mask(e, 0, 0, 0) == 0x1111);
    CHECK(e1_steps(e, 0, 0) == 16);
    S(e, 0, "steps", "8"); S(e, 0, "pulses", "3");
    CHECK(e1_hit_mask(e, 0, 0, 0) == 0x49);            /* x..x..x. */
    S(e, 0, "rotate", "1");
    CHECK(e1_hit_mask(e, 0, 0, 0) == 0x92);
    S(e, 0, "rotate", "0"); S(e, 0, "pulses", "0");
    CHECK(e1_hit_mask(e, 0, 0, 0) == 0);
    S(e, 0, "pulses", "8"); CHECK(e1_hit_mask(e, 0, 0, 0) == 0xFF);
    S(e, 0, "pulses", "12"); CHECK(e1_hit_mask(e, 0, 0, 0) == 0xFF);
    S(e, 0, "steps", "16"); S(e, 0, "pulses", "4"); S(e, 0, "fill", "4");
    CHECK(e1_hit_mask(e, 0, 0, 1) == 0x5555);
    CHECK(e1_hit_mask(e, 0, 0, 0) == 0x1111);
    S(e, 0, "steps", "64"); S(e, 0, "pulses", "64");
    CHECK(e1_hit_mask(e, 0, 0, 0) == ~0ULL);

    /* the formula, every steps/pulses, three rotations */
    int bad = 0;
    for (int s = 1; s <= 64; s++)
        for (int p = 0; p <= s; p++) {
            int rots[3] = { 0, s / 3, s - 1 };
            for (int r = 0; r < 3; r++) {
                e->t[0].v[E1_P_STEPS] = (int16_t)s; e->t[0].v[E1_P_PULSES] = (int16_t)p; e->t[0].v[E1_P_ROTATE] = (int16_t)rots[r];
                uint64_t m = e1_hit_mask(e, 0, 0, 0);
                if (popcount64(m) != p) bad++;
                for (int i = 0; i < s; i++) {
                    int j = ((i - rots[r]) % s + s) % s;
                    int want = p >= s ? 1 : (p > 0 && ((j * p) % s) < p);
                    if (((m >> i) & 1) != (uint64_t)want) bad++;
                }
            }
        }
    CHECKM(bad == 0, "%d euclid mismatches", bad);
    free(e);
}

static void test_toggle(void)
{
    e1_engine_t *e = eng_new(); setup(e); fresh(e, 0);
    S(e, 0, "steps", "8"); S(e, 0, "pulses", "3"); S(e, 0, "rotate", "2");
    uint64_t m = e1_hit_mask(e, 0, 0, 0);
    S(e, 0, "toggle", "1");
    CHECK(e1_hit_mask(e, 0, 0, 0) == (m ^ 2));
    S(e, 0, "toggle", "1");
    CHECK(e1_hit_mask(e, 0, 0, 0) == m && e->t[0].add_mask == 0 && e->t[0].del_mask == 0);
    int first = __builtin_ctzll(m);
    S(e, 0, "toggle", first == 0 ? "0" : (first == 1 ? "1" : "2"));
    CHECK(!((e1_hit_mask(e, 0, 0, 0) >> first) & 1));
    /* hand edits rotate with the pattern */
    S(e, 0, "rotate", "3");
    CHECK(!((e1_hit_mask(e, 0, 0, 0) >> ((first + 1) % 8)) & 1));
    /* changing pulses brings removed hits back */
    S(e, 0, "pulses", "4");
    CHECK(e->t[0].del_mask == 0);
    S(e, 0, "clearedits", "1");
    CHECK(e->t[0].add_mask == 0 && e->t[0].del_mask == 0);

    /* toggle in cycle edit touches only the picked cycle */
    fresh(e, 1);
    S(e, 1, "edit", "1");
    S(e, 1, "toggle", "1");
    CHECK(e1_hit_mask(e, 1, 0, 0) == 0x1113);
    CHECK(e1_hit_mask(e, 1, 1, 0) == 0x1111);
    CHECK(e->t[1].add_mask == 0);
    S(e, 1, "toggle", "4");
    CHECK(e1_hit_mask(e, 1, 0, 0) == 0x1103);
    S(e, 1, "edit", "0");
    CHECK(e1_cycles_edited(&e->t[1]) == 1);
    S(e, 1, "clearcycle", "1");
    CHECK(e1_hit_mask(e, 1, 0, 0) == 0x1111 && e1_cycles_edited(&e->t[1]) == 0);
    free(e);
}

/* ------------------------------------------------------------- timing */

static void test_basic_playback(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0);
    e1_start(e, 0);
    run(e, c, 0, 4 * 384 - 1, 1);
    non_t o[64];
    int n = ons(c, 0, o, 64);
    CHECK(n == 16);
    int ok = 1;
    for (int i = 0; i < n && i < 64; i++) if (o[i].at != 96.0 * i || o[i].note != 36 || o[i].vel != 100) ok = 0;
    CHECK(ok);
    /* sustain 25 = half a step: the first note-off at tick 12 */
    double off = -1;
    for (int i = 0; i < c->n; i++) if ((c->ev[i].st & 0xF0) == 0x80) { off = c->ev[i].at; break; }
    CHECKM(off == 12.0, "first off at %g", off);
    CHECK(c->ev[0].ports == E1_PORT_MOVE);
    e1_stop(e, emit, c);
    CHECK(balanced(c));
    /* nothing while stopped */
    int before = c->n;
    run(e, c, 1536, 2000, 1);
    CHECK(c->n == before);
    free(e); free(c);
}

/* Per-channel note-on sequences (note, velocity) must match. */
static int same_sequence(const cap_t *a, const cap_t *b, int ch)
{
    static non_t x[20000], y[20000];
    int na = ons(a, ch, x, 20000), nb = ons(b, ch, y, 20000);
    if (na != nb) { printf("  ch%d: %d vs %d note-ons\n", ch + 1, na, nb); return 0; }
    for (int i = 0; i < na && i < 20000; i++)
        if (x[i].note != y[i].note || x[i].vel != y[i].vel) { printf("  ch%d: differ at #%d (%d/%d vs %d/%d)\n", ch + 1, i, x[i].note, x[i].vel, y[i].note, y[i].vel); return 0; }
    return 1;
}
/* Note-on times must match from `from` on. */
static int same_times(const cap_t *a, const cap_t *b, int ch, double from, double to)
{
    static non_t x[20000], y[20000];
    int na = ons(a, ch, x, 20000), nb = ons(b, ch, y, 20000);
    int i = 0, j = 0;
    while (i < na && x[i].at < from) i++;
    while (j < nb && y[j].at < from) j++;
    for (;;) {
        int ea = i >= na || x[i].at >= to, eb = j >= nb || y[j].at >= to;
        if (ea && eb) return 1;
        if (ea != eb || x[i].at != y[j].at || x[i].note != y[j].note) {
            printf("  ch%d: times differ: %g vs %g\n", ch + 1, ea ? -1 : x[i].at, eb ? -1 : y[j].at);
            return 0;
        }
        i++; j++;
    }
}

static void config_mix(e1_engine_t *e)
{
    setup(e);
    for (int i = 0; i < 8; i++) fresh(e, i);
    S(e, 1, "steps", "7"); S(e, 1, "pulses", "3"); S(e, 1, "rotate", "2"); S(e, 1, "division", "1/8T");
    S(e, 2, "steps", "13"); S(e, 2, "pulses", "5"); S(e, 2, "division", "1/16T"); S(e, 2, "delay", "30"); S(e, 2, "timing", "20");
    S(e, 3, "c2.steps", "-5"); S(e, 3, "c3.division", "-1"); S(e, 3, "c4.pulses", "3"); S(e, 3, "velocity", "90");
    S(e, 4, "length", "3"); S(e, 4, "steps", "11"); S(e, 4, "division", "1/8."); S(e, 4, "pulses", "6");
    S(e, 5, "pool", "60,64,67"); S(e, 5, "style", "PolyClimb"); S(e, 5, "voicing", "2"); S(e, 5, "pulses", "7");
    S(e, 6, "pool", "48,55,63"); S(e, 6, "style", "MonoRamp"); S(e, 6, "voicing", "3"); S(e, 6, "pulses", "9"); S(e, 6, "division", "1/32");
    S(e, 7, "steps", "5"); S(e, 7, "pulses", "2"); S(e, 7, "division", "1/64"); S(e, 7, "accent", "60"); S(e, 7, "groove", "Congas");
}

static void test_jump_equivalence(void)
{
    e1_engine_t *a = eng_new(), *b = eng_new(); cap_t *ca = cap_new(), *cb = cap_new();
    config_mix(a); config_mix(b);
    e1_start(a, 0); e1_start(b, 0);
    run(a, ca, 0, 16 * 384, 0.37); g_now = 16 * 384; e1_advance(a, g_now, emit, ca);
    run(b, cb, 0, 16 * 384, 96);               /* a beat per call */
    int ok = 1;
    for (int ch = 0; ch < 8; ch++) ok &= same_sequence(ca, cb, ch);
    CHECK(ok);
    cap_clear(ca); cap_clear(cb);
    config_mix(a); config_mix(b);
    e1_start(a, 0); e1_start(b, 0);
    run(a, ca, 0, 16 * 384, 1);
    run(b, cb, 0, 16 * 384, 384);              /* a bar per call */
    ok = 1;
    for (int ch = 0; ch < 8; ch++) ok &= same_sequence(ca, cb, ch);
    CHECK(ok);

    /* with repeats */
    cap_clear(ca); cap_clear(cb);
    setup(a); fresh(a, 0); S(a, 0, "repeats", "2"); S(a, 0, "time", "1/32");
    setup(b); fresh(b, 0); S(b, 0, "repeats", "2"); S(b, 0, "time", "1/32");
    e1_start(a, 0); e1_start(b, 0);
    run(a, ca, 0, 4 * 384, 1);
    run(b, cb, 0, 4 * 384, 384);
    /* BUG: e1_advance fires every pulse of the jump (advance_track) before any
     * repeat (run_chains), so each pulse chokes the previous pulse's repeats
     * before they sound: a one-bar call loses 3 of 4 echo groups. */
    CHECKM(count_ons(ca, 0, 0, 1e9) == count_ons(cb, 0, 0, 1e9), "small steps %d vs bar jumps %d",
           count_ons(ca, 0, 0, 1e9), count_ons(cb, 0, 0, 1e9));
    free(a); free(b); free(ca); free(cb);
}

static void test_change_and_back(void)
{
    e1_engine_t *a = eng_new(), *b = eng_new(); cap_t *ca = cap_new(), *cb = cap_new();
    config_mix(a); config_mix(b);
    e1_start(a, 0); e1_start(b, 0);
    for (int k = 0; k < 8 * 384; k++) {
        if (k == 500) { S(b, 0, "steps", "7"); S(b, 0, "division", "1/8T"); S(b, 3, "c2.steps", "9"); S(b, 4, "steps", "3"); }
        if (k == 777) { S(b, 0, "division", "1/64"); S(b, 6, "division", "1/4."); S(b, 1, "length", "1"); }
        if (k == 1300) {
            S(b, 0, "steps", "16"); S(b, 0, "division", "1/16"); S(b, 3, "c2.steps", "-5");
            S(b, 4, "steps", "11"); S(b, 6, "division", "1/32"); S(b, 1, "length", "Off");
        }
        g_now = k;
        e1_advance(a, k, emit, ca);
        e1_advance(b, k, emit, cb);
    }
    int ok = 1;
    for (int ch = 0; ch < 8; ch++) ok &= same_times(ca, cb, ch, 1536, 8 * 384);
    CHECK(ok);
    free(a); free(b); free(ca); free(cb);
}

static void test_join(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0);
    e1_join(e, 0, 1000);
    run(e, c, 1000, 1500, 1);
    non_t o[8];
    int n = ons(c, 0, o, 8);
    CHECK(n == 5 && o[0].at == 1056);           /* nothing replayed from before 1000 */
    cap_clear(c);
    e1_join(e, 0, 960);
    run(e, c, 960, 1000, 1);
    n = ons(c, 0, o, 8);
    CHECK(n == 1 && o[0].at == 960);
    /* origin shifts the grid */
    cap_clear(c);
    e1_join(e, 50, 50);
    run(e, c, 50, 50 + 383, 1);
    n = ons(c, 0, o, 8);
    CHECK(n == 4 && o[0].at == 50 && o[1].at == 146);
    free(e); free(c);
}

static int times_are(const cap_t *c, int ch, const double *want, int nw, double to)
{
    non_t o[512];
    int n = ons(c, ch, o, 512), k = 0;
    for (int i = 0; i < n && i < 512; i++) {
        if (o[i].at >= to) break;
        if (k >= nw || o[i].at != want[k]) { printf("  note-on #%d at %g, want %g\n", k, o[i].at, k < nw ? want[k] : -1.0); return 0; }
        k++;
    }
    if (k != nw) { printf("  %d note-ons, want %d\n", k, nw); return 0; }
    return 1;
}

static void test_length(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0);
    S(e, 0, "steps", "7"); S(e, 0, "pulses", "1"); S(e, 0, "length", "1");
    e1_start(e, 0);
    run(e, c, 0, 800, 1);
    double w[] = { 0, 168, 336, 384, 552, 720, 768 };
    CHECK(times_are(c, 0, w, 7, 801));
    /* 3 bars, 1/4T steps: restarts at 1152 */
    cap_clear(c); setup(e); fresh(e, 0);
    S(e, 0, "steps", "5"); S(e, 0, "pulses", "1"); S(e, 0, "division", "1/4T"); S(e, 0, "length", "3");
    e1_start(e, 0);
    run(e, c, 0, 1200, 1);
    double w2[] = { 0, 320, 640, 960, 1152 };
    CHECK(times_are(c, 0, w2, 5, 1201));
    free(e); free(c);
}

static void test_delay_timing(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "delay", "50");
    e1_start(e, 0); run(e, c, 0, 383, 1);
    double w[] = { 12, 108, 204, 300 };
    CHECK(times_are(c, 0, w, 4, 384));
    cap_clear(c); setup(e); fresh(e, 0); S(e, 0, "delay", "-25");
    e1_start(e, 0); run(e, c, 0, 400, 1);
    double w2[] = { 0, 90, 186, 282, 378 };
    CHECK(times_are(c, 0, w2, 5, 401));

    /* Timing: every second step (x1), pair (x2), four (x4) */
    static const char *grid[3] = { "x1", "x2", "x4" };
    int tv[2] = { 50, -50 };
    for (int gi = 0; gi < 3; gi++)
        for (int k = 0; k < 2; k++) {
            int g = 1 << gi;
            cap_clear(c); setup(e); fresh(e, 0);
            S(e, 0, "pulses", "16"); S(e, 0, "swinggrid", grid[gi]);
            char b[8]; snprintf(b, sizeof b, "%d", tv[k]); S(e, 0, "timing", b);
            e1_start(e, 0); run(e, c, 0, 2 * 384 - 1, 1);
            double want[32]; int nw = 0;
            for (int bar = 0; bar < 2; bar++)
                for (int i = 0; i < 16; i++) {
                    double t = bar * 384 + 24.0 * i + (((i / g) & 1) ? 24.0 * g * tv[k] / 100.0 : 0);
                    if (t < 0) t = 0;
                    if (t < 2 * 384 - 24) want[nw++] = t;
                }
            for (int i = 1; i < nw; i++) for (int j = i; j > 0 && want[j - 1] > want[j]; j--) { double x = want[j]; want[j] = want[j - 1]; want[j - 1] = x; }
            /* BUG (x4 only): a step held back by Timing blocks the next unshifted
             * step, which then fires late (step 8 at 216 instead of 192 with +50;
             * step 4 at 72 instead of 48 with -50). Cause: advance_track only
             * looks at the slot at `cursor` and stops at the first one not due. */
            CHECKM(times_are(c, 0, want, nw, 2 * 384 - 24), "swing grid %s timing %d", grid[gi], tv[k]);
        }
    free(e); free(c);
}

/* -------------------------------------------------------- perform moves */

static void test_perf_speed(void)
{
    e1_engine_t *a = eng_new(), *b = eng_new(); cap_t *ca = cap_new(), *cb = cap_new();
    for (int sp = -1; sp <= 1; sp += 2) {
        cap_clear(ca); cap_clear(cb);
        setup(a); fresh(a, 0); setup(b); fresh(b, 0);
        S(a, 0, "repeats", "1"); S(b, 0, "repeats", "1");
        e1_start(a, 0); e1_start(b, 0);
        for (int k = 0; k < 6 * 384; k++) {
            if (k == 384) e1_perf(b, 1, "speed", sp);
            if (k == 768 + 384) e1_perf(b, 1, "speed", 0);
            g_now = k;
            e1_advance(a, k, emit, ca); e1_advance(b, k, emit, cb);
        }
        /* repeats double too, so count pulses: one note per 96 at 1x */
        int want = (sp > 0 ? 16 : 4) * 2;  /* two bars held, a repeat per pulse */
        CHECKM(count_ons(cb, 0, 384, 1152) == want, "speed %d: %d note-ons in the held bars", sp, count_ons(cb, 0, 384, 1152));
        CHECK(same_times(ca, cb, 0, 1152, 6 * 384));
        e1_stop(b, emit, cb);
        CHECK(balanced(cb));
    }
    free(a); free(b); free(ca); free(cb);
}

static void test_perf_freeze(void)
{
    e1_engine_t *a = eng_new(), *b = eng_new(); cap_t *ca = cap_new(), *cb = cap_new();
    setup(a); fresh(a, 0); setup(b); fresh(b, 0);
    e1_start(a, 0); e1_start(b, 0);
    for (int k = 0; k < 4 * 384; k++) {
        if (k == 400) e1_perf(b, 1, "freeze", 2);
        if (k == 768) e1_perf(b, 1, "freeze", 0);
        g_now = k;
        e1_advance(a, k, emit, ca); e1_advance(b, k, emit, cb);
    }
    /* frozen on steps 16 and 1 (the hit): one note every 48 ticks */
    double w[] = { 432, 480, 528, 576, 624, 672, 720 };
    non_t o[64]; int n = ons(cb, 0, o, 64), k = 0, ok = 1;
    for (int i = 0; i < n; i++) if (o[i].at >= 400 && o[i].at < 768) { if (k >= 7 || o[i].at != w[k]) ok = 0; k++; }
    CHECKM(ok && k == 7, "%d frozen notes", k);
    CHECK(same_times(ca, cb, 0, 768, 4 * 384));
    free(a); free(b); free(ca); free(cb);
}

static void test_perf_misc(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    /* reverse */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "1");
    e1_perf(e, 1, "reverse", 1);
    e1_start(e, 0); run(e, c, 0, 767, 1);
    double w[] = { 360, 744 };
    CHECK(times_are(c, 0, w, 2, 768));
    cap_clear(c); S(e, 0, "rotate", "3");
    e1_start(e, 0); run(e, c, 0, 383, 1);
    double w1[] = { 288 };
    CHECK(times_are(c, 0, w1, 1, 384));
    e1_perf(e, 1, "reverse", 0);

    /* restart: the next step is step 1, on the track's grid */
    cap_clear(c); setup(e); fresh(e, 0);
    e1_start(e, 0); run(e, c, 0, 399, 1);
    e1_perf(e, 1, "restart", 1);
    run(e, c, 400, 1000, 1);
    non_t o[32]; int n = ons(c, 0, o, 32);
    int first = -1; for (int i = 0; i < n; i++) if (o[i].at >= 400) { first = i; break; }
    /* BUG: restart sets toff = now, so step 1 sounds at once (tick 400, between
     * two 1/16 steps) and the track stays off the grid (496, 592, ...) until
     * the transport restarts. e1_perf "restart" in e1_engine.c. */
    CHECKM(first >= 0 && o[first].at == 408, "first note after restart at %g", first >= 0 ? o[first].at : -1);
    int ongrid = 1; for (int i = 0; i < n; i++) if (fmod(o[i].at, 24.0) != 0) ongrid = 0;
    CHECK(ongrid);

    /* fill */
    cap_clear(c); setup(e); fresh(e, 0);
    e1_perf(e, 1, "fill", 1);
    e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_ons(c, 0, 0, 384) == 6);
    e1_perf(e, 1, "fill", 0);

    /* ratchet: three evenly spaced hits per pulse */
    cap_clear(c); setup(e); fresh(e, 0);
    e1_perf(e, 1, "ratchet", 3);
    e1_start(e, 0); run(e, c, 0, 383, 1);
    double w3[] = { 0, 8, 16, 96, 104, 112, 192, 200, 208, 288, 296, 304 };
    CHECK(times_are(c, 0, w3, 12, 384));
    e1_stop(e, emit, c);
    CHECK(balanced(c));

    /* accent, solo, clear */
    cap_clear(c); setup(e); fresh(e, 0); fresh(e, 1);
    e1_perf(e, 1, "accent", 1);
    e1_perf(e, 2, "solo", 1);
    e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_ons(c, 0, 0, 384) == 0 && count_ons(c, 1, 0, 384) == 4);
    e1_perf(e, 2, "solo", 0);
    run(e, c, 384, 767, 1);
    n = ons(c, 0, o, 32);
    CHECK(n == 4 && o[0].vel == 127);
    e1_perf(e, 3, "fill", 1); e1_perf(e, 3, "ratchet", 2); e1_perf(e, 3, "reverse", 1);
    e1_perf(e, 3, "speed", 1); e1_perf(e, 3, "freeze", 4); e1_perf(e, 1, "solo", 1);
    e1_perf(e, 0xFFFF, "clear", 0);
    int clean = 1;
    for (int i = 0; i < 2; i++) {
        e1_track_t *t = &e->t[i];
        if (t->p_fill || t->p_ratchet || t->p_reverse || t->p_accent || t->p_speed || t->p_freeze || t->p_solo_mute) clean = 0;
    }
    CHECK(clean);
    /* rotate is a saved edit */
    e1_perf(e, 1, "rotate", 3); CHECK(e->t[0].v[E1_P_ROTATE] == 3);
    e1_perf(e, 1, "rotate", -5); CHECK(e->t[0].v[E1_P_ROTATE] == 14);
    e1_perf(e, 1, NULL, 1);
    e1_perf(e, 1, "bogus", 1);
    free(e); free(c);
}

/* -------------------------------------------------------------- cycles */

static void test_cycles(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    char b[64];
    setup(e); fresh(e, 0);
    CHECK(e->t[0].v[E1_P_CYCLES] == 4);
    S(e, 0, "edit", "2");
    S(e, 0, "velocity", "50");
    CHECK(e->t[0].v[E1_P_VELOCITY] == 100 && e->t[0].cyc[1][E1_P_VELOCITY] == -50);
    CHECK(e1_get_num(e, 0, E1_P_VELOCITY) == 50);
    e1_get(e, 0, "velocity", b, sizeof b); CHECK(strcmp(b, "50") == 0);
    CHECK(e1_display_cycle(&e->t[0]) == 1);
    /* while editing, only cycle 2 loops */
    e1_start(e, 0); run(e, c, 0, 2 * 384 - 1, 1);
    non_t o[64]; int n = ons(c, 0, o, 64), ok = n == 8;
    for (int i = 0; i < n && i < 64; i++) if (o[i].vel != 50) ok = 0;
    CHECK(ok);
    CHECK(e->t[0].cur_cycle == 1);
    S(e, 0, "velocity", "d+5");
    CHECK(e->t[0].cyc[1][E1_P_VELOCITY] == -45);
    S(e, 0, "edit", "0");
    CHECK(e1_cycles_edited(&e->t[0]) == 2);
    /* a round of four cycles */
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    n = ons(c, 0, o, 64);
    CHECK(n == 16 && o[0].vel == 100 && o[4].vel == 55 && o[8].vel == 100 && o[12].vel == 100);
    /* moving the base moves every cycle and keeps the difference */
    S(e, 0, "velocity", "110");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    n = ons(c, 0, o, 64);
    CHECK(n == 16 && o[0].vel == 110 && o[4].vel == 65);
    /* clamped */
    S(e, 0, "velocity", "30");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 2 * 384 - 1, 1);
    n = ons(c, 0, o, 64);
    CHECK(n == 8 && o[0].vel == 30 && o[4].vel == 1);
    S(e, 0, "velocity", "100");

    /* edit two cycles at once, relative */
    S(e, 0, "edit", "c");
    S(e, 0, "pitch", "d+2");
    CHECK(e->t[0].cyc[2][E1_P_PITCH] == 2 && e->t[0].cyc[3][E1_P_PITCH] == 2 && e->t[0].v[E1_P_PITCH] == 0);
    S(e, 0, "edit", "0");
    CHECK(e1_cycles_edited(&e->t[0]) == 0xE);
    /* non-lockable keys in edit mode set the base */
    S(e, 0, "edit", "1"); S(e, 0, "channel", "5"); S(e, 0, "edit", "0");
    CHECK(e->t[0].v[E1_P_CHANNEL] == 5);
    S(e, 0, "channel", "1");

    /* copycycle, clearparam, clearcycle */
    S(e, 0, "copycycle", "2 1");
    CHECK(e->t[0].cyc[0][E1_P_VELOCITY] == -45);
    CHECK(e1_set(e, 0, "copycycle", "2") == 0 && e1_set(e, 0, "copycycle", "0 1") == 0 && e1_set(e, 0, "copycycle", "1 17") == 0);
    S(e, 0, "clearparam", "velocity");
    CHECK(e->t[0].cyc[0][E1_P_VELOCITY] == 0 && e->t[0].cyc[1][E1_P_VELOCITY] == 0 && e->t[0].v[E1_P_VELOCITY] == 100);
    CHECK(e1_cycles_edited(&e->t[0]) == 0xC);
    S(e, 0, "clearcycle", "3");
    CHECK(e1_cycles_edited(&e->t[0]) == 0x8);
    CHECK(e1_set(e, 0, "clearcycle", "0") == 0 && e1_set(e, 0, "clearcycle", "17") == 0);
    S(e, 0, "clearcycle", "4");

    /* per-cycle steps change that cycle's length */
    S(e, 0, "edit", "2"); S(e, 0, "steps", "8"); S(e, 0, "edit", "0");
    CHECK(e1_steps(e, 0, 1) == 8 && e1_steps(e, 0, 0) == 16);
    CHECK(e1_hit_mask(e, 0, 1, 0) == 0x55);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 2 * 1344 - 1, 1);
    double w[] = { 0, 96, 192, 288, 384, 432, 480, 528, 576, 672, 768, 864, 960, 1056, 1152, 1248,
                   1344, 1440, 1536, 1632, 1728, 1776, 1824, 1872, 1920, 2016, 2112, 2208, 2304, 2400, 2496, 2592 };
    CHECK(times_are(c, 0, w, 32, 2 * 1344));
    /* per-cycle division */
    S(e, 0, "clearcycle", "2");
    S(e, 0, "c3.division", "-1");               /* 1/8 in cycle 3 */
    CHECK(e1_cycles_edited(&e->t[0]) == 4);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 5 * 384 - 1, 1);
    CHECK(count_ons(c, 0, 0, 5 * 384) == 4 + 4 + 4 + 4);   /* cycle 3 is two bars */
    double w2[] = { 768, 960, 1152, 1344 };
    non_t o2[64]; n = ons(c, 0, o2, 64);
    CHECK(n >= 12 && o2[8].at == w2[0] && o2[9].at == w2[1] && o2[10].at == w2[2] && o2[11].at == w2[3]);
    S(e, 0, "clearcycle", "3");

    /* cycles count */
    S(e, 0, "cycles", "2");
    S(e, 0, "c2.velocity", "-60");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    n = ons(c, 0, o, 64);
    CHECK(n == 16 && o[4].vel == 40 && o[8].vel == 100 && o[12].vel == 40);
    CHECK(e1_display_cycle(&e->t[0]) == e->t[0].cur_cycle);

    /* cycle CC lanes count as edits */
    fresh(e, 1); S(e, 1, "cc3", "40"); S(e, 1, "edit", "10"); S(e, 1, "cc3", "50"); S(e, 1, "edit", "0");
    CHECK(e->t[1].cc_cyc[4][2] == 10 && e1_cycles_edited(&e->t[1]) == 0x10);
    free(e); free(c);
}

/* -------------------------------------------------------------- pitch */

static const char *chord_of(e1_engine_t *e, cap_t *c, int nth)
{
    static grp_t g[64];
    int n = play_groups(e, c, 0, 1, g, 64);
    if (nth >= n) return "none";
    return notes_str(&g[nth]);
}

static void test_pool(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    char b[128];
    setup(e); fresh(e, 0);
    S(e, 0, "pool", "67,60,64");
    e1_get(e, 0, "pool", b, sizeof b); CHECK(strcmp(b, "60,64,67") == 0);
    S(e, 0, "pooltoggle", "62");
    e1_get(e, 0, "pool", b, sizeof b); CHECK(strcmp(b, "60,62,64,67") == 0);
    S(e, 0, "pooltoggle", "62");
    e1_get(e, 0, "pool", b, sizeof b); CHECK(strcmp(b, "60,64,67") == 0);
    S(e, 0, "pool", "1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16");
    CHECK(e->t[0].pool_n == E1_POOL);
    S(e, 0, "pooltoggle", "100");
    CHECK(e->t[0].pool_n == E1_POOL);
    S(e, 0, "pool", "300,-5");
    e1_get(e, 0, "pool", b, sizeof b); CHECK(strcmp(b, "0,127") == 0);
    /* empty pool plays nothing */
    S(e, 0, "pool", "");
    CHECK(e->t[0].pool_n == 0);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 767, 1);
    CHECK(count_ons(c, -1, 0, 1e9) == 0);
    free(e); free(c);
}

static void test_pitch(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    grp_t g[64]; int n;
#define CFG(...) do { setup(e); fresh(e, 0); S(e, 0, "pool", "60,64,67"); const char *kv[] = { __VA_ARGS__ }; \
        for (size_t _i = 0; _i + 1 < sizeof kv / sizeof kv[0]; _i += 2) S(e, 0, kv[_i], kv[_i + 1]); } while (0)

    CFG("style", "PolyFix");                          CHECK(strcmp(chord_of(e, c, 0), "60,64,67") == 0);
    CFG("pitch", "2");                                CHECK(strcmp(chord_of(e, c, 0), "62,66,69") == 0);
    CFG("scale", "Major", "pitch", "1");              CHECK(strcmp(chord_of(e, c, 0), "62,65,69") == 0);
    CFG("scale", "Major", "pitch", "-1");             CHECK(strcmp(chord_of(e, c, 0), "59,62,65") == 0);
    CFG("scale", "Major", "pitch", "7");              CHECK(strcmp(chord_of(e, c, 0), "72,76,79") == 0);
    setup(e); fresh(e, 0); S(e, 0, "pool", "62,66,69"); S(e, 0, "scale", "Major"); S(e, 0, "root", "D"); S(e, 0, "pitch", "1");
    CHECK(strcmp(chord_of(e, c, 0), "64,67,71") == 0);
    CFG("scale", "Major", "harmony", "1");            CHECK(strcmp(chord_of(e, c, 0), "62,64,67") == 0);
    CFG("scale", "Major", "harmony", "2");            CHECK(strcmp(chord_of(e, c, 0), "62,65,67") == 0);
    CFG("scale", "Major", "harmony", "3");            CHECK(strcmp(chord_of(e, c, 0), "62,65,69") == 0);
    CFG("voicing", "1");                              CHECK(strcmp(chord_of(e, c, 0), "64,67,72") == 0);
    CFG("voicing", "2");                              CHECK(strcmp(chord_of(e, c, 0), "67,72,76") == 0);
    CFG("voicing", "-1");                             CHECK(strcmp(chord_of(e, c, 0), "55,60,64") == 0);
    CFG("style", "PolyRamp", "voicing", "1");         CHECK(strcmp(chord_of(e, c, 0), "60,64,67,72") == 0);
    CFG("style", "PolyRamp", "voicing", "-1");        CHECK(strcmp(chord_of(e, c, 0), "55,60,64,67") == 0);
    CFG("style", "PolyRamp", "voicing", "4");         CHECK(strcmp(chord_of(e, c, 0), "60,64,67,72,76,79,84") == 0);
    CFG("style", "PolyClimb", "voicing", "2");
    n = play_groups(e, c, 0, 1, g, 64);
    CHECK(n == 4 && !strcmp(notes_str(&g[0]), "60,64,67") && !strcmp(notes_str(&g[1]), "64,67,72") &&
          !strcmp(notes_str(&g[2]), "67,72,76") && !strcmp(notes_str(&g[3]), "60,64,67"));
    CFG("style", "MonoFix");
    n = play_groups(e, c, 0, 1, g, 64);
    CHECK(n == 4 && g[0].n == 1 && g[0].note[0] == 60 && g[1].note[0] == 64 && g[2].note[0] == 67 && g[3].note[0] == 60);
    CFG("style", "MonoFix", "dir", "Down");
    n = play_groups(e, c, 0, 1, g, 64);
    CHECK(n == 4 && g[0].note[0] == 67 && g[1].note[0] == 64 && g[2].note[0] == 60 && g[3].note[0] == 67);
    CFG("style", "MonoRamp", "voicing", "3", "pulses", "8");
    n = play_groups(e, c, 0, 1, g, 64);
    CHECK(n == 8 && g[3].note[0] == 72 && g[5].note[0] == 79 && g[6].note[0] == 60);

    /* range + phrase over 4 bars (Saw: 0, 2, 5, 7 tenths at the bar starts) */
    static const struct { const char *rate; int b1, b2, b3; } ph[3] = { { "x1", 62, 65, 67 }, { "x2", 65, 60, 65 }, { "/2", 61, 62, 63 } };
    for (int r = 0; r < 3; r++) {
        setup(e); fresh(e, 0); S(e, 0, "pool", "60"); S(e, 0, "range", "10"); S(e, 0, "phrase", "Saw"); S(e, 0, "phraserate", ph[r].rate);
        n = play_groups(e, c, 0, 4, g, 64);
        CHECKM(n == 16 && g[0].note[0] == 60 && g[4].note[0] == ph[r].b1 && g[8].note[0] == ph[r].b2 && g[12].note[0] == ph[r].b3,
               "phraserate %s: %d %d %d %d", ph[r].rate, g[0].note[0], g[4].note[0], g[8].note[0], g[12].note[0]);
    }
    setup(e); fresh(e, 0); S(e, 0, "pool", "60"); S(e, 0, "range", "0"); S(e, 0, "phrase", "Sine");
    n = play_groups(e, c, 0, 4, g, 64);
    int flat = n == 16; for (int i = 0; i < n; i++) if (g[i].note[0] != 60) flat = 0;
    CHECK(flat);

    /* extremes stay in 0..127 and keep the chord size */
    setup(e); fresh(e, 0); S(e, 0, "pool", "120,124,127"); S(e, 0, "pitch", "36"); S(e, 0, "range", "16");
    S(e, 0, "phrase", "Pulse"); S(e, 0, "harmony", "16"); S(e, 0, "voicing", "16"); S(e, 0, "style", "PolyRamp"); S(e, 0, "scale", "Tetra");
    n = play_groups(e, c, 0, 4, g, 64);
    int inr = n > 0; for (int i = 0; i < n; i++) for (int k = 0; k < g[i].n; k++) if (g[i].note[k] > 127) inr = 0;
    CHECK(inr && c->neg == 0 && balanced(c));
    setup(e); fresh(e, 0); S(e, 0, "pool", "0,1,3"); S(e, 0, "pitch", "-36"); S(e, 0, "range", "-16");
    S(e, 0, "phrase", "Cad4"); S(e, 0, "harmony", "-16"); S(e, 0, "voicing", "-16"); S(e, 0, "scale", "Iwato"); S(e, 0, "root", "B");
    n = play_groups(e, c, 0, 4, g, 64);
    CHECK(n > 0 && balanced(c));
#undef CFG
    free(e); free(c);
}

/* Choke: a new pulse cuts the notes before it. */
static void test_choke_tail(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "pool", "60,64"); S(e, 0, "pulses", "16"); S(e, 0, "sustain", "100");
    S(e, 0, "style", "PolyClimb"); S(e, 0, "voicing", "1");
    e1_start(e, 0); run(e, c, 0, 767, 1); e1_stop(e, emit, c);
    CHECK(c->maxheld[0] == 2 && balanced(c));
    cap_clear(c); S(e, 0, "tail", "Tail");
    e1_start(e, 0); run(e, c, 0, 767, 1); e1_stop(e, emit, c);
    CHECK(c->maxheld[0] >= 3 && balanced(c));
    free(e); free(c);
}

/* ------------------------------------------------------------- repeats */

static void test_repeats(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "repeats", "3"); S(e, 0, "time", "1/32");
    e1_start(e, 0); run(e, c, 0, 191, 1);
    double w[] = { 0, 12, 24, 36, 96, 108, 120, 132 };
    CHECK(times_are(c, 0, w, 8, 192));
    e1_stop(e, emit, c); CHECK(balanced(c));

    cap_clear(c); S(e, 0, "repeats", "49");
    e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_ons(c, 0, 0, 96) == 8 && count_ons(c, 0, 0, 384) == 32);

    cap_clear(c); S(e, 0, "repeats", "3"); S(e, 0, "pace", "100");
    e1_start(e, 0); run(e, c, 0, 95, 1);
    double w2[] = { 0, 12, 30, 57 };
    CHECK(times_are(c, 0, w2, 4, 96));
    cap_clear(c); S(e, 0, "pace", "-100");
    e1_start(e, 0); run(e, c, 0, 95, 1);
    double w3[] = { 0, 12, 18, 21 };
    CHECK(times_are(c, 0, w3, 4, 96));

    S(e, 0, "pace", "0"); S(e, 0, "ramp", "50");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 95, 1);
    non_t o[16]; int n = ons(c, 0, o, 16);
    CHECK(n == 4 && o[0].vel == 100 && o[1].vel == 116 && o[2].vel == 127 && o[3].vel == 127);
    S(e, 0, "ramp", "-100");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 95, 1);
    n = ons(c, 0, o, 16);
    CHECK(n == 4 && o[1].vel == 68 && o[2].vel == 36 && o[3].vel == 4);

    /* Choke stops an earlier pulse's repeats, Tail lets them run on */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16"); S(e, 0, "repeats", "3"); S(e, 0, "time", "1/8");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_ons(c, 0, 0, 384) == 16);
    S(e, 0, "tail", "Tail");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_ons(c, 0, 0, 384) > 16);
    e1_stop(e, emit, c); CHECK(balanced(c));

    /* 49 = until the next pulse, with Tail as well */
    setup(e); fresh(e, 0); S(e, 0, "repeats", "49"); S(e, 0, "time", "1/32"); S(e, 0, "tail", "Tail");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    /* BUG (doubtful): with tail=Tail an endless chain (left = 100000) is never
     * stopped by the next pulse, so up to E1_CHAINS endless echo streams pile
     * up (each runs to k > 512, ~16 bars at 1/32). fire()/run_chains. */
    CHECKM(count_ons(c, 0, 0, 4 * 384) == 128, "%d note-ons in 4 bars, want 128", count_ons(c, 0, 0, 4 * 384));
    e1_stop(e, emit, c); CHECK(balanced(c));

    /* mono styles walk on through the repeats */
    setup(e); fresh(e, 0); S(e, 0, "pool", "60,64,67"); S(e, 0, "style", "MonoFix"); S(e, 0, "pulses", "1"); S(e, 0, "repeats", "3");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 95, 1);
    n = ons(c, 0, o, 16);
    CHECK(n == 4 && o[0].note == 60 && o[1].note == 64 && o[2].note == 67 && o[3].note == 60);
    free(e); free(c);
}

/* ------------------------------------------------- velocity, prob, random */

static void test_step_edit(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "velocity", "60");          /* 4 pulses on steps 0 4 8 12 */
    S(e, 0, "stepedit", "10");                                 /* step 5 (index 4) */
    S(e, 0, "velocity", "100"); S(e, 0, "repeats", "2");
    char b[32]; e1_get(e, 0, "velocity", b, sizeof b);
    CHECK(strcmp(b, "100") == 0);                              /* the picked step's value shows */
    S(e, 0, "stepedit", "0");
    e1_get(e, 0, "velocity", b, sizeof b);
    CHECK(strcmp(b, "60") == 0 && e->t[0].v[E1_P_REPEATS] == 0);
    CHECK(e1_steps_edited(&e->t[0]) == 0x10);
    e1_start(e, 0); run(e, c, 0, 383, 1);
    non_t o[16]; int n = ons(c, 0, o, 16);
    CHECK(n == 6 && o[0].vel == 60 && o[1].vel == 100);        /* only that step: louder, two repeats */
    /* survives a dump and load */
    char d[8192]; e1_dump_track(e, 0, d, sizeof d);
    CHECK(strstr(d, "s5.velocity=40") != NULL);
    fresh(e, 0); e1_load_track(e, 0, d);
    CHECK(e1_steps_edited(&e->t[0]) == 0x10);
    S(e, 0, "clearstep", "5");
    CHECK(e1_steps_edited(&e->t[0]) == 0);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(ons(c, 0, o, 16) == 4);
    free(e); free(c);
}

static void test_velocity(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "velocity", "60"); S(e, 0, "accent", "100"); S(e, 0, "groove", "Agogo");
    e1_start(e, 0); run(e, c, 0, 383, 1);
    non_t o[16]; int n = ons(c, 0, o, 16);
    CHECK(n == 4 && o[0].vel == 120 && o[1].vel == 96 && o[2].vel == 120 && o[3].vel == 96);
    S(e, 0, "accent", "-100");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
    n = ons(c, 0, o, 16);
    CHECK(n == 4 && o[0].vel == 1 && o[1].vel == 24);
    /* every groove, every accent: within 1..127 */
    int ok = 1;
    for (int gr = 0; gr < 8; gr++)
        for (int a = -100; a <= 100; a += 50) {
            setup(e); fresh(e, 0); S(e, 0, "pulses", "16");
            e->t[0].v[E1_P_GROOVE] = (int16_t)gr; e->t[0].v[E1_P_ACCENT] = (int16_t)a; e->t[0].v[E1_P_VELOCITY] = (int16_t)(a > 0 ? 127 : 1);
            cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
            n = ons(c, 0, o, 16);
            if (n != 16) ok = 0;
            for (int i = 0; i < n && i < 16; i++) if (o[i].vel < 1 || o[i].vel > 127) ok = 0;
        }
    CHECK(ok);
    free(e); free(c);
}

static int run_count(e1_engine_t *e, cap_t *c, int ch, int bars)
{
    cap_clear(c); e1_start(e, 0); run(e, c, 0, bars * 384 - 1, 1); e1_stop(e, emit, c);
    return count_ons(c, ch, 0, 1e12);
}
static uint32_t signature(const cap_t *c)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < c->n; i++) { h = (h ^ c->ev[i].st) * 16777619u; h = (h ^ c->ev[i].d1) * 16777619u; h = (h ^ c->ev[i].d2) * 16777619u; h = (h ^ (uint32_t)c->ev[i].at) * 16777619u; }
    return h;
}

static void test_prob(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16");
    CHECK(run_count(e, c, 0, 8) == 128);
    S(e, 0, "prob", "-100"); CHECK(run_count(e, c, 0, 8) == 0);
    S(e, 0, "prob", "100");  CHECK(run_count(e, c, 0, 8) == 0);
    S(e, 0, "prob", "-50");
    int n1 = run_count(e, c, 0, 8); uint32_t s1 = signature(c);
    int n2 = run_count(e, c, 0, 8); uint32_t s2 = signature(c);
    CHECK(n1 > 30 && n1 < 98 && n1 == n2 && s1 == s2);
    S(e, -1, "seed", "12345");
    run_count(e, c, 0, 8);
    CHECK(signature(c) != s1);
    /* a silent pulse silences its repeats too: groups of 0 or 3 */
    S(e, 0, "pulses", "4"); S(e, 0, "repeats", "2"); S(e, 0, "time", "1/32");
    run_count(e, c, 0, 8);
    int ok = 1, some0 = 0, some3 = 0;
    for (int k = 0; k < 32; k++) { int m = count_ons(c, 0, k * 96.0, k * 96.0 + 96); if (m != 0 && m != 3) ok = 0; some0 |= m == 0; some3 |= m == 3; }
    CHECK(ok && some0 && some3);
    /* positive: each note of a chord on its own */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16"); S(e, 0, "pool", "60,62,64,65,67,69,71,72"); S(e, 0, "prob", "50");
    run_count(e, c, 0, 4);
    grp_t g[128]; int ng = groups(c, 0, g, 128), partial = 0, total = 0;
    for (int i = 0; i < ng; i++) { total += g[i].n; if (g[i].n > 0 && g[i].n < 8) partial++; }
    CHECK(partial > 10 && total > 128 && total < 384);
    S(e, 0, "prob", "0"); CHECK(run_count(e, c, 0, 1) == 16 * 8);
    free(e); free(c);
}

static void test_random(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    non_t o[512]; int n;
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16"); S(e, 0, "r.velocity", "100");
    run_count(e, c, 0, 4); n = ons(c, 0, o, 512);
    int ge = 1, some = 0; for (int i = 0; i < n; i++) { if (o[i].vel < 100) ge = 0; if (o[i].vel > 100) some = 1; }
    CHECK(n == 64 && ge && some);
    uint32_t s1 = signature(c);
    run_count(e, c, 0, 4); CHECK(signature(c) == s1);
    S(e, -1, "seed", "99"); run_count(e, c, 0, 4); CHECK(signature(c) != s1);
    S(e, 0, "r.velocity", "-100");
    run_count(e, c, 0, 4); n = ons(c, 0, o, 512);
    int le = 1; some = 0; for (int i = 0; i < n; i++) { if (o[i].vel > 100) le = 0; if (o[i].vel < 100) some = 1; }
    CHECK(le && some);
    S(e, 0, "random", "0");
    run_count(e, c, 0, 4); n = ons(c, 0, o, 512);
    int eq = 1; for (int i = 0; i < n; i++) if (o[i].vel != 100) eq = 0;
    CHECK(eq);
    /* clamped to the range */
    S(e, 0, "random", "100"); S(e, 0, "velocity", "120"); S(e, 0, "r.velocity", "100");
    run_count(e, c, 0, 4); n = ons(c, 0, o, 512);
    int inr = 1; for (int i = 0; i < n; i++) if (o[i].vel > 127) inr = 0;
    CHECK(inr);
    char b[16]; e1_get(e, 0, "r.velocity", b, sizeof b); CHECK(strcmp(b, "100") == 0);
    S(e, 0, "r.velocity", "500"); CHECK(e->t[0].rnd[E1_P_VELOCITY] == 100);
    S(e, 0, "r.velocity", "d-250"); CHECK(e->t[0].rnd[E1_P_VELOCITY] == -100);
    CHECK(e1_set(e, 0, "r.channel", "50") == 0);
    CHECK(e1_set(e, 0, "r.nope", "50") == 0);
    S(e, 0, "clearrandom", "1"); CHECK(e->t[0].rnd[E1_P_VELOCITY] == 0);

    /* random wander over a run: velocities differ bar to bar */
    S(e, 0, "r.velocity", "100"); S(e, 0, "velocity", "60"); S(e, 0, "rate", "1/16");
    run_count(e, c, 0, 8); n = ons(c, 0, o, 512);
    int bars_same = 1;
    for (int i = 16; i < n; i++) if (o[i].vel != o[i % 16].vel) bars_same = 0;
    /* doubtful: rate-driven random uses ((T / rate) & 15), so it repeats every
     * 16 rate steps (one bar at 1/16); the "wander" is a fixed one-bar loop. */
    CHECKM(!bars_same, "random velocities repeat exactly every bar");

    /* random steps/rotate/pulses: no bursts, sync holds */
    e1_engine_t *b2 = eng_new(); cap_t *c2 = cap_new();
    for (int sgn = -1; sgn <= 1; sgn += 2) {
        setup(e); fresh(e, 0); S(e, 0, "cycles", "8");
        S(e, 0, "r.steps", sgn > 0 ? "100" : "-100"); S(e, 0, "r.rotate", "100"); S(e, 0, "r.pulses", "-60");
        memcpy(b2, e, sizeof *e);
        int stp_ok = 1, varied = 0;
        for (int cy = 0; cy < 8; cy++) { int s = e1_steps(e, 0, cy); if (s < 1 || s > 64) stp_ok = 0; if (s != 16) varied = 1; }
        CHECK(stp_ok && varied);
        cap_clear(c); cap_clear(c2);
        e1_start(e, 0); e1_start(b2, 0);
        run(e, c, 0, 32 * 384, 0.5); run(b2, c2, 0, 32 * 384, 192);
        CHECK(same_sequence(c, c2, 0));
        non_t *oo = malloc(sizeof(non_t) * 4096); int m = ons(c, 0, oo, 4096), burst = 0;
        for (int i = 1; i < m && i < 4096; i++) if (oo[i].at - oo[i - 1].at < 24) burst++;
        CHECK(burst == 0 && m > 0);
        free(oo);
        /* changing the random amount and the seed mid-play */
        cap_clear(c);
        for (int k = 32 * 384; k < 40 * 384; k++) {
            if (k % 500 == 0) { char v[16]; snprintf(v, sizeof v, "%d", (k / 500) % 200 - 100); S(e, 0, "r.steps", v); S(e, -1, "seed", v); }
            g_now = k; e1_advance(e, k, emit, c);
        }
        int per_slot_max = 0;
        for (int k = 0; k < 8 * 16; k++) { int q = count_ons(c, 0, 32 * 384 + k * 24.0, 32 * 384 + k * 24.0 + 24); if (q > per_slot_max) per_slot_max = q; }
        CHECK(per_slot_max <= 1);
        e1_stop(e, emit, c); CHECK(balanced(c));
    }
    free(b2); free(c2);
    free(e); free(c);
}

/* ------------------------------------------------------------------ CC */

static void test_cc(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 12);
    S(e, 12, "type", "CC"); S(e, 12, "cc1", "64"); S(e, 12, "ccn1", "74"); S(e, 12, "cc3", "10");
    char b[16]; e1_get(e, 12, "cc1", b, sizeof b); CHECK(strcmp(b, "64") == 0);
    e1_get(e, 12, "ccn1", b, sizeof b); CHECK(strcmp(b, "74") == 0);
    e1_get(e, 12, "cc2", b, sizeof b); CHECK(strcmp(b, "-1") == 0);
    /* live edit goes out at once, even stopped */
    g_now = 0; e1_advance(e, 0, emit, c);
    CHECK(count_cc(c, 12, 74, 64) == 1 && count_cc(c, 12, 72, 10) == 1);
    cap_clear(c);
    e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    CHECK(count_cc(c, 12, 74, 64) == 16 && count_cc(c, 12, 72, 10) == 16 && count_cc(c, 12, -1, -1) == 32);
    CHECK(count_ons(c, -1, 0, 1e9) == 0);
    /* on change */
    S(e, 12, "ccmode", "Change");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    CHECK(count_cc(c, 12, -1, -1) == 0);
    /* per cycle: cycle 2 sends 100 */
    S(e, 12, "edit", "2"); S(e, 12, "cc1", "100"); S(e, 12, "edit", "0");
    CHECK(e->t[12].cc_cyc[1][0] == 36 && e->t[12].cc_val[0] == 64);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 8 * 384 - 1, 1);
    CHECK(count_cc(c, 12, 74, 100) == 2 && count_cc(c, 12, 74, 64) == 3);   /* edit send + back from cycle 2 twice */
    /* random lane amount */
    S(e, 12, "ccmode", "Pulse"); S(e, 12, "r.cc1", "100");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    int ge = 1, var = 0;
    for (int i = 0; i < c->n; i++) if (c->ev[i].d1 == 74) { if (c->ev[i].d2 < 64) ge = 0; if (c->ev[i].d2 != 64 && c->ev[i].d2 != 100) var = 1; }
    CHECK(ge && var);
    S(e, 12, "clearparam", "cc1");
    CHECK(e->t[12].cc_rnd[0] == 0 && e->t[12].cc_cyc[1][0] == 0);
    /* glide slews */
    S(e, 12, "ccmode", "Glide"); S(e, 12, "slew", "30"); S(e, 12, "cc3", "-1");
    S(e, 12, "c2.cc1", "60");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 4 * 384 - 1, 1);
    int distinct = 0, seen[128] = { 0 };
    for (int i = 0; i < c->n; i++) if ((c->ev[i].st & 0xF0) == 0xB0 && c->ev[i].d1 == 74 && !seen[c->ev[i].d2]) { seen[c->ev[i].d2] = 1; distinct++; }
    CHECK(distinct > 4 && seen[124] && seen[64]);
    /* muted: nothing at all */
    S(e, 12, "ccmode", "Pulse"); S(e, 12, "mute", "On"); S(e, 12, "cc1", "33");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 2 * 384 - 1, 1);
    CHECK(c->n == 0);
    /* solo-muted: nothing either */
    S(e, 12, "mute", "Off"); e1_perf(e, 1, "solo", 1);
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 2 * 384 - 1, 1);
    CHECK(count_cc(c, 12, -1, -1) == 0);
    e1_perf(e, 0, "clear", 0);
    /* the channel and ports follow the track */
    S(e, 12, "channel", "9"); S(e, 12, "output", "Computer");
    cap_clear(c); e1_start(e, 0); run(e, c, 0, 383, 1);
    CHECK(count_cc(c, 8, 74, -1) >= 4 && c->ev[0].ports == E1_PORT_COMPUTER);
    free(e); free(c);
}

/* ----------------------------------------------------- note-off balance */

static uint32_t rng = 1;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void test_balance_stress(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    static const char *keys[] = { "steps", "pulses", "rotate", "division", "length", "cycles", "velocity", "prob", "sustain",
        "repeats", "time", "ramp", "pace", "fill", "delay", "pitch", "harmony", "voicing", "style", "range", "phrase", "scale",
        "root", "accent", "groove", "timing", "swinggrid", "random", "rate", "channel", "output", "mute", "dir", "tail",
        "phraserate", "type", "r.steps", "r.pitch", "r.repeats", "r.voicing", "r.sustain", "pool", "pooltoggle", "toggle", "edit",
        "cc1", "ccmode", "slew" };
    static const char *moves[] = { "fill", "ratchet", "speed", "reverse", "freeze", "accent", "solo", "restart", "clear" };
    char dump[16384];
    for (int round = 0; round < 3; round++) {
        cap_clear(c); e1_init(e); rng = 1234 + round;
        for (int i = 0; i < E1_TRACKS; i++) e->t[i].v[E1_P_MUTE] = 0;
        S(e, 9, "tail", "Tail"); S(e, 9, "repeats", "49"); S(e, 10, "sustain", "100");
        e1_start(e, 0);
        double t = 0;
        for (int k = 0; k < 60000; k++) {
            t += 0.3 + (rnd() % 100) / 50.0;
            if (rnd() % 40 == 0) {
                int tr = rnd() % 16, ki = rnd() % (int)(sizeof keys / sizeof keys[0]);
                char v[48];
                if (!strcmp(keys[ki], "pool")) snprintf(v, sizeof v, "%u,%u,%u", rnd() % 128, rnd() % 128, rnd() % 128);
                else if (!strcmp(keys[ki], "edit")) snprintf(v, sizeof v, "%x", rnd() % 4 ? 0 : rnd() & 0xFFFF);
                else if (rnd() % 3 == 0) snprintf(v, sizeof v, "d%+d", (int)(rnd() % 7) - 3);
                else snprintf(v, sizeof v, "%d", (int)(rnd() % 140) - 20);
                e1_set(e, tr, keys[ki], v);
            }
            if (rnd() % 300 == 0) e1_perf(e, rnd() & 0xFFFF, moves[rnd() % 9], (int)(rnd() % 5) - 1);
            if (rnd() % 2000 == 0) { int a = rnd() % 16, b = rnd() % 16; e1_dump_track(e, a, dump, sizeof dump); e1_load_track(e, b, dump); e1_resync(e, b); }
            if (rnd() % 3000 == 0) e1_track_silence(e, rnd() % 16, emit, c);
            if (rnd() % 5000 == 0) { e1_set(e, -1, "dest", (rnd() & 1) ? "Both" : "Move"); }
            if (rnd() % 7000 == 0) { e1_stop(e, emit, c); CHECK(sounding(c) == 0); e1_start(e, t); }
            g_now = t;
            e1_advance(e, t, emit, c);
        }
        CHECKM(c->neg == 0, "round %d: %d note-offs without a note-on", round, c->neg);
        e1_stop(e, emit, c);
        CHECKM(balanced(c), "round %d: %d notes left on", round, sounding(c));
        e1_all_notes_off(e, emit, c);
        CHECK(balanced(c));
        CHECK(count_ons(c, -1, 0, 1e12) > 1000);
    }
    free(e); free(c);
}

/* ------------------------------------------------------------ save text */

static void heavy_edit(e1_engine_t *e, int ti)
{
    S(e, ti, "name", "Heavy-Trk_12");
    S(e, ti, "steps", "13"); S(e, ti, "pulses", "5"); S(e, ti, "rotate", "4"); S(e, ti, "division", "1/8T");
    S(e, ti, "length", "16"); S(e, ti, "cycles", "16"); S(e, ti, "type", "CC"); S(e, ti, "scale", "Dorian");
    S(e, ti, "root", "F#"); S(e, ti, "style", "MonoClimb"); S(e, ti, "phrase", "Tri"); S(e, ti, "groove", "Bongo");
    S(e, ti, "swinggrid", "x4"); S(e, ti, "rate", "1/4."); S(e, ti, "output", "Both"); S(e, ti, "dir", "Down");
    S(e, ti, "tail", "Tail"); S(e, ti, "phraserate", "/2"); S(e, ti, "ccmode", "Glide"); S(e, ti, "slew", "77");
    S(e, ti, "channel", "11"); S(e, ti, "mute", "On"); S(e, ti, "delay", "-37"); S(e, ti, "prob", "-12");
    S(e, ti, "pool", "40,52,61,70,99"); S(e, ti, "addmask", "8000000000000003"); S(e, ti, "delmask", "f0");
    S(e, ti, "r.pitch", "-55"); S(e, ti, "r.steps", "33"); S(e, ti, "r.velocity", "100");
    S(e, ti, "cc1", "0"); S(e, ti, "cc16", "127"); S(e, ti, "ccn16", "1"); S(e, ti, "r.cc16", "-100"); S(e, ti, "r.cc5", "7");
    S(e, ti, "edit", "8001"); S(e, ti, "velocity", "3"); S(e, ti, "steps", "64"); S(e, ti, "toggle", "2"); S(e, ti, "cc1", "90");
    S(e, ti, "edit", "4"); S(e, ti, "division", "1/1"); S(e, ti, "pitch", "-36"); S(e, ti, "toggle", "0");
    S(e, ti, "edit", "0");
    S(e, ti, "c9.cc7", "-20");
}

static void test_save(void)
{
    e1_engine_t *a = eng_new(), *b = eng_new();
    char *buf = malloc(1 << 20);
    e1_init(a); e1_init(b);
    heavy_edit(a, 3);
    int n = e1_dump_track(a, 3, buf, 1 << 20);
    CHECK(n > 0 && (int)strlen(buf) == n);
    e1_load_track(b, 7, buf);
    CHECK(memcmp(&a->t[3], &b->t[7], E1_TRACK_STORED_BYTES) == 0);
    char b2[64]; e1_get(b, 7, "name", b2, sizeof b2); CHECK(strcmp(b2, "Heavy-Trk_12") == 0);
    /* dumping the copy gives the same text */
    char *buf2 = malloc(1 << 20);
    e1_dump_track(b, 7, buf2, 1 << 20);
    CHECK(strcmp(buf, buf2) == 0);

    /* absent fields reset, edit mode ignored */
    S(b, 7, "edit", "2");
    e1_load_track(b, 7, "steps=5\nvelocity=50\ncc2=9\n");
    e1_track_t want; e1_track_defaults(&want, 7);
    want.v[E1_P_STEPS] = 5; want.v[E1_P_VELOCITY] = 50; want.cc_val[1] = 9;
    CHECK(memcmp(&b->t[7], &want, E1_TRACK_STORED_BYTES) == 0);
    CHECK(b->t[7].cyc[1][E1_P_VELOCITY] == 0 && b->t[7].edit_mask == 2);
    S(b, 7, "edit", "0");

    e1_init(a);
    printf("default track dump: %d bytes\n", e1_dump_track(a, 0, buf, 1 << 20));

    /* pooltoggle-removed notes leave bytes behind */
    e1_init(a); e1_init(b);
    S(a, 0, "pool", "40,50,60"); S(a, 0, "pooltoggle", "60");
    e1_dump_track(a, 0, buf, 1 << 20); e1_load_track(b, 0, buf);
    /* BUG (minor): pool_toggle/pool_parse leave stale notes past pool_n, so the
     * stored part is not byte-identical after a save/load round trip (and
     * plugin "temp" sees a change that is not there). e1_engine.c pool_toggle. */
    CHECK(memcmp(&a->t[0], &b->t[0], E1_TRACK_STORED_BYTES) == 0);

    /* worst-case dump: every cycle offset, mask and random amount at its widest */
    e1_init(a);
    e1_track_t *t = &a->t[0];
    snprintf(t->name, sizeof t->name, "MMMMMMMMMMMM");
    for (int p = 0; p < E1_NP; p++) { char k[32]; snprintf(k, sizeof k, "r.%s", e1_param_key(p)); e1_set(a, 0, k, "-100"); }
    for (int c = 0; c < E1_CYCLES; c++) {
        for (int p = 0; p < E1_NP; p++) { char k[32]; snprintf(k, sizeof k, "c%d.%s", c + 1, e1_param_key(p)); e1_set(a, 0, k, "-200"); }
        for (int l = 0; l < E1_CC_LANES; l++) t->cc_cyc[c][l] = -127;
        t->cadd[c] = t->cdel[c] = ~0ULL;
    }
    for (int l = 0; l < E1_CC_LANES; l++) { t->cc_val[l] = 127; t->cc_num[l] = 127; t->cc_rnd[l] = -100; }
    t->add_mask = t->del_mask = ~0ULL;
    S(a, 0, "pool", "100,101,102,103,104,105,106,107");
    n = e1_dump_track(a, 0, buf, 1 << 20);
    printf("worst-case track dump: %d bytes (x16 tracks = %d)\n", n, n * 16);
    CHECK(n < 128 * 1024);
    e1_load_track(b, 0, buf);
    CHECK(memcmp(&a->t[0], &b->t[0], E1_TRACK_STORED_BYTES) == 0);
    /* a short buffer truncates cleanly */
    n = e1_dump_track(a, 0, buf, 100);
    CHECK(n < 100 && buf[n] == 0 && (int)strlen(buf) == n);
    CHECK(e1_dump_track(a, 0, buf, 1) == 0 && buf[0] == 0);

    /* globals */
    S(a, -1, "dest", "Both"); S(a, -1, "seed", "4242");
    n = e1_dump_global(a, buf, 4096);
    e1_load_global(b, buf);
    CHECK(b->dest == E1_DEST_BOTH && b->seed == 4242);
    e1_get(b, -1, "dest", b2, sizeof b2); CHECK(strcmp(b2, "Both") == 0);
    CHECK(e1_get(b, -1, "nope", b2, sizeof b2) == -1);

    /* names: no characters that break the save text or JSON */
    S(a, 0, "name", "a\"b\\c=d\ne");
    e1_get(a, 0, "name", b2, sizeof b2);
    CHECK(!strpbrk(b2, "\"\\=\n"));
    S(a, 0, "name", "x\ty\rz\x01");
    e1_get(a, 0, "name", b2, sizeof b2);
    int ctrl = 0; for (char *q = b2; *q; q++) if ((unsigned char)*q < 0x20) ctrl = 1;
    /* BUG: the name filter (set_track "name") lets tabs, CR and other control
     * characters through; they make the plugin's "ui" JSON invalid and a
     * trailing CR is lost on load (each_line strips it). */
    CHECK(!ctrl);
    S(a, 0, "name", "a very long name indeed");
    e1_get(a, 0, "name", b2, sizeof b2); CHECK(strlen(b2) == E1_NAME_LEN - 1);
    free(buf); free(buf2); free(a); free(b);
}

/* --------------------------------------------------------- robustness */

static void test_keys(void)
{
    e1_engine_t *e = eng_new(); char b[256];
    e1_init(e);
    CHECK(e1_set(e, 0, "nope", "1") == 0);
    CHECK(e1_set(e, 0, "", "1") == 0);
    CHECK(e1_set(e, 0, "c17.steps", "1") == 0);
    CHECK(e1_set(e, 0, "c0.steps", "1") == 0);
    CHECK(e1_set(e, 0, "c1.mute", "1") == 0);
    CHECK(e1_set(e, 0, "c1.nope", "1") == 0);
    CHECK(e1_set(e, 0, "cc17", "1") == 0);
    CHECK(e1_set(e, 0, "cc0", "1") == 0);
    CHECK(e1_set(e, 0, "ccn17", "1") == 0);
    CHECK(e1_set(e, 0, "r.", "1") == 0);
    CHECK(e1_set(e, 16, "steps", "1") == 0);
    CHECK(e1_set(e, -1, "steps", "1") == 0);
    CHECK(e1_set(e, 0, NULL, "1") == 0 && e1_set(e, 0, "steps", NULL) == 0);
    CHECK(e1_set(e, 0, "steps", "") == 0 && e1_set(e, 0, "steps", "abc") == 0);
    CHECK(e1_get(e, 0, "nope", b, sizeof b) == -1 && e1_get(e, 16, "steps", b, sizeof b) == -1);
    CHECK(e1_get(e, 0, "steps", NULL, 4) == -1 && e1_get(e, 0, "steps", b, 0) == -1);
    /* names, numbers and relative steps */
    S(e, 0, "division", "1/8T");  e1_get(e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/8T") == 0);
    S(e, 0, "division", "3");     e1_get(e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/8") == 0);
    S(e, 0, "division", "d+1");   e1_get(e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/16") == 0);
    S(e, 0, "length", "16");      CHECK(e->t[0].v[E1_P_LENGTH] == 9);
    S(e, 0, "length", "Off");     CHECK(e->t[0].v[E1_P_LENGTH] == 0);
    S(e, 0, "steps", "8"); S(e, 0, "steps", "999"); CHECK(e->t[0].v[E1_P_STEPS] == 64);
    S(e, 0, "steps", "d-100");    CHECK(e->t[0].v[E1_P_STEPS] == 1);
    S(e, 0, "steps", "4294967312");   /* 2^32 + 16 */
    /* doubtful: strtol's long is cast to int before clamping, so 2^32+16 sets 16 */
    CHECKM(e->t[0].v[E1_P_STEPS] == 64, "steps=4294967312 gave %d", e->t[0].v[E1_P_STEPS]);
    CHECK(e1_param_count() == E1_NP && strcmp(e1_param_key(0), "type") == 0 && e1_param_key(-1)[0] == 0 && e1_param_key(E1_NP)[0] == 0);
    CHECK(e1_get_num(e, 99, 0) == 0 && e1_get_num(e, 0, E1_NP) == 0);
    CHECK(e1_hit_mask(e, 99, 0, 0) == 0 && e1_hit_mask(e, 0, 40, 0) != 0);
    CHECK(e1_ports(e, &e->t[0]) == E1_PORT_MOVE);
    S(e, -1, "dest", "Computer"); CHECK(e1_ports(e, &e->t[0]) == E1_PORT_COMPUTER);
    S(e, -1, "dest", "2");        CHECK(e1_ports(e, &e->t[0]) == (E1_PORT_MOVE | E1_PORT_COMPUTER));
    S(e, 0, "output", "Move");    CHECK(e1_ports(e, &e->t[0]) == E1_PORT_MOVE);
    S(e, 0, "output", "Computer"); CHECK(e1_ports(e, &e->t[0]) == E1_PORT_COMPUTER);
    free(e);
}

static void fuzz_str(char *s, int max)
{
    static const char alpha[] = "abcdefghijklmnopqrstuvwxyz0123456789.,=+-d:\n\r/ #xX%\\\"";
    int n = (int)(rnd() % (uint32_t)max);
    for (int i = 0; i < n; i++) s[i] = (rnd() % 20 == 0) ? (char)(rnd() % 255 + 1) : alpha[rnd() % (sizeof alpha - 1)];
    s[n] = 0;
}

static void test_fuzz(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    static const char *pre[] = { "", "r.", "c1.", "c16.", "c99.", "cc", "ccn", "r.cc", "c3.cc" };
    static char big[300000];
    char k[400], v[400], out[512];
    e1_init(e); e1_start(e, 0); rng = 77;
    for (int i = 0; i < 40000; i++) {
        int mode = (int)(rnd() % 4);
        if (mode == 0) { snprintf(k, sizeof k, "%s%s", pre[rnd() % 9], e1_param_key((int)(rnd() % (E1_NP + 2)) - 1)); fuzz_str(v, 40); }
        else if (mode == 1) { fuzz_str(k, 30); fuzz_str(v, 300); }
        else if (mode == 2) {
            static const char *cmd[] = { "toggle", "addmask", "delmask", "pool", "pooltoggle", "name", "edit", "clearcycle", "copycycle", "clearparam", "clearedits", "clearrandom" };
            snprintf(k, sizeof k, "%s", cmd[rnd() % 12]); fuzz_str(v, 60);
        } else {
            snprintf(k, sizeof k, "%s", e1_param_key((int)(rnd() % E1_NP)));
            static const char *vals[] = { "d+999999999", "d-999999999", "-2147483648", "2147483647", "99999999999999999999", "1e9", "0x10", " 5", "d+", "d", "-", "Major", "1/4." };
            snprintf(v, sizeof v, "%s", vals[rnd() % 13]);
        }
        e1_set(e, (int)(rnd() % 18) - 1, k, v);
        e1_get(e, (int)(rnd() % 18) - 1, k, out, (int)(rnd() % 64) + 1);
        if (i % 50 == 0) { g_now = i; e1_advance(e, i, emit, c); if (c->n > MAXEV / 2) cap_clear(c); }
        if (i % 997 == 0) {
            int L = (int)(rnd() % (sizeof big - 1));
            for (int j = 0; j < L; j++) big[j] = (rnd() % 8 == 0) ? '\n' : (char)(rnd() % 255 + 1);
            big[L] = 0;
            e1_load_track(e, (int)(rnd() % 16), big);
            e1_load_global(e, big);
        }
        if (i % 2003 == 0) e1_perf(e, rnd(), e1_param_key((int)(rnd() % E1_NP)), (int)rnd());
    }
    /* still sane: every value in range, dump/load round trip holds */
    int sane = 1;
    for (int ti = 0; ti < E1_TRACKS; ti++) {
        for (int p = 0; p < E1_NP; p++) { int x = e1_get_num(e, ti, p); char tmp[64]; if (e1_get(e, ti, e1_param_key(p), tmp, sizeof tmp) < 0) sane = 0; (void)x; }
        if (e->t[ti].pool_n > E1_POOL) sane = 0;
    }
    CHECK(sane);
    e1_stop(e, emit, c);
    CHECK(c->neg == 0);
    free(e); free(c);
}

/* ------------------------------------------- probes for undefined behaviour */

static int probe(const char *name)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    setup(e); fresh(e, 0);
    if (!strcmp(name, "dplus")) {
        e1_set(e, 0, "steps", "d+2147483647");
        e1_set(e, 0, "steps", "d-2147483648");
    } else if (!strcmp(name, "perfrotate")) {
        S(e, 0, "rotate", "5");
        e1_perf(e, 1, "rotate", INT_MAX);
    } else if (!strcmp(name, "longrun")) {
        /* about 10 days of transport at 120 bpm */
        S(e, 0, "prob", "50"); S(e, 0, "pulses", "16");
        e1_join(e, 0, 1.7e8);
        run(e, c, 1.7e8, 1.7e8 + 384, 1);
    } else if (!strcmp(name, "nan")) {
        e1_start(e, 0);
        run(e, c, 0, 100, 1);
        int before = count_ons(c, -1, -1e18, 1e18), n0 = c->n;
        g_now = NAN; e1_advance(e, NAN, emit, c);
        int burst = 0;
        for (int i = n0; i < c->n; i++) if ((c->ev[i].st & 0xF0) == 0x90 && c->ev[i].d2) burst++;
        (void)before;
        printf("e1_advance(NaN): %d note-ons in one call\n", burst);
        free(e); free(c);
        return burst > 2 ? 3 : 0;
    }
    free(e); free(c);
    return 0;
}

/* 0.4: audition, repeat stop, random start, retrigger steps, loop lock. */
static uint32_t bar_sig(const cap_t *c, int bar)
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < c->n; i++) {
        const ev_t *e = &c->ev[i];
        if ((e->st & 0xF0) != 0x90 || !e->d2) continue;
        if (e->at < bar * 384.0 || e->at >= (bar + 1) * 384.0) continue;
        h = (h ^ e->d1) * 16777619u; h = (h ^ e->d2) * 16777619u; h = (h ^ (uint32_t)(e->at - bar * 384.0)) * 16777619u;
    }
    return h;
}

static void test_v04(void)
{
    e1_engine_t *e = eng_new(); cap_t *c = cap_new();
    non_t o[1024]; int n;

    /* audition: one hit while stopped, let go on release, CC lanes for a CC track */
    setup(e); fresh(e, 0); S(e, 0, "pool", "60,64,67"); S(e, 0, "velocity", "90");
    cap_clear(c);
    e1_audition(e, 0, 1, emit, c);
    n = ons(c, 0, o, 16);
    CHECK(n == 3 && o[0].note == 60 && o[2].note == 67 && o[0].vel == 90 && sounding(c) == 3);
    e1_audition(e, 0, 0, emit, c);
    CHECK(balanced(c));
    e1_audition(e, 0, 1, emit, c); e1_audition(e, 0, 1, emit, c);      /* a second press lets the first go */
    CHECK(sounding(c) == 3);
    e1_track_silence(e, 0, emit, c);
    CHECK(balanced(c));
    e->t[0].v[E1_P_MUTE] = 1;
    e1_audition(e, 0, 1, emit, c); CHECK(sounding(c) == 3);            /* heard even when muted */
    e1_audition(e, 0, 0, emit, c); CHECK(balanced(c));
    fresh(e, 11); S(e, 11, "type", "CC"); S(e, 11, "cc1", "55"); S(e, 11, "cc3", "99");
    cap_clear(c);
    e1_audition(e, 11, 1, emit, c);
    CHECK(c->n == 2 && count_cc(c, 11, e->t[11].cc_num[0], 55) == 1 && count_cc(c, 11, e->t[11].cc_num[2], 99) == 1);
    e1_audition(e, 16, 1, emit, c); e1_audition(e, -1, 1, emit, c);
    CHECK(c->n == 2);

    /* repeat stop: an endless chain is cut, the next pulse starts a new one */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "1"); S(e, 0, "repeats", "49"); S(e, 0, "time", "1/16"); S(e, 0, "tail", "Tail");
    cap_clear(c); e1_start(e, 0);
    run(e, c, 0, 100, 1);
    int a = count_ons(c, 0, 0, 1e12);
    CHECK(a >= 4 && e->t[0].chain[0].left > 0);
    e1_perf(e, 1, "stoprepeats", 0);
    run(e, c, 101, 380, 1);
    CHECK(count_ons(c, 0, 0, 1e12) == a);
    run(e, c, 381, 500, 1);
    CHECK(count_ons(c, 0, 0, 1e12) > a);
    e1_stop(e, emit, c); CHECK(balanced(c));

    /* random start: the track jumps by whole steps and stays on the grid */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16");
    cap_clear(c); e1_start(e, 0);
    run(e, c, 0, 100, 1);
    int moved = 0;
    for (int k = 0; k < 8; k++) {
        double was = e->t[0].toff;
        e1_perf(e, 1, "randstart", 1);
        double d = was - e->t[0].toff;
        if (d >= 24 && fmod(d, 24.0) == 0.0 && d < 16 * 24) moved++;
    }
    CHECK(moved == 8);
    run(e, c, 101, 300, 1);
    n = ons(c, 0, o, 1024);
    int grid = 1; for (int i = 0; i < n; i++) if (fmod(o[i].at, 24.0) != 0.0) grid = 0;
    CHECK(grid && n >= 12 && n <= 14);
    e1_perf(e, 1, "randstart", 0); CHECK(e->t[0].toff == 0);
    e1_stop(e, emit, c);

    /* loop lock: chance and random values come round every N bars */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16"); S(e, 0, "prob", "-50"); S(e, 0, "r.velocity", "100");
    S(e, 0, "pool", "60,62,64"); S(e, 0, "repeats", "1"); S(e, 0, "time", "1/32");
    run_count(e, c, 0, 8);
    CHECK(bar_sig(c, 0) != bar_sig(c, 1) && bar_sig(c, 1) != bar_sig(c, 2));   /* Free: every bar differs */
    uint32_t free_sig = signature(c);
    S(e, 0, "loop", "1");
    run_count(e, c, 0, 8);
    int same = 1; for (int b = 1; b < 8; b++) if (bar_sig(c, b) != bar_sig(c, 0)) same = 0;
    CHECK(same && count_ons(c, 0, 0, 384) > 4);
    uint32_t one = bar_sig(c, 0);
    S(e, 0, "loop", "2");
    run_count(e, c, 0, 8);
    CHECK(bar_sig(c, 0) == bar_sig(c, 2) && bar_sig(c, 1) == bar_sig(c, 3) && bar_sig(c, 4) == bar_sig(c, 0) && bar_sig(c, 0) != bar_sig(c, 1));
    S(e, 0, "loop", "8");
    run_count(e, c, 0, 16);
    CHECK(bar_sig(c, 0) == bar_sig(c, 8) && bar_sig(c, 7) == bar_sig(c, 15) && bar_sig(c, 0) != bar_sig(c, 4));
    /* re-roll: a new track seed gives a new loop, and only for that track */
    S(e, 0, "loop", "1"); S(e, 0, "rseed", "777");
    run_count(e, c, 0, 4);
    CHECK(bar_sig(c, 0) != one && bar_sig(c, 0) == bar_sig(c, 3));
    CHECK(e->t[1].rseed == 0);
    S(e, 0, "rseed", "0"); S(e, 0, "loop", "Free");
    run_count(e, c, 0, 8);
    CHECK(signature(c) == free_sig);                                    /* Free with no own seed: as before */
    S(e, 0, "prob", "50"); S(e, 0, "loop", "4");                        /* per-note drops loop too */
    run_count(e, c, 0, 8);
    CHECK(bar_sig(c, 0) == bar_sig(c, 4) && bar_sig(c, 3) == bar_sig(c, 7));

    /* retrigger step: random and phrase count from it */
    setup(e); fresh(e, 0); S(e, 0, "steps", "12"); S(e, 0, "pulses", "12"); S(e, 0, "r.velocity", "100");
    S(e, 0, "range", "7"); S(e, 0, "phrase", "Saw"); S(e, 0, "scale", "Major");
    run_count(e, c, 0, 6); n = ons(c, 0, o, 1024);
    int diff = 0; for (int i = 0; i + 12 < n; i++) if (o[i].vel != o[i + 12].vel || o[i].note != o[i + 12].note) diff = 1;
    CHECK(n == 6 * 16 && diff);                                         /* 12 steps against the bar: passes differ */
    S(e, 0, "retrigtoggle", "0");
    CHECK(e->t[0].retrig == 1);
    run_count(e, c, 0, 6); n = ons(c, 0, o, 1024);
    diff = 0; for (int i = 0; i + 12 < n; i++) if (o[i].vel != o[i + 12].vel || o[i].note != o[i + 12].note) diff = 1;
    CHECK(n == 6 * 16 && !diff);                                        /* every pass now the same */
    S(e, 0, "retrigtoggle", "0"); CHECK(e->t[0].retrig == 0);
    /* style: a mono walk starts again on the marked step */
    setup(e); fresh(e, 0); S(e, 0, "steps", "8"); S(e, 0, "pulses", "8"); S(e, 0, "pool", "60,62,64"); S(e, 0, "style", "MonoFix");
    S(e, 0, "retrig", "10");                                            /* step 5 */
    run_count(e, c, 0, 1); n = ons(c, 0, o, 1024);
    CHECK(n == 16 && o[0].note == 60 && o[1].note == 62 && o[3].note == 60 && o[4].note == 60 && o[5].note == 62 && o[8].note == 60);
    /* groove counts from the marked step too */
    setup(e); fresh(e, 0); S(e, 0, "pulses", "16"); S(e, 0, "accent", "100"); S(e, 0, "groove", "Agogo"); S(e, 0, "velocity", "60");
    run_count(e, c, 0, 1); n = ons(c, 0, o, 1024);
    int v0 = o[0].vel, v1 = o[1].vel;
    S(e, 0, "retrig", "20");                                            /* step 6 */
    run_count(e, c, 0, 1); n = ons(c, 0, o, 1024);
    CHECK(o[0].vel == v0 && o[5].vel == v0 && o[6].vel == v1 && v0 != v1);

    /* saved: loop, retrig and rseed round-trip; old dumps load as Free */
    static char buf[16384];
    setup(e); fresh(e, 0); S(e, 0, "loop", "4"); S(e, 0, "retrig", "81"); S(e, 0, "rseed", "4242");
    e1_dump_track(e, 0, buf, sizeof buf);
    CHECK(strstr(buf, "loop=4\n") && strstr(buf, "retrig=81\n") && strstr(buf, "rseed=4242\n"));
    e1_load_track(e, 1, buf);
    CHECK(e->t[1].v[E1_P_LOOP] == 3 && e->t[1].retrig == 0x81 && e->t[1].rseed == 4242);
    e1_load_track(e, 1, "steps=8\nfuturekey=9\n");
    CHECK(e->t[1].v[E1_P_LOOP] == 0 && e->t[1].retrig == 0 && e->t[1].rseed == 0 && e->t[1].v[E1_P_STEPS] == 8);
    fresh(e, 2); e1_dump_track(e, 2, buf, sizeof buf);
    CHECK(strstr(buf, "loop=Free\n") && !strstr(buf, "retrig=") && !strstr(buf, "rseed="));
    free(e); free(c);
}

static void run_probe(const char *name, const char *why)
{
    fflush(stdout);
    pid_t pid = fork();
    if (pid == 0) {
        setenv("UBSAN_OPTIONS", "halt_on_error=1:exitcode=97:print_stacktrace=0", 1);
        execl("/proc/self/exe", "test_engine", "--probe", name, (char *)NULL);
        _exit(98);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + WTERMSIG(st);
    CHECKM(code == 0, "probe %s exited %d: %s", name, code, why);
}

int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "--probe")) return probe(argv[2]);

    test_euclid();
    test_toggle();
    test_basic_playback();
    test_jump_equivalence();
    test_change_and_back();
    test_join();
    test_length();
    test_delay_timing();
    test_perf_speed();
    test_perf_freeze();
    test_perf_misc();
    test_cycles();
    test_pool();
    test_pitch();
    test_choke_tail();
    test_repeats();
    test_velocity();
    test_step_edit();
    test_prob();
    test_random();
    test_cc();
    test_balance_stress();
    test_save();
    test_keys();
    test_fuzz();
    test_v04();

    /* BUG: parse_value computes cur + atoi(val + 1) in int: signed overflow. */
    run_probe("dplus", "\"d+2147483647\" overflows int in parse_value");
    /* BUG: e1_perf "rotate" adds value to the rotate without a clamp. */
    run_probe("perfrotate", "e1_perf rotate with a huge value overflows");
    /* BUG: fire() id * 31 overflows int64 after ~1.1e8 ticks (~6.6 days at 120 bpm). */
    run_probe("longrun", "a pulse id overflows on a long-running transport");
    /* BUG: a NaN tick passes the "never run time backwards" guard and every
     * comparison against it fails, so each track fires up to 128 slots at once. */
    run_probe("nan", "e1_advance(NaN) bursts notes");

    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
