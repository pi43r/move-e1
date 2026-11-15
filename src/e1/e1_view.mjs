/*
 * e1_view.mjs — what E-1 shows: knob pages, parameter metadata, the Values
 * view, LED colours and the screen. Pure functions over a state object, with
 * no device globals, so tools/preview.mjs can render it headlessly.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

export const DIVISIONS = ["1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
    "1/2T", "1/4T", "1/8T", "1/16T", "1/32T", "1/4.", "1/8.", "1/16."];
export const LENGTHS = ["Off", "1", "2", "3", "4", "5", "6", "7", "8", "16"];
export const CLOCKS = ["Move", "Internal", "Computer"];
export const DESTS = ["Move", "Computer", "Both"];
export const VIEWS = ["Values", "Keys", "Perform", "Patterns"];

const NOTE_PC = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const noteName = (n) => NOTE_PC[n % 12] + (Math.floor(n / 12) - 2);

/* Track parameter metadata, in Schwung's chain_params shape. */
export const TRACK_PARAMS = [
    { key: "type", name: "Type", type: "enum", options: ["Note", "CC"] },
    { key: "steps", name: "Steps", type: "int", min: 1, max: 64 },
    { key: "pulses", name: "Pulses", type: "int", min: 0, max: 64 },
    { key: "rotate", name: "Rotate", type: "int", min: 0, max: 63 },
    { key: "division", name: "Div", type: "enum", options: DIVISIONS },
    { key: "length", name: "Length", type: "enum", options: LENGTHS },
    { key: "cycles", name: "Cycles", type: "int", min: 1, max: 16 },
    { key: "prob", name: "Prob", type: "int", min: 0, max: 100, unit: "%" },
    { key: "fill", name: "Fill", type: "int", min: 0, max: 16 },
    { key: "channel", name: "Chan", type: "int", min: 1, max: 16 },
    { key: "output", name: "Output", type: "enum", options: ["Global", "Move", "Computer", "Both"] },
    { key: "notemode", name: "Mode", type: "enum", options: ["Drum", "Melodic"] },
    { key: "note", name: "Note", type: "int", min: 0, max: 127 },
    { key: "vel", name: "Vel", type: "int", min: 1, max: 127 },
    { key: "sustain", name: "Sustain", type: "int", min: 1, max: 100 },
    { key: "timing", name: "Swing", type: "int", min: -50, max: 50 },
    { key: "delay", name: "Delay", type: "int", min: -50, max: 50 },
    { key: "mute", name: "Mute", type: "enum", options: ["Off", "On"] },
    { key: "ccmode", name: "Send", type: "enum", options: ["Pulse", "Change", "Glide"] },
    { key: "slew", name: "Slew", type: "int", min: 0, max: 100 },
];

export const GLOBAL_PARAMS = [
    { key: "dest", name: "Dest", type: "enum", options: DESTS },
    { key: "clock", name: "Clock", type: "enum", options: CLOCKS },
    { key: "bpm", name: "Tempo", type: "int", min: 20, max: 300 },
    { key: "clockout", name: "ClkOut", type: "enum", options: ["Off", "On"] },
];

/* Knob pages. Every page fills the 8 knobs in the same order on every track
 * of that type, so muscle memory carries across tracks. */
export const NOTE_PAGES = [
    { name: "Rhythm", keys: ["steps", "pulses", "rotate", "division", "length", "cycles", "prob", "fill"] },
    { name: "Groove", keys: ["vel", "sustain", "timing", "delay", null, null, null, null] },
    { name: "Setup", keys: ["channel", "output", "notemode", "note", "type", "mute", null, null] },
];
export const CC_PAGES = [
    { name: "Rhythm", keys: ["steps", "pulses", "rotate", "division", "length", "cycles", "prob", "fill"] },
    { name: "CC A", keys: ["cc1", "cc2", "cc3", "cc4", "cc5", "cc6", "cc7", "cc8"] },
    { name: "CC B", keys: ["cc9", "cc10", "cc11", "cc12", "cc13", "cc14", "cc15", "cc16"] },
    { name: "Motion", keys: ["ccmode", "slew", "timing", "delay", null, null, null, null] },
    { name: "Setup", keys: ["channel", "output", "type", "mute", null, null, null, null] },
];
export const MENU_PAGE = { name: "Settings", keys: ["dest", "clock", "bpm", "clockout", null, null, null, null] };

export const TRACK_NAMES = ["KICK", "SNARE", "HAT", "CLAP", "PERC", "TOM", "RIM", "SHAKE",
    "BASS", "CHORD", "LEAD", "CC 1", "CC 2", "CC 3", "CC 4", "CC 5"];

export function pagesFor(isCC) { return isCC ? CC_PAGES : NOTE_PAGES; }

/* chain_params-style list for the current focus track: CC lane names carry
 * their CC number, so the label reads "CC74". */
export function paramDefs(snap) {
    const out = TRACK_PARAMS.slice();
    for (let i = 0; i < 16; i++) {
        const num = snap && snap.cn ? snap.cn[i] : 70 + i;
        out.push({ key: "cc" + (i + 1), name: "CC" + num, type: "int", min: -1, max: 127 });
    }
    return out.concat(GLOBAL_PARAMS);
}

export function metaFor(key) {
    return TRACK_PARAMS.find((p) => p.key === key) || GLOBAL_PARAMS.find((p) => p.key === key) ||
        (/^cc\d+$/.test(key) ? { key, type: "int", min: -1, max: 127 } : null);
}

/* Snapshot ("ui" JSON from the DSP) -> {key: wire string}. pkeys is the DSP's
 * own parameter order, read once from "pkeys". */
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

/* Readable values where the raw number says little. */
export function displayFor(key, raw) {
    if (raw === undefined || raw === null) return null;
    if (key === "note") return noteName(parseInt(raw, 10) | 0);
    if (/^cc\d+$/.test(key) && parseInt(raw, 10) < 0) return "OFF";
    if ((key === "timing" || key === "delay") && parseInt(raw, 10) > 0) return "+" + raw;
    return null;
}

/* Values view: 16 direct choices for the last knob touched, like the T-1's
 * value buttons. Returns [{label, value}] where value is the wire string. */
export function choicesFor(key, cur) {
    const m = metaFor(key);
    if (!m) return [];
    if (m.type === "enum") return m.options.slice(0, 16).map((o) => ({ label: o, value: o }));
    if (key === "note") {
        const base = Math.max(0, Math.min(112, (parseInt(cur, 10) | 0) - ((parseInt(cur, 10) | 0) % 12)));
        return Array.from({ length: 16 }, (_, i) => ({ label: noteName(base + i), value: String(base + i) }));
    }
    const lo = m.min, hi = m.max;
    if (hi - lo + 1 <= 16) return Array.from({ length: hi - lo + 1 }, (_, i) => ({ label: String(lo + i), value: String(lo + i) }));
    if (key === "steps" || key === "pulses" || key === "rotate" || key === "fill") {
        const start = key === "rotate" ? 0 : 1;
        return Array.from({ length: 16 }, (_, i) => ({ label: String(start + i), value: String(start + i) }));
    }
    if (/^cc\d+$/.test(key)) {
        const vals = [-1].concat(Array.from({ length: 15 }, (_, i) => Math.round(i * 127 / 14)));
        return vals.map((v) => ({ label: v < 0 ? "OFF" : String(v), value: String(v) }));
    }
    return Array.from({ length: 16 }, (_, i) => {
        const v = Math.round(lo + (hi - lo) * i / 15);
        return { label: String(v), value: String(v) };
    });
}

/* ------------------------------------------------------------ LEDs */

/* Move pad palette indices. Tuned on hardware later; kept in one place. */
export const COL = {
    off: 0, white: 120, dim: 124, red: 1, green: 8, orange: 47, amber: 9,
    teal: 87, blue: 95, purple: 104, yellow: 13,
};

export const PAD_TRACK_TOP = 92;   /* tracks 1-8 on the top row, 9-16 below */
export const PAD_LOWER_TOP = 76;   /* lower view slots 1-8, then 9-16 at 68 */
export const STEP_BASE = 16;

export function trackPadNote(t) { return t < 8 ? PAD_TRACK_TOP + t : PAD_TRACK_TOP - 8 + (t - 8); }
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

/* Desired colour of every pad and step LED for a state. */
export function ledFrame(st) {
    const out = {};
    const s = st.snap || {};
    const fl = s.fl | 0, mu = s.mu | 0, ty = s.ty | 0;
    for (let t = 0; t < 16; t++) {
        const isCC = (ty >> t) & 1, muted = (mu >> t) & 1, hit = (fl >> t) & 1;
        const base = isCC ? COL.teal : COL.orange;
        let c = muted ? COL.dim : base;
        if (t === st.focus) c = hit ? base : COL.white;
        else if (hit && !muted) c = COL.white;
        out[trackPadNote(t)] = c;
    }
    /* Lower view */
    const choices = st.view === 0 && st.valuesKey ? choicesFor(st.valuesKey, st.values[st.valuesKey]) : [];
    for (let i = 0; i < 16; i++) {
        let c = COL.off;
        if (st.view === 0) {
            if (i < choices.length) c = choices[i].value === String(st.values[st.valuesKey]) ? COL.white : COL.blue;
        } else if (st.view === 2) {
            if (i === 0) c = st.fillHeld ? COL.white : COL.purple;   /* FILL; the rest arrive in Phase 3 */
        }
        out[slotNote(i)] = c;
    }
    /* Step row: the focus track's pattern, current page of 16 */
    const steps = parseInt(st.values.steps, 10) || 16;
    const hits = BigInt("0x" + (s.h || "0"));
    const cur = s.st ? s.st[st.focus] : -1;
    for (let i = 0; i < 16; i++) {
        const idx = st.stepPage * 16 + i;
        let c = COL.off;
        if (idx < steps) {
            const on = (hits >> BigInt(idx)) & 1n;
            c = on ? (((ty >> st.focus) & 1) ? COL.teal : COL.orange) : COL.dim;
            if (s.r && idx === cur) c = on ? COL.white : COL.green;
        }
        out[STEP_BASE + i] = c;
    }
    return out;
}

/* ---------------------------------------------------------- screen */

/* Arguments for renderPageMovy. lib = { PAGE_KNOBS, buildMetaIndex }. */
export function pageOptions(st, lib) {
    const s = st.snap || {};
    const isCC = ((s.ty | 0) >> st.focus) & 1;
    const pages = st.menu ? [MENU_PAGE] : pagesFor(isCC);
    const pi = st.menu ? 0 : Math.min(st.page, pages.length - 1);
    const pg = pages[pi];
    const metaIndex = lib.buildMetaIndex({ chainParams: paramDefs(s) });
    const title = st.menu ? "E-1" : "T" + (st.focus + 1) + " " + TRACK_NAMES[st.focus];
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
    const clk = s.ck === 1 ? String(Math.round(s.bpm || 120)) : (s.ck === 2 ? "PC" : "MOVE");
    const dst = ["MOVE", "PC", "BOTH"][s.d | 0];
    const out = [["CLK", clk], ["OUT", dst]];
    if (!st.menu) out.push(["VIEW", VIEWS[st.view].slice(0, 4)]);
    return out;
}

/* Small overlay on top of the movy page: a play marker in the header's right
 * corner so the transport state is always visible. */
export function drawOverlay(ctx, st) {
    const s = st.snap || {};
    if (!s.r) return;
    const beat = Math.floor((s.tk || 0) / 96) % 4;
    for (let i = 0; i < 4; i++) ctx.fillRect(108 + i * 5, 62, 4, 2, i === beat ? 1 : 0);
}
