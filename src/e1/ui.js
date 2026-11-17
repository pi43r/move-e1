/*
 * E-1 — overtake UI for Ableton Move (Schwung).
 *
 * All sequencing lives in dsp.so. This file reads one compact snapshot from
 * the DSP per frame, draws it, lights the pads, turns presses into edits,
 * and keeps sets, patterns and the live state on disk.
 *
 * The surface has one home and no pad pages:
 *   Top 16 pads      tracks. Tap = select, hold = aim knobs and moves at it.
 *                    Tap the selected one = hear one hit of it.
 *   Step row         the selected track's pulses, for the cycle shown.
 *   Bottom 16 pads   whatever the knob under your finger needs: divisions,
 *                    a keyboard for Pitch, cycles for Cycles, styles...
 *   Knobs 1-8        the page; Shift = each knob's second function.
 *   Left buttons     hold (or tap to latch): Pattern, Cycles, Temp, Perform.
 *   Mute             hold + track pads, applied together on release.
 *   Sample           hold + turn a knob = how much that knob's value wanders.
 *   Jog              page; click = home. Shift+jog = tempo.
 *   Menu             save, load, rename, settings.
 *
 * Inspired by the Torso T-1. Not affiliated with Torso Electronics.
 */

import { renderPageMovy } from "/data/UserData/schwung/shared/param_pages/render_page_movy.mjs";
import { PAGE_KNOBS } from "/data/UserData/schwung/shared/param_pages/page_plan.mjs";
import { buildMetaIndex } from "/data/UserData/schwung/shared/param_pages/param_meta.mjs";
import { decodeDelta } from "/data/UserData/schwung/shared/input_filter.mjs";
import {
    openTextEntry, isTextEntryActive, handleTextEntryMidi, drawTextEntry, tickTextEntry,
} from "/data/UserData/schwung/shared/text_entry.mjs";
import {
    PERFORM, CLOCKS, DESTS, COL, BTN, STEP_BASE, GLOBAL_KEYS,
    valuesFrom, metaFor, ledFrame, pageOptions, drawLayer, LAYER_COL, currentPages, pageIndex, knobKeys,
    padModel, padMode, padPages, padToTrack, padToSlot, trackName, AUTOS, SONG, songLabel, SOURCES,
    knobLeds, drawSelection, drawValueList, selectedKnob, drawCycleStrip,
} from "./e1_view.mjs";
import { createLibrary, createMenu, starterSet, cleanChain, chainLines, chainNext } from "./e1_library.mjs";

const LIB = { PAGE_KNOBS, buildMetaIndex };

const CC = {
    jogTurn: 14, jogClick: 3, shift: 49, menu: 50, back: 51, capture: 52,
    down: 54, up: 55, undo: 56, loop: 58, copy: 60, left: 62, right: 63,
    play: 85, rec: 86, mute: 88, sample: 118, del: 119,
    lPattern: 43, lCycles: 42, lRandom: 41, lPerform: 40, knob0: 71,
};
const LAYER_OF = { [CC.lPattern]: "pattern", [CC.lCycles]: "cycles", [CC.lRandom]: "random", [CC.lPerform]: "perform" };

const SAVE_DIR = "/data/UserData/schwung/e1";
const LIVE_FILE = SAVE_DIR + "/live.txt";
const SESSION_FILE = SAVE_DIR + "/session.json";
const LED_BUDGET = 20;          /* LED packets per tick; the out buffer holds ~64 */
const TOAST_MS = 900;
const LIST_MS = 700;             /* a value list stays this long after the last turn */
const BACK_HOLD_MS = 450;        /* hold Back this long to switch to Move */
const TAP_MS = 300;             /* a press shorter than this is a tap */
const DOUBLE_MS = 380;          /* two presses this close latch a hold button */
const UNDO_DEPTH = 24;
const GESTURE_MS = 1200;        /* edits closer than this share one undo step */
const IDENTITY = new Set(["name", "type", "channel", "output", "mute"]);

const st = {
    focus: 0, pageName: "Left", settings: false, setPage: 0, stepPage: 0, touched: -1,
    padKey: "steps", padPage: 0, kbOct: 3, selKnob: 0, listKey: null, listUntil: 0,
    shift: false, muteHeld: false, mutePend: 0, copyHeld: false, delHeld: false,
    randomHeld: false, tempHeld: false, tempLock: false, keepTemp: false, fzOn: false, fzLock: false, fzLen: 4, fzMask: 0xFFFF, muteLock: false, saveMode: false,
    layer: null, layerHeld: null, layerLock: null,
    held: new Set(), snap: null, values: {}, warn: null, now: 0,
    flashUntil: new Array(16).fill(0), toast: null,
    perfHeld: new Array(16).fill(false), latched: new Array(16).fill(false), perfMask: new Array(16).fill(0),
    soloMask: 0,
    patterns: new Array(16).fill(null), current: -1, queued: -1,
    chain: [], chainPos: -1, chainNextPos: -1, patDown: [],
    setId: null, setName: "Set 1", dirty: false,
    copySrc: -1, copyPat: -1, copyCyc: -1,
};
let pkeys = [];
let dirty = true;
let lastSnapStr = "";
let editsSinceSave = 0, idleTicks = 0, saveQueue = [], saveParts = [];
let sessionDirty = false;
let ledHave = {};
let knobAcc = [0, 0, 0, 0, 0, 0, 0, 0];
let undo = [], redo = [], lastEditAt = -1e9;
let audTracks = new Set();      /* tracks sounding a hand-played hit */
let delDownAt = 0, delUsed = false;
let toastShown = false, listShown = false;
let layerPressAt = 0, layerPadUsed = false, layerSkipLock = false;
let audNotes = {};              /* pad slot -> note being auditioned */
let cycPadsDown = new Set();
let wasTyping = false;
let backDownAt = 0;
let lib = null, menu = null;

/* ------------------------------------------------------------ DSP I/O */

function setP(k, v) { if (typeof host_module_set_param === "function") host_module_set_param(k, String(v)); }
function getP(k) { return typeof host_module_get_param === "function" ? host_module_get_param(k) : null; }

function edited() { editsSinceSave++; idleTicks = 0; dirty = true; if (!st.dirty) { st.dirty = true; sessionDirty = true; } }

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
        st.current = st.queued; st.queued = -1; sessionDirty = true; edited();
        if (st.chain.length >= 2 && st.chainNextPos >= 0) { st.chainPos = st.chainNextPos; chainQueueNext(); }
    } else if (st.chain.length >= 2 && st.chainPos >= 0 && st.queued < 0 && !(snap.q | 0)) chainQueueNext();   /* a loaded chain */
    if ((snap.wr | 0) !== lastWebRev) { if (lastWebRev >= 0) edited(); lastWebRev = snap.wr | 0; }
    if (snap.wq | 0) takeWebRequests();
    dirty = true;
}

/* ------------------------------------------------------------ web page */
/* Schwung's Tool tab shows web_ui.html. Track edits go straight to the DSP.
 * Set and pattern names live here, so they are handed to the DSP for the page
 * to read, and renames from the page are collected from a small queue. */
let lastWebRev = -1, lastNames = "";

function pushNames() {
    let f = "\"set_name\":" + JSON.stringify(String(st.setName || ""));
    for (let i = 0; i < 16; i++) {
        const p = st.patterns[i];
        f += ",\"p" + (i + 1) + "_name\":" + JSON.stringify(p ? String(p.name || "") : "") + ",\"p" + (i + 1) + "_used\":\"" + (p ? 1 : 0) + "\"";
    }
    if (f === lastNames) return;
    lastNames = f;
    setP("uinames", f);
}

function takeWebRequests() {
    for (let n = 0; n < 8; n++) {
        const r = getP("webreq");
        if (!r) break;
        const sp = r.indexOf(" ");
        const kind = sp < 0 ? r : r.slice(0, sp), arg = sp < 0 ? "" : r.slice(sp + 1);
        if (kind === "sname") {
            const name = arg.trim().slice(0, 24);
            if (!name) continue;
            if (st.setId) lib.renameSet(st.setId, name);
            st.setName = name; sessionDirty = true;
            toast("SET", name);
        } else if (kind === "pname") {
            const m = /^(\d+) ?(.*)$/.exec(arg);
            const i = m ? parseInt(m[1], 10) - 1 : -1;
            if (i < 0 || i > 15 || !st.patterns[i]) continue;
            st.patterns[i].name = m[2].trim().slice(0, 12);
            st.dirty = true; sessionDirty = true;
            toast("P" + (i + 1), st.patterns[i].name || "NO NAME");
        }
    }
}

function toast(tag, text) {
    st.toast = { tag: String(tag).toUpperCase(), text: String(text || "").toUpperCase(), until: st.now + TOAST_MS };
    dirty = true;
}

/* ------------------------------------------------------------ helpers */

function isCC(t) { return st.snap ? ((st.snap.ty >> t) & 1) === 1 : t >= 11; }
function pages() { return currentPages(st); }
function rel(d) { return d >= 0 ? "d+" + d : "d" + d; }
function hex(m) { return (m >>> 0).toString(16); }
function tname(t) { return "T" + (t + 1) + " " + trackName(st, t); }
function refreshLayer() {
    st.layer = st.layerHeld || st.layerLock;
    const rnd = st.layer === "random";
    if (rnd !== st.randomHeld) { st.randomHeld = rnd; knobAcc.fill(0); }
    dirty = true;
}

/* True on the second of two quick presses of the same button. */
const lastPress = {};
function doubleTap(name) {
    const d = st.now - (lastPress[name] || -1e9) < DOUBLE_MS;
    lastPress[name] = d ? -1e9 : st.now;
    return d;
}

/* Steps picked for step edit, as a BigInt mask. */
function big(h) { try { return BigInt("0x" + (h || "0")); } catch (e) { return 0n; } }
function stepMask() { return st.snap ? big(st.snap.sm) : 0n; }
function leaveStepEdit() { setP("t" + (st.focus + 1) + ":stepedit", "0"); stepPicking = false; toast("STEPS", "ALL"); }
let stepPicking = false, pickedNow = 0n;         /* Shift is still down from the first pick: more steps join */

function setFocus(t) {
    if (t === st.focus) return;
    if (st.snap && (st.snap.em | 0)) setP("t" + (st.focus + 1) + ":edit", "0");   /* leave cycle edit behind */
    if (stepMask() !== 0n) setP("t" + (st.focus + 1) + ":stepedit", "0");
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
    if ((m & 0xFFFF) === 0xFFFF) return "ALL";
    const ts = []; for (let t = 0; t < 16; t++) if ((m >> t) & 1) ts.push(t);
    if (ts.length === 1) return tname(ts[0]);
    return ts.length <= 4 ? ts.map((t) => "T" + (t + 1)).join(" ") : ts.length + " TRACKS";
}
function who() { return st.held.size > 1 ? maskLabel(heldMask()) : ""; }

function dumpAll() {
    const out = [];
    for (let t = 1; t <= 16; t++) out.push(getP("dump:" + t) || "");
    return out;
}

/* One undo step per gesture: the state before the first edit of a burst. */
function checkpoint() {
    redo = [];                                   /* a new edit ends the redo trail */
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

/* Put one track's saved settings onto another, keeping who it plays. */
function graft(fromText, dst) {
    const from = parseDump(fromText);
    const to = parseDump(getP("dump:" + (dst + 1)));
    for (const k of IDENTITY) { if (to.has(k)) from.set(k, to.get(k)); else from.delete(k); }
    setP("stage:" + (dst + 1), joinDump(from));
    setP("commit", "now");
}

/* ------------------------------------------------------------ edits */

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

function setTrack(key, value) {
    checkpoint();
    for (const t of targets()) setP("t" + (t + 1) + ":" + key, value);
    edited();
}

function baseKey(key) { return key && key.slice(0, 2) === "r." ? key.slice(2) : key; }

function showPadsFor(key) {
    const k = baseKey(key);
    if (!k || k === st.padKey) return;
    st.padKey = k;
    st.padPage = 0;
    dirty = true;
}

function turnKnob(i, delta) {
    const key = knobKeys(st)[i];
    if (!key || delta === 0) return;
    const m = metaFor(key);
    if (!m) return;
    st.selKnob = i;
    if (m.options) { st.listKey = key; st.listUntil = st.now + LIST_MS; dirty = true; }
    else st.listKey = null;
    /* Detents per value: small ranges move slowly so a value is easy to hit. */
    const div = m.div || 1;
    if (knobAcc[i] !== 0 && Math.sign(knobAcc[i]) !== Math.sign(delta)) knobAcc[i] = 0;
    knobAcc[i] += delta;
    const n = Math.trunc(knobAcc[i] / div);
    if (n === 0) return;
    knobAcc[i] -= n * div;
    showPadsFor(key);

    if (GLOBAL_KEYS.has(key)) {
        if (key === "bpm" || key === "quant" || key === "songroot" || key === "songscale" || key === "chordtrack") { setP(key, rel(n)); edited(); return; }
        const opts = m.options;
        const cur = Math.max(0, opts.indexOf(st.values[key]));
        setGlobal(key, opts[Math.max(0, Math.min(opts.length - 1, cur + n))]);
        return;
    }
    setTrack(key, rel(n));
    if (key === "type") toast("TYPE", n > 0 ? "CC TRACK" : "NOTE TRACK");
}

function nudge(key, d) {
    if (GLOBAL_KEYS.has(key)) { setP(key, rel(d)); edited(); return; }
    setTrack(key, rel(d));
}

function toggleMute(t) {
    checkpoint();
    const muted = st.snap ? (st.snap.mu >> t) & 1 : 0;
    setP("t" + (t + 1) + ":mute", muted ? "Off" : "On");
    toast(muted ? "ON" : "MUTE", tname(t));
    edited();
}

function applyMutes() {
    if (!st.mutePend) return;
    checkpoint();
    const mu = st.snap ? st.snap.mu | 0 : 0;
    let n = 0;
    for (let t = 0; t < 16; t++) {
        if (!((st.mutePend >> t) & 1)) continue;
        setP("t" + (t + 1) + ":mute", (mu >> t) & 1 ? "Off" : "On");
        n++;
    }
    st.mutePend = 0;
    toast("MUTE", n + (n === 1 ? " TRACK" : " TRACKS"));
    edited();
}

function clearTrack(t) {
    checkpoint();
    const p = "t" + (t + 1) + ":";
    setP(p + "pulses", "0"); setP(p + "rotate", "0");
    setP(p + "addmask", "0"); setP(p + "delmask", "0");
    for (let c = 1; c <= 16; c++) setP(p + "clearcycle", c);
    toast("CLEAR", tname(t));
    edited();
}

function copyTrack(src, dst) {
    if (src === dst) return;
    checkpoint();
    graft(getP("dump:" + (src + 1)), dst);
    toast("COPY", "T" + (src + 1) + " > T" + (dst + 1));
    edited();
}

function doUndo() {
    const prev = undo.pop();
    if (!prev) { toast("UNDO", "NOTHING LEFT"); return; }
    redo.push(dumpAll());
    if (redo.length > UNDO_DEPTH) redo.shift();
    stageAll(prev, "now");
    lastEditAt = -1e9;
    toast("UNDO", undo.length + " LEFT");
    edited();
}

function doRedo() {
    const next = redo.pop();
    if (!next) { toast("REDO", "NOTHING LEFT"); return; }
    undo.push(dumpAll());
    if (undo.length > UNDO_DEPTH) undo.shift();
    stageAll(next, "now");
    lastEditAt = -1e9;
    toast("REDO", redo.length + " LEFT");
    edited();
}

/* ------------------------------------------------------------ 0.4 moves */

function aimMask() { return heldMask() || (1 << st.focus); }

/* A new seed for the aimed tracks: their chances and random values change,
 * and with Loop set the new ones come round from now on. */
function reroll() {
    setTrack("rseed", String(1 + Math.floor(Math.random() * 0x7FFFFFFE)));
    toast("REROLL", who() || tname(st.focus));
}

const LOOPS = ["Free", "1", "2", "4", "8"];
function loopSay(v) { return v === "Free" ? "FREE" : v + (v === "1" ? " BAR" : " BARS"); }
function stepLoop(d) {
    const cur = Math.max(0, LOOPS.indexOf(st.values.loop));
    const next = LOOPS[Math.max(0, Math.min(LOOPS.length - 1, cur + d))];
    setTrack("loop", next);
    showPadsFor("loop");
    toast("LOOP", loopSay(next) + " " + who());
}

/* Dice: a new 16-step lane for one layer of the aimed tracks. The other
 * layers stay. Delete + pad puts the layer back to plain. */
function rollDice(layer, label) {
    if (st.delHeld) { setTrack("cleardice", layer); toast("DICE OFF", label + " " + who()); return; }
    setTrack("roll", layer);
    toast("DICE", label + " " + who());
}
function diceBack() {
    if (!st.snap || !(st.snap.dh | 0)) { toast("DICE", "NO EARLIER ROLL"); return; }
    setTrack("rollback", "1");
    toast("DICE", "BACK " + who());
}
function autoSay(v) { return v === "Off" ? "OFF" : v + (v === "1" ? " BAR" : " BARS"); }
function setAuto(v) { setTrack("autoroll", v); toast("AUTO ROLL", autoSay(v) + " " + who()); }
function stepAuto(d) {
    const cur = Math.max(0, AUTOS.indexOf(st.values.autoroll));
    setAuto(AUTOS[Math.max(0, Math.min(AUTOS.length - 1, cur + d))]);
}

/* Source: Random + Left/Right steps through the shapes. */
function stepSource(d) {
    const cur = Math.max(0, SOURCES.indexOf(st.values.source));
    const next = SOURCES[Math.max(0, Math.min(SOURCES.length - 1, cur + d))];
    setTrack("source", next);
    toast("SOURCE", next + " " + who());
}
function toggleFit() {
    const on = st.values.fit !== "On";
    setTrack("fit", on ? "On" : "Off");
    toast("FIT", (on ? "ON " : "OFF ") + who());
}

function stopRepeats() {
    const m = aimMask();
    if (!st.snap || !((st.snap.rp | 0) & m)) return;
    perf("stoprepeats", m, 0);
    toast("REPEATS", "STOPPED " + who());
}

function randomStart(on) {
    perf("randstart", aimMask(), on ? 1 : 0);
    toast("START", (on ? "RANDOM " : "IN LINE ") + who());
}

function auditionOff(t) { if (audTracks.delete(t)) setP("hit:" + (t + 1), "0"); }

/* ------------------------------------------------------------ perform */

function perf(move, mask, value) { setP("perf:" + move, hex(mask) + " " + value); }

function setSolo(mask) {
    st.soloMask = mask & 0xFFFF;
    setP("perf:solo", hex(st.soloMask) + " " + (st.soloMask ? 1 : 0));
}

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
            toast("DICE", "REROLLED");
        } else {
            if (p.move === "rotate") checkpoint();
            perf(p.move, mask, p.value);
            toast(p.label, maskLabel(mask));
        }
        edited();
        return;
    }
    if (st.latched[i]) {                       /* tap a latched pad to let go */
        st.latched[i] = false;
        disengage(i);
        toast(p.label, "OFF");
        return;
    }
    st.perfMask[i] = mask;
    engage(i);
    if (st.shift) { st.latched[i] = true; toast(p.label, "LATCHED"); }
    else { st.perfHeld[i] = true; toast(p.label, maskLabel(mask)); }
}

function releaseAllPerform() {
    for (let i = 0; i < 16; i++) { st.perfHeld[i] = false; st.latched[i] = false; }
    perf("clear", 0xFFFF, 0);
    st.soloMask = 0;
    st.fzOn = st.fzLock = false;
    dirty = true;
}

/* ------------------------------------------------------------ patterns */

function saveSession() {
    sessionDirty = false;
    if (typeof host_write_file !== "function") return;
    if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
    host_write_file(SESSION_FILE, JSON.stringify({
        v: 3, setId: st.setId, setName: st.setName, dirty: st.dirty, current: st.current, patterns: st.patterns,
        chain: st.chain,
    }));
}

function loadSession() {
    if (typeof host_read_file !== "function") return;
    const text = host_read_file(SESSION_FILE);
    if (!text) return;
    try {
        const o = JSON.parse(text);
        if (!o || o.v !== 3) return;
        if (Array.isArray(o.patterns)) for (let i = 0; i < 16; i++) st.patterns[i] = o.patterns[i] || null;
        if (typeof o.current === "number") st.current = o.current;
        setChain(cleanChain(o.chain), st.current);
        if (o.setName) st.setName = String(o.setName);
        st.setId = o.setId || null;
        st.dirty = !!o.dirty;
    } catch (e) { /* a broken file leaves the slots empty */ }
}

function patternLabel(i) { const p = st.patterns[i]; return "P" + (i + 1) + (p && p.name ? " " + p.name : ""); }

function savePattern(i) {
    const old = st.patterns[i];
    st.patterns[i] = { name: old && old.name ? old.name : "", tracks: dumpAll() };
    st.current = i;
    st.dirty = true; sessionDirty = true;
    toast("SAVED", patternLabel(i));
}

function recallPattern(i) {
    const p = st.patterns[i];
    if (!p) { toast("EMPTY", "SHIFT+PAD SAVES"); return; }
    if (st.chain.length) { st.chain = []; st.chainPos = st.chainNextPos = -1; sessionDirty = true; st.dirty = true; }
    const running = st.snap && st.snap.r;
    const now = !running || st.queued === i;      /* a second tap while it waits = switch now */
    checkpoint();
    stageAll(p.tracks, now ? "now" : "bar");
    if (now) { st.current = i; st.queued = -1; sessionDirty = true; toast("PLAY", patternLabel(i)); edited(); }
    else { st.queued = i; toast("NEXT", patternLabel(i)); }
}

/* Pattern chain: hold a pattern pad and tap more in order. The chain plays
 * each pattern for its length, then the next, and loops; a single tap ends it. */
function chainLabel() { return st.chain.map((c) => "P" + (c + 1)).join(">"); }

function setChain(chain, current) {
    st.chain = chain;
    st.chainPos = chain.length ? Math.max(0, chain.indexOf(current)) : -1;
    st.chainNextPos = -1;
}

/* Stage the chain's next pattern to land when the playing one has had its length. */
function chainQueueNext() {
    if (st.chain.length < 2 || st.chainPos < 0) return;
    const nx = chainNext(st.chain, st.chainPos, st.patterns);
    const cur = st.patterns[st.chain[st.chainPos]];
    if (!nx || !cur) return;
    stageAll(st.patterns[nx.slot].tracks, "after " + chainLines(cur.tracks, st.snap ? st.snap.qn : 2));
    st.queued = nx.slot; st.chainNextPos = nx.pos;
    dirty = true;
}

/* A pattern pad pressed while another is still held adds to the chain. */
function chainPress(i) {
    if (!st.patterns[i]) return;
    const first = st.patDown[0];
    if (st.chain.length === 0) {
        if (first === undefined || !st.patterns[first]) return;
        st.chain = [first];
        st.chainPos = -1;
        st.chainNextPos = st.queued === first ? 0 : -1;   /* the first is still on its way */
        if (st.current === first && st.queued < 0) st.chainPos = 0;
    }
    if (st.chain.length >= 16) return;
    st.chain.push(i);
    st.dirty = true; sessionDirty = true;
    if (st.chainPos >= 0 && st.queued < 0) chainQueueNext();   /* the first already plays: queue the second */
    toast("CHAIN", chainLabel());
}

function patternPress(i) {
    if (st.saveMode) { st.saveMode = false; savePattern(i); return; }
    if (st.patDown.length > 1 && !st.delHeld && !st.copyHeld && !st.shift) { chainPress(i); return; }
    if (st.delHeld) {
        if (!st.patterns[i]) return;
        st.patterns[i] = null;
        if (st.current === i) st.current = -1;
        st.dirty = true; sessionDirty = true;
        toast("CLEAR", "P" + (i + 1));
        return;
    }
    if (st.copyHeld) {
        if (st.copyPat < 0) { if (st.patterns[i]) { st.copyPat = i; toast("COPY", "P" + (i + 1) + " TO?"); } return; }
        const src = st.patterns[st.copyPat];
        st.patterns[i] = { name: src.name, tracks: src.tracks.slice() };
        st.dirty = true; sessionDirty = true;
        toast("COPY", "P" + (st.copyPat + 1) + " > P" + (i + 1));
        st.copyPat = -1;
        return;
    }
    if (st.shift) { savePattern(i); return; }
    recallPattern(i);
}

/* ------------------------------------------------------------ cycles */

function cyclePress(i, on) {
    if (!on) { cycPadsDown.delete(i); return; }
    const f = "t" + (st.focus + 1) + ":";
    if (st.delHeld) { checkpoint(); setP(f + "clearcycle", i + 1); toast("CLEAR", "CYCLE " + (i + 1)); edited(); return; }
    if (st.copyHeld) {
        if (st.copyCyc < 0) { st.copyCyc = i; toast("COPY", "CYCLE " + (i + 1) + " TO?"); return; }
        checkpoint();
        setP(f + "copycycle", (st.copyCyc + 1) + " " + (i + 1));
        toast("COPY", "C" + (st.copyCyc + 1) + " > C" + (i + 1));
        st.copyCyc = -1;
        edited();
        return;
    }
    if (st.shift) { setTrack("cycles", i + 1); toast("CYCLES", String(i + 1)); return; }
    const em = st.snap ? st.snap.em | 0 : 0;
    let next;
    if (cycPadsDown.size > 0) next = em | (1 << i);           /* hold one, tap more: edit them together */
    else next = em === (1 << i) ? 0 : (1 << i);               /* tap the picked one again: all cycles */
    cycPadsDown.add(i);
    setP(f + "edit", hex(next));
    if (next) toast("EDIT", "CYCLE " + (i + 1)); else toast("CYCLES", "ALL");
    dirty = true;
}

function goHome() {
    if (st.snap && (st.snap.em | 0)) setP("t" + (st.focus + 1) + ":edit", "0");
    if (stepMask() !== 0n) setP("t" + (st.focus + 1) + ":stepedit", "0");
    st.layerLock = null; st.layerHeld = null; st.saveMode = false; st.muteLock = false;
    if (st.settings) { st.settings = false; reselect(); }
    if (menu && menu.isOpen()) menu.close();
    refreshLayer();
}

/* ------------------------------------------------------------ input */

function pressPlay() {
    const ck = st.snap ? st.snap.ck | 0 : 0;
    if (st.shift) {
        const next = CLOCKS[(ck + 1) % 3];
        setP("clock", next);
        toast("CLOCK", next);
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

/* After the knobs change meaning, the same physical knob stays selected. */
function reselect() {
    st.listKey = null;
    const keys = knobKeys(st);
    let i = st.selKnob;
    if (!keys[i]) i = keys.findIndex((k) => k);
    if (i >= 0) { st.selKnob = i; showPadsFor(keys[i]); }
}

function changePage(d) {
    const p = pages();
    if (st.settings) {                /* Set, then Track */
        const n = Math.max(0, Math.min(p.length - 1, st.setPage + d));
        if (n === st.setPage) return;
        st.setPage = n;
        knobAcc.fill(0);
        reselect();
        dirty = true;
        return;
    }
    const i = pageIndex(p, st.pageName);
    const n = Math.max(0, Math.min(p.length - 1, i + d));
    if (p[n].name === st.pageName) return;
    st.pageName = p[n].name;
    knobAcc.fill(0);
    reselect();
    dirty = true;                    /* the header names the page; no pop-up */
}

function onStep(idx, on) {
    if (!on) return;
    const steps = (st.snap && st.snap.sn) || parseInt(st.values.steps, 10) || 16;
    const f = "t" + (st.focus + 1) + ":";
    if (st.fzOn) { st.fzLen = idx + 1; freezeApply(); freezeSay(); return; }
    if (st.randomHeld && !st.shift && !st.delHeld) {
        /* Mark a retrigger step: random, groove, phrase and style start again there. */
        if (idx >= 64) return;
        const was = (big(st.snap && st.snap.rt) >> BigInt(idx)) & 1n;
        checkpoint(); setP(f + "retrigtoggle", idx); edited();
        toast("RETRIG", "STEP " + (idx + 1) + (was ? " OFF" : " ON"));
        return;
    }
    if (st.shift) {
        /* Pick this step: knobs and value pads now change only it. */
        if (idx >= steps) return;
        const bit = 1n << BigInt(idx);
        const m = stepPicking ? (stepMask() | pickedNow | bit) : bit;
        pickedNow = m; stepPicking = true;
        if (!((big(st.snap && st.snap.h) >> BigInt(idx)) & 1n)) { checkpoint(); setP(f + "toggle", idx); edited(); }
        setP(f + "stepedit", m.toString(16));
        toast("STEP", "EDIT " + (idx + 1));
        return;
    }
    if (st.delHeld) {
        const bit = 1n << BigInt(idx);
        if ((big(st.snap && st.snap.se) | big(st.snap && st.snap.sc) | stepMask()) & bit) {
            /* In cycle edit this clears the step's values for that cycle and for every cycle. */
            checkpoint(); setP(f + "clearstep", idx + 1);
            if (stepMask() & bit) setP(f + "stepedit", (stepMask() & ~bit).toString(16));
            toast("CLEAR", "STEP " + (idx + 1)); edited(); return;
        }
        checkpoint(); setP(f + "clearedits", "1"); toast("CLEAR", "HAND EDITS"); edited(); return;
    }
    if (idx < steps) { checkpoint(); setP(f + "toggle", idx); edited(); }
}

function onTrackPad(t, on) {
    if (!on) { st.held.delete(t); auditionOff(t); dirty = true; return; }
    if (st.copyHeld) {
        if (st.copySrc < 0) { st.copySrc = t; toast("COPY", tname(t) + " TO?"); }
        else { copyTrack(st.copySrc, t); st.copySrc = -1; }
        return;
    }
    if (st.delHeld) { clearTrack(t); return; }
    if (st.fzOn && !st.muteHeld && !st.muteLock && !st.shift) {
        st.fzMask = (st.fzMask ^ (1 << t)) & 0xFFFF;
        freezeApply(); freezeSay();
        return;
    }
    if (st.muteHeld && !st.muteLock && !st.shift) { st.mutePend ^= 1 << t; dirty = true; return; }   /* applied on release */
    if (st.muteHeld || st.muteLock || st.shift) { toggleMute(t); return; }
    if (st.held.size === 0) {
        if (t !== st.focus) setFocus(t);
        else if (!(menu && menu.isOpen())) { setP("hit:" + (t + 1), "1"); audTracks.add(t); }   /* already selected: hear it */
    }
    st.held.add(t);
    dirty = true;
}

function onSlot(i, on, vel) {
    const mode = padMode(st);
    /* A note heard from this pad ends on release, whatever the pads show by then. */
    if (!on && audNotes[i]) { setP("aud:" + audNotes[i].t, audNotes[i].n + " 0"); delete audNotes[i]; }
    layerPadUsed = true;
    if (mode !== "pattern") st.patDown = [];
    if (mode === "perform") { perfPress(i, on); return; }
    if (mode === "cycles") { cyclePress(i, on); return; }
    if (mode === "pattern") {
        if (!on) { st.patDown = st.patDown.filter((x) => x !== i); return; }
        if (!st.patDown.includes(i)) st.patDown.push(i);
    }
    if (!on) return;
    if (mode === "pattern") { patternPress(i); return; }
    const act = padModel(st).pads[i].act;
    if (!act) return;
    switch (act.k) {
    case "dice": rollDice(act.layer, act.label); break;
    case "diceback": diceBack(); break;
    case "autoroll": setAuto(act.value); break;
    case "set":
        /* Shift + a scale or root pad sets the song's, whatever track is up. */
        if (st.shift && (act.key === "scale" || act.key === "root") && act.value !== SONG) {
            setGlobal("song" + act.key, act.value);
            toast("SONG " + act.key, act.value);
            break;
        }
        if (GLOBAL_KEYS.has(act.key)) setGlobal(act.key, act.value);
        else setTrack(act.key, act.value);
        toast(st.padKey, (act.label || act.value).replace(/^\S+\s/, "") + " " + who());
        break;
    case "nudge": nudge(act.key, act.d); break;
    case "pool":
        if (!st.shift) setTrack("pooltoggle", act.note);         /* Shift = just hear it */
        if (audNotes[i]) setP("aud:" + audNotes[i].t, audNotes[i].n + " 0");
        setP("aud:" + (st.focus + 1), act.note + " " + Math.max(1, vel));
        audNotes[i] = { t: st.focus + 1, n: act.note };
        break;
    case "randstart": randomStart(!st.shift); break;
    case "reroll": reroll(); break;
    case "fit": toggleFit(); break;
    case "oct":
        st.kbOct = Math.max(-2, Math.min(8, st.kbOct + act.d));
        dirty = true;
        break;
    default: break;
    }
}

function onNote(note, vel) {
    const on = vel > 0;
    if (on && st.delHeld) delUsed = true;
    if (note >= 0 && note <= 7) {               /* knob touch: the pads follow */
        if (on) {
            st.touched = note;
            st.selKnob = note;
            const key = knobKeys(st)[note];
            if (key && st.delHeld) {
                checkpoint();
                for (const t of targets()) setP("t" + (t + 1) + ":clearparam", baseKey(key));
                toast("CLEAR", (metaFor(baseKey(key)) || {}).name + " CYCLES");
                edited();
            }
            showPadsFor(key);
        } else if (st.touched === note) { st.touched = -1; if (st.listKey) st.listUntil = st.now + LIST_MS; }
        dirty = true;
        return;
    }
    if (note < 10) return;                       /* master/jog touch */
    if (note >= STEP_BASE && note < STEP_BASE + 16) { onStep(st.stepPage * 16 + (note - STEP_BASE), on); return; }
    const t = padToTrack(note);
    if (t >= 0) { onTrackPad(t, on); return; }
    const slot = padToSlot(note);
    if (slot >= 0) onSlot(slot, on, vel);
}

/* Left buttons: hold for as long as you need the layer, press twice to keep
 * it up, press once more to let it go. A single press of Cycles while a
 * cycle is being edited goes back to all cycles. */
function onLayerButton(cc, down) {
    const name = LAYER_OF[cc];
    if (down) {
        if (name === "perform" && st.shift) { releaseAllPerform(); toast("PERFORM", "ALL OFF"); return; }
        if (name === "random" && st.delHeld) { setTrack("clearrandom", "1"); toast("CLEAR", "RANDOM " + who()); return; }
        const wasLocked = st.layerLock === name;
        st.layerLock = null;                     /* one latch at a time */
        if (!wasLocked && doubleTap(name)) { st.layerLock = name; toast(name, "LATCHED"); }
        else if (wasLocked) lastPress[name] = -1e9;
        st.layerHeld = name;
        layerPressAt = st.now;
        layerPadUsed = false;
        layerSkipLock = wasLocked;
    } else if (st.layerHeld === name) {
        const tap = !layerPadUsed && st.now - layerPressAt < TAP_MS;
        st.layerHeld = null;
        if (name === "cycles" && tap && !layerSkipLock && st.layerLock !== "cycles" && st.snap && (st.snap.em | 0)) {
            setP("t" + (st.focus + 1) + ":edit", "0");
            lastPress.cycles = -1e9;
            toast("CYCLES", "ALL");
        }
        if (name === "perform" && st.layerLock !== "perform")
            for (let i = 0; i < 16; i++) if (st.perfHeld[i]) { st.perfHeld[i] = false; if (!st.latched[i]) disengage(i); }
    }
    refreshLayer();
}

function onMute(down) {
    if (down) {
        if (st.muteLock) { st.muteLock = false; lastPress.mute = -1e9; toast("MUTE", "LATCH OFF"); }
        else if (doubleTap("mute")) { st.muteLock = true; toast("MUTE", "LATCHED"); }
        st.muteHeld = true;
    } else {
        st.muteHeld = false;
        applyMutes();
    }
    dirty = true;
}

/* Loop freezes the last steps and repeats them. It takes the held tracks,
 * or all of them. While it is on, the step row sets how many steps repeat
 * and track pads take tracks in and out. Press twice to keep it on. */
function freezeApply() { perf("freeze", 0xFFFF, 0); if (st.fzOn && st.fzMask) perf("freeze", st.fzMask, st.fzLen); }
function freezeOff() { st.fzOn = false; st.fzLock = false; perf("freeze", 0xFFFF, 0); dirty = true; }
function freezeSay() {
    const m = st.fzMask & 0xFFFF;
    let n = 0, one = -1; for (let t = 0; t < 16; t++) if ((m >> t) & 1) { n++; one = t; }
    toast("FREEZE", st.fzLen + " ST " + (m === 0xFFFF ? "ALL" : n === 1 ? tname(one) : n + " TRACKS") + (st.fzLock ? " KEPT" : ""));
}
function onFreeze(down) {
    if (down) {
        if (st.fzLock) { freezeOff(); lastPress.freeze = -1e9; toast("FREEZE", "OFF"); return; }
        let m = 0; for (const t of st.held) m |= 1 << t;
        st.fzMask = m || 0xFFFF;
        st.fzOn = true; st.fzLock = doubleTap("freeze");
        freezeApply(); freezeSay();
    } else if (st.fzOn && !st.fzLock) freezeOff();
    dirty = true;
}

/* Temp: changes made while it is held are undone on release. Press twice to
 * keep Temp on, once more to undo. Capture keeps the changes. */
function onTemp(down) {
    if (down) {
        if (st.tempLock) {
            st.tempLock = false; lastPress.temp = -1e9;
            if (!st.keepTemp) { st.mutePend = 0; setP("temp", "0"); toast("TEMP", "RESTORED"); }
            return;
        }
        const lock = doubleTap("temp");
        st.tempHeld = true; st.keepTemp = false; st.tempLock = lock;
        setP("temp", "1");
        toast("TEMP", lock ? "LATCHED" : "LET GO TO UNDO");
    } else if (st.tempHeld) {
        st.tempHeld = false;
        /* Mutes picked under Temp and not yet applied go with it. */
        if (!st.tempLock && !st.keepTemp) { st.mutePend = 0; setP("temp", "0"); toast("TEMP", "RESTORED"); }
    }
}

/* Back steps out of whatever is open. Held, it hands the surface to Move
 * while E-1 keeps playing. Shift+Back is the host's and quits E-1. */
function stepBack() {
    if (st.saveMode) { st.saveMode = false; return true; }
    if (st.fzLock) { freezeOff(); toast("FREEZE", "OFF"); return true; }
    if (st.settings) { st.settings = false; reselect(); return true; }
    if (st.muteLock) { st.muteLock = false; return true; }
    if (stepMask() !== 0n) { leaveStepEdit(); return true; }
    if (st.snap && (st.snap.em | 0)) { setP("t" + (st.focus + 1) + ":edit", "0"); toast("CYCLES", "ALL"); return true; }
    if (st.layerLock) { st.layerLock = null; refreshLayer(); return true; }
    return false;
}

function toMove() {
    backDownAt = 0;
    if (typeof host_suspend_overtake === "function") host_suspend_overtake();
    else toast("BACK", "NEEDS NEWER SCHWUNG");
}

function onBack(down) {
    if (down) { backDownAt = st.now; return; }
    if (!backDownAt) return;                     /* the hold already fired */
    backDownAt = 0;
    if (!stepBack()) toast("HOLD BACK", "GO TO MOVE");
    dirty = true;
}

function onMenuInput(cc, v) {
    if (cc === CC.jogTurn) { const d = decodeDelta(v); if (d) menu.jog(d > 0 ? 1 : -1); return true; }
    if (v === 0) return cc !== CC.shift;
    if (cc === CC.jogClick) { menu.click(); return true; }
    if (cc === CC.back || cc === CC.left) { menu.back(); return true; }
    if (cc === CC.menu) { menu.close(); return true; }
    return cc !== CC.shift && cc !== CC.play;    /* everything else waits until the menu closes */
}

function onCC(cc, v) {
    const down = v > 0;
    if (cc === CC.del) {
        st.delHeld = down; dirty = true;
        if (menu && menu.isOpen()) return;
        /* Delete alone: in the Repeats view it cuts the repeats that are
         * running, in the Length view a tap jumps to a random start. */
        const view = padMode(st) === "param" ? st.padKey : null;
        if (down) { delDownAt = st.now; delUsed = false; if (view === "repeats") stopRepeats(); }
        else if (!delUsed && st.now - delDownAt < TAP_MS && view === "length") randomStart(true);
        return;
    }
    if (down && st.delHeld) delUsed = true;
    if (menu && menu.isOpen() && onMenuInput(cc, v)) { dirty = true; return; }
    if (cc === CC.shift) { st.shift = down; if (!down) stepPicking = false; knobAcc.fill(0); dirty = true; return; }
    if (cc === CC.mute) { onMute(down); return; }
    if (cc === CC.sample) { onTemp(down); return; }
    if (cc === CC.loop) { onFreeze(down); return; }
    if (cc === CC.copy) { st.copyHeld = down; if (!down) { st.copySrc = -1; st.copyPat = -1; st.copyCyc = -1; } dirty = true; return; }
    if (cc >= CC.knob0 && cc < CC.knob0 + 8) { turnKnob(cc - CC.knob0, decodeDelta(v)); return; }
    if (cc === CC.jogTurn) {
        const d = decodeDelta(v);
        if (!d) return;
        if (st.randomHeld) {                     /* the T-1's Random knob: chance, and Ctrl = rate */
            const key = st.shift ? "rate" : "random";
            setTrack(key, rel(st.shift ? (d > 0 ? 1 : -1) : d * 2));
            showPadsFor(key);
            toast(st.shift ? "RND RATE" : "RANDOM", who() || tname(st.focus));
            return;
        }
        if (st.shift) { setP("bpm", rel(d)); edited(); toast("TEMPO", String(Math.round((st.snap ? st.snap.bpm : 120) + d))); return; }
        changePage(d > 0 ? 1 : -1);
        return;
    }
    if (cc in LAYER_OF) { onLayerButton(cc, down); return; }
    if (cc === CC.back) { onBack(down); return; }
    if (!down) return;   /* the rest act on press */
    switch (cc) {
    case CC.play: pressPlay(); break;
    case CC.menu:
        if (st.shift) {
            const d = st.snap ? st.snap.d | 0 : 0;
            setGlobal("dest", DESTS[(d + 1) % 3]);
            toast("OUT", DESTS[(d + 1) % 3]);
        } else if (st.settings) { st.settings = false; reselect(); dirty = true; }
        else if (menu) { menu.open(); dirty = true; }
        break;
    case CC.jogClick: if (st.randomHeld) reroll(); else goHome(); break;
    case CC.up: case CC.down: {
        const d = cc === CC.up ? 1 : -1;
        if (st.randomHeld) { if (st.shift) stepAuto(d); else stepLoop(d); break; }
        if (st.padKey === "pitch") { st.kbOct = Math.max(-2, Math.min(8, st.kbOct + d)); toast("NOTES", "OCTAVE " + st.kbOct); break; }
        const n = padPages(st.padKey);
        const p = Math.max(0, Math.min(n - 1, st.padPage + d));
        if (p !== st.padPage) { st.padPage = p; toast("PADS", "PAGE " + (p + 1) + "/" + n); }
        break;
    }
    case CC.undo: if (st.shift) doRedo(); else doUndo(); break;
    case CC.capture:
        if (st.tempHeld || st.tempLock) { st.keepTemp = true; st.tempLock = false; setP("temp", "keep"); toast("TEMP", "KEPT"); edited(); }
        else { st.saveMode = !st.saveMode; st.toast = null; dirty = true; }
        break;
    case CC.left: case CC.right: {
        const d = cc === CC.right ? 1 : -1;
        if (st.randomHeld) { stepSource(d); break; }
        if (st.shift) { setTrack("rotate", rel(d)); toast("ROTATE", (d > 0 ? "+1 " : "-1 ") + who()); }
        else {
            const steps = (st.snap && st.snap.sn) || 16;
            const maxPage = Math.max(0, Math.ceil(steps / 16) - 1);
            const n = Math.max(0, Math.min(maxPage, st.stepPage + d));
            if (n !== st.stepPage) { st.stepPage = n; toast("STEPS", (n * 16 + 1) + "-" + Math.min(steps, n * 16 + 16)); }
        }
        break;
    }
    default: break;
    }
}

globalThis.onMidiMessageInternal = function (data) {
    if (!data || globalThis.overtakeParked) return;
    if (isTextEntryActive()) { handleTextEntryMidi(data); return; }
    const s = data[0] | 0, d1 = data[1] | 0, d2 = data[2] | 0, type = s & 0xF0;
    if (s >= 0xF8) return;
    if (type === 0x90) onNote(d1, d2);
    else if (type === 0x80) onNote(d1, 0);
    else if (type === 0xB0) onCC(d1, d2);
};

globalThis.onMidiMessageExternal = function () { /* E-1 ignores incoming notes for now */ };

/* ------------------------------------------------------------ library */

function setObject() {
    return {
        v: 1, name: st.setName,
        globals: JSON.stringify({ g: getP("dump:g") || "", live: dumpAll() }),
        trackNames: st.snap && st.snap.nm ? st.snap.nm.slice() : [],
        current: st.current,
        patterns: st.patterns,
        chain: st.chain,
    };
}

function applySet(id, set) {
    for (let i = 0; i < 16; i++) st.patterns[i] = set.patterns[i] || null;
    st.current = typeof set.current === "number" ? set.current : -1;
    st.queued = -1;
    setChain(cleanChain(set.chain), st.current);
    st.setId = id; st.setName = set.name;
    let extra = null;
    try { extra = JSON.parse(set.globals || "null"); } catch (e) { extra = null; }
    setP("load:g", (extra && extra.g) || "");      /* an older set without song settings: C minor, no chord track */
    const live = extra && Array.isArray(extra.live) ? extra.live
        : (st.current >= 0 && st.patterns[st.current] ? st.patterns[st.current].tracks : null);
    if (live) stageAll(live, "now");
    undo = []; redo = [];
    editsSinceSave++; idleTicks = 0;
    st.dirty = false; sessionDirty = true;
}

const host = {
    state() {
        return {
            setId: st.setId, setName: st.setName, dirty: st.dirty,
            sets: lib.listSets(),
            patterns: st.patterns.map((p) => ({ name: p ? p.name : "", filled: !!p })),
            currentPattern: st.current,
            focus: st.focus,
            trackNames: Array.from({ length: 16 }, (_, t) => trackName(st, t)),
            presets: lib.listTrackPresets(),
        };
    },
    notify(text) { toast("OK", text); },
    deleteHeld() { return st.delHeld; },
    askName(title, initial, onConfirm) {
        openTextEntry({ title, initialText: initial || "", onConfirm: (text) => { onConfirm(text); dirty = true; }, onCancel: () => { dirty = true; } });
    },
    act(name, a) {
        a = a || {};
        switch (name) {
        case "settings.open": st.settings = true; st.setPage = 0; reselect(); break;
        case "set.save":
            if (st.setId) lib.saveSet(st.setId, setObject()); else st.setId = lib.createSet(st.setName, setObject());
            lib.setLast(st.setId); st.dirty = false; sessionDirty = true;
            break;
        case "set.saveAs":
            st.setName = a.name;
            st.setId = lib.createSet(a.name, setObject());
            lib.setLast(st.setId); st.dirty = false; sessionDirty = true;
            break;
        case "set.new":
            st.patterns.fill(null); st.current = -1; st.queued = -1; setChain([], -1);
            st.setName = a.name;
            st.setId = lib.createSet(a.name, setObject());
            lib.setLast(st.setId); st.dirty = false; sessionDirty = true;
            break;
        case "set.starter": { const s = starterSet(a.i); if (s) { if (a.i === 0) s.name = lib.uniqueName(); applySet(null, s); st.dirty = false; lib.setLast(null); } break; }
        case "set.load": { const s = lib.loadSet(a.id); if (s) { applySet(a.id, s); lib.setLast(a.id); } break; }
        case "set.rename": lib.renameSet(a.id, a.name); if (a.id === st.setId) { st.setName = a.name; sessionDirty = true; } break;
        case "set.duplicate": lib.duplicateSet(a.id); break;
        case "set.delete": lib.deleteSet(a.id); if (a.id === st.setId) { st.setId = null; st.dirty = true; sessionDirty = true; } break;
        case "pattern.goto": recallPattern(a.i); break;
        case "pattern.save": savePattern(a.i); break;
        case "pattern.rename": if (st.patterns[a.i]) { st.patterns[a.i].name = a.name; st.dirty = true; sessionDirty = true; } break;
        case "pattern.copy":
            if (st.patterns[a.from]) { st.patterns[a.to] = { name: st.patterns[a.from].name, tracks: st.patterns[a.from].tracks.slice() }; st.dirty = true; sessionDirty = true; }
            break;
        case "pattern.clear": st.patterns[a.i] = null; if (st.current === a.i) st.current = -1; st.dirty = true; sessionDirty = true; break;
        case "preset.save": lib.saveTrackPreset(a.name, getP("dump:" + (st.focus + 1)) || ""); break;
        case "preset.load": { const p = lib.getTrackPreset(a.i); if (p) { checkpoint(); graft(p.dump, st.focus); edited(); } break; }
        case "preset.rename": lib.renameTrackPreset(a.i, a.name); break;
        case "preset.delete": lib.deleteTrackPreset(a.i); break;
        case "track.rename": checkpoint(); setP("t" + (a.t + 1) + ":name", String(a.name).toUpperCase().slice(0, 12)); edited(); break;
        default: break;
        }
        dirty = true;
    },
};

/* ------------------------------------------------------------ LEDs */

function buttonFrame() {
    const s = st.snap || {};
    const b = {};
    const w = (on) => (on ? BTN.on : BTN.dim);    /* white-LED buttons */
    /* A left button shows its layer's colour, and white while its layer is up. */
    const lay = (name) => (st.layer === name ? COL.white : LAYER_COL[name]);
    b[CC.play] = s.r ? COL.green : COL.grey;
    b[CC.sample] = s.tmp ? COL.yellow : COL.dim;
    b[CC.menu] = w((menu && menu.isOpen()) || st.settings);
    b[CC.shift] = w(st.shift);
    b[CC.mute] = w(st.muteHeld || st.muteLock);
    b[CC.loop] = w(st.fzOn);
    b[CC.capture] = w(st.saveMode);
    b[CC.undo] = (st.shift ? redo.length : undo.length) ? BTN.dim : BTN.off;
    b[CC.copy] = w(st.copyHeld);
    /* Bright in the Repeats view while repeats run: one press stops them. */
    b[CC.del] = w(st.delHeld || (padMode(st) === "param" && st.padKey === "repeats" && (((s.rp | 0) >> st.focus) & 1)));
    b[CC.back] = BTN.dim;
    const pp = padPages(st.padKey);
    b[CC.up] = st.padKey === "pitch" || st.padPage < pp - 1 ? BTN.on : BTN.dim;
    b[CC.down] = st.padKey === "pitch" || st.padPage > 0 ? BTN.on : BTN.dim;
    const steps = (s.sn | 0) || 16;
    b[CC.left] = st.stepPage > 0 ? BTN.on : BTN.off;
    b[CC.right] = (st.stepPage + 1) * 16 < steps ? BTN.on : BTN.off;
    b[CC.lPattern] = lay("pattern");
    b[CC.lCycles] = (s.em | 0) && st.layer !== "cycles" ? (Math.floor(st.now / 250) % 2 ? COL.red : COL.off) : lay("cycles");
    b[CC.lRandom] = lay("random");
    b[CC.lPerform] = lay("perform");
    const kl = knobLeds(st);
    for (let i = 0; i < 8; i++) b[CC.knob0 + i] = kl[i];
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

function listShowing() {
    if (!st.listKey) return false;
    const i = knobKeys(st).indexOf(st.listKey);
    return i >= 0 && (st.touched === i || st.listUntil > st.now);
}

function draw() {
    if (typeof clear_screen !== "function") return;
    if (isTextEntryActive()) { drawTextEntry(); return; }
    clear_screen();
    if (menu && menu.isOpen()) { menu.draw(ctx); return; }
    if (drawLayer(ctx, st)) return;
    if (listShowing() && drawValueList(ctx, st)) return;
    renderPageMovy(ctx, pageOptions(st, LIB));
    drawSelection(ctx, st);
    drawCycleStrip(ctx, st);
}

/* ------------------------------------------------------------ save/load */

function loadLive() {
    if (typeof host_read_file !== "function") return;
    const text = host_read_file(LIVE_FILE);
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

function writeLive(parts) {
    if (typeof host_write_file !== "function") return;
    if (typeof host_ensure_dir === "function") host_ensure_dir(SAVE_DIR);
    host_write_file(LIVE_FILE, parts.join("\n") + "\n");
}

/* One read per tick so saving never stalls the screen. Temp edits are not
 * saved until they are kept. */
function saveStep() {
    if (saveQueue.length === 0) {
        if (sessionDirty && idleTicks > 40) { saveSession(); return; }
        if (editsSinceSave === 0 || (st.snap && st.snap.tmp) || ++idleTicks < 120) { if (sessionDirty) idleTicks++; return; }
        editsSinceSave = 0;
        saveQueue = SECTIONS.slice();
        saveParts = [];
        return;
    }
    saveParts.push(part(saveQueue.shift()));
    if (saveQueue.length === 0) writeLive(saveParts);
}

function saveNow() { writeLive(SECTIONS.map(part)); }

/* ------------------------------------------------------------ lifecycle */

globalThis.init = function () {
    st.now = Date.now();
    const pk = getP("pkeys");
    pkeys = pk ? pk.split(",") : [];
    lib = createLibrary({
        io: {
            read: (p) => (typeof host_read_file === "function" ? host_read_file(p) : null),
            write: (p, t) => { if (typeof host_write_file === "function") host_write_file(p, t); },
            ensureDir: (p) => { if (typeof host_ensure_dir === "function") host_ensure_dir(p); },
        },
        dir: SAVE_DIR,
    });
    menu = createMenu({ host });
    loadLive();
    loadSession();
    setP("focus", st.focus + 1);
    ledHave = {};
    poll();
};

/* Back to E-1 from another screen. Buttons released while away never
 * reached us, so let go of everything held. */
globalThis.onResume = function () {
    st.now = Date.now();
    st.shift = st.muteHeld = st.copyHeld = st.delHeld = st.saveMode = false;
    st.mutePend = 0;
    st.held.clear();
    for (const t of [...audTracks]) auditionOff(t);
    st.copySrc = st.copyPat = st.copyCyc = -1;
    st.touched = -1;
    st.layerHeld = null;
    if (st.fzOn && !st.fzLock) freezeOff();
    backDownAt = 0;
    cycPadsDown.clear();
    for (const i in audNotes) setP("aud:" + audNotes[i].t, audNotes[i].n + " 0");
    audNotes = {};
    for (let i = 0; i < 16; i++) if (st.perfHeld[i]) { st.perfHeld[i] = false; if (!st.latched[i]) disengage(i); }
    st.saveMode = false;
    if (st.tempHeld) { st.tempHeld = false; if (!st.keepTemp && !st.tempLock) setP("temp", "0"); }
    knobAcc.fill(0);
    ledHave = {};
    lastSnapStr = "";
    refreshLayer();
};

let frame = 0;
globalThis.tick = function () {
    frame++;
    st.now = Date.now();
    poll();
    if ((frame % 20) === 0) pushNames();
    saveStep();
    /* Parked behind another screen: keep state and saving going, draw nothing. */
    if (globalThis.overtakeParked) return;
    const typing = isTextEntryActive();
    if (typing) { tickTextEntry(); draw(); wasTyping = true; return; }
    if (wasTyping) { wasTyping = false; ledHave = {}; dirty = true; }   /* text entry used the pads */
    if (backDownAt && st.now - backDownAt >= BACK_HOLD_MS) { toMove(); return; }
    if ((frame & 15) === 0) updateWarning();
    pushLEDs();
    const showing = !!(st.toast && st.toast.until > st.now);
    if (showing !== toastShown) { toastShown = showing; dirty = true; }
    const listing = listShowing();
    if (listing !== listShown) { listShown = listing; dirty = true; }
    if ((frame % 6) === 0 && ((menu && menu.isOpen()) || st.layer || st.saveMode)) dirty = true;
    if (dirty) { draw(); dirty = false; }
};

globalThis.onUnload = function () {
    if (editsSinceSave > 0 || saveQueue.length > 0) saveNow();
    if (sessionDirty) saveSession();
    setP("perf:clear", "ffff 0");
    setP("panic", "1");
};
