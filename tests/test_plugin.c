/* Drives the overtake DSP through a fake host: `make test` */
#include "../src/e1/dsp/e1_plugin.c"
#include <ctype.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECKM(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s -- ", __FILE__, __LINE__, #c); printf(__VA_ARGS__); printf("\n"); } } while (0)

/* ------------------------------------------------------------ fake host */

enum { P_MOVE = 0, P_COMP = 1 };
typedef struct { int port; uint8_t b[4]; double tick; } pk_t;
#define MAXPK 400000
static pk_t pk[MAXPK];
static int npk;
static int onmap[2][16][128], negs[2];
static int n_clock, n_start, n_stop, first_clock_before_start;
static int block_move, block_comp;
static double beat = -1.0;
static float host_bpm = 120.0f;
static e1_inst_t *S;              /* the instance under test */

static void reset_log(void)
{
    npk = 0; memset(onmap, 0, sizeof onmap); negs[0] = negs[1] = 0;
    n_clock = n_start = n_stop = 0; first_clock_before_start = 0;
}
static void record(int port, const uint8_t *m)
{
    uint8_t st = m[1];
    if (port == P_COMP && st >= 0xF8) {
        if (st == 0xF8) { if (!n_start) first_clock_before_start = 1; n_clock++; }
        else if (st == 0xFA) n_start++;
        else if (st == 0xFC) n_stop++;
        return;
    }
    int ty = st & 0xF0, ch = st & 0x0F;
    if (ty == 0x90 && m[3]) onmap[port][ch][m[2] & 127]++;
    else if (ty == 0x80 || ty == 0x90) { if (onmap[port][ch][m[2] & 127] <= 0) negs[port]++; else onmap[port][ch][m[2] & 127]--; }
    if (npk < MAXPK) { pk_t *p = &pk[npk++]; p->port = port; memcpy(p->b, m, 4); p->tick = S ? S->e.tick : 0; }
}
static double fake_beat(void) { return beat; }
static float fake_bpm(void) { return host_bpm; }
static int fake_inject(const uint8_t *m, int len) { if (block_move || len != 4) return 0; record(P_MOVE, m); return 4; }
static int fake_ext(const uint8_t *m, int len) { if (block_comp || len != 4) return 0; record(P_COMP, m); return 4; }

static int sounding(int port) { int n = 0; for (int c = 0; c < 16; c++) for (int k = 0; k < 128; k++) n += onmap[port][c][k]; return n; }
static int note_ons(int port, int ch, int note, double from, double to)
{
    int n = 0;
    for (int i = 0; i < npk; i++) {
        const pk_t *p = &pk[i];
        if (p->port != port || (p->b[1] & 0xF0) != 0x90 || !p->b[3]) continue;
        if (ch >= 0 && (p->b[1] & 0x0F) != ch) continue;
        if (note >= 0 && p->b[2] != note) continue;
        if (p->tick >= from && p->tick < to) n++;
    }
    return n;
}
static double first_on(int port, int note, double from)
{
    for (int i = 0; i < npk; i++) {
        const pk_t *p = &pk[i];
        if (p->port == port && (p->b[1] & 0xF0) == 0x90 && p->b[3] && (note < 0 || p->b[2] == note) && p->tick >= from) return p->tick;
    }
    return -1;
}

static plugin_api_v2_t *api;
static host_api_v1_t host;
static int16_t audio[256];

static void render(void *inst, int n) { for (int i = 0; i < n; i++) api->render_block(inst, audio, 128); }
/* Follow Move: advance the beat by one block of 128 frames at `bpm`. */
static void follow(void *inst, int n, double bpm)
{
    for (int i = 0; i < n; i++) {
        if (beat < 0) beat = 0;
        api->render_block(inst, audio, 128);
        beat += 128.0 / 44100.0 * bpm / 60.0;
    }
}
static void set(void *inst, const char *k, const char *v) { api->set_param(inst, k, v); }
static void setf(void *inst, const char *v, const char *fmt, int n) { char k[64]; snprintf(k, sizeof k, fmt, n); api->set_param(inst, k, v); }
static const char *get(void *inst, const char *k) { static char b[65536]; if (api->get_param(inst, k, b, sizeof b) < 0) return "(err)"; return b; }

static void *fresh_instance(void)
{
    api = move_plugin_init_v2(&host);
    void *inst = api->create_instance("/tmp", NULL);
    S = (e1_inst_t *)inst;
    beat = -1.0; block_move = block_comp = 0;
    reset_log();
    return inst;
}
/* Mute every track but `keep` (-1: all muted). */
static void only(void *inst, int keep) { for (int i = 1; i <= 16; i++) setf(inst, i == keep ? "Off" : "On", "t%d:mute", i); }

/* ---------------------------------------------------------- JSON check */

static const char *jp;
static int j_ws(void) { while (*jp == ' ' || *jp == '\n' || *jp == '\t' || *jp == '\r') jp++; return 1; }
static int j_value(int depth);
static int j_string(void)
{
    if (*jp != '"') return 0;
    jp++;
    while (*jp != '"') {
        unsigned char ch = (unsigned char)*jp;
        if (!ch || ch < 0x20) return 0;
        if (ch == '\\') {
            jp++;
            if (strchr("\"\\/bfnrt", *jp) && *jp) jp++;
            else if (*jp == 'u') { for (int i = 1; i <= 4; i++) if (!isxdigit((unsigned char)jp[i])) return 0; jp += 5; }
            else return 0;
        } else jp++;
    }
    jp++;
    return 1;
}
static int j_number(void)
{
    const char *s = jp;
    if (*jp == '-') jp++;
    if (!isdigit((unsigned char)*jp)) return 0;
    if (*jp == '0') jp++; else while (isdigit((unsigned char)*jp)) jp++;
    if (*jp == '.') { jp++; if (!isdigit((unsigned char)*jp)) return 0; while (isdigit((unsigned char)*jp)) jp++; }
    if (*jp == 'e' || *jp == 'E') { jp++; if (*jp == '+' || *jp == '-') jp++; if (!isdigit((unsigned char)*jp)) return 0; while (isdigit((unsigned char)*jp)) jp++; }
    return jp > s;
}
static int j_value(int depth)
{
    if (depth > 32) return 0;
    j_ws();
    if (*jp == '{') {
        jp++; j_ws();
        if (*jp == '}') { jp++; return 1; }
        for (;;) {
            j_ws(); if (!j_string()) return 0;
            j_ws(); if (*jp++ != ':') return 0;
            if (!j_value(depth + 1)) return 0;
            j_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == '}') { jp++; return 1; }
            return 0;
        }
    }
    if (*jp == '[') {
        jp++; j_ws();
        if (*jp == ']') { jp++; return 1; }
        for (;;) {
            if (!j_value(depth + 1)) return 0;
            j_ws();
            if (*jp == ',') { jp++; continue; }
            if (*jp == ']') { jp++; return 1; }
            return 0;
        }
    }
    if (*jp == '"') return j_string();
    if (!strncmp(jp, "true", 4)) { jp += 4; return 1; }
    if (!strncmp(jp, "false", 5)) { jp += 5; return 1; }
    if (!strncmp(jp, "null", 4)) { jp += 4; return 1; }
    return j_number();
}
static int json_ok(const char *s) { jp = s; if (!j_value(0)) return 0; j_ws(); return *jp == 0; }
static int ui_ok(void *inst, int *len)
{
    static char b[65536];
    int n = api->get_param(inst, "ui", b, sizeof b);
    if (len) *len = n;
    if (n <= 0 || (int)strlen(b) != n) return 0;
    if (!json_ok(b)) { printf("  bad JSON: %.300s\n", b); return 0; }
    return 1;
}

/* ------------------------------------------------------------- tests */

static void test_follow(void)
{
    void *inst = fresh_instance();
    only(inst, 1);
    render(inst, 100);                                   /* Move stopped */
    CHECK(npk == 0 && !S->e.running);
    follow(inst, 689 * 2, 120);                          /* two bars */
    CHECK(S->e.running);
    CHECK(note_ons(P_MOVE, 0, 36, 0, 1e9) == 8 && note_ons(P_COMP, -1, -1, 0, 1e9) == 0);
    CHECK(npk > 0 && pk[0].b[0] == 0x29 && pk[0].b[1] == 0x90 && pk[0].b[2] == 36);
    double t1 = first_on(P_MOVE, 36, 1);
    CHECK(t1 >= 96 && t1 < 97);                          /* on the beat, within a block */
    beat = -1.0; render(inst, 20);                       /* Move stops */
    CHECK(!S->e.running && sounding(P_MOVE) == 0 && negs[P_MOVE] == 0);

    /* joining mid-bar: no replay, the next beat sounds */
    reset_log();
    beat = 1.5;
    follow(inst, 300, 120);
    double t = first_on(P_MOVE, 36, 0);
    CHECKM(t >= 192 && t < 193, "first note after joining at beat 1.5: tick %g", t);
    CHECK(note_ons(P_MOVE, 0, 36, 0, 192) == 0);

    /* Move loops back to bar 1 without stopping */
    reset_log(); beat = -1; render(inst, 2);
    follow(inst, 689 * 2, 120);                          /* beats 0..8 */
    beat = 0.0;                                          /* jump back */
    int before = note_ons(P_MOVE, 0, 36, -1e9, 1e9);
    follow(inst, 689 * 2, 120);                          /* beats 0..8 again */
    int after = note_ons(P_MOVE, 0, 36, -1e9, 1e9) - before;
    /* BUG: e1_advance never runs time backwards, and run_follow only rejoins
     * on a stop (beat < 0), so after Move's beat jumps back the engine stalls
     * until the beat passes the old position: a whole loop of silence. */
    CHECKM(after >= 7, "%d notes in the two bars after the beat jumped back", after);
    beat = -1.0; render(inst, 20);
    CHECK(sounding(P_MOVE) == 0);
    api->destroy_instance(inst);
}

static void test_midi_clock_fallback(void)
{
    host.get_beat_position = NULL;
    void *inst = fresh_instance();
    only(inst, 1);
    uint8_t fa = 0xFA, f8 = 0xF8, fc = 0xFC;
    api->on_midi(inst, &fa, 1, 0);
    double clk = 0, tm = 0, spc = 60.0 / 120.0 / 24.0;  /* seconds per clock */
    for (int i = 0; i < 689 * 2; i++) {
        while (clk <= tm) { api->on_midi(inst, &f8, 1, 0); clk += spc; }
        api->render_block(inst, audio, 128);
        tm += 128.0 / 44100.0;
    }
    CHECK(note_ons(P_MOVE, 0, 36, 0, 1e9) == 8);
    api->on_midi(inst, &fc, 1, 0);
    render(inst, 20);
    CHECK(!S->e.running && sounding(P_MOVE) == 0);
    api->destroy_instance(inst);
    host.get_beat_position = fake_beat;

    /* "Computer" clock: follow the computer's MIDI clock */
    inst = fresh_instance();
    only(inst, 1);
    set(inst, "clock", "Computer");
    CHECK(strcmp(get(inst, "clock"), "Computer") == 0);
    api->on_midi(inst, &fa, 1, 2);
    clk = 0; tm = 0;
    for (int i = 0; i < 689; i++) {
        while (clk <= tm) { api->on_midi(inst, &f8, 1, 2); clk += spc; }
        api->render_block(inst, audio, 128);
        tm += 128.0 / 44100.0;
    }
    /* BUG: CLOCK_COMPUTER is offered but run_follow() reads Move's beat
     * position whenever the host has one, so the computer's clock is ignored. */
    CHECKM(S->e.running && note_ons(P_MOVE, 0, 36, 0, 1e9) >= 3, "clock=Computer with external MIDI clock: running %d", S->e.running);
    api->destroy_instance(inst);
}

static void test_internal(void)
{
    void *inst = fresh_instance();
    only(inst, 1);
    set(inst, "dest", "Computer");
    set(inst, "clock", "Internal");
    CHECK(strcmp(get(inst, "clock"), "Internal") == 0);
    set(inst, "play", "1");
    render(inst, 689);                                   /* ~1 bar at 120 */
    CHECK(n_start == 1 && !first_clock_before_start);
    CHECK(n_clock >= 94 && n_clock <= 98);
    CHECK(note_ons(P_COMP, 0, 36, 0, 1e9) == 4 && note_ons(P_MOVE, -1, -1, 0, 1e9) == 0);
    set(inst, "play", "0");
    render(inst, 4);
    CHECK(n_stop == 1 && sounding(P_COMP) == 0 && !S->e.running);
    /* "toggle" */
    set(inst, "play", "toggle"); render(inst, 2); CHECK(S->e.running);
    set(inst, "play", "toggle"); render(inst, 2); CHECK(!S->e.running);
    /* tempo */
    set(inst, "bpm", "60"); reset_log();
    set(inst, "play", "1"); render(inst, 689);
    CHECK(n_clock >= 46 && n_clock <= 50 && note_ons(P_COMP, 0, 36, 0, 1e9) == 2);
    set(inst, "play", "0"); render(inst, 2);
    set(inst, "bpm", "d+10"); CHECK(strcmp(get(inst, "bpm"), "70.0") == 0);
    set(inst, "bpm", "1000"); CHECK(strcmp(get(inst, "bpm"), "300.0") == 0);
    set(inst, "bpm", "120");
    /* clock out off: notes but no clock */
    set(inst, "clockout", "0"); reset_log();
    set(inst, "play", "1"); render(inst, 300); set(inst, "play", "0"); render(inst, 2);
    CHECK(n_clock == 0 && n_start == 0 && n_stop == 0 && note_ons(P_COMP, 0, 36, 0, 1e9) > 0);
    set(inst, "clockout", "On");

    /* the computer must see Stop for every Start, whatever the routing did meanwhile */
    reset_log();
    set(inst, "play", "1"); render(inst, 100);
    set(inst, "dest", "Move");
    set(inst, "play", "0"); render(inst, 4);
    /* BUG: run_internal sends Stop only if computer_wanted() still holds, so
     * moving the destination off the computer while playing leaves the
     * computer's sequencer running (no 0xFC). Same in reverse: */
    CHECKM(n_start == 1 && n_stop == 1, "start %d stop %d", n_start, n_stop);
    reset_log();
    set(inst, "play", "1"); render(inst, 100);           /* dest Move: no clock */
    set(inst, "dest", "Computer"); render(inst, 100);
    /* BUG: ...clock starts flowing mid-song with no Start (0xFA) first. */
    CHECKM(n_clock == 0 || n_start == 1, "%d clocks, %d starts", n_clock, n_start);
    set(inst, "play", "0"); render(inst, 4);

    /* bpm must stay a number */
    set(inst, "bpm", "nan");
    /* BUG: atof("nan") passes both clamps (comparisons with NaN are false);
     * the tick becomes NaN, every track fires a burst of slots per block and
     * "ui" prints "bpm":nan (invalid JSON). Plugin "bpm" setter. */
    CHECKM(S->bpm >= 20.0f && S->bpm <= 300.0f, "bpm %f", (double)S->bpm);
    set(inst, "bpm", "120");
    api->destroy_instance(inst);
}

static void test_routing(void)
{
    void *inst = fresh_instance();
    set(inst, "clock", "Internal");
    only(inst, -1);
    set(inst, "t1:mute", "Off"); set(inst, "t9:mute", "Off");    /* ch1 drums, ch2 bass */
    static const char *dests[3] = { "Move", "Computer", "Both" };
    for (int d = 0; d < 3; d++) {
        reset_log();
        set(inst, "dest", dests[d]);
        CHECK(strcmp(get(inst, "dest"), dests[d]) == 0);
        set(inst, "play", "1"); render(inst, 689); set(inst, "play", "0"); render(inst, 4);
        int m = note_ons(P_MOVE, -1, -1, 0, 1e9), c = note_ons(P_COMP, -1, -1, 0, 1e9);
        CHECKM((d == 1 ? m == 0 : m == 9) && (d == 0 ? c == 0 : c == 9), "dest %s: move %d computer %d", dests[d], m, c);
        CHECK(sounding(P_MOVE) == 0 && sounding(P_COMP) == 0);
    }
    /* per-track output overrides the destination */
    set(inst, "dest", "Move");
    set(inst, "t9:output", "Computer");
    reset_log();
    set(inst, "play", "1"); render(inst, 689); set(inst, "play", "0"); render(inst, 4);
    CHECK(note_ons(P_MOVE, 0, -1, 0, 1e9) == 4 && note_ons(P_MOVE, 1, -1, 0, 1e9) == 0 && note_ons(P_COMP, 1, -1, 0, 1e9) == 5);
    CHECK(n_start == 1 && n_stop == 1);                  /* one track on the computer wants clock */
    /* changing routing mid-note closes the note where it was opened */
    set(inst, "t9:output", "Global"); set(inst, "t9:sustain", "100"); set(inst, "t9:channel", "3");
    reset_log();
    set(inst, "play", "1");
    for (int i = 0; i < 600; i++) {
        if (i == 100) set(inst, "t9:output", "Both");
        if (i == 200) set(inst, "dest", "Computer");
        if (i == 300) set(inst, "t9:channel", "7");
        if (i == 400) set(inst, "t9:type", "CC");
        if (i == 500) set(inst, "t9:type", "Note");
        render(inst, 1);
    }
    set(inst, "play", "0"); render(inst, 4);
    CHECK(sounding(P_MOVE) == 0 && sounding(P_COMP) == 0 && negs[P_MOVE] == 0 && negs[P_COMP] == 0);
    api->destroy_instance(inst);
}

static void test_backpressure(void)
{
    void *inst = fresh_instance();
    set(inst, "clock", "Internal");
    only(inst, 1);
    /* short stall: nothing lost, everything arrives in order */
    set(inst, "play", "1");
    render(inst, 100);
    block_move = 1; render(inst, 200); block_move = 0;
    render(inst, 400);
    set(inst, "play", "0"); render(inst, 50);
    CHECK(S->q_move.dropped == 0 && sounding(P_MOVE) == 0 && negs[P_MOVE] == 0 && note_ons(P_MOVE, 0, 36, -1, 1e9) >= 5);

    /* a long stall with every track busy */
    api->destroy_instance(inst);
    inst = fresh_instance();
    set(inst, "clock", "Internal");
    for (int t = 1; t <= 16; t++) {
        setf(inst, "Note", "t%d:type", t); setf(inst, "Off", "t%d:mute", t); setf(inst, "16", "t%d:pulses", t);
        setf(inst, "60,62,64,65", "t%d:pool", t);
    }
    set(inst, "play", "1");
    block_move = 1; render(inst, 1500); block_move = 0;
    render(inst, 600);
    set(inst, "play", "0");
    render(inst, 3000);
    printf("long stall: %u packets dropped on the Move queue\n", (unsigned)S->q_move.dropped);
    /* BUG: q_push drops whatever does not fit, note-offs included, so a long
     * stall (or a dense set at a high tempo) leaves notes stuck on Move. */
    CHECKM(sounding(P_MOVE) == 0, "%d notes left on after the queue overflowed", sounding(P_MOVE));
    api->destroy_instance(inst);

    /* the same without any stall, at 300 bpm with 1/32 steps */
    inst = fresh_instance();
    set(inst, "clock", "Internal"); set(inst, "bpm", "300");
    for (int t = 1; t <= 16; t++) {
        setf(inst, "Note", "t%d:type", t); setf(inst, "Off", "t%d:mute", t); setf(inst, "32", "t%d:pulses", t);
        setf(inst, "32", "t%d:steps", t); setf(inst, "1/32", "t%d:division", t); setf(inst, "60,62,64,65,67,69,71,72", "t%d:pool", t);
    }
    set(inst, "play", "1"); render(inst, 2000); set(inst, "play", "0"); render(inst, 6000);
    printf("dense set at 300 bpm: %u packets dropped on the Move queue\n", (unsigned)S->q_move.dropped);
    CHECKM(sounding(P_MOVE) == 0, "%d notes left on", sounding(P_MOVE));
    api->destroy_instance(inst);
}

static void test_commit(void)
{
    static const char *qn[6] = { "1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars" };
    static const int qt[6] = { 96, 192, 384, 768, 1536, 3072 };
    void *inst = fresh_instance();
    set(inst, "clock", "Internal");
    only(inst, 1);
    for (int q = 0; q < 6; q++) {
        reset_log();
        set(inst, "quant", qn[q]);
        CHECK(strcmp(get(inst, "quant"), qn[q]) == 0);
        set(inst, "t1:pool", "36");
        set(inst, "play", "1");
        render(inst, 250 + q * 37);                      /* somewhere off the line */
        double c0 = S->e.tick;
        double line = (floor(c0 / qt[q]) + 1.0) * qt[q];
        set(inst, "stage:1", "pool=50\npulses=16\nmute=Off\n");
        set(inst, "commit", "bar");
        CHECK(S->commit == 1);
        double applied = -1, prev = c0;
        for (int i = 0; i < 20000 && applied < 0; i++) {
            render(inst, 1);
            if (S->stage_mask == 0) applied = S->e.tick; else prev = S->e.tick;
        }
        CHECKM(prev < line && applied >= line, "quant %s: line %g, applied between %g and %g", qn[q], line, prev, applied);
        CHECK(S->commit == 0 && strcmp(get(inst, "t1:pulses"), "16") == 0);
        render(inst, 10);
        double f = first_on(P_MOVE, 50, 0);
        CHECKM(f >= line && f < line + 1, "quant %s: first new note at %g, line %g", qn[q], f, line);
        CHECK(note_ons(P_MOVE, 0, 36, line, 1e9) == 0 && note_ons(P_MOVE, 0, 50, 0, line) == 0);
        set(inst, "play", "0"); render(inst, 2);
        CHECK(sounding(P_MOVE) == 0);
    }
    set(inst, "quant", "d+5"); CHECK(strcmp(get(inst, "quant"), "8 bars") == 0);
    set(inst, "quant", "d-9"); CHECK(strcmp(get(inst, "quant"), "1/4") == 0);
    set(inst, "quant", "bogus"); CHECK(strcmp(get(inst, "quant"), "1/4") == 0);
    set(inst, "quant", "1 bar");

    /* now: at once while playing */
    set(inst, "play", "1"); render(inst, 100);
    set(inst, "stage:1", "steps=7\n"); set(inst, "commit", "now");
    render(inst, 1);
    CHECK(strcmp(get(inst, "t1:steps"), "7") == 0 && S->stage_mask == 0);
    /* cancel */
    set(inst, "stage:1", "steps=9\n"); set(inst, "commit", "cancel");
    render(inst, 1500);
    CHECK(strcmp(get(inst, "t1:steps"), "7") == 0 && S->stage_mask == 0);
    /* stopped: bar applies at once */
    set(inst, "play", "0"); render(inst, 2);
    set(inst, "stage:2", "steps=5\n"); set(inst, "commit", "bar");
    render(inst, 1);
    CHECK(strcmp(get(inst, "t2:steps"), "5") == 0);
    /* a staged track replaces only that track, all of it */
    CHECK(strcmp(get(inst, "t1:steps"), "7") == 0);
    CHECK(strcmp(get(inst, "t2:pulses"), "4") == 0 && strcmp(get(inst, "t2:name"), "SNARE") == 0);
    api->destroy_instance(inst);
}

static void test_temp(void)
{
    void *inst = fresh_instance();
    static e1_track_t before[E1_TRACKS];
    for (int i = 0; i < E1_TRACKS; i++) memcpy(&before[i], &S->e.t[i], E1_TRACK_STORED_BYTES);
    set(inst, "temp", "1");
    set(inst, "t1:pulses", "1"); set(inst, "t2:division", "1/8T"); set(inst, "t3:pool", "1,2,3");
    set(inst, "t4:name", "TEMPNAME"); set(inst, "t5:edit", "6"); set(inst, "t5:velocity", "12"); set(inst, "t5:edit", "0");
    set(inst, "t12:cc4", "99"); set(inst, "t12:r.cc4", "-40"); set(inst, "t13:toggle", "3"); set(inst, "t14:c7.pitch", "5");
    set(inst, "t16:r.steps", "50"); set(inst, "t9:addmask", "ff");
    set(inst, "temp", "1");                              /* a second press changes nothing */
    set(inst, "temp", "0");
    int same = 1;
    for (int i = 0; i < E1_TRACKS; i++) if (memcmp(&before[i], &S->e.t[i], E1_TRACK_STORED_BYTES) != 0) { same = 0; printf("  track %d differs\n", i + 1); }
    CHECK(same);
    CHECK(strcmp(get(inst, "t1:pulses"), "4") == 0 && strcmp(get(inst, "t2:division"), "1/16") == 0);
    set(inst, "temp", "1"); set(inst, "t1:pulses", "2"); set(inst, "temp", "keep");
    CHECK(strcmp(get(inst, "t1:pulses"), "2") == 0 && !S->temp_on);
    set(inst, "temp", "0");
    CHECK(strcmp(get(inst, "t1:pulses"), "2") == 0);
    /* mutes changed under Temp come back, kept ones stay */
    set(inst, "t1:mute", "Off"); set(inst, "t2:mute", "On");
    set(inst, "temp", "1");
    set(inst, "t1:mute", "On"); set(inst, "t2:mute", "Off"); set(inst, "t3:mute", "On");
    CHECK(S->e.t[0].v[E1_P_MUTE] == 1 && S->e.t[1].v[E1_P_MUTE] == 0);
    set(inst, "temp", "0");
    CHECK(S->e.t[0].v[E1_P_MUTE] == 0 && S->e.t[1].v[E1_P_MUTE] == 1 && S->e.t[2].v[E1_P_MUTE] == 0);
    api->destroy_instance(inst);
}

static void test_perf_aud(void)
{
    void *inst = fresh_instance();
    set(inst, "perf:ratchet", "3 4");
    CHECK(S->e.t[0].p_ratchet == 4 && S->e.t[1].p_ratchet == 4 && S->e.t[2].p_ratchet == 0);
    char b[8192];
    int n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"pf\":3,"));
    set(inst, "perf:solo", "2 1");
    CHECK(S->e.t[0].p_solo_mute && !S->e.t[1].p_solo_mute);
    set(inst, "perf:speed", "1 -1");  CHECK(S->e.t[0].p_speed == -1);
    set(inst, "perf:freeze", "ffff 4"); CHECK(S->e.t[15].p_freeze == 4);
    set(inst, "perf:clear", "ffff 0");
    CHECK(S->e.t[0].p_ratchet == 0 && !S->e.t[0].p_solo_mute && S->e.t[0].p_speed == 0 && S->e.t[15].p_freeze == 0);
    set(inst, "perf:rotate", "1 2"); CHECK(S->e.t[0].v[E1_P_ROTATE] == 2);
    set(inst, "perf:fill", "zz"); set(inst, "perf:", "1 1"); set(inst, "perf:nope", "1 1");

    /* audition */
    reset_log();
    set(inst, "aud:3", "60 100"); render(inst, 1);
    CHECK(npk == 1 && pk[0].port == P_MOVE && pk[0].b[0] == 0x29 && pk[0].b[1] == 0x90 && pk[0].b[2] == 60 && pk[0].b[3] == 100);
    set(inst, "aud:3", "60 0"); render(inst, 1);
    CHECK(npk == 2 && pk[1].b[1] == 0x80 && pk[1].b[2] == 60 && sounding(P_MOVE) == 0);
    set(inst, "aud:10", "48 200"); render(inst, 1);     /* track 10: channel 3 */
    CHECK(npk == 3 && pk[2].b[1] == 0x92 && pk[2].b[3] == 127);
    set(inst, "aud:10", "48 0");
    set(inst, "t10:output", "Computer");
    set(inst, "aud:10", "49 90"); set(inst, "aud:10", "49 0"); render(inst, 1);
    CHECK(note_ons(P_COMP, 2, 49, -1, 1e9) == 1 && sounding(P_COMP) == 0);
    int before = npk;
    set(inst, "aud:0", "60 100"); set(inst, "aud:17", "60 100"); set(inst, "aud:1", "60"); set(inst, "aud:1", "");
    render(inst, 1);
    CHECK(npk == before);

    /* hit: one hit of a track by hand, while stopped; repeats and retrigger marks in the snapshot */
    reset_log();
    set(inst, "t9:mute", "On");
    set(inst, "hit:9", "1"); render(inst, 1);
    CHECK(npk == 1 && pk[0].b[1] == 0x91 && pk[0].b[2] == 36 && sounding(P_MOVE) == 1);
    set(inst, "hit:9", "0"); render(inst, 1);
    CHECK(npk == 2 && sounding(P_MOVE) == 0);
    set(inst, "hit:0", "1"); set(inst, "hit:17", "1"); render(inst, 1);
    CHECK(npk == 2);
    set(inst, "focus", "2"); set(inst, "t2:retrigtoggle", "4");
    n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"rt\":\"10\"") && strstr(b, "\"rp\":0,"));
    set(inst, "clock", "Internal"); only(inst, 1);
    set(inst, "t1:repeats", "49"); set(inst, "t1:pulses", "1"); set(inst, "t1:tail", "Tail"); set(inst, "t1:rotate", "0");
    set(inst, "play", "1"); render(inst, 40);
    n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"rp\":1,"));
    set(inst, "perf:stoprepeats", "1 0");
    n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"rp\":0,"));
    set(inst, "perf:randstart", "1 1"); CHECK(S->e.t[0].toff != 0);
    set(inst, "play", "0"); render(inst, 10);
    api->destroy_instance(inst);
}

static void test_dump_load(void)
{
    void *inst = fresh_instance();
    static char b[131072];
    set(inst, "t3:steps", "12"); set(inst, "t3:edit", "3"); set(inst, "t3:pitch", "4"); set(inst, "t3:edit", "0");
    set(inst, "t3:pool", "60,61,62"); set(inst, "t3:cc5", "77"); set(inst, "t3:name", "Copy me");
    int n = api->get_param(inst, "dump:3", b, sizeof b);
    CHECK(n > 0 && strstr(b, "steps=12") && strstr(b, "c1.pitch=4"));
    set(inst, "t4:edit", "1");                           /* loading ignores edit mode */
    set(inst, "load:4", b);
    CHECK(memcmp(&S->e.t[2], &S->e.t[3], E1_TRACK_STORED_BYTES) == 0);
    CHECK(strcmp(get(inst, "t4:steps"), "12") == 0);
    set(inst, "t4:edit", "0");

    /* a dump that does not fit must say so, not hand back half a track */
    n = api->get_param(inst, "dump:3", b, 64);
    /* BUG: e1_dump_track truncates silently and returns len-1; the plugin
     * passes that on, so a short host buffer yields a truncated save. */
    CHECKM(n == -1, "dump:3 into 64 bytes returned %d", n);
    CHECK(api->get_param(inst, "dump:0", b, sizeof b) == -1 && api->get_param(inst, "dump:17", b, sizeof b) == -1);

    /* globals round trip */
    set(inst, "clock", "Internal"); set(inst, "bpm", "97.5"); set(inst, "clockout", "0");
    set(inst, "dest", "Both"); set(inst, "seed", "4321"); set(inst, "quant", "2 bars");
    n = api->get_param(inst, "dump:g", b, sizeof b);
    CHECK(n > 0 && strstr(b, "clock=Internal") && strstr(b, "bpm=97.5") && strstr(b, "dest=Both") && strstr(b, "seed=4321") &&
          strstr(b, "clockout=0") && strstr(b, "quant=2 bars"));
    static char g[131072]; memcpy(g, b, sizeof g);
    set(inst, "clock", "Move"); set(inst, "bpm", "120"); set(inst, "clockout", "1");
    set(inst, "dest", "Move"); set(inst, "seed", "1"); set(inst, "quant", "1/4");
    set(inst, "load:g", g);
    CHECK(S->clock == CLOCK_INTERNAL && fabsf(S->bpm - 97.5f) < 0.01f && S->clock_out == 0 &&
          S->e.dest == E1_DEST_BOTH && S->e.seed == 4321 && S->quant == 3);
    CHECK(api->get_param(inst, "dump:g", b, 20) == -1);
    /* load:g takes only the global keys */
    set(inst, "load:g", "focus=5\nt1:steps=3\npanic=1\n");
    CHECK(S->focus == 0 && strcmp(get(inst, "t1:steps"), "16") == 0);
    /* song settings: saved with the globals, reachable from the web page, and
     * a set saved before they existed comes back as C minor, no chord track */
    set(inst, "songroot", "E"); set(inst, "songscale", "Lydian"); set(inst, "w:chordtrack", "10");
    n = api->get_param(inst, "dump:g", b, sizeof b);
    CHECK(n > 0 && strstr(b, "songroot=E\n") && strstr(b, "songscale=Lydian\n") && strstr(b, "chordtrack=10\n"));
    memcpy(g, b, sizeof g);
    set(inst, "load:g", "");
    CHECK(S->e.song_root == 0 && S->e.song_scale == 2 && S->e.chord_track == 0);
    set(inst, "load:g", g);
    CHECK(S->e.song_root == 4 && S->e.song_scale == 9 && S->e.chord_track == 10);
    n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"sr\":4,\"ss\":9,\"ct\":10,"));
    CHECK(strstr(get(inst, "state"), "\"song_root\":\"4\"") != NULL);
    api->destroy_instance(inst);
}

static void test_ui_json(void)
{
    void *inst = fresh_instance();
    int len = 0;
    CHECK(ui_ok(inst, &len));
    set(inst, "focus", "3");
    char b[8192];
    int n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && strstr(b, "\"f\":3") && b[n - 1] == '}');
    CHECK(api->get_param(inst, "ui", b, 40) == -1);
    CHECK(strcmp(get(inst, "module_id"), "e1") == 0);
    CHECK(strstr(get(inst, "pkeys"), "type,steps,pulses") != NULL);

    /* heavy focus track, odd names */
    for (int t = 1; t <= 16; t++) setf(inst, "Q\"u\\o=t\ne!MMMM", "t%d:name", t);
    CHECK(ui_ok(inst, &len));
    set(inst, "t3:pool", "120,121,122,123,124,125,126,127");
    for (int l = 1; l <= 16; l++) { char k[32]; snprintf(k, sizeof k, "t3:cc%d", l); set(inst, k, "127"); snprintf(k, sizeof k, "t3:r.cc%d", l); set(inst, k, "-100"); snprintf(k, sizeof k, "t3:ccn%d", l); set(inst, k, "127"); }
    for (int p = 0; p < E1_NP; p++) { char k[32]; snprintf(k, sizeof k, "t3:r.%s", e1_param_key(p)); set(inst, k, "-100"); }
    set(inst, "t3:edit", "8000"); set(inst, "t3:pitch", "-36"); set(inst, "t3:steps", "64"); set(inst, "t3:pulses", "61");
    set(inst, "perf:ratchet", "ffff 4"); set(inst, "perf:solo", "1 1");
    CHECK(ui_ok(inst, &len));
    set(inst, "clock", "Internal"); set(inst, "play", "1"); render(inst, 300);
    CHECK(ui_ok(inst, &len));
    set(inst, "t3:edit", "0");
    CHECK(ui_ok(inst, &len));
    printf("ui JSON, heavy state: %d bytes\n", len);

    /* worst case: every field at its widest */
    for (int i = 0; i < E1_TRACKS; i++) { e1_track_t *t = &S->e.t[i]; t->cur_step = 63; snprintf(t->name, sizeof t->name, "MMMMMMMMMMMM"); t->flash = 1; t->v[E1_P_MUTE] = 1; t->v[E1_P_TYPE] = 1; t->p_solo_mute = 1; }
    e1_track_t *f = &S->e.t[S->focus];
    S->q_move.dropped = 2000000000u; S->q_comp.dropped = 2000000000u; S->bpm = 300.0f;
    f->edit_mask = 0x8000;
    for (int c = 0; c < E1_CYCLES; c++) f->cyc[c][E1_P_VELOCITY] = 0;
    n = api->get_param(inst, "ui", b, sizeof b);
    CHECK(n > 0 && json_ok(b));
    printf("ui JSON, worst case: %d bytes\n", n);

    /* control characters in a name */
    set(inst, "t3:name", "tab\there");
    /* BUG: names keep control characters (see test_engine), so "ui" is not valid JSON. */
    CHECK(ui_ok(inst, NULL));
    set(inst, "t3:name", "ok");
    set(inst, "bpm", "nan");
    /* BUG: see test_internal: "bpm":nan */
    CHECK(ui_ok(inst, NULL));
    api->destroy_instance(inst);
}

static void test_garbage(void)
{
    void *inst = fresh_instance();
    static const char *keys[] = { "t1:", "t0:steps", "t17:steps", "t1:steps", "t99999999999:x", "t1x", "load:", "load:0", "load:99", "load:g",
        "stage:", "stage:0", "stage:17", "stage:3", "commit", "temp", "perf:", "perf:solo", "perf:rotate", "aud:", "aud:5", "focus", "clock",
        "quant", "bpm", "play", "clockout", "panic", "dest", "seed", "", "dump:1", "ui" };
    static const char *vals[] = { "", "1", "-1", "d+", "d-99999999999", "zz", "ffffffff 2147483647", "1 2 3", "\n\n=\n", "a=b\nc=d",
        "steps=999\npulses=-5\nc99.steps=1\nr.cc99=5\n", "Internal", "toggle", "now", "bar", "keep", "0" };
    char b[256];
    unsigned r = 5;
    set(inst, "clock", "Internal"); set(inst, "play", "1");
    for (int i = 0; i < 20000; i++) {
        r = r * 1103515245u + 12345u;
        const char *k = keys[(r >> 8) % (sizeof keys / sizeof keys[0])];
        const char *v = vals[(r >> 16) % (sizeof vals / sizeof vals[0])];
        if (!strcmp(k, "bpm") && !strcmp(v, "nan")) continue;
        api->set_param(inst, k, v);
        api->get_param(inst, k, b, (int)((r >> 4) % sizeof b));
        if (i % 20 == 0) render(inst, 1);
    }
    api->set_param(inst, NULL, "1"); api->set_param(inst, "x", NULL); api->set_param(NULL, "x", "1");
    CHECK(api->get_param(inst, NULL, b, sizeof b) == -1 && api->get_param(inst, "ui", NULL, 10) == -1 && api->get_param(NULL, "ui", b, 10) == -1);
    api->render_block(NULL, audio, 128); api->render_block(inst, NULL, 128); api->on_midi(inst, NULL, 3, 0); api->on_midi(NULL, (const uint8_t *)b, 1, 0);
    set(inst, "play", "0"); set(inst, "clock", "Internal"); render(inst, 2000);
    CHECK(ui_ok(inst, NULL));
    api->destroy_instance(inst);
    api->destroy_instance(NULL);
}

int main(void)
{
    host.get_beat_position = fake_beat; host.get_bpm = fake_bpm;
    host.midi_inject_to_move = fake_inject; host.midi_send_external = fake_ext;

    test_follow();
    test_midi_clock_fallback();
    test_internal();
    test_routing();
    test_backpressure();
    test_commit();
    test_temp();
    test_perf_aud();
    test_dump_load();
    test_ui_json();
    test_garbage();

    printf("%d/%d plugin checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
