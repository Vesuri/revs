// Which key gets the engine past its input wait?  An experiment, not a deduction.
//
// The engine parks in key_config_menu ($6571), polling kbd_test_key ($0E50) against the table at
// $39E0.  At runtime that table holds SPACE / 1 / 2 / 3 (the static image has two more entries
// that the engine zeroes).  Pulsing those keys did not get past it, so the question is whether
// the press is registering at all, or registering and being rejected.
//
// This holds each candidate key continuously and reports how many NEW engine addresses execute
// while it is held.  A key that unblocks the game shows a jump; one that does nothing shows zero.
//
// Run FROM tools/jsbeeb:
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_keys.mjs

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

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

let ok = false;
for (let i = 0; i < 40; i++) {
    tm.processor.sysvia.keyDown(utils.keyCodes.SPACE);
    await tm.runFor(40000);
    tm.processor.sysvia.keyUp(utils.keyCodes.SPACE);
    await tm.runFor(3 * 1000 * 1000);
    if (/PRESS.*BRANDS HATCH/i.test(tm.drainText())) {
        ok = true;
        break;
    }
}
if (!ok) throw new Error("never reached REVSMEN track menu");
tm.processor.sysvia.keyDown(utils.keyCodes.K5); // Silverstone
await tm.runFor(40000);
tm.processor.sysvia.keyUp(utils.keyCodes.K5);

let hit = false;
const w = tm.processor.debugInstruction.add((a) => (a === 0x1200 ? ((hit = true), true) : false));
// The path from the track menu to the engine entry has its own prompts, and REVSMEN is still
// BASIC at this point so drainText() can still see them.
for (let i = 0; i < 80 && !hit; i++) {
    await tm.runFor(2 * 1000 * 1000);
    const text = tm.drainText();
    let key = null;
    if (/CONTINUE/i.test(text)) key = utils.keyCodes.SPACE;
    else if (/PRACTICE/i.test(text)) key = utils.keyCodes.K1;
    if (key !== null) {
        tm.processor.sysvia.keyDown(key);
        await tm.runFor(40000);
        tm.processor.sysvia.keyUp(key);
    }
}
w.remove();
if (!hit) throw new Error("never reached REVS2 entry");
console.log("REVS2 entry reached.\n");

const cover = new Uint8Array(0x10000);
let fresh = 0;
const trace = tm.processor.debugInstruction.add((a) => {
    if (a >= 0x0b00 && a < 0x7900 && !cover[a]) {
        cover[a] = 1;
        fresh++;
    }
    return false;
});

// Settle first, so the baseline is "what runs while parked" and not "what runs at startup".
await tm.runFor(4 * 1000 * 1000);
let base = 0;
for (let a = 0x0b00; a < 0x7900; a++) if (cover[a]) base++;
console.log(`parked baseline: ${base} engine addresses executed`);
console.log(`  ${stackTrace()}`);

// $39E0 as the engine currently has it — the authority, not the static image.
const tbl = [];
for (let i = 0; i < 8; i++) tbl.push(tm.readbyte(0x39e0 + i));
console.log(`live $39E0: ${tbl.map((b) => b.toString(16).padStart(2, "0")).join(" ")}`);

// Which of the five key_config_menu call sites in the $63E0 menu chain is it sitting in?  The
// hot-address list only says "$657C", which is inside the shared routine and identifies nothing.
// Counting the call sites says which MENU, and that is the actionable fact.
const CALL_SITES = { 0x63f7: "menu@63F7 X=2", 0x6416: "menu@6416 X=3", 0x6426: "menu@6426 X=3",
                     0x646d: "menu@646D X=2", 0x64e7: "menu@64E7" };
const siteHits = {};
tm.processor.debugInstruction.add((a) => {
    if (CALL_SITES[a] !== undefined) siteHits[a] = (siteHits[a] || 0) + 1;
    return false;
});

const CANDIDATES = [
    ["SPACE", utils.keyCodes.SPACE],
    ["1", utils.keyCodes.K1],
    ["2", utils.keyCodes.K2],
    ["3", utils.keyCodes.K3],
    ["RETURN", utils.keyCodes.RETURN],
    ["T", utils.keyCodes.T],
    ["S", utils.keyCodes.S],
    ["A", utils.keyCodes.A],
    ["Q", utils.keyCodes.Q],
    ["TAB", utils.keyCodes.TAB],
    ["SHIFT", utils.keyCodes.SHIFT],
    ["ESCAPE", utils.keyCodes.ESCAPE],
];

// Read the 6502 stack and decode it as return addresses.  This is the decisive measurement: it
// says which call is actually outstanding, whereas a hot-address list only names the shared
// routine everything funnels into.  (JSR pushes target-1, so a return address on the stack is
// the address of the last byte of the JSR — add 1 to get the instruction after it.)
function stackTrace() {
    const sp = tm.processor.s;
    const out = [];
    for (let a = 0x0100 + ((sp + 1) & 0xff); a <= 0x01ff; a++) {
        const lo = tm.readbyte(a);
        const hi = tm.readbyte(a + 1);
        const ret = (lo | (hi << 8)) + 1;
        if (ret >= 0x0b00 && ret < 0x7900) out.push(`$${ret.toString(16)}`);
    }
    return `S=$${sp.toString(16)} plausible return addresses: ${out.join(" ")}`;
}

function sites() {
    const e = Object.entries(siteHits).map(([a, n]) => `${CALL_SITES[a]}x${n}`);
    return e.length ? e.join(" ") : "(none reached)";
}

for (const [name, code] of CANDIDATES) {
    fresh = 0;
    tm.processor.sysvia.keyDown(code);
    await tm.runFor(3 * 1000 * 1000); // held, not pulsed
    tm.processor.sysvia.keyUp(code);
    await tm.runFor(1 * 1000 * 1000);
    console.log(`  hold ${name.padEnd(7)} -> ${fresh} new engine addresses   ${sites()}`);
}

// Then the sequence the tracer uses, to see how far a pulse chain actually gets.
console.log("\npulsing 1/SPACE alternately, 24 times:");
for (let i = 0; i < 24; i++) {
    fresh = 0;
    const code = i % 2 === 0 ? utils.keyCodes.K1 : utils.keyCodes.SPACE;
    tm.processor.sysvia.keyDown(code);
    await tm.runFor(120000);
    tm.processor.sysvia.keyUp(code);
    await tm.runFor(250000);
    console.log(`  pulse ${String(i + 1).padStart(2)} ${i % 2 === 0 ? "1" : "SPACE"} -> `
        + `+${fresh}   ${sites()}`);
    if (i === 3 || i === 23) console.log(`      ${stackTrace()}`);
}

trace.remove();
let total = 0;
for (let a = 0x0b00; a < 0x7900; a++) if (cover[a]) total++;
console.log(`\ntotal engine addresses seen: ${total}`);
