/* Host unit tests for the E-1 engine: `make test` */
#include "../src/engine/e1_engine.h"
#include <stdio.h>
#include <string.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

typedef struct { int n; int ports[4096]; uint8_t st[4096], d1[4096], d2[4096]; double at[4096]; } cap_t;
static double g_now;
static void emit(void *u, int ports, uint8_t st, uint8_t d1, uint8_t d2) {
    cap_t *c = u; if (c->n >= 4096) return;
    c->ports[c->n] = ports; c->st[c->n] = st; c->d1[c->n] = d1; c->d2[c->n] = d2; c->at[c->n] = g_now; c->n++;
}
static int count_on(cap_t *c, int ch) { int n = 0; for (int i = 0; i < c->n; i++) if (c->st[i] == (0x90 | ch) && c->d2[i]) n++; return n; }
static void run(e1_engine_t *e, cap_t *c, double from, double to, double stepsz) {
    for (g_now = from; g_now <= to; g_now += stepsz) e1_advance(e, g_now, emit, c);
}
static void solo(e1_engine_t *e, int ti) { for (int i = 0; i < E1_TRACKS; i++) e->t[i].muted = (i != ti); }

static void test_euclid(void) {
    e1_track_t t; e1_track_defaults(&t, 0);
    t.steps = 8; t.pulses = 3;
    /* E(3,8) = x..x..x. */
    CHECK(e1_hit_mask(&t, 0) == 0x49);
    t.rotate = 1;
    CHECK(e1_hit_mask(&t, 0) == 0x92);
    t.rotate = 0; t.pulses = 0; CHECK(e1_hit_mask(&t, 0) == 0);
    t.pulses = 8; CHECK(e1_hit_mask(&t, 0) == 0xFF);
    t.steps = 16; t.pulses = 4; CHECK(e1_hit_mask(&t, 0) == 0x1111);
    t.fill = 4; CHECK(e1_hit_mask(&t, 1) == 0x5555);
    t.steps = 64; t.pulses = 64; CHECK(e1_hit_mask(&t, 0) == ~0ULL);
}

static void test_toggle(void) {
    e1_track_t t; e1_track_defaults(&t, 0);
    t.steps = 8; t.pulses = 3; t.rotate = 2;
    uint64_t m = e1_hit_mask(&t, 0);
    e1_toggle_step(&t, 1);
    CHECK(e1_hit_mask(&t, 0) == (m ^ 2));
    e1_toggle_step(&t, 1);
    CHECK(e1_hit_mask(&t, 0) == m && t.add_mask == 0 && t.del_mask == 0);
    int first = __builtin_ctzll(m);
    e1_toggle_step(&t, first);
    CHECK(!e1_hit(&t, first, 0));
    /* edits rotate with the pattern */
    t.rotate = 3; CHECK(!e1_hit(&t, (first + 1) % 8, 0));
}

static void test_playback(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 0);
    e.t[0].steps = 16; e.t[0].pulses = 4; e.t[0].rotate = 0;
    cap_t c = {0};
    e1_start(&e, 0);
    run(&e, &c, 0, 384 * 4 - 1, 16);   /* four bars at 1/16 */
    CHECK(count_on(&c, 0) == 16);
    /* note ons on beats */
    int ok = 1; for (int i = 0; i < c.n; i++) if (c.st[i] == 0x90 && c.d2[i] && ((int)c.at[i] % 96) >= 16) ok = 0;
    CHECK(ok);
    /* every note on gets a note off */
    int offs = 0; for (int i = 0; i < c.n; i++) if (c.st[i] == 0x80) offs++;
    CHECK(offs >= 15);
    e1_stop(&e, emit, &c);
    offs = 0; for (int i = 0; i < c.n; i++) if (c.st[i] == 0x80) offs++;
    CHECK(offs == 16);
}

static void test_polymeter_and_length(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 0);
    e1_track_t *t = &e.t[0];
    t->steps = 5; t->pulses = 1; t->length = 0;
    cap_t c = {0}; e1_start(&e, 0); run(&e, &c, 0, 24 * 20 - 1, 6);
    CHECK(count_on(&c, 0) == 4);       /* 20 slots / 5 */
    t->length = 1;                      /* 1 bar = 16 slots, restarts step 0 */
    c.n = 0; e1_start(&e, 0); run(&e, &c, 0, 24 * 32 - 1, 6);
    CHECK(count_on(&c, 0) == 8);       /* 4 per bar (0,5,10,15) x 2 */
}

static void test_prob_deterministic(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 0);
    e.t[0].pulses = 16; e.t[0].prob = 50;
    cap_t a = {0}, b = {0};
    e1_start(&e, 0); run(&e, &a, 0, 24 * 64, 12);
    e1_start(&e, 0); run(&e, &b, 0, 24 * 64, 12);
    int na = count_on(&a, 0);
    CHECK(na > 16 && na < 48);
    CHECK(na == count_on(&b, 0));
}

static void test_dest_ports(void) {
    e1_engine_t e; e1_init(&e);
    CHECK(e1_ports(&e, &e.t[0]) == E1_PORT_MOVE);
    e1_set(&e, -1, "dest", "Computer");
    CHECK(e1_ports(&e, &e.t[0]) == E1_PORT_COMPUTER);
    e1_set(&e, -1, "dest", "2");
    CHECK(e1_ports(&e, &e.t[0]) == 3);
    e1_set(&e, 0, "output", "Move");
    CHECK(e1_ports(&e, &e.t[0]) == E1_PORT_MOVE);
}

static void test_params(void) {
    e1_engine_t e; e1_init(&e); char b[64];
    CHECK(e1_set(&e, 0, "division", "1/8T"));
    e1_get(&e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/8T") == 0);
    CHECK(e1_set(&e, 0, "division", "3"));
    e1_get(&e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/8") == 0);
    e1_set(&e, 0, "division", "d+1"); e1_get(&e, 0, "division", b, sizeof b); CHECK(strcmp(b, "1/16") == 0);
    e1_set(&e, 0, "steps", "8"); e1_set(&e, 0, "pulses", "20");
    CHECK(e.t[0].pulses == 8);
    e1_set(&e, 0, "pulses", "d-3"); CHECK(e.t[0].pulses == 5);
    e1_set(&e, 0, "channel", "16"); CHECK(e.t[0].channel == 15);
    e1_get(&e, 0, "channel", b, sizeof b); CHECK(strcmp(b, "16") == 0);
    e1_set(&e, 0, "timing", "-20"); CHECK(e.t[0].timing == -20);
    e1_set(&e, 0, "timing", "d-40"); CHECK(e.t[0].timing == -50);
    CHECK(!e1_set(&e, 0, "nope", "1"));
    e1_set(&e, 12, "cc3", "64"); CHECK(e.t[12].cc_val[2] == 64);
    e1_set(&e, 12, "ccn3", "d+1"); CHECK(e.t[12].cc_num[2] == 73);
    e1_set(&e, 12, "cc3", "-1"); CHECK(e.t[12].cc_val[2] == -1);
}

static void test_dump_load(void) {
    e1_engine_t a, b; e1_init(&a); e1_init(&b);
    e1_set(&a, 3, "steps", "13"); e1_set(&a, 3, "pulses", "9"); e1_set(&a, 3, "rotate", "11");
    e1_set(&a, 3, "division", "1/8."); e1_set(&a, 3, "cc5", "99"); e1_toggle_step(&a.t[3], 2);
    char buf[2048];
    e1_dump_track(&a, 3, buf, sizeof buf);
    e1_load_lines(&b, 0, buf);
    CHECK(b.t[0].steps == 13 && b.t[0].pulses == 9 && b.t[0].rotate == 11);
    CHECK(b.t[0].division == 13 && b.t[0].cc_val[4] == 99);
    CHECK(e1_hit_mask(&b.t[0], 0) == e1_hit_mask(&a.t[3], 0));
}

static void test_cc_track(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 12);
    e1_track_t *t = &e.t[12];
    e1_set(&e, 12, "cc1", "10");
    cap_t c = {0};
    e1_advance(&e, 0, emit, &c);              /* live edit sends at once */
    CHECK(c.n == 1 && c.st[0] == (0xB0 | t->channel) && c.d1[0] == 70 && c.d2[0] == 10);
    c.n = 0; e1_start(&e, 0); run(&e, &c, 0, 24 * 16 - 1, 24);
    CHECK(c.n == 4);                           /* one CC per pulse */
    e1_set(&e, 12, "ccmode", "Glide"); e1_set(&e, 12, "slew", "10");
    e1_set(&e, 12, "cc1", "120");
    c.n = 0; run(&e, &c, 24 * 16, 24 * 40, 1);
    CHECK(c.n > 2 && c.d2[c.n - 1] == 120);
    /* CC budget: 16 lanes on 5 tracks edited at once never exceed 8 per call */
    for (int ti = 11; ti < 16; ti++) { e.t[ti].muted = 0; e1_set(&e, ti, "ccmode", "Change");
        for (int l = 1; l <= 16; l++) { char k[16]; snprintf(k, sizeof k, "cc%d", l); e1_set(&e, ti, k, "50"); } }
    c.n = 0; e1_stop(&e, emit, &c); e1_advance(&e, 0, emit, &c); CHECK(c.n <= 8);
}

static void test_swing_delay(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 0);
    e.t[0].pulses = 16; e.t[0].timing = 50; e.t[0].delay = 0;
    cap_t c = {0}; e1_start(&e, 0); run(&e, &c, 0, 95, 1);
    double ons[8]; int n = 0;
    for (int i = 0; i < c.n; i++) if (c.st[i] == 0x90 && c.d2[i] && n < 8) ons[n++] = c.at[i];
    CHECK(n == 4 && ons[0] == 0 && ons[1] == 36 && ons[2] == 48 && ons[3] == 84);
}

static void test_join(void) {
    e1_engine_t e; e1_init(&e); solo(&e, 0);
    e.t[0].pulses = 16;
    cap_t c = {0};
    e1_join(&e, 0, 96 * 1000);          /* Move has been playing for 1000 beats */
    g_now = 96 * 1000; e1_advance(&e, g_now, emit, &c);
    CHECK(count_on(&c, 0) == 1);        /* only the step that is due now */
    c.n = 0; run(&e, &c, 96 * 1000 + 1, 96 * 1001 - 1, 1);
    CHECK(count_on(&c, 0) == 3);
    c.n = 0; g_now = 96 * 2000; e1_advance(&e, g_now, emit, &c);   /* clock jump */
    CHECK(count_on(&c, 0) <= 2);
    CHECK(e1_param_count() > 10 && strcmp(e1_param_key(0), "type") == 0);
    e1_set(&e, 0, "channel", "5"); 
    int ci = -1; for (int i = 0; i < e1_param_count(); i++) if (!strcmp(e1_param_key(i), "channel")) ci = i;
    CHECK(e1_get_num(&e, 0, ci) == 5);
}

int main(void) {
    test_join();
    test_euclid(); test_toggle(); test_playback(); test_polymeter_and_length();
    test_prob_deterministic(); test_dest_ports(); test_params(); test_dump_load();
    test_cc_track(); test_swing_delay();
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
