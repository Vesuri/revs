// ⛔ SUPERSEDED (2026-08-14) — USE tools/bbc_refloop_race.mjs, which does reach a race.
//
// The instinct below is right (watch what the engine asks for) but it watches the wrong thing.
// The front end prints through ONE routine, print_message ($4D7E), indexed by X with its string
// table at $3AD0/$3B50 — hooking that and decoding the string gives a readable transcript of
// the whole dialogue, so there was never a need to infer the prompt from which poll fired.
//
// ⚠ Its bisect verdict "console_io NEVER RETURNED: no RETURN reached the MOS.  Fix key
// injection." is FALSE, and it stood for two days.  tools/bbc_probe_return.mjs proves RETURN
// arrives on all three injection paths at the BASIC prompt.  What actually blocked a race was
// the unseen wing-settings prompt ($3C50) plus jsbeeb's `FakeVideo` never raising vertical sync,
// which hangs the engine at $4E11 (`BIT $FE4D`) forever.
//
// ── original header ───────────────────────────────────────────────────────────────────────
// Drive the real BBC into a race, by WATCHING what the engine is asking for.
//
// Three blind scripted probes failed here in a row, each one reporting its own stall as
// evidence about something else.  The engine's front end alternates two completely
// different input modes and there is no way to tell them apart from outside — it draws in
// MODE 4/5, so drainText() sees nothing:
//
//   menu_wait_key ($6571)  polls menu_key_tbl via kbd_test_key at $6581 — wants a NUMBER
//                          key held to select, then SPACE held to confirm.
//   console_io    ($6300)  a blocking OSRDCH line editor at $6316; its caller FUN_3EE0
//                          asks for TWO DIGITS and re-prompts until FUN_32D0 accepts.
//
// So: watch $6581 and $6316, and answer whichever one has fired recently.  The feedback is
// the point — a fixed key sequence cannot work when the order of prompts is unknown.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_drive.mjs [seconds-of-BBC]

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

// ⚠⚠ PRESS KEYS BY MATRIX POSITION, NOT BY BROWSER KEY CODE.  This cost five probe runs.
//
// jsbeeb has two key tables in utils.js and two ways to press a key:
//   utils.BBC.<name>       -> [col, row] on the BBC keyboard matrix; sysvia.keyDownRaw()
//   utils.keyCodes.<name>  -> a BROWSER key code; sysvia.keyDown(), which resolves it
//                             through whichever PC->BBC layout happens to be installed
// Two independent traps live in the second path.  `utils.keyCodes.RETURN` does not exist at
// all (that table calls it `ENTER`), so pressing it was `keyDown(undefined)` — a silent
// no-op.  And even `keyCodes.ENTER` did not reach the MOS here, because the layout map is a
// browser-side concern that a headless TestMachine has no reason to have set up.
//
// The symptom in both cases was identical and deeply misleading: the engine sat in its
// OSRDCH line editor forever, and three probes in a row reported that as evidence about the
// $7Bxx page.  What settled it was counting FUN_32D0 ($32D0) and FUN_3EE0's reject arm
// ($3EEE) separately: BOTH zero means console_io never returned, i.e. no RETURN arrived —
// as opposed to "returned and was rejected", which needs the opposite fix.
//
// So: raw matrix positions only.  Assert the names exist rather than trusting them, because
// an undefined entry here fails silently rather than loudly.
const KEY = {};
for (const k of ["RETURN", "DELETE", "SPACE", "K0", "K1", "K5", "T", "S", "Q"]) {
    if (!Array.isArray(utils.BBC[k])) throw new Error(`jsbeeb utils.BBC has no ${k}`);
    KEY[k] = utils.BBC[k];
}

const bbcSeconds = Number(process.argv[2] || 60);

const MENU_POLL = 0x6581;   // kbd_test_key call inside menu_wait_key
const RDCH_SITE = 0x6316;   // the OSRDCH inside console_io
const MAINLOOP = 0x1701;
const VALIDATE = 0x32d0;    // FUN_32D0 — runs only once console_io RETURNS a completed line
const REPROMPT = 0x3eee;    // FUN_3EE0's delete-and-ask-again arm — runs only on a REJECT
const UNMAPPED = [0x16e6, 0x1704, 0x1739, 0x1748, 0x502a, 0x503b, 0x6612];

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

let menuHits = 0, rdchHits = 0, loopHits = 0, valHits = 0, rejHits = 0;
// ⭐ THE BISECT.  "Stuck in the line editor" has two completely different causes and they
// need opposite fixes.  If FUN_32D0 never runs, console_io never returned — the RETURN is
// not arriving, and the problem is key injection.  If it runs and $3EEE runs with it, the
// line WAS entered and FUN_32D0 rejected it — the problem is the value.  Counting both
// costs nothing and settles it in one run instead of one run per guess.
let firstUnmapped = 0, snapshot = null;
const seen = new Set();
tm.processor.debugInstruction.add((addr) => {
    if (addr === MENU_POLL) menuHits++;
    else if (addr === RDCH_SITE) rdchHits++;
    else if (addr === MAINLOOP) loopHits++;
    else if (addr === VALIDATE) valHits++;
    else if (addr === REPROMPT) rejHits++;
    else if (!firstUnmapped && UNMAPPED.includes(addr)) {
        firstUnmapped = addr;
        snapshot = [...Array(0x100).keys()].map((i) => tm.processor.readmem(0x7b00 + i));
    }
    if (addr >= 0x1200 && addr < 0x7000) seen.add(addr);
    return false;
});

async function pulse(colrow, hold = 120000, gap = 200000) {
    tm.processor.sysvia.keyDownRaw(colrow);
    await tm.runFor(hold);
    tm.processor.sysvia.keyUpRaw(colrow);
    await tm.runFor(gap);
}

// --- boot to the track menu ------------------------------------------------
await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();
let ok = false;
for (let i = 0; i < 40; i++) {
    await pulse(KEY.SPACE, 40000, 3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) { ok = true; break; }
}
if (!ok) throw new Error("never reached REVSMEN track menu");
await pulse(KEY.K5, 40000, 3 * 1000 * 1000);   // Silverstone
console.log("in REVS2; driving the front end by feedback\n");

// --- the feedback loop -----------------------------------------------------
const CYCLES_PER_SEC = 2 * 1000 * 1000;
const deadline = bbcSeconds * CYCLES_PER_SEC;
let elapsed = 0, round = 0;

while (elapsed < deadline && !firstUnmapped) {
    const m0 = menuHits, r0 = rdchHits;
    await tm.runFor(500000);            // 0.25 s: let it settle and show us what it wants
    elapsed += 500000;
    const menuActive = menuHits > m0, rdchActive = rdchHits > r0;

    if (rdchActive) {
        // The line editor: clear whatever is in the two-character buffer first (DELETE is
        // $7F at $6329, which decrements Y), then two digits and RETURN.  "10" is accepted
        // by any of the numeric prompts the front end asks.
        await pulse(KEY.DELETE, 80000, 120000);
        await pulse(KEY.DELETE, 80000, 120000);
        elapsed += 2 * 200000;
        await pulse(KEY.K1);
        await pulse(KEY.K0);
        await pulse(KEY.RETURN);
        elapsed += 3 * 320000;
    } else if (menuActive) {
        // A menu: select option 1, then confirm with SPACE.
        await pulse(KEY.K1);
        await pulse(KEY.SPACE);
        elapsed += 2 * 320000;
    } else {
        // Neither: either a "press SPACE to continue" text page ($34D2 waits for SPACE to be
        // released and then pressed) or the race itself.
        //
        // ⚠ DO NOT press the driving keys speculatively here.  Measured: 'T' and 'S' ended
        // up in the line editor's buffer ($74/$75 read $54,$53), and that buffer is TWO
        // characters — once full, $6334 answers every further key with a bell instead of
        // storing it, so the prompt can never be completed and the run is wedged by its own
        // input.  Only nudge with keys the editor treats as harmless, and only send driving
        // keys once the main loop has actually been entered.
        await pulse(KEY.SPACE);
        elapsed += 320000;
        if (loopHits > 0) {
            for (const k of [KEY.T, KEY.S])
                await pulse(k, 300000, 300000);
            elapsed += 2 * 600000;
        }
    }
    if (++round % 8 === 0)
        console.log(`  t=${(elapsed / CYCLES_PER_SEC).toFixed(1)}s  menu=${menuHits} ` +
            `rdch=${rdchHits} validate=${valHits} reject=${rejHits} ` +
            `mainloop=${loopHits} seen=${seen.size} buf=${tm.processor.readmem(0x74).toString(16)},${tm.processor.readmem(0x75).toString(16)}`);
}

// --- report ----------------------------------------------------------------
const hex = (v, n = 2) => v.toString(16).padStart(n, "0");
console.log(`\nmenu polls=${menuHits}  rdch=${rdchHits}  FUN_32D0=${valHits}  reject-arm=${rejHits}  MAIN LOOP=${loopHits}`);
console.log(valHits === 0
    ? "  => console_io NEVER RETURNED: no RETURN reached the MOS.  Fix key injection."
    : "  => the line WAS entered and validated; a reject means FUN_32D0 disliked the value.");
console.log(`distinct engine addresses executed: ${seen.size}`);
console.log(`first unmapped-page call: ${firstUnmapped ? "$" + hex(firstUnmapped, 4) : "NONE"}`);

if (snapshot) {
    console.log("\n$7B00-$7BFF at that instant:");
    for (let r = 0; r < 16; r++)
        console.log(`  $${hex(0x7b00 + r * 16, 4)}: ` +
            snapshot.slice(r * 16, r * 16 + 16).map((b) => hex(b)).join(" ") + "  |" +
            snapshot.slice(r * 16, r * 16 + 16)
                .map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : ".")).join("") + "|");
    console.log(`  nonzero: ${snapshot.filter((b) => b).length}/256`);
}
