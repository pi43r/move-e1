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

/* The menu is one flat list acting on the loaded set, the playing pattern
 * and the selected track. Rows: 0 Save set, 1 Save set as, 2 Load set,
 * 3 Name pattern, 4 Name track, 5 Save preset, 6 Load preset, 7 Settings, 8 More. */
{
    const { m, host } = setup();
    host.notes = []; host.notify = (t) => host.notes.push(t);
    check("open", m.isOpen());
    checkEq("root path", m.path(), ["E-1"]);
    go(m, 0); m.click();
    checkEq("save set", last(host), ["set.save"]);
    check("a finished action closes the menu", !m.isOpen());
    checkEq("and reports on the main screen", host.notes, ["SAVED Beta"]);
}
{
    const { m, host } = setup({ setId: null, setName: "Set 1" });
    go(m, 0); m.click();
    check("save without a file asks for a name", host.asks.length === 1 && host.acts.length === 0);
    host.asks[0].cb("Night");
    checkEq("then saves as", last(host), ["set.saveAs", { name: "Night" }]);
}
{
    const { m, host } = setup();
    go(m, 2); m.click();
    checkEq("load list", m.path(), ["E-1", "Load set"]);
    go(m, 2); m.click();
    checkEq("clean: loads at once", last(host), ["set.load", { id: "c" }]);
    check("closed after load", !m.isOpen());
}
{
    const { m, host } = setup({ dirty: true });
    go(m, 2); m.click(); go(m, 0); m.click();
    check("dirty: asks first", host.acts.length === 0 && m.path().length === 3);
    m.click();
    checkEq("save, then load", host.acts, [["set.save"], ["set.load", { id: "a" }]]);
}
{
    const { m, host } = setup();
    go(m, 3); m.click(); host.asks[0].cb("Verse");
    checkEq("name the playing pattern", last(host), ["pattern.rename", { i: 0, name: "Verse" }]);
}
{
    const { m, host } = setup();
    go(m, 4); m.click();
    checkEq("track name starts from the current one", host.asks[0].initial, "HAT");
    host.asks[0].cb("Ride");
    checkEq("name the selected track", last(host), ["track.rename", { t: 2, name: "Ride" }]);
}
{
    const { m, host } = setup({ currentPattern: -1 });
    go(m, 3); m.click();
    check("no pattern playing: the row is not there", host.asks[0].title.startsWith("Track"));
}
{
    const { m, host } = setup();
    go(m, 5); m.click(); host.asks[0].cb("Tight hat");
    checkEq("save preset", last(host), ["preset.save", { name: "Tight hat" }]);
    m.open(); go(m, 6); m.click(); go(m, 1); m.click();
    checkEq("load preset", last(host), ["preset.load", { i: 1 }]);
    m.open(); go(m, 7); m.click();
    checkEq("settings", last(host), ["settings.open"]);
    check("settings closes the menu", !m.isOpen());
}
{
    const { m, host } = setup();
    go(m, 8); m.click();
    checkEq("more", m.path(), ["E-1", "More"]);
    go(m, 3); m.click(); go(m, 0); m.click();
    check("delete asks first", host.acts.length === 0);
    m.jog(1); m.click();
    check("cancel is the default and deletes nothing", host.acts.length === 0);
    m.click(); m.jog(-1); m.click();
    checkEq("delete set", last(host), ["set.delete", { id: "a" }]);
    checkEq("after delete: back in More", m.path(), ["E-1", "More"]);
    check("back steps up", m.back() === true && m.path().length === 1);
    check("back at the top closes", m.back() === false && !m.isOpen());
}
{
    const { m } = setup({ sets: [], presets: [] });
    go(m, 2); m.click(); m.click();
    checkEq("empty list: click goes back", m.path(), ["E-1"]);
    const prints = [];
    m.draw({ print: (x, y, t) => prints.push(t), fillRect() {}, textWidth: (t) => t.length * 6 });
    check("draws title and hint", prints.includes("E-1") && prints.some((p) => p.startsWith("CLICK")), prints);
}

console.log(pass + "/" + (pass + fail) + " library checks passed");
process.exit(fail ? 1 : 0);
