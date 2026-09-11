// ⭐ WHAT THE MOS's MODE 7 VDU DRIVER DOES TO A PRINTABLE CODE, measured on a real MOS.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_m7charmap.mjs
//
// The port's teletext driver (src/platform/teletext.cpp) stored every code >= $20 verbatim, and
// `make mode7` found a page where that is wrong: the engine prints twelve `_` ($5F) to underline
// the ENTER NAME OF DRIVER field and the real screen holds $60.  The SAA5050 has no underscore
// glyph, so the MOS remaps — but "which codes and to what" is a contract to MEASURE, not to
// recall, so this probe prints EVERY code $20..$FF into a fixed cell and reads back what landed
// in screen RAM.  ⚠ $7F is DELETE and moves the cursor instead of printing; it is reported as
// such rather than as a translation.
import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import fs from "fs";

const SCREEN = 0x7c00;
const SCRATCH = 0x7000; // free in MODE 7 (the screen starts at $7C00) and above BASIC's PAGE

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine();
await tm.initialise();
tm.loadDiscData(new Uint8Array(data));
await tm.runUntilInput(20);
tm.drainText();

// ⚠ Measure each code against a KNOWN FILLER, and away from the home position.  A first attempt
// printed at (0,0) and read the cell back, which reports a code that printed NOTHING as whatever
// the previous iteration left there — and $7F is DELETE, so from $7F on the whole sweep was the
// stale value $7E.  So: put a '.' in the cell, home the cursor back onto it, print the code, and
// read.  A cell still holding '.' means the code did not store.
await tm.type("VDU 22,7");
await tm.type("FOR I%=32 TO 255:VDU 31,10,12,46,31,10,12,I%:?(&7000+I%)=?(&7C00+12*40+10):NEXT");
await tm.runUntilInput(30);

const cpu = tm.processor;
const rows = [];
for (let code = 0x20; code <= 0xff; code++) {
    const got = cpu.readmem(SCRATCH + code);
    if (got !== code) rows.push([code, got]);   /* '.' ($2E) means the code stored nothing */
}
console.log(`=== MODE 7: codes whose SCREEN BYTE differs from the code printed (${rows.length} of 224)`);
for (const [code, got] of rows) {
    const ch = (c) => (c >= 0x20 && c < 0x7f ? `'${String.fromCharCode(c)}'` : "   ");
    console.log(`   $${code.toString(16).padStart(2, "0")} ${ch(code)}  ->  $${got.toString(16).padStart(2, "0")} ${ch(got)}`);
}
console.log(`\n=== the screen byte for every code, $20..$FF:`);
for (let base = 0x20; base <= 0xf0; base += 16) {
    let line = `   $${base.toString(16)}x `;
    for (let i = 0; i < 16 && base + i <= 0xff; i++)
        line += ` ${cpu.readmem(SCRATCH + base + i).toString(16).padStart(2, "0")}`;
    console.log(line);
}
