// Does RETURN injection actually reach the MOS?  Verified AT THE BASIC PROMPT, where the
// answer is visible in drainText() — not through a game whose state cannot be seen.
//
// This exists because "no RETURN reached the MOS" was the standing verdict of bbc_drive.mjs's
// own bisect (FUN_32D0 == 0), and every attempt to work around it was made inside the game,
// where a silent no-op and a rejected value look identical.  `*FX 4` / a full line editor is
// far too much machinery to test a keypress: `PRINT 1+1` either prints 2 or it does not.
//
// Three injection paths are compared on the same machine, in order of decreasing trust:
//   raw   sysvia.keyDownRaw(utils.BBC.RETURN)   — matrix position [9,4]
//   code  sysvia.keyDown(13)                    — what TestMachine.type() itself uses for "\n"
//   name  sysvia.keyDown(utils.keyCodes.ENTER)  — the path documented as broken
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_return.mjs

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";

const tm = new TestMachine();
await tm.initialise();
await tm.runUntilInput(20);
tm.startCapture();
tm.drainText();

async function press(down, up, hold = 60000, gap = 300000) {
    down();
    await tm.runFor(hold);
    up();
    await tm.runFor(gap);
}

// Type the letters with the path that is already known to work, then RETURN with the path
// under test — so a failure is attributable to RETURN alone.
async function trial(label, downFn, upFn) {
    tm.drainText();
    for (const ch of ["P", "R", "I", "N", "T", "K1", "SEMICOLON_PLUS", "K1"]) {
        const colrow = utils.BBC[ch] || utils.BBC[ch.toUpperCase()];
        const shift = ch === "SEMICOLON_PLUS";
        if (shift) tm.processor.sysvia.keyDownRaw(utils.BBC.SHIFT);
        await press(
            () => tm.processor.sysvia.keyDownRaw(colrow),
            () => tm.processor.sysvia.keyUpRaw(colrow),
            60000,
            120000,
        );
        if (shift) tm.processor.sysvia.keyUpRaw(utils.BBC.SHIFT);
    }
    const echoed = tm.drainText();
    await press(downFn, upFn);
    await tm.runFor(2 * 1000 * 1000);
    const out = tm.drainText();
    const ok = /2/.test(out);
    console.log(`  ${label.padEnd(6)} echoed=${JSON.stringify(echoed)} after-RETURN=${JSON.stringify(out)}  => ${ok ? "RETURN WORKS" : "no RETURN"}`);
    if (!ok) {
        // Leave the prompt clean for the next trial regardless.
        await press(
            () => tm.processor.sysvia.keyDownRaw(utils.BBC.ESCAPE),
            () => tm.processor.sysvia.keyUpRaw(utils.BBC.ESCAPE),
        );
        tm.drainText();
    }
    return ok;
}

console.log("keyCodes.RETURN =", utils.keyCodes.RETURN, " keyCodes.ENTER =", utils.keyCodes.ENTER);
console.log("BBC.RETURN =", utils.BBC.RETURN, "\n");

const results = {};
results.raw = await trial(
    "raw",
    () => tm.processor.sysvia.keyDownRaw(utils.BBC.RETURN),
    () => tm.processor.sysvia.keyUpRaw(utils.BBC.RETURN),
);
results.code = await trial(
    "code",
    () => tm.processor.sysvia.keyDown(13),
    () => tm.processor.sysvia.keyUp(13),
);
results.name = await trial(
    "name",
    () => tm.processor.sysvia.keyDown(utils.keyCodes.ENTER),
    () => tm.processor.sysvia.keyUp(utils.keyCodes.ENTER),
);

console.log("\nverdict:", JSON.stringify(results));
if (!results.raw && !results.code && !results.name)
    console.log("  => NO path delivers RETURN.  The problem is below the key tables.");
