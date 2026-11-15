/*
 * e1_view.mjs — what E-1 shows: knob pages, parameter metadata, the four
 * bottom-pad views, LED colours and the screen. Pure functions over a state
 * object with no device globals, so tools/preview.mjs can render it
 * headlessly with Schwung's own renderer.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

export const DIVISIONS = ["1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
    "1/2T", "1/4T", "1/8T", "1/16T", "1/32T", "1/4.", "1/8.", "1/16."];
export const LENGTHS = ["Off", "1", "2", "3", "4", "5", "6", "7", "8", "16"];
export const CLOCKS = ["Move", "Internal", "Computer"];
export const DESTS = ["Move", "Computer", "Both"];
export const VIEWS = ["Values", "Perform", "Mutes", "Patterns"];

const NOTE_PC = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (n) => NOTE_PC[n % 12] + (Math.floor(n / 12) - 2);

/* Track parameters in Schwung's chain_params shape. `div` is how many encoder
 * ticks one value step takes: small ranges move slowly so a value is easy
 * to land on, wide ranges move one value per tick. */
export const TRACK_PARAMS = [
    { key: "type", name: "Type", type: "enum", options: ["Note", "CC"], div: 6 },
    { key: "steps", name: "Steps", type: "int", min: 1, max: 64, div: 4 },
    { key: "pulses", name: "Pulses", type: "int", min: 0, max: 64, div: 4 },
    { key: "rotate", name: "Rotate", type: "int", min: 0, max: 63, div: 4 },
    { key: "division", name: "Div", type: "enum", options: DIVISIONS, div: 5 },
    { key: "length", name: "Length", type: "enum", options: LENGTHS, div: 5 },
    { key: "cycles", name: "Cycles", type: "int", min: 1, max: 16, div: 4 },
    { key: "prob", name: "Prob", type: "int", min: 0, max: 100, div: 1 },
    { key: "fill", name: "Fill", type: "int", min: 0, max: 16, div: 4 },
    { key: "channel", name: "Chan", type: "int", min: 1, max: 16, div: 4 },
    { key: "output", name: "Output", type: "enum", options: ["Global", "Move", "Computer", "Both"], div: 6 },
    { key: "notemode", name: "Mode", type: "enum", options: ["Drum", "Melodic"], div: 6 },
    { key: "note", name: "Note", type: "int", min: 0, max: 127, div: 2 },
    { key: "vel", name: "Vel", type: "int", min: 1, max: 127, div: 1 },
    { key: "sustain", name: "Sustain", type: "int", min: 1, max: 100, div: 1 },
    { key: "timing", name: "Swing", type: "int", min: -50, max: 50, div: 1 },
    { key: "delay", name: "Delay", type: "int", min: -50, max: 50, div: 1 },
    { key: "mute", name: "Mute", type: "enum", options: ["Off", "On"], div: 6 },
    { key: "ccmode", name: "Send", type: "enum", options: ["Pulse", "Change", "Glide"], div: 6 },
    { key: "slew", name: "Slew", type: "int", min: 0, max: 100, div: 1 },
];

export const GLOBAL_PARAMS = [
    { key: "dest", name: "Dest", type: "enum", options: DESTS, div: 6 },
    { key: "clock", name: "Clock", type: "enum", options: CLOCKS, div: 6 },
    { key: "bpm", name: "Tempo", type: "int", min: 20, max: 300, div: 1 },
    { key: "clockout", name: "ClkOut", type: "enum", options: ["Off", "On"], div: 6 },
];

/* Knob pages. Rhythm and Setup are the same on both track types, and Type
 * is knob 3 of Setup on both, so switching a track's type never moves you. */
export const NOTE_PAGES = [
    { name: "Rhythm", keys: ["steps", "pulses", "rotate", "division", "length", "cycles", "prob", "fill"], color: "orange" },
    { name: "Groove", keys: ["vel", "sustain", "timing", "delay", null, null, null, null], color: "green" },
    { name: "Setup", keys: ["channel", "output", "type", "mute", "notemode", "note", null, null], color: "white" },
];
export const CC_PAGES = [
    { name: "Rhythm", keys: ["steps", "pulses", "rotate", "division", "length", "cycles", "prob", "fill"], color: "orange" },
    { name: "CC A", keys: ["cc1", "cc2", "cc3", "cc4", "cc5", "cc6", "cc7", "cc8"], color: "teal" },
    { name: "CC B", keys: ["cc9", "cc10", "cc11", "cc12", "cc13", "cc14", "cc15", "cc16"], color: "blue" },
    { name: "Motion", keys: ["ccmode", "slew", "timing", "delay", null, null, null, null], color: "green" },
    { name: "Setup", keys: ["channel", "output", "type", "mute", null, null, null, null], color: "white" },
];
export const MENU_PAGE = { name: "Settings", keys: ["dest", "clock", "bpm", "clockout", null, null, null, null], color: "white" };

export const TRACK_NAMES = ["KICK", "SNARE", "HAT", "CLAP", "PERC", "TOM", "RIM", "SHAKE",
    "BASS", "CHORD", "LEAD", "CC 1", "CC 2", "CC 3", "CC 4", "CC 5"];

/* Perform pads, bottom two rows left to right, upper row first.
 * kind: "hold" is momentary, "shot" fires once on press. */
export const PERFORM = [
    { label: "FILL", move: "fill", value: 1, kind: "hold", color: "orange" },
    { label: "RATCHET 2", move: "ratchet", value: 2, kind: "hold", color: "red" },
    { label: "RATCHET 3", move: "ratchet", value: 3, kind: "hold", color: "red" },
    { label: "RATCHET 4", move: "ratchet", value: 4, kind: "hold", color: "red" },
    { label: "HALF TIME", move: "speed", value: -1, kind: "hold", color: "green" },
    { label: "DOUBLE TIME", move: "speed", value: 1, kind: "hold", color: "green" },
    { label: "REVERSE", move: "reverse", value: 1, kind: "hold", color: "purple" },
    { label: "THIN", move: "thin", value: 1, kind: "hold", color: "orange" },
    { label: "FREEZE 1", move: "freeze", value: 1, kind: "hold", color: "purple" },
    { label: "FREEZE 2", move: "freeze", value: 2, kind: "hold", color: "purple" },
    { label: "FREEZE 4", move: "freeze", value: 4, kind: "hold", color: "purple" },
    { label: "ACCENT", move: "accent", value: 1, kind: "hold", color: "orange" },
    { label: "SOLO", move: "solo", value: 1, kind: "hold", color: "yellow" },
    { label: "RESTART", move: "restart", value: 0, kind: "shot", color: "teal" },
    { label: "ROTATE +1", move: "rotate", value: 1, kind: "shot", color: "teal" },
    { label: "NEW DICE", move: "dice", value: 0, kind: "shot", color: "teal" },
];

export function pagesFor(isCC) { return isCC ? CC_PAGES : NOTE_PAGES; }

/* Pages are remembered by name, so a type change or a new focus track keeps
 * you on the same page when it exists there. */
export function pageIndex(pages, name) {
    const i = pages.findIndex((p) => p.name === name);
    return i < 0 ? 0 : i;
}

export function paramDefs(snap) {
    const out = TRACK_PARAMS.slice();
    for (let i = 0; i < 16; i++) {
        const num = snap && snap.cn ? snap.cn[i] : 70 + i;
        out.push({ key: "cc" + (i + 1), name: "CC" + num, type: "int", min: -1, max: 127, div: 1 });
    }
    return out.concat(GLOBAL_PARAMS);
}

export function metaFor(key) {
    return TRACK_PARAMS.find((p) => p.key === key) || GLOBAL_PARAMS.find((p) => p.key === key) ||
        (/^cc\d+$/.test(key) ? { key, type: "int", min: -1, max: 127, div: 1 } : null);
}

/* Snapshot ("ui" JSON from the DSP) -> {key: wire string}. */
export function valuesFrom(snap, pkeys) {
    const v = {};
    if (!snap) return v;
    (pkeys || []).forEach((k, i) => {
        const m = metaFor(k);
        const n = snap.p ? snap.p[i] : 0;
        v[k] = m && m.type === "enum" ? m.options[n] : String(n);
    });
    for (let i = 0; i < 16; i++) v["cc" + (i + 1)] = String(snap.cv ? snap.cv[i] : -1);
    v.dest = DESTS[snap.d | 0];
    v.clock = CLOCKS[snap.ck | 0];
    v.bpm = String(Math.round(snap.bpm || 120));
    v.clockout = snap.co ? "On" : "Off";
    return v;
}

export function displayFor(key, raw) {
    if (raw === undefined || raw === null) return null;
    if (key === "note") return noteName(parseInt(raw, 10) | 0);
    if (/^cc\d+$/.test(key) && parseInt(raw, 10) < 0) return "OFF";
    if ((key === "timing" || key === "delay") && parseInt(raw, 10) > 0) return "+" + raw;
    return null;
}

/* Values view: 16 direct choices for the last knob touched. */
export function choicesFor(key, cur) {
    const m = metaFor(key);
    if (!m) return [];
    if (m.type === "enum") return m.options.slice(0, 16).map((o) => ({ label: o, value: o }));
    if (key === "note") {
        const c = parseInt(cur, 10) | 0;
        const base = Math.max(0, Math.min(112, c - (c % 12)));
        return Array.from({ length: 16 }, (_, i) => ({ label: noteName(base + i), value: String(base + i) }));
    }
    if (key === "steps" || key === "pulses" || key === "fill" || key === "cycles" || key === "channel") {
        const top = key === "steps" ? 16 : 16;
        const start = key === "pulses" || key === "fill" ? 0 : 1;
        return Array.from({ length: top }, (_, i) => ({ label: String(start + i), value: String(start + i) }));
    }
    if (key === "rotate") return Array.from({ length: 16 }, (_, i) => ({ label: String(i), value: String(i) }));
    if (/^cc\d+$/.test(key)) {
        const vals = [-1].concat(Array.from({ length: 15 }, (_, i) => Math.round(i * 127 / 14)));
        return vals.map((v) => ({ label: v < 0 ? "OFF" : String(v), value: String(v) }));
    }
    if (key === "bpm") return [60, 70, 80, 90, 100, 110, 115, 120, 125, 130, 135, 140, 150, 160, 170, 180]
        .map((v) => ({ label: String(v), value: String(v) }));
    const lo = m.min, hi = m.max;
    return Array.from({ length: 16 }, (_, i) => {
        const v = Math.round(lo + (hi - lo) * i / 15);
        return { label: String(v), value: String(v) };
    });
}

/* ------------------------------------------------------------ LEDs */

/* Move pad palette indices, in one place so they can be tuned. */
export const COL = {
    off: 0, white: 120, dim: 124, red: 1, green: 8, orange: 47, amber: 9,
    teal: 87, blue: 95, purple: 104, yellow: 13,
};

export const PAD_TRACK_TOP = 92;   /* tracks 1-8 on the top row, 9-16 below */
export const STEP_BASE = 16;

export function trackPadNote(t) { return t < 8 ? 92 + t : 84 + (t - 8); }
export function padToTrack(note) {
    if (note >= 92 && note <= 99) return note - 92;
    if (note >= 84 && note <= 91) return note - 84 + 8;
    return -1;
}
export function padToSlot(note) {
    if (note >= 76 && note <= 83) return note - 76;
    if (note >= 68 && note <= 75) return note - 68 + 8;
    return -1;
}
export function slotNote(i) { return i < 8 ? 76 + i : 68 + (i - 8); }

const isCCBit = (s, t) => (((s.ty | 0) >> t) & 1) === 1;
const typeColor = (s, t) => (isCCBit(s, t) ? COL.teal : COL.orange);

/* Desired colour of every pad and step LED. */
export function ledFrame(st) {
    const out = {};
    const s = st.snap || {};
    const mu = s.mu | 0, pf = s.pf | 0;
    const blink = Math.floor((st.now || 0) / 250) % 2 === 0;
    for (let t = 0; t < 16; t++) {
        const muted = (mu >> t) & 1, soloed = (pf >> (16 + t)) & 1;
        const hit = st.flashUntil && st.flashUntil[t] > (st.now || 0);
        let c = muted || soloed ? COL.dim : typeColor(s, t);
        if (muted && soloed) c = COL.off;
        if (t === st.focus) c = hit ? typeColor(s, t) : COL.white;
        else if (hit && !muted && !soloed) c = COL.white;
        if (st.copySrc === t && blink) c = COL.off;
        out[trackPadNote(t)] = c;
    }

    for (let i = 0; i < 16; i++) {
        let c = COL.off;
        if (st.view === 0) {
            const ch = st.valuesKey ? choicesFor(st.valuesKey, st.values[st.valuesKey]) : [];
            if (i < ch.length) c = ch[i].value === String(st.values[st.valuesKey]) ? COL.white : COL.blue;
        } else if (st.view === 1) {
            const p = PERFORM[i];
            const on = (st.perfHeld && st.perfHeld[i]) || (st.latched && st.latched[i]);
            c = on ? (st.latched && st.latched[i] && !blink ? COL[p.color] : COL.white) : COL[p.color];
        } else if (st.view === 2) {
            const muted = (mu >> i) & 1;
            c = muted ? COL.dim : typeColor(s, i);
            if (i === st.focus && !muted) c = COL.white;
            if ((st.soloMask >> i) & 1) c = COL.yellow;
        } else if (st.view === 3) {
            const p = st.patterns || [];
            if (st.queued === i) c = blink ? COL.green : COL.off;
            else if (st.current === i) c = COL.white;
            else if (p[i]) c = COL.blue;
            else c = COL.dim;
        }
        out[slotNote(i)] = c;
    }

    /* Step row: the focus track, current page of 16. */
    const steps = parseInt(st.values.steps, 10) || 16;
    let hits = 0n;
    try { hits = BigInt("0x" + (s.h || "0")); } catch (e) { hits = 0n; }
    const cur = s.st ? s.st[st.focus] : -1;
    for (let i = 0; i < 16; i++) {
        const idx = st.stepPage * 16 + i;
        let c = COL.off;
        if (idx < steps) {
            const on = (hits >> BigInt(idx)) & 1n;
            c = on ? typeColor(s, st.focus) : COL.dim;
            if (s.r && idx === cur) c = on ? COL.white : COL.green;
        }
        out[STEP_BASE + i] = c;
    }
    return out;
}

/* ---------------------------------------------------------- screen */

export function currentPages(st) {
    const s = st.snap || {};
    return st.menu ? [MENU_PAGE] : pagesFor(isCCBit(s, st.focus));
}

/* Arguments for renderPageMovy. lib = { PAGE_KNOBS, buildMetaIndex }. */
export function pageOptions(st, lib) {
    const s = st.snap || {};
    const pages = currentPages(st);
    const pi = st.menu ? 0 : pageIndex(pages, st.pageName);
    const pg = pages[pi];
    const metaIndex = lib.buildMetaIndex({ chainParams: paramDefs(s) });
    let title = st.menu ? "E-1" : "T" + (st.focus + 1) + " " + TRACK_NAMES[st.focus];
    if (!st.menu && st.held && st.held.size > 1) title = st.held.size + " TRACKS";
    return {
        page: { kind: lib.PAGE_KNOBS, name: pg.name, level: "root", keys: pg.keys },
        metaIndex,
        values: st.values,
        title,
        pageLabel: pg.name,
        pageIndex: pi,
        pageCount: pages.length,
        touched: st.touched,
        viz: [],
        displayFor: (k, v) => displayFor(k, v),
        footer: footerFor(st),
    };
}

export function footerFor(st) {
    const s = st.snap || {};
    if (st.warn) return [["!", st.warn]];
    const out = [];
    if (s.tmp) out.push(["TEMP", "HELD"]);
    if (st.queued >= 0 && st.queued !== undefined && st.queued !== null) out.push(["NEXT", "P" + (st.queued + 1)]);
    const clk = s.ck === 1 ? String(Math.round(s.bpm || 120)) : (s.ck === 2 ? "PC" : "MOVE");
    out.push(["CLK", clk]);
    out.push(["OUT", ["MOVE", "PC", "BOTH"][s.d | 0]]);
    return out;
}

/* A banner across the middle of the screen: page changes, perform moves,
 * pattern actions. lib.bigPrint/bigWidth draw the 6x12 Tamzen face. */
export function drawBanner(ctx, st, lib) {
    const b = st.banner;
    if (!b || !(b.until > (st.now || 0))) return;
    const big = String(b.text || "").toUpperCase();
    const small = String(b.sub || "").toUpperCase();
    const bw1 = lib.bigWidth ? lib.bigWidth(big) : big.length * 6;
    /* Twice the size when it fits, so it reads at arm's length. */
    const scale = lib.bigPrint && bw1 * 2 <= 116 ? 2 : 1;
    const bw = bw1 * scale, bh = 7 * scale;
    const sw = ctx.textWidth ? ctx.textWidth(small) : small.length * 6;
    const w = Math.min(126, Math.max(bw, sw) + 14);
    const dots = b.dots ? 5 : 0;
    const h = 8 + bh + (small ? 11 : 0) + dots + 2;
    const x = Math.floor((128 - w) / 2), y = Math.max(0, Math.floor((64 - h) / 2));
    ctx.fillRect(x - 1, y - 1, w + 2, h + 2, 0);
    ctx.fillRect(x, y, w, h, 1);
    ctx.fillRect(x + 1, y + 1, w - 2, h - 2, 0);
    const bx = Math.floor(64 - bw / 2), by = y + 5;
    if (lib.bigPrint) {
        const sc = scale === 1 ? ctx : {
            fillRect: (px, py, pw, ph, c) => ctx.fillRect(bx + (px - bx) * 2, by + (py - by) * 2, pw * 2, ph * 2, c),
        };
        lib.bigPrint(sc, bx, by - (scale === 2 ? 0 : 0), big, 1);
    } else ctx.print(bx, by, big, 1);
    let cy = by + bh + 4;
    if (small) { ctx.print(Math.floor(64 - sw / 2), cy, small, 1); cy += 11; }
    if (b.dots) {
        const n = b.dots[1], cur = b.dots[0];
        const dx = Math.floor(64 - (n * 6) / 2);
        for (let i = 0; i < n; i++) {
            if (i === cur) ctx.fillRect(dx + i * 6, cy, 4, 3, 1);
            else ctx.fillRect(dx + i * 6 + 1, cy + 1, 2, 1, 1);
        }
    }
}
