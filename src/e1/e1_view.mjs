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
export const SHIFT_MODES = ["Hold", "Latch"];
export const QUANTS = ["1/4", "1/2", "1 bar", "2 bars", "4 bars", "8 bars"];
export const STYLES = ["PolyFix", "PolyRamp", "PolyClimb", "MonoFix", "MonoRamp", "MonoClimb", "Dice"];
export const PHRASES = ["Cad1", "Cad2", "Cad3", "Cad4", "Saw", "Tri", "Sine", "Pulse", "Dice"];
export const SCALES = ["Chrom", "Major", "Minor", "Penta", "Hira", "Iwato", "Tetra", "Dorian",
    "Phryg", "Lydian", "Mixo", "Locri", "HarMin", "MelMin", "Blues", "MinPen", "Whole"];
export const ROOTS = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
/* A track's Scale and Root lists start with Song (engine value -1): follow the set. */
export const SONG = "Song";
export const TRACK_SCALES = [SONG].concat(SCALES);
export const TRACK_ROOTS = [SONG].concat(ROOTS);
export const FOLLOWS = ["Off", "Bass", "Chord", "Arp"];
/* Chord track and Split: Off, or a track (engine values 0..16). */
export const TRACK_CHOICES = ["Off"].concat(Array.from({ length: 16 }, (_, i) => "T" + (i + 1)));
export const GROOVES = ["Agogo", "Congas", "Timbal", "Bongo", "Saw", "Tri", "Sine", "Pulse", "Dice"];
export const REPEATS_INF = 49;
export const LOOPS = ["Free", "1", "2", "4", "8"];   /* Loop lock: bars after which chance and random come round */
export const AUTOS = ["Off", "1", "2", "4", "8"];    /* Auto roll: bars between new dice */
export const DIRS = ["Forward", "Backward", "Pendulum", "Random"];
/* Source: how the random amounts move. Short names for the footer. */
export const SOURCES = ["Random", "Drift", "RampUp", "RampDn", "Tri", "Square"];
export const SOURCE_SHORT = ["RND", "DRF", "UP", "DN", "TRI", "SQR"];
/* Dice pads, in the order of the engine's layers (bit i of snap.dc / snap.dm). */
export const DICE = [
    { layer: "notes", label: "NOTES", short: "NOTE" },
    { layer: "accents", label: "ACCENTS", short: "ACC" },
    { layer: "lengths", label: "LENGTHS", short: "LEN" },
    { layer: "octaves", label: "OCTAVES", short: "OCT" },
    { layer: "rhythm", label: "RHYTHM", short: "RHY" },
];

const SCALE_DEG = [
    [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11], [0, 2, 4, 5, 7, 9, 11], [0, 2, 3, 5, 7, 8, 10], [0, 2, 4, 7, 9],
    [0, 2, 3, 7, 8], [0, 1, 5, 6, 10], [0, 4, 7, 10], [0, 2, 3, 5, 7, 9, 10],
    [0, 1, 3, 5, 7, 8, 10], [0, 2, 4, 6, 7, 9, 11], [0, 2, 4, 5, 7, 9, 10], [0, 1, 3, 5, 6, 8, 10],
    [0, 2, 3, 5, 7, 8, 11], [0, 2, 3, 5, 7, 9, 11], [0, 3, 5, 6, 7, 10], [0, 3, 5, 7, 10], [0, 2, 4, 6, 8, 10],
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
    { key: "scale", name: "Scale", type: "enum", options: TRACK_SCALES, base: -1, div: 6 },
    { key: "root", name: "Root", type: "enum", options: TRACK_ROOTS, base: -1, div: 5 },
    { key: "accent", name: "Accent", type: "int", min: -100, max: 100, div: 1, rnd: 1 },
    { key: "groove", name: "Groove", type: "enum", options: GROOVES, div: 6 },
    { key: "timing", name: "Timing", type: "int", min: -50, max: 50, div: 1, rnd: 1 },
    { key: "swinggrid", name: "Grid", type: "enum", options: ["x1", "x2", "x4"], div: 8 },
    { key: "random", name: "Random", type: "int", min: 0, max: 100, div: 1 },
    { key: "rate", name: "Rate", type: "enum", options: DIVISIONS, div: 5 },
    { key: "channel", name: "Chan", type: "int", min: 1, max: 16, div: 4 },
    { key: "output", name: "Output", type: "enum", options: ["Global", "Move", "Computer", "Both"], div: 8 },
    { key: "mute", name: "Mute", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "dir", name: "Dir", type: "enum", options: DIRS, div: 6 },
    { key: "tail", name: "Repeat", type: "enum", options: ["Choke", "Tail"], div: 8 },
    { key: "phraserate", name: "PhrRate", type: "enum", options: ["x1", "x2", "/2"], div: 8 },
    { key: "ccmode", name: "Send", type: "enum", options: ["Pulse", "Change", "Glide"], div: 8 },
    { key: "slew", name: "Slew", type: "int", min: 0, max: 100, div: 1 },
    { key: "loop", name: "Loop", type: "enum", options: LOOPS, div: 8 },
    { key: "density", name: "Density", type: "int", min: 0, max: 16, div: 4 },
    { key: "dicetype", name: "DiceTyp", type: "int", min: 1, max: 16, div: 4 },
    { key: "autoroll", name: "AutoRol", type: "enum", options: AUTOS, div: 8 },
    { key: "dicelen", name: "DiceLen", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "follow", name: "Follow", type: "enum", options: FOLLOWS, div: 8 },
    { key: "split", name: "Split", type: "enum", options: TRACK_CHOICES, div: 4 },
    { key: "source", name: "Source", type: "enum", options: SOURCES, div: 6 },
    { key: "fit", name: "Fit", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "warp", name: "Warp", type: "int", min: -100, max: 100, div: 1 },
    { key: "jitter", name: "Jitter", type: "int", min: 0, max: 100, div: 1 },
];

export const GLOBAL_PARAMS = [
    { key: "dest", name: "Output", type: "enum", options: DESTS, div: 8 },
    { key: "clock", name: "Clock", type: "enum", options: CLOCKS, div: 8 },
    { key: "bpm", name: "Tempo", type: "int", min: 20, max: 300, div: 1 },
    { key: "clockout", name: "ClkOut", type: "enum", options: ["Off", "On"], div: 8 },
    { key: "quant", name: "Quant", type: "enum", options: QUANTS, div: 6 },
    { key: "songroot", name: "SongRoot", type: "enum", options: ROOTS, div: 5 },
    { key: "songscale", name: "SongScale", type: "enum", options: SCALES, div: 6 },
    { key: "chordtrack", name: "ChordTrk", type: "enum", options: TRACK_CHOICES, div: 4 },
    /* Kept by the UI in session.json, not by the DSP: how Shift behaves on this Move. */
    { key: "shiftmode", name: "Shift", type: "enum", options: SHIFT_MODES, div: 8 },
];
export const GLOBAL_KEYS = new Set(GLOBAL_PARAMS.map((p) => p.key));

/* Knob pages. The T-1 has two rows of nine knobs. Move has eight, shown on
 * screen as two rows of four, so the two pages are the left and the right half
 * of the T-1 panel with every knob where the T-1 has it:
 *
 *   Left    STEPS   PULSES  CYCLES   DIVISION      Right   VELOCITY SUSTAIN PITCH  LENGTH
 *           REPEATS TIME    VOICING  RANGE                 ACCENT   TIMING  SCALE  CHANNEL
 *
 * `alt` is what Shift (the T-1's Ctrl) gives, the same pairing the T-1 prints
 * under each knob. The T-1's ninth column is elsewhere: Tempo on Shift + jog,
 * Random on Sample + jog. Fill is E-1's own and sits in an empty Ctrl slot. */
const LEFT = {
    name: "Left",
    keys: ["steps", "pulses", "cycles", "division", "repeats", "time", "voicing", "range"],
    alt: ["fill", "rotate", "dicetype", "density", "ramp", "pace", "style", "phrase"],
};
export const NOTE_PAGES = [
    LEFT,
    { name: "Right",
        keys: ["velocity", "sustain", "pitch", "length", "accent", "timing", "scale", "channel"],
        alt: ["prob", "follow", "harmony", "quant", "groove", "delay", "root", "output"] },
];
const lanes = (a, p) => Array.from({ length: 8 }, (_, i) => p + (a + i));
export const CC_PAGES = [
    { name: "Left",
        keys: ["steps", "pulses", "cycles", "division", "repeats", "time", null, null],
        alt: ["fill", "rotate", null, "density", "ramp", "pace", null, null] },
    { name: "Right",
        keys: ["ccmode", "slew", null, "length", null, "timing", null, "channel"],
        alt: ["prob", null, null, "quant", null, "delay", null, "output"] },
    { name: "CC A", keys: lanes(1, "cc"), alt: lanes(1, "ccn") },
    { name: "CC B", keys: lanes(9, "cc"), alt: lanes(9, "ccn") },
];
/* Settings has two pages, so what belongs to the whole set and what belongs
 * to the selected track never share a screen: "Set" (the header says E-1 SET)
 * and "Track" (the header names the track). Only what no knob page, pad or
 * button already reaches; Shift + Menu opens Track straight away. */
export const SETTINGS_PAGES = [
    { name: "Set",
        keys: ["dest", "clock", "clockout", "bpm", "songroot", "songscale", "chordtrack", "shiftmode"],
        alt: [null, null, null, "quant", null, null, null, null] },
    { name: "Track",
        keys: ["type", "channel", "output", "dir", "scale", "root", "follow", "split"],
        alt: [null, null, null, null, null, null, null, null] },
];
export const TRACK_SETTINGS = SETTINGS_PAGES.length - 1;
export const SETTINGS_PAGE = SETTINGS_PAGES[0];

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
    { label: "RESTART", move: "restart", value: 0, kind: "shot", color: "cyan" },
    { label: "ROTATE +1", move: "rotate", value: 1, kind: "shot", color: "cyan" },
    { label: "NEW DICE", move: "dice", value: 0, kind: "shot", color: "cyan" },
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

export function paramDefs(snap, values) {
    let out = TRACK_PARAMS.concat(GLOBAL_PARAMS);
    /* With Fit on, the Div square reads FIT whatever the division. */
    if (values && values.fit === "On")
        out = out.map((p) => (p.key === "division" ? Object.assign({}, p, { short_options: DIVISIONS.map(() => "FIT") }) : p));
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
        v[k] = m && m.type === "enum" ? m.options[n - (m.base | 0)] : String(n);
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
    v.songroot = ROOTS[snap.sr | 0] || "C";
    v.songscale = SCALES[snap.ss === undefined ? 2 : snap.ss | 0] || "Minor";
    v.chordtrack = TRACK_CHOICES[snap.ct | 0] || "Off";
    return v;
}

const SIGNED = new Set(["prob", "ramp", "pace", "delay", "pitch", "harmony", "voicing", "range", "accent", "timing", "warp"]);

/* What Scale or Root comes to: the track's own, or the song's while it says Song. */
export function resolved(v, key) {
    const own = (v || {})[key];
    return own === SONG || own === undefined ? (v || {})["song" + key] : own;
}
/* "SONG (MINOR)": the Song entry spelled out with what it stands for now. */
export function songLabel(v, key) { return "SONG (" + String((v || {})["song" + key] || "").toUpperCase() + ")"; }

export function displayFor(key, raw, v) {
    if (raw === undefined || raw === null) return null;
    const n = parseInt(raw, 10);
    if ((key === "scale" || key === "root") && raw === SONG) return "S:" + String((v || {})["song" + key] || "").toUpperCase();
    if (isLane(key) && n < 0) return "OFF";
    if (key === "division" && (v || {}).fit === "On") return "FIT";   /* Fit spreads the steps; Division waits */
    if (key === "repeats" && n >= REPEATS_INF) return "INF";
    if ((SIGNED.has(key) || isRnd(key)) && n > 0) return "+" + raw;
    return null;
}

/* ------------------------------------------------------------ colours */

/* Move palette numbers (Schwung shared/constants.mjs), named for the T-1
 * colour they stand in for. The T-1's rules, which the pads follow:
 *   white      the current value, the playing cycle or pattern, the playhead
 *   orange     the selected note track, pulses, amounts, notes in the pool
 *   cyan       the selected CC track and its pulses
 *   grey       there but idle; near black: muted, empty, unused
 *   blue       triplets, black keys, patterns with content; bright blue: chained
 *   green      bars (Length, Quantize), cadences; dark green: styles, grooves
 *   pink       modifier pads at the right end of each row
 *   red        something edited in one cycle
 * Nothing blinks except a queued pattern and a cycle picked for editing.
 */
export const COL = {
    off: 0, white: 120, grey: 123, dim: 124,
    red: 1, dimRed: 65,
    orange: 3, dimOrange: 69, darkOrange: 70, amber: 4, yellow: 8,
    green: 11, darkGreen: 85,
    cyan: 46, dimCyan: 93, darkCyan: 94,
    blue: 17, dimBlue: 97, brightBlue: 16, lightGrey: 118,
    purple: 23, pink: 26, dimPink: 115,
    /* The darkest shade of each hue (palette 65-116), for bottom pads that are there but not on. */
    darkRed: 66, darkYellow: 80, darkestGreen: 86, darkBlue: 98, darkSky: 96, darkPurple: 110, darkPink: 116,
};
/* Bottom pads light the way track pads do: the chosen value, or a line or
 * move that is on, in full colour; the rest of what you can tap in the
 * darkest shade of the same hue; nothing there, off; white only while a pad
 * is held, a pattern or cycle plays, or a perform move is held. */
export const HUE = {
    orange: [COL.orange, COL.darkOrange], red: [COL.red, COL.darkRed], yellow: [COL.yellow, COL.darkYellow],
    green: [COL.green, COL.darkestGreen], cyan: [COL.cyan, COL.darkCyan], blue: [COL.blue, COL.darkBlue],
    sky: [COL.brightBlue, COL.darkSky], purple: [COL.purple, COL.darkPurple], pink: [COL.pink, COL.darkPink],
    grey: [COL.lightGrey, COL.dim],
};
export const lit = (hue, on) => HUE[hue][on ? 0 : 1];
/* Buttons with a white LED take a brightness, not a palette number. */
export const BTN = { off: 0, dim: 16, on: 124 };

/* One colour per layer, on its left button and on its pads. White always
 * means "this one, now". Red always means "editing one cycle". */
export const LAYER_COL = { pattern: COL.blue, cycles: COL.red, random: COL.purple, perform: COL.pink };

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
const typeColor = (s, t) => (isCCBit(s, t) ? COL.cyan : COL.orange);
const typeDim = (s, t) => (isCCBit(s, t) ? COL.dimCyan : COL.dimOrange);
const typeDark = (s, t) => (isCCBit(s, t) ? COL.darkCyan : COL.darkOrange);

/* ------------------------------------------------------- the 16 value pads */

/* Which thing the bottom pads show right now. Held layers win over the
 * knob under your finger. */
export function padMode(st) {
    if (st.layer === "pattern" || st.saveMode) return "pattern";
    if (st.layer === "cycles") return "cycles";
    if (st.layer === "perform") return "perform";
    /* Random up and no knob under a finger: the pads are dice. */
    if (st.layer === "random" && !(st.touched >= 0)) return "dice";
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

const ENUM_HUE = { length: "green", quant: "green", scale: "blue", loop: "green", autoroll: "green", follow: "green", dir: "pink", fit: "pink" };
/* Source pads: one hue per shape so they read at a glance. */
export const SOURCE_HUE = ["purple", "cyan", "green", "yellow", "orange", "sky"];
const SCALE_PADS = 15;                               /* scales per pad page; pad 16 is Song */
/* Dice pads, one hue per line: full while the line plays, darkest while it does not. */
const DICE_HUE = ["orange", "red", "green", "sky", "cyan"];
const set = (key, value, label) => ({ k: "set", key, value: String(value), label });
const trackHue = (s, t) => (isCCBit(s, t) ? "cyan" : "orange");

/* What each of the 16 pads does for the current parameter.
 * Returns { title, pads: [{ c, act }] } with act null for an unused pad. */
export function padModel(st) {
    const mode = padMode(st);
    const s = st.snap || {};
    const blink = Math.floor((st.now || 0) / 200) % 2 === 0;
    const pads = Array.from({ length: 16 }, () => ({ c: COL.off, act: null }));
    const v = st.values || {};

    if (mode === "pattern") {
        /* Saved: dark blue. In the chain: blue. Playing: white. Empty: off,
         * or darkest grey while a tap would save there. */
        const saving = st.saveMode || st.shift;
        for (let i = 0; i < 16; i++) {
            const filled = st.patterns && st.patterns[i];
            let c = filled ? COL.darkBlue : (saving ? COL.dim : COL.off);
            if (filled && st.chain && st.chain.length >= 2 && st.chain.includes(i)) c = COL.blue;
            if (st.current === i && filled) c = COL.white;
            if (st.queued === i) c = blink ? COL.white : COL.blue;
            pads[i] = { c, act: { k: "pattern", i } };
        }
        return { title: "PATTERNS", pads };
    }
    if (mode === "perform") {
        /* Darkest while idle, full colour while latched, white while held. */
        for (let i = 0; i < 16; i++) {
            const p = PERFORM[i];
            const held = st.perfHeld && st.perfHeld[i], latched = st.latched && st.latched[i];
            pads[i] = { c: held ? COL.white : lit(p.color, latched), act: { k: "perf", i } };
        }
        return { title: "PERFORM", pads };
    }
    if (mode === "dice") {
        /* Upper row: one pad per lane, All, Back. Lower row: auto roll, and
         * the track's random dice at the right end. */
        const cc = isCCBit(s, st.focus | 0);
        const dm = s.dm | 0;
        let all = true;
        DICE.forEach((d, i) => {
            if (cc && d.layer !== "rhythm") return;
            const on = ((dm >> i) & 1) === 1;
            if (!on) all = false;
            pads[i] = { c: lit(DICE_HUE[i], on), act: { k: "dice", layer: d.layer, label: d.label } };
        });
        pads[6] = { c: lit("purple", all), act: { k: "dice", layer: "all", label: "ALL" } };
        pads[7] = { c: (s.dh | 0) ? COL.darkPink : COL.off, act: { k: "diceback" } };
        AUTOS.forEach((o, i) => {
            pads[8 + i] = { c: lit(i ? "green" : "grey", o === v.autoroll), act: { k: "autoroll", value: o } };
        });
        pads[15] = { c: COL.darkPink, act: { k: "reroll" } };
        return { title: "DICE", pads };
    }
    if (mode === "cycles") {
        /* In use: dark red. With its own edits: red. Playing: white.
         * Picked for editing: blinks white and red. */
        const n = parseInt(v.cycles, 10) || 1;
        const em = s.em | 0, ce = s.ce | 0;
        for (let i = 0; i < 16; i++) {
            let c = i < n ? lit("red", (ce >> i) & 1) : COL.off;
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

    if (key === "scale" || key === "songscale") {
        /* Blue: the scale that plays. Yellow pad at the end: Song, bright
         * while the track follows the song. Shift + pad sets the song's scale. */
        const track = key === "scale";
        const playing = track ? resolved(v, "scale") : cur;
        for (let i = 0; i < SCALE_PADS; i++) {
            const idx = page * SCALE_PADS + i;
            if (idx >= SCALES.length) break;
            pads[i] = { c: lit("blue", SCALES[idx] === playing), act: set(key, SCALES[idx], m.name + " " + SCALES[idx]) };
        }
        if (track) pads[15] = { c: lit("yellow", cur === SONG), act: set(key, SONG, "Scale " + songLabel(v, "scale")) };
        return { title: track ? "SCALE" : "SONG SCALE", pads };
    }
    if (key === "chordtrack" || key === "split") {
        /* One pad per track in its own hue; the chosen one again turns it off. */
        for (let i = 0; i < 16; i++) {
            const on = m.options.indexOf(cur) === i + 1;
            const self = key === "split" && i === (st.focus | 0);
            pads[i] = { c: self ? COL.off : lit(trackHue(s, i), on),
                act: self ? null : set(key, on ? 0 : i + 1, m.name + (on ? " OFF" : " T" + (i + 1))) };
        }
        return { title: key === "split" ? "SPLIT TO" : "CHORD TRACK", pads };
    }

    if (key === "pitch" || key === "root" || key === "songroot") {
        const scale = SCALE_DEG[Math.max(0, SCALES.indexOf(resolved(v, "scale")))];
        const root = Math.max(0, ROOTS.indexOf(key === "songroot" ? v.songroot : resolved(v, "root")));
        const oct = st.kbOct | 0;
        const pool = new Set(s.po || []);
        for (let i = 0; i < 16; i++) {
            const pc = keyboardNote(i);
            if (pc < 0) continue;
            const black = i < 8;
            const key0 = black ? COL.darkBlue : COL.dim;            /* a key you can tap */
            if (key === "root" || key === "songroot") {
                /* Pink: the root chosen. Yellow: the song's root while the track follows it. */
                const mark = key === "root" && cur === SONG ? COL.yellow : COL.pink;
                pads[i] = { c: pc === root ? mark : key0, act: set(key, ROOTS[pc], "ROOT " + ROOTS[pc]) };
                continue;
            }
            const note = 12 * (oct + 2) + pc;
            const inScale = scale.includes(((pc - root) % 12 + 12) % 12);
            if (note > 127) continue;
            /* Scale notes dark, notes in the pool orange, the rest off. */
            let c = inScale ? key0 : COL.off;
            if (pool.has(note)) c = COL.orange;
            pads[i] = { c, act: inScale || pool.has(note) ? { k: "pool", note } : null };
        }
        if (key === "pitch") {
            /* Octave pads are bright when the pool has notes that way. */
            const lo = 12 * (oct + 2);
            let above = false, below = false;
            for (const n of pool) { if (n >= lo + 12) above = true; if (n < lo) below = true; }
            pads[7] = { c: lit("pink", above), act: { k: "oct", d: 1 } };
            pads[15] = { c: lit("pink", below), act: { k: "oct", d: -1 } };
        }
        if (key === "root") pads[15] = { c: lit("yellow", cur === SONG), act: set("root", SONG, "Root " + songLabel(v, "root")) };
        return { title: key === "pitch" ? "NOTES C" + oct : (key === "root" ? "ROOT" : "SONG ROOT"), pads };
    }

    if (key === "division" || key === "time" || key === "rate") {
        /* Straight orange, triplets blue, dotted purple. */
        for (const slot in DIV_SLOTS) {
            const idx = DIV_SLOTS[slot];
            const hue = idx < 7 ? "orange" : (idx < 12 ? "blue" : "purple");
            pads[slot] = { c: lit(hue, DIVISIONS[idx] === cur), act: set(key, DIVISIONS[idx], m.name + " " + DIVISIONS[idx]) };
        }
        if (key === "rate") pads[15] = { c: COL.darkPink, act: { k: "reroll" } };
        /* Division: the pink pad at the end turns Fit on and off. */
        if (key === "division") pads[15] = { c: lit("pink", v.fit === "On"), act: { k: "fit" } };
        return { title: key === "division" && v.fit === "On" ? "DIV (FIT ON)" : title, pads };
    }

    if (key === "style") {
        [0, 1, 2, 8, 9, 10, 4].forEach((slot, i) => {
            pads[slot] = { c: lit(i === 6 ? "purple" : "green", STYLES[i] === cur), act: set("style", STYLES[i], STYLES[i]) };
        });
        return { title, pads };
    }
    if (key === "source") {
        /* One hue per shape; pink re-rolls (Drift too). */
        SOURCES.forEach((o, i) => {
            pads[i] = { c: lit(SOURCE_HUE[i], o === cur), act: set("source", o, "SOURCE " + o) };
        });
        pads[15] = { c: COL.darkPink, act: { k: "reroll" } };
        return { title, pads };
    }
    if (key === "phrase" || key === "groove") {
        /* Cadences and grooves green, waves yellow, Dice purple. */
        const opts = key === "phrase" ? PHRASES : GROOVES;
        [0, 1, 2, 3, 8, 9, 10, 11, 4].forEach((slot, i) => {
            const hue = i === 8 ? "purple" : (i < 4 ? "green" : "yellow");
            pads[slot] = { c: lit(hue, opts[i] === cur), act: set(key, opts[i], opts[i]) };
        });
        if (key === "phrase") ratePads(pads, v);
        return { title, pads };
    }

    if (m.type === "enum") {
        const hue = ENUM_HUE[key] || "orange";
        m.options.slice(0, 16).forEach((o, i) => {
            pads[i] = { c: lit(hue, o === cur), act: set(key, o, m.name + " " + o) };
        });
        /* Pink at the end of the row: a random start for Length, new dice for Loop. */
        if (key === "length") pads[15] = { c: COL.darkPink, act: { k: "randstart" } };
        if (key === "loop") pads[15] = { c: COL.darkPink, act: { k: "reroll" } };
        return { title, pads };
    }

    if (key === "repeats") {
        const slots = [0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14];
        slots.forEach((slot, i) => {
            const n = page * 14 + i;
            if (n >= REPEATS_INF) return;
            pads[slot] = { c: lit("orange", n === curN), act: set(key, n, "REPEATS " + n) };
        });
        pads[15] = { c: lit("sky", curN >= REPEATS_INF), act: set(key, REPEATS_INF, "REPEATS INF") };
        pads[7] = { c: lit("pink", v.tail === "Tail"), act: set("tail", v.tail === "Tail" ? "Choke" : "Tail", v.tail === "Tail" ? "CHOKE" : "TAIL") };
        return { title, pads };
    }

    const span = m.max - m.min;
    if (m.min < 0) {
        /* Bipolar: lower row jumps to coarse amounts with zero (yellow) in
         * the middle, upper row nudges, as on the T-1. */
        COARSE.forEach((f, i) => {
            const val = Math.round(f * m.max);
            pads[8 + i] = { c: lit(f === 0 ? "yellow" : "orange", val === curN), act: set(key, val, m.name + " " + val) };
        });
        NUDGE.forEach((d, i) => {
            if (d === null) pads[i] = { c: COL.darkYellow, act: set(key, 0, m.name + " 0") };
            else pads[i] = { c: COL.darkOrange, act: { k: "nudge", key, d } };
        });
        if (key === "range") ratePads(pads, v);
        if (key === "timing") {
            pads[7] = { c: lit("pink", v.swinggrid === "x2"), act: set("swinggrid", v.swinggrid === "x2" ? "x1" : "x2", "GRID " + (v.swinggrid === "x2" ? "X1" : "X2")) };
            pads[15] = { c: lit("pink", v.swinggrid === "x4"), act: set("swinggrid", v.swinggrid === "x4" ? "x1" : "x4", "GRID " + (v.swinggrid === "x4" ? "X1" : "X4")) };
        }
        return { title, pads };
    }

    /* Unipolar. Short ranges get one pad per value, paged with +/-. */
    let vals;
    if (isLane(key)) vals = [-1].concat(Array.from({ length: 15 }, (_, i) => Math.round(i * 127 / 14)));
    else if (key === "bpm") vals = [60, 70, 80, 90, 100, 110, 115, 120, 125, 130, 135, 140, 150, 160, 170, 180];
    else if (key === "density") vals = Array.from({ length: 16 }, (_, i) => i + 1);
    else if (span <= 16) vals = Array.from({ length: Math.min(16, span + 1) }, (_, i) => m.min + i);
    else if (span <= 64) vals = Array.from({ length: 16 }, (_, i) => m.min + page * 16 + i).filter((x) => x <= m.max);
    else vals = Array.from({ length: 16 }, (_, i) => Math.round(m.min + span * (i + 1) / 16));
    vals.forEach((val, i) => {
        const lab = isLane(key) && val < 0 ? "OFF" : String(val);
        /* A bar, as the T-1 shows Steps: full colour up to and including the
         * value, darkest above. Channel is a choice, not an amount, so only
         * the choice is bright. A CC lane's Off pad is red. */
        let c;
        if (isLane(key) && val < 0) c = lit("red", curN < 0);
        else if (key === "channel") c = lit("orange", val === curN);
        else c = lit(key === "steps" || key === "cycles" ? "blue" : "orange", val <= curN);
        pads[i] = { c, act: set(key, val, m.name + " " + lab) };
    });
    return { title, pads };
}

function ratePads(pads, v) {
    pads[7] = { c: lit("pink", v.phraserate === "x2"), act: set("phraserate", v.phraserate === "x2" ? "x1" : "x2", "PHRASE " + (v.phraserate === "x2" ? "X1" : "X2")) };
    pads[15] = { c: lit("pink", v.phraserate === "/2"), act: set("phraserate", v.phraserate === "/2" ? "x1" : "/2", "PHRASE " + (v.phraserate === "/2" ? "X1" : "/2")) };
}

/* How many pad pages a parameter has (for the +/- buttons). */
export function padPages(key) {
    const m = metaFor(key);
    if (key === "scale" || key === "songscale") return Math.ceil(SCALES.length / SCALE_PADS);
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
        /* A track that is on shows its colour, dim, and flashes white when it
         * sounds. The selected track is bright. Muted tracks are dark, and
         * grey when selected, so colour always means "this track plays". */
        const sel = t === st.focus || (st.held && st.held.has(t));
        const quiet = muted || soloed;
        let c;
        if (quiet) c = sel ? COL.lightGrey : COL.off;
        else if (hit) c = COL.white;
        else c = sel ? typeColor(s, t) : typeDark(s, t);
        /* Harmony roles: the chord track is blue, tracks following it green. */
        if (!quiet && !hit && !isCCBit(s, t)) {
            if (((s.fo | 0) >> t) & 1) c = sel ? COL.green : COL.darkGreen;
            else if ((s.ct | 0) === t + 1) c = sel ? COL.brightBlue : COL.dimBlue;
        }
        if (((st.mutePend | 0) >> t) & 1) c = COL.red;
        if (st.copySrc === t) c = COL.green;
        out[trackPadNote(t)] = c;
    }

    const model = padModel(st);
    /* A bottom pad under a finger is white while it does something. */
    for (let i = 0; i < 16; i++) {
        const p = model.pads[i];
        out[slotNote(i)] = st.slotsDown && st.slotsDown.has(i) && p.act ? COL.white : p.c;
    }

    /* Step row: the cycle being edited, else the one playing. */
    const steps = s.sn | 0 || parseInt((st.values || {}).steps, 10) || 16;
    let hits = 0n;
    try { hits = BigInt("0x" + (s.h || "0")); } catch (e) { hits = 0n; }
    const cur = s.st ? s.st[st.focus] : -1;
    let sm = 0n, se = 0n, sc = 0n, rt = 0n, lv = hits;
    try { sm = BigInt("0x" + (s.sm || "0")); se = BigInt("0x" + (s.se || "0")); sc = BigInt("0x" + (s.sc || "0")); rt = BigInt("0x" + (s.rt || "0")); } catch (e) { sm = se = sc = rt = 0n; }
    try { if (s.lv !== undefined) lv = BigInt("0x" + (s.lv || "0")); } catch (e) { lv = hits; }
    /* Shift shows every step that carries its own values, hit or not.
     * In cycle edit, yellow marks steps with values for this cycle only.
     * Random or Delete held shows the retrigger steps. */
    const showRt = st.randomHeld || st.delHeld;
    for (let i = 0; i < 16; i++) {
        const idx = (st.stepPage | 0) * 16 + i;
        let c = COL.off;
        if (idx < steps) {
            const on = (hits >> BigInt(idx)) & 1n;
            const thin = on && !((lv >> BigInt(idx)) & 1n);             /* Density took it out */
            c = on ? (thin ? typeDark(s, st.focus) : typeColor(s, st.focus)) : COL.dim;
            const own = (se >> BigInt(idx)) & 1n;
            if (own && on && !thin) c = COL.red;                        /* carries its own values */
            if (s.r && idx === cur) c = on && !thin ? COL.white : COL.lightGrey;
            if (own && st.shift) c = COL.red;
            if ((sc >> BigInt(idx)) & 1n) c = COL.yellow;               /* its own values in this cycle (cycle edit) */
            if (showRt && ((rt >> BigInt(idx)) & 1n)) c = COL.pink;
            if ((sm >> BigInt(idx)) & 1n) c = COL.white;                /* picked for step edit */
            if (st.fzOn) c = idx < (st.fzLen | 0) ? COL.brightBlue : COL.dim;   /* freeze length */
        }
        out[STEP_BASE + i] = c;
    }
    return out;
}

/* ---------------------------------------------------------- screen */

export function currentPages(st) {
    const s = st.snap || {};
    return st.settings ? SETTINGS_PAGES : pagesFor(isCCBit(s, st.focus));
}

const settingsIndex = (st) => Math.max(0, Math.min(SETTINGS_PAGES.length - 1, st.setPage | 0));

export function currentPage(st) {
    const pages = currentPages(st);
    return st.settings ? SETTINGS_PAGES[settingsIndex(st)] : pages[pageIndex(pages, st.pageName)];
}

/* The eight keys under the knobs right now: Shift shows the second layer,
 * holding Random shows the random amounts of whichever layer is up. */
/* Knobs that take no random amount carry the time moves while Random is
 * held: Cycles = Jitter, Division = Warp, Shift + Cycles' slot = Source,
 * Shift + Division's slot = Fit. */
export const RANDOM_EXTRA = { cycles: "jitter", division: "warp", dicetype: "source", density: "fit" };
export function knobKeys(st) {
    const pg = currentPage(st);
    let keys = st.shift ? pg.alt : pg.keys;
    if (st.settings && st.shift) keys = pg.keys.map((k, i) => pg.alt[i] || k);   /* settings: Shift swaps only knob 4 on Set */
    if (st.randomHeld) keys = keys.map((k) => (k ? randomKey(k) || (st.settings ? null : RANDOM_EXTRA[k] || null) : null));
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
    if (st.fzOn) return [["FREEZE", (st.fzLen | 0) + " ST " + ((st.fzMask & 0xFFFF) === 0xFFFF ? "ALL" : "PART")]];
    if (st.randomHeld && !st.settings) {
        const l = (st.values || {}).loop || "Free", a = (st.values || {}).autoroll || "Off";
        const src = SOURCE_SHORT[Math.max(0, SOURCES.indexOf((st.values || {}).source))];
        /* Auto roll only when it is on, so all three fit. */
        const out3 = [["SRC", src], ["LOOP", l === "Free" ? "FREE" : l + "B"]];
        if (a !== "Off") out3.push(["AUTO", a + "B"]);
        return out3;
    }
    if (s.tmp) out.push(["TEMP", ""]);
    if (s.sm && s.sm !== "0") {
        const ids = []; let m = 0n;
        try { m = BigInt("0x" + s.sm); } catch (e) { m = 0n; }
        for (let i = 0; i < 64; i++) if ((m >> BigInt(i)) & 1n) ids.push(i + 1);
        /* In cycle edit the step's values are for that cycle only. */
        const cyc = []; for (let c = 0; c < 16; c++) if (((s.em | 0) >> c) & 1) cyc.push(c + 1);
        out.push(["STEP", cut(ids.join("+"), cyc.length ? 6 : 12) + (cyc.length ? " C" + cut(cyc.join("+"), 5) : "")]);
        return out;
    }
    /* Pitch keyboard: the octave on the pads and the pool notes in it. */
    if (st.padKey === "pitch" && !st.settings && !st.layer && !(s.em | 0)) {
        const oct = st.kbOct | 0, lo = 12 * (oct + 2);
        const NN = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
        const here = [], po = s.po || [];
        let up = 0, dn = 0;
        for (const n of po) { if (n < lo) dn++; else if (n >= lo + 12) up++; else here.push(NN[n - lo]); }
        let text = here.length ? here.join(" ") : "NO NOTES";
        if (dn) text += " -" + dn;
        if (up) text += " +" + up;
        out.push(["OCT " + oct, cut(text, 15)]);
        return out;
    }
    if (s.em | 0) {
        const ids = []; for (let c = 0; c < 16; c++) if (((s.em | 0) >> c) & 1) ids.push(c + 1);
        out.push(["EDIT", "C" + cut(ids.join("+"), 5)]);
        return out;
    }
    /* Harmony role of the selected track, in place of the pattern name. */
    const f = st.focus | 0, ct = s.ct | 0, role = (st.values || {}).follow;
    if (ct === f + 1 && !isCCBit(s, f)) { out.push(["CHORD", "TRACK"]); return out; }
    if (((s.fo | 0) >> f) & 1 && role && role !== "Off") { out.push([role.toUpperCase(), "OF T" + ct]); return out; }
    if (st.queued >= 0 && st.queued !== undefined && st.queued !== null) out.push(["NEXT", "P" + (st.queued + 1)]);
    else if (st.current >= 0 && st.patterns && st.patterns[st.current]) out.push(["P" + (st.current + 1), cut(st.patterns[st.current].name || "", 7).toUpperCase()]);
    if (!out.length) out.push(["", String(Math.round(s.bpm || 120)) + " BPM"]);
    return out;
}

/* Arguments for renderPageMovy. lib = { PAGE_KNOBS, buildMetaIndex }. */
export function pageOptions(st, lib) {
    const s = st.snap || {};
    const pages = currentPages(st);
    const pi = st.settings ? settingsIndex(st) : pageIndex(pages, st.pageName);
    const pg = pages[pi];
    const keys = knobKeys(st);
    let title = st.settings && pi === 0 ? "E-1 SET" : "T" + (st.focus + 1) + " " + cut(trackName(st, st.focus), 8);
    if (!st.settings && st.held && st.held.size > 1) title = st.held.size + " TRACKS";
    let label = pg.name;
    if (((s.em | 0) || (s.sm && s.sm !== "0")) && !st.settings) title = cut(title, 8) + " EDIT";
    if (st.randomHeld) label = "Random";
    else if (st.shift && !(st.settings && !pg.alt.some((k) => k))) label = pg.name + " 2";
    return {
        page: { kind: lib.PAGE_KNOBS, name: label, level: "root", keys },
        metaIndex: lib.buildMetaIndex({ chainParams: paramDefs(s, st.values) }),
        values: st.values,
        title,
        pageLabel: label,
        pageIndex: pi,
        pageCount: pages.length,
        touched: st.touched,
        viz: [],
        displayFor: (k, v) => displayFor(k, v, st.values),
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
        if (c.mark) ctx.fillRect(x + 28, y + 3, 2, 4, c.on ? 0 : 1);   /* chained */
    }
    const foot = st.toast && st.toast.until > now ? st.toast.tag + " " + st.toast.text : hint;
    ctx.print(1, 56, cut(foot, 21), 1);
}

const PERF_SHORT = ["FILL", "RAT2", "RAT3", "RAT4", "HALF", "DBL", "REV", "ACC", "FRZ1", "FRZ2", "FRZ4", "FRZ8", "SOLO", "RSTRT", "ROT+", "DICE"];

/* The screen for a held or latched layer. Returns false when there is none. */
export function drawLayer(ctx, st) {
    const s = st.snap || {};
    const blink = Math.floor((st.now || 0) / 200) % 2 === 0;
    if (st.layer === "pattern" || st.saveMode) {
        const cells = Array.from({ length: 16 }, (_, i) => {
            const p = st.patterns && st.patterns[i];
            return { txt: p ? (p.name || "P" + (i + 1)).toUpperCase() : "-", on: st.current === i, box: st.queued === i && blink,
                mark: !!(p && st.chain && st.chain.length >= 2 && st.chain.includes(i)) };
        });
        const chain = st.chain && st.chain.length >= 2 ? "CHAIN " + st.chain.map((c) => c + 1).join(">") : "";
        drawGrid(ctx, st, "PATTERNS", cut((st.setName || "").toUpperCase(), 11), cells,
            st.saveMode ? "TAP A SLOT TO SAVE" : (st.shift ? "TAP SAVES HERE" : (chain ? cut(chain, 21) : "TAP A SLOT TO PLAY")));
        return true;
    }
    if (st.layer === "perform") {
        const cells = PERF_SHORT.map((txt, i) => ({ txt, on: st.perfHeld && st.perfHeld[i], box: st.latched && st.latched[i] }));
        const n = st.held ? st.held.size : 0;
        drawGrid(ctx, st, "PERFORM", n ? n + (n === 1 ? " TRACK" : " TRACKS") : "ALL TRACKS", cells,
            st.shift ? "TAP LATCHES" : "HOLD  SHIFT LATCHES");
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

/* Knob LEDs, Schwung's convention: knobs 1-4 white, 5-8 amber, like the two
 * rows on screen. The selected knob is at full brightness, unused ones off. */
export function knobLeds(st) {
    const keys = knobKeys(st);
    const sel = selectedKnob(st);
    return keys.map((k, i) => (!k ? COL.off : i < 4 ? (i === sel ? COL.white : COL.grey) : (i === sel ? COL.orange : COL.dimOrange)));
}

/* ------------------------------------------------------------ value list */

/* The options of a named value, shown while its knob turns. */
export function listFor(st) {
    const key = st.listKey;
    const m = key ? metaFor(key) : null;
    if (!m || !m.options) return null;
    const v = st.values || {};
    const cur = Math.max(0, m.options.indexOf(v[key]));
    const titles = { songroot: "SONG ROOT", songscale: "SONG SCALE" };
    /* Scale and Root lists say what the song is, so Song is never a riddle. */
    const hint = key === "scale" || key === "root" ? "SONG IS " + String(v.songroot).toUpperCase() + " " + String(v.songscale).toUpperCase() : null;
    /* The Song entry names what it plays: "SONG (C)", or "S:MINOR" where the full form will not fit. */
    const song = (o) => {
        if (o !== SONG) return o;
        const full = songLabel(v, key), cols = m.options.length > 8;
        return !cols || full.length <= 9 ? full : "S:" + String(v["song" + key] || "").toUpperCase();
    };
    const options = key === "scale" || key === "root" ? m.options.map(song) : m.options;
    return { title: titles[key] || m.name.toUpperCase(), options, cur, hint };
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
    let hint = l.hint || (foot ? "TURN OR TAP" : "TURN, OR TAP A PAD");
    while (hint.length > 1 && ctx.textWidth(hint) + ctx.textWidth(foot) + 4 > 128) hint = hint.slice(0, -1);
    ctx.print(1, 57, hint, 1);
    ctx.print(127 - ctx.textWidth(foot), 57, foot, 1);
    return true;
}

/* ------------------------------------------------------------ cycle strip */

/* One small box per cycle at the right of the footer, always on screen:
 * filled = playing, outline = waiting, half filled = has its own edits, and the ones
 * picked for editing are filled and underlined. */
export function drawCycleStrip(ctx, st) {
    const s = st.snap || {};
    if (st.settings || st.warn || st.fzOn || st.randomHeld || st.padKey === "pitch" || (s.sm && s.sm !== "0") || (st.toast && st.toast.until > (st.now || 0))) return;
    const n = Math.max(1, Math.min(16, parseInt((st.values || {}).cycles, 10) || 1));
    const em = s.em | 0, ce = s.ce | 0, pc = s.pc | 0;
    const x0 = 128 - n * 6, y = 57;
    ctx.fillRect(x0 - 2, 55, n * 6 + 2, 9, 0);
    for (let i = 0; i < n; i++) {
        const x = x0 + i * 6;
        const picked = (em >> i) & 1;
        const playing = em ? picked && i === pc : (s.r ? i === pc : i === 0);
        if (playing || (picked && !s.r)) ctx.fillRect(x, y, 5, 5, 1);
        else {
            ctx.fillRect(x, y, 5, 1, 1); ctx.fillRect(x, y + 4, 5, 1, 1);
            ctx.fillRect(x, y, 1, 5, 1); ctx.fillRect(x + 4, y, 1, 5, 1);
            if ((ce >> i) & 1) ctx.fillRect(x, y + 3, 5, 2, 1);
        }
        if (picked) ctx.fillRect(x, y + 6, 5, 1, 1);
    }
}
