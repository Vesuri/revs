// Where does a scripted BBC run actually GET TO inside the engine?
//
// Two probes in a row (bbc_probe_unmapped_calls.mjs, bbc_probe_7bxx.mjs) reported "zero
// executions" of the $7Bxx call sites and both were read as evidence about the sites.  It
// is only evidence if the run reached the code AROUND them, and neither probe checked.
// This one checks: it counts executions of the landmarks along the whole path from the
// engine entry to the main loop, so a zero further down can be attributed to the right
// place instead of to the target.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_frontend.mjs [Mcycles]

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const megaCycles = Number(process.argv[2] || 20);

// The path, in order.  A run that stops advancing tells you exactly which step blocked.
const LANDMARKS = [
    [0x1200, "loader_stub entry"],
    [0x790e, "unpack stub (relocated copy)"],
    [0x63bd, "engine_main"],
    [0x3850, "engine_init"],
    [0x5a22, "CallTrackHook"],
    [0x63e0, "front_end_menus"],
    [0x6571, "menu_wait_key"],
    [0x0e50, "kbd_test_key"],
    [0x655a, "FUN_655a (post-selection)"],
    [0x6560, "the $6560 gate loop"],
    [0x16dc, "FUN_16dc (race init)"],
    [0x16e6, "JSR $7BE2   <-- unmapped"],
    [0x4ddd, "hw_init"],
    [0x4e5c, "irq1v_band_schedule"],
    [0x1701, "THE MAIN LOOP"],
    [0x1704, "JSR $7B4A   <-- unmapped"],
    [0x1739, "JSR $7B00   <-- unmapped"],
    [0x1748, "JSR $7BE2   <-- unmapped"],
    [0x1760, "main-loop frame wait"],
];

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

const counts = new Map();
const want = new Set(LANDMARKS.map(([a]) => a));
tm.processor.debugInstruction.add((addr) => {
    if (want.has(addr)) counts.set(addr, (counts.get(addr) || 0) + 1);
    return false;
});

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

let reached = false;
for (let i = 0; i < 40; i++) {
    await pulse(utils.keyCodes.SPACE, 40000, 3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) { reached = true; break; }
}
if (!reached) throw new Error("never reached REVSMEN track menu");
await pulse(utils.keyCodes.K5, 40000, 4 * 1000 * 1000);   // Silverstone
tm.drainText();

// Alternate select / confirm, and after each pair report what the screen says and how far
// the landmarks have moved.  The engine draws in MODE 4/5, so drainText() may be empty —
// the landmark counts are the real read-out.
const report = (tag) => {
    const hit = LANDMARKS.filter(([a]) => counts.get(a)).map(([a]) => a);
    const deepest = hit.length ? hit[hit.length - 1] : 0;
    console.log(`${tag}: deepest landmark $${deepest.toString(16)} ` +
        `(${LANDMARKS.find(([a]) => a === deepest)?.[1] || "none"})`);
};

for (let i = 0; i < 12; i++) {
    await pulse(utils.keyCodes.K1, 200000, 600000);
    await pulse(utils.keyCodes.SPACE, 200000, 600000);
    report(`after menu pair ${i + 1}`);
}
for (const [k, n] of [["T", "starter"], ["S", "throttle"], ["Q", "gear up"], ["S", "throttle"]]) {
    await pulse(utils.keyCodes[k], 400000, 400000);
    report(`after ${n}`);
}
await tm.runFor(megaCycles * 1000 * 1000);

console.log("\n--- landmark executions, in path order");
for (const [a, name] of LANDMARKS)
    console.log(`  $${a.toString(16).padStart(4, "0")}  ${String(counts.get(a) || 0).padStart(9)}  ${name}`);
