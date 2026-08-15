// ⭐ GROUND TRUTH FOR THE TRACK MENU — what REVSMEN actually puts on a real BBC's teletext page.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_trackmenu.mjs [--dump=DIR]
//
// ── Why this probe exists ─────────────────────────────────────────────────────────────────
//
// `REVSMEN` is BASIC and this port does not run BASIC, so the circuit menu is PORT-AUTHORED
// (docs/phases.md §5d).  That is the whole reason it needs a fixture: a port-authored page has
// no oracle unless one is recorded, and "it looks about right" is exactly the standard this
// project keeps paying for.  With this fixture the menu is authored ONCE and then diffed
// byte-for-byte against real hardware by `make trackmenu` — the same arrangement as `make mode7`,
// which deliberately SKIPS these very snapshots because they are BASIC's work.
//
// The BASIC program is on the disc and detokenises to 43 lines; the page it draws is:
//
//     row  2  VDU 151 then 39x VDU 185           a graphics-white mosaic rule
//     rows 3,4 VDU 141 ... twice                 "REVS REVS REVS" in double height
//     row  5  the rule again
//     row 10  VDU 134,136 "    PRESS"            cyan, FLASHING
//     rows 12,14,16,18,20                        the five circuits, digit on a blue field
//     row 23  "     PRESS SPACE BAR TO CONTINUE"
//
// and the selection feedback is line 240, `PRINTTAB(5,10+2*A%);:VDU129,157,131` — three bytes,
// which recolour the chosen digit's field from blue to RED.  Every one of those claims is read
// out of the BASIC; NONE of them is trusted here.  What is recorded is screen RAM.
//
// ── What it records ───────────────────────────────────────────────────────────────────────
//
//     title.bin        the 5TRSCRN teletext title page, as the real machine displays it
//     menu.bin         the menu page, no selection made
//     menu_sel<N>.bin  the same page after option N has been pressed (N = 1..5)
//     trackmenu.txt    the manifest, plus the MEASURED title-screen dwell in cycles
//
// ⚠ ONE SELECTION PER BOOT, and that is not laziness: REVSMEN's line 220 leaves its REPEAT the
// instant a digit arrives and never asks again, so a run can only ever witness one highlight.
// The probe therefore boots once per option.  The base page is captured on every one of those
// runs and all five are required to be IDENTICAL — a free consistency check that costs nothing
// and would catch a probe that snapshots at a drifting moment.
//
// ⚠ It CHAINs REVSMEN directly instead of walking REVINST's instruction pages with SPACE.  Same
// program, same MOS, same disc — !BOOT itself does `PAGE=&1900` and then CHAINs, so this is the
// documented entry, and it turns a ~90 M-cycle walk into a ~10 M one, five times over.

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import * as utils from "./jsbeeb/src/utils.js";
import fs from "fs";
import path from "path";

const SCREEN = 0x7c00; // MODE 7 screen RAM: 25 rows of 40, 1 KB
const CPS = 2 * 1000 * 1000;

const argv = process.argv.slice(2);
const opt = (name, dflt) => {
    const hit = argv.find((a) => a.startsWith(`--${name}=`));
    return hit === undefined ? dflt : hit.slice(name.length + 3);
};
const dumpDir = opt("dump", null);

const disc = fs.readFileSync(new URL("../revs.ssd", import.meta.url));

/* The page as text, for the log.  Teletext control codes are < $20 and print as '.', which is
   enough to recognise a page and NOT enough to compare two — the comparison is on bytes. */
function pageText(bytes) {
    const out = [];
    for (let r = 0; r < 25; r++) {
        let s = "";
        for (let c = 0; c < 40; c++) {
            const b = bytes[r * 40 + c];
            s += b >= 0x20 && b < 0x7f ? String.fromCharCode(b) : ".";
        }
        out.push(s);
    }
    return out;
}

const has = (bytes, needle) => pageText(bytes).some((l) => l.includes(needle));

/* Boot, CHAIN REVSMEN, and stop when `ready(page)` says the page we want is up.  Returns the
   cycle count at that moment so the caller can time one phase against another. */
async function bootToMenu() {
    const tm = new TestMachine();
    await tm.initialise();
    tm.loadDiscData(new Uint8Array(disc));
    tm.startCapture();
    const cpu = tm.processor;
    const page = () => new Uint8Array(1024).map((_, i) => cpu.readmem(SCREEN + i));

    await tm.runUntilInput(20);
    tm.drainText();
    await tm.type("PAGE=&1900");
    await tm.runUntilInput(20);
    tm.drainText();
    await tm.type('CHAIN"REVSMEN"');

    /* ⭐ TWO WAITS, and the ORDER is the measurement: the title screen goes up first (line 40
       `*LOAD 5TRSCRN`), the delay loop runs, then line 60's MODE7 clears it and the menu is
       drawn.  Waiting for the menu alone would never see the title page at all, and taking the
       dwell from the BASIC listing would be a guess about BASIC's own speed. */
    let title = null;
    let titleAt = 0;
    for (let i = 0; i < 400 && !title; i++) {
        await tm.runFor(CPS / 20);
        const p = page();
        if (has(p, "Revs + Revs 4 Tracks")) {
            title = p;
            titleAt = cpu.cycleSeconds * CPS + cpu.currentCycles;
        }
    }
    if (!title) throw new Error("never saw the 5TRSCRN title page");

    let menu = null;
    let menuAt = 0;
    for (let i = 0; i < 600 && !menu; i++) {
        await tm.runFor(CPS / 20);
        const p = page();
        /* Wait for the LAST thing the page gets, not the first: SILVERSTONE is option 5, so a
           page holding it holds every earlier row too.  A `BRANDS HATCH` test would fire while
           the four rows under it were still being printed. */
        if (has(p, "SILVERSTONE")) {
            /* ...and let the SPACE-BAR row (line 250, printed only after a digit) settle out of
               the way by taking the page one field later; nothing else writes here. */
            await tm.runFor(CPS / 20);
            menu = page();
            menuAt = cpu.cycleSeconds * CPS + cpu.currentCycles;
        }
    }
    if (!menu) throw new Error("never saw the REVSMEN menu page");

    return { tm, cpu, page, title, menu, dwell: menuAt - titleAt };
}

const results = [];
let baseMenu = null;
let baseTitle = null;
let dwell = 0;

for (const option of [1, 2, 3, 4, 5]) {
    const { tm, cpu, page, title, menu, dwell: d } = await bootToMenu();

    if (!baseMenu) {
        baseMenu = menu;
        baseTitle = title;
        dwell = d;
    } else {
        /* ⚠ THE CONSISTENCY CHECK, and it is not decoration: it is the only thing standing
           between this fixture and a snapshot taken at a drifting moment in the paint. */
        for (const [what, a, b] of [
            ["menu", baseMenu, menu],
            ["title", baseTitle, title],
        ]) {
            for (let i = 0; i < 1024; i++)
                if (a[i] !== b[i])
                    throw new Error(
                        `${what} page differs between boots at offset ${i} ` +
                            `(${a[i]} vs ${b[i]}) — the snapshot moment is not stable`,
                    );
        }
    }

    /* Press the digit.  REVSMEN reads it with INKEY(6), i.e. out of the MOS keyboard buffer
       after a `*FX15,0` flush, so a press-and-release is what it wants. */
    const key = [utils.BBC.K1, utils.BBC.K2, utils.BBC.K3, utils.BBC.K4, utils.BBC.K5][option - 1];
    cpu.sysvia.keyDownRaw(key);
    await tm.runFor(40000);
    cpu.sysvia.keyUpRaw(key);
    await tm.runFor(CPS);

    const sel = page();
    if (!has(sel, "PRESS SPACE BAR TO CONTINUE"))
        throw new Error(`option ${option}: the digit was not accepted (no SPACE BAR row)`);

    results.push({ option, sel });
    console.log(`   option ${option}: highlighted, SPACE BAR row present`);
}

console.log(`\n   title-screen dwell: ${dwell} cycles = ${(dwell / CPS).toFixed(2)} s ` +
            `= ${Math.round((dwell / CPS) * 50)} display fields`);
console.log("\n   the menu page as the real machine holds it:");
for (const [i, l] of pageText(baseMenu).entries()) if (l.trim()) console.log(`   ${String(i).padStart(2)}|${l}|`);

/* The three bytes option 1 changes, read out rather than asserted from the BASIC. */
{
    const diff = [];
    for (let i = 0; i < 1024; i++) if (baseMenu[i] !== results[0].sel[i]) diff.push(i);
    console.log(`\n   option 1 changes ${diff.length} bytes:`);
    for (const o of diff)
        console.log(`      row ${Math.floor(o / 40)} col ${o % 40}: ` +
                    `$${baseMenu[o].toString(16)} -> $${results[0].sel[o].toString(16)}`);
}

if (dumpDir) {
    const dir = path.resolve(dumpDir);
    fs.mkdirSync(dir, { recursive: true });
    fs.writeFileSync(path.join(dir, "title.bin"), baseTitle);
    fs.writeFileSync(path.join(dir, "menu.bin"), baseMenu);
    const lines = [`# recorded by tools/bbc_probe_trackmenu.mjs off revs.ssd on a real BBC (jsbeeb)`,
                   `D ${dwell}  # title-screen dwell, in 2 MHz cycles`,
                   `S title.bin  # the 5TRSCRN teletext page`,
                   `S menu.bin  # the menu page, nothing selected`];
    for (const { option, sel } of results) {
        const name = `menu_sel${option}.bin`;
        fs.writeFileSync(path.join(dir, name), sel);
        lines.push(`S ${name}  # after pressing '${option}'`);
    }
    fs.writeFileSync(path.join(dir, "trackmenu.txt"), lines.join("\n") + "\n");
    console.log(`\n   fixture: ${results.length + 2} pages -> ${dir}`);
}
