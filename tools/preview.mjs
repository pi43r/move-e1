#!/usr/bin/env node
/*
 * Render E-1's screens headlessly with Schwung's own movy page renderer.
 *
 *   SCHWUNG=../schwung node tools/preview.mjs --png docs/screens
 *
 * SCHWUNG points at a checkout of https://github.com/charlesvestal/schwung.
 * tools/fixtures.json holds snapshots taken from the real DSP.
 */
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";
import * as V from "../src/e1/e1_view.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(process.env.SCHWUNG || "../schwung");
const imp = (p) => import(pathToFileURL(path.join(root, p)).href);
const { createFramebuffer, drawContext } = await imp("tools/param-pages/harness.mjs");
const { renderPageMovy } = await imp("src/shared/param_pages/render_page_movy.mjs");
const { PAGE_KNOBS } = await imp("src/shared/param_pages/page_plan.mjs");
const { buildMetaIndex } = await imp("src/shared/param_pages/param_meta.mjs");
const LIB = { PAGE_KNOBS, buildMetaIndex };
const FX = JSON.parse(fs.readFileSync(path.join(here, "fixtures.json"), "utf8"));

const argv = process.argv.slice(2);
const pngDir = argv.includes("--png") ? argv[argv.indexOf("--png") + 1] : null;

function state(snapName, pageName, extra = {}) {
    const snap = JSON.parse(JSON.stringify(FX.snaps[snapName]));
    const patterns = new Array(16).fill(null);
    patterns[0] = { name: "INTRO", tracks: [] }; patterns[1] = { name: "DROP", tracks: [] }; patterns[4] = { name: "", tracks: [] };
    return {
        focus: snap.f - 1, pageName, now: 1000, held: new Set(), stepPage: 0, touched: -1, padKey: "steps", padPage: 0, kbOct: 3,
        snap, values: V.valuesFrom(snap, FX.pkeys), warn: null, layer: null, settings: false, shift: false, randomHeld: false,
        mutePend: 0, copySrc: -1, flashUntil: new Array(16).fill(0), patterns, current: 0, queued: -1,
        perfHeld: new Array(16).fill(false), latched: new Array(16).fill(false), toast: null, setName: "Live A", ...extra,
    };
}
const toast = (tag, text) => ({ tag, text, until: 2000 });

const cases = [
    ["home-shape", state("t1", "Left")],
    ["shape-shift", state("t1", "Left", { shift: true })],
    ["touch-division", state("t1", "Left", { touched: 3, padKey: "division" })],
    ["tone", state("t10", "Right")],
    ["tone-shift", state("t10", "Right", { shift: true })],
    ["random", state("t10", "Right", { randomHeld: true })],
    ["cycle-edit", state("t1edit", "Left")],
    ["cc-a", state("t12", "CC A")],
    ["cc-motion", state("t12", "Right")],
    ["settings", state("t1", "Left", { settings: true })],
    ["toast-mute", state("t1", "Left", { toast: toast("MUTE", "3 TRACKS") })],
    ["toast-next", (() => { const s = state("t1", "Left", { toast: toast("NEXT", "P2 DROP"), queued: 1 }); s.snap.q = 1; return s; })()],
    ["warn-sync", (() => { const s = state("t1", "Left"); s.snap.ck = 2; s.warn = "SET MOVE TO MIDI SYNC"; return s; })()],
    ["patterns", state("t1", "Left", { layer: "pattern" })],
    ["perform", state("t1", "Left", { layer: "perform", perfHeld: Array.from({ length: 16 }, (_, i) => i === 2), latched: Array.from({ length: 16 }, (_, i) => i === 6) })],
    ["cycles", state("t1edit", "Left", { layer: "cycles" })],
    ["list-scale", state("t10", "Right", { listKey: "scale", padKey: "scale" })],
    ["list-division", state("t1", "Left", { listKey: "division", padKey: "division" })],
    ["selected-knob", state("t1", "Left", { padKey: "pulses" })],
];

if (pngDir) fs.mkdirSync(pngDir, { recursive: true });
let bad = 0;
for (const [name, st] of cases) {
    const fb = createFramebuffer();
    const ctx = drawContext(fb);
    if (st.listKey && V.drawValueList(ctx, st)) { /* list of a named value */ }
    else if (!V.drawLayer(ctx, st)) { renderPageMovy(ctx, V.pageOptions(st, LIB)); V.drawSelection(ctx, st); }
    if (fb.clipped()) bad++;
    if (pngDir) fs.writeFileSync(path.join(pngDir, name + ".png"), fb.toPng(4));
    else { console.log("== " + name); console.log(fb.toBlocks()); }
    console.log(name + "  clipped=" + fb.clipped());
}
process.exit(bad ? 1 : 0);
