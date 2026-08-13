// ⭐ BBC GROUND TRUTH FOR THE HORIZON BLACK LINES.
//
// The port shows horizontal BLACK runs in the sky just above the horizon, and a few on the
// ground.  The host and the Amiga backends show them identically, so they are not a
// display-path bug: the game's own frame buffer genuinely has ZERO bytes there, and a zero
// byte is pen 0, which band 2's palette ($3458) paints BLACK.
//
// That leaves exactly one question a real BBC can answer and nothing else can: does the
// REAL game leave those bytes zero too?  If it does, the port is faithful and the lines are
// the 1985 game's own artefact.  If it does not, the port has a hole in whatever fills the
// sky, and the count of zero bytes per frame localises it.
//
// So this counts zero bytes per frame in the VISIBLE SKY WINDOW and reports the
// distribution, rather than dumping one frame and hoping it is representative — the
// sky/track split moves with the hills every frame (MoveHorizon, $4F44), so a single frame
// proves nothing either way.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_sky.mjs [seconds]
//
// ⚠ THE WINDOW IS LINES 80-99, NOT THE WHOLE SKY.  Display lines 24-79 are the engine's own
// code and variables ($5E40-$66FF) sitting inside the frame buffer, which are full of zero
// bytes and are invisible because band 1 maps all sixteen palette entries to the same blue
// (src/platform/bbc_screen.h).  Counting zeros there would drown the signal in the program.
//
// ⚠⚠ THE FRONT END IS NOT DRIVEN BY KEYS HERE, and it cannot be: bbc_drive.mjs does not
// reach a race on this disc and never has.  Measured just now, 60 BBC seconds: menu polls
// 1144, rdch 133, FUN_32D0 **0**, MAIN LOOP **0** — its own bisect verdict is "console_io
// NEVER RETURNED: no RETURN reached the MOS", with the two-character buffer sitting full.
// So every key-driven BBC reference run to date has been measuring the front end.
//
// The way past it is the port's own STRAIGHT_TO_RACE shortcut (src/platform/autorun.cpp),
// which works here because jsbeeb — unlike the FS-UAE gdb stub — genuinely lets us WRITE the
// emulated machine.  A practice session needs exactly ONE menu answer: at $63F7 the engine
// asks `1 PRACTICE 2 COMPETITION`; option 1 stores $5F3B = $FF (at $6401) and enters the
// session at $6407.  So when PC reaches $63F7 we do precisely that and jump to $6407.
// It skips NO game code — class, qualifying duration, driver names and ANOTHER-START are all
// on the COMPETITION branch and are genuinely never reached on this path.
//
// ⚠ That is a poke, so it is exactly the kind of stimulus that can silently not happen.
// It is verified two ways below: `bypassed` must become 1, and `loopHits` must become
// non-zero — a run that reports NO SAMPLES says so loudly instead of reporting a sky.
//
// SPACE is still pressed by RAW MATRIX POSITION for the "SPACE BAR TO CONTINUE" page, which
// demonstrably works (it is how the track menu is reached).  See the long note at the top of
// bbc_drive.mjs: utils.keyCodes goes through a browser layout map a headless TestMachine has
// not set up, so keyDown() is a silent no-op — keyDownRaw() only.

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const KEY = {};
for (const k of ["RETURN", "DELETE", "SPACE", "K0", "K1", "K5", "T", "S", "Q"]) {
    if (!Array.isArray(utils.BBC[k])) throw new Error(`jsbeeb utils.BBC has no ${k}`);
    KEY[k] = utils.BBC[k];
}

const bbcSeconds = Number(process.argv[2] || 90);

const MENU_POLL = 0x6581;
const RDCH_SITE = 0x6316;
const MAINLOOP = 0x1701;

// The screen, exactly as src/platform/bbc_screen.h derives it.
const BASE = 0x5a80, CELLS = 40, LINES = 8, BPR = CELLS * LINES;
const SKY_LO = 80, SKY_HI = 100;      // visible sky under band 2: lines 80..99
const GND_LO = 100, GND_HI = 166;     // band 3, the track

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

const PRACTICE_MENU = 0x63f7;   // the `1 PRACTICE 2 COMPETITION` prompt
const PRACTICE_ENTER = 0x6407;  // where option 1 goes after storing $5F3B
const PRACTICE_FLAG = 0x5f3b;

let menuHits = 0, rdchHits = 0, loopHits = 0, bypassed = 0;
const samples = [];

function scan(lo, hi) {
    // -> [zeroBytes, totalBytes, longest run of consecutive zero cells on any one line]
    let zeros = 0, total = 0, longest = 0;
    for (let y = lo; y < hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        let run = 0;
        for (let c = 0; c < CELLS; c++) {
            const b = tm.processor.readmem(BASE + row * BPR + c * LINES + line);
            total++;
            if (b === 0) { zeros++; run++; if (run > longest) longest = run; }
            else run = 0;
        }
    }
    return [zeros, total, longest];
}

tm.processor.debugInstruction.add((addr) => {
    if (addr === PRACTICE_MENU && !bypassed) {
        // Answer the one menu a practice session needs, the way option 1 does.
        tm.processor.writemem(PRACTICE_FLAG, 0xff);
        tm.processor.pc = PRACTICE_ENTER;
        bypassed++;
        return false;
    }
    if (addr === MENU_POLL) menuHits++;
    else if (addr === RDCH_SITE) rdchHits++;
    else if (addr === MAINLOOP) {
        loopHits++;
        // Sample every 4th frame once the race is genuinely running (the first frames are
        // still the mode switch and the pit exit).
        if (loopHits > 12 && loopHits % 4 === 0 && samples.length < 60) {
            const sky = scan(SKY_LO, SKY_HI);
            const gnd = scan(GND_LO, GND_HI);
            samples.push({ frame: loopHits, sky, gnd });
        }
    }
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

const CYCLES_PER_SEC = 2 * 1000 * 1000;
const deadline = bbcSeconds * CYCLES_PER_SEC;
let elapsed = 0, round = 0;

while (elapsed < deadline && samples.length < 60) {
    const m0 = menuHits, r0 = rdchHits;
    await tm.runFor(500000);
    elapsed += 500000;
    const menuActive = menuHits > m0, rdchActive = rdchHits > r0;

    if (rdchActive) {
        await pulse(KEY.DELETE, 80000, 120000);
        await pulse(KEY.DELETE, 80000, 120000);
        elapsed += 2 * 200000;
        await pulse(KEY.K1); await pulse(KEY.K0); await pulse(KEY.RETURN);
        elapsed += 3 * 320000;
    } else if (menuActive) {
        await pulse(KEY.K1); await pulse(KEY.SPACE);
        elapsed += 2 * 320000;
    } else {
        await pulse(KEY.SPACE);
        elapsed += 320000;
        if (loopHits > 0) {
            // Starter, then first gear, then hold the throttle — a MOVING car, so the road
            // geometry (and therefore the sky/track split) actually changes between samples.
            for (const k of [KEY.T, KEY.Q, KEY.S])
                await pulse(k, 300000, 300000);
            elapsed += 3 * 600000;
        }
    }
    if (++round % 8 === 0)
        console.log(`  t=${(elapsed / CYCLES_PER_SEC).toFixed(1)}s  bypassed=${bypassed} ` +
            `menu=${menuHits} rdch=${rdchHits} mainloop=${loopHits} samples=${samples.length}`);
}

// --- report ----------------------------------------------------------------
console.log(`\nbypassed=${bypassed}  main-loop frames=${loopHits}  samples=${samples.length}\n`);
if (!samples.length) {
    console.log("NO SAMPLES — the race was never entered, so this run says NOTHING about the sky.");
    console.log(bypassed === 0
        ? "  => PC never reached $63F7: the run did not get as far as the practice menu."
        : "  => the $63F7 bypass DID fire but the main loop never ran: the poke or the jump did not take.");
    process.exit(1);
}
console.log("  frame   sky zeros/total  longest-run    ground zeros/total  longest-run");
for (const s of samples.slice(0, 24))
    console.log(`  ${String(s.frame).padStart(5)}   ${String(s.sky[0]).padStart(5)}/${s.sky[1]}` +
        `        ${String(s.sky[2]).padStart(3)}          ` +
        `${String(s.gnd[0]).padStart(5)}/${s.gnd[1]}        ${String(s.gnd[2]).padStart(3)}`);

const skyZ = samples.map((s) => s.sky[0]), skyR = samples.map((s) => s.sky[2]);
const sum = (a) => a.reduce((x, y) => x + y, 0);
console.log(`\nSKY  (lines ${SKY_LO}-${SKY_HI - 1}): zero bytes per frame ` +
    `min=${Math.min(...skyZ)} max=${Math.max(...skyZ)} mean=${(sum(skyZ) / skyZ.length).toFixed(1)}` +
    ` of ${samples[0].sky[1]};  longest zero run max=${Math.max(...skyR)} cells`);
console.log(`  => ${Math.max(...skyZ) === 0
    ? "THE REAL BBC NEVER LEAVES A SKY BYTE ZERO.  The port's black horizon lines are a PORT BUG."
    : "the real BBC DOES leave sky bytes zero — compare the counts against the port before blaming the port."}`);
