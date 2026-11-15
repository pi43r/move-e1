/*
 * e1_view.mjs — what E-1 shows: knob pages, parameter metadata, what the
 * bottom 16 pads mean for the knob under your finger, LED colours and the
 * screen. Pure functions over a state object with no device globals, so
 * tools/preview.mjs can render everything headlessly.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

export const DIVISIONS = ["1/1", "1/2", "1/4", "1/8", "1/16", "1/32", "1/64",
    "1/2T", "1/4T", "1/8T", "1/16T", "1/32T", "1/4.", "1/8.", "1/16."];
export const LENGTHS = ["Off", "1", "2", "3", "4", "5", "6", "7", "8", "16"];
export const CLOCKS = ["Move", "Internal", "Computer"];
export const DESTS = ["Move", "Computer", "Both"];
export const QUANTS = ["1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"];
export const STYLES = ["PolyFix", "PolyRamp", "PolyClimb", "MonoFix", "MonoRamp", "MonoClimb"];
export const PHRASES = ["Cad1", "Cad2", "Cad3", "Cad4", "Saw", "Tri", "Sine", "Pulse"];
export const SCALES = ["Chrom", "Major", "Minor", "Penta", "Hira", "Iwato", "Tetra", "Dorian"];
export const ROOTS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
export const GROOVES = ["Agogo", "Congas", "Timbal", "Bongo", "Saw", "Tri", "Sine", "Pulse"];
export const REPEATS_INF = 49;

const SCALE_DEG = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 7, 9],
    [0, 2, 3, 7, 8], [0, 1, 5, 6, 10], [0, 4, 7, 10], [0, 2, 3, 5, 7, 9, 10],
];

export const noteName = (n) => ROOTS[n % 12] + (Math.floor(n / 12) - 2);

/* Track parameters in Schwung's chain_params shape. `div` = encoder detents
 * per value: small ranges move slowly so a value is easy to land on.
 * `rnd` marks parameters that take a random amount. */
export const TRACK_PARAMS = [
    { key: "type", name: "Type", type: "enum", options: ["Note", "CC"], div: 8 },
    { key: "steps", name: "Steps", type: "int", min: 1, max: 64, div: 4, rnd: 1 },
    { key: "pulses", name: "Pulses", type: "int", min: 0, max: 64, div: 4, rnd: 1 },
    { key: "rotate", name: "Rotate", type: "int", min: 0, max: 63, div: 4, rnd: 1 },
    { key: "division", name: "Div", type: "enum", options: DIVISIONS, div: 5 },
    { key: "length", name: "Length", type: "enum", options: LENGTHS, div: 5 },
    { key: "cycles", name: "Cycles", type: "int", min: 1, max: 16, div: 4 },
    { key: "velocity", name: "Vel", type: "int", min: 1, max: 127, div: 1, rnd: 1 },
    { key: "prob", name: "Prob", type: "int", min: -100, max: 100, div: 1, rnd: 1 },
    { key: "sustain", name: "Sustain", type: "int", min: 1, max: 100, div: 1, rnd: 1 },
    { key: "repeats", name: "Repeats", type: "int", min: 0, max: REPEATS_INF, div: 3, rnd: 1 },
    { key: "time", name: "Time", type: "enum", options: DIVISIONS, div: 5 },
    { key: "ramp", name: "Ramp", type: "int", min: -100, max: 100, div: 1 },
    { key: "pace", name: "Pace", type: "int", min: -100, max: 100, div: 1 },
    { key: "fill", name: "Fill", type: "int", min: 0, max: 16, div: 4 },
    { key: "delay", name: "Delay", type: "int", min: -50, max: 50, div: 1 },
    { key: "pitch", name: "Pitch", type: "int", min: -36, max: 36, div: 3, rnd: 1 },
    { key: "harmony", name: "Harmony", type: "int", min: -16, max: 16, div: 4, rnd: 1 },
    { key: "voicing", name: "Voicing", type: "int", min: -16, max: 16, div: 4, rnd: 1 },
    { key: "style", name: "Style", type: "enum", options: STYLES, div: 6 },
    { key: "range", name: "Range", type: "int", min: -16, max: 16, div: 4, rnd: 1 },
    { key: "phrase", name: "Phrase", type: "enum", options: PHRASES, div: 6 },
    { key: "scale", name: "Scale", type: "enum", options: SCALES, div: 6 },
    { key: "root", name: "Root", type: "enum", options: ROOTS, div: 5 },
    { key: "accent", name: "Accent", type: "int", min: -100, max: 100, div: 1, rnd: 1 },
    { key: "groove", name: "Groove", type: "enum", options: GROOVES, div: 6 },
    { key: "timing", name: "Timing", type: "int", min: -50, max: 50, div: 1, rnd: 1 },
    { key: "swinggrid", name: "Grid", type: "enum", options: ["x1", "x2", "x4"], div: 8 },
    { key: "random", name: "Random", type: "int", min: 0, max: 100, div: 1 },
    { key: "rate", name: "Rate", type: "enum", options: DIVISIONS, div: 5 },
    { key: "channel", name: "Chan", type: "int", min: 1, max: 16, div: 4 },
    { key: "output", name: "Output", type: "enum", options: ["Global", "Move", "Computer", "Both"], div: 8 },
    { key: "mute", name: "Mute", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "dir", name: "Dir", type: "enum", options: ["Up", "Down"], div: 8 },
    { key: "tail", name: "Repeat", type: "enum", options: ["Choke", "Tail"], div: 8 },
    { key: "phraserate", name: "PhrRate", type: "enum", options: ["x1", "x2", "/2"], div: 8 },
    { key: "ccmode", name: "Send", type: "enum", options: ["Pulse", "Change", "Glide"], div: 8 },
    { key: "slew", name: "Slew", type: "int", min: 0, max: 100, div: 1 },
];

export const GLOBAL_PARAMS = [
    { key: "dest", name: "Dest", type: "enum", options: DESTS, div: 8 },
    { key: "clock", name: "Clock", type: "enum", options: CLOCKS, div: 8 },
    { key: "bpm", name: "Tempo", type: "int", min: 20, max: 300, div: 1 },
    { key: "clockout", name: "ClkOut", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "quant", name: "Quant", type: "enum", options: QUANTS, div: 6 },
];
export const GLOBAL_KEYS = new Set(GLOBAL_PARAMS.map((p) => p.key));

/* Knob pages. `keys` is what the knobs do, `alt` what they do with Shift
 * held, the same pairing the T-1 prints under each knob. */
const SHAPE = {
    name: "Shape",
    keys: ["steps", "pulses", "cycles", "division", "repeats", "time", "velocity", "sustain"],
    alt: ["length", "rotate", "fill", "quant", "ramp", "pace", "prob", "delay"],
};
export const NOTE_PAGES = [
    SHAPE,
    { name: "Tone",
        keys: ["pitch", "voicing", "range", "scale", "accent", "timing", "random", "channel"],
        alt: ["harmony", "style", "phrase", "root", "groove", "swinggrid", "rate", "output"] },
];
const lanes = (a, p) => Array.from({ length: 8 }, (_, i) => p + (a + i));
export const CC_PAGES = [
    SHAPE,
    { name: "CC A", keys: lanes(1, "cc"), alt: lanes(1, "ccn") },
    { name: "CC B", keys: lanes(9, "cc"), alt: lanes(9, "ccn") },
    { name: "Motion",
        keys: ["ccmode", "slew", null, null, null, "timing", "random", "channel"],
        alt: [null, null, null, null, null, "swinggrid", "rate", "output"] },
];
export const SETTINGS_PAGE = {
    name: "Settings",
    keys: ["dest", "clock", "bpm", "clockout", "quant", "type", "tail", "phraserate"],
    alt: [null, null, null, null, null, "mute", "dir", null],
};

export const DEFAULT_NAMES = ["KICK", "SNARE", "HAT", "CLAP", "PERC", "TOM", "RIM", "SHAKE",
    "BASS", "CHORD", "LEAD", "CC 1", "CC 2", "CC 3", "CC 4", "CC 5"];

/* Perform pads, upper row first. "hold" is momentary, "shot" fires once. */
export const PERFORM = [
    { label: "FILL", move: "fill", value: 1, kind: "hold", color: "orange" },
    { label: "RATCHET 2", move: "ratchet", value: 2, kind: "hold", color: "red" },
    { label: "RATCHET 3", move: "ratchet", value: 3, kind: "hold", color: "red" },
    { label: "RATCHET 4", move: "ratchet", value: 4, kind: "hold", color: "red" },
    { label: "HALF TIME", move: "speed", value: -1, kind: "hold", color: "green" },
    { label: "DOUBLE TIME", move: "speed", value: 1, kind: "hold", color: "green" },
    { label: "REVERSE", move: "reverse", value: 1, kind: "hold", color: "pink" },
    { label: "ACCENT", move: "accent", value: 1, kind: "hold", color: "orange" },
    { label: "FREEZE 1", move: "freeze", value: 1, kind: "hold", color: "blue" },
    { label: "FREEZE 2", move: "freeze", value: 2, kind: "hold", color: "blue" },
    { label: "FREEZE 4", move: "freeze", value: 4, kind: "hold", color: "blue" },
    { label: "FREEZE 8", move: "freeze", value: 8, kind: "hold", color: "blue" },
    { label: "SOLO", move: "solo", value: 1, kind: "hold", color: "yellow" },
    { label: "RESTART", move: "restart", value: 0, kind: "shot", color: "teal" },
    { label: "ROTATE +1", move: "rotate", value: 1, kind: "shot", color: "teal" },
    { label: "NEW DICE", move: "dice", value: 0, kind: "shot", color: "teal" },
];

export function pagesFor(isCC) { return isCC ? CC_PAGES : NOTE_PAGES; }
export function pageIndex(pages, name) {
    const i = pages.findIndex((p) => p.name === name);
    return i < 0 ? 0 : i;
}

const isLane = (k) => /^cc\d+$/.test(k);
const isLaneNum = (k) => /^ccn\d+$/.test(k);
const isRnd = (k) => k.slice(0, 2) === "r.";

export function metaFor(key) {
    if (!key) return null;
    if (isRnd(key)) {
        const m = metaFor(key.slice(2));
        return m ? { key, name: m.name, type: "int", min: -100, max: 100, div: 1 } : null;
    }
    if (isLane(key)) return { key, name: "CC", type: "int", min: -1, max: 127, div: 1, rnd: 1 };
    if (isLaneNum(key)) return { key, name: "CC NO", type: "int", min: 0, max: 127, div: 3 };
    return TRACK_PARAMS.find((p) => p.key === key) || GLOBAL_PARAMS.find((p) => p.key === key) || null;
}

/* Can this key take a random amount? */
export function randomKey(key) {
    const m = metaFor(key);
    return m && m.rnd ? "r." + key : null;
}

export function paramDefs(snap) {
    const out = TRACK_PARAMS.concat(GLOBAL_PARAMS);
    for (const p of TRACK_PARAMS) if (p.rnd) out.push({ key: "r." + p.key, name: p.name, type: "int", min: -100, max: 100 });
    for (let i = 0; i < 16; i++) {
        const num = snap && snap.cn ? snap.cn[i] : 70 + i;
        out.push({ key: "cc" + (i + 1), name: "CC" + num, type: "int", min: -1, max: 127 });
        out.push({ key: "ccn" + (i + 1), name: "NO " + (i + 1), type: "int", min: 0, max: 127 });
        out.push({ key: "r.cc" + (i + 1), name: "CC" + num, type: "int", min: -100, max: 100 });
    }
    return out;
}

/* Snapshot ("ui" JSON from the DSP) -> {key: wire string}. */
export function valuesFrom(snap, pkeys) {
    const v = {};
    if (!snap) return v;
    (pkeys || []).forEach((k, i) => {
        const m = metaFor(k);
        const n = snap.p ? snap.p[i] : 0;
        v[k] = m && m.type === "enum" ? m.options[n] : String(n);
        if (snap.rn) v["r." + k] = String(snap.rn[i] | 0);
    });
    for (let i = 0; i < 16; i++) {
        v["cc" + (i + 1)] = String(snap.cv ? snap.cv[i] : -1);
        v["ccn" + (i + 1)] = String(snap.cn ? snap.cn[i] : 70 + i);
        v["r.cc" + (i + 1)] = String(snap.cr ? snap.cr[i] : 0);
    }
    v.dest = DESTS[snap.d | 0];
    v.clock = CLOCKS[snap.ck | 0];
    v.bpm = String(Math.round(snap.bpm || 120));
    v.clockout = snap.co ? "On" : "Off";
    v.quant = QUANTS[snap.qn === undefined ? 2 : snap.qn | 0];
    return v;
}

const SIGNED = new Set(["prob", "ramp", "pace", "delay", "pitch", "harmony", "voicing", "range", "accent", "timing"]);

export function displayFor(key, raw) {
    if (raw === undefined || raw === null) return null;
    const n = parseInt(raw, 10);
    if (isLane(key) && n < 0) return "OFF";
    if (key === "repeats" && n >= REPEATS_INF) return "INF";
    if ((SIGNED.has(key) || isRnd(key)) && n > 0) return "+" + raw;
    return null;
}

/* ------------------------------------------------------------ colours */

/* Move pad palette indices, in one place so they can be tuned. */
export const COL = {
    off: 0, white: 120, dim: 124, red: 1, green: 8, orange: 47, amber: 9,
    teal: 87, blue: 95, purple: 104, yellow: 13, pink: 25, grey: 118,
};

/* One colour per layer, on its left button and on its pads. White always
 * means "this one, now". Red always means "editing one cycle". */
export const LAYER_COL = { pattern: COL.green, cycles: COL.blue, temp: COL.yellow, perform: COL.pink };

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

/* ------------------------------------------------------- the 16 value pads */

/* Which thing the bottom pads show right now. Held layers win over the
 * knob under your finger. */
export function padMode(st) {
    if (st.layer === "pattern") return "pattern";
    if (st.layer === "cycles") return "cycles";
    if (st.layer === "perform") return "perform";
    if (st.padKey === "cycles") return "cycles";
    return "param";
}

/* Keyboard layout: white keys along the lower row, black keys above and
 * between them, octave buttons at the right end of each row. */
const KB_WHITE = [0, 2, 4, 5, 7, 9, 11];
const KB_BLACK = { 1: 1, 2: 3, 4: 6, 5: 8, 6: 10 };
export function keyboardNote(slot) {
    if (slot >= 8 && slot <= 14) return KB_WHITE[slot - 8];
    if (slot in KB_BLACK) return KB_BLACK[slot];
    return -1;
}

const DIV_SLOTS = { 0: 0, 1: 1, 2: 2, 3: 3, 4: 4, 5: 5, 6: 6, 10: 7, 11: 8, 12: 9, 13: 10, 14: 11, 7: 12, 8: 13, 9: 14 };
const COARSE = [-1, -0.5, -0.25, 0, 0.25, 0.5, 1];
const NUDGE = [-5, -2, -1, null, 1, 2, 5];

const set = (key, value, label) => ({ k: "set", key, value: String(value), label });

/* What each of the 16 pads does for the current parameter.
 * Returns { title, pads: [{ c, act }] } with act null for an unused pad. */
export function padModel(st) {
    const mode = padMode(st);
    const s = st.snap || {};
    const blink = Math.floor((st.now || 0) / 200) % 2 === 0;
    const pads = Array.from({ length: 16 }, () => ({ c: COL.off, act: null }));
    const v = st.values || {};

    if (mode === "pattern") {
        for (let i = 0; i < 16; i++) {
            const filled = st.patterns && st.patterns[i];
            let c = filled ? COL.green : COL.dim;
            if (st.current === i) c = COL.white;
            if (st.queued === i) c = blink ? COL.white : COL.green;
            pads[i] = { c, act: { k: "pattern", i } };
        }
        return { title: "PATTERNS", pads };
    }
    if (mode === "perform") {
        for (let i = 0; i < 16; i++) {
            const p = PERFORM[i];
            const held = st.perfHeld && st.perfHeld[i], latched = st.latched && st.latched[i];
            pads[i] = { c: held || (latched && blink) ? COL.white : COL[p.color], act: { k: "perf", i } };
        }
        return { title: "PERFORM", pads };
    }
    if (mode === "cycles") {
        const n = parseInt(v.cycles, 10) || 1;
        const em = s.em | 0, ce = s.ce | 0;
        for (let i = 0; i < 16; i++) {
            let c = i < n ? COL.blue : COL.off;
            if ((ce >> i) & 1) c = i < n ? COL.purple : COL.dim;   /* differs from the base */
            if (s.r && !em && i === (s.pc | 0)) c = COL.white;
            if ((em >> i) & 1) c = blink ? COL.white : COL.red;
            pads[i] = { c, act: { k: "cycle", i } };
        }
        return { title: "CYCLES", pads };
    }

    const key = st.padKey;
    const m = metaFor(key);
    if (!m) return { title: "", pads };
    const cur = v[key];
    const curN = parseInt(cur, 10);
    const title = m.name.toUpperCase();
    const page = st.padPage | 0;

    if (key === "pitch" || key === "root") {
        const scale = SCALE_DEG[Math.max(0, SCALES.indexOf(v.scale))];
        const root = Math.max(0, ROOTS.indexOf(v.root));
        const oct = st.kbOct | 0;
        const pool = new Set(s.po || []);
        for (let i = 0; i < 16; i++) {
            const pc = keyboardNote(i);
            if (pc < 0) continue;
            const black = i < 8;
            if (key === "root") {
                pads[i] = { c: pc === root ? COL.white : (black ? COL.blue : COL.grey), act: set("root", ROOTS[pc], "ROOT " + ROOTS[pc]) };
                continue;
            }
            const note = 12 * (oct + 2) + pc;
            const inScale = scale.includes(((pc - root) % 12 + 12) % 12);
            if (note > 127) continue;
            let c = inScale ? (black ? COL.blue : COL.grey) : COL.off;
            if (pool.has(note)) c = COL.orange;
            pads[i] = { c, act: inScale || pool.has(note) ? { k: "pool", note } : null };
        }
        if (key === "pitch") {
            pads[7] = { c: oct > 3 ? COL.white : COL.pink, act: { k: "oct", d: 1 } };
            pads[15] = { c: oct < 3 ? COL.white : COL.pink, act: { k: "oct", d: -1 } };
        }
        return { title: key === "pitch" ? "NOTES C" + oct : "ROOT", pads };
    }

    if (key === "division" || key === "time" || key === "rate") {
        for (const slot in DIV_SLOTS) {
            const idx = DIV_SLOTS[slot];
            const c = DIVISIONS[idx] === cur ? COL.white : (idx < 7 ? COL.blue : (idx < 12 ? COL.purple : COL.teal));
            pads[slot] = { c, act: set(key, DIVISIONS[idx], m.name + " " + DIVISIONS[idx]) };
        }
        return { title, pads };
    }

    if (key === "style") {
        [0, 1, 2, 8, 9, 10].forEach((slot, i) => {
            pads[slot] = { c: STYLES[i] === cur ? COL.white : COL.green, act: set("style", STYLES[i], STYLES[i]) };
        });
        dirPads(pads, v);
        return { title, pads };
    }
    if (key === "phrase" || key === "groove") {
        const opts = key === "phrase" ? PHRASES : GROOVES;
        [0, 1, 2, 3, 8, 9, 10, 11].forEach((slot, i) => {
            pads[slot] = { c: opts[i] === cur ? COL.white : COL.green, act: set(key, opts[i], opts[i]) };
        });
        if (key === "phrase") ratePads(pads, v);
        return { title, pads };
    }

    if (m.type === "enum") {
        m.options.slice(0, 16).forEach((o, i) => {
            pads[i] = { c: o === cur ? COL.white : COL.blue, act: set(key, o, m.name + " " + o) };
        });
        return { title, pads };
    }

    if (key === "repeats") {
        const slots = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14];
        slots.forEach((slot, i) => {
            const n = page * 14 + i;
            if (n >= REPEATS_INF) return;
            pads[slot] = { c: n === curN ? COL.white : COL.orange, act: set(key, n, "REPEATS " + n) };
        });
        pads[15] = { c: curN >= REPEATS_INF ? COL.white : COL.blue, act: set(key, REPEATS_INF, "REPEATS INF") };
        pads[7] = { c: v.tail === "Tail" ? COL.white : COL.pink, act: set("tail", v.tail === "Tail" ? "Choke" : "Tail", v.tail === "Tail" ? "CHOKE" : "TAIL") };
        return { title, pads };
    }

    const span = m.max - m.min;
    if (m.min < 0) {
        /* Bipolar: lower row jumps to coarse amounts with zero in the
         * middle, upper row nudges, as on the T-1. */
        COARSE.forEach((f, i) => {
            const val = Math.round(f * m.max);
            pads[8 + i] = { c: val === curN ? COL.white : (f === 0 ? COL.grey : COL.orange), act: set(key, val, m.name + " " + val) };
        });
        NUDGE.forEach((d, i) => {
            if (d === null) pads[i] = { c: COL.grey, act: set(key, 0, m.name + " 0") };
            else pads[i] = { c: COL.blue, act: { k: "nudge", key, d } };
        });
        if (key === "voicing") dirPads(pads, v);
        if (key === "range") ratePads(pads, v);
        if (key === "timing") {
            pads[7] = { c: v.swinggrid === "x2" ? COL.white : COL.pink, act: set("swinggrid", v.swinggrid === "x2" ? "x1" : "x2", "GRID " + (v.swinggrid === "x2" ? "X1" : "X2")) };
            pads[15] = { c: v.swinggrid === "x4" ? COL.white : COL.pink, act: set("swinggrid", v.swinggrid === "x4" ? "x1" : "x4", "GRID " + (v.swinggrid === "x4" ? "X1" : "X4")) };
        }
        return { title, pads };
    }

    /* Unipolar. Short ranges get one pad per value, paged with +/-. */
    let vals;
    if (isLane(key)) vals = [-1].concat(Array.from({ length: 15 }, (_, i) => Math.round(i * 127 / 14)));
    else if (key === "bpm") vals = [60, 70, 80, 90, 100, 110, 115, 120, 125, 130, 135, 140, 150, 160, 170, 180];
    else if (span <= 16) vals = Array.from({ length: Math.min(16, span + 1) }, (_, i) => m.min + i);
    else if (span <= 64) vals = Array.from({ length: 16 }, (_, i) => m.min + page * 16 + i).filter((x) => x <= m.max);
    else vals = Array.from({ length: 16 }, (_, i) => Math.round(m.min + span * (i + 1) / 16));
    vals.forEach((val, i) => {
        const lab = isLane(key) && val < 0 ? "OFF" : String(val);
        pads[i] = { c: val === curN ? COL.white : COL.blue, act: set(key, val, m.name + " " + lab) };
    });
    return { title, pads };
}

function dirPads(pads, v) {
    pads[7] = { c: v.dir === "Up" ? COL.white : COL.pink, act: set("dir", "Up", "DIRECTION UP") };
    pads[15] = { c: v.dir === "Down" ? COL.white : COL.pink, act: set("dir", "Down", "DIRECTION DOWN") };
}
function ratePads(pads, v) {
    pads[7] = { c: v.phraserate === "x2" ? COL.white : COL.pink, act: set("phraserate", v.phraserate === "x2" ? "x1" : "x2", "PHRASE " + (v.phraserate === "x2" ? "X1" : "X2")) };
    pads[15] = { c: v.phraserate === "/2" ? COL.white : COL.pink, act: set("phraserate", v.phraserate === "/2" ? "x1" : "/2", "PHRASE " + (v.phraserate === "/2" ? "X1" : "/2")) };
}

/* How many pad pages a parameter has (for the +/- buttons). */
export function padPages(key) {
    const m = metaFor(key);
    if (!m || m.type === "enum" || m.min < 0) return 1;
    if (key === "repeats") return 4;
    const span = m.max - m.min;
    return span > 16 && span <= 64 ? Math.ceil((span + 1) / 16) : 1;
}

/* ------------------------------------------------------------ LEDs */

export function ledFrame(st) {
    const out = {};
    const s = st.snap || {};
    const mu = s.mu | 0, pf = s.pf | 0;
    const now = st.now || 0;
    const blink = Math.floor(now / 200) % 2 === 0;
    for (let t = 0; t < 16; t++) {
        const muted = (mu >> t) & 1, soloed = (pf >> (16 + t)) & 1;
        const hit = st.flashUntil && st.flashUntil[t] > now;
        let c = muted || soloed ? COL.dim : typeColor(s, t);
        if (t === st.focus) c = hit && !muted ? typeColor(s, t) : COL.white;
        else if (hit && !muted && !soloed) c = COL.white;
        if (st.held && st.held.has(t) && t !== st.focus) c = COL.white;
        if (((st.mutePend | 0) >> t) & 1) c = blink ? COL.red : COL.off;
        if (st.copySrc === t && blink) c = COL.off;
        out[trackPadNote(t)] = c;
    }

    const model = padModel(st);
    for (let i = 0; i < 16; i++) out[slotNote(i)] = model.pads[i].c;

    /* Step row: the cycle being edited, else the one playing. */
    const steps = s.sn | 0 || parseInt((st.values || {}).steps, 10) || 16;
    let hits = 0n;
    try { hits = BigInt("0x" + (s.h || "0")); } catch (e) { hits = 0n; }
    const cur = s.st ? s.st[st.focus] : -1;
    for (let i = 0; i < 16; i++) {
        const idx = (st.stepPage | 0) * 16 + i;
        let c = COL.off;
        if (idx < steps) {
            const on = (hits >> BigInt(idx)) & 1n;
            c = on ? ((s.em | 0) ? COL.red : typeColor(s, st.focus)) : COL.dim;
            if (s.r && idx === cur) c = on ? COL.white : COL.green;
        }
        out[STEP_BASE + i] = c;
    }
    return out;
}

/* ---------------------------------------------------------- screen */

export function currentPages(st) {
    const s = st.snap || {};
    return st.settings ? [SETTINGS_PAGE] : pagesFor(isCCBit(s, st.focus));
}

export function currentPage(st) {
    const pages = currentPages(st);
    return st.settings ? SETTINGS_PAGE : pages[pageIndex(pages, st.pageName)];
}

/* The eight keys under the knobs right now: Shift shows the second layer,
 * holding Random shows the random amounts of whichever layer is up. */
export function knobKeys(st) {
    const pg = currentPage(st);
    let keys = st.shift ? pg.alt : pg.keys;
    if (st.randomHeld) keys = keys.map((k) => (k ? randomKey(k) : null));
    return keys;
}

export function trackName(st, t) {
    const s = st.snap || {};
    return ((s.nm && s.nm[t]) || DEFAULT_NAMES[t]).toUpperCase();
}

function cut(str, n) { str = String(str); return str.length > n ? str.slice(0, n) : str; }

export function footerFor(st) {
    const s = st.snap || {};
    const now = st.now || 0;
    if (st.warn) return [["!", st.warn]];
    if (st.toast && st.toast.until > now) return [[st.toast.tag, st.toast.text]];
    const out = [];
    if (s.tmp) out.push(["TEMP", ""]);
    if (s.em | 0) {
        const ids = []; for (let c = 0; c < 16; c++) if (((s.em | 0) >> c) & 1) ids.push(c + 1);
        out.push(["EDIT", "CYC " + cut(ids.join("+"), 8)]);
        return out;
    }
    if (st.queued >= 0 && st.queued !== undefined && st.queued !== null) out.push(["NEXT", "P" + (st.queued + 1)]);
    else if (st.current >= 0 && st.patterns && st.patterns[st.current]) out.push(["P" + (st.current + 1), cut(st.patterns[st.current].name || "", 7).toUpperCase()]);
    const n = parseInt((st.values || {}).cycles, 10) || 1;
    if (!s.tmp) out.push(["C", ((s.pc | 0) + 1) + "/" + n]);
    out.push(["", s.ck === 2 ? "EXT" : String(Math.round(s.bpm || 120))]);
    return out;
}

/* Arguments for renderPageMovy. lib = { PAGE_KNOBS, buildMetaIndex }. */
export function pageOptions(st, lib) {
    const s = st.snap || {};
    const pages = currentPages(st);
    const pi = st.settings ? 0 : pageIndex(pages, st.pageName);
    const pg = pages[pi];
    const keys = knobKeys(st);
    let title = st.settings ? "E-1" : "T" + (st.focus + 1) + " " + cut(trackName(st, st.focus), 8);
    if (!st.settings && st.held && st.held.size > 1) title = st.held.size + " TRACKS";
    let label = pg.name;
    if (st.randomHeld) label = "Random";
    else if (st.shift) label = pg.name + " 2";
    return {
        page: { kind: lib.PAGE_KNOBS, name: label, level: "root", keys },
        metaIndex: lib.buildMetaIndex({ chainParams: paramDefs(s) }),
        values: st.values,
        title,
        pageLabel: label,
        pageIndex: pi,
        pageCount: pages.length,
        touched: st.touched,
        viz: [],
        displayFor: (k, v) => displayFor(k, v),
        footer: footerFor(st),
    };
}

/* A 4 x 4 grid of short labels that mirrors the bottom pads, shown while a
 * left button is held or latched. cells: { txt, on (inverted), box (outlined) } */
function drawGrid(ctx, st, head, right, cells, hint) {
    const now = st.now || 0;
    ctx.fillRect(0, 0, 128, 64, 0);
    ctx.print(1, 1, head, 1);
    ctx.print(127 - ctx.textWidth(right), 1, right, 1);
    ctx.fillRect(0, 9, 128, 1, 1);
    for (let i = 0; i < 16; i++) {
        const x = (i % 4) * 32, y = 11 + Math.floor(i / 4) * 11;
        const c = cells[i] || { txt: "" };
        if (c.on) ctx.fillRect(x, y, 31, 10, 1);
        else if (c.box) { ctx.fillRect(x, y, 31, 1, 1); ctx.fillRect(x, y + 9, 31, 1, 1); ctx.fillRect(x, y, 1, 10, 1); ctx.fillRect(x + 30, y, 1, 10, 1); }
        ctx.print(x + 2, y + 2, cut(c.txt, 5), c.on ? 0 : 1);
    }
    const foot = st.toast && st.toast.until > now ? st.toast.tag + " " + st.toast.text : hint;
    ctx.print(1, 56, cut(foot, 21), 1);
}

const PERF_SHORT = ["FILL", "RAT2", "RAT3", "RAT4", "HALF", "DBL", "REV", "ACC", "FRZ1", "FRZ2", "FRZ4", "FRZ8", "SOLO", "RSTRT", "ROT+", "DICE"];

/* The screen for a held or latched layer. Returns false when there is none. */
export function drawLayer(ctx, st) {
    const s = st.snap || {};
    const blink = Math.floor((st.now || 0) / 200) % 2 === 0;
    if (st.layer === "pattern") {
        const cells = Array.from({ length: 16 }, (_, i) => {
            const p = st.patterns && st.patterns[i];
            return { txt: p ? (p.name || "P" + (i + 1)).toUpperCase() : "-", on: st.current === i, box: st.queued === i && blink };
        });
        drawGrid(ctx, st, "PATTERNS", cut((st.setName || "").toUpperCase(), 11), cells,
            st.shift ? "TAP SAVES HERE" : "TAP PLAY  SHIFT SAVE");
        return true;
    }
    if (st.layer === "perform") {
        const cells = PERF_SHORT.map((txt, i) => ({ txt, on: st.perfHeld && st.perfHeld[i], box: st.latched && st.latched[i] }));
        const n = st.held ? st.held.size : 0;
        drawGrid(ctx, st, "PERFORM", n ? n + (n === 1 ? " TRACK" : " TRACKS") : "ALL TRACKS", cells,
            st.shift ? "TAP LATCHES" : "HOLD  SHIFT LATCHES");
        return true;
    }
    if (st.layer === "cycles") {
        const n = parseInt((st.values || {}).cycles, 10) || 1;
        const em = s.em | 0, ce = s.ce | 0;
        const cells = Array.from({ length: 16 }, (_, i) => ({
            txt: i < n ? (i + 1) + (((ce >> i) & 1) ? "*" : "") : "-",
            on: (em >> i) & 1, box: !em && s.r && i === (s.pc | 0),
        }));
        drawGrid(ctx, st, "CYCLES", "T" + (st.focus + 1) + " " + cut(trackName(st, st.focus), 8), cells,
            st.shift ? "TAP SETS HOW MANY" : (em ? "EDITING  TAP TO END" : "TAP EDITS  * CHANGED"));
        return true;
    }
    return false;
}

/* ------------------------------------------------------------ knob marks */

/* Which of the eight knobs owns the bottom pads right now, or -1. */
export function selectedKnob(st) {
    const keys = knobKeys(st);
    const base = (k) => (k && k.slice(0, 2) === "r." ? k.slice(2) : k);
    return keys.findIndex((k) => k && base(k) === st.padKey);
}

/* Corner marks around the selected knob's cell, drawn over the page. */
export function drawSelection(ctx, st) {
    const i = selectedKnob(st);
    if (i < 0 || st.touched === i) return;        /* a touched knob is already inverted */
    const x = (i % 4) * 32, y = i < 4 ? 11 : 34, w = 32, h = 21;
    for (const [cx, cy, dx, dy] of [[x, y, 1, 1], [x + w - 1, y, -1, 1], [x, y + h - 1, 1, -1], [x + w - 1, y + h - 1, -1, -1]]) {
        const arm = dy > 0 ? 4 : 2;               /* short at the bottom, clear of the label */
        ctx.fillRect(Math.min(cx, cx + dx * 3), cy, 4, 1, 1);
        ctx.fillRect(cx, Math.min(cy, cy + dy * (arm - 1)), 1, arm, 1);
    }
}

/* Knob LEDs: upper row of four in one colour, lower row in another, the
 * selected knob white, unused knobs off. */
export function knobLeds(st) {
    const keys = knobKeys(st);
    const sel = selectedKnob(st);
    return keys.map((k, i) => (!k ? COL.off : i === sel ? COL.white : i < 4 ? COL.teal : COL.orange));
}

/* ------------------------------------------------------------ value list */

/* The options of a named value, shown while its knob turns. */
export function listFor(st) {
    const key = st.listKey;
    const m = key ? metaFor(key) : null;
    if (!m || !m.options) return null;
    const cur = Math.max(0, m.options.indexOf((st.values || {})[key]));
    return { title: m.name.toUpperCase(), options: m.options, cur };
}

export function drawValueList(ctx, st) {
    const l = listFor(st);
    if (!l) return false;
    const n = l.options.length;
    const ROWS = 5, ROW_H = 9, TOP = 11;
    const cols = n > ROWS ? 2 : 1;
    const per = ROWS * cols;
    const first = Math.floor(l.cur / per) * per;
    const colW = cols === 2 ? 62 : 124;
    ctx.fillRect(0, 0, 128, 64, 0);
    ctx.print(1, 1, l.title, 1);
    const who = st.settings && GLOBAL_KEYS.has(st.listKey) ? "E-1" : "T" + (st.focus + 1) + " " + cut(trackName(st, st.focus), 8);
    ctx.print(127 - ctx.textWidth(who), 1, who, 1);
    ctx.fillRect(0, 9, 128, 1, 1);
    for (let j = 0; j < per && first + j < n; j++) {
        const i = first + j;
        const x = Math.floor(j / ROWS) * 64, y = TOP + (j % ROWS) * ROW_H;
        const on = i === l.cur;
        if (on) ctx.fillRect(x, y, colW, ROW_H, 1);
        ctx.print(x + 3, y + 1, cut(String(l.options[i]).toUpperCase(), cols === 2 ? 9 : 20), on ? 0 : 1);
    }
    if (n > per) {                                /* where this page sits in the whole list */
        const pages = Math.ceil(n / per), pg = Math.floor(l.cur / per);
        const h = Math.max(4, Math.floor(45 / pages));
        ctx.fillRect(126, TOP + Math.floor((45 - h) * pg / (pages - 1)), 2, h, 1);
    }
    const foot = n > per ? (l.cur + 1) + "/" + n : "";
    ctx.print(1, 57, "TURN, OR TAP A PAD", 1);
    ctx.print(127 - ctx.textWidth(foot), 57, foot, 1);
    return true;
}
