#!/usr/bin/env node
/*
 * Render E-1's screens headlessly with Schwung's own movy page renderer.
 *
 *   SCHWUNG=../schwung node tools/preview.mjs --png docs/screens
 *
 * SCHWUNG points at a checkout of https://github.com/charlesvestal/schwung.
 */
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL } from "node:url";
import * as V from "../src/e1/e1_view.mjs";

const root = path.resolve(process.env.SCHWUNG || "../schwung");
const imp = (p) => import(pathToFileURL(path.join(root, p)).href);
const { createFramebuffer, drawContext } = await imp("tools/param-pages/harness.mjs");
const { renderPageMovy } = await imp("src/shared/param_pages/render_page_movy.mjs");
const { PAGE_KNOBS } = await imp("src/shared/param_pages/page_plan.mjs");
const { buildMetaIndex } = await imp("src/shared/param_pages/param_meta.mjs");
const LIB = { PAGE_KNOBS, buildMetaIndex };

const argv = process.argv.slice(2);
const pngDir = argv.includes("--png") ? argv[argv.indexOf("--png") + 1] : null;
const pkeys = V.TRACK_PARAMS.map((p) => p.key);

/* A snapshot shaped like the DSP's "ui" answer. */
function snap(track, over = {}) {
    const p = { type: track >= 11 ? 1 : 0, steps: 16, pulses: 5, rotate: 2, division: 4, length: 0, cycles: 1,
        prob: 100, fill: 3, channel: 1, output: 0, notemode: 0, note: 36 + track, vel: 100, sustain: 25,
        timing: 12, delay: 0, mute: 0, ccmode: 2, slew: 30, ...over };
    return {
        r: 1, ck: 0, bpm: 120, d: 0, co: 1, f: track + 1, fl: 0, mu: 0xFFF0, ty: 0xF800, tk: 200, st: Array(16).fill(3),
        h: "a52", p: pkeys.map((k) => p[k]),
        cv: [64, 20, -1, 100, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1],
        cn: Array.from({ length: 16 }, (_, i) => 70 + i),
    };
}

function state(track, page, extra = {}) {
    const s = snap(track);
    return { focus: track, page, menu: false, view: 0, stepPage: 0, touched: -1, valuesKey: "steps",
        snap: s, values: V.valuesFrom(s, pkeys), warn: null, fillHeld: false, ...extra };
}

const cases = [
    ["note-rhythm", state(0, 0)],
    ["note-groove", state(0, 1)],
    ["note-setup", state(0, 2)],
    ["note-touch-division", state(0, 0, { touched: 3 })],
    ["cc-a", state(11, 1)],
    ["cc-motion", state(11, 3)],
    ["settings", state(0, 0, { menu: true })],
    ["warn-sync", (() => { const s = state(0, 0); s.snap.ck = 2; s.warn = "SET MOVE TO MIDI SYNC"; return s; })()],
];

if (pngDir) fs.mkdirSync(pngDir, { recursive: true });
let bad = 0;
for (const [name, st] of cases) {
    const fb = createFramebuffer();
    const ctx = drawContext(fb);
    renderPageMovy(ctx, V.pageOptions(st, LIB));
    V.drawOverlay(ctx, st);
    const missing = [];
    if (fb.clipped() || (missing && missing.length)) bad++;
    if (pngDir) fs.writeFileSync(path.join(pngDir, name + ".png"), fb.toPng(4));
    else { console.log("== " + name); console.log(fb.toBlocks()); }
    console.log(name + "  clipped=" + fb.clipped() + (missing && missing.length ? "  missing=" + JSON.stringify(missing) : ""));
}
process.exit(bad ? 1 : 0);
