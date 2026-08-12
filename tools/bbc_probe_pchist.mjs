// A PC histogram of the engine, after a scripted front-end walk.
//
// bbc_probe_frontend.mjs narrowed the block to "somewhere inside FUN_3c50 ($3C50), which
// never returns to its caller at $6563".  Guessing which of its callees is the wait is how
// the last two probes wasted a run each; this one just counts every executed address in
// the engine range and prints the hottest, which names the loop directly.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_pchist.mjs [Mcycles]

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const megaCycles = Number(process.argv[2] || 10);

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

async function pulse(code, hold = 200000, gap = 600000) {
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
await pulse(utils.keyCodes.K5, 40000, 4 * 1000 * 1000);

// Get into the engine and past the first menus.
for (let i = 0; i < 10; i++) {
    await pulse(utils.keyCodes.K1);
    await pulse(utils.keyCodes.SPACE);
}

// NOW start counting: whatever it is doing at this point is the thing that never finishes.
const hist = new Map();
const w = tm.processor.debugInstruction.add((addr) => {
    if (addr >= 0x0b00 && addr < 0x7000) hist.set(addr, (hist.get(addr) || 0) + 1);
    return false;
});
// Keep driving while sampling — a wait that a key WOULD have released must not be
// misreported as a hang.
for (let i = 0; i < 6; i++) {
    await pulse(utils.keyCodes.SPACE);
    await pulse(utils.keyCodes.RETURN);
    await pulse(utils.keyCodes.K1);
}
await tm.runFor(megaCycles * 1000 * 1000);
w.remove();

const total = [...hist.values()].reduce((a, b) => a + b, 0);
console.log(`\n--- hottest engine addresses (${total} sampled instruction executions)`);
for (const [a, n] of [...hist.entries()].sort((x, y) => y[1] - x[1]).slice(0, 30))
    console.log(`  $${a.toString(16).padStart(4, "0")}  ${String(n).padStart(9)}  ${(100 * n / total).toFixed(1)}%`);

// Contiguous hot ranges read better than a scatter of addresses.
const hot = [...hist.keys()].sort((a, b) => a - b);
const runs = [];
for (const a of hot) {
    const last = runs[runs.length - 1];
    if (last && a - last.hi <= 3) { last.hi = a; last.n += hist.get(a); }
    else runs.push({ lo: a, hi: a, n: hist.get(a) });
}
console.log("\n--- hot contiguous ranges");
for (const r of runs.sort((x, y) => y.n - x.n).slice(0, 12))
    console.log(`  $${r.lo.toString(16)}-$${r.hi.toString(16)}  ${r.n}  ${(100 * r.n / total).toFixed(1)}%`);
