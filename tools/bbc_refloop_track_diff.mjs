// Phase 1 self-modifying-code inventory (docs/bbc-reference-loop.md step 2 extras).
// Boots revs.ssd, selects a track, dumps RAM at REVS2 entry ($1200) and again 1M cycles
// later, and reports which bytes in the engine's own range changed -- the per-track hook
// patch surface (ModifyGameCode / CallTrackHook / Hook*).
//
// 1M cycles was chosen by probing how the diff count grows over time: for Brands Hatch it
// plateaus at 5838 bytes by 500k cycles and is still exactly 5838 at 3M, i.e. the hook
// patch completes fast and the engine then goes idle (waiting on the next keypress) rather
// than continuing to mutate its own memory -- so this window captures the patch cleanly,
// not an arbitrary slice of ongoing gameplay drift. See docs/bbc-reference-loop.md status.
//
// jsbeeb loads its ROMs relative to cwd, so run FROM tools/jsbeeb:
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_track_diff.mjs <1-5>
//   1=Brands Hatch 2=Donington Park 3=Oulton Park 4=Snetterton 5=Silverstone (Silverstone's
//   track data is passive -- exec $0000 -- so 5 is expected to show 0 diffs)

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const TRACK_KEYS = {
    1: utils.keyCodes.K1,
    2: utils.keyCodes.K2,
    3: utils.keyCodes.K3,
    4: utils.keyCodes.K4,
    5: utils.keyCodes.K5,
};
const TRACK_NAMES = { 1: "BRANDS", 2: "DONING", 3: "OULTON", 4: "SNETTER", 5: "SILVER" };

const trackNum = Number(process.argv[2] || 1);

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

function pressKey(code, holdCycles = 40000) {
    tm.processor.sysvia.keyDown(code);
    return tm.runFor(holdCycles).then(() => tm.processor.sysvia.keyUp(code));
}

await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();

// Page through the REVINST instructions program to the REVSMEN track menu.
let reachedRevsmen = false;
for (let i = 0; i < 40; i++) {
    await pressKey(utils.keyCodes.SPACE, 40000);
    await tm.runFor(3 * 1000 * 1000);
    const text = tm.drainText();
    if (/PRESS.*BRANDS HATCH/i.test(text)) {
        reachedRevsmen = true;
        break;
    }
}
if (!reachedRevsmen) throw new Error("never reached REVSMEN track menu");

console.log(`selecting track ${trackNum} (${TRACK_NAMES[trackNum]})...`);
await pressKey(TRACK_KEYS[trackNum], 40000);

let revs2Hit = false;
const entryWatch = tm.processor.debugInstruction.add((addr) => {
    if (addr === 0x1200) {
        revs2Hit = true;
        return true;
    }
});

for (let i = 0; i < 80 && !revs2Hit; i++) {
    await tm.runFor(2 * 1000 * 1000);
    const text = tm.drainText();
    if (/CONTINUE/i.test(text)) await pressKey(utils.keyCodes.SPACE, 40000);
    else if (/PRACTICE/i.test(text)) await pressKey(utils.keyCodes.K1, 40000);
}
entryWatch.remove();
if (!revs2Hit) throw new Error("never reached REVS2 entry ($1200)");
console.log("REVS2 entry reached.");

const dumpBefore = Buffer.alloc(0x10000);
for (let a = 0; a < 0x10000; a++) dumpBefore[a] = tm.readbyte(a);

await tm.runFor(1000000);
const dumpAfter = Buffer.alloc(0x10000);
for (let a = 0; a < 0x10000; a++) dumpAfter[a] = tm.readbyte(a);

fs.mkdirSync(new URL("../tmp/", import.meta.url), { recursive: true });
fs.writeFileSync(new URL(`../tmp/dump_${TRACK_NAMES[trackNum]}_before.bin`, import.meta.url), dumpBefore);
fs.writeFileSync(new URL(`../tmp/dump_${TRACK_NAMES[trackNum]}_after.bin`, import.meta.url), dumpAfter);

const diffs = [];
for (let a = 0x1200; a < 0x7000; a++) {
    if (dumpBefore[a] !== dumpAfter[a]) diffs.push(a);
}
console.log(`Track ${TRACK_NAMES[trackNum]}: ${diffs.length} bytes changed in $1200-$6FFF after entry`);
for (const a of diffs.slice(0, 60)) {
    console.log(
        `  $${a.toString(16).padStart(4, "0")}: before=${dumpBefore[a].toString(16).padStart(2, "0")} after=${dumpAfter[a].toString(16).padStart(2, "0")}`,
    );
}
if (diffs.length > 60) console.log(`  ... and ${diffs.length - 60} more`);
