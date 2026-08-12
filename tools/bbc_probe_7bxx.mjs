// What is at $7B00-$7BFF on a real BBC when the engine calls into it?
//
// Phase 4 upgraded the question.  bbc_probe_unmapped_calls.mjs measured ZERO executions of
// all seven sites and concluded "consistent with unreachable, but the run never got past
// the $6560 gate".  Generating and RUNNING the corpus settled the reachability half:
// $1704 ($7B4A), $1739 ($7B00) and $1748 ($7BE2) are in the engine's MAIN LOOP
// ($1701-$1763) — one call each, EVERY FRAME — and $16E6 is four instructions into the
// routine that loop lives in.  So they are not dead; the earlier probe simply never
// reached the loop.
//
// This probe therefore asks the only question left: WHAT IS THERE.  It
//   1. logs every write into $7A00-$7C00 from power-on (not just from the REVS2 entry —
//      the filler could be the BASIC front end or the loader, both of which the earlier
//      probe started after), and
//   2. breaks the instant $16E6 or any of the four targets executes, and dumps the page.
//
// Run FROM tools/jsbeeb (it resolves its ROMs relative to cwd):
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_7bxx.mjs [Mcycles]

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const megaCycles = Number(process.argv[2] || 60);

const CALL_SITES = [0x16e6, 0x1704, 0x1739, 0x1748, 0x502a, 0x503b, 0x6612];
const TARGETS = [0x7b00, 0x7b4a, 0x7b9c, 0x7be2];
const REGION_LO = 0x7a00, REGION_HI = 0x7c00;

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

// --- watch the region from power-on ---------------------------------------
const writes = [];
let lastPC = 0;
let firstHitAddr = 0, firstHitPage = null;

tm.processor.debugInstruction.add((addr) => {
    lastPC = addr;
    if (!firstHitAddr && (CALL_SITES.includes(addr) || TARGETS.includes(addr))) {
        firstHitAddr = addr;
        // Snapshot the page AT THE MOMENT OF THE CALL — the whole point.  Reading it at
        // the end of the run would show whatever overwrote it since.
        firstHitPage = [...Array(0x100).keys()].map((i) => tm.processor.readmem(0x7b00 + i));
    }
    return false;
});
tm.processor.debugWrite.add((addr, val) => {
    if (addr >= REGION_LO && addr < REGION_HI && writes.length < 4000)
        writes.push({ addr, val, pc: lastPC });
    return false;
});

// --- boot, track menu, front end, then drive ------------------------------
async function pulse(code, hold = 120000, gap = 250000) {
    tm.processor.sysvia.keyDown(code);
    await tm.runFor(hold);
    tm.processor.sysvia.keyUp(code);
    await tm.runFor(gap);
}

await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();

let reachedMenu = false;
for (let i = 0; i < 40; i++) {
    await pulse(utils.keyCodes.SPACE, 40000, 3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) { reachedMenu = true; break; }
}
if (!reachedMenu) throw new Error("never reached REVSMEN track menu");
await pulse(utils.keyCodes.K5, 40000, 250000);       // Silverstone

console.log(`writes into $${REGION_LO.toString(16)}-$${REGION_HI.toString(16)} before REVS2 ran: ${writes.length}`);

// ⚠ THE FRONT END IS NOT ONLY MENUS.  Two earlier probes stalled here and reported the
// stall as evidence about $7Bxx.  A PC histogram (bbc_probe_pchist.mjs) put the block in
// console_io ($6300-$6342), the OSRDCH line editor — and its caller FUN_3ee0 ($3EE0) calls
// it with X=2 and RE-PROMPTS until FUN_32d0 accepts the result, i.e. it wants a TWO-DIGIT
// NUMBER typed and entered.  So the walk is: select/confirm menu pairs, and answer any
// numeric prompt with two digits and RETURN.  Interleave both, because which one is on
// screen is not observable from here (the engine draws in MODE 4/5, not teletext).
async function answerNumber(d1, d2) {
    await pulse(d1, 120000, 200000);
    await pulse(d2, 120000, 200000);
    await pulse(utils.keyCodes.ENTER, 120000, 400000);
}

for (let i = 0; i < 16 && !firstHitAddr; i++) {
    await pulse(utils.keyCodes.K1);                       // select menu option 1
    await pulse(utils.keyCodes.SPACE);                    // confirm
    await answerNumber(utils.keyCodes.K1, utils.keyCodes.K0);   // "10" for any prompt
}
// Then: starter, throttle, a gear.
for (const k of [utils.keyCodes.T, utils.keyCodes.S, utils.keyCodes.Q, utils.keyCodes.S])
    if (!firstHitAddr) await pulse(k, 400000, 400000);
if (!firstHitAddr) await tm.runFor(megaCycles * 1000 * 1000);

// --- report ---------------------------------------------------------------
const hex = (v, n = 2) => v.toString(16).padStart(n, "0");

console.log(`\n--- first execution of a call site or target: ` +
    (firstHitAddr ? `$${hex(firstHitAddr, 4)}` : "NONE"));

if (firstHitPage) {
    console.log("--- $7B00-$7BFF at that instant:");
    for (let r = 0; r < 16; r++)
        console.log(`  $${hex(0x7b00 + r * 16, 4)}: ` +
            firstHitPage.slice(r * 16, r * 16 + 16).map((b) => hex(b)).join(" ") + "  |" +
            firstHitPage.slice(r * 16, r * 16 + 16)
                .map((b) => (b >= 32 && b < 127 ? String.fromCharCode(b) : ".")).join("") + "|");
    const nz = firstHitPage.filter((b) => b).length;
    console.log(`  nonzero bytes: ${nz}/256`);
}

console.log(`\n--- writes into $${REGION_LO.toString(16)}-$${REGION_HI.toString(16)} in total: ${writes.length}`);
const byPC = new Map();
for (const w of writes) byPC.set(w.pc, (byPC.get(w.pc) || 0) + 1);
for (const [pc, n] of [...byPC.entries()].sort((a, b) => b[1] - a[1]).slice(0, 15))
    console.log(`  written by PC ~$${hex(pc, 4)}: ${n} writes`);
