/* Drives E-1 Lite through a fake host: `make test` */
#include "../src/e1_lite/dsp/e1_lite.c"
#include <stdio.h>

static int fails = 0, checks = 0;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); } } while (0)
static double beat = -1.0;
static double fake_beat(void) { return beat; }

int main(void) {
    static host_api_v1_t h;
    h.get_beat_position = fake_beat;
    midi_fx_api_v1_t *api = move_midi_fx_init(&h);
    void *s = api->create_instance("/tmp", NULL);
    uint8_t out[16][3]; int lens[16];
    int ons = 0, offs = 0;
    for (int i = 0; i < 689; i++) {     /* one bar at 120 bpm */
        beat = i * 128.0 / 44100.0 * 2.0;
        int n = api->tick(s, 128, 44100, out, lens, 16);
        for (int j = 0; j < n; j++) { if ((out[j][0] & 0xF0) == 0x90 && out[j][2]) ons++; if ((out[j][0] & 0xF0) == 0x80) offs++; }
    }
    CHECK(ons == 4);
    CHECK(offs >= 3);
    char buf[2048];
    api->set_param(s, "division", "1/8T");
    api->get_param(s, "division", buf, sizeof buf); CHECK(!strcmp(buf, "1/8T"));
    api->set_param(s, "pulses", "d+2");
    api->get_param(s, "pulses", buf, sizeof buf); CHECK(!strcmp(buf, "6"));
    uint8_t in[3] = { 0x90, 60, 100 };
    CHECK(api->process_midi(s, in, 3, out, lens, 16) == 1 && out[0][1] == 60);
    beat = -1.0; int n = api->tick(s, 128, 44100, out, lens, 16); (void)n;
    api->destroy_instance(s);
    printf("%d/%d lite checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
