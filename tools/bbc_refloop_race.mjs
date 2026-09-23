// ⭐ THE BBC DRIVING REFERENCE LOOP — boots revs.ssd and gets a REAL BBC into a REAL race,
// with a readable transcript of every prompt on the way.  This is the ground-truth instrument
// for every "is the port faithful?" pixel question about the 3D view.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_refloop_race.mjs [--frames=N]
//        [--track=1..5] [--wing=0..40] [--drive] [--dump=DIR] [--fill=lo-hi] [--irq-abi]
//
//   --drive     start the engine, engage first gear and hold the throttle (a MOVING car; a
//               parked car gives byte-identical frames and a confident useless answer)
//   --fill=a-b  attribute every frame-buffer write in those display lines to the routine that
//               made it — how the horizon-stripe fill loop was found
//   --irq-abi   measure which registers survive an interrupt, i.e. the contract the port's ISR
//               shim has to reproduce (it got A wrong, and that WAS the stripes)
//
// ── Why the previous attempts failed, and what actually fixed it ──────────────────────────
//
// Three things were believed and all three were wrong:
//
//  1. "No RETURN reaches the MOS; fix key injection."  That was bbc_drive.mjs's own bisect
//     verdict and it stood for two days.  It is FALSE — `tools/bbc_probe_return.mjs` types
//     `PRINT 1+1` at the BASIC prompt, where success is visible in drainText(), and RETURN
//     arrives on all three injection paths (keyDownRaw([9,4]), keyDown(13), keyDown(ENTER)).
//     The verdict was measured through a game whose state could not be seen, so "the key did
//     not arrive" and "we are not where we think we are" looked identical.
//
//  2. "The front end cannot be read, because it draws in MODE 4/5 and drainText() sees
//     nothing."  Also false, and this is the fix.  The front end prints through ONE routine,
//     print_message ($4D7E), indexed by X, with the string pointers in $3AD0/$3B50 and a
//     token/space encoding.  Hooking that one address and decoding the string gives a
//     complete human-readable transcript of the dialogue.  Every earlier probe was driving
//     blind past a prompt it could have simply read.
//
//  3. The STRAIGHT_TO_RACE-style bypass — poke $5F3B=$FF at $63F7 and jump to $6407 — looked
//     like the way past the front end.  It "fired" (bypassed=1) and still never reached a
//     race, because $6407 is `JSR $655A` and when $655A RETURNS execution falls into $640A,
//     which is the COMPETITION chain: class, qualifying duration, then the driver-name line
//     editor.  That is where all those OSRDCH hits came from.  The bypass did not skip the
//     front end, it skipped the ANSWER and landed in the longer branch.
//
// ── How it drives the menus now ───────────────────────────────────────────────────────────
//
// menu_wait_key ($6571) is the only menu primitive.  With X = option count it polls
// key_binding_tbl ($39E0) from X down to 0 via kbd_test_key, where entry 0 is the CONFIRM key
// and 1..X are the options; a hit on 1..X records the choice in $78 and prints message $1E
// ("PRESS SPACE BAR TO CONTINUE"), and a hit on 0 with a choice recorded returns X = $78-1.
//
// So instead of pulsing guessed keys on a timer, this reads the key the engine is ACTUALLY
// polling out of $39E0 in live memory and presses that, holding until the engine's own state
// says it registered ($77 becomes non-zero), then confirms.  ⭐ The mapping is exact: a BBC
// negative-INKEY byte b is internal key number 255-b, and the internal key number is
// (row<<4)|col on the very matrix jsbeeb's keyDownRaw() takes —
//   $9D -> 98 -> [col 2, row 6] = SPACE,  $CF -> 48 -> [col 0, row 3] = '1'.
// Verified against jsbeeb's own utils.BBC table for SPACE/1/2/3 at startup, so a table change
// or a bad derivation fails loudly here instead of becoming a silent no-op.
//
// PRACTICE is one answer: option 1 at the $63F7 menu (message $27) stores $5F3B=$FF and calls
// the session driver $655A, which runs frames until $05F4 says stop.  Nothing is poked and no
// game code is skipped.

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import { Video } from "./jsbeeb/src/video.js";
import * as utils from "./jsbeeb/src/utils.js";
import { CaptureSoundChip, attachSoundCapture, hookOswordSound, writeFixture } from "./bbc_sound_capture.mjs";
import fs from "fs";
import path from "path";

// ── addresses (runtime image; see disasm/symbols.csv) ─────────────────────────────────────
const PRINT_MSG = 0x4d7e; // print_message, X = message index
const MENU_WAIT = 0x6571; // menu_wait_key, X = option count
const MENU_POLL = 0x6581; //   its kbd_test_key call
const MENU_DONE = 0x659c; //   the DEX before its RTS — X+1 is the chosen option
const SESSION = 0x655a; // the session driver: frames until $05F4
// ⚠ $655A is entered ONCE, not once per frame: $16DC never returns until the session ends, and
// the per-frame back-edge is inside it ($1768 `BMI $16EE` on the practice flag).  $1701 sits
// after the frame's setup and BEFORE the road rasteriser ($1A20 at $171F), so the frame buffer
// there holds the previous frame COMPLETE — which is what a zero-byte count needs.  Sampling
// at $6560 instead produced a confident "no samples" and would have read as "no stripes".
const FRAME = 0x1701;
const KEY_TBL = 0x39e0; // key_binding_tbl: [confirm, opt1, opt2, ...] as negative INKEY
const SEL_FLAG = 0x77; //   menu_wait_key: 0 until a choice is recorded
const SEL_IDX = 0x78; //   menu_wait_key: the recorded choice
const MSG_SPACEBAR = 0x1e; // "PRESS SPACE BAR TO CONTINUE"
// ⭐ The prompt that blocked every previous run.  The session driver's own preamble ($3C50)
// asks for the REAR then FRONT wing setting, 0-40, through the two-character line editor
// console_io ($6300) with its buffer at $0074, and validates with $32D0 (carry clear = in
// range).  Nothing gets to a race until both are answered, and because the front end draws in
// MODE 4/5 nobody had ever seen it being asked.
const NUM_READ = 0x3ee0; // read+validate a number into $0074
const NUM_VALID = 0x32d0; //   the validator, reached only once a line was ENTERED
const NUM_REJECT = 0x3eee; //   the delete-and-ask-again arm, reached only on out-of-range
const RDCH_SITE = 0x6316; //   the OSRDCH inside console_io
const NUM_BUF = 0x0074; //   its two-character buffer
// ⚠ The per-keystroke acknowledgement has to be console_io's own STA ($70),Y — NOT "the buffer
// byte now holds the character I sent".  The buffer is reused between the rear and front wing
// prompts, so the byte a keystroke was supposed to produce is often ALREADY there from the
// previous answer: the check passes without the key ever being pressed, the line is entered one
// character short, and "20" silently becomes 2.  That is a passing test measuring nothing.
const CHAR_STORE = 0x633c;
const KBD_TEST = 0x0e50; // kbd_test_key: X = negative INKEY code, Z set on return if held
const STARTER = 0x4978; // the 'T' starter poll
const ENGINE_ON = 0x61; //   set to $FF once the engine catches

const CPS = 2 * 1000 * 1000; // BBC cycles per second

const argv = process.argv.slice(2);
const opt = (name, dflt) => {
    const hit = argv.find((a) => a.startsWith(`--${name}=`));
    return hit === undefined ? dflt : hit.slice(name.length + 3);
};
const wantFrames = Number(opt("frames", 200));
const track = Number(opt("track", 5)); // 5 = Silverstone
const dumpDir = opt("dump", null);
// ⚠ --park is a STOPPING POINT INSIDE the drive-in sequence, not an alternative to it: the
// starter and the gear change live under `if (drive)` below, so `--park` on its own once
// captured a car that was still in the pits with the engine OFF — a dump that compares fine
// against a port in the same state and proves nothing about a running engine.  It implies
// --drive for that reason.  (`park` is declared further down; hoisted here.)
// ⭐ --mem-at=NNNN : dump the whole 64 KB the moment the CPU first reaches that PC, once the
// drive-in has settled (frame >= --mem-at-frame, default 40).  The frame-boundary dump below is
// taken with the road pass long finished, so every edge_* / surface_edge_* cell in it is
// end-of-frame SCRATCH — three of those cells sent this hunt after the object plotter before a
// control showed they are rewritten after draw_road.  Sampling at draw_road's own entry ($1A20)
// instead compares build_track_geometry's real OUTPUT.  The port's counterpart is
// REVS_MEM_DUMP_AT (src/platform/... ), and the two must name the same PC.
// ⭐ --watch=NNNN : every WRITE to one address, attributed to the PC that made it, over the
// settled frames.  "Which routine puts that value there" is the question a memory diff always
// raises and can never answer, and reasoning about it from the listing is exactly the
// nearly-right scan this project has paid for before.  `--watch-frames=a-b` moves the window.
// ⭐ `--watch=lo-hi` widens it to a RANGE, which is what an ARRAY's tenancy question needs: a
// single-address watch on $0100 says only what happened to car 0, and "no car was ever spun out"
// is a claim about all twenty slots.
const watchArg = opt("watch", null);
const watchAddr = watchArg === null || watchArg.includes("-") ? null : parseInt(watchArg, 16);
const watchLo = watchArg !== null && watchArg.includes("-")
    ? parseInt(watchArg.split("-")[0], 16) : null;
const watchHi = watchLo === null ? null : parseInt(watchArg.split("-")[1], 16);
const watchPCs = new Map();
// ⭐ --trace-edge : one line per EDGE POINT the road walk emits, for ONE settled frame — the
// slot it lands in, the section byte it came from, and the bearing/heading the store is made
// from.  A memory diff can say WHICH edge cell differs; only a per-point trace can say whether
// the port's walk visited the same points in the same order, which is the question every
// road-pass divergence turns into.  Matched on the port side by REVS_TRACE_EDGE.
const traceEdge = argv.includes("--trace-edge");
const traceEdgeLines = [];
const memAtArg = opt("mem-at", null);
const memAt = memAtArg === null ? null : parseInt(memAtArg, 16);
const memAtFrame = Number(opt("mem-at-frame", 40));
let memAtDone = false;
let memAtSnapshot = null;
let memAtFrames = 0;
const peekArg = opt("peek", null);
const peekAddrs = peekArg === null ? null : peekArg.split(",").map((h) => parseInt(h, 16));
const peekHist = new Map();
const holdSteer = opt("hold-steer", null); // "left" | "right": see the drive-in sequence below
if (holdSteer !== null && holdSteer !== "left" && holdSteer !== "right")
    throw new Error(`--hold-steer must be left or right, got ${holdSteer}`);
const park = argv.includes("--park");
// ⭐ --press=<code>[+<code>...]@<sec>[:<hold>] : hold negative-INKEY codes DOWN during the race,
// `sec` seconds into the racing loop, for `hold` seconds (default 1).  The only way to settle what
// a COMMAND key actually does on real hardware — the manual (REVINST) and shift_key_tbl ($3DE2)
// disagree about which key quits, and neither is evidence.  Codes are hex negative-INKEY bytes,
// e.g. `--press=ff+e9@4` = SHIFT + f7.  state_flags ($05F4) is sampled every second either way.
const pressArg = opt("press", null);
let press = null;
if (pressArg !== null) {
    const [codeSpec, when] = pressArg.split("@");
    const [at, hold] = (when || "4").split(":");
    press = {
        codes: codeSpec.split("+").map((h) => parseInt(h, 16)),
        at: Number(at),
        hold: Number(hold || 1),
        down: false,
        released: false,
    };
    if (press.codes.some((c) => !(c >= 0 && c <= 255)) || !(press.at >= 0))
        throw new Error(`--press: cannot parse ${pressArg}`);
}
const drive = argv.includes("--drive") || park;
const wing = String(opt("wing", "20")); // rear and front wing, 0-40 (the game has no default)
// --fill=lo-hi : attribute every frame-buffer write in those DISPLAY LINES to the routine
// that made it.  This is how you find the fill loop the port is failing to run: the real BBC
// writes these bytes, so whoever writes them is the code to compare against.
// ⭐ `--fill=all` is the whole picture (display lines 0..207), which turns this from "who fills
// the horizon" into THE STORE CENSUS the Phase 6 layout choice needs: every frame-buffer write a
// real BBC makes, attributed to the routine that made it.
// `--fill-frames=a-b` moves the sampling window off its old hard-coded 9..23.
const fillArg = opt("fill", null);
const fillFramesArg = opt("fill-frames", "9-23");
// --irq-abi : measure, on real hardware, WHICH REGISTERS SURVIVE AN INTERRUPT.
// The port's ISR shim has to reproduce this contract exactly.  It already got A wrong once
// (irq1v_band_schedule restores A from $FC, which only the MOS's entry ever writes), and the way to
// find the rest is to measure the contract rather than read the handler and reason about it.
const irqAbi = argv.includes("--irq-abi");
// --charset : measure THE RACE VIEW'S BITMAP TEXT — which character codes the engine asks the
// MOS for through OSWORD 10, and where each glyph lands.  `vdu_char_def` ($5092) has two arms
// and this is the OTHER one from MODE 7's: $5096 stores the code in the OSWORD block at $62C3,
// calls OSWORD 10 at $50A7, and plots the eight returned bytes into the frame buffer.  The port
// has no MOS ROM and will not lift Acorn's font, so those 96 glyphs have to be DRAWN — and the
// first thing to know is which of them are ever asked for.
//
// ⚠ THIS PROBE DELIBERATELY DOES NOT RECORD THE RETURNED BITMAPS, only the codes and the
// positions — the same line tools/bbc_probe_mode7.mjs holds.  A "tmp-only" capture of the MOS
// font would still be the ROM, and a fixture has a way of becoming a source.
const charset = argv.includes("--charset");
// --competition : take the COMPETITION branch instead of PRACTICE, so the session has a FIELD
// of 19 other cars in it.  Practice runs the player alone, so it is structurally incapable of
// showing whether competitor-car rendering works — a clean practice frame is not evidence.
const competition = argv.includes("--competition");
// --park : start the engine, engage first gear, and then TOUCH NOTHING — no throttle, no
// steering — before rendering the frames and dumping.  ⭐ This exists so a dump can be compared
// with the port's, because `src/platform/autorun.cpp` parks in exactly this state.
// ⭐⭐ AND THE STATE IS A STALLED ENGINE, not an idling one — measured here, 2026-09-06, on both
// machines: first gear with no throttle drops the revs below 3 within a second and $4A43 INCs
// $61 from $FF back to 0.  So the settled parked row is $61=$00, $3C=$00, $40=2, $63=0.  (An
// earlier note here quoted $61=$FF / $3C=$28, which is the row printed one second EARLIER —
// "engine on, first gear, no throttle", before the stall.)  The port reproduces the stall
// exactly, which is what makes the parked frame comparable at all.  ⚠ Plain --drive holds the throttle and
// steers, so its dump is taken at speed on a moving car: comparing it against a parked target is
// comparing two different scenes, which is the trap this project has paid for more than once.
// The dashboard is the reason it matters — a needle is a function of $3C, so the two machines
// have to be at the same $3C before a pixel diff of the dial means anything.
// (park is declared beside `drive` above — --park implies --drive.)
// --via-t2 : measure WHAT THE GAME ACTUALLY READS FROM $FE68, on real hardware.  The port
// answered a constant $0 there, and $FE68 is Revs's ONLY entropy source (the starter's catch
// delay, the idle-rev jitter, the gravel/skid trigger, the mirrors' shudder), so "what is the
// right answer" is a question about a real 6522, not about the game.
// ⚠ It samples the value the GAME got — a breakpoint one instruction after each `LDA/LDX $FE68`,
// reading A/X — rather than calling readmem($FE68) itself: reading T2C-L on a 6522 CLEARS the T2
// interrupt flag, so a probe that reads the register is perturbing the machine it is measuring.
const viaT2 = argv.includes("--via-t2");

// ── negative INKEY -> jsbeeb keyboard matrix ──────────────────────────────────────────────
function inkeyToColRow(b) {
    const internal = 255 - b; // b is the two's-complement low byte of a negative INKEY
    return [internal & 0x0f, internal >> 4];
}
// Prove the derivation against jsbeeb's own table rather than trusting it.
for (const [inkey, name] of [
    [0x9d, "SPACE"],
    [0xcf, "K1"],
    [0xce, "K2"],
    [0xee, "K3"],
]) {
    const got = inkeyToColRow(inkey),
        want = utils.BBC[name];
    if (got[0] !== want[0] || got[1] !== want[1])
        throw new Error(`INKEY->matrix derivation broken: $${inkey.toString(16)} -> ${got}, jsbeeb ${name} = ${want}`);
}

// ── ⭐⭐ THE THING THAT WAS ACTUALLY BROKEN: no vertical sync ─────────────────────────────
//
// `TestMachine` defaults to jsbeeb's `FakeVideo`, whose `polltime()` is empty and which never
// calls `sysvia.setVBlankInt()`.  So the emulated machine has NO VERTICAL SYNC.  The MOS does
// not care — it runs on the System VIA's 100 Hz timer — which is why booting, REVINST, the
// track menu and the whole of Revs' front end always worked, and why this was invisible for so
// long.  But the engine's display setup spins on `LDA #2 / BIT $FE4D / BEQ` at $4E11, System
// VIA IFR bit 1 = CA1 = vsync, and that bit never arrives.  Every "the reference loop cannot
// reach a race" run ended in that two-instruction loop.
//
// It was never a key-injection problem.  Fitting a REAL `Video` fixes it — and pays twice,
// because a real Video also renders actual RGB pixels, which is the ground truth the port's
// output can be compared against instead of just the frame-buffer bytes.
// Absolute 6502 cycles.  jsbeeb keeps a wrapping `currentCycles` plus a `cycleSeconds` count,
// and the wrap unit is the MODEL's clock — never hardcode 2 MHz, ask the model (jsbeeb's own
// comment on `cyclesPerSecond` says the same).
// ⚠ `tm` is a `const` declared below and the Video CONSTRUCTOR paints once, so the callback can
// fire while that binding is still in its temporal dead zone — where even `typeof` throws.  Hence
// the explicit flag rather than a guard on `tm` itself.
let machineReady = false;
const cpuCycles = () =>
    tm.processor.cycleSeconds * tm.processor.model.cyclesPerSecond + tm.processor.currentCycles;

const FB_W = 1024, FB_H = 625;
const fb8 = new Uint8Array(FB_W * FB_H * 4);
const fb32 = new Uint32Array(fb8.buffer);
const completeFb8 = new Uint8Array(FB_W * FB_H * 4);
let paints = 0;
// ⚠ THE FRAME-COST INSTRUMENT'S CALIBRATION, against a KNOWN QUANTITY (docs/method-lessons.md):
// this callback fires once per displayed PAL field, i.e. every 312*64 us = 39 936 cycles at
// 2 MHz.  If the median gap here is not ~39 936, the cycle accessor is being read wrong and the
// frame figure beside it is worthless.
const paintCycles = [];
const video = new Video(false, fb32, function () {
    paints++;
    if (machineReady && paintCycles.length < 4000) paintCycles.push(cpuCycles());
    completeFb8.set(fb8); // snapshot at paint time: always a whole frame, never mid-render
});

// ── --sound: what the ENGINE asks the MOS for, and what the MOS writes to the chip ────────
// Revs never addresses the SN76489: every note is an OSWORD 7 block (`sound_queue` $0B4A) and
// one OSWORD 8 envelope (`sound_envelope` $0B65), so the port has to reproduce the MOS's
// scheduler.  Both halves have to come off a real machine WITH THE ENGINE RUNNING — the sound is
// the rev count, so a parked car measures silence.  Use --drive with this.
const soundOut = opt("sound", null);
const soundChip = soundOut ? new CaptureSoundChip() : undefined;
const soundCmds = [];

const data = fs.readFileSync(new URL("../revs.ssd", import.meta.url));
const tm = new TestMachine("B-DFS1.2", soundChip ? { video, soundChip } : { video });
machineReady = true;
await tm.initialise();
if (soundChip) {
    attachSoundCapture(tm, soundChip);
    soundChip.tag = () => ({ frame: frames });
    hookOswordSound(tm, soundChip, soundCmds);
}
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();
const rd = (a) => tm.processor.readmem(a);
/* ⭐ The harness's OWN frame-buffer walks go through peekmem, which fires no debug hook.
   With readmem they were charged to whatever instruction the CPU last executed — 1376
   phantom 'reads per frame' at $1701, stepping by 8 like a cell scan, because that is
   exactly what they were.  An instrument must not appear in its own measurement. */
const fbPeek = (a) => tm.processor.peekmem(a);

// ── decode a front-end message out of LIVE memory ─────────────────────────────────────────
// Bytes: $FF ends; >=$C8 is a nested message ($C8+$36 is a special "clear" action, not text);
// $A0..$C7 is (n-$A0) spaces; $1F is a two-byte TAB(x,y); the rest are literal characters.
function decodeMsg(x, depth = 0) {
    if (depth > 6) return "";
    let p = rd(0x3ad0 + x) | (rd(0x3b50 + x) << 8);
    let out = "";
    for (let i = 0; i < 512; i++) {
        const b = rd(p + i);
        if (b === 0xff) break;
        if (b >= 0xc8) {
            const t = b - 0xc8;
            if (t !== 0x36) out += decodeMsg(t, depth + 1);
        } else if (b >= 0xa0) out += " ".repeat(b - 0xa0);
        else if (b === 0x1f) {
            i += 2; // TAB(x,y)
            out += "  ";
        } else if (b === 13) out += " / ";
        else if (b >= 0x20 && b < 0x7f) out += String.fromCharCode(b);
    }
    return out.replace(/\s+/g, " ").trim();
}

// ── the sky/ground zero-byte scan ─────────────────────────────────────────────────────────
// The port shows horizontal BLACK runs just above the horizon.  Host and Amiga backends show
// them identically, so they are not a display-path bug: the game's frame buffer genuinely has
// ZERO bytes there, and pen 0 under band 2's palette is black.  Only a real BBC can say
// whether the real game leaves those bytes zero too.
//
// ⚠⚠ THE WINDOW MUST BE BAND 2 ALONE — lines 81..100 — and NOT the 80..99 used before.
// Getting this off by one line inverts the answer.  Per src/platform/bbc_screen.h:
//   band 1 (lines 18..81) is the sky, and ALL SIXTEEN of its palette entries are the same
//     blue, so a zero byte there is invisible — it is also where 5.5 KB of live engine code
//     and variables ($5E40-$66FF) sit on display, so it is full of zeros by construction.
//   band 2 (lines 81..100) is the horizon, palette $3458, where pen 0 IS BLACK.  This is the
//     only window in which a zero byte becomes a black stripe.
//   band 3 (lines 100..166) is the track, also pen-0 black — but there the black is the ROAD,
//     which legitimately widens toward the viewer.
// Scanning 80..99 straddles all of this: line 80 belongs to the invisible sky band and by
// itself contributes a 16-cell run of zeros, which reads as "the real BBC has stripes too".
const FB_BASE = 0x5a80, CELLS = 40, LINES = 8, BPR = CELLS * LINES, ROWS = 26;
function scanZeros(lo, hi) {
    let zeros = 0, total = 0, longest = 0;
    for (let y = lo; y < hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        let run = 0;
        for (let c = 0; c < CELLS; c++) {
            const b = fbPeek(FB_BASE + row * BPR + c * LINES + line);
            total++;
            if (b === 0) { zeros++; if (++run > longest) longest = run; } else run = 0;
        }
    }
    return { zeros, total, longest };
}
function checksum(lo, hi) {
    let h = 2166136261;
    for (let y = lo; y < hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        for (let c = 0; c < CELLS; c++)
            h = ((h ^ fbPeek(FB_BASE + row * BPR + c * LINES + line)) * 16777619) >>> 0;
    }
    return h;
}
const skySamples = [];

// ── observation ───────────────────────────────────────────────────────────────────────────
let menuEntries = 0,
    menuAnswers = 0,
    menuPolls = 0,
    menuOptions = 0;
let sessionEntries = 0,
    frames = 0,
    spacebarPrompts = 0;
const frameCycles = [];

/* ── ⭐⭐⭐ `--profile` : WHERE THE REAL BBC'S 97 ms ACTUALLY GOES, PER ROUTINE ──────────────
   The frame cost above has been the campaign's yardstick for days, but every "the port is N x
   the original" claim divides one of OUR per-routine numbers by a GUESSED share of it.  This
   measures the share.

   ⭐ Bracketed BY STACK POINTER, which makes it exact and nest-safe without a return-address
   table: at the entry PC the JSR's return address is already pushed, so the routine has
   returned exactly when S has risen back past its entry value.  The cost is therefore the
   SUBTREE — the same thing an Amiga phase bracket measures, which is what makes the two
   columns comparable at all.
   ⚠ An interrupt taken inside the routine pushes 3 bytes (S falls) so it cannot close the
   bracket early, and its time lands in the subtree — again exactly as the Amiga side charges
   the VERTB ISR to whatever phase it preempted.  Symmetric, and stated rather than hidden.
   ⚠ Reported as a MEDIAN over settled frames for the same reason the frame cost is: the
   drive-in and the engine's crash holds are a different workload. */
/* ⭐⭐ MEASURED (2026-09-23, Silverstone practice, driving): the site MEANS sum to 96.4 of the
   frame's 97.0 ms, so this table IS the BBC frame — read docs/open-work.md §THE PER-PHASE
   COMPARISON for it lined up against the port.  ⚠ Compare the MEAN column: the median read the
   three engine_sound_update calls as 0.0 ms where the mean is 1.1, because they work on fewer
   than half their frames.
   The main loop's 24 call SITES, $1701..$1748 — one per Amiga phase id, in order.  Bracketed at
   the SITE rather than the routine because engine_sound_update is called three times (phases
   9/12/20) and a routine-level bracket would merge them.  Opens on the JSR itself (S = s0) and
   closes on the instruction after it with S back at s0 — exact, and nest-proof by construction. */
const PROFILE_SITES = [
    [0x1701, 1, "tick_race_timers"],     [0x1704, 2, "draw_starting_lights"],
    [0x1707, 3, "read_driving_controls"], [0x170a, 4, "apply_driving_model"],
    [0x170d, 5, "build_track_geometry"], [0x1710, 6, "place_player_in_section"],
    [0x1713, 7, "advance_player_section"], [0x1716, 8, "update_lap_timers"],
    [0x1719, 9, "engine_sound_update"],  [0x171c, 10, "clear_surface_buffers"],
    [0x171f, 11, "draw_road"],           [0x1722, 12, "engine_sound_update"],
    [0x1725, 13, "fill_line_surface"],   [0x1728, 14, "build_road_sign"],
    [0x172d, 15, "draw_track_object"],   [0x1730, 16, "draw_corner_markers"],
    [0x1733, 17, "move_and_draw_cars"],  [0x1736, 18, "fill_dash_edge_columns"],
    [0x1739, 19, "mirrors_update"],      [0x173c, 20, "engine_sound_update"],
    [0x173f, 21, "update_horizon_band"], [0x1742, 22, "process_car_contact"],
    [0x1745, 23, "check_crash"],         [0x1748, 24, "view_paint_lines"],
    /* ...and the LOOP TAIL the port brackets as phase 32 ($174B-$17B7): the three calls on the
       usual ($62F6 == 0) path back to $1701.  Numbered 32 so the rows line up with the port's. */
    [0x1791, 32, "tail: shift_key_commands"],         [0x17b1, 32, "tail: engine_sound_update"],
    [0x17b4, 32, "tail: draw_dash_needles"],
];
/* ...and the practice-session DELAY PAD inside move_and_draw_cars ($262D-$2636, 1536 decrements
   of math_lo), which the port deliberately does not reproduce (src/gen/revs_native.c twin #179).
   A PC-RANGE bracket, because it is a branch target and not a call. */
const PAD_LO = 0x262d, PAD_HI = 0x2636;
const profileOn = argv.includes("--profile");
/* pc -> {name, note, open:[{sp,t0}], frameCycles:<cycles this frame>, per:[] , calls:[] } */
const profState = new Map();
const siteAt = new Map();   /* site pc -> state */
if (profileOn) {
    for (const [pc, ph, name] of PROFILE_SITES) {
        const st = { name, ph, pc, s0: -1, t0: 0, cur: 0, curCalls: 0, per: [], calls: [] };
        profState.set(pc, st); siteAt.set(pc, st);
    }
    profState.set(-1, { name: "  (the practice DELAY PAD)", ph: 0, pc: PAD_LO, s0: -1, t0: 0,
                        cur: 0, curCalls: 0, per: [], calls: [] });
}
const padSt = () => profState.get(-1);
let numAsks = 0,
    numValidations = 0,
    numRejects = 0,
    rdchHits = 0,
    charsStored = 0,
    starterPolls = 0;
const keyPolls = new Map();
const transcript = [];
const answered = [];
const RING = 128;
const ring = new Array(RING).fill(0);
let ringAt = 0,
    engineInsns = 0;

tm.processor.debugInstruction.add((addr) => {
    if (profileOn) {
        const now = cpuCycles(), S = tm.processor.s;
        /* close a site on the instruction after its JSR, with the stack back where it was */
        const back = siteAt.get(addr - 3);
        if (back && back.s0 >= 0 && S >= back.s0) { back.cur += now - back.t0; back.s0 = -1; }
        const st = siteAt.get(addr);
        if (st && st.s0 < 0) { st.s0 = S; st.t0 = now; st.curCalls++; }
        const pad = padSt(), inPad = addr >= PAD_LO && addr <= PAD_HI;
        if (inPad && pad.s0 < 0) { pad.s0 = 1; pad.t0 = now; pad.curCalls++; }
        else if (!inPad && pad.s0 >= 0) { pad.cur += now - pad.t0; pad.s0 = -1; }
    }
    if (traceEdge && addr === 0x23c0 && frames === memAtFrame) {
        const pk = (a) => tm.processor.peekmem(a);
        const w = (a) => pk(a) | (pk(a + 1) << 8);
        traceEdgeLines.push(`  slot $${tm.processor.y.toString(16).padStart(2, "0")}` +
            ` section $${tm.processor.x.toString(16).padStart(2, "0")}` +
            ` count $${pk(0x42).toString(16).padStart(2, "0")}` +
            ` bearing $${w(0x8a).toString(16).padStart(4, "0")}` +
            ` heading $${w(0x0a).toString(16).padStart(4, "0")}` +
            ` -> $${((w(0x8a) - w(0x0a)) & 0xffff).toString(16).padStart(4, "0")}`);
    }
    if (memAt !== null && !memAtDone && addr === memAt && frames >= memAtFrame) {
        memAtDone = true;
        memAtSnapshot = Buffer.alloc(0x10000);
        for (let i = 0; i < 0x10000; i++) memAtSnapshot[i] = tm.processor.peekmem(i);
        memAtFrames = frames;
    }
    switch (addr) {
        case PRINT_MSG: {
            const x = tm.processor.x;
            if (x === MSG_SPACEBAR) spacebarPrompts++;
            const text = decodeMsg(x);
            if (text) transcript.push({ msg: x, text });
            break;
        }
        case MENU_WAIT:
            menuEntries++;
            menuOptions = tm.processor.x;
            break;
        case MENU_POLL:
            menuPolls++;
            break;
        case MENU_DONE:
            menuAnswers++;
            answered.push(tm.processor.x); // pre-DEX: chosen option, 1-based
            break;
        case SESSION:
            sessionEntries++;
            break;
        case NUM_READ:
            numAsks++;
            break;
        case NUM_VALID:
            numValidations++;
            break;
        case NUM_REJECT:
            numRejects++;
            break;
        case RDCH_SITE:
            rdchHits++;
            break;
        case CHAR_STORE:
            charsStored++;
            break;
        case KBD_TEST:
            // Which keys does the engine poll while racing, and is the one we are holding
            // among them?  Holding a key that is never polled looks exactly like holding a
            // key that is polled and ignored.
            if (frames > 0) {
                const x = tm.processor.x;
                keyPolls.set(x, (keyPolls.get(x) || 0) + 1);
            }
            break;
        case STARTER:
            starterPolls++;
            break;
        case FRAME:
            frames++;
            // ⭐⭐⭐ THE PORT'S ONLY HONEST TARGET: what does the REAL BBC spend on one painted
            // frame of the same scene?  $1701 is the engine's own per-frame back-edge, so the
            // gap between two hits is one whole game frame in 2 MHz cycles — DMA, interrupts,
            // the 50 Hz body and all.  Everything else this project measures is the port's
            // cost with nothing to compare it to.
            frameCycles.push(cpuCycles());
            if (profileOn)
                for (const st of profState.values()) {
                    st.per.push(st.cur); st.calls.push(st.curCalls);
                    st.cur = 0; st.curCalls = 0;
                }
            // ⭐ --peek=a,b,... : the TUPLE of those cells sampled once a frame, histogrammed.
            // --watch answers "who wrote it"; this answers "did these two ever hold X at the same
            // time", which no per-address watch can (surface_change_0 AND _1 both $FF is what
            // selects grip_limit_base_alt_tbl, and each alone proves nothing about the pair).
            if (peekAddrs && frames >= memAtFrame) {
                const key = peekAddrs.map((a) => rd(a).toString(16).padStart(2, "0")).join(" ");
                peekHist.set(key, (peekHist.get(key) || 0) + 1);
            }
            // Every 4th frame, because the sky/track split moves with the hills
            // (update_horizon_band, $4F44) — one frame proves nothing either way.
            if (frames > 8 && frames % 4 === 0 && skySamples.length < 80)
                skySamples.push({
                    frame: frames,
                    sky: scanZeros(81, 101), // band 2: the horizon, pen 0 = black
                    gnd: scanZeros(101, 166), // band 3: the track
                    skyBand: scanZeros(24, 81), // band 1: invisible, for contrast
                    // ⚠ A checksum of the visible picture, so "every frame reports the same
                    // count" can be told apart from "the picture never changed".  Identical
                    // counts across 50 frames is exactly what a PARKED car looks like, and a
                    // parked car is ground truth for a scene that never moves.
                    sum: checksum(80, 166),
                });
            break;
    }
    // ⚠ Keep a ring of the last engine addresses executed.  When the engine stops running
    // altogether — which is what "0 frames" turned out to mean — the ring still holds the
    // instructions it left through, and that is the only cheap way to see the exit.
    if (addr < 0x7fff) {
        ring[ringAt++ & (RING - 1)] = addr;
        engineInsns++;
    }
    return false;
});

// ── the display-composition analyser: the REAL band schedule ──────────────────────────────
// Colour in Revs is a function of raster position: one static screen mode, five palette/mode
// bands per field written by irq1v_band_schedule ($4E5C) and timed by the User VIA T1 latch.
// src/platform/bbc_screen.h DERIVES that schedule from the binary; this MEASURES it on the
// real thing, which is the missing half.
//
// ⭐ It matters for the stripes specifically because band 2's boundary MOVES EVERY FRAME —
// update_horizon_band ($4F44) shifts it with the hills — so "lines 81..100" is only ever an
// approximation of the horizon band, and a zero-byte count over a fixed window silently mixes
// in lines that belong to the flat-blue sky band (where a zero is invisible) or to the track
// band (where black is the road).  Comparing the port's stripes against the real machine needs
// the real boundaries, not a nominal table.
// ── fill attribution: WHO writes these pixels on a real BBC ───────────────────────────────
// The port leaves black runs at display lines 83/88/93/98 (5-line period, widths growing
// downward) that the real machine fills.  Rather than read the disassembly hoping to spot the
// loop, ask the running machine: flag every frame-buffer byte belonging to those lines and
// record the PC of whatever writes one.  The answer is a routine, not a guess.
const fillFlags = new Uint8Array(0x10000);
const fillLineOf = new Int16Array(0x10000).fill(-1);
/* ⭐ …AND ITS CELL.  A rectangle painter needs the CELL bounds as much as the line bounds — a
   writer confined to four cells of a row is a 44-byte re-expand where the row is 40 cells, and
   the line-only roll-up below cannot tell those apart.  (docs/span-render-plan.md §12c.) */
const fillCellOf = new Int16Array(0x10000).fill(-1);
const fillPC = new Map(); // pc -> {n, lines:Set}
let fillWrites = 0, fillChanged = 0;
const [fillFrameLo, fillFrameHi] = fillFramesArg.split("-").map(Number);
if (fillArg) {
    const [lo, hi] = fillArg === "all" ? [0, LINES * ROWS - 1] : fillArg.split("-").map(Number);
    for (let y = lo; y <= hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        for (let c = 0; c < CELLS; c++) {
            const a = FB_BASE + row * BPR + c * LINES + line;
            fillFlags[a] = 1;
            fillLineOf[a] = y;
            fillCellOf[a] = c;
        }
    }
    console.log(`fill attribution armed for display lines ${lo}..${hi}\n`);
}

// ⭐⭐ …AND WHO READS THEM BACK.  `--fill-reads` arms the same flags on the READ side, and it is
// the fact a direct-to-bitplane plotter turns on: if a region of the BBC frame buffer is
// WRITE-ONLY, the port can stop maintaining it in mem[] and plot straight into bitplanes; if
// anything reads it back, that byte is state and the mem[] copy has to stay.  Reasoning about it
// from the disassembly is exactly the assumption this project's rules say to measure instead.
// ⚠ Opcode fetches come through readmem too, so read this only for line ranges that hold no code
// (the engine's own code inside the frame buffer is display lines 24..55 — $5E40..$66FF).
const fillReads = argv.includes("--fill-reads");
const readPC = new Map();
let fbReads = 0;
if (fillReads) {
    tm.processor.debugRead.add((addr) => {
        if (frames < fillFrameLo || frames > fillFrameHi || !fillFlags[addr]) return;
        const pc = tm.processor.getPrevPc(0);   // see the write hook: NOT processor.pc
        let e = readPC.get(pc);
        if (!e) readPC.set(pc, (e = { n: 0, lines: new Set(), cells: new Set(), sample: [] }));
        e.n++;
        if (e.sample.length < 6) e.sample.push(addr);
        e.lines.add(fillLineOf[addr]);
        e.cells.add(fillCellOf[addr]);
        fbReads++;
    });
}

// ── ⭐⭐⭐ THE SOURCE-BLOCK READER AUDIT (`--src-audit`) ───────────────────────────────────
// The RESULTS rule (docs/validation-harness.md) lets the port stop reproducing a mem[] byte that
// nothing outside the twin reads — but only behind a WRITTEN reader audit, and the readers include
// arms no Silverstone run reaches.  So ask the authentic engine: flag every byte of the forty
// $80-spaced view source blocks and record the PC of everything that reads or writes one.
//
// ⚠⚠ THE LIVE SPAN IS NOT THE WHOLE BLOCK, and getting that wrong would put twelve innocent
// tables in the answer.  Per disasm/symbols.csv §the $3080 question: each block's live source span
// is offsets dash_block_starts[col]..$4F; offsets BELOW the start are dead, which is exactly why
// the game packs view_run_right_end, dial_needle_dda_tbl, dash_block_starts itself and nine more
// tables into them, and offsets $50..$7F are the block TAILS (the dashData code copy_dash_data
// moves to $7B00).  `--src-audit` arms only the live span; `--src-audit=full` arms all 3200 bytes,
// which is how you SEE the tables show up and confirm the span is right rather than assuming it.
//
// ⚠ Opcode fetches come through readmem too.  The live span holds no code — the dash code is in
// the tails — so `--src-audit` is clean, and `=full` is the arm where a fetch could appear.
/* ⭐⭐ `--range-audit=LO-HI` (hex) — the SAME read/write PC attribution over an ARBITRARY address
   range, because a RESULTS-rule reader audit is not a one-off: every step of the producer rewrite
   (docs/open-work.md entry 3) moves a set of intermediate mem[] cells into registers and owes the
   same question — "does anything outside this pass read these bytes?".  `--src-audit` is the view
   source blocks' special case (it has to compute the live span from dash_block_starts); this is
   the general form and needs no knowledge of what the bytes mean.
   ⚠ Opcode fetches come through readmem, so a range holding CODE will report the CPU as its own
   reader.  Zero page and the engine's scratch are safe; $5E40..$66FF and $7B00..$7FFF are not. */
const rangeAuditArg = opt("range-audit", null);
const srcAuditArg = opt("src-audit", null);
const srcFlags = new Uint8Array(0x10000);
const srcCellOf = new Int16Array(0x10000).fill(-1);
const srcLineOf = new Int16Array(0x10000).fill(-1);
const srcReadPC = new Map(), srcWritePC = new Map();
let srcReads = 0, srcWrites = 0, srcArmed = 0;
const SRC_BASE = 0x3000, SRC_CELLS = 40, SRC_STRIDE = 0x80, SRC_LINES = 0x50;

/* ⚠⚠ ARMED LAZILY, ON THE FIRST ACCESS INSIDE THE WINDOW, and that is not a style choice — it is
   the fix for a measured failure.  The flags depend on dash_block_starts, which has to be read out
   of the RUNNING machine (an expansion circuit's hook may patch it), so the first version armed
   after the race was reached: `frames` was already 44 and the 20..40 window had closed, giving a
   confident 0 reads / 0 writes.  Registering the hooks at module scope and building the flags on
   first use inside the window gets both: the real machine's table, and the whole window.
   ⭐ The "NOTHING touched the armed bytes" line in the report is what caught it — an instrument
   that cannot report its own silence is indistinguishable from a finding. */
function rangeArm() {
    const [lo, hi] = rangeAuditArg.split("-").map((v) => parseInt(v, 16));
    for (let a = lo; a <= hi; a++) { srcFlags[a] = 1; srcCellOf[a] = -1; srcLineOf[a] = a - lo; srcArmed++; }
    console.log(`\nrange audit armed at frame ${frames}: $${lo.toString(16)}..$${hi.toString(16)} ` +
                `(${srcArmed} bytes).  "lines" below are OFFSETS from $${lo.toString(16)}.\n`);
}

function srcArm() {
    if (rangeAuditArg) return rangeArm();
    /* live   — offsets dash_block_starts[col]..$4F, the bytes the view actually uses
       full   — offsets $00..$4F, i.e. the live span PLUS the dead-below-start offsets the game
                packs twelve tables into: the arm that CONFIRMS the boundary instead of assuming
                it, because every extra reader it shows must be one of those tables
       tails  — offsets $50..$7F, the block TAILS copy_dash_data assembles $7B00-$7FFF from.  This
                is the arm that DISCHARGES the named `copy_dash_data` concern positively, by
                showing where it does read; its absence from `live`/`full` proves nothing, because
                those never arm a byte it could touch.
       blocks — all $80 bytes, all three at once */
    const mode = srcAuditArg;
    const full = mode === "full" || mode === "blocks";
    const lineLo = (c, starts) => mode === "tails" ? SRC_LINES : (full ? 0 : starts[c]);
    const lineHi = (mode === "tails" || mode === "blocks") ? SRC_STRIDE : SRC_LINES;
    const starts = [];
    for (let c = 0; c < SRC_CELLS; c++) starts.push(tm.processor.peekmem(0x3900 + c));
    for (let c = 0; c < SRC_CELLS; c++)
        for (let L = lineLo(c, starts); L < lineHi; L++) {
            const a = SRC_BASE + c * SRC_STRIDE + L;
            srcFlags[a] = 1; srcCellOf[a] = c; srcLineOf[a] = L; srcArmed++;
        }
    const what = { live:  "LIVE span only, offsets dash_block_starts[col]..$4F",
                   full:  "offsets $00..$4F — live span PLUS the twelve tables in the dead offsets",
                   tails: "block TAILS only, offsets $50..$7F — the dashData source",
                   blocks:"WHOLE blocks, offsets $00..$7F" }[mode] || mode;
    console.log(`\nsource-block audit armed at frame ${frames}: ${srcArmed} bytes (${what})`);
    console.log(`  dash_block_starts = ${starts.map(v => v.toString(16).padStart(2, "0")).join(" ")}\n`);
}
if (srcAuditArg || rangeAuditArg) {
    tm.processor.debugRead.add((addr) => {
        if (frames < fillFrameLo || frames > fillFrameHi) return;
        if (!srcArmed) srcArm();
        if (!srcFlags[addr]) return;
        const pc = tm.processor.getPrevPc(0);
        let e = srcReadPC.get(pc);
        if (!e) srcReadPC.set(pc, (e = { n: 0, cells: new Set(), lines: new Set(), sample: [] }));
        e.n++; e.cells.add(srcCellOf[addr]); e.lines.add(srcLineOf[addr]);
        if (e.sample.length < 6) e.sample.push(addr);
        srcReads++;
    });
    tm.processor.debugWrite.add((addr, b) => {
        if (frames < fillFrameLo || frames > fillFrameHi) return;
        if (!srcArmed) srcArm();
        if (!srcFlags[addr]) return;
        const pc = tm.processor.getPrevPc(0);
        let e = srcWritePC.get(pc);
        if (!e) srcWritePC.set(pc, (e = { n: 0, cells: new Set(), lines: new Set(), zero: 0 }));
        e.n++; e.cells.add(srcCellOf[addr]); e.lines.add(srcLineOf[addr]);
        if (b === 0) e.zero++;
        srcWrites++;
    });
}

// ── the interrupt register contract, measured ─────────────────────────────────────────────
// Catch the CPU at the OS's IRQ entry (the address in $FFFE/$FFFF), where A/X/Y are still the
// INTERRUPTED program's, read the return address off the 6502 stack the sequence just pushed,
// and compare A/X/Y again when execution arrives back there.  Whatever fails to match is a
// register the foreground must not rely on across an interrupt — and therefore a register the
// port's shim must treat exactly the same way.
// ⭐⭐ --force-revs=NN : PIN $003C to NN at the instant the rev-counter needle reads it, so the
// real 6502 draws the dial for a rev value WE choose.  This exists because "the port's needle is
// wrong" and "the port's rev VALUE is wrong" produce the same wrong picture, and the only way to
// separate them is to put both machines at the same $3C and diff the pixels.  Pinning the value
// where the DRAWING reads it ($51AC, `LDA $3C` at the top of the needle routine) rather than
// poking it periodically is what makes the sample exact: the engine rewrites $3C every body
// frame, so a poke between frames is a race the dial usually wins.
// ⚠ It deliberately does NOT stop the engine recomputing $3C — the physics stays untouched and
// only the dial's own input is substituted, so nothing else in the picture is perturbed.
const forceRevs = opt("force-revs", null);
if (forceRevs !== null) {
    const v = Number(forceRevs) & 0xff;
    const DIAL_READ = 0x51ac;
    let pinned = 0;
    tm.processor.debugInstruction.add((addr) => {
        if (addr === DIAL_READ) { tm.processor.writemem(0x3c, v); pinned++; }
        return false;
    });
    process.on("exit", () => console.log(`   --force-revs=$${v.toString(16)}: pinned at $51AC ${pinned}x`));
}

// ── $FE68, as the game sees it ────────────────────────────────────────────────────────────
// Six `LDA $FE68` sites in the engine, keyed by the address of the instruction AFTER each one,
// where A holds what the read returned.  The per-site record answers three separate questions:
// is it constant (the port's model), is it uniform over 0-255, and does it move like a CLOCK —
// i.e. do successive samples differ by roughly the 6502 cycles that elapsed between them,
// negated (a 1 MHz DOWN counter), rather than like a PRNG.
const T2_SITES = {
    0x0e7f: "$0E7C gravel/skid trigger (CMP #$3F)",
    0x2751: "$274E (AND #$1F)",
    0x498f: "$498C starter catch delay (AND $09)",
    0x49c0: "$49BD idle-rev jitter (AND #7)",
    0x4c09: "$4C06 (mul8, AND #7)",
    0x6362: "$635F (AND #$7F)",
};
const t2Stats = new Map();
if (viaT2) {
    for (const a of Object.keys(T2_SITES)) t2Stats.set(Number(a), { n: 0, vals: [], last: null, lastCyc: null, deltas: [] });
    tm.processor.debugInstruction.add((addr) => {
        const s = t2Stats.get(addr);
        if (s) {
            const v = tm.processor.a, cyc = tm.processor.cycleSeconds * CPS + tm.processor.currentCycles;
            s.n++;
            if (s.vals.length < 4096) s.vals.push(v);
            if (s.last !== null) {
                // A 1 MHz down-counter's low byte: v == (last - elapsed_us) mod 256.
                const el = Math.round((cyc - s.lastCyc) / 2); // 2 MHz CPU, 1 MHz timer
                s.deltas.push(((s.last - v - el) % 256 + 256) % 256);
            }
            s.last = v; s.lastCyc = cyc;
        }
        return false;
    });
    process.on("exit", () => {
        console.log("\n$FE68 (User VIA T2 counter-low), as the GAME read it:");
        for (const [addr, s] of t2Stats) {
            if (!s.n) { console.log(`   ${T2_SITES[addr]}: never reached`); continue; }
            const uniq = new Set(s.vals);
            const lo = Math.min(...s.vals), hi = Math.max(...s.vals);
            // How often the "down-counter at 1 MHz" prediction lands exactly, and within +-2.
            const exact = s.deltas.filter((d) => d === 0).length;
            const near = s.deltas.filter((d) => d <= 2 || d >= 254).length;
            console.log(`   ${T2_SITES[addr]}: n=${s.n} distinct=${uniq.size} range=$${lo.toString(16)}..$${hi.toString(16)}` +
                (s.deltas.length ? `  clock-prediction exact ${exact}/${s.deltas.length}, +-2 ${near}/${s.deltas.length}` : ""));
        }
    });
}

const irqStats = { taken: 0, engine: 0, aBad: 0, xBad: 0, yBad: 0, pBad: 0, samples: [] };
let irqPending = null;
if (irqAbi) {
    const vec = rd(0xfffe) | (rd(0xffff) << 8);
    console.log(`IRQ ABI probe: OS interrupt entry is $${vec.toString(16)}\n`);
    tm.processor.debugInstruction.add((addr) => {
        const p = tm.processor;
        if (addr === vec && !irqPending) {
            // The 6502 pushed PCH, PCL then P, so S+1 = P, S+2 = PCL, S+3 = PCH.
            const s = p.s;
            const ret = rd(0x100 + ((s + 2) & 0xff)) | (rd(0x100 + ((s + 3) & 0xff)) << 8);
            irqPending = { a: p.a, x: p.x, y: p.y, p: p.p, ret, fc: rd(0xfc) };
            irqStats.taken++;
        } else if (irqPending && addr === irqPending.ret) {
            const q = irqPending;
            irqPending = null;
            // ⚠ ONLY interrupts taken while the ENGINE was running count.  Interrupts during
            // MOS code (return address in ROM) go through the OS's own chained handler and
            // clobber registers that OS code does not rely on — including them reports "A, X
            // and Y are all clobbered", which is true of the machine as a whole and useless
            // as a contract for the port, which never runs the MOS's foreground.
            if (q.ret < 0x1200 || q.ret >= 0x8000) return false;
            irqStats.engine++;
            const bad = [];
            if (p.a !== q.a) { irqStats.aBad++; bad.push(`A ${q.a}->${p.a}`); }
            if (p.x !== q.x) { irqStats.xBad++; bad.push(`X ${q.x}->${p.x}`); }
            if (p.y !== q.y) { irqStats.yBad++; bad.push(`Y ${q.y}->${p.y}`); }
            // The FLAGS matter as much as the registers here: the fill chain's
            // `LDY src / BEQ skip` decides a cell from Z, so a lost Z is a wrong pixel.
            if (p.p !== q.p) { irqStats.pBad++; bad.push(`P ${q.p}->${p.p}`); }
            if (bad.length && irqStats.samples.length < 8)
                irqStats.samples.push(`ret $${q.ret.toString(16)}: ${bad.join(", ")}`);
        }
        return false;
    });
}

// ── the race view's bitmap character set, measured ────────────────────────────────────────
const charStats = {
    calls: 0,                 // times the bitmap arm ran
    codes: new Map(),         // char code -> count
    cells: new Set(),         // "col,row" the glyphs landed on
    callers: new Map(),       // return address of the JSR $5092 -> count
    modeFlag: new Set(),      // $64 as seen by the bitmap arm (must be < $80 every time)
    // ⭐ $77 AT $50AA — the DOUBLE-WIDTH selector.  $509B zeroes it just before the OSWORD, so
    // reading the code says it is always 0 and the nibble-expansion arm at $50AE is dead.  That
    // is a reading, not a measurement, and the two halves of that arm are exactly a double-width
    // renderer: `AND #$F0` keeps the glyph's LEFT four columns where they are, `ASL A` x4 lifts
    // the RIGHT four into their place, so one glyph is plotted across TWO cells.  Whether it
    // ever runs is a question only the machine can answer.
    widthFlag: new Map(),     // $77 as seen at $50AA -> count
    // ⚠ Attributing a MID-ENTRY to the last $5096 caller is a stale reading, not a measurement.
    // Any address in $5092-$50AA reached from OUTSIDE that span is an entry point, so record
    // the transition itself: (entry, whence) -> count.  A JSR from the $7B00 overlay is invisible
    // to every static walk, because the overlay is BUILT at runtime (docs/static-map.md §10).
    entries: new Map(),
};
if (charset) {
    const CHAR_BMP = 0x5096;      // vdu_char_def's bitmap arm — A is the character code
    const CHAR_EXP = 0x50aa;      // ...where it reads $77 and decides whether to half-expand
    const CHAR_BLK = 0x62c3;      // the OSWORD 10 control block: [0]=code, [1..8]=bitmap
    const CHAR_COL = 0x62cc;      // the cell column the glyph is plotted at
    const CHAR_ROW = 0x62cd;      // ...and the frame-buffer scan line of its BOTTOM row
    const WIDTH_FLAG = 0x77;
    const SPAN_LO = 0x5092, SPAN_HI = 0x50aa;
    let prevPC = 0;
    tm.processor.debugInstruction.add((addr) => {
        const inSpan = addr >= SPAN_LO && addr <= SPAN_HI;
        const wasIn = prevPC >= SPAN_LO && prevPC <= SPAN_HI;
        if (inSpan && !wasIn) {
            const k = `${addr.toString(16)}<-${prevPC.toString(16)}`;
            const e = charStats.entries.get(k) || { n: 0, w: new Set(), codes: new Set() };
            e.n++;
            e.w.add(rd(WIDTH_FLAG));
            e.codes.add(rd(0x62c3));
            charStats.entries.set(k, e);
        }
        prevPC = addr;
        if (addr === CHAR_EXP) {
            const w = rd(WIDTH_FLAG);
            charStats.widthFlag.set(w, (charStats.widthFlag.get(w) || 0) + 1);
            return false;
        }
        if (addr !== CHAR_BMP) return false;
        const p = tm.processor;
        charStats.calls++;
        charStats.codes.set(p.a, (charStats.codes.get(p.a) || 0) + 1);
        charStats.cells.add(`${rd(CHAR_COL)},${rd(CHAR_ROW)}`);
        charStats.modeFlag.add(rd(0x64));
        // Who prints?  The JSR that got here is still on the stack: S+1/S+2 hold the return
        // address minus one.  Attributing the codes to a caller is what separates the lap-time
        // readout from any other text the race view has.
        const s = p.s;
        const ret = ((rd(0x100 + ((s + 1) & 0xff)) | (rd(0x100 + ((s + 2) & 0xff)) << 8)) + 1) & 0xffff;
        charStats.callers.set(ret, (charStats.callers.get(ret) || 0) + 1);
        return false;
    });
}

const ULA_CTRL = 0xfe20, ULA_PAL = 0xfe21;
const bandFrames = []; // one entry per captured field: the writes and the line they landed on
let curBand = null;
tm.processor.debugWrite.add((addr, b) => {
    if ((watchAddr !== null ? addr === watchAddr
                            : watchLo !== null && addr >= watchLo && addr <= watchHi)
        && frames >= memAtFrame) {
        /* getPrevPc(0), not processor.pc — see the note in the fill census below. */
        const pc = tm.processor.getPrevPc(0);
        let w = watchPCs.get(pc);
        if (!w) watchPCs.set(pc, (w = { n: 0, vals: new Map() }));
        w.n++;
        w.vals.set(b, (w.vals.get(b) || 0) + 1);
        if (watchLo !== null) (w.addrs || (w.addrs = new Set())).add(addr);
    }
    if (fillArg && frames >= fillFrameLo && frames <= fillFrameHi && fillFlags[addr]) {
        // ⚠ A window of frames only: this fires on every frame-buffer write in it.
        //
        // ⭐⭐ A STORE IS NOT A CHANGE, and the difference is the whole Phase 6 sizing.  The port's
        // own shape counters (src/platform/shape.h) are SNAPSHOT DIFFS, so they measure bytes that
        // ended up different and call them writes — which is right for pricing a dirty-region
        // DECODE and wrong for pricing a direct PLOTTER, whose cost is stores.  A rasteriser that
        // re-plots an identical span costs full price and shows up as nothing.  debugWrite fires
        // BEFORE the store lands (src/6502.js writemem), so the byte still there is the old one.
        // ⚠⚠ NOT `processor.pc` — jsbeeb advances it during the instruction, so the store at
        // $7C64 gets reported as $7C66, which is the NEXT unit's `LDY $3300,X` and stores
        // nothing.  An off-by-one-instruction attribution is a quiet wrong answer: it names a
        // real, plausible, innocent instruction.  `getPrevPc(0)` is the PC recorded at this
        // instruction's own start (6502.js executeInternal).
        const pc = tm.processor.getPrevPc(0);
        let e = fillPC.get(pc);
        if (!e) fillPC.set(pc, (e = { n: 0, lines: new Set(), cells: new Set(), nonzero: 0, changed: 0,
                                      perLine: new Uint32Array(LINES * ROWS),
                                      perLineChanged: new Uint32Array(LINES * ROWS) }));
        e.n++;
        e.lines.add(fillLineOf[addr]);
        e.cells.add(fillCellOf[addr]);
        e.perLine[fillLineOf[addr]]++;
        if (b !== 0) e.nonzero++;
        /* ⚠ peekmem, NOT readmem: readmem fires the READ hook, and this instrument would then
           report its own comparison as the game reading the frame buffer back — measured, it
           doubled every store site's read count exactly. */
        if (tm.processor.peekmem(addr) !== b) {
            e.changed++; fillChanged++; e.perLineChanged[fillLineOf[addr]]++;
        }
        fillWrites++;
    }
    if (addr !== ULA_CTRL && addr !== ULA_PAL) return;
    if (frames === 0) return;
    const v = tm.processor.video;
    // bitmapY is the display line being generated; that is the raster position the write
    // takes effect at, which is the whole point of a mid-frame palette poke.
    const line = v.bitmapY;
    if (!curBand || curBand.frame !== frames) {
        curBand = { frame: frames, writes: [] };
        if (bandFrames.length < 4) bandFrames.push(curBand);
    }
    if (curBand.writes.length < 200)
        curBand.writes.push({ line, reg: addr === ULA_CTRL ? "MODE" : "PAL", val: b });
});

// ── input ─────────────────────────────────────────────────────────────────────────────────
async function hold(colrow, cycles) {
    tm.processor.sysvia.keyDownRaw(colrow);
    await tm.runFor(cycles);
    tm.processor.sysvia.keyUpRaw(colrow);
}
// Hold a key until `test()` goes true, or give up.  This is the whole difference from the
// timed-pulse approach: the engine's own state says when the key landed.
async function holdUntil(colrow, test, label, budget = 4 * CPS) {
    if (test()) return true;
    tm.processor.sysvia.keyDownRaw(colrow);
    let spent = 0;
    while (spent < budget && !test()) {
        await tm.runFor(20000);
        spent += 20000;
    }
    tm.processor.sysvia.keyUpRaw(colrow);
    const ok = test();
    if (!ok) console.log(`    ! ${label}: key held ${(spent / CPS).toFixed(1)}s with no effect`);
    await tm.runFor(40000);
    return ok;
}

// Answer the menu that is currently active by pressing the key the engine is polling for
// `option` (1-based), then the confirm key.
async function answerMenu(option, why) {
    const before = menuAnswers;
    const optKey = inkeyToColRow(rd(KEY_TBL + option));
    const okKey = inkeyToColRow(rd(KEY_TBL + 0));
    console.log(`  -> option ${option} (${why}); polling ${menuOptions} options, ` +
        `key=[${optKey}] confirm=[${okKey}]`);
    await holdUntil(optKey, () => rd(SEL_FLAG) !== 0, `option ${option} not registered`);
    await holdUntil(okKey, () => menuAnswers > before, "confirm not accepted");
    return menuAnswers > before;
}

// Answer the two-character numeric line editor.  Each keystroke is confirmed against the
// engine's OWN buffer at $0074 before the next one is sent, and RETURN against the validator
// actually having run — so "the key did not arrive" can never again be confused with "the
// value was rejected", which is exactly the ambiguity that stalled this loop for two days.
const DIGIT = [utils.BBC.K0, utils.BBC.K1, utils.BBC.K2, utils.BBC.K3, utils.BBC.K4,
    utils.BBC.K5, utils.BBC.K6, utils.BBC.K7, utils.BBC.K8, utils.BBC.K9];
async function answerNumber(text, why) {
    const v0 = numValidations, r0 = numRejects;
    console.log(`  -> type "${text}" (${why})`);
    for (let i = 0; i < text.length; i++) {
        const c0 = charsStored;
        const ok = await holdUntil(DIGIT[Number(text[i])], () => charsStored > c0,
            `digit '${text[i]}' was never stored by console_io`);
        if (!ok) return false;
    }
    const ok = await holdUntil(utils.BBC.RETURN, () => numValidations > v0,
        "RETURN never completed the line");
    if (!ok) return false;
    if (numRejects > r0) {
        console.log(`    ! "${text}" was REJECTED as out of range — the line WAS entered, the value is wrong`);
        return false;
    }
    return true;
}

// ⭐ Answer the DRIVER-NAME line editor — the competition branch's third prompt, and the one
// that has no numeric validator behind it.
//
// ⚠ It is NOT the same prompt shape as the wing settings, and treating it as one is what
// stalled the first competition run: the wing editor is $3EE0 (read + VALIDATE), so `numAsks`
// counts it and `numValidations` says when RETURN landed.  The name goes straight through
// console_io ($6300) with no validator at all, so BOTH of those counters stay flat, the pump
// falls through to its `hold(SPACE)` arm forever, and the transcript fills with the echo of
// whatever key the fallback happens to be holding.  Detect it from the TEXT the engine printed
// and complete it on `console_io RETURNING`, which is the only event that actually means the
// line was taken.
async function answerName(text, why) {
    console.log(`  -> type "${text}" (${why})`);
    const LETTER = { R: utils.BBC.R, E: utils.BBC.E, V: utils.BBC.V, S: utils.BBC.S };
    for (const ch of text) {
        const c0 = charsStored;
        if (!LETTER[ch]) throw new Error(`no key mapping for '${ch}'`);
        if (!(await holdUntil(LETTER[ch], () => charsStored > c0,
                              `letter '${ch}' was never stored by console_io`)))
            return false;
    }
    // console_io's caller moves on as soon as the line is entered, and the next thing the front
    // end does is print.  A new transcript message is therefore the completion signal; the
    // character counter is not, because it stops moving on a rejected key too.
    const t0 = transcript.length, m0 = menuEntries;
    return holdUntil(utils.BBC.RETURN, () => transcript.length > t0 || menuEntries > m0,
                     "RETURN never ended the name line");
}

// ── boot ──────────────────────────────────────────────────────────────────────────────────
const SPACE = utils.BBC.SPACE;
await tm.runUntilInput(20);
tm.drainText();
await tm.type("*EXEC !BOOT");
await tm.runUntilInput(20);
tm.drainText();

let atMenu = false;
for (let i = 0; i < 40 && !atMenu; i++) {
    await hold(SPACE, 40000);
    await tm.runFor(3 * CPS);
    atMenu = /PRESS.*BRANDS HATCH/i.test(tm.drainText());
}
if (!atMenu) throw new Error("never reached the REVSMEN track menu");
// REVSMEN is BASIC in MODE 7, so it reads the keyboard through the MOS, not through
// key_binding_tbl — a plain keypress is right here.
const TRACKKEY = [utils.BBC.K1, utils.BBC.K2, utils.BBC.K3, utils.BBC.K4, utils.BBC.K5][track - 1];
await hold(TRACKKEY, 40000);
await tm.runFor(4 * CPS);
console.log(`REVS2 loaded (track ${track}); driving the front end by transcript\n`);

// ── the front end, answered from what it prints ────────────────────────────────────────────
// ONE pump for all three prompt kinds, run until frames actually start advancing.  The
// earlier two-phase shape (front end, then "we must be racing") was itself a source of error:
// entering the session driver $655A is NOT the same as racing, because $655A's own preamble
// $3C50 asks two more questions.  The only honest completion test is that frames advance.
const deadline = 180 * CPS;
let spent = 0;
let wingsAsked = 0;
let nameAsked = false;
while (spent < deadline && frames === 0) {
    const e0 = menuEntries, a0 = numAsks;
    await tm.runFor(400000);
    spent += 400000;

    for (const t of transcript.splice(0)) {
        if (t.msg !== MSG_SPACEBAR) console.log(`  [$${t.msg.toString(16).padStart(2, "0")}] ${t.text}`);
        // ⚠ Match with \s* between the words, not a literal space.  print_message's token/space
        // encoding emits this prompt as "ENTERNAME OFDRIVER" — the spaces fall where the tokens
        // do, not where English puts them, so /ENTER NAME/ never fires and the pump falls
        // through to its SPACE arm until the deadline.  Measured, not assumed.
        if (/NAME\s*OF\s*DRIVER/i.test(t.text)) nameAsked = true;
    }

    if (nameAsked) {
        nameAsked = false;
        if (!(await answerName("REVS", "driver name"))) break;
        spent += 2 * CPS;
    } else if (numAsks > a0 || (rdchHits > 0 && numValidations < numAsks)) {
        // The numeric line editor is open.  On the practice path this is the wing settings,
        // rear then front.
        if (!(await answerNumber(wing, `wing setting ${++wingsAsked === 1 ? "rear" : "front"}`))) break;
        spent += 2 * CPS;
    } else if (menuEntries > e0 || (menuPolls > 0 && menuAnswers < menuEntries)) {
        // ⭐ --competition takes option 2 at the FIRST menu only ($63F7, message $27,
        // `1 PRACTICE 2 COMPETITION`).  Everything after it is the competition branch's own
        // chain — class, qualifying duration, driver names — and the first option is a fine
        // answer to each; what matters is that the session has a FIELD OF CARS in it, which
        // practice does not, so competitor-car rendering has a stimulus at all.
        const wantTwo = competition && menuAnswers === 0;
        if (!(await answerMenu(wantTwo ? 2 : 1,
                               wantTwo ? "COMPETITION" : "take the first option"))) break;
        spent += 2 * CPS;
    } else {
        await hold(SPACE, 60000); // a "PRESS SPACE BAR TO CONTINUE" page ($34D0)
        await tm.runFor(200000);
        spent += 260000;
    }
}

if (frames === 0) {
    console.log("\nFAILED to reach a running race.");
    console.log(`  menus seen=${menuEntries} answered=${menuAnswers} choices=[${answered}]`);
    console.log(`  numbers asked=${numAsks} entered=${numValidations} rejected=${numRejects} rdch=${rdchHits}`);
    console.log(`  session entries=${sessionEntries}`);
} else {
    console.log(`\n⭐ RACING.  session=$655A entered ${sessionEntries}x, menu choices [${answered}], ` +
        `wings entered ${numValidations}`);
    // ⭐ $0003 — the FIELD-WALK TERMINATOR, and worth printing on every run.
    // check_car_pair ($2692) starts at `LDX $03` and loops `car_index_inc / CPX $03 / BNE`
    // ($2797), so it walks all 20 cars and stops when the index wraps back to $03.
    // car_index_inc wraps 19->0, so a value outside 0..19 here NEVER matches and the loop spins
    // forever.  The port hung there in its first competition race with $03 = $FF: zero page is
    // exactly what revs_mem.bin does not model (the real MOS/BASIC leaves it populated), so this
    // is the ground truth for what the cell should hold.  $2637's `LDA $5F3B / BMI` is why
    // practice never reaches any of it.
    console.log(`   ⭐ $0003 (check_car_pair's field-walk terminator) = ` +
        `$${rd(0x0003).toString(16).padStart(2, "0")} (${rd(0x0003)})` +
        (rd(0x0003) < 20 ? "  — a valid car index, so the walk terminates"
                         : "  ⚠ NOT a valid car index (0..19)"));
    // ...and the array it walks.  find_player_neighbours ($63A2) searches car_order for the
    // player's index ($6F) and falls out with X = $FF when it is not there, which is how $0003
    // becomes an impossible terminator.  So print both: a car_order that is not a permutation of
    // 0..19 is the upstream fault, and $0003 is only the symptom.
    // ⚠⚠ AND THE 6502 STACK POINTER, because car_order lives in PAGE ONE.
    // $0100-$019F holds eight 20-entry per-car arrays ($0100 $0114 $0128 $013C $0150 $0164
    // $0178 $018C) — i.e. the game deliberately uses the BOTTOM of the stack page as data, on
    // the assumption that S never descends that far.  So S is a correctness invariant, not a
    // curiosity: the moment it drops below $9F a PHA/PHP lands in car_order and the field is
    // silently corrupted.  The port was measured at S = $B8 and falling, with ASCII ('0','1','2')
    // sitting in car_order — pushed characters, exactly what that failure looks like.
    // engine_init ($386D `TSX / STX $6B`) saves S and $3275 (`TXS`) restores it, which is the
    // mechanism that is supposed to keep this bounded.
    // $6B is engine_init's own saved copy ($386D `TSX / STX $6B`), i.e. the S the MOS handed the
    // engine — which is the value the port has to START at.  Nothing in the game sets S up; it
    // INHERITS it, and `mem[]` built from the disc does not model that (docs/bbc-reference-loop.md
    // on provisional zero page).
    console.log(`   ⭐ entry S saved at $6B = $${rd(0x6b).toString(16)} ` +
        "— what the MOS handed the engine, i.e. what cpu.S must be initialised to");
    console.log(`   ⭐ 6502 S = $${tm.processor.s.toString(16)} ` +
        `(pushes land at $${(0x100 + tm.processor.s).toString(16)}; ` +
        `page-1 car arrays end at $019F — S must stay ABOVE $9F)`);
    const order = Array.from({ length: 20 }, (_, i) => rd(0x013c + i));
    const perm = new Set(order).size === 20 && order.every((v) => v < 20);
    console.log(`   car_order $013C = [${order.join(" ")}]`);
    console.log(`   player $6F = ${rd(0x6f)}; car_order is ` +
        (perm ? "a permutation of 0..19 ✓" : "⚠ NOT a permutation of 0..19") +
        `, player ${order.includes(rd(0x6f)) ? "IS" : "is NOT"} in it`);
    const rear = rd(0x5f3e), front = rd(0x5f3d);
    console.log(`   $5F3B (practice flag) = $${rd(0x5f3b).toString(16)}  ` +
        `rear wing $5F3E = ${rear}  front wing $5F3D = ${front}`);
    // Check the values the engine actually stored, not just that it accepted something: a
    // dropped digit is accepted as a smaller number and changes the car's aerodynamics, which
    // would quietly make every captured frame ground truth for the wrong setup.
    if (rear !== Number(wing) || front !== Number(wing))
        console.log(`   ⚠ WING MISMATCH: asked for ${wing}/${wing}, engine stored ${rear}/${front} — ` +
            `a keystroke was dropped; this capture is of a DIFFERENT car setup than requested`);
    console.log("");
}

// ── in the race ───────────────────────────────────────────────────────────────────────────
// Starter, first gear, throttle.  ⚠ Each one waits on the ENGINE'S OWN state, not on a timer.
// A fixed 0.15 s tap on 'T' looked like it worked — the key inventory showed the engine polling
// $DC once per frame all race — and left $61 (engine-running) at 0 for 200 frames.  The car
// stayed parked, and a parked car yields a scene that never changes, which the zero-byte scan
// would have reported as a perfectly confident (and useless) answer.
// ⭐⭐ WHAT THE DASHBOARD AND THE INPUT ACTUALLY HOLD, ON REAL HARDWARE.  Added 2026-08-16 to
// settle three reports off the Amiga build — a rev counter pinned to maximum with the engine OFF,
// a throttle that behaves as if it were held, and a steering indicator that does not move.  Every
// one of those is a claim about state the game keeps, so the only way to judge it is to read the
// same bytes off a machine that is definitively right.
//
//   $05F5  input mode: bit 7 set = ADC (mouse, on the port), clear = keyboard.  engine_init
//          zeroes $05F4-$05FD, so a real BBC is in KEYBOARD mode unless SHIFT+f2 was pressed.
//   $0076  the keyboard steering path's own answer ($15B3): 0 none, 1 right, 2 left, 3 both
//   $0074/$0075  the steering value the rest of the engine reads (both paths converge here)
//   $0061  engine running ($FF once it catches)   $003C  the rev counter
//   $0063  road speed    $0040  gear
const DASH = [
    [0x05f5, "input mode ($05F5, bit7 = ADC)"],
    [0x0076, "steer keys ($76: 0/1/2/3)"],
    [0x0074, "steer value lo ($74)"],
    [0x0075, "steer value hi ($75)"],
    [0x0061, "engine running ($61)"],
    [0x003c, "rev counter ($3C)"],
    [0x0063, "road speed ($63)"],
    [0x0040, "gear ($40)"],
];
const dashRow = (tag) =>
    console.log(`   [${tag}] ` + DASH.map(([a, n]) => `${n}=$${rd(a).toString(16).padStart(2, "0")}`).join("  "));

if (drive) {
    // ⭐ BEFORE THE STARTER — this is the state the report is about: engine OFF, neutral, no
    // throttle.  A rev counter reading maximum here would be wrong on any machine.
    dashRow("engine off, in the pits");
    const gear0 = rd(0x40);
    const cranked = await holdUntil(utils.BBC.T, () => rd(ENGINE_ON) === 0xff,
        "'T' held but the engine never caught ($61 stayed 0)", 8 * CPS);
    console.log(`   starter: engine-running $61 = $${rd(ENGINE_ON).toString(16)} ${cranked ? "✓" : "✗"}`);
    // The gear change is debounced through $19: it is only accepted when $19 is 0, which
    // happens on a frame where neither gear key is held — so release matters as much as press.
    const geared = await holdUntil(utils.BBC.Q, () => rd(0x40) !== gear0,
        "'Q' held but the gear at $40 never changed", 4 * CPS);
    console.log(`   first gear: $40 ${gear0} -> ${rd(0x40)} ${geared ? "✓" : "✗"}`);
    dashRow("engine on, first gear, no throttle");
    if (park) {
        // ⭐ Stop here, matching the port's parked autorun state.  Let the idle settle for a
        // second so the sample and the dump describe the same steady state rather than the
        // frame the gear change landed on.
        await tm.runFor(CPS);
        dashRow("PARKED (--park: nothing held)");
    }
    if (!park) {
    tm.processor.sysvia.keyDownRaw(utils.BBC.S); // hold the throttle down from here
    await tm.runFor(CPS);
    dashRow("throttle held");

    // ⭐ AND STEER, which no probe here has ever done.  $15B3 polls -87 then -88; the report is
    // that neither the keys nor the mouse move the indicator on the port, so the first thing to
    // establish is what the KEYS do on real hardware.  Held for a second each so the value has
    // time to ramp — steering is integrated, not instantaneous ($1EE9 onwards).
    for (const [name, code] of [["-87 (steer one way)", 0xa9], ["-88 (steer the other)", 0xa8]]) {
        const colrow = inkeyToColRow(code);
        tm.processor.sysvia.keyDownRaw(colrow);
        await tm.runFor(CPS);
        dashRow(`throttle + ${name}`);
        tm.processor.sysvia.keyUpRaw(colrow);
        await tm.runFor(CPS / 2);
    }
    dashRow("throttle, steering released");
    // ⭐ --hold-steer=left|right : keep the wheel HARD OVER for the rest of the run, which is the
    // only way this loop has ever provoked a SPIN.  A straight-line drive crashes eventually but
    // never spins, so the spin arms of update_camera_and_drive_state ($45BF) and begin_spin_from_a
    // ($4DCB) are unreachable without it — and they are what drive_state's 1 and spin_countdown's
    // seeding mean.  Pair it with --watch=002D.
    if (holdSteer) {
        const code = holdSteer === "left" ? 0xa9 : 0xa8;
        tm.processor.sysvia.keyDownRaw(inkeyToColRow(code));
        console.log(`   --hold-steer=${holdSteer}: wheel held over for the rest of the run`);
    }
    }
}

const f0 = frames;
let waited = 0;
while (frames - f0 < wantFrames && waited < 60 * CPS) {
    await tm.runFor(CPS);
    waited += CPS;
    const t = waited / CPS;
    if (press) {
        // ⚠ The report has to be per SECOND, not just at the end: a key that ends the session
        // shows up as state_flags changing and the frame counter STOPPING, and an end-of-run
        // total cannot tell those apart from a run that simply finished.
        if (!press.down && t >= press.at) {
            for (const c of press.codes) tm.processor.sysvia.keyDownRaw(inkeyToColRow(c));
            press.down = true;
            console.log(`   t=${t}s --press: holding ${press.codes.map((c) => "$" + c.toString(16)).join(" + ")}`);
        } else if (press.down && !press.released && t >= press.at + press.hold) {
            for (const c of press.codes) tm.processor.sysvia.keyUpRaw(inkeyToColRow(c));
            press.released = true;
            console.log(`   t=${t}s --press: released`);
        }
        console.log(`   t=${t}s frames=${frames - f0}  $05F4=$${rd(0x05f4).toString(16).padStart(2, "0")}` +
            `  session_is_race=$${rd(0x006c).toString(16).padStart(2, "0")}  engine insns=${engineInsns}`);
    } else if (waited % (10 * CPS) === 0) {
        console.log(`   t=${t}s frames=${frames - f0}/${wantFrames}`);
    }
}

// ⚠ A session that renders no frames must SAY WHERE IT IS, not just report zero.  Every
// earlier probe here reported its own stall as evidence about something else; a PC histogram
// names the loop directly, so the next step is never a guess.
if (frames - f0 === 0) {
    const hist = new Map();
    const hook = tm.processor.debugInstruction.add((a) => {
        hist.set(a, (hist.get(a) || 0) + 1);
        return false;
    });
    await tm.runFor(2 * CPS);
    hook.remove();
    const top = [...hist.entries()].sort((a, b) => b[1] - a[1]).slice(0, 16);
    console.log("\nNO FRAMES.  Hottest addresses over 2 BBC seconds — this names the loop:");
    for (const [a, n] of top) console.log(`   $${a.toString(16).padStart(4, "0")}  ${n}`);
    console.log(`   distinct addresses executed: ${hist.size}`);
    console.log(`   $05F4 = $${rd(0x05f4).toString(16)}  (the session driver loops while bit 6 is set)`);
    const seq = [];
    for (let i = 0; i < RING; i++) {
        const a = ring[(ringAt + i) & (RING - 1)];
        if (a) seq.push("$" + a.toString(16).padStart(4, "0"));
    }
    console.log(`   engine instructions executed in total: ${engineInsns}`);
    console.log("   last engine addresses before it stopped running:\n     " + seq.join(" "));
    const text = tm.drainText();
    if (text.trim()) console.log(`   text the MOS printed meanwhile: ${JSON.stringify(text)}`);
}
if (drive) tm.processor.sysvia.keyUpRaw(utils.BBC.S);

console.log(`\nframes rendered in the race: ${frames - f0} (${((waited / CPS) / Math.max(1, frames - f0)).toFixed(3)} s/frame)`);
console.log(`session entries=${sessionEntries}  spacebar prompts=${spacebarPrompts}`);

// ── what the engine polled while racing ───────────────────────────────────────────────────
if (frames > 0) {
    const NAME = {
        0x9d: "SPACE amplify-steering", 0x9f: "TAB gear-down", 0xa6: "DELETE unfreeze",
        0xa8: "'+' steer-right", 0xa9: "'L' steer-left", 0xae: "'S' throttle",
        0xb6: "RETURN", 0xbe: "'A' brake", 0xdc: "'T' starter", 0xef: "'Q' gear-up",
        0xff: "SHIFT", 0x86: "RIGHT (SHIFT+ = quit)",
        /* shift_key_commands' table, $3DE2 — ⚠ the BBC function keys are NOT contiguous:
           f0, f4 and f7 sit outside row 7.  Re-derived from utils.BBC 2026-09-08. */
        0xdf: "f0 return to pits", 0x8e: "f1 keyboard", 0x8d: "f2 joystick",
        0x8c: "f3 CAS off", 0xeb: "f4 volume down", 0x8b: "f5 volume up",
        0x8a: "f6 CAS on", 0xe9: "f7 retire", 0x96: "COPY freeze",
    };
    const rows = [...keyPolls.entries()].sort((a, b) => b[1] - a[1]);
    console.log(`\nkeys the engine polled during the race (starter poll $4978 x${starterPolls}, ` +
        `engine-running $61 = $${rd(ENGINE_ON).toString(16)}):`);
    for (const [x, n] of rows)
        console.log(`   $${x.toString(16).padStart(2, "0")} ${(NAME[x] || "?").padEnd(24)} ${n}`);
}

// ── the measured interrupt register contract ───────────────────────────────────────────────
if (irqAbi) {
    const s = irqStats;
    console.log(`\n⭐ INTERRUPT REGISTER CONTRACT, measured over ${s.engine} interrupts taken\n   while the ENGINE was running (of ${s.taken} total; the rest interrupted MOS code):`);
    for (const [name, n] of [["A", s.aBad], ["X", s.xBad], ["Y", s.yBad], ["P (flags)", s.pBad]])
        console.log(`   ${name}: ${n === 0
            ? "PRESERVED across every interrupt — the port's shim must preserve it too"
            : `CLOBBERED on ${n}/${s.engine} interrupts — the foreground cannot rely on it, ` +
              `and neither may the port`}`);
    for (const line of s.samples) console.log(`     e.g. ${line}`);
}

// ── the race view's character set, measured ───────────────────────────────────────────────
if (charset) {
    const c = charStats;
    console.log(`\n⭐ THE RACE VIEW'S BITMAP TEXT — ${c.calls} calls to vdu_char_def's OSWORD 10 arm:`);
    if (c.calls === 0) {
        console.log("   NONE.  Either the run never reached the race view, or the text this");
        console.log("   session draws is elsewhere — do not read a zero here as 'no font needed'.");
    } else {
        const codes = [...c.codes.entries()].sort((a, b) => a[0] - b[0]);
        const glyph = (k) => (k >= 0x20 && k < 0x7f ? `'${String.fromCharCode(k)}'` : "   ");
        console.log(`   ${codes.length} distinct codes, ${c.cells.size} distinct cells, ` +
            `$64 seen as {${[...c.modeFlag].map((v) => "$" + v.toString(16)).join(",")}} ` +
            `(the bitmap arm requires bit 7 clear)`);
        let line = "   ";
        for (const [k, n] of codes) {
            line += `$${k.toString(16).padStart(2, "0")}${glyph(k)}x${String(n).padEnd(5)} `;
            if (line.length > 90) { console.log(line); line = "   "; }
        }
        if (line.trim()) console.log(line);
        const lo = Math.min(...codes.map((e) => e[0])), hi = Math.max(...codes.map((e) => e[0]));
        console.log(`   range $${lo.toString(16)}..$${hi.toString(16)} — ` +
            (lo >= 0x20 && hi <= 0x7f
                ? "entirely inside printable ASCII, so a 96-glyph font is the whole job"
                : "⚠ OUTSIDE printable ASCII: user-defined characters are in play too"));
        const callers = [...c.callers.entries()].sort((a, b) => b[1] - a[1]);
        console.log("   printed from:");
        for (const [pc, n] of callers.slice(0, 8))
            console.log(`     $${pc.toString(16).padStart(4, "0")}  x${n}`);
        console.log("   every way INTO $5092-$50AA (entry <- whence), which is how a call that");
        console.log("   skips $509B's `STA $77` shows itself:");
        for (const [k, e] of [...c.entries.entries()].sort((a, b) => b[1].n - a[1].n)) {
            const codes = [...e.codes].map((v) => "$" + v.toString(16)).join(",");
            console.log(`     $${k}  x${String(e.n).padEnd(5)} ` +
                `$77={${[...e.w].map((v) => "$" + v.toString(16)).join(",")}}  code={${codes}}`);
        }
        const wf = [...c.widthFlag.entries()].sort((a, b) => a[0] - b[0]);
        console.log(`   ⭐ $77 at $50AA (the DOUBLE-WIDTH selector): ` +
            wf.map(([v, n]) => `$${v.toString(16)} x${n}`).join("  "));
        if (wf.length === 1 && wf[0][0] === 0) {
            console.log("      only 0 — the nibble-expansion arm at $50AE never runs in this");
            console.log("      session, so every glyph is plotted whole, one cell wide.");
        } else {
            console.log("      NONZERO VALUES OCCUR — $509B's `LDA #0 / STA $77` is NOT the last");
            console.log("      word, so something writes $77 between it and $50AA, and the port");
            console.log("      must reproduce whatever that is or double-width text comes out");
            console.log("      single-width (or blank, when the half it wants is empty).");
        }
    }
}

// ── ⭐⭐⭐ THE SOURCE-BLOCK READER AUDIT REPORT ───────────────────────────────────────────────
if (srcAuditArg || rangeAuditArg) {
    const syms = [];
    try {
        const csv = fs.readFileSync(new URL("../disasm/symbols.csv", import.meta.url), "utf8");
        for (const line of csv.split("\n")) {
            const m = line.match(/^0x([0-9A-Fa-f]{4}),([^,]+),([^,]*),/);
            if (m && m[3].trim() === "func") syms.push([parseInt(m[1], 16), m[2]]);
        }
        syms.sort((a, b) => a[0] - b[0]);
    } catch { /* names are a convenience; the addresses are the finding */ }
    const nameOf = (pc) => {
        let best = null;
        for (const [a, n] of syms) { if (a <= pc) best = [a, n]; else break; }
        return best ? `${best[1]}${best[0] === pc ? "" : "+" + (pc - best[0])}` : "?";
    };
    const nWin = fillFrameHi - fillFrameLo + 1;
    const roll = (map) => {
        const byFn = new Map();
        for (const [pc, e] of map.entries()) {
            const fn = nameOf(pc).split("+")[0];
            let f = byFn.get(fn);
            if (!f) byFn.set(fn, (f = { n: 0, pcs: 0, cells: new Set(), lines: new Set(), zero: 0 }));
            f.n += e.n; f.pcs++; f.zero += (e.zero || 0);
            for (const c of e.cells) f.cells.add(c);
            for (const L of e.lines) f.lines.add(L);
        }
        return [...byFn.entries()].sort((a, b) => b[1].n - a[1].n);
    };
    const span = (set) => {
        const v = [...set].sort((a, b) => a - b);
        return v.length === 0 ? "-" : v.length <= 6 ? v.join(",") : `${v[0]}..${v[v.length - 1]} (${v.length})`;
    };
    console.log(`\n⭐⭐⭐ ${rangeAuditArg ? `THE RANGE $${rangeAuditArg}` : "THE VIEW SOURCE BLOCKS"} ON A REAL BBC — frames ${fillFrameLo}..${fillFrameHi}, ` +
                `${srcArmed} bytes armed`);
    console.log(`   READS  ${srcReads} (${(srcReads / nWin).toFixed(0)}/frame) from ${srcReadPC.size} PCs`);
    console.log(`   WRITES ${srcWrites} (${(srcWrites / nWin).toFixed(0)}/frame) from ${srcWritePC.size} PCs`);
    console.log(`\n   ⭐ WHO READS THEM — every one of these is a reader the RESULTS rule must account for:`);
    for (const [fn, f] of roll(srcReadPC))
        console.log(`      ${String(Math.round(f.n / nWin)).padStart(6)}/frame  ${fn.padEnd(26)} ` +
                    `cells ${span(f.cells).padEnd(14)} lines ${span(f.lines)}   (${f.pcs} PCs)`);
    console.log(`\n   WHO WRITES THEM:`);
    for (const [fn, f] of roll(srcWritePC))
        console.log(`      ${String(Math.round(f.n / nWin)).padStart(6)}/frame  ${fn.padEnd(26)} ` +
                    `cells ${span(f.cells).padEnd(14)} lines ${span(f.lines)}   ` +
                    `(${f.pcs} PCs, ${Math.round(f.zero / nWin)}/frame wrote ZERO)`);
    if (srcReads === 0 && srcWrites === 0)
        console.log("   ⚠⚠ NOTHING touched the armed bytes — suspect the arming, not the engine.");
}

// ── who filled those lines ────────────────────────────────────────────────────────────────
if (fillArg && fillWrites) {
    // Name the PCs from disasm/symbols.csv — the nearest preceding symbol, so a write from
    // the middle of a routine still lands on that routine.
    // ⚠⚠ `func` ROWS ONLY. A PC is code, so only a code symbol can name it — and the pages the
    // dash-code overlay shares with the MODE 7 screen carry DATA symbols interleaved with the
    // very chains that run there. `menu_row_attr` ($7E85) is the correct name for the front
    // end's menu attribute cell and the nearest preceding row for eight of view_cell_chain_b's
    // unit stores, so every attribution report of the RACE used to read as if a menu routine
    // were painting the viewport. Filtering to `func` names them view_cell_chain_b_mid, which
    // is what they are, and costs nothing elsewhere: a PC always has a preceding func.
    const syms = [];
    try {
        const csv = fs.readFileSync(new URL("../disasm/symbols.csv", import.meta.url), "utf8");
        for (const line of csv.split("\n")) {
            const m = line.match(/^0x([0-9A-Fa-f]{4}),([^,]+),([^,]*),/);
            if (m && m[3].trim() === "func") syms.push([parseInt(m[1], 16), m[2]]);
        }
        syms.sort((a, b) => a[0] - b[0]);
    } catch { /* names are a convenience; the addresses are the finding */ }
    const nameOf = (pc) => {
        let best = null;
        for (const [a, n] of syms) { if (a <= pc) best = [a, n]; else break; }
        return best ? `${best[1]}${best[0] === pc ? "" : "+" + (pc - best[0])}` : "?";
    };
    const rows = [...fillPC.entries()].sort((a, b) => b[1].n - a[1].n);
    const nWin = fillFrameHi - fillFrameLo + 1;
    console.log(`\n⭐ WHO WRITES DISPLAY LINES ${fillArg} ON A REAL BBC — ${fillWrites} writes ` +
        `(${fillChanged} of them CHANGED the byte) from ${rows.length} distinct PCs over ` +
        `frames ${fillFrameLo}..${fillFrameHi}:`);
    console.log(`   per frame: ${(fillWrites / nWin).toFixed(0)} stores, ` +
        `${(fillChanged / nWin).toFixed(0)} changes ` +
        `(${(100 * fillChanged / Math.max(1, fillWrites)).toFixed(1)}% of stores land a new value)`);
    for (const [pc, e] of rows.slice(0, 20)) {
        const ls = [...e.lines].sort((a, b) => a - b);
        console.log(`   PC $${pc.toString(16).padStart(4, "0")}  ${String(e.n).padStart(6)} writes ` +
            `(${e.nonzero} non-zero, ${e.changed} changed)  lines ` +
            `${ls.length > 8 ? ls[0] + ".." + ls[ls.length - 1] : ls.join(",")}` +
            `   ${nameOf(pc)}`);
    }
    /* ⭐ And the same totals rolled up per ROUTINE, because a plotter is an unrolled chain of
       hundreds of distinct PCs and the per-PC table above hides it behind its own detail. */
    const byFn = new Map();
    for (const [pc, e] of fillPC.entries()) {
        const fn = nameOf(pc).split("+")[0];
        let f = byFn.get(fn);
        if (!f) byFn.set(fn, (f = { n: 0, changed: 0, pcs: 0, lo: 999, hi: -1,
                                    cells: new Set(),
                                    perLine: new Uint32Array(LINES * ROWS),
                                    perLineChanged: new Uint32Array(LINES * ROWS) }));
        f.n += e.n; f.changed += e.changed; f.pcs++;
        for (let y = 0; y < f.perLine.length; y++) {
            f.perLine[y] += e.perLine[y];
            f.perLineChanged[y] += e.perLineChanged[y];
        }
        for (const y of e.lines) { if (y < f.lo) f.lo = y; if (y > f.hi) f.hi = y; }
        for (const c of e.cells) f.cells.add(c);
    }
    /* ⚠⚠ EVERY ROUTINE, NOT A TOP-N SLICE.  A `slice(0, 16)` here hid `vdu_char_emit` — 32
       stores in 200 frames, i.e. the cheapest writer on the screen and therefore the one whose
       rows are most worth OWNING — and a narrower `--fill` window had already reported it.  A
       census that ranks by volume buries exactly the finding an ownership ledger is looking for. */
    /* A compact "0,1,38,39" / "3..34" rendering: a writer split between the two screen edges is
       a DIFFERENT shape from one covering the middle, and a bare lo..hi hides exactly that. */
    // eslint-disable-next-line no-var
    var cellSpan = (set) => {
        const cs = [...set].filter((c) => c >= 0).sort((a, b) => a - b);
        if (!cs.length) return "-";
        if (cs.length <= 6) return cs.join(",");
        return `${cs[0]}..${cs[cs.length - 1]} (${cs.length})`;
    };
    console.log(`\n⭐⭐ ROLLED UP PER ROUTINE (stores per frame over ${nWin} frames):`);
    const ranked = [...byFn.entries()].sort((a, b) => b[1].n - a[1].n);
    for (const [fn, f] of ranked) {
        console.log(`   ${fn.padEnd(24)} ${String((f.n / nWin).toFixed(f.n / nWin < 10 ? 2 : 0)).padStart(7)} stores/frame  ` +
            `${String((f.changed / nWin).toFixed(f.changed / nWin < 10 ? 2 : 0)).padStart(7)} changes/frame  ` +
            `lines ${f.lo}..${f.hi}  cells ${cellSpan(f.cells)}  (${f.pcs} PCs)`);
    }
    /* ⭐⭐⭐ AND THE RECTANGLE EACH ONE OCCUPIES, which is what a per-frame re-expand costs.
       A writer's price to an owned block is not its store count — the painter re-expands its
       BOUNDING BOX from mem[] once a painted frame, so the number that matters is
       (lines x cells) bytes at ~13.6 cyc, and a writer with a tall thin box is cheap however
       often it fires.  (docs/span-render-plan.md §12c.) */
    console.log(`\n⭐⭐⭐ THE RE-EXPAND RECTANGLE PER ROUTINE — (lines x cells) bytes a painted frame:`);
    for (const [fn, f] of ranked) {
        const cs = [...f.cells].filter((c) => c >= 0).sort((a, b) => a - b);
        if (!cs.length || f.hi < f.lo) continue;
        const nl = f.hi - f.lo + 1, nc = cs[cs.length - 1] - cs[0] + 1;
        console.log(`   ${fn.padEnd(24)} ${String(nl).padStart(3)} lines x ${String(nc).padStart(2)} cells ` +
            `= ${String(nl * nc).padStart(5)} bytes  ~${(nl * nc * 13.6 / 7093).toFixed(3)} ms/frame ` +
            `(vs ${(nl * 544 / 7093).toFixed(3)} ms of decode those lines cost)`);
    }

    /* ⭐⭐⭐ THE OWNERSHIP LEDGER — DISPLAY LINES GROUPED BY THEIR *WRITER SET*.
       docs/span-render-plan.md §11 prices the decode per display row and the only thing that
       decides whether a row can be OWNED by a direct-to-bitplane painter is which routines write
       it: own a row and every one of its writers has to be retargeted, so a block with one cheap
       writer is worth more than a block with a big decode cost and five.  A lo..hi range per
       routine cannot answer that — this can, and it is the same walk that produces §11's table.
       ⚠ A LINE WITH NO WRITER IS THE CHEAPEST ROW ON THE SCREEN: its pixels are already in the
       bitplanes from the frame the picture was built, so owning it costs a claim and nothing else.
       ⚠⚠ But "no writer IN THIS WINDOW" is not "no writer": this is a driving Silverstone
       practice lap, so a routine that only runs at a lap boundary, on a gear change, in the pits
       or in a RACE is absent by construction.  Widen --fill-frames and re-run before owning a
       block on the strength of a zero here. */
    {
        const setOf = (y) => {
            const w = [];
            for (const [fn, f] of ranked) if (f.perLine[y]) w.push(fn);
            return w;
        };
        const storesAt = (y) => { let n = 0; for (const [, f] of ranked) n += f.perLine[y]; return n; };
        const changesAt = (y) => { let n = 0; for (const [, f] of ranked) n += f.perLineChanged[y]; return n; };
        console.log(`\n⭐⭐⭐ THE OWNERSHIP LEDGER — display lines grouped by their WRITER SET ` +
            `(stores and changes per frame over ${nWin} frames):`);
        const H = LINES * ROWS;
        let y0 = 0;
        for (let y = 1; y <= H; y++) {
            const a = y < H ? setOf(y).join(",") : "\u0000";
            if (a === setOf(y0).join(",")) continue;
            let st = 0, ch = 0;
            for (let k = y0; k < y; k++) { st += storesAt(k); ch += changesAt(k); }
            const w = setOf(y0);
            console.log(`   lines ${String(y0).padStart(3)}..${String(y - 1).padStart(3)} ` +
                `(${String(y - y0).padStart(3)})  ${String((st / nWin).toFixed(1)).padStart(7)} st/f ` +
                `${String((ch / nWin).toFixed(1)).padStart(7)} ch/f  ` +
                `${w.length ? w.join(" + ") : "⭐ NO WRITER AT ALL"}`);
            y0 = y;
        }
    }
    /* ⭐⭐ WHERE ON THE SCREEN, per display line — because "lines 81..157" is a RANGE, and a
       renderer's shape is in the distribution.  A routine that paints a 3D view puts most of its
       stores near the horizon and tails off; one that maintains a cockpit is flat.  This is the
       input §3's layout choice actually needs, and a lo..hi pair cannot carry it. */
    if (fillReads) {
        const byFnR = new Map();
        for (const [pc, e] of readPC.entries()) {
            const fn = nameOf(pc).split("+")[0];
            let f = byFnR.get(fn);
            if (!f) byFnR.set(fn, (f = { n: 0, lo: 999, hi: -1, cells: new Set() }));
            f.n += e.n;
            for (const y of e.lines) { if (y < f.lo) f.lo = y; if (y > f.hi) f.hi = y; }
            for (const c of e.cells) f.cells.add(c);
        }
        console.log(`\n⭐⭐ WHO READS THE FRAME BUFFER BACK — ${fbReads} reads ` +
            `(${(fbReads / nWin).toFixed(0)}/frame) from ${readPC.size} PCs.  A WRITE-ONLY region ` +
            `can be plotted straight to bitplanes; a region read back is STATE:`);
        if (!byFnR.size) console.log("   (none — the flagged lines are write-only)");
        for (const [fn, f] of [...byFnR.entries()].sort((a, b) => b[1].n - a[1].n))
            console.log(`   ${fn.padEnd(24)} ${String((f.n / nWin).toFixed(f.n / nWin < 10 ? 2 : 0)).padStart(7)} reads/frame` +
                `  lines ${f.lo}..${f.hi}  cells ${cellSpan(f.cells)}`);
        /* ⚠ THE PC MATTERS MORE THAN THE COUNT HERE: the 6502's `STA (zp),Y` performs a DUMMY
           READ of the un-carried address before it writes, and jsbeeb models it, so a store site
           shows up as a reader.  Print the PCs so the opcode at each can be checked against the
           listing instead of the totals being believed. */
        for (const [pc, e] of [...readPC.entries()].sort((a, b) => b[1].n - a[1].n).slice(0, 12)) {
            const ls = [...e.lines].sort((a, b) => a - b);
            console.log(`     PC $${pc.toString(16).padStart(4, "0")} ${String(e.n).padStart(6)} reads` +
                `  lines ${ls.length > 6 ? ls[0] + ".." + ls[ls.length - 1] : ls.join(",")}` +
                `   ${nameOf(pc)}   first addrs ` +
                e.sample.map((a) => "$" + a.toString(16)).join(","));
        }
        for (const [fn, f] of [...byFnR.entries()].sort((a, b) => b[1].n - a[1].n).slice(0, 12))
            console.log(`   ${fn.padEnd(24)} ${String((f.n / nWin).toFixed(0)).padStart(6)} reads/frame` +
                `   lines ${f.lo}..${f.hi}`);
    }
    for (const [fn, f] of ranked.slice(0, 3)) {
        const parts = [];
        for (let y = 0; y < f.perLine.length; y += 8) {
            let s = 0;
            for (let k = 0; k < 8 && y + k < f.perLine.length; k++) s += f.perLine[y + k];
            if (s) parts.push(`${y}:${(s / nWin).toFixed(0)}`);
        }
        console.log(`\n   ${fn} — stores per frame by display line (8-line buckets):\n     ` +
            parts.join("  "));
    }
}

// ── the measured band schedule ─────────────────────────────────────────────────────────────
if (bandFrames.length) {
    // ⚠ video.bitmapY is a framebuffer row in a 625-row buffer — DOUBLED scanlines — and the
    // first DISPLAYED line sits at row 112 (the top border above it is blank).  So
    //     display line = (bitmapY - 112) / 2
    // Reporting raw bitmapY makes the numbers unrecognisable and uncomparable to the port.
    const FB_FIRST_DISPLAY_ROW = 112;
    const toDisplayLine = (y) => (y - FB_FIRST_DISPLAY_ROW) / 2;
    const f = bandFrames[bandFrames.length - 1];
    // A band boundary is one burst of palette writes at one raster position; the burst can
    // straddle a framebuffer row, so cluster writes within a few rows into one boundary.
    const bounds = [];
    for (const w of f.writes) {
        const at = bounds[bounds.length - 1];
        if (at && w.line - at.line <= 4) {
            at.n++;
            if (w.reg === "MODE") at.mode = w.val;
        } else bounds.push({ line: w.line, n: 1, mode: w.reg === "MODE" ? w.val : null });
    }
    console.log("\nMEASURED BAND SCHEDULE — the real 6845 + Video ULA, one captured field.");
    console.log("  This is the half src/platform/bbc_screen.h could only DERIVE from the binary:");
    const NOMINAL = [-26.4, 18.0, 81.1, 100.5, 166.1];
    const WHAT = ["band 0 top text rows", "band 1 SKY (flat blue, live code hides here)",
        "band 2 HORIZON (pen 0 = black)", "band 3 track", "band 4 dashboard"];
    bounds.forEach((b, i) => {
        const dl = toDisplayLine(b.line);
        const nom = NOMINAL[i];
        const mode = b.mode === 0x88 ? " MODE 4" : b.mode === 0xc4 ? " MODE 5" : "       ";
        console.log(`   display line ${String(dl).padStart(6)}${mode}  ${b.n} writes   ` +
            (nom === undefined ? "" : `derived ${String(nom).padStart(6)}  ` +
                `${Math.abs(dl - nom) <= 1.5 ? "✓ matches" : "⚠ DIFFERS"}   ${WHAT[i]}`));
    });
    const ok = bounds.length >= NOMINAL.length &&
        NOMINAL.every((n, i) => Math.abs(toDisplayLine(bounds[i].line) - n) <= 1.5);
    console.log(ok
        ? "  ⭐ Every band boundary matches the derived table — the port's BAND MODEL is confirmed\n" +
          "     against real hardware, so the stripes are not a band-phase error."
        : "  ⚠ A boundary differs from the derived table — the band model itself is suspect.");
}

// ── the stripes verdict ───────────────────────────────────────────────────────────────────
if (skySamples.length) {
    const skyZ = skySamples.map((s) => s.sky.zeros);
    const skyR = skySamples.map((s) => s.sky.longest);
    const gndZ = skySamples.map((s) => s.gnd.zeros);
    const sum = (a) => a.reduce((x, y) => x + y, 0);
    const mean = (a) => (sum(a) / a.length).toFixed(1);
    const distinct = new Set(skySamples.map((s) => s.sum)).size;
    console.log(`\nZERO-BYTE SCAN over ${skySamples.length} race frames (Silverstone practice):`);
    console.log(`  distinct pictures among those frames: ${distinct}` +
        (distinct === 1
            ? "  ⚠ THE SCENE NEVER CHANGED — the car is PARKED, so this is ground truth for a static view only"
            : ""));
    console.log(`  band 1  sky      lines 24-80  of ${skySamples[0].skyBand.total}: ` +
        `mean=${mean(skySamples.map((s) => s.skyBand.zeros))}  (invisible — flat-blue palette, live code lives here)`);
    console.log(`  band 2  HORIZON  lines 81-100 of ${skySamples[0].sky.total}: ` +
        `min=${Math.min(...skyZ)} max=${Math.max(...skyZ)} mean=${mean(skyZ)}; ` +
        `longest zero run max=${Math.max(...skyR)} cells   <- pen 0 is BLACK here`);
    console.log(`  band 3  track    lines 101-165 of ${skySamples[0].gnd.total}: ` +
        `min=${Math.min(...gndZ)} max=${Math.max(...gndZ)} mean=${mean(gndZ)}  (the road itself is pen 0)`);
    console.log(Math.max(...skyZ) === 0
        ? "\n  ⭐⭐ THE REAL BBC NEVER LEAVES A BYTE ZERO IN THE HORIZON BAND, on any sampled frame.\n" +
          "      The port's black horizontal runs above the horizon are a PORT BUG — something the\n" +
          "      real game fills is not being filled."
        : `\n  the real BBC DOES leave horizon bytes zero (up to ${Math.max(...skyZ)}, longest run ` +
          `${Math.max(...skyR)} cells) — compare the port's count against this before blaming the port.`);
} else if (frames > 0) {
    console.log("\n(no zero-byte samples: the race ran fewer than the 8 warm-up frames the scan skips)");
}

// ── the engine's sound, as the MOS actually played it ─────────────────────────────────────
if (soundOut) {
    const dir = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..", path.dirname(soundOut));
    fs.mkdirSync(dir, { recursive: true });
    const file = path.join(dir, path.basename(soundOut));
    fs.writeFileSync(file, JSON.stringify({ cmds: soundCmds, writes: soundChip.writes }, null, 1));
    // ...and the tick-quantised fixture `make sound` replays through the port's own scheduler.
    const fixture = file.replace(/\.json$/, "") + "_events.txt";
    const q = writeFixture(fixture, soundCmds, soundChip.writes, fs);
    const sevens = soundCmds.filter((c) => c.osword === 7);
    console.log(`\nSOUND CAPTURE: ${sevens.length} OSWORD 7, ${soundCmds.length - sevens.length} OSWORD 8, ` +
        `${soundChip.writes.length} chip writes -> ${file}`);
    console.log(`  fixture: ${fixture}  (${q.events} events over ${q.ticks} ticks, ` +
        `worst grid residual ${q.worstResidualCycles} cycles)`);
    if (!sevens.length)
        console.log("  ⚠ NOT ONE SOUND COMMAND.  The engine was never started (--drive) or it never revved:\n" +
            "     this run is silence, not evidence about sound.");
    else {
        const byChan = new Map();
        for (const c of sevens) {
            const ch = c.bytes[0] & 3;
            const e = byChan.get(ch) || { n: 0, pitches: new Set(), amps: new Set(), durs: new Set() };
            e.n++;
            e.pitches.add(c.bytes[4]);
            e.amps.add(c.bytes[2] | (c.bytes[3] << 8));
            e.durs.add(c.bytes[6]);
            byChan.set(ch, e);
        }
        for (const [ch, e] of [...byChan].sort()) {
            const p = [...e.pitches].sort((a, b) => a - b);
            console.log(`  BBC ch${ch}: ${e.n} commands  pitch ${p[0]}..${p[p.length - 1]} (${p.length} distinct)  ` +
                `amp {${[...e.amps].map((a) => (a & 0x8000 ? a - 0x10000 : a)).join(",")}}  ` +
                `duration {${[...e.durs].join(",")}}`);
        }
        const last = soundChip.writes[soundChip.writes.length - 1];
        console.log(`  final chip state: tone=[${last.tone}] noise=${last.noise} vol=[${last.vol}]`);
    }
}

// ── ground truth out ──────────────────────────────────────────────────────────────────────
if (traceEdge) {
    console.log(`\nedge points emitted at $23C0 during frame ${memAtFrame} (${traceEdgeLines.length}):`);
    for (const l of traceEdgeLines) console.log(l);
}

if (peekAddrs) {
    console.log(`\nper-frame values of ${peekAddrs.map((a) => "$" + a.toString(16)).join(", ")} from frame ${memAtFrame} on:`);
    for (const [k, n] of [...peekHist.entries()].sort((x, y) => y[1] - x[1]))
        console.log(`   ${k}   x${n}`);
}

if (watchAddr !== null || watchLo !== null) {
    const what = watchAddr !== null ? `$${watchAddr.toString(16)}`
                                    : `$${watchLo.toString(16)}-$${watchHi.toString(16)}`;
    console.log(`\nwrites to ${what} from frame ${memAtFrame} on, by the PC that made them:`);
    if (watchPCs.size === 0) console.log("   NONE — nothing wrote it in the window");
    for (const [pc, w] of [...watchPCs.entries()].sort((x, y) => y[1].n - x[1].n))
        console.log(`   $${pc.toString(16).padStart(4, "0")}  ${w.n} writes  values ` +
            [...w.vals.entries()].sort((x, y) => y[1] - x[1]).slice(0, 6)
                .map(([v, n]) => `$${v.toString(16).padStart(2, "0")}x${n}`).join(" ") +
            (w.addrs ? `   cells ${[...w.addrs].sort((a, b) => a - b)
                .map((a) => "$" + a.toString(16)).join(",")}` : ""));
}

if (dumpDir && frames > 0) {
    // ⚠ Paths inside these scripts are relative to the SCRIPT, not the cwd, because they must
    // be run from tools/jsbeeb — "../tmp" is the repo's tmp, "../../tmp" silently writes into
    // ~/Documents/tmp and the run still looks successful.
    const dir = path.resolve(path.dirname(new URL(import.meta.url).pathname), "..", dumpDir);
    fs.mkdirSync(dir, { recursive: true });
    const tag = String(frames).padStart(6, "0");

    // (1) The BBC frame buffer as the game wrote it — exactly the bytes src/platform/bbc_screen.h
    // models, so the port's own decode can be diffed against the real thing byte for byte.
    const BASE = 0x5a80, LEN = 0x2580;
    const buf = Buffer.alloc(LEN);
    for (let i = 0; i < LEN; i++) buf[i] = rd(BASE + i);
    const rawF = path.join(dir, `bbc_fb_${tag}.bin`);
    fs.writeFileSync(rawF, buf);

    // (2) What the real 6845 + Video ULA actually PUT ON THE SCREEN, as a PPM.  This is the
    // half no memory dump can give: palette, band boundaries and scanline doubling included,
    // which is precisely where a stripe artefact would live.
    const ppm = Buffer.alloc(FB_W * FB_H * 3);
    for (let i = 0, o = 0; i < FB_W * FB_H; i++, o += 3) {
        ppm[o] = completeFb8[i * 4];
        ppm[o + 1] = completeFb8[i * 4 + 1];
        ppm[o + 2] = completeFb8[i * 4 + 2];
    }
    const ppmF = path.join(dir, `bbc_screen_${tag}.ppm`);
    fs.writeFileSync(ppmF, Buffer.concat([Buffer.from(`P6\n${FB_W} ${FB_H}\n255\n`), ppm]));

    // (3) ⭐ THE WHOLE 64 KB, the exact counterpart of the port's REVS_MEM_DUMP.  A frame-buffer
    // diff can say the picture is wrong but never why: `make viewdiff` reported Brands Hatch's
    // horizon 13 display lines too high and there was no way to ask the real machine what its
    // own horizon cells held at that moment.  Dumping everything turns "the picture differs"
    // into "cell $NN differs", which is a localisation rather than a search.
    // ⚠ Read it against a port dump taken in the SAME parked state, and only cells the game
    // actually owns — page 1, the OS workspace and the MOS's own scratch differ by construction.
    const memBuf = Buffer.alloc(0x10000);
    for (let i = 0; i < 0x10000; i++) memBuf[i] = rd(i);
    const memF = path.join(dir, `bbc_mem_${tag}.bin`);
    fs.writeFileSync(memF, memBuf);

// ── ⭐⭐⭐ THE REAL BBC'S OWN FRAME COST ────────────────────────────────────────────────────
// Reported as a MEDIAN over the settled window, not a mean: the engine's crash/reset holds and
// the first frames after the drive-in are outliers of a different workload, exactly as phase 0
// is on the Amiga side (docs/perf-method.md §the dash-edge walk).
if (frameCycles.length > 12) {
    const warm = frameCycles.slice(8);            // skip the drive-in, as the zero-byte scan does
    const gaps = [];
    for (let i = 1; i < warm.length; i++) gaps.push(warm[i] - warm[i - 1]);
    gaps.sort((a, b) => a - b);
    const med = gaps[gaps.length >> 1];
    const mean = gaps.reduce((a, b) => a + b, 0) / gaps.length;
    const hz = tm.processor.model.cyclesPerSecond;   // never hardcode the clock — ask the model
    const ms = (c) => ((c * 1000) / hz).toFixed(1);
    console.log(`\nTHE REAL BBC'S FRAME COST over ${gaps.length} settled frames at $1701:`);
    console.log(`   median ${med} cycles = ${ms(med)} ms = ${(hz / med).toFixed(2)} fps` +
                `   (mean ${ms(mean)} ms, p10 ${ms(gaps[Math.floor(gaps.length * 0.1)])}, ` +
                `p90 ${ms(gaps[Math.floor(gaps.length * 0.9)])})`);
    console.log(`   ⭐ This is the number the Amiga port's bracketed frame must be quoted against.`);

    // ── the per-routine share of that frame ───────────────────────────────────────────────
    if (profileOn) {
        const med = (a) => { const b = a.slice().sort((x, y) => x - y); return b[b.length >> 1]; };
        const frameMed = med(gaps);
        console.log(`\nWHERE THAT FRAME GOES — per main-loop CALL SITE, subtree cycles, over the same window:`);
        console.log(`   ${"phase routine".padEnd(28)} ${"n/f".padStart(4)} ${"median cyc".padStart(10)}` +
                    ` ${"med ms".padStart(6)} ${"% frame".padStart(8)} ${"MEAN ms".padStart(8)}`);
        let sum = 0, sumMean = 0;
        for (const st of profState.values()) {
            const per = st.per.slice(8), cl = st.calls.slice(8);
            if (!per.length) continue;
            const m = med(per), c = med(cl);
            /* ⚠ the MEAN is the column to compare against the port's phase row (which is a mean):
               a median reads a routine that works on under half its frames as its idle cost. */
            const mean = per.reduce((a, b) => a + b, 0) / per.length;
            if (st.ph) { sum += m; sumMean += mean; }
            console.log(`   ${(st.ph ? "ph" + String(st.ph).padStart(2) + " " : "     ") + st.name.padEnd(24)}` +
                        ` ${String(c).padStart(4)} ${String(m).padStart(10)}` +
                        ` ${ms(m).padStart(6)} ${((m / frameMed) * 100).toFixed(1).padStart(7)}%` +
                        ` ${ms(mean).padStart(8)}`);
        }
        console.log(`   Σ of the site medians = ${ms(sum)} ms against the frame's ${ms(frameMed)} —` +
                    ` the rest is the $1760 frame wait and the loop code between calls.`);
        console.log(`   Σ of the site MEANS = ${ms(sumMean)} ms.`);
        console.log(`   ⚠ PORT-COMPARABLE: the MEAN column, and exclude the delay pad (the port does not reproduce it).`);
        console.log(`   ⚠ An interrupt taken inside a routine is charged to it, exactly as the`);
        console.log(`     Amiga side charges the VERTB ISR to whatever phase it preempted.`);
    }

    // The calibration, printed beside it so the figure is never read without it.
    if (paintCycles.length > 20) {
        const pg = [];
        for (let i = 1; i < paintCycles.length; i++) pg.push(paintCycles[i] - paintCycles[i - 1]);
        pg.sort((a, b) => a - b);
        const pmed = pg[pg.length >> 1];
        const want = Math.round(hz * 312 * 64e-6);
        const off = Math.abs(pmed - want) / want;
        console.log(`   calibration: one PAL field measures ${pmed} cycles, a 312x64us field is ` +
                    `${want} — ${(off * 100).toFixed(2)}% off  ` +
                    (off < 0.02 ? "✓" : "⚠ INSTRUMENT SUSPECT"));
    }
}

    console.log(`\nground truth written (${paints} frames painted by the real Video chip):`);
    console.log(`   frame buffer $${BASE.toString(16)}+$${LEN.toString(16)} -> ${rawF}`);
    console.log(`   real display ${FB_W}x${FB_H} RGB   -> ${ppmF}`);
    console.log(`   whole 64 KB                 -> ${memF}`);

    if (memAt !== null) {
        if (memAtSnapshot === null) {
            console.log(`   ⚠ --mem-at=$${memAt.toString(16)} NEVER REACHED (frame >= ${memAtFrame}) — no snapshot`);
        } else {
            const atF = path.join(dir, `bbc_memat_${memAt.toString(16).padStart(4, "0")}.bin`);
            fs.writeFileSync(atF, memAtSnapshot);
            console.log(`   64 KB at $${memAt.toString(16)} (frame ${memAtFrames}) -> ${atF}`);
        }
    }
}
