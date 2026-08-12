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

const bbcSeconds = Number(process.argv[2] || 60);

const MENU_POLL = 0x6581;   // kbd_test_key call inside menu_wait_key
const RDCH_SITE = 0x6316;   // the OSRDCH inside console_io
const MAINLOOP = 0x1701;
const UNMAPPED = [0x16e6, 0x1704, 0x1739, 0x1748, 0x502a, 0x503b, 0x6612];

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

let menuHits = 0, rdchHits = 0, loopHits = 0;
let firstUnmapped = 0, snapshot = null;
const seen = new Set();
tm.processor.debugInstruction.add((addr) => {
    if (addr === MENU_POLL) menuHits++;
    else if (addr === RDCH_SITE) rdchHits++;
    else if (addr === MAINLOOP) loopHits++;
    else if (!firstUnmapped && UNMAPPED.includes(addr)) {
        firstUnmapped = addr;
        snapshot = [...Array(0x100).keys()].map((i) => tm.processor.readmem(0x7b00 + i));
    }
    if (addr >= 0x1200 && addr < 0x7000) seen.add(addr);
    return false;
});

async function pulse(code, hold = 120000, gap = 200000) {
    tm.processor.sysvia.keyDown(code);
    await tm.runFor(hold);
    tm.processor.sysvia.keyUp(code);
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
    await pulse(utils.keyCodes.SPACE, 40000, 3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) { ok = true; break; }
}
if (!ok) throw new Error("never reached REVSMEN track menu");
await pulse(utils.keyCodes.K5, 40000, 3 * 1000 * 1000);   // Silverstone
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
        // The line editor: two digits then RETURN.  "10" is accepted by any of the
        // numeric prompts the front end asks (laps, grid position, skill).
        await pulse(utils.keyCodes.K1);
        await pulse(utils.keyCodes.K0);
        await pulse(utils.keyCodes.RETURN);
        elapsed += 3 * 320000;
    } else if (menuActive) {
        // A menu: select option 1, then confirm with SPACE.
        await pulse(utils.keyCodes.K1);
        await pulse(utils.keyCodes.SPACE);
        elapsed += 2 * 320000;
    } else {
        // Neither: either a "press SPACE to continue" text page ($34D2 waits for SPACE to
        // be released and then pressed) or the race itself.  Nudge with SPACE, then try
        // the driving keys.
        await pulse(utils.keyCodes.SPACE);
        if (round % 3 === 2) {
            for (const k of [utils.keyCodes.T, utils.keyCodes.S])
                await pulse(k, 300000, 300000);
            elapsed += 2 * 600000;
        }
        elapsed += 320000;
    }
    if (++round % 8 === 0)
        console.log(`  t=${(elapsed / CYCLES_PER_SEC).toFixed(1)}s  menu=${menuHits} ` +
            `rdch=${rdchHits} mainloop=${loopHits} engine-addrs-seen=${seen.size}`);
}

// --- report ----------------------------------------------------------------
const hex = (v, n = 2) => v.toString(16).padStart(n, "0");
console.log(`\nmenu polls=${menuHits}  rdch=${rdchHits}  MAIN LOOP entries=${loopHits}`);
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
