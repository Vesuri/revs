// Execution-coverage trace — the MEASURED half of the entry-point sweep.
//
// tools/sweep_entrypoints.py walks the binary statically.  Anything it misses is, by
// definition, something it could not see: a dispatch table it did not recognise, a handler
// only the MOS calls, code a track hook patches in.  A static tool cannot report its own
// blind spots.  A trace can: every address the real machine executes must appear in the
// static walk, and any that does not is a missed entry point.  That is the check
// docs/entrypoint-sweep.md's "definition of done" actually needs.
//
// Boots revs.ssd, selects a track, plays it for a while, and writes a 64 KB coverage map
// (1 byte per address: 1 = executed as an instruction OPCODE) plus a JSON summary.
//
// jsbeeb loads its ROMs relative to cwd, so run FROM tools/jsbeeb:
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_trace.mjs [track 1-5] [Mcycles]
//   1=Brands Hatch 2=Donington Park 3=Oulton Park 4=Snetterton 5=Silverstone
//
// Writes tmp/trace_<TRACK>.bin (64 KB coverage) and tmp/trace_<TRACK>.json.

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

const trackNum = Number(process.argv[2] || 5);
const megaCycles = Number(process.argv[3] || 20);

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

let reachedRevsmen = false;
for (let i = 0; i < 40; i++) {
    await pressKey(utils.keyCodes.SPACE, 40000);
    await tm.runFor(3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) {
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
console.log("REVS2 entry reached — tracing from here.");

// Coverage from the engine entry onward.  Recording OPCODE addresses only (not operand
// bytes) keeps the map directly comparable with the static walk's instruction starts.
const cover = new Uint8Array(0x10000);
const hits = new Uint32Array(0x10000);
let executed = 0;
const trace = tm.processor.debugInstruction.add((addr) => {
    if (!cover[addr]) {
        cover[addr] = 1;
        executed++;
    }
    if (hits[addr] < 0xffffffff) hits[addr]++;
    return false;
});

// Once REVS2 starts it takes the screen over, so drainText() sees nothing and there is no
// way to steer by reading the display.  Drive it blind instead: cycle the documented keys
// (docs: L/+ steer, S throttle, A brake, T starter, Q gear up, TAB gear down, SPACE amplify)
// plus RETURN/1/2 for whatever the engine's own front end asks.  The goal is breadth of
// executed code, not a good lap.
const DRIVE = [
    utils.keyCodes.SPACE,
    utils.keyCodes.RETURN,
    utils.keyCodes.K1,
    utils.keyCodes.K2,
    utils.keyCodes.T,
    utils.keyCodes.S,
    utils.keyCodes.Q,
    utils.keyCodes.A,
];
const steps = megaCycles;
for (let i = 0; i < steps; i++) {
    await tm.runFor(1000 * 1000);
    await pressKey(DRIVE[i % DRIVE.length], 150000);
    if (i % 5 === 0) console.log(`  ${i + 1}/${steps} Mcycles — ${executed} distinct addresses`);
}
trace.remove();

// The hottest addresses in the engine range say what the trace actually spent its time in.
// If the list is a handful of addresses in a tight window, the engine is parked on an input
// wait and the trace is not worth trusting as a coverage cross-check — report that plainly
// rather than letting a thin trace look like agreement.
const hot = [];
for (let a = 0x0b00; a < 0x7900; a++) if (hits[a]) hot.push([a, hits[a]]);
hot.sort((x, y) => y[1] - x[1]);
console.log("\nhottest engine addresses (a tight cluster here means it was parked on a wait):");
for (const [a, n] of hot.slice(0, 12)) console.log(`  $${a.toString(16)}  ${n}`);

const name = TRACK_NAMES[trackNum];
fs.mkdirSync(new URL("../tmp/", import.meta.url), { recursive: true });
fs.writeFileSync(new URL(`../tmp/trace_${name}.bin`, import.meta.url), cover);

const inEngine = [];
for (let a = 0x0b00; a < 0x7900; a++) if (cover[a]) inEngine.push(a);
const summary = {
    track: name,
    megaCycles,
    distinctOpcodeAddresses: executed,
    inEngineRange: inEngine.length,
    ranges: (() => {
        const out = [];
        let s = null,
            p = null;
        for (const a of inEngine) {
            if (s === null) {
                s = p = a;
                continue;
            }
            if (a - p > 64) {
                out.push([s, p]);
                s = a;
            }
            p = a;
        }
        if (s !== null) out.push([s, p]);
        return out.map(([x, y]) => `$${x.toString(16)}-$${y.toString(16)}`);
    })(),
};
fs.writeFileSync(
    new URL(`../tmp/trace_${name}.json`, import.meta.url),
    JSON.stringify(summary, null, 2),
);
console.log(`\n${executed} distinct opcode addresses executed (${inEngine.length} in $0B00-$78FF)`);
console.log(`wrote tmp/trace_${name}.bin and tmp/trace_${name}.json`);
