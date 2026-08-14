// ⭐ GROUND TRUTH FOR THE MODE 7 FRONT END — what a real BBC's teletext screen actually holds,
// and the exact OSWRCH byte stream the port's VDU driver has to interpret to produce it.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_mode7.mjs [--frames=N]
//        [--dump=DIR]
//
// ── Why this probe exists ─────────────────────────────────────────────────────────────────
//
// The port renders nothing out of a race, and `docs/phases.md` item 3 called that a font
// problem.  It is not: `vdu_char_def` ($5092) branches on `$64` bit 7 and the MODE 7 arm does
// `JSR $FFEE` — OSWRCH — so in the front end EVERY character leaves the engine as a VDU byte
// and the MOS maintains the screen.  A few sites also poke teletext codes straight into
// $7C00-$7FFF (docs/static-map.md §Open items 6).  So the port needs two things this probe
// pins down against real hardware, in the only order that can be checked:
//
//   1. the OSWRCH byte stream  -> what Platform::wrch() must implement, and
//   2. the resulting screen RAM -> the byte-for-byte target the driver must reproduce.
//
// ⚠ $7C00-$7FFF IS TIME-MULTIPLEXED with the dashboard code overlay (copy_dash_data, $18EA),
// so a dump is only teletext while the machine is in MODE 7.  This probe records `$64` and the
// Video ULA control byte alongside every snapshot rather than assuming.
//
// Provenance note for the renderer: the SAA5050 glyph shapes are specified normatively in the
// published World System Teletext standard, and the G1 mosaics are 2x3 block geometry — this
// probe deliberately captures CODES, never glyph bitmaps, so nothing here is lifted.

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";
import path from "path";

// ── addresses (runtime image; see disasm/symbols.csv) ─────────────────────────────────────
const OSWRCH = 0xffee; // the MOS entry itself: `JMP (WRCHV)`, so PC really does land here
const PRINT_MSG = 0x4d7e; // text_script_interp, X = message index
const MENU_WAIT = 0x6571; // menu_wait_key, X = option count
const MENU_POLL = 0x6581; //   its kbd_test_key call — proof a menu is actually polling
const CHAR_DEF = 0x5092; // vdu_char_def — the $64 branch
const CHAR_M7 = 0x50f6; //   ...its MODE 7 arm (JSR OSWRCH)
const CHAR_BMP = 0x5096; //   ...its bitmap arm (OSWORD 10 + plot)
const MODE_FLAG = 0x64; // 0 = bitmap race view, $80 = MODE 7
const SCREEN = 0x7c00; // MODE 7 screen RAM (1 KB, 25 rows of 40)
const ULA_CTRL = 0xfe20;

const CPS = 2 * 1000 * 1000;

const argv = process.argv.slice(2);
const opt = (name, dflt) => {
    const hit = argv.find((a) => a.startsWith(`--${name}=`));
    return hit === undefined ? dflt : hit.slice(name.length + 3);
};
const megaCycles = Number(opt("mcycles", 12));
const dumpDir = opt("dump", null);

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

const cpu = tm.processor;
const readMem = (a) => cpu.readmem(a);
const hex = (b) => b.toString(16).padStart(2, "0");

// ── the OSWRCH stream ─────────────────────────────────────────────────────────────────────
// Recorded as (byte, caller) pairs.  The caller matters: it separates the front end's text
// from the line editor's echo, and it is how a byte with no visible effect gets attributed.
const wrchStream = [];
const wrchCounts = new Map();
let charDefM7 = 0;
let charDefBmp = 0;
const engineWrch = [];
const engineCounts = new Map();
let menuPolling = false; // menu_wait_key's kbd_test_key call ($6581) has run since we last answered

// ── screen snapshots ──────────────────────────────────────────────────────────────────────
const snapshots = [];
const snapKeys = new Set();
function snapshot(label) {
    if (snapKeys.has(label)) return; // one per distinct moment, not one per poll
    snapKeys.add(label);
    const bytes = new Uint8Array(1024);
    for (let i = 0; i < 1024; i++) bytes[i] = readMem(SCREEN + i);
    snapshots.push({
        label,
        mode: readMem(MODE_FLAG),
        ula: cpu.video ? -1 : -1,
        wrchAt: wrchStream.length,
        eventAt: events.length,
        bytes,
    });
}

// ⭐ WHO WRITES THE TELETEXT SCREEN.  The whole design of the port's MODE 7 path turns on this:
// if the engine pokes $7C00-$7FFF directly then screen RAM is the source of truth and the port
// needs a RENDERER; if it goes through OSWRCH then the port needs a VDU DRIVER as well.  Both
// answers are possible a priori (docs/static-map.md names three direct-store sites), so count
// them by PC instead of assuming.  MOS PCs (>= $8000) are the OS's own VDU driver.
const screenWriters = new Map();
// ⭐ THE ORDERED EVENT LOG — the fixture the port is validated against.
// A snapshot alone cannot validate the VDU driver, because the page is built by TWO writers and
// a pure VDU replay would differ from the real screen in exactly the cells the game poked.  So
// record both, in order: replaying this log must reproduce the dump BYTE FOR BYTE, which is a
// test that can actually fail.  MOS writes ($8000+) are deliberately NOT logged — reproducing
// them is the driver's job and logging them would make the test vacuous.
const events = [];
cpu.debugWrite.add((addr, val) => {
    if (addr < SCREEN || addr > SCREEN + 0x3ff) return;
    const pc = cpu.pc;
    screenWriters.set(pc, (screenWriters.get(pc) || 0) + 1);
    if (pc < 0x8000) events.push(`P ${addr - SCREEN} ${val}`);
});

cpu.debugInstruction.add((addr) => {
    if (addr === OSWRCH) {
        const b = cpu.a;
        wrchStream.push(b);
        wrchCounts.set(b, (wrchCounts.get(b) || 0) + 1);
    } else if (addr === CHAR_M7) {
        // ⭐ THE SPEC FOR Platform::wrch().  $50F6 is the ONLY place the engine itself emits a
        // VDU byte, so this stream — not the whole-machine one, which is mostly BASIC's — is
        // what the port's VDU driver has to handle.  A is the byte about to go to OSWRCH.
        charDefM7++;
        engineWrch.push(cpu.a);
        engineCounts.set(cpu.a, (engineCounts.get(cpu.a) || 0) + 1);
        events.push(`V ${cpu.a}`);
    }
    else if (addr === CHAR_BMP) charDefBmp++;
    else if (addr === MENU_POLL) menuPolling = true;
    return false;
});

// ── boot, exactly as bbc_refloop_race.mjs does it, as far as REVS2's own front end ────────
async function hold(colrow, cycles) {
    cpu.sysvia.keyDownRaw(colrow);
    await tm.runFor(cycles);
    cpu.sysvia.keyUpRaw(colrow);
}
const track = Number(opt("track", 5)); // 5 = Silverstone

snapshot("boot (before any engine code)");
await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();
snapshot("REVINST loaded (BASIC instructions, MODE 7)");

let atMenu = false;
for (let i = 0; i < 40 && !atMenu; i++) {
    await hold(utils.BBC.SPACE, 40000);
    await tm.runFor(3 * CPS);
    const t = tm.drainText();
    if (/PRESS.*BRANDS HATCH/i.test(t)) atMenu = true;
    snapshot(`REVINST page ${i}`);
}
if (!atMenu) throw new Error("never reached the REVSMEN track menu");
snapshot("REVSMEN track menu (5TRSCRN title screen)");

const TRACKKEY = [utils.BBC.K1, utils.BBC.K2, utils.BBC.K3, utils.BBC.K4, utils.BBC.K5][track - 1];
await hold(TRACKKEY, 40000);
await tm.runFor(4 * CPS);
snapshot("REVS2 just loaded");

// ── drive REVS2's OWN front end and snapshot every distinct teletext screen ────────────────
// The menu primitive is menu_wait_key ($6571): with X = option count it polls key_binding_tbl
// ($39E0) — entry 0 is CONFIRM, 1..X are the options — and records the choice in $78 once $77
// goes non-zero.  So press the key the engine is ACTUALLY polling, read out of live memory,
// exactly as bbc_refloop_race.mjs derives it.  A BBC negative-INKEY byte b is internal key
// number 255-b, and the internal key number is (row<<4)|col, which is jsbeeb's keyDownRaw
// matrix.
const KEY_TBL = 0x39e0, SEL_FLAG = 0x77, SEL_IDX = 0x78;
const colrowOf = (negInkey) => {
    const kn = 255 - negInkey;
    return [kn & 0x0f, kn >> 4]; // [col, row]
};
async function holdUntil(colrow, test, budget = 3 * CPS) {
    if (test()) return true;
    cpu.sysvia.keyDownRaw(colrow);
    let spent = 0;
    while (spent < budget && !test()) {
        await tm.runFor(20000);
        spent += 20000;
    }
    cpu.sysvia.keyUpRaw(colrow);
    return test();
}

// Snapshot on CHANGE, not on a timer: the front end is a sequence of static pages, and a timer
// either misses one or captures the same page twenty times.
let lastHash = "";
const hashScreen = () => {
    let h = "";
    for (let i = 0; i < 1024; i += 7) h += hex(readMem(SCREEN + i));
    return h;
};
const armTrace = [];
let lastArm = null;
for (let step = 0; step < 60; step++) {
    await tm.runFor(0.2 * CPS);

    const arm = `\$64=$${hex(readMem(MODE_FLAG))} m7=${charDefM7} bmp=${charDefBmp} wrch=${wrchStream.length}`;
    if (arm !== lastArm) {
        armTrace.push(`  step ${String(step).padStart(2)}  ${arm}`);
        lastArm = arm;
    }
    const h = hashScreen();
    if (h !== lastHash) {
        lastHash = h;
        snapshot(`front-end page, step ${step}`);
    }

    // If a menu is polling, answer option 1 then confirm.  If not, it is a
    // "PRESS SPACE BAR TO CONTINUE" page.
    if (readMem(SEL_FLAG) === 0 && menuPolling) {
        await holdUntil(colrowOf(readMem(KEY_TBL + 1)), () => readMem(SEL_FLAG) !== 0);
        await holdUntil(colrowOf(readMem(KEY_TBL)), () => !menuPolling);
        menuPolling = false;
    } else {
        await hold(utils.BBC.SPACE, 60000);
    }
}
snapshot("end of the front-end drive");
console.log("\n=== how the engine's front end draws, over time:");
for (const l of armTrace) console.log(l);

// ── report ────────────────────────────────────────────────────────────────────────────────

console.log(`\n=== vdu_char_def arms:  MODE 7 (OSWRCH) ${charDefM7}   bitmap (OSWORD 10) ${charDefBmp}`);
console.log(`=== OSWRCH bytes: ${wrchStream.length} total, ${wrchCounts.size} distinct\n`);

const ctrl = [...wrchCounts.keys()].filter((b) => b < 0x20).sort((a, b) => a - b);
const print = [...wrchCounts.keys()].filter((b) => b >= 0x20 && b < 0x80).sort((a, b) => a - b);
const high = [...wrchCounts.keys()].filter((b) => b >= 0x80).sort((a, b) => a - b);
console.log(`VDU control codes (${ctrl.length}):`);
for (const b of ctrl) console.log(`   VDU ${String(b).padStart(3)}  $${hex(b)}  x${wrchCounts.get(b)}`);
console.log(`printable (${print.length}): ${print.map((b) => String.fromCharCode(b)).join("")}`);
console.log(`>= $80 (${high.length}): ${high.map(hex).join(" ")}`);

console.log(`\n=== ⭐ THE ENGINE'S OWN VDU STREAM (via $50F6): ${engineWrch.length} bytes, ${engineCounts.size} distinct`);
{
    const ks = [...engineCounts.keys()].sort((a, b) => a - b);
    const c = ks.filter((b) => b < 0x20), pr = ks.filter((b) => b >= 0x20 && b < 0x80), hi = ks.filter((b) => b >= 0x80);
    console.log(`   control: ${c.map((b) => `VDU ${b}(x${engineCounts.get(b)})`).join(" ") || "(none)"}`);
    console.log(`   printable: ${pr.map((b) => String.fromCharCode(b)).join("")}`);
    console.log(`   >= $80: ${hi.map((b) => `$${hex(b)}(x${engineCounts.get(b)})`).join(" ") || "(none)"}`);
    let l = "   ";
    for (const b of engineWrch.slice(0, 160)) {
        l += b >= 0x20 && b < 0x7f ? `'${String.fromCharCode(b)}' ` : `$${hex(b)} `;
        if (l.length > 92) { console.log(l); l = "   "; }
    }
    if (l.trim()) console.log(l);
}

// ⭐ the decisive table
console.log(`\n=== who wrote $7C00-$7FFF (by PC):`);
const writers = [...screenWriters.entries()].sort((a, b) => b[1] - a[1]);
let mos = 0, game = 0;
for (const [pc, n] of writers) (pc >= 0x8000 ? (mos += n) : (game += n));
console.log(`   MOS VDU driver (PC >= $8000): ${mos} writes`);
console.log(`   GAME code      (PC <  $8000): ${game} writes`);
for (const [pc, n] of writers.slice(0, 15))
    console.log(`      $${pc.toString(16).padStart(4, "0")}  x${n}${pc >= 0x8000 ? "  (MOS)" : ""}`);

// The first 200 bytes verbatim: VDU 22,7 and friends are only legible in sequence.
console.log(`\nfirst 200 bytes of the stream:`);
let line = "  ";
for (let i = 0; i < Math.min(200, wrchStream.length); i++) {
    const b = wrchStream[i];
    line += b >= 0x20 && b < 0x7f ? `'${String.fromCharCode(b)}' ` : `$${hex(b)} `;
    if (line.length > 92) {
        console.log(line);
        line = "  ";
    }
}
if (line.trim()) console.log(line);

// ── the screen snapshots, decoded the way the SAA5050 would ───────────────────────────────
// Alpha vs mosaic is per-row state, so a raw hexdump is unreadable and a raw ASCII dump lies
// (a byte is a letter or a 2x3 block depending on what came before it on that row).
function render(bytes) {
    const out = [];
    for (let r = 0; r < 25; r++) {
        let gfx = false;
        let s = "";
        for (let c = 0; c < 40; c++) {
            const b = bytes[r * 40 + c] & 0x7f;
            if (b < 0x20) {
                if (b >= 0x10 && b <= 0x17) gfx = true;
                else if (b <= 0x07) gfx = false;
                s += "·"; // a control code occupies a cell and displays as space
            } else if (gfx && (b < 0x40 || b >= 0x60)) s += "#";
            else s += String.fromCharCode(b);
        }
        out.push(s);
    }
    return out;
}

for (const s of snapshots) {
    const nonSpace = [...s.bytes].filter((b) => b !== 0x20 && b !== 0x00).length;
    console.log(
        `\n=== ${s.label}   \$64=$${hex(s.mode)} (${s.mode & 0x80 ? "MODE 7" : "bitmap"})   ` +
            `${nonSpace} non-blank cells`,
    );
    if (nonSpace === 0) {
        console.log("   (blank)");
        continue;
    }
    for (const [i, row] of render(s.bytes).entries()) {
        if (row.trim() && row.trim() !== "·".repeat(row.trim().length)) {
            console.log(`   ${String(i).padStart(2)}|${row}|`);
        }
    }
}

if (dumpDir) {
    fs.mkdirSync(dumpDir, { recursive: true });
    for (const [i, s] of snapshots.entries()) {
        const name = `mode7_${String(i).padStart(2, "0")}.bin`;
        fs.writeFileSync(path.join(dumpDir, name), s.bytes);
    }
    fs.writeFileSync(
        path.join(dumpDir, "wrch_stream.bin"),
        new Uint8Array(wrchStream),
    );
    // ⭐ the one the port is specified by: only the bytes REVS2 itself emitted
    fs.writeFileSync(path.join(dumpDir, "engine_wrch.bin"), new Uint8Array(engineWrch));
    // ⭐⭐ THE FIXTURE.  "V b" = the engine sent VDU byte b; "P off val" = the game poked screen
    // RAM directly; "S n file" = at this point the real screen looked like file.
    const lines = events.slice();
    for (const [i, sn] of snapshots.entries())
        lines.splice(sn.eventAt + i, 0, `S ${i} mode7_${String(i).padStart(2, "0")}.bin`);
    fs.writeFileSync(path.join(dumpDir, "mode7_events.txt"), lines.join("\n") + "\n");
    console.log(`   fixture: ${events.length} events + ${snapshots.length} snapshot markers`);
    console.log(`\nwrote ${snapshots.length} screen dumps + wrch_stream.bin to ${dumpDir}`);
}
