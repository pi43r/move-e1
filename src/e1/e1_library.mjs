/*
 * E-1 library: named sets, pattern slots and track presets stored as JSON,
 * plus the jog-wheel menu that manages them.
 *
 * No device globals. File I/O comes in through `io`, the host app through
 * `host`, and drawing through `ctx`, so all of it runs under node for tests.
 */

export const SLOTS = 16;
export const TRACKS = 16;
export const NAME_MAX = 20;
const ID_MAX = 40;

/* ------------------------------------------------------------------ names */

/* Names end up on a 21-char display line and in JSON, so keep them short and
 * single-line. Callers that must not fall back (track names) pass "". */
export function cleanName(name, fallback = "Untitled") {
    let s = name == null ? "" : String(name);
    s = s.replace(/[\u0000-\u001f\u007f]/g, " ").replace(/\s+/g, " ").trim();
    if (s.length > NAME_MAX) s = s.slice(0, NAME_MAX).trim();
    return s || fallback;
}

export function slugify(name) {
    const s = String(name == null ? "" : name).toLowerCase()
        .replace(/[^a-z0-9_-]+/g, "-").replace(/^[-_]+|[-_]+$/g, "");
    /* Leave room for a "-NNN" suffix inside the 40 char limit. */
    return s.slice(0, ID_MAX - 4).replace(/[-_]+$/g, "") || "set";
}

const SAFE_ID = /^[a-z0-9_-]{1,40}$/;
export function isSafeId(id) { return typeof id === "string" && SAFE_ID.test(id); }

/* "Set 1", "Set 2", ...: first number whose name is free (case-insensitive). */
export function uniqueSetName(names, base = "Set") {
    const used = new Set((names || []).map((n) => String(n).toLowerCase()));
    const b = cleanName(base, "Set");
    for (let n = 1; ; n++) {
        const tail = " " + n;
        const name = b.slice(0, NAME_MAX - tail.length).trim() + tail;
        if (!used.has(name.toLowerCase())) return name;
    }
}

/* "Groove" -> "Groove 2", "Groove 2" -> "Groove 3": duplicates count up from
 * the source's own number rather than stacking suffixes. */
function copyName(names, name) {
    const used = new Set((names || []).map((n) => String(n).toLowerCase()));
    const m = /^(.*?) (\d+)$/.exec(name);
    const base = m && m[1] ? m[1] : name;
    for (let n = m && m[1] ? Number(m[2]) + 1 : 2; ; n++) {
        const tail = " " + n;
        const cand = base.slice(0, NAME_MAX - tail.length).trim() + tail;
        if (!used.has(cand.toLowerCase())) return cand;
    }
}

/* Opaque payloads are text; accept a structured value without losing it. */
function asText(v) {
    if (typeof v === "string") return v;
    if (v == null) return "";
    try { return JSON.stringify(v); } catch (e) { return ""; }
}

function parse(text) {
    if (typeof text !== "string" || !text.length) return null;
    try { return JSON.parse(text); } catch (e) { return null; }
}

function isObj(v) { return v !== null && typeof v === "object" && !Array.isArray(v); }

/* ----------------------------------------------------------------- sets */

export function emptySet(name) {
    return {
        v: 1,
        name: cleanName(name),
        globals: "",
        trackNames: Array.from({ length: TRACKS }, () => ""),
        current: -1,
        patterns: Array.from({ length: SLOTS }, () => null),
    };
}

/* Accept anything a file might contain and return a complete set. */
export function normaliseSet(raw) {
    const src = isObj(raw) ? raw : {};
    const set = emptySet(src.name);
    set.globals = asText(src.globals);
    const tn = Array.isArray(src.trackNames) ? src.trackNames : [];
    for (let t = 0; t < TRACKS; t++) set.trackNames[t] = cleanName(tn[t], "");
    const pats = Array.isArray(src.patterns) ? src.patterns : [];
    for (let i = 0; i < SLOTS; i++) {
        const p = pats[i];
        if (!isObj(p)) continue;
        const tr = Array.isArray(p.tracks) ? p.tracks : [];
        set.patterns[i] = {
            name: cleanName(p.name),
            tracks: Array.from({ length: TRACKS }, (_, t) => asText(tr[t])),
        };
    }
    const c = Number(src.current);
    set.current = Number.isInteger(c) && c >= 0 && c < SLOTS ? c : -1;
    return set;
}

/* ------------------------------------------------------------- starters */

/* Track dumps load on top of an empty track, so each lists only what differs.
 * Layout as in the engine's starter: 1-8 drums on channel 1, 9-11 notes on
 * channels 2-4, 12-16 CC on channels 5-9. */
function starterTracks(over) {
    const base = (t) => {
        if (t < 8) return { channel: 1 };
        if (t < 11) return { channel: t - 6, scale: "Minor", sustain: 40, pool: [36, "60,63,67", "72,75,79"][t - 8] };
        return { type: "CC", channel: t - 6 };
    };
    return Array.from({ length: TRACKS }, (_, t) => {
        const kv = Object.assign({ pulses: 0 }, base(t), over[t] || {});
        return Object.keys(kv).map((k) => k + "=" + kv[k]).join("\n");
    });
}

const STARTERS = [
    { name: "Init", tracks: {} },
    { name: "Drums", tracks: {
        0: { pulses: 4 }, 1: { pulses: 2, rotate: 4 }, 2: { pulses: 8, velocity: 80 }, 3: { pulses: 3, rotate: 2 },
        4: { pulses: 5, rotate: 1, velocity: 90 }, 5: { pulses: 3, rotate: 6 }, 6: { pulses: 2, rotate: 3 }, 7: { pulses: 7, velocity: 70 },
    } },
    { name: "Drums Bass Chords", tracks: {
        0: { pulses: 4 }, 1: { pulses: 2, rotate: 4 }, 2: { pulses: 8, velocity: 80 }, 4: { pulses: 5, rotate: 1, velocity: 90 },
        8: { pulses: 5 }, 9: { pulses: 3, division: "1/8", sustain: 60 },
        10: { steps: 12, pulses: 7, style: "MonoRamp", voicing: 2 },
    } },
    { name: "Generative", tracks: {
        0: { steps: 16, pulses: 3 }, 2: { steps: 12, pulses: 5, velocity: 75 }, 4: { steps: 9, pulses: 7, velocity: 85 }, 6: { steps: 7, pulses: 2 },
        8: { steps: 13, pulses: 5, style: "MonoClimb", range: 5, phrase: "Tri" },
        9: { pulses: 3, division: "1/8", sustain: 70, style: "PolyRamp", voicing: 3, harmony: 2 },
        10: { steps: 11, pulses: 6, style: "MonoRamp", phrase: "Sine", range: 9 },
    } },
];

export const STARTER_NAMES = STARTERS.map((s) => s.name);

/* A complete set for starter i, playing pattern 1. */
export function starterSet(i) {
    const s = STARTERS[i];
    if (!s) return null;
    const tracks = starterTracks(s.tracks);
    const set = emptySet(s.name);
    set.globals = JSON.stringify({ g: "", live: tracks });
    set.patterns[0] = { name: s.name === "Init" ? "" : s.name, tracks: tracks.slice() };
    set.current = 0;
    return set;
}

export function createLibrary({ io, dir, now } = {}) {
    const clock = typeof now === "function" ? now : () => Date.now();
    const root = String(dir || ".").replace(/\/+$/, "");
    const setsDir = root + "/sets";
    const indexPath = setsDir + "/index.json";
    const presetsPath = root + "/track_presets.json";
    const setPath = (id) => setsDir + "/" + id + ".json";

    const read = (p) => { try { return io.read(p); } catch (e) { return null; } };
    const write = (p, text) => {
        try { io.ensureDir(root); io.ensureDir(setsDir); } catch (e) { /* write reports it */ }
        io.write(p, text);
    };

    function readIndex() {
        const j = parse(read(indexPath));
        const idx = { v: 1, last: null, sets: [] };
        if (!isObj(j) || !Array.isArray(j.sets)) return idx;
        const seen = new Set();
        for (const e of j.sets) {
            if (!isObj(e) || !isSafeId(e.id) || seen.has(e.id)) continue;
            seen.add(e.id);
            idx.sets.push({ id: e.id, name: cleanName(e.name), updated: Number(e.updated) || 0 });
        }
        if (isSafeId(j.last) && seen.has(j.last)) idx.last = j.last;
        return idx;
    }
    const writeIndex = (idx) => write(indexPath, JSON.stringify(idx));

    function uniqueId(idx, name) {
        const base = slugify(name);
        const used = new Set(idx.sets.map((e) => e.id));
        if (!used.has(base) && base !== "index") return base;
        for (let n = 2; ; n++) {
            const id = base + "-" + n;
            if (!used.has(id)) return id;
        }
    }

    function putEntry(idx, id, name) {
        const e = idx.sets.find((s) => s.id === id);
        const updated = clock();
        if (e) { e.name = name; e.updated = updated; }
        else idx.sets.push({ id, name, updated });
    }

    function readPresets() {
        const j = parse(read(presetsPath));
        const list = [];
        if (isObj(j) && Array.isArray(j.presets)) {
            for (const p of j.presets) {
                if (isObj(p)) list.push({ name: cleanName(p.name), dump: asText(p.dump) });
            }
        }
        return list;
    }
    const writePresets = (list) => write(presetsPath, JSON.stringify({ v: 1, presets: list }));

    const lib = {
        listSets() {
            return readIndex().sets
                .map((e) => ({ id: e.id, name: e.name, updated: e.updated }))
                .sort((a, b) => {
                    const x = a.name.toLowerCase(), y = b.name.toLowerCase();
                    return x < y ? -1 : x > y ? 1 : (a.id < b.id ? -1 : a.id > b.id ? 1 : 0);
                });
        },

        loadSet(id) {
            if (!isSafeId(id)) return null;
            const j = parse(read(setPath(id)));
            return isObj(j) ? normaliseSet(j) : null;
        },

        saveSet(id, setObj) {
            if (!isSafeId(id)) return false;
            const set = normaliseSet(setObj);
            write(setPath(id), JSON.stringify(set));
            const idx = readIndex();
            putEntry(idx, id, set.name);
            writeIndex(idx);
            return true;
        },

        createSet(name, setObj) {
            const set = normaliseSet(setObj || emptySet(name));
            if (name != null) set.name = cleanName(name);
            const idx = readIndex();
            const id = uniqueId(idx, set.name);
            write(setPath(id), JSON.stringify(set));
            putEntry(idx, id, set.name);
            writeIndex(idx);
            return id;
        },

        renameSet(id, name) {
            const idx = readIndex();
            const e = idx.sets.find((s) => s.id === id);
            if (!e) return false;
            const clean = cleanName(name);
            const set = lib.loadSet(id);
            if (set) { set.name = clean; write(setPath(id), JSON.stringify(set)); }
            putEntry(idx, id, clean);
            writeIndex(idx);
            return true;
        },

        duplicateSet(id) {
            const set = lib.loadSet(id);
            if (!set) return null;
            const names = readIndex().sets.map((e) => e.name);
            return lib.createSet(copyName(names, set.name), set);
        },

        deleteSet(id) {
            const idx = readIndex();
            const before = idx.sets.length;
            idx.sets = idx.sets.filter((s) => s.id !== id);
            if (idx.sets.length === before) return false;
            if (idx.last === id) idx.last = null;
            /* No delete on the device: blank the file so a stale copy cannot
             * come back if the id is reused. */
            write(setPath(id), "");
            writeIndex(idx);
            return true;
        },

        lastSetId() { return readIndex().last; },

        setLast(id) {
            const idx = readIndex();
            idx.last = id != null && idx.sets.some((s) => s.id === id) ? id : null;
            writeIndex(idx);
        },

        uniqueName(base = "Set") {
            return uniqueSetName(readIndex().sets.map((e) => e.name), base);
        },

        emptySet,

        listTrackPresets() { return readPresets().map((p) => ({ name: p.name })); },

        /* Same name (case-insensitive) overwrites in place; returns the index. */
        saveTrackPreset(name, dump) {
            const list = readPresets();
            const clean = cleanName(name);
            let i = list.findIndex((p) => p.name.toLowerCase() === clean.toLowerCase());
            if (i >= 0) list[i] = { name: clean, dump: asText(dump) };
            else { list.push({ name: clean, dump: asText(dump) }); i = list.length - 1; }
            writePresets(list);
            return i;
        },

        getTrackPreset(i) {
            const p = readPresets()[i];
            return p ? { name: p.name, dump: p.dump } : null;
        },

        renameTrackPreset(i, name) {
            const list = readPresets();
            if (!list[i]) return false;
            list[i].name = cleanName(name);
            writePresets(list);
            return true;
        },

        deleteTrackPreset(i) {
            const list = readPresets();
            if (!list[i]) return false;
            list.splice(i, 1);
            writePresets(list);
            return true;
        },
    };
    return lib;
}

/* ------------------------------------------------------------------ menu */

/* Movy-like geometry. The title rule sits at y=9 rather than 10 so five 9px
 * rows (11..55) and a 7px footer (57..63) fit without overlapping. */
export const MENU_LAYOUT = {
    W: 128, H: 64, TITLE_Y: 1, RULE_Y: 9, LIST_Y: 11, ROW_H: 9, ROWS: 5,
    FOOTER_Y: 57, TEXT_X: 3, BAR_W: 3,
};
const L = MENU_LAYOUT;
export const TOAST_MS = 1000;
const HINT = "CLICK: OK   BACK: UP";

const pad2 = (n) => (n < 9 ? "0" : "") + (n + 1);

export function createMenu({ host, now } = {}) {
    const clock = typeof now === "function" ? now : () => Date.now();
    let stack = [];
    let isOpenFlag = false;
    let toast = null;
    const memory = {};   /* cursor per level key, so re-entering a list lands where you left */

    const st = () => {
        const s = (host && host.state && host.state()) || {};
        return {
            setId: s.setId == null ? null : s.setId,
            setName: s.setName || "",
            dirty: !!s.dirty,
            sets: Array.isArray(s.sets) ? s.sets : [],
            patterns: Array.from({ length: SLOTS }, (_, i) => {
                const p = Array.isArray(s.patterns) ? s.patterns[i] : null;
                return { name: (p && p.name) || "", filled: !!(p && p.filled) };
            }),
            currentPattern: Number.isInteger(s.currentPattern) ? s.currentPattern : -1,
            focus: Number.isInteger(s.focus) ? Math.max(0, Math.min(TRACKS - 1, s.focus)) : 0,
            trackNames: Array.from({ length: TRACKS }, (_, t) => (Array.isArray(s.trackNames) && s.trackNames[t]) || ""),
            presets: Array.isArray(s.presets) ? s.presets : [],
        };
    };
    const act = (name, args) => { if (host && host.act) host.act(name, args); };
    const say = (text) => { toast = { text, until: clock() + TOAST_MS }; };

    const keyOf = (lv) => lv.kind + (lv.id != null ? ":" + lv.id : "") + (lv.i != null ? ":" + lv.i : "");
    const top = () => stack[stack.length - 1];

    function push(lv) {
        lv.cur = lv.kind === "confirm" ? (lv.cur || 0) : (memory[keyOf(lv)] || 0);
        lv.scroll = 0;
        stack.push(lv);
    }
    function pop() {
        const lv = stack.pop();
        if (lv && lv.kind !== "confirm") memory[keyOf(lv)] = lv.cur;
        return lv;
    }
    /* Return to the list that owns `lv` (its parent), if `lv` is still live. */
    function backFrom(lv) {
        const at = stack.indexOf(lv);
        if (at < 1) return;
        while (stack.length > at) pop();
    }
    /* Text entry is asynchronous; the menu may have moved on by the time the
     * name comes back, so act regardless but only navigate if still there. */
    function ask(title, initial, fn) {
        if (!host || !host.askName) return;
        host.askName(title, initial, (text) => {
            if (text == null) return;
            const name = cleanName(text, "");
            if (!name) return;
            fn(name);
        });
    }
    function confirm(title, right, yes, yesLabel, opts = {}) {
        const rows = opts.rows || [
            { label: yesLabel, fn: yes },
            { label: "Cancel", fn: () => pop() },
        ];
        push({ kind: "confirm", title, right, rows, cur: opts.cur != null ? opts.cur : rows.length - 1 });
    }

    const setName = (s, id) => {
        const e = s.sets.find((x) => x.id === id);
        return e ? cleanName(e.name) : "";
    };
    
    /* An action that finishes closes the menu and reports on the main screen. */
    const done = (text) => {
        close();
        if (host && host.notify) host.notify(text); else say(text);
    };
    const pickSet = (s, fn) => s.sets.map((e) => ({ label: cleanName(e.name), mark: e.id === s.setId, fn: () => fn(e.id) }));
    const none = (text) => [{ label: text, fn: () => pop() }];

    /* Short lists. Everything acts on what is loaded, playing or
     * selected. In the Load list, hold Delete and click to remove a set. */
    const LEVELS = {
        root: {
            title: () => "E-1",
            right: (s) => (s.setName || "") + (s.dirty ? "*" : ""),
            rows: (s) => {
                const names = s.sets.map((e) => e.name);
                const saveAs = () => ask("Save set as", s.setName || uniqueSetName(names),
                    (name) => { act("set.saveAs", { name }); done("SAVED " + name); });
                return [
                    { label: "Save", fn: () => { if (s.setId == null) return saveAs(); act("set.save"); done("SAVED " + s.setName); } },
                    { label: "Save as...", fn: saveAs },
                    { label: "Load...", fn: () => push({ kind: "loadSet" }) },
                    { label: "New...", fn: () => push({ kind: "newSet" }) },
                    { label: "Rename...", fn: () => push({ kind: "rename" }) },
                    { label: "Settings", fn: () => { act("settings.open"); close(); } },
                ];
            },
        },

        newSet: {
            title: () => "New set",
            right: () => "",
            rows: (s) => STARTER_NAMES.map((name, i) => ({ label: name, fn: () => {
                const start = (save) => { if (save) act("set.save"); act("set.starter", { i }); done("NEW " + name); };
                if (!s.dirty) return start(false);
                confirm("Unsaved changes", "", null, null, { cur: 0, rows: [
                    { label: "Save, then start new", fn: () => start(true) },
                    { label: "Discard changes", fn: () => start(false) },
                    { label: "Cancel", fn: () => pop() },
                ] });
            } })),
        },

        loadSet: {
            title: () => "Load",
            right: () => "DEL REMOVES",
            rows: (s, lv) => {
                if (!s.sets.length) return none("No saved sets yet");
                return pickSet(s, (id) => {
                    if (host && host.deleteHeld && host.deleteHeld())
                        return confirm("Delete set?", setName(s, id), () => { act("set.delete", { id }); backFrom(lv); say("DELETED"); }, "Delete");
                    const load = (save) => { if (save) act("set.save"); act("set.load", { id }); done("LOADED " + setName(s, id)); };
                    if (!s.dirty) return load(false);
                    confirm("Unsaved changes", "", null, null, { cur: 0, rows: [
                        { label: "Save, then load", fn: () => load(true) },
                        { label: "Load without saving", fn: () => load(false) },
                        { label: "Cancel", fn: () => pop() },
                    ] });
                });
            },
        },

        rename: {
            title: () => "Rename",
            right: () => "",
            rows: (s) => {
                const t = s.focus, tn = s.trackNames[t], cp = s.currentPattern;
                const rows = [
                    { label: "Track T" + (t + 1) + (tn ? " " + tn : ""), fn: () => ask("Track " + (t + 1) + " name", tn || "T" + (t + 1),
                        (name) => { act("track.rename", { t, name }); done("T" + (t + 1) + " " + name); }) },
                ];
                if (cp >= 0 && s.patterns[cp].filled)
                    rows.push({ label: "Pattern P" + (cp + 1) + (s.patterns[cp].name ? " " + s.patterns[cp].name : ""),
                        fn: () => ask("Pattern " + (cp + 1) + " name", s.patterns[cp].name,
                            (name) => { act("pattern.rename", { i: cp, name }); done("P" + (cp + 1) + " " + name); }) });
                if (s.setId != null)
                    rows.push({ label: "Set " + s.setName, fn: () => ask("Rename set", s.setName,
                        (name) => { act("set.rename", { id: s.setId, name }); done("SET " + name); }) });
                return rows;
            },
        },

        confirm: {
            title: (s, lv) => lv.title,
            right: (s, lv) => lv.right || "",
            rows: (s, lv) => lv.rows,
        },
    };

    function view(lv, s) {
        const def = LEVELS[lv.kind];
        const rows = def.rows(s, lv) || [];
        /* A list can shrink under the cursor (a set deleted elsewhere). */
        lv.cur = Math.max(0, Math.min(rows.length - 1, lv.cur | 0));
        if (lv.cur < lv.scroll) lv.scroll = lv.cur;
        if (lv.cur >= lv.scroll + L.ROWS) lv.scroll = lv.cur - L.ROWS + 1;
        lv.scroll = Math.max(0, Math.min(Math.max(0, rows.length - L.ROWS), lv.scroll));
        return { title: def.title(s, lv), right: def.right(s, lv), rows };
    }

    function open() {
        stack = [];
        push({ kind: "root" });
        isOpenFlag = true;
        toast = null;
    }
    function close() {
        while (stack.length) pop();
        isOpenFlag = false;
    }

    function jog(delta) {
        if (!isOpenFlag) return;
        const lv = top();
        const n = view(lv, st()).rows.length;
        lv.cur = Math.max(0, Math.min(n - 1, lv.cur + (delta | 0)));
        view(lv, st());
    }

    function click() {
        if (!isOpenFlag) return;
        const lv = top();
        const row = view(lv, st()).rows[lv.cur];
        if (row && row.fn) row.fn();
    }

    function back() {
        if (!isOpenFlag) return false;
        if (stack.length <= 1) { close(); return false; }
        pop();
        return true;
    }

    function path() {
        const s = st();
        return stack.map((lv) => LEVELS[lv.kind].title(s, lv));
    }

    /* --------------------------------------------------------- drawing */

    const show = (str) => String(str == null ? "" : str).toUpperCase().replace(/[^\x20-\x7e]/g, "?");
    const tw = (ctx, s) => (ctx.textWidth ? ctx.textWidth(s) : s.length * 6);
    function fit(ctx, str, maxW) {
        let s = show(str);
        if (maxW <= 0) return "";
        if (tw(ctx, s) <= maxW) return s;
        while (s.length && tw(ctx, s + "..") > maxW) s = s.slice(0, -1);
        s = s.replace(/\s+$/, "");
        return tw(ctx, s + "..") <= maxW ? s + ".." : "";
    }

    function draw(ctx) {
        if (!isOpenFlag || !ctx) return;
        const s = st();
        const lv = top();
        const v = view(lv, s);

        /* Header: the title wins; the right-hand extra takes what is left and
         * is dropped when too little room remains to say anything useful. */
        const titleText = fit(ctx, v.title, L.W - 1);
        const room = L.W - 1 - tw(ctx, titleText) - 8;
        let rightText = room >= 18 ? fit(ctx, v.right, Math.min(room, 64)) : "";
        if (rightText === "..") rightText = "";
        const rw = rightText ? tw(ctx, rightText) : 0;
        ctx.print(1, L.TITLE_Y, titleText, 1);
        if (rightText) ctx.print(L.W - rw, L.TITLE_Y, rightText, 1);
        ctx.fillRect(0, L.RULE_Y, L.W, 1, 1);

        const scrolls = v.rows.length > L.ROWS;
        const listW = scrolls ? L.W - L.BAR_W - 2 : L.W;
        for (let r = 0; r < L.ROWS; r++) {
            const idx = lv.scroll + r;
            const row = v.rows[idx];
            if (!row) break;
            const y = L.LIST_Y + r * L.ROW_H;
            const sel = idx === lv.cur;
            const fg = sel ? 0 : 1;
            if (sel) {
                ctx.fillRect(0, y, listW, L.ROW_H, 1);
                /* 1px corner notches, the movy idiom for a floating highlight. */
                ctx.fillRect(0, y, 1, 1, 0); ctx.fillRect(listW - 1, y, 1, 1, 0);
                ctx.fillRect(0, y + L.ROW_H - 1, 1, 1, 0); ctx.fillRect(listW - 1, y + L.ROW_H - 1, 1, 1, 0);
            }
            const markW = row.mark ? 6 : 0;
            if (row.mark) ctx.fillRect(listW - 6, y + 3, 3, 3, fg);
            ctx.print(L.TEXT_X, y + 1, fit(ctx, row.label, listW - L.TEXT_X - 2 - markW), fg);
        }

        if (scrolls) {
            const x = L.W - L.BAR_W, top0 = L.LIST_Y, h = L.ROWS * L.ROW_H;
            for (let yy = top0; yy < top0 + h; yy += 2) ctx.fillRect(x + 1, yy, 1, 1, 1);
            const th = Math.max(4, Math.round(h * L.ROWS / v.rows.length));
            const maxScroll = v.rows.length - L.ROWS;
            const ty = top0 + Math.round((h - th) * lv.scroll / maxScroll);
            ctx.fillRect(x, ty, L.BAR_W, th, 1);
        }

        if (toast && clock() < toast.until) {
            const t = fit(ctx, toast.text, L.W - 4);
            /* Keep a dark row between the toast and a highlighted last row so
             * the two bars do not merge into one block. */
            const ty = lv.cur - lv.scroll === L.ROWS - 1 ? L.FOOTER_Y : L.FOOTER_Y - 1;
            ctx.fillRect(0, ty, L.W, L.H - ty, 1);
            ctx.print(Math.max(2, ((L.W - tw(ctx, t)) / 2) | 0), L.FOOTER_Y, t, 0);
        } else {
            toast = null;
            ctx.print(1, L.FOOTER_Y, fit(ctx, HINT, L.W - 1), 1);
        }
    }

    return {
        open, close, isOpen: () => isOpenFlag, jog, click, back, draw, path,
        /* For tests and the host: the current footer message, if still live. */
        toast: () => (toast && clock() < toast.until ? toast.text : null),
    };
}
