// Are the seven JSRs into $7B00-$7BFF ever executed — and does anything ever put code
// there?
//
// The transpiler found engine code calling $7B00 / $7B4A / $7B9C / $7BE2.  Nothing on
// either disc loads into that page: REVS2 covers $1200-$6FFF and a track file $70DB-$7814,
// so in both disasm/revs_mem.bin and disasm/revs_runtime.bin the page is all zero, and a
// real-BBC dump taken 1M cycles in shows it holding leftover text from the BASIC front end
// ("...ornso" at $7BE2 — the tail of "Acornsoft").  A JSR there would execute garbage.
//
// Two readings, and they need different responses from the port, so guessing is not an
// option: either the call sites are unreachable, or something fills the page at run time
// through a mechanism no absolute-operand scan can see (a zero-page pointer copy — the
// exact blind spot the Phase 2 pointer pass was written for).
//
// So: watch every WRITE into $7B00-$7BFF and every EXECUTION of the four targets and of
// the seven call sites, across boot, the track menu and the engine's front end.
//
// jsbeeb loads its ROMs relative to cwd, so run FROM tools/jsbeeb:
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_unmapped_calls.mjs [Mcycles]

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const megaCycles = Number(process.argv[2] || 30);

const CALL_SITES = [0x16e6, 0x1704, 0x1739, 0x1748, 0x502a, 0x503b, 0x6612];
const TARGETS = [0x7b00, 0x7b4a, 0x7b9c, 0x7be2];
const REGION_LO = 0x7a00, REGION_HI = 0x7c00;   // up to MODE 7 screen RAM

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();

function pressKey(code, holdCycles = 40000) {
    tm.processor.sysvia.keyDown(code);
    return tm.runFor(holdCycles).then(() => tm.processor.sysvia.keyUp(code));
}

let reachedMenu = false;
for (let i = 0; i < 40; i++) {
    await pressKey(utils.keyCodes.SPACE, 40000);
    await tm.runFor(3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) { reachedMenu = true; break; }
}
if (!reachedMenu) throw new Error("never reached REVSMEN track menu");
await pressKey(utils.keyCodes.K5, 40000);           // Silverstone

let revs2Hit = false;
const entryWatch = tm.processor.debugInstruction.add((addr) => {
    if (addr === 0x1200) { revs2Hit = true; return true; }
    return false;
});
for (let i = 0; i < 80 && !revs2Hit; i++) {
    await tm.runFor(2 * 1000 * 1000);
    const text = tm.drainText();
    if (/CONTINUE/i.test(text)) await pressKey(utils.keyCodes.SPACE, 40000);
    else if (/PRACTICE/i.test(text)) await pressKey(utils.keyCodes.K1, 40000);
}
entryWatch.remove();
if (!revs2Hit) throw new Error("never reached REVS2 entry ($1200)");
console.log("REVS2 entry reached — probing from here.");

const execCount = new Map();
const writes = [];            // {addr, val, pc}
let lastPC = 0;

const iWatch = tm.processor.debugInstruction.add((addr) => {
    lastPC = addr;
    if (CALL_SITES.includes(addr) || TARGETS.includes(addr))
        execCount.set(addr, (execCount.get(addr) || 0) + 1);
    return false;
});
const wWatch = tm.processor.debugWrite.add((addr, val) => {
    if (addr >= REGION_LO && addr < REGION_HI && writes.length < 200)
        writes.push({ addr, val, pc: lastPC });
    return false;
});

async function pulse(code, hold = 120000, gap = 250000) {
    tm.processor.sysvia.keyDown(code);
    await tm.runFor(hold);
    tm.processor.sysvia.keyUp(code);
    await tm.runFor(gap);
}

// Walk the front-end menu chain the same way bbc_trace.mjs does, then try to drive.
for (let i = 0; i < 24; i++)
    await pulse(i % 2 === 0 ? utils.keyCodes.K1 : utils.keyCodes.SPACE);
await pulse(utils.keyCodes.T, 400000, 200000);
for (const k of [utils.keyCodes.S, utils.keyCodes.Q, utils.keyCodes.S, utils.keyCodes.L])
    await pulse(k, 300000, 150000);
await tm.runFor(megaCycles * 1000 * 1000);

iWatch.remove();
wWatch.remove();

console.log("\n--- execution of the call sites and their targets");
for (const a of [...CALL_SITES, ...TARGETS])
    console.log(`  $${a.toString(16).padStart(4, "0")}: ${execCount.get(a) || 0} executions`);

console.log(`\n--- writes into $${REGION_LO.toString(16)}-$${REGION_HI.toString(16)}: ${writes.length}`);
for (const w of writes.slice(0, 40))
    console.log(`  $${w.addr.toString(16)} = $${w.val.toString(16).padStart(2, "0")}  (PC ~$${w.pc.toString(16)})`);

const cpu = tm.processor;
const peek = (a) => cpu.readmem(a);
console.log("\n--- final contents");
for (const a of TARGETS) {
    const b = [...Array(8).keys()].map((i) => peek(a + i).toString(16).padStart(2, "0"));
    console.log(`  $${a.toString(16)}: ${b.join(" ")}`);
}
let nz = 0;
for (let a = REGION_LO; a < REGION_HI; a++) if (peek(a)) nz++;
console.log(`  nonzero in the region: ${nz}`);
