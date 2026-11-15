/* Drives the overtake DSP through a fake host: `make test` */
#include "../src/e1/dsp/e1_plugin.c"
#include <assert.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)

static double beat = -1.0;
static int n_move, n_comp, n_clock, n_start, n_stop, move_full;
static uint8_t last_move[4];
static double fake_beat(void) { return beat; }
static float fake_bpm(void) { return 120.0f; }
static int fake_inject(const uint8_t *m, int len) { (void)len; if (move_full) return 0; n_move++; memcpy(last_move, m, 4); return 4; }
static int fake_ext(const uint8_t *m, int len) {
    (void)len;
    if (m[1] == 0xF8) n_clock++; else if (m[1] == 0xFA) n_start++; else if (m[1] == 0xFC) n_stop++; else n_comp++;
    return 4;
}

int main(void) {
    static host_api_v1_t h;
    h.get_beat_position = fake_beat; h.get_bpm = fake_bpm;
    h.midi_inject_to_move = fake_inject; h.midi_send_external = fake_ext;
    plugin_api_v2_t *api = move_plugin_init_v2(&h);
    void *inst = api->create_instance("/tmp", NULL);
    int16_t audio[256];
    char buf[8192];

    /* Follow Move: nothing until Move plays */
    for (int i = 0; i < 100; i++) api->render_block(inst, audio, 128);
    CHECK(n_move == 0);
    /* Move plays one bar at 120 bpm: 2 s = ~689 blocks */
    for (int i = 0; i < 689; i++) { beat = i * 128.0 / 44100.0 * 2.0; api->render_block(inst, audio, 128); }
    CHECK(n_move > 0 && n_comp == 0);
    CHECK((last_move[0] & 0xF0) == 0x20);
    int before = n_move;
    beat = -1.0; for (int i = 0; i < 50; i++) api->render_block(inst, audio, 128);
    CHECK(n_move >= before);   /* note offs flushed on stop */

    /* UI snapshot parses and reports the focus track */
    api->set_param(inst, "focus", "3");
    int n = api->get_param(inst, "ui", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "\"f\":3") && buf[n - 1] == '}');

    /* Per-track keys and dumps */
    api->set_param(inst, "t3:steps", "12");
    api->get_param(inst, "t3:steps", buf, sizeof buf); CHECK(strcmp(buf, "12") == 0);
    n = api->get_param(inst, "dump:3", buf, sizeof buf); CHECK(n > 0 && strstr(buf, "steps=12"));
    api->set_param(inst, "load:4", buf);
    api->get_param(inst, "t4:steps", buf, sizeof buf); CHECK(strcmp(buf, "12") == 0);

    /* Internal clock to the computer */
    api->set_param(inst, "dest", "Computer");
    api->set_param(inst, "clock", "Internal");
    api->set_param(inst, "play", "1");
    n_comp = 0; n_clock = 0;
    for (int i = 0; i < 689; i++) api->render_block(inst, audio, 128);  /* ~2 s = 1 bar */
    CHECK(n_start == 1);
    CHECK(n_clock >= 94 && n_clock <= 98);  /* 96 clocks per bar */
    CHECK(n_comp > 0);
    api->set_param(inst, "play", "0");
    api->render_block(inst, audio, 128);
    CHECK(n_stop == 1);

    /* Globals round-trip */
    api->set_param(inst, "bpm", "97");
    n = api->get_param(inst, "dump:g", buf, sizeof buf);
    CHECK(strstr(buf, "clock=Internal") && strstr(buf, "bpm=97.0") && strstr(buf, "dest=Computer"));
    api->set_param(inst, "clock", "Move"); api->set_param(inst, "dest", "Move");
    api->set_param(inst, "load:g", buf);
    api->get_param(inst, "clock", buf, sizeof buf); CHECK(strcmp(buf, "Internal") == 0);
    api->get_param(inst, "dest", buf, sizeof buf); CHECK(strcmp(buf, "Computer") == 0);

    /* A full Move queue never blocks or crashes; packets wait */
    api->set_param(inst, "clock", "Move"); api->set_param(inst, "dest", "Move");
    move_full = 1; beat = 0;
    for (int i = 0; i < 3000; i++) { beat = i * 128.0 / 44100.0 * 2.0; api->render_block(inst, audio, 128); }
    move_full = 0; int was = n_move;
    for (int i = 0; i < 400; i++) api->render_block(inst, audio, 128);
    CHECK(n_move > was);

    /* Pattern recall lands on the bar line */
    api->set_param(inst, "clock", "Internal"); api->set_param(inst, "dest", "Move");
    api->set_param(inst, "play", "1");
    for (int i = 0; i < 100; i++) api->render_block(inst, audio, 128);    /* mid bar */
    api->set_param(inst, "stage:1", "steps=7\npulses=3\n");
    api->set_param(inst, "commit", "bar");
    api->get_param(inst, "t1:steps", buf, sizeof buf); CHECK(strcmp(buf, "7") != 0);
    for (int i = 0; i < 1500; i++) api->render_block(inst, audio, 128);   /* 97 bpm: over a bar */
    api->get_param(inst, "t1:steps", buf, sizeof buf); CHECK(strcmp(buf, "7") == 0);
    e1_inst_t *S = (e1_inst_t *)inst;
    CHECK(S->commit == 0);

    /* Temp: edits made while held snap back on release */
    api->set_param(inst, "temp", "1");
    api->set_param(inst, "t1:pulses", "1");
    api->set_param(inst, "t2:division", "1/8T");
    api->set_param(inst, "temp", "0");
    api->get_param(inst, "t1:pulses", buf, sizeof buf); CHECK(strcmp(buf, "3") == 0);
    api->get_param(inst, "t2:division", buf, sizeof buf); CHECK(strcmp(buf, "1/16") == 0);
    api->set_param(inst, "temp", "1"); api->set_param(inst, "t1:pulses", "2"); api->set_param(inst, "temp", "keep");
    api->get_param(inst, "t1:pulses", buf, sizeof buf); CHECK(strcmp(buf, "2") == 0);

    /* Perform moves by mask */
    api->set_param(inst, "perf:ratchet", "3 4");
    CHECK(S->e.t[0].p_ratchet == 4 && S->e.t[1].p_ratchet == 4 && S->e.t[2].p_ratchet == 0);
    n = api->get_param(inst, "ui", buf, sizeof buf);
    CHECK(n > 0 && strstr(buf, "\"pf\":3,"));
    api->set_param(inst, "perf:clear", "ffff 0");
    CHECK(S->e.t[0].p_ratchet == 0);

    api->destroy_instance(inst);
    printf("%d/%d plugin checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
