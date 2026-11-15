#!/usr/bin/env node
/*
 * Render the library menu headlessly with Schwung's device-font harness.
 *
 *   SCHWUNG=../schwung node tools/preview_library.mjs [--out docs/screens]
 *
 * Writes docs/screens/menu-*.png at scale 4 and exits 1 if any screen draws
 * outside the 128x64 panel or uses a glyph the device font lacks.
 */
import fs from "node:fs";
import path from "node:path";
import { pathToFileURL, fileURLToPath } from "node:url";
import { createMenu } from "../src/e1/e1_library.mjs";

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(process.env.SCHWUNG || "../schwung");
const { createFramebuffer, drawContext } = await import(pathToFileURL(path.join(root, "tools/param-pages/harness.mjs")).href);

const argv = process.argv.slice(2);
const outDir = argv.includes("--out") ? argv[argv.indexOf("--out") + 1] : path.join(here, "..", "docs", "screens");

const NAMES = ["KICK", "SNARE", "CL HAT", "OP HAT", "CLAP", "RIM", "TOM LO", "TOM HI",
    "PERC", "SHAKER", "RIDE", "BASS", "LEAD", "PAD", "FILTER CUTOFF", "MOD"];

function makeState(over = {}) {
    const patterns = Array.from({ length: 16 }, () => ({ name: "", filled: false }));
    patterns[0] = { name: "Intro", filled: true };
    patterns[1] = { name: "Verse", filled: true };
    patterns[2] = { name: "Chorus with long name", filled: true };
    patterns[4] = { name: "Break", filled: true };
    return {
        setId: "techno-jam", setName: "Techno Jam", dirty: false,
        sets: [
            { id: "ambient", name: "Ambient" },
            { id: "broken-beats-session", name: "Broken Beats Session" },
            { id: "dub", name: "Dub" },
            { id: "house-2", name: "House 2" },
            { id: "techno-jam", name: "Techno Jam" },
            { id: "test", name: "Test" },
        ],
        patterns, currentPattern: 1, focus: 2, trackNames: NAMES.slice(),
        presets: [{ name: "Tight hat" }, { name: "Open hat long tail" }, { name: "808 kick" }],
        ...over,
    };
}

/* Each case drives a fresh menu to a screen through the public API only. */
const cases = [
    ["root", {}, (m) => {}],
    ["sets", {}, (m) => { m.jog(1); m.click(); m.jog(6); }],
    ["set-submenu", {}, (m) => { m.jog(1); m.click(); m.jog(4); m.click(); }],
    ["unsaved-confirm", { dirty: true }, (m) => { m.jog(1); m.click(); m.jog(4); m.click(); m.click(); }],
    ["patterns", {}, (m) => { m.jog(2); m.click(); m.jog(2); }],
    ["pattern-submenu", {}, (m) => { m.jog(2); m.click(); m.jog(2); m.click(); m.jog(3); }],
    ["copy-to", {}, (m) => { m.jog(2); m.click(); m.click(); m.jog(3); m.click(); m.jog(9); }],
    ["track-presets", {}, (m) => { m.jog(3); m.click(); }],
    ["tracks", { focus: 14 }, (m) => { m.jog(4); m.click(); m.jog(14); }],
    ["toast", {}, (m) => { m.jog(1); m.click(); m.click(); }],
    ["toast-last-row", {}, (m) => { m.jog(4); m.click(); m.jog(4); m.click(); }],
    ["delete-confirm", {}, (m) => { m.jog(1); m.click(); m.jog(5); m.click(); m.jog(3); m.click(); }],
];

fs.mkdirSync(outDir, { recursive: true });
let bad = 0;
for (const [name, over, drive] of cases) {
    const state = makeState(over);
    const menu = createMenu({
        host: { state: () => state, act: () => {}, askName: (title, initial, done) => done(initial) },
        now: () => 1000,
    });
    menu.open();
    drive(menu);
    const fb = createFramebuffer();
    menu.draw(drawContext(fb));
    const missing = [...fb.missingGlyphs];
    if (fb.clipped() || missing.length) bad++;
    const file = path.join(outDir, "menu-" + name + ".png");
    fs.writeFileSync(file, fb.toPng(4));
    console.log(("menu-" + name).padEnd(22) + " clipped=" + fb.clipped() +
        (missing.length ? " missing=" + JSON.stringify(missing) : "") + "  " + menu.path().join(" > "));
}
process.exit(bad ? 1 : 0);
