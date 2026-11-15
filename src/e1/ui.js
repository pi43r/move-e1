/*
 * E-1 — overtake UI for Ableton Move (Schwung).
 *
 * All sequencing lives in dsp.so. This file reads one compact snapshot from
 * the DSP per frame, draws it with Schwung's movy-style param pages, lights
 * the pads and step row, turns button presses into parameter edits, and saves
 * the project to /data/UserData/schwung/e1/.
 *
 * Surface (see the guide for the full map):
 *   Top 16 pads      tracks 1-16 (tap select, hold several = edit together,
 *                    Shift+tap = Note/CC, Mute held + tap = mute)
 *   Lower 16 pads    the view picked with the four track buttons:
 *                    1 Values, 2 Keys, 3 Perform, 4 Patterns
 *   Step row         the selected track's steps; tap = add/remove a hit
 *   Knobs 1-8        the current page; touch = show value, Shift = all tracks
 *                    (on CC pages Shift+turn sets the lane's CC number)
 *   Jog              page; Shift+jog = tempo (Internal clock)
 *   Play             start/stop per Clock mode; Shift+Play = next Clock mode
 *   Menu             settings page; Shift+Menu = next Destination
 *   Left/Right       step pages 17-64; Shift = rotate
 *   Back             hide E-1 (keeps playing); Shift+Back quits
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

import { renderPageMovy } from "/data/UserData/schwung/shared/param_pages/render_page_movy.mjs";
import { PAGE_KNOBS } from "/data/UserData/schwung/shared/param_pages/page_plan.mjs";
import { buildMetaIndex } from "/data/UserData/schwung/shared/param_pages/param_meta.mjs";
import { decodeDelta, setLED, setButtonLED } from "/data/UserData/schwung/shared/input_filter.mjs";
import {
    pagesFor, MENU_PAGE, valuesFrom, choicesFor, metaFor, ledFrame, pageOptions, drawOverlay,
    padToTrack, padToSlot, STEP_BASE, CLOCKS, DESTS, COL,
} from "./e1_view.mjs";

const LIB = { PAGE_KNOBS, buildMetaIndex };

const CC = {
    jogTurn: 14, jogClick: 3, shift: 49, menu: 50, back: 51, capture: 52,
    down: 54, up: 55, undo: 56, loop: 58, copy: 60, left: 62, right: 63,
    play: 85, rec: 86, mute: 88, sample: 118, del: 119,
    trackTop: 43, trackBottom: 40, knob0: 71,
};

const SAVE_DIR = "/data/UserData/schwung/e1";
const SAVE_FILE = SAVE_DIR + "/project.txt";
const LED_BUDGET = 20;          /* LED packets per tick; the out buffer holds ~64 */

const st = {
    focus: 0, page: 0, pageNote: 0, pageCC: 0, menu: false,
    view: 0, stepPage: 0, touched: -1, valuesKey: "steps",
    shift: false, muteHeld: false, fillHeld: false, held: new Set(),
    snap: null, values: {}, warn: null,
};
let pkeys = [];
let dirty = true;          /* redraw */
let lastSnapStr = "";
let editsSinceSave = 0, idleTicks = 0, saveQueue = [];
let ledWant = {}, ledHave = {};
let knobAcc = [0, 0, 0, 0, 0, 0, 0, 0];

/* ------------------------------------------------------------ DSP I/O */

function setP(k, v) { if (typeof host_module_set_param === "function") host_module_set_param(k, String(v)); }
function getP(k) { return typeof host_module_get_param === "function" ? host_module_get_param(k) : null; }

function edited() { editsSinceSave++; idleTicks = 0; dirty = true; }

function poll() {
    const s = getP("ui");
    if (!s || s === lastSnapStr) return;
    lastSnapStr = s;
    try { st.snap = JSON.parse(s); } catch (e) { return; }
    st.values = valuesFrom(st.snap, pkeys);
    dirty = true;
}

/* ------------------------------------------------------------ pages */

function isCC(t) { return st.snap ? ((st.snap.ty >> t) & 1) === 1 : t >= 11; }
function pages() { return st.menu ? [MENU_PAGE] : pagesFor(isCC(st.focus)); }
function currentPage() { const p = pages(); return p[Math.min(st.page, p.length - 1)]; }

function setFocus(t) {
    if (t === st.focus) return;
    if (isCC(st.focus)) st.pageCC = st.page; else st.pageNote = st.page;
    st.focus = t;
    st.page = isCC(t) ? st.pageCC : st.pageNote;
    st.stepPage = 0;
    setP("focus", t + 1);
    dirty = true;
}

/* Tracks a knob edit applies to: every held track, else the focus track. */
function targets() {
    if (st.held.size > 0) return [...st.held];
    return [st.focus];
}

function rel(d) { return d >= 0 ? "d+" + d : "d" + d; }

const GLOBAL_KEYS = new Set(["dest", "clock", "bpm", "clockout"]);

function setGlobal(key, value) {
    if (key === "dest") {
        setP("dest", value);
        /* Clock follows the destination unless changed by hand afterwards:
         * Move -> Follow Move, Computer -> Internal (E-1 is the master). */
        const d = DESTS.indexOf(value);
        if (d === 0 || d === 2) setP("clock", "Move");
        else if (d === 1) setP("clock", "Internal");
    } else {
        setP(key, value);
    }
    edited();
}

function turnKnob(i, delta) {
    const key = currentPage().keys[i];
    if (!key || delta === 0) return;
    const m = metaFor(key);
    /* Small ranges and lists move one notch per two detents so a choice
     * does not fly past. */
    const small = m && (m.type === "enum" || (m.max - m.min) <= 20);
    if (small) {
        knobAcc[i] += delta;
        if (Math.abs(knobAcc[i]) < 2) return;
        delta = knobAcc[i] > 0 ? 1 : -1;
        knobAcc[i] = 0;
    }
    if (GLOBAL_KEYS.has(key)) {
        if (key === "bpm") { setP("bpm", rel(delta)); edited(); return; }
        const opts = m.options;
        const cur = opts.indexOf(st.values[key]);
        const next = opts[(cur + delta + opts.length * 4) % opts.length];
        setGlobal(key, next);
        return;
    }
    let tks = targets();
    let k = key;
    if (st.shift) {
        if (/^cc\d+$/.test(key)) k = "ccn" + key.slice(2);          /* CC number */
        else tks = Array.from({ length: 16 }, (_, t) => t).filter((t) => isCC(t) === isCC(st.focus));
    }
    for (const t of tks) setP("t" + (t + 1) + ":" + k, rel(delta));
    edited();
}

function pickValue(slot) {
    const key = st.valuesKey;
    if (!key) return;
    const ch = choicesFor(key, st.values[key]);
    if (slot >= ch.length) return;
    if (GLOBAL_KEYS.has(key)) { setGlobal(key, ch[slot].value); return; }
    for (const t of targets()) setP("t" + (t + 1) + ":" + key, ch[slot].value);
    edited();
}

/* ------------------------------------------------------------ input */

function pressPlay() {
    if (st.shift) {
        const ck = st.snap ? st.snap.ck | 0 : 0;
        setP("clock", CLOCKS[(ck + 1) % 3]);
        edited();
        return;
    }
    const ck = st.snap ? st.snap.ck | 0 : 0;
    if (ck === 1) { setP("play", "toggle"); return; }
    /* Move owns the transport: press Move's own Play. */
    if (typeof move_midi_inject_to_move === "function") {
        move_midi_inject_to_move([0x0B, 0xB0, CC.play, 127]);
        move_midi_inject_to_move([0x0B, 0xB0, CC.play, 0]);
    }
}

function onNote(note, vel) {
    const on = vel > 0;
    /* Knob touch */
    if (note >= 0 && note <= 7) {
        if (on) {
            st.touched = note;
            const key = currentPage().keys[note];
            if (key) st.valuesKey = key;
        } else if (st.touched === note) {
            st.touched = -1;
        }
        dirty = true;
        return;
    }
    if (note < 10) return;   /* master/jog touch */

    /* Step row */
    if (note >= STEP_BASE && note < STEP_BASE + 16) {
        if (!on) return;
        const idx = st.stepPage * 16 + (note - STEP_BASE);
        const steps = parseInt(st.values.steps, 10) || 16;
        if (idx < steps) { setP("t" + (st.focus + 1) + ":toggle", idx); edited(); }
        return;
    }

    /* Track pads */
    const t = padToTrack(note);
    if (t >= 0) {
        if (!on) { st.held.delete(t); return; }
        if (st.muteHeld) {
            const muted = st.snap ? (st.snap.mu >> t) & 1 : 0;
            setP("t" + (t + 1) + ":mute", muted ? "0" : "1");
            edited();
            return;
        }
        if (st.shift) {
            setP("t" + (t + 1) + ":type", isCC(t) ? "Note" : "CC");
            edited();
            return;
        }
        if (st.held.size === 0) setFocus(t);
        st.held.add(t);
        return;
    }

    /* Lower pads */
    const slot = padToSlot(note);
    if (slot >= 0) {
        if (st.view === 0 && on) pickValue(slot);
        else if (st.view === 2 && slot === 0) { st.fillHeld = on; setP("fill", on ? "1" : "0"); dirty = true; }
    }
}

function onCC(cc, v) {
    if (cc === CC.shift) { st.shift = v > 0; dirty = true; return; }
    if (cc === CC.mute) { st.muteHeld = v > 0; return; }
    if (cc >= CC.knob0 && cc < CC.knob0 + 8) { turnKnob(cc - CC.knob0, decodeDelta(v)); return; }
    if (cc === CC.jogTurn) {
        const d = decodeDelta(v);
        if (!d) return;
        if (st.shift) { setP("bpm", rel(d)); edited(); return; }
        if (st.menu) return;
        const n = pages().length;
        st.page = Math.max(0, Math.min(n - 1, st.page + (d > 0 ? 1 : -1)));
        dirty = true;
        return;
    }
    if (v === 0) return;   /* the rest act on press */
    if (cc >= CC.trackBottom && cc <= CC.trackTop) {
        st.view = CC.trackTop - cc;
        if (st.view !== 2 && st.fillHeld) { st.fillHeld = false; setP("fill", "0"); }
        dirty = true;
        return;
    }
    switch (cc) {
    case CC.play: pressPlay(); break;
    case CC.menu:
        if (st.shift) {
            const d = st.snap ? st.snap.d | 0 : 0;
            setGlobal("dest", DESTS[(d + 1) % 3]);
        } else {
            st.menu = !st.menu;
            if (st.menu) st.valuesKey = "dest";
            dirty = true;
        }
        break;
    case CC.left: case CC.right: {
        const d = cc === CC.right ? 1 : -1;
        if (st.shift) {
            for (const t of targets()) setP("t" + (t + 1) + ":rotate", rel(d));
            edited();
        } else {
            const steps = parseInt(st.values.steps, 10) || 16;
            const maxPage = Math.max(0, Math.ceil(steps / 16) - 1);
            st.stepPage = Math.max(0, Math.min(maxPage, st.stepPage + d));
            dirty = true;
        }
        break;
    }
    default: break;
    }
}

globalThis.onMidiMessageInternal = function (data) {
    if (!data) return;
    const s = data[0] | 0, d1 = data[1] | 0, d2 = data[2] | 0, type = s & 0xF0;
    if (s >= 0xF8) return;
    if (type === 0x90) onNote(d1, d2);
    else if (type === 0x80) onNote(d1, 0);
    else if (type === 0xB0) onCC(d1, d2);
};

globalThis.onMidiMessageExternal = function () { /* E-1 ignores incoming notes in Phase 1 */ };

/* ------------------------------------------------------------ LEDs */

function buttonFrame() {
    const s = st.snap || {};
    const b = {};
    b[CC.play] = s.r ? COL.green : COL.dim;
    b[CC.menu] = st.menu ? COL.white : COL.dim;
    b[CC.shift] = st.shift ? COL.white : COL.dim;
    b[CC.mute] = st.muteHeld ? COL.white : COL.dim;
    const steps = parseInt(st.values.steps, 10) || 16;
    b[CC.left] = st.stepPage > 0 ? COL.white : COL.off;
    b[CC.right] = (st.stepPage + 1) * 16 < steps ? COL.white : COL.off;
    for (let i = 0; i < 4; i++) b[CC.trackTop - i] = st.view === i ? COL.white : COL.dim;
    const keys = currentPage().keys;
    for (let i = 0; i < 8; i++) b[CC.knob0 + i] = keys[i] ? COL.white : COL.off;
    return b;
}

function pushLEDs() {
    const pads = ledFrame(st);
    const btns = buttonFrame();
    let budget = LED_BUDGET;
    for (const n in pads) {
        if (budget <= 0) return;
        if (ledHave["n" + n] !== pads[n]) { setLED(+n, pads[n], true); ledHave["n" + n] = pads[n]; budget--; }
    }
    for (const c in btns) {
        if (budget <= 0) return;
        if (ledHave["c" + c] !== btns[c]) { setButtonLED(+c, btns[c], true); ledHave["c" + c] = btns[c]; budget--; }
    }
}

/* ------------------------------------------------------------ screen */

const ctx = {
    fillRect: (x, y, w, h, c) => fill_rect(x, y, w, h, c),
    print: (x, y, s, c) => print(x, y, s, c),
    textWidth: (s) => text_width(s),
    line: typeof draw_line === "function" ? draw_line : undefined,
    fillCircle: typeof fill_circle === "function" ? fill_circle : undefined,
    drawCircle: typeof draw_circle === "function" ? draw_circle : undefined,
    drawArc: typeof draw_arc === "function" ? draw_arc : undefined,
};

function updateWarning() {
    st.warn = null;
    const ck = st.snap ? st.snap.ck | 0 : 0;
    if (ck === 2 && typeof host_get_move_info === "function") {
        const mi = host_get_move_info();
        if (mi && mi.valid && mi.midiClockSync === 0) st.warn = "SET MOVE TO MIDI SYNC";
    }
}

function draw() {
    if (typeof clear_screen !== "function") return;
    clear_screen();
    renderPageMovy(ctx, pageOptions(st, LIB));
    drawOverlay(ctx, st);
}

/* ------------------------------------------------------------ save/load */

function loadProject() {
    if (typeof host_read_file !== "function") return;
    const text = host_read_file(SAVE_FILE);
    if (!text) return;
    let section = null, buf = [];
    const flush = () => {
        if (section === null) return;
        setP(section === "g" ? "load:g" : "load:" + section.slice(1), buf.join("\n"));
    };
    for (const line of text.split("\n")) {
        const m = /^\[(g|t\d+)\]$/.exec(line.trim());
        if (m) { flush(); section = m[1]; buf = []; } else if (line) buf.push(line);
    }
    flush();
}

/* One read per tick so saving never stalls the screen. */
function saveStep() {
    if (saveQueue.length === 0) {
        if (editsSinceSave === 0 || ++idleTicks < 120) return;
        editsSinceSave = 0;
        saveQueue = ["g"].concat(Array.from({ length: 16 }, (_, i) => String(i + 1)));
        saveStep.parts = [];
        return;
    }
    const k = saveQueue.shift();
    const body = getP("dump:" + k) || "";
    saveStep.parts.push("[" + (k === "g" ? "g" : "t" + k) + "]\n" + body);
    if (saveQueue.length === 0 && typeof host_write_file === "function") {
        if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
        host_write_file(SAVE_FILE, saveStep.parts.join("\n") + "\n");
    }
}
saveStep.parts = [];

function saveNow() {
    const parts = [];
    for (const k of ["g"].concat(Array.from({ length: 16 }, (_, i) => String(i + 1))))
        parts.push("[" + (k === "g" ? "g" : "t" + k) + "]\n" + (getP("dump:" + k) || ""));
    if (typeof host_write_file === "function") {
        if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
        host_write_file(SAVE_FILE, parts.join("\n") + "\n");
    }
}

/* ------------------------------------------------------------ lifecycle */

globalThis.init = function () {
    const pk = getP("pkeys");
    pkeys = pk ? pk.split(",") : [];
    loadProject();
    setP("focus", st.focus + 1);
    ledHave = {};
    poll();
};

globalThis.onResume = function () { ledHave = {}; dirty = true; };

let frame = 0;
globalThis.tick = function () {
    frame++;
    poll();
    if ((frame & 15) === 0) updateWarning();
    pushLEDs();
    if (dirty) { draw(); dirty = false; }
    saveStep();
};

globalThis.onUnload = function () {
    if (editsSinceSave > 0 || saveQueue.length > 0) saveNow();
    setP("panic", "1");
};
