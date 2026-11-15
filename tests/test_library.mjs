#!/usr/bin/env node
/* Library storage and menu checks. Run: node tests/test_library.mjs */
import {
    createLibrary, createMenu, cleanName, slugify, isSafeId, uniqueSetName, emptySet, normaliseSet, TOAST_MS,
} from "../src/e1/e1_library.mjs";

let pass = 0, fail = 0;
function check(name, cond, extra) {
    if (cond) pass++;
    else { fail++; console.log("FAIL " + name + (extra !== undefined ? "  " + JSON.stringify(extra) : "")); }
}
const eq = (a, b) => JSON.stringify(a) === JSON.stringify(b);
function checkEq(name, got, want) { check(name, eq(got, want), { got, want }); }

function memIO() {
    const files = new Map(), dirs = new Set();
    return {
        files, dirs,
        read: (p) => (files.has(p) ? files.get(p) : null),
        write: (p, t) => { files.set(p, String(t)); },
        ensureDir: (p) => { dirs.add(p); },
    };
}
const DIR = "/data/UserData/schwung/e1";
let tick = 100;
const fresh = () => { const io = memIO(); return { io, lib: createLibrary({ io, dir: DIR, now: () => ++tick }) }; };

/* ------------------------------------------------------------ names */
checkEq("clean trims and collapses", cleanName("  my   big\tset  "), "my big set");
checkEq("clean max 20", cleanName("abcdefghijklmnopqrstuvwxyz"), "abcdefghijklmnopqrst");
checkEq("clean empty -> Untitled", cleanName("   "), "Untitled");
checkEq("clean null -> Untitled", cleanName(null), "Untitled");
checkEq("clean newline", cleanName("a\nb"), "a b");
checkEq("slug basic", slugify("My Set #1!"), "my-set-1");
checkEq("slug empty", slugify("!!!"), "set");
check("slug safe and short", isSafeId(slugify("x".repeat(100))) && slugify("x".repeat(100)).length <= 36);
checkEq("uniqueSetName", uniqueSetName(["Set 1", "set 2"]), "Set 3");

/* ------------------------------------------------------ round trip */
{
    const { io, lib } = fresh();
    checkEq("empty library lists nothing", lib.listSets(), []);
    const s = emptySet("Techno Jam");
    s.globals = "G";
    s.trackNames[0] = "KICK";
    s.current = 2;
    s.patterns[2] = { name: "Intro", tracks: Array.from({ length: 16 }, (_, t) => "t" + t) };
    const id = lib.createSet("Techno Jam", s);
    checkEq("create id slug", id, "techno-jam");
    check("ensureDir called", io.dirs.has(DIR) && io.dirs.has(DIR + "/sets"));
    check("set file written", io.files.has(DIR + "/sets/techno-jam.json"));
    const back = lib.loadSet(id);
    checkEq("round trip globals", back.globals, "G");
    checkEq("round trip track name", back.trackNames[0], "KICK");
    checkEq("round trip pattern", back.patterns[2], s.patterns[2]);
    checkEq("round trip current", back.current, 2);
    checkEq("16 patterns", back.patterns.length, 16);
    checkEq("index entry", lib.listSets().map((e) => [e.id, e.name]), [["techno-jam", "Techno Jam"]]);
    const idx = JSON.parse(io.files.get(DIR + "/sets/index.json"));
    check("index shape", idx.v === 1 && idx.last === null && Array.isArray(idx.sets));

    back.globals = "G2";
    back.name = "Techno Jam II";
    const before = lib.listSets()[0].updated;
    lib.saveSet(id, back);
    checkEq("save updates file", lib.loadSet(id).globals, "G2");
    checkEq("save updates index name", lib.listSets()[0].name, "Techno Jam II");
    check("save bumps updated", lib.listSets()[0].updated > before);

    lib.setLast(id);
    checkEq("lastSetId", lib.lastSetId(), id);
    lib.setLast("nope");
    checkEq("setLast unknown -> null", lib.lastSetId(), null);
    check("loadSet unsafe id", lib.loadSet("../x") === null && lib.loadSet("missing") === null);
}

/* ------------------------------------------- unique ids and names */
{
    const { lib } = fresh();
    const a = lib.createSet("Dub");
    const b = lib.createSet("Dub");
    const c = lib.createSet("DUB!");
    checkEq("unique ids", [a, b, c], ["dub", "dub-2", "dub-3"]);
    checkEq("uniqueName start", lib.uniqueName(), "Set 1");
    lib.createSet("Set 1"); lib.createSet("set 2");
    checkEq("uniqueName skips used", lib.uniqueName(), "Set 3");
    checkEq("sorted case-insensitive", lib.listSets().map((e) => e.name), ["Dub", "Dub", "DUB!", "Set 1", "set 2"]);
    const long = lib.createSet("x".repeat(60));
    check("long id safe", isSafeId(long) && long.length <= 40, long);
    const idIndex = lib.createSet("index");
    check("id never 'index'", idIndex !== "index", idIndex);
    const blank = lib.createSet("   ");
    checkEq("blank name -> Untitled", lib.loadSet(blank).name, "Untitled");
}

/* --------------------------------------- rename, duplicate, delete */
{
    const { io, lib } = fresh();
    const id = lib.createSet("Groove");
    lib.setLast(id);
    check("rename ok", lib.renameSet(id, "  Deep   Groove "));
    checkEq("rename index", lib.listSets()[0].name, "Deep Groove");
    checkEq("rename file", lib.loadSet(id).name, "Deep Groove");
    checkEq("rename keeps id", lib.listSets()[0].id, id);
    check("rename missing", lib.renameSet("nope", "x") === false);

    const d1 = lib.duplicateSet(id);
    const d2 = lib.duplicateSet(id);
    const d3 = lib.duplicateSet(d1);
    checkEq("duplicate names", [d1, d2, d3].map((x) => lib.loadSet(x).name), ["Deep Groove 2", "Deep Groove 3", "Deep Groove 4"]);
    check("duplicate distinct ids", new Set([id, d1, d2, d3]).size === 4);
    check("duplicate missing", lib.duplicateSet("nope") === null);

    check("delete ok", lib.deleteSet(id));
    check("delete removes from index", !lib.listSets().some((e) => e.id === id));
    checkEq("delete blanks file", io.files.get(DIR + "/sets/" + id + ".json"), "");
    checkEq("delete clears last", lib.lastSetId(), null);
    check("load deleted -> null", lib.loadSet(id) === null);
    check("delete missing", lib.deleteSet(id) === false);
    const again = lib.createSet("Groove");
    checkEq("id reusable after delete", again, id);
}

/* ------------------------------------------------------ corruption */
{
    const { io, lib } = fresh();
    io.files.set(DIR + "/sets/index.json", "{not json");
    checkEq("corrupt index -> empty", lib.listSets(), []);
    checkEq("corrupt index last", lib.lastSetId(), null);
    const id = lib.createSet("After");
    checkEq("corrupt index recovers", lib.listSets().map((e) => e.id), [id]);

    io.files.set(DIR + "/sets/index.json", JSON.stringify({ v: 1, last: "../etc", sets: [{ id: "../etc", name: "x" }, { id: "ok", name: 5 }, null, "x"] }));
    checkEq("bad index entries dropped", lib.listSets().map((e) => e.id), ["ok"]);
    io.files.set(DIR + "/sets/index.json", JSON.stringify([1, 2]));
    checkEq("index wrong type", lib.listSets(), []);

    io.files.set(DIR + "/sets/bad.json", "garbage{");
    check("corrupt set -> null", lib.loadSet("bad") === null);
    io.files.set(DIR + "/sets/bad.json", "[]");
    check("array set -> null", lib.loadSet("bad") === null);
    io.files.set(DIR + "/sets/partial.json", JSON.stringify({ name: "P", patterns: [null, { name: "", tracks: ["a"] }, 7], trackNames: ["A"], current: 99 }));
    const p = lib.loadSet("partial");
    check("partial normalised", p && p.patterns.length === 16 && p.trackNames.length === 16 && p.current === -1);
    checkEq("partial pattern tracks padded", p.patterns[1].tracks.length, 16);
    checkEq("partial pattern unnamed", p.patterns[1].name, "Untitled");
    check("partial junk pattern -> null", p.patterns[2] === null);
    checkEq("partial track names padded", p.trackNames.slice(0, 2), ["A", ""]);
    checkEq("normalise junk", normaliseSet(42).name, "Untitled");

    const throwing = createLibrary({ io: { read: () => { throw new Error("io"); }, write: () => {}, ensureDir: () => {} }, dir: DIR });
    checkEq("read throwing -> empty", throwing.listSets(), []);
}

/* ---------------------------------------------------- track presets */
{
    const { io, lib } = fresh();
    checkEq("presets empty", lib.listTrackPresets(), []);
    checkEq("save preset idx", lib.saveTrackPreset("Tight Hat", "D1"), 0);
    lib.saveTrackPreset("Kick", "D2");
    checkEq("same name overwrites", lib.saveTrackPreset("tight hat", "D3"), 0);
    checkEq("list presets", lib.listTrackPresets(), [{ name: "tight hat" }, { name: "Kick" }]);
    checkEq("get preset", lib.getTrackPreset(0), { name: "tight hat", dump: "D3" });
    check("get missing", lib.getTrackPreset(9) === null);
    check("rename preset", lib.renameTrackPreset(1, " Big  Kick "));
    checkEq("renamed preset", lib.getTrackPreset(1).name, "Big Kick");
    check("delete preset", lib.deleteTrackPreset(0));
    checkEq("after delete", lib.listTrackPresets(), [{ name: "Big Kick" }]);
    check("delete missing preset", lib.deleteTrackPreset(5) === false);
    const f = JSON.parse(io.files.get(DIR + "/track_presets.json"));
    check("preset file shape", f.v === 1 && Array.isArray(f.presets) && f.presets[0].dump === "D2");
    io.files.set(DIR + "/track_presets.json", "oops");
    checkEq("corrupt presets -> empty", lib.listTrackPresets(), []);
}

/* =============================================================== menu */

function makeHost(over = {}) {
    const patterns = Array.from({ length: 16 }, () => ({ name: "", filled: false }));
    patterns[0] = { name: "Intro", filled: true };
    patterns[3] = { name: "Drop", filled: true };
    const state = {
        setId: "b", setName: "Beta", dirty: false,
        sets: [{ id: "a", name: "Alpha" }, { id: "b", name: "Beta" }, { id: "c", name: "Gamma" }],
        patterns, currentPattern: 0, focus: 2,
        trackNames: Array.from({ length: 16 }, (_, t) => (t === 2 ? "HAT" : "TR" + (t + 1))),
        presets: [{ name: "P one" }, { name: "P two" }],
        ...over,
    };
    const host = {
        state: () => state, acts: [], asks: [],
        act(name, args) { this.acts.push(args === undefined ? [name] : [name, args]); },
        askName(title, initial, cb) { this.asks.push({ title, initial, cb }); },
    };
    host.act = host.act.bind(host);
    host.askName = host.askName.bind(host);
    return { host, state };
}
let clockNow = 0;
function setup(over) {
    const { host, state } = makeHost(over);
    clockNow = 1000;
    const m = createMenu({ host, now: () => clockNow });
    m.open();
    return { m, host, state };
}
/* Move to row n from wherever the cursor is, by jogging to the top first. */
const go = (m, n) => { m.jog(-99); m.jog(n); };
const last = (h) => h.acts[h.acts.length - 1];

/* Root and settings */
{
    const { m, host } = setup();
    check("open", m.isOpen());
    checkEq("root path", m.path(), ["E-1"]);
    m.click();
    checkEq("settings act", host.acts, [["settings.open"]]);
    check("settings closes", !m.isOpen());
    m.open();
    check("back at root closes", m.back() === false && !m.isOpen());
    m.open(); go(m, 1); m.click();
    checkEq("sets path", m.path(), ["E-1", "Sets"]);
    check("back returns true when open", m.back() === true && m.isOpen());
}

/* Sets: save, save as, new */
{
    const { m, host } = setup();
    go(m, 1); m.click();
    m.click();
    checkEq("set.save", last(host), ["set.save"]);
    checkEq("toast saved", m.toast(), "SAVED");
    clockNow += TOAST_MS + 1;
    checkEq("toast expires", m.toast(), null);
    checkEq("stays at sets", m.path(), ["E-1", "Sets"]);

    go(m, 1); m.click();
    checkEq("save as ask", [host.asks[0].title, host.asks[0].initial], ["Save set as", "Beta"]);
    host.asks[0].cb("  New   Name ");
    checkEq("set.saveAs", last(host), ["set.saveAs", { name: "New Name" }]);
    const n = host.acts.length;
    host.asks[0].cb(""); host.asks[0].cb(null);
    checkEq("empty name ignored", host.acts.length, n);

    go(m, 2); m.click();
    checkEq("new set default", host.asks[1].initial, "Set 1");
    host.asks[1].cb("Fresh");
    checkEq("set.new", last(host), ["set.new", { name: "Fresh" }]);
    checkEq("created toast", m.toast(), "CREATED");
}

/* Set submenu: load clean, rename, duplicate, delete */
{
    const { m, host } = setup();
    go(m, 1); m.click();
    go(m, 5); m.click();   /* Gamma */
    checkEq("set submenu path", m.path(), ["E-1", "Sets", "Gamma"]);
    m.click();
    checkEq("clean load acts at once", host.acts, [["set.load", { id: "c" }]]);
    checkEq("load returns to list", m.path(), ["E-1", "Sets"]);
    checkEq("loaded toast", m.toast(), "LOADED");

    m.click(); go(m, 1); m.click();
    checkEq("rename ask", [host.asks[0].title, host.asks[0].initial], ["Rename set", "Gamma"]);
    host.asks[0].cb("Delta");
    checkEq("set.rename", last(host), ["set.rename", { id: "c", name: "Delta" }]);
    checkEq("rename back to list", m.path(), ["E-1", "Sets"]);

    m.click(); go(m, 2); m.click();
    checkEq("set.duplicate", last(host), ["set.duplicate", { id: "c" }]);
    checkEq("duplicate back to list", m.path(), ["E-1", "Sets"]);

    m.click(); go(m, 3); m.click();
    checkEq("delete confirm", m.path(), ["E-1", "Sets", "Gamma", "Delete set?"]);
    const n = host.acts.length;
    m.click();   /* defaults to Cancel */
    checkEq("cancel does nothing", host.acts.length, n);
    checkEq("cancel back to submenu", m.path(), ["E-1", "Sets", "Gamma"]);
    m.click(); m.jog(-1); m.click();
    checkEq("set.delete", last(host), ["set.delete", { id: "c" }]);
    checkEq("delete back to list", m.path(), ["E-1", "Sets"]);
    checkEq("deleted toast", m.toast(), "DELETED");
}

/* Dirty load confirm */
{
    const { m, host, state } = setup({ dirty: true });
    go(m, 1); m.click(); go(m, 3); m.click();   /* Alpha */
    m.click();
    checkEq("dirty confirm path", m.path(), ["E-1", "Sets", "Alpha", "Unsaved changes"]);
    checkEq("no act before choice", host.acts, []);
    m.click();   /* Save then load */
    checkEq("save then load", host.acts, [["set.save"], ["set.load", { id: "a" }]]);
    checkEq("after save-load list", m.path(), ["E-1", "Sets"]);

    host.acts.length = 0;
    m.click(); m.click(); m.jog(1); m.click();
    checkEq("load anyway", host.acts, [["set.load", { id: "a" }]]);

    host.acts.length = 0;
    m.click(); m.click(); m.jog(2); m.click();
    checkEq("cancel dirty", host.acts, []);
    checkEq("cancel dirty path", m.path(), ["E-1", "Sets", "Alpha"]);
    m.back();
    state.dirty = false;
}

/* Patterns */
{
    const { m, host } = setup();
    go(m, 2); m.click();
    checkEq("patterns path", m.path(), ["E-1", "Patterns"]);
    m.click();   /* 01 Intro, filled */
    checkEq("pattern path", m.path(), ["E-1", "Patterns", "P01 Intro"]);
    m.click();
    checkEq("pattern.goto", last(host), ["pattern.goto", { i: 0 }]);
    checkEq("goto back to list", m.path(), ["E-1", "Patterns"]);

    m.click(); go(m, 1); m.click();
    checkEq("pattern.save", last(host), ["pattern.save", { i: 0 }]);

    m.click(); go(m, 2); m.click();
    checkEq("pattern rename ask", host.asks[0].initial, "Intro");
    host.asks[0].cb("Outro");
    checkEq("pattern.rename", last(host), ["pattern.rename", { i: 0, name: "Outro" }]);
    checkEq("rename back to list", m.path(), ["E-1", "Patterns"]);

    m.click(); go(m, 3); m.click();
    checkEq("copy picker path", m.path(), ["E-1", "Patterns", "P01 Intro", "Copy P01 to"]);
    const n = host.acts.length;
    m.click();   /* same slot */
    checkEq("same slot no act", host.acts.length, n);
    checkEq("same slot toast", m.toast(), "SAME SLOT");
    go(m, 7); m.click();
    checkEq("pattern.copy", last(host), ["pattern.copy", { from: 0, to: 7 }]);
    checkEq("copy back to list", m.path(), ["E-1", "Patterns"]);

    m.click(); go(m, 4); m.click();
    checkEq("clear confirm", m.path(), ["E-1", "Patterns", "P01 Intro", "Clear P01?"]);
    m.jog(-1); m.click();
    checkEq("pattern.clear", last(host), ["pattern.clear", { i: 0 }]);
    checkEq("clear back to list", m.path(), ["E-1", "Patterns"]);

    /* Empty slot offers only Save here. */
    go(m, 1); m.click();
    m.jog(5); m.click();
    checkEq("empty slot save", last(host), ["pattern.save", { i: 1 }]);
}

/* Track presets */
{
    const { m, host } = setup();
    go(m, 3); m.click();
    m.click();
    checkEq("preset save ask", [host.asks[0].title, host.asks[0].initial], ["Preset name", "HAT"]);
    host.asks[0].cb("Hat A");
    checkEq("preset.save", last(host), ["preset.save", { name: "Hat A" }]);

    go(m, 2); m.click();
    checkEq("preset path", m.path(), ["E-1", "Track presets", "P two"]);
    m.click();
    checkEq("preset.load", last(host), ["preset.load", { i: 1 }]);
    checkEq("load back to list", m.path(), ["E-1", "Track presets"]);

    m.click(); go(m, 1); m.click();
    host.asks[1].cb("P2");
    checkEq("preset.rename", last(host), ["preset.rename", { i: 1, name: "P2" }]);

    m.click(); go(m, 2); m.click(); m.jog(-1); m.click();
    checkEq("preset.delete", last(host), ["preset.delete", { i: 1 }]);
    checkEq("preset delete back to list", m.path(), ["E-1", "Track presets"]);
}

/* Tracks */
{
    const { m, host } = setup();
    go(m, 4); m.click();
    go(m, 2); m.click();
    checkEq("track ask", [host.asks[0].title, host.asks[0].initial], ["Track 3 name", "HAT"]);
    host.asks[0].cb("OPEN HAT");
    checkEq("track.rename", last(host), ["track.rename", { t: 2, name: "OPEN HAT" }]);
    checkEq("renamed toast", m.toast(), "RENAMED");
    checkEq("tracks stays", m.path(), ["E-1", "Tracks"]);
    m.jog(100); m.click();
    checkEq("jog clamps to last track", host.asks[1].title, "Track 16 name");
}

/* Cursor memory and fresh state */
{
    const { m, host, state } = setup();
    go(m, 2); m.click(); m.jog(6); m.back();
    m.jog(-1); m.click();   /* Sets */
    m.back(); m.jog(1); m.click();   /* Patterns again */
    m.click();
    checkEq("cursor remembered", m.path()[2], "P07 empty");
    m.back(); m.back();
    checkEq("two backs reach root", m.path(), ["E-1"]);
    m.click();
    checkEq("root cursor at Patterns", m.path(), ["E-1", "Patterns"]);
    m.close(); m.open();
    checkEq("reopen at root", m.path(), ["E-1"]);
    m.click();
    checkEq("reopen keeps root cursor", m.path(), ["E-1", "Patterns"]);
    m.back();
    go(m, 1); m.click(); go(m, 5); m.click();
    state.sets[2].name = "Renamed Elsewhere";
    checkEq("title re-reads state", m.path()[2], "Renamed Elsewhere");
    m.back();
    state.sets.length = 1;
    m.jog(0);
    m.click();
    checkEq("cursor clamps when list shrinks", m.path()[2], "Alpha");
    void host;
}

/* Drawing stays on screen, scrolls, and uppercases */
{
    const { m, state } = setup();
    state.sets = Array.from({ length: 12 }, (_, i) => ({ id: "s" + i, name: "A very long set name " + i }));
    let out = 0, prints = [];
    const ctx = {
        fillRect(x, y, w, h) { if (x < 0 || y < 0 || x + w > 128 || y + h > 64) out++; },
        print(x, y, s) { prints.push(s); if (x < 0 || y < 0 || x + s.length * 6 > 129 || y + 7 > 64) out++; },
        textWidth: (s) => s.length * 6,
    };
    m.draw(ctx);
    checkEq("root draw in bounds", out, 0);
    check("draw uppercases", prints.includes("E-1") && prints.includes("SETTINGS"));
    m.jog(1); m.click(); m.jog(14);
    prints = []; m.draw(ctx);
    checkEq("scrolled draw in bounds", out, 0);
    check("long names truncated with ..", prints.some((p) => p.endsWith("..")));
    check("scrolled to cursor", prints.some((p) => p.startsWith("A VERY LONG SET NAME 11".slice(0, 10))));
    m.click(); m.click();
    prints = []; m.draw(ctx);
    check("toast drawn on footer", prints.includes("LOADED"), prints);
    clockNow += TOAST_MS;
    prints = []; m.draw(ctx);
    check("hint after toast", !prints.includes("LOADED") && prints.some((p) => p.startsWith("CLICK")), prints);
}

console.log(pass + "/" + (pass + fail) + " library checks passed");
process.exit(fail ? 1 : 0);
