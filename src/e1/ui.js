/*
 * E-1 — overtake UI for Ableton Move (Schwung).
 *
 * All sequencing lives in dsp.so. This file reads one compact snapshot from
 * the DSP per frame, draws it with Schwung's movy-style param pages, lights
 * the pads, turns presses into edits and perform moves, and saves the
 * project and patterns to /data/UserData/schwung/e1/.
 *
 * Surface (the guide has the full map):
 *   Top 16 pads      tracks 1-16. Tap = select, hold = target for knobs,
 *                    +/- and perform pads. Shift+tap = mute.
 *   Lower 16 pads    the view on the four track buttons:
 *                    1 Values, 2 Perform, 3 Mutes, 4 Patterns
 *   Step row         the selected track's steps. Tap = add/remove a hit,
 *                    Shift+tap = set Steps, Loop+tap = freeze that many steps
 *   Knobs 1-8        the current page. Shift = all tracks of that type
 *   Jog              page (a banner names it). Click = back to Rhythm.
 *                    Shift+jog = tempo
 *   +/-              pulses; Shift = steps
 *   Left/Right       step pages 17-64; Shift = rotate
 *   Sample (hold)    Temp: edits made while held are undone on release.
 *                    Capture while holding keeps them.
 *   Capture          save the current state to the next empty pattern
 *   Loop (hold)      freeze the last 4 steps
 *   Copy + 2 pads    copy a track (or a pattern in Patterns view)
 *   Delete + pad     clear a track's rhythm, a step edit, or a pattern
 *   Undo             step back through edits
 *   Play             start/stop per Clock mode; Shift+Play = next Clock mode
 *   Menu             settings page; Shift+Menu = next Destination
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

import { renderPageMovy } from "/data/UserData/schwung/shared/param_pages/render_page_movy.mjs";
import { PAGE_KNOBS } from "/data/UserData/schwung/shared/param_pages/page_plan.mjs";
import { buildMetaIndex } from "/data/UserData/schwung/shared/param_pages/param_meta.mjs";
import { fontPrint, fontWidth } from "/data/UserData/schwung/shared/param_pages/font_tamzen6x12.mjs";
import { decodeDelta } from "/data/UserData/schwung/shared/input_filter.mjs";
import {
    MENU_PAGE, PERFORM, VIEWS, TRACK_NAMES, valuesFrom, choicesFor, metaFor, ledFrame, pageOptions,
    drawBanner, currentPages, pageIndex, padToTrack, padToSlot, STEP_BASE, CLOCKS, DESTS, COL,
} from "./e1_view.mjs";

const LIB = { PAGE_KNOBS, buildMetaIndex, bigPrint: fontPrint, bigWidth: fontWidth };

const CC = {
    jogTurn: 14, jogClick: 3, shift: 49, menu: 50, back: 51, capture: 52,
    down: 54, up: 55, undo: 56, loop: 58, copy: 60, left: 62, right: 63,
    play: 85, rec: 86, mute: 88, sample: 118, del: 119,
    trackTop: 43, trackBottom: 40, knob0: 71,
};

const SAVE_DIR = "/data/UserData/schwung/e1";
const SAVE_FILE = SAVE_DIR + "/project.txt";
const PAT_FILE = SAVE_DIR + "/patterns.json";
const LED_BUDGET = 20;          /* LED packets per tick; the out buffer holds ~64 */
const BANNER_MS = 750;
const UNDO_DEPTH = 24;
const GESTURE_MS = 1200;        /* edits closer than this share one undo step */
const IDENTITY = new Set(["type", "channel", "output", "notemode", "note", "mute"]);
const VIEW_COL = [COL.blue, COL.orange, COL.yellow, COL.green];

const st = {
    focus: 0, pageName: "Rhythm", menu: false, view: 0, stepPage: 0, touched: -1, valuesKey: "steps",
    shift: false, muteHeld: false, copyHeld: false, delHeld: false, loopHeld: false, sampleHeld: false,
    held: new Set(), snap: null, values: {}, warn: null, now: 0,
    flashUntil: new Array(16).fill(0), banner: null,
    perfHeld: new Array(16).fill(false), latched: new Array(16).fill(false), perfMask: new Array(16).fill(0),
    soloMask: 0, patterns: new Array(16).fill(null), current: -1, queued: -1,
    copySrc: -1, copyPat: -1, keepTemp: false,
};
let pkeys = [];
let dirty = true;
let lastSnapStr = "";
let editsSinceSave = 0, idleTicks = 0, saveQueue = [], saveParts = [];
let ledHave = {};
let knobAcc = [0, 0, 0, 0, 0, 0, 0, 0];
let undo = [], lastEditAt = -1e9;
let bannerShown = false;
let loopMask = 0xFFFF;

/* ------------------------------------------------------------ DSP I/O */

function setP(k, v) { if (typeof host_module_set_param === "function") host_module_set_param(k, String(v)); }
function getP(k) { return typeof host_module_get_param === "function" ? host_module_get_param(k) : null; }

function edited() { editsSinceSave++; idleTicks = 0; dirty = true; }

function poll() {
    const s = getP("ui");
    if (!s || s === lastSnapStr) return;
    lastSnapStr = s;
    let snap;
    try { snap = JSON.parse(s); } catch (e) { return; }
    st.snap = snap;
    st.values = valuesFrom(snap, pkeys);
    const fl = snap.fl | 0;
    if (fl) for (let t = 0; t < 16; t++) if ((fl >> t) & 1) st.flashUntil[t] = st.now + 90;
    if (st.queued >= 0 && !(snap.q | 0)) {          /* the queued pattern landed */
        st.current = st.queued; st.queued = -1; edited();
    }
    dirty = true;
}

function banner(text, sub, dots) {
    st.banner = { text, sub: sub || "", until: st.now + BANNER_MS, dots: dots || null };
    dirty = true;
}

/* ------------------------------------------------------------ helpers */

function isCC(t) { return st.snap ? ((st.snap.ty >> t) & 1) === 1 : t >= 11; }
function pages() { return currentPages(st); }
function currentPage() { const p = pages(); return st.menu ? MENU_PAGE : p[pageIndex(p, st.pageName)]; }
function rel(d) { return d >= 0 ? "d+" + d : "d" + d; }
function hex(m) { return (m >>> 0).toString(16); }
function tname(t) { return "T" + (t + 1) + " " + TRACK_NAMES[t]; }

function setFocus(t) {
    if (t === st.focus) return;
    st.focus = t;
    st.stepPage = 0;
    knobAcc.fill(0);
    setP("focus", t + 1);
    dirty = true;
}

/* Tracks an edit applies to: every held track pad, else the focus track. */
function targets() { return st.held.size > 0 ? [...st.held] : [st.focus]; }
function heldMask() { let m = 0; for (const t of st.held) m |= 1 << t; return m; }
function maskLabel(m) {
    if ((m & 0xFFFF) === 0xFFFF) return "ALL TRACKS";
    const ts = []; for (let t = 0; t < 16; t++) if ((m >> t) & 1) ts.push(t);
    if (ts.length === 1) return tname(ts[0]);
    return ts.length <= 4 ? ts.map((t) => "T" + (t + 1)).join(" ") : ts.length + " TRACKS";
}

function dumpAll() {
    const out = [];
    for (let t = 1; t <= 16; t++) out.push(getP("dump:" + t) || "");
    return out;
}

/* One undo step per gesture: the state before the first edit of a burst. */
function checkpoint() {
    if (st.now - lastEditAt > GESTURE_MS) {
        undo.push(dumpAll());
        if (undo.length > UNDO_DEPTH) undo.shift();
    }
    lastEditAt = st.now;
}

function stageAll(dumps, when) {
    for (let t = 0; t < 16; t++) if (dumps[t]) setP("stage:" + (t + 1), dumps[t]);
    setP("commit", when);
}

function parseDump(text) {
    const m = new Map();
    for (const line of String(text || "").split("\n")) {
        const i = line.indexOf("=");
        if (i > 0) m.set(line.slice(0, i), line.slice(i + 1));
    }
    return m;
}
function joinDump(m) { let s = ""; for (const [k, v] of m) s += k + "=" + v + "\n"; return s; }

/* ------------------------------------------------------------ edits */

const GLOBAL_KEYS = new Set(["dest", "clock", "bpm", "clockout"]);

function setGlobal(key, value) {
    if (key === "dest") {
        setP("dest", value);
        /* Clock follows the destination: Move -> follow Move,
         * Computer -> Internal (E-1 is the master). Change it after if needed. */
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
    const laneNum = st.shift && /^cc\d+$/.test(key);
    /* Detents per value: small ranges move slowly so a value is easy to hit. */
    const div = laneNum ? 3 : (m && m.div) || 1;
    if (knobAcc[i] !== 0 && Math.sign(knobAcc[i]) !== Math.sign(delta)) knobAcc[i] = 0;
    knobAcc[i] += delta;
    const n = Math.trunc(knobAcc[i] / div);
    if (n === 0) return;
    knobAcc[i] -= n * div;
    delta = n;
    st.valuesKey = key;

    if (GLOBAL_KEYS.has(key)) {
        if (key === "bpm") { setP("bpm", rel(delta)); edited(); return; }
        const opts = m.options;
        const cur = Math.max(0, opts.indexOf(st.values[key]));
        setGlobal(key, opts[Math.max(0, Math.min(opts.length - 1, cur + delta))]);
        return;
    }
    checkpoint();
    let tks = targets();
    let k = key;
    if (st.shift) {
        if (laneNum) k = "ccn" + key.slice(2);
        else tks = Array.from({ length: 16 }, (_, t) => t).filter((t) => isCC(t) === isCC(st.focus));
    }
    for (const t of tks) setP("t" + (t + 1) + ":" + k, rel(delta));
    if (key === "type") {
        /* Type changes the page set; stay on Setup, where Type lives. */
        st.pageName = "Setup";
        banner(delta > 0 ? "CC TRACK" : "NOTE TRACK", tname(st.focus));
    }
    edited();
}

function pickValue(slot) {
    const key = st.valuesKey;
    if (!key) return;
    const ch = choicesFor(key, st.values[key]);
    if (slot >= ch.length) return;
    if (GLOBAL_KEYS.has(key)) { setGlobal(key, ch[slot].value); return; }
    checkpoint();
    for (const t of targets()) setP("t" + (t + 1) + ":" + key, ch[slot].value);
    const m = metaFor(key);
    banner((m && m.name ? m.name : key) + " " + ch[slot].label, st.held.size > 1 ? maskLabel(heldMask()) : "");
    edited();
}

function nudge(key, d) {
    checkpoint();
    for (const t of targets()) setP("t" + (t + 1) + ":" + key, rel(d));
    const m = metaFor(key);
    const cur = parseInt(st.values[key], 10) || 0;
    let next = Math.max(m.min, Math.min(m.max, cur + d));
    if (key === "pulses") next = Math.min(next, parseInt(st.values.steps, 10) || 16);
    banner(m.name + " " + next, st.held.size > 1 ? maskLabel(heldMask()) : tname(st.focus));
    edited();
}

function toggleMute(t) {
    checkpoint();
    const muted = st.snap ? (st.snap.mu >> t) & 1 : 0;
    setP("t" + (t + 1) + ":mute", muted ? "0" : "1");
    banner(muted ? "UNMUTE" : "MUTE", tname(t));
    edited();
}

function setSolo(mask) {
    st.soloMask = mask & 0xFFFF;
    setP("perf:solo", hex(st.soloMask) + " " + (st.soloMask ? 1 : 0));
    dirty = true;
}

function clearTrack(t) {
    checkpoint();
    const p = "t" + (t + 1) + ":";
    setP(p + "pulses", "0"); setP(p + "rotate", "0"); setP(p + "fill", "0");
    setP(p + "clearedits", "1");
    banner("CLEARED", tname(t));
    edited();
}

function copyTrack(src, dst) {
    if (src === dst) return;
    checkpoint();
    const from = parseDump(getP("dump:" + (src + 1)));
    const to = parseDump(getP("dump:" + (dst + 1)));
    for (const [k, v] of from) if (!IDENTITY.has(k)) to.set(k, v);
    setP("stage:" + (dst + 1), joinDump(to));
    setP("commit", "now");
    banner("COPIED", "T" + (src + 1) + " > T" + (dst + 1));
    edited();
}

function doUndo() {
    const prev = undo.pop();
    if (!prev) { banner("NOTHING TO UNDO"); return; }
    stageAll(prev, "now");
    lastEditAt = -1e9;
    banner("UNDO", undo.length + " LEFT");
    edited();
}

/* ------------------------------------------------------------ perform */

function perf(move, mask, value) { setP("perf:" + move, hex(mask) + " " + value); }

function engage(i) {
    const p = PERFORM[i];
    const mask = st.perfMask[i];
    if (p.move === "solo") { setP("perf:solo", hex(mask | st.soloMask) + " 1"); return; }
    if (p.move === "freeze") perf("freeze", mask, 0);
    perf(p.move, mask, p.value);
}

function disengage(i) {
    const p = PERFORM[i];
    if (p.move === "solo") setSolo(st.soloMask);
    else perf(p.move, st.perfMask[i], 0);
    /* Another pad with the same move still down takes over again. */
    for (let j = 0; j < 16; j++)
        if (j !== i && PERFORM[j].move === p.move && (st.perfHeld[j] || st.latched[j])) engage(j);
}

function perfPress(i, on) {
    const p = PERFORM[i];
    if (!on) {
        if (st.perfHeld[i]) {
            st.perfHeld[i] = false;
            if (!st.latched[i]) disengage(i);
            dirty = true;
        }
        return;
    }
    const mask = heldMask() || 0xFFFF;
    if (p.kind === "shot") {
        if (p.move === "dice") {
            setP("seed", String(1 + Math.floor(Math.random() * 0x7FFFFFFE)));
            banner("NEW DICE", "PROB AND THIN REROLLED");
        } else {
            if (p.move === "rotate") checkpoint();
            perf(p.move, mask, p.value);
            banner(p.label, maskLabel(mask));
        }
        edited();
        return;
    }
    if (st.latched[i]) {                       /* tap a latched pad to let go */
        st.latched[i] = false;
        disengage(i);
        banner(p.label + " OFF");
        return;
    }
    st.perfMask[i] = mask;
    engage(i);
    if (st.shift) { st.latched[i] = true; banner(p.label + " LATCHED", maskLabel(mask)); }
    else { st.perfHeld[i] = true; banner(p.label, maskLabel(mask)); }
}

function releaseAllPerform() {
    for (let i = 0; i < 16; i++) { st.perfHeld[i] = false; st.latched[i] = false; }
    perf("clear", 0xFFFF, 0);
    st.soloMask = 0;
    if (st.loopHeld) st.loopHeld = false;
}

/* ------------------------------------------------------------ patterns */

function writePatterns() {
    if (typeof host_write_file !== "function") return;
    if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
    host_write_file(PAT_FILE, JSON.stringify({ v: 1, current: st.current, slots: st.patterns }));
}

function loadPatterns() {
    if (typeof host_read_file !== "function") return;
    const text = host_read_file(PAT_FILE);
    if (!text) return;
    try {
        const o = JSON.parse(text);
        if (o && Array.isArray(o.slots)) for (let i = 0; i < 16; i++) st.patterns[i] = o.slots[i] || null;
        if (o && typeof o.current === "number") st.current = o.current;
    } catch (e) { /* a broken file leaves the slots empty */ }
}

function savePattern(i) {
    st.patterns[i] = dumpAll();
    st.current = i;
    writePatterns();
    banner("SAVED", "PATTERN " + (i + 1));
}

function patternPress(i) {
    if (st.delHeld) {
        if (!st.patterns[i]) return;
        st.patterns[i] = null;
        if (st.current === i) st.current = -1;
        writePatterns();
        banner("DELETED", "PATTERN " + (i + 1));
        return;
    }
    if (st.copyHeld) {
        if (st.copyPat < 0) { if (st.patterns[i]) { st.copyPat = i; banner("COPY P" + (i + 1), "PICK A SLOT"); } return; }
        st.patterns[i] = st.patterns[st.copyPat].slice();
        writePatterns();
        banner("COPIED", "P" + (st.copyPat + 1) + " > P" + (i + 1));
        st.copyPat = -1;
        return;
    }
    if (st.shift) { savePattern(i); return; }
    if (!st.patterns[i]) { banner("EMPTY", "SHIFT+PAD SAVES HERE"); return; }
    const running = st.snap && st.snap.r;
    /* Tap again while it waits = switch now. */
    const now = !running || st.queued === i;
    checkpoint();
    stageAll(st.patterns[i], now ? "now" : "bar");
    if (now) { st.current = i; st.queued = -1; banner("PATTERN " + (i + 1)); edited(); }
    else { st.queued = i; banner("NEXT P" + (i + 1), "TAP AGAIN = NOW"); }
}

function captureToNext() {
    let i = st.patterns.findIndex((p) => !p);
    if (i < 0) { banner("ALL 16 FULL", "SHIFT+PAD OVERWRITES"); return; }
    savePattern(i);
}

/* ------------------------------------------------------------ input */

function pressPlay() {
    const ck = st.snap ? st.snap.ck | 0 : 0;
    if (st.shift) {
        const next = CLOCKS[(ck + 1) % 3];
        setP("clock", next);
        banner("CLOCK " + next);
        edited();
        return;
    }
    if (ck === 1) { setP("play", "toggle"); return; }
    /* Move owns the transport: press Move's own Play. */
    if (typeof move_midi_inject_to_move === "function") {
        move_midi_inject_to_move([0x0B, 0xB0, CC.play, 127]);
        move_midi_inject_to_move([0x0B, 0xB0, CC.play, 0]);
    }
}

function changePage(d) {
    if (st.menu) return;
    const p = pages();
    const i = pageIndex(p, st.pageName);
    const n = Math.max(0, Math.min(p.length - 1, i + d));
    st.pageName = p[n].name;
    knobAcc.fill(0);
    banner(p[n].name, "PAGE " + (n + 1) + " OF " + p.length, [n, p.length]);
}

function onStep(idx, on) {
    if (!on) return;
    const steps = parseInt(st.values.steps, 10) || 16;
    const f = "t" + (st.focus + 1) + ":";
    if (st.loopHeld) {
        const n = idx + 1;
        perf("freeze", loopMask, 0);
        perf("freeze", loopMask, n);
        banner("FREEZE " + n, maskLabel(loopMask));
        return;
    }
    if (st.shift) {
        checkpoint();
        for (const t of targets()) setP("t" + (t + 1) + ":steps", idx + 1);
        banner("STEPS " + (idx + 1), st.held.size > 1 ? maskLabel(heldMask()) : tname(st.focus));
        edited();
        return;
    }
    if (st.delHeld) {
        checkpoint();
        setP(f + "clearedits", "1");
        banner("HAND EDITS CLEARED", tname(st.focus));
        edited();
        return;
    }
    if (idx < steps) { checkpoint(); setP(f + "toggle", idx); edited(); }
}

function onTrackPad(t, on) {
    if (!on) { st.held.delete(t); dirty = true; return; }
    if (st.copyHeld) {
        if (st.copySrc < 0) { st.copySrc = t; banner("COPY " + tname(t), "PICK A TRACK"); }
        else { copyTrack(st.copySrc, t); st.copySrc = -1; }
        return;
    }
    if (st.delHeld) { clearTrack(t); return; }
    if (st.muteHeld || st.shift) { toggleMute(t); return; }
    if (st.held.size === 0) setFocus(t);
    st.held.add(t);
    dirty = true;
}

function onSlot(i, on) {
    if (st.view === 1) { perfPress(i, on); return; }
    if (!on) return;
    if (st.view === 0) pickValue(i);
    else if (st.view === 2) {
        if (st.shift) {
            const m = st.soloMask ^ (1 << i);
            setSolo(m);
            banner(m ? "SOLO" : "SOLO OFF", m ? maskLabel(m) : "");
        } else toggleMute(i);
    } else if (st.view === 3) patternPress(i);
}

function onNote(note, vel) {
    const on = vel > 0;
    if (note >= 0 && note <= 7) {               /* knob touch */
        if (on) {
            st.touched = note;
            const key = currentPage().keys[note];
            if (key) st.valuesKey = key;
        } else if (st.touched === note) st.touched = -1;
        dirty = true;
        return;
    }
    if (note < 10) return;                       /* master/jog touch */
    if (note >= STEP_BASE && note < STEP_BASE + 16) { onStep(st.stepPage * 16 + (note - STEP_BASE), on); return; }
    const t = padToTrack(note);
    if (t >= 0) { onTrackPad(t, on); return; }
    const slot = padToSlot(note);
    if (slot >= 0) onSlot(slot, on);
}

function onCC(cc, v) {
    const down = v > 0;
    if (cc === CC.shift) { st.shift = down; dirty = true; return; }
    if (cc === CC.mute) { st.muteHeld = down; return; }
    if (cc === CC.copy) { st.copyHeld = down; if (!down) { st.copySrc = -1; st.copyPat = -1; } dirty = true; return; }
    if (cc === CC.del) { st.delHeld = down; return; }
    if (cc >= CC.knob0 && cc < CC.knob0 + 8) { turnKnob(cc - CC.knob0, decodeDelta(v)); return; }
    if (cc === CC.jogTurn) {
        const d = decodeDelta(v);
        if (!d) return;
        if (st.shift) {
            setP("bpm", rel(d)); edited();
            banner("TEMPO " + Math.round((st.snap ? st.snap.bpm : 120) + d), (st.snap && st.snap.ck === 1) ? "" : "USED ON INTERNAL CLOCK");
            return;
        }
        changePage(d > 0 ? 1 : -1);
        return;
    }
    if (cc === CC.sample) {
        if (down) {
            st.sampleHeld = true; st.keepTemp = false;
            setP("temp", "1");
            banner("TEMP", "LET GO TO UNDO");
        } else if (st.sampleHeld) {
            st.sampleHeld = false;
            if (!st.keepTemp) { setP("temp", "0"); banner("RESTORED"); edited(); }
        }
        return;
    }
    if (cc === CC.loop) {
        const mask = heldMask() || 0xFFFF;
        if (down) { st.loopHeld = true; loopMask = mask; perf("freeze", mask, 0); perf("freeze", mask, 4); banner("FREEZE 4", maskLabel(mask)); }
        else if (st.loopHeld) { st.loopHeld = false; perf("freeze", loopMask, 0); dirty = true; }
        return;
    }
    if (!down) return;   /* the rest act on press */
    if (cc >= CC.trackBottom && cc <= CC.trackTop) {
        const view = CC.trackTop - cc;
        if (view === 1 && st.view === 1) { releaseAllPerform(); banner("ALL MOVES OFF"); }
        else if (view !== st.view) {
            if (st.view === 1) for (let i = 0; i < 16; i++) if (st.perfHeld[i]) { st.perfHeld[i] = false; if (!st.latched[i]) disengage(i); }
            st.view = view;
            banner(VIEWS[view].toUpperCase(), ["PICK A VALUE", "HOLD PADS. SHIFT LATCHES", "TAP MUTES. SHIFT SOLOS", "TAP PLAYS. SHIFT SAVES"][view]);
        }
        return;
    }
    switch (cc) {
    case CC.play: pressPlay(); break;
    case CC.menu:
        if (st.shift) {
            const d = st.snap ? st.snap.d | 0 : 0;
            setGlobal("dest", DESTS[(d + 1) % 3]);
            banner("TO " + DESTS[(d + 1) % 3]);
        } else {
            st.menu = !st.menu;
            if (st.menu) st.valuesKey = "dest";
            knobAcc.fill(0);
            banner(st.menu ? "SETTINGS" : st.pageName);
        }
        break;
    case CC.jogClick:
        st.menu = false;
        if (st.pageName !== "Rhythm") { st.pageName = "Rhythm"; knobAcc.fill(0); banner("RHYTHM", "PAGE 1"); }
        break;
    case CC.up: nudge(st.shift ? "steps" : "pulses", 1); break;
    case CC.down: nudge(st.shift ? "steps" : "pulses", -1); break;
    case CC.undo: doUndo(); break;
    case CC.capture:
        if (st.sampleHeld) { st.keepTemp = true; setP("temp", "keep"); banner("KEPT"); edited(); }
        else captureToNext();
        break;
    case CC.left: case CC.right: {
        const d = cc === CC.right ? 1 : -1;
        if (st.shift) {
            checkpoint();
            for (const t of targets()) setP("t" + (t + 1) + ":rotate", rel(d));
            banner("ROTATE " + (d > 0 ? "+1" : "-1"), st.held.size > 1 ? maskLabel(heldMask()) : tname(st.focus));
            edited();
        } else {
            const steps = parseInt(st.values.steps, 10) || 16;
            const maxPage = Math.max(0, Math.ceil(steps / 16) - 1);
            const n = Math.max(0, Math.min(maxPage, st.stepPage + d));
            if (n !== st.stepPage) { st.stepPage = n; banner("STEPS " + (n * 16 + 1) + "-" + Math.min(steps, n * 16 + 16)); }
        }
        break;
    }
    default: break;
    }
}

globalThis.onMidiMessageInternal = function (data) {
    if (!data || globalThis.overtakeParked) return;
    const s = data[0] | 0, d1 = data[1] | 0, d2 = data[2] | 0, type = s & 0xF0;
    if (s >= 0xF8) return;
    if (type === 0x90) onNote(d1, d2);
    else if (type === 0x80) onNote(d1, 0);
    else if (type === 0xB0) onCC(d1, d2);
};

globalThis.onMidiMessageExternal = function () { /* E-1 ignores incoming notes for now */ };

/* ------------------------------------------------------------ LEDs */

function buttonFrame() {
    const s = st.snap || {};
    const b = {};
    b[CC.play] = s.r ? COL.green : COL.dim;
    b[CC.menu] = st.menu ? COL.white : COL.dim;
    b[CC.shift] = st.shift ? COL.white : COL.dim;
    b[CC.mute] = (s.mu | 0) ? COL.white : COL.dim;
    b[CC.sample] = s.tmp ? COL.white : COL.dim;
    b[CC.loop] = st.loopHeld ? COL.white : COL.dim;
    b[CC.capture] = COL.dim;
    b[CC.undo] = undo.length ? COL.dim : COL.off;
    b[CC.copy] = st.copyHeld ? COL.white : COL.dim;
    b[CC.del] = st.delHeld ? COL.white : COL.dim;
    b[CC.up] = COL.dim; b[CC.down] = COL.dim;
    const steps = parseInt(st.values.steps, 10) || 16;
    b[CC.left] = st.stepPage > 0 ? COL.white : COL.off;
    b[CC.right] = (st.stepPage + 1) * 16 < steps ? COL.white : COL.off;
    for (let i = 0; i < 4; i++) b[CC.trackTop - i] = st.view === i ? VIEW_COL[i] : COL.dim;
    const keys = currentPage().keys;
    for (let i = 0; i < 8; i++) b[CC.knob0 + i] = keys[i] ? COL.white : COL.off;
    return b;
}

function send(pkt) { return typeof move_midi_internal_send === "function" ? move_midi_internal_send(pkt) : 0; }

function pushLEDs() {
    const pads = ledFrame(st);
    const btns = buttonFrame();
    let budget = LED_BUDGET;
    for (const n in pads) {
        if (ledHave["n" + n] === pads[n]) continue;
        if (budget-- <= 0 || !send([0x09, 0x90, +n, pads[n]])) return;
        ledHave["n" + n] = pads[n];
    }
    for (const c in btns) {
        if (ledHave["c" + c] === btns[c]) continue;
        if (budget-- <= 0 || !send([0x0B, 0xB0, +c, btns[c]])) return;
        ledHave["c" + c] = btns[c];
    }
}

/* ------------------------------------------------------------ screen */

/* Draw calls look the host function up on every call. Capturing the function
 * at load time would bypass the no-op swap the host does while E-1 is parked,
 * and E-1's knobs would draw over other screens. */
const call = (name) => (...a) => { const f = globalThis[name]; if (typeof f === "function") return f(...a); };
const ctx = {
    fillRect: call("fill_rect"),
    print: call("print"),
    textWidth: (s) => (typeof text_width === "function" ? text_width(s) : String(s).length * 6),
    line: call("draw_line"),
    fillCircle: call("fill_circle"),
    drawCircle: call("draw_circle"),
    drawArc: call("draw_arc"),
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
    drawBanner(ctx, st, LIB);
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

const SECTIONS = ["g"].concat(Array.from({ length: 16 }, (_, i) => String(i + 1)));
const part = (k) => "[" + (k === "g" ? "g" : "t" + k) + "]\n" + (getP("dump:" + k) || "");

function writeProject(parts) {
    if (typeof host_write_file !== "function") return;
    if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
    host_write_file(SAVE_FILE, parts.join("\n") + "\n");
}

/* One read per tick so saving never stalls the screen. Temp edits are not
 * saved until they are kept. */
function saveStep() {
    if (saveQueue.length === 0) {
        if (editsSinceSave === 0 || (st.snap && st.snap.tmp) || ++idleTicks < 120) return;
        editsSinceSave = 0;
        saveQueue = SECTIONS.slice();
        saveParts = [];
        return;
    }
    saveParts.push(part(saveQueue.shift()));
    if (saveQueue.length === 0) writeProject(saveParts);
}

function saveNow() { writeProject(SECTIONS.map(part)); }

/* ------------------------------------------------------------ lifecycle */

globalThis.init = function () {
    st.now = Date.now();
    const pk = getP("pkeys");
    pkeys = pk ? pk.split(",") : [];
    loadProject();
    loadPatterns();
    setP("focus", st.focus + 1);
    ledHave = {};
    poll();
};

/* Back to E-1 from another screen. Buttons released while away never
 * reached us, so let go of everything held. */
globalThis.onResume = function () {
    st.now = Date.now();
    st.shift = st.muteHeld = st.copyHeld = st.delHeld = false;
    st.held.clear();
    st.copySrc = st.copyPat = -1;
    st.touched = -1;
    for (let i = 0; i < 16; i++) if (st.perfHeld[i]) { st.perfHeld[i] = false; if (!st.latched[i]) disengage(i); }
    if (st.loopHeld) { st.loopHeld = false; perf("freeze", loopMask, 0); }
    if (st.sampleHeld) { st.sampleHeld = false; if (!st.keepTemp) setP("temp", "0"); }
    knobAcc.fill(0);
    ledHave = {};
    lastSnapStr = "";
    dirty = true;
};

let frame = 0;
globalThis.tick = function () {
    frame++;
    st.now = Date.now();
    poll();
    saveStep();
    /* Parked behind another screen: keep state and saving going, draw nothing. */
    if (globalThis.overtakeParked) return;
    if ((frame & 15) === 0) updateWarning();
    pushLEDs();
    const showing = !!(st.banner && st.banner.until > st.now);
    if (showing !== bannerShown) { bannerShown = showing; dirty = true; }
    if (dirty) { draw(); dirty = false; }
};

globalThis.onUnload = function () {
    if (editsSinceSave > 0 || saveQueue.length > 0) saveNow();
    setP("perf:clear", "ffff 0");
    setP("panic", "1");
};
