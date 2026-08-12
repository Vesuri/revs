// Phase 1 smoke test for the BBC reference loop (docs/bbc-reference-loop.md).
// Boots the real revs.ssd under jsbeeb, navigates REVINST -> REVSMEN -> track
// select -> REVS2 entry, and diffs a full RAM dump against disasm/revs_mem.bin.
//
// Requires jsbeeb cloned + `npm install`ed at tools/jsbeeb (git-ignored, node >=24.15).
// jsbeeb loads its ROMs relative to cwd, so run FROM tools/jsbeeb:
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_smoke.mjs

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));

const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();

function pressKey(code, holdCycles = 40000) {
    tm.processor.sysvia.keyDown(code);
    return tm.runFor(holdCycles).then(() => tm.processor.sysvia.keyUp(code));
}

console.log("running until input prompt...");
await tm.runUntilInput(20);
console.log("captured so far:", JSON.stringify(tm.drainText()));

console.log("running boot sequence via *EXEC !BOOT (Option 3 = EXEC autoboot)...");
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
console.log(tm.drainText());
console.log("PC after boot sequence settles:", tm.processor.pc.toString(16));

console.log("pressing SPACE repeatedly through the REVINST instructions pages...");
let reachedRevsmen = false;
for (let i = 0; i < 40; i++) {
    await pressKey(utils.keyCodes.SPACE, 40000);
    await tm.runFor(3 * 1000 * 1000);
    const text = tm.drainText();
    console.log(`-- page ${i} -- PC=${tm.processor.pc.toString(16)} textlen=${text.length}`);
    if (/PRESS.*BRANDS HATCH/i.test(text)) {
        console.log("*** track menu (REVSMEN) reached ***");
        reachedRevsmen = true;
        break;
    }
}
if (!reachedRevsmen) throw new Error("never reached REVSMEN track menu");

let revs2HitAddr = null;
const revs2Watch = tm.processor.debugInstruction.add((addr) => {
    if (addr === 0x1200) {
        revs2HitAddr = addr;
        return true; // halt execution exactly at engine entry
    }
});

console.log("selecting track 5 (Silverstone)...");
await pressKey(utils.keyCodes.K5, 40000);
for (let i = 0; i < 80 && revs2HitAddr === null; i++) {
    await tm.runFor(2 * 1000 * 1000);
    const text = tm.drainText();
    if (text.length) console.log(`[${i}] PC=${tm.processor.pc.toString(16)}:`, JSON.stringify(text.slice(0, 300)));
    if (/CONTINUE/i.test(text)) await pressKey(utils.keyCodes.SPACE, 40000);
    else if (/PRACTICE/i.test(text)) await pressKey(utils.keyCodes.K1, 40000);
}
revs2Watch.remove();
if (revs2HitAddr === null) throw new Error("never reached REVS2 entry ($1200)");
console.log("*** REVS2 entry reached ***");

console.log("dumping full 64K memory at engine entry ($1200)...");
const dump = Buffer.alloc(0x10000);
for (let a = 0; a < 0x10000; a++) dump[a] = tm.readbyte(a);
fs.mkdirSync(new URL("../tmp/", import.meta.url), { recursive: true });
fs.writeFileSync(new URL("../tmp/dump_silverstone_entry.bin", import.meta.url), dump);

const ref = fs.readFileSync(new URL("../disasm/revs_mem.bin", import.meta.url));
console.log("dump length:", dump.length, "reference length:", ref.length);
let diffs = [];
for (let a = 0; a < Math.min(dump.length, ref.length); a++) {
    if (dump[a] !== ref[a]) diffs.push(a);
}
console.log(`total differing bytes: ${diffs.length} / ${Math.min(dump.length, ref.length)}`);
console.log("first 40 diffs (addr: dump vs ref):");
for (const a of diffs.slice(0, 40)) {
    console.log(
        `  $${a.toString(16).padStart(4, "0")}: dump=${dump[a].toString(16).padStart(2, "0")} ref=${ref[a].toString(16).padStart(2, "0")}`,
    );
}
console.log("FINAL: PC=", tm.processor.pc.toString(16));
