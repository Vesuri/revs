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
const drive = argv.includes("--drive");
const wing = String(opt("wing", "20")); // rear and front wing, 0-40 (the game has no default)
// --fill=lo-hi : attribute every frame-buffer write in those DISPLAY LINES to the routine
// that made it.  This is how you find the fill loop the port is failing to run: the real BBC
// writes these bytes, so whoever writes them is the code to compare against.
const fillArg = opt("fill", null);
// --irq-abi : measure, on real hardware, WHICH REGISTERS SURVIVE AN INTERRUPT.
// The port's ISR shim has to reproduce this contract exactly.  It already got A wrong once
// (irq1v_handler restores A from $FC, which only the MOS's entry ever writes), and the way to
// find the rest is to measure the contract rather than read the handler and reason about it.
const irqAbi = argv.includes("--irq-abi");

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
const FB_W = 1024, FB_H = 625;
const fb8 = new Uint8Array(FB_W * FB_H * 4);
const fb32 = new Uint32Array(fb8.buffer);
const completeFb8 = new Uint8Array(FB_W * FB_H * 4);
let paints = 0;
const video = new Video(false, fb32, function () {
    paints++;
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
await tm.initialise();
if (soundChip) {
    attachSoundCapture(tm, soundChip);
    soundChip.tag = () => ({ frame: frames });
    hookOswordSound(tm, soundChip, soundCmds);
}
tm.loadDiscData(new Uint8Array(data));
tm.startCapture();
const rd = (a) => tm.processor.readmem(a);

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
const FB_BASE = 0x5a80, CELLS = 40, LINES = 8, BPR = CELLS * LINES;
function scanZeros(lo, hi) {
    let zeros = 0, total = 0, longest = 0;
    for (let y = lo; y < hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        let run = 0;
        for (let c = 0; c < CELLS; c++) {
            const b = rd(FB_BASE + row * BPR + c * LINES + line);
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
            h = ((h ^ rd(FB_BASE + row * BPR + c * LINES + line)) * 16777619) >>> 0;
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
            // Every 4th frame, because the sky/track split moves with the hills
            // (MoveHorizon, $4F44) — one frame proves nothing either way.
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
// bands per field written by irq1v_handler ($4E5C) and timed by the User VIA T1 latch.
// src/platform/bbc_screen.h DERIVES that schedule from the binary; this MEASURES it on the
// real thing, which is the missing half.
//
// ⭐ It matters for the stripes specifically because band 2's boundary MOVES EVERY FRAME —
// MoveHorizon ($4F44) shifts it with the hills — so "lines 81..100" is only ever an
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
const fillPC = new Map(); // pc -> {n, lines:Set}
let fillWrites = 0;
if (fillArg) {
    const [lo, hi] = fillArg.split("-").map(Number);
    for (let y = lo; y <= hi; y++) {
        const row = (y / LINES) | 0, line = y % LINES;
        for (let c = 0; c < CELLS; c++) {
            const a = FB_BASE + row * BPR + c * LINES + line;
            fillFlags[a] = 1;
            fillLineOf[a] = y;
        }
    }
    console.log(`fill attribution armed for display lines ${lo}..${hi}\n`);
}

// ── the interrupt register contract, measured ─────────────────────────────────────────────
// Catch the CPU at the OS's IRQ entry (the address in $FFFE/$FFFF), where A/X/Y are still the
// INTERRUPTED program's, read the return address off the 6502 stack the sequence just pushed,
// and compare A/X/Y again when execution arrives back there.  Whatever fails to match is a
// register the foreground must not rely on across an interrupt — and therefore a register the
// port's shim must treat exactly the same way.
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

const ULA_CTRL = 0xfe20, ULA_PAL = 0xfe21;
const bandFrames = []; // one entry per captured field: the writes and the line they landed on
let curBand = null;
tm.processor.debugWrite.add((addr, b) => {
    if (fillArg && frames > 8 && frames < 24 && fillFlags[addr]) {
        // ⚠ Only a few frames' worth: this fires on every frame-buffer write in the window
        // and the point is to name the routine, not to measure it.
        const pc = tm.processor.pc;
        let e = fillPC.get(pc);
        if (!e) fillPC.set(pc, (e = { n: 0, lines: new Set(), nonzero: 0 }));
        e.n++;
        e.lines.add(fillLineOf[addr]);
        if (b !== 0) e.nonzero++;
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
while (spent < deadline && frames === 0) {
    const e0 = menuEntries, a0 = numAsks;
    await tm.runFor(400000);
    spent += 400000;

    for (const t of transcript.splice(0))
        if (t.msg !== MSG_SPACEBAR) console.log(`  [$${t.msg.toString(16).padStart(2, "0")}] ${t.text}`);

    if (numAsks > a0 || (rdchHits > 0 && numValidations < numAsks)) {
        // The numeric line editor is open.  On the practice path this is the wing settings,
        // rear then front.
        if (!(await answerNumber(wing, `wing setting ${++wingsAsked === 1 ? "rear" : "front"}`))) break;
        spent += 2 * CPS;
    } else if (menuEntries > e0 || (menuPolls > 0 && menuAnswers < menuEntries)) {
        if (!(await answerMenu(1, "take the first option"))) break;
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
if (drive) {
    const gear0 = rd(0x40);
    const cranked = await holdUntil(utils.BBC.T, () => rd(ENGINE_ON) === 0xff,
        "'T' held but the engine never caught ($61 stayed 0)", 8 * CPS);
    console.log(`   starter: engine-running $61 = $${rd(ENGINE_ON).toString(16)} ${cranked ? "✓" : "✗"}`);
    // The gear change is debounced through $19: it is only accepted when $19 is 0, which
    // happens on a frame where neither gear key is held — so release matters as much as press.
    const geared = await holdUntil(utils.BBC.Q, () => rd(0x40) !== gear0,
        "'Q' held but the gear at $40 never changed", 4 * CPS);
    console.log(`   first gear: $40 ${gear0} -> ${rd(0x40)} ${geared ? "✓" : "✗"}`);
    tm.processor.sysvia.keyDownRaw(utils.BBC.S); // hold the throttle down from here
}

const f0 = frames;
let waited = 0;
while (frames - f0 < wantFrames && waited < 60 * CPS) {
    await tm.runFor(CPS);
    waited += CPS;
    if (waited % (10 * CPS) === 0) console.log(`   t=${waited / CPS}s frames=${frames - f0}/${wantFrames}`);
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
        0x9d: "SPACE amplify-steering", 0x9f: "TAB gear-down", 0xa6: "DELETE",
        0xa8: "'+' steer-right", 0xa9: "'L' steer-left", 0xae: "'S' throttle",
        0xb6: "RETURN", 0xbe: "'A' brake", 0xdc: "'T' starter", 0xef: "'Q' gear-up",
        0xff: "SHIFT", 0x86: "RIGHT",
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

// ── who filled those lines ────────────────────────────────────────────────────────────────
if (fillArg && fillWrites) {
    // Name the PCs from disasm/symbols.csv — the nearest preceding symbol, so a write from
    // the middle of a routine still lands on that routine.
    const syms = [];
    try {
        const csv = fs.readFileSync(new URL("../disasm/symbols.csv", import.meta.url), "utf8");
        for (const line of csv.split("\n")) {
            const m = line.match(/^0x([0-9A-Fa-f]{4}),([^,]+),/);
            if (m) syms.push([parseInt(m[1], 16), m[2]]);
        }
        syms.sort((a, b) => a[0] - b[0]);
    } catch { /* names are a convenience; the addresses are the finding */ }
    const nameOf = (pc) => {
        let best = null;
        for (const [a, n] of syms) { if (a <= pc) best = [a, n]; else break; }
        return best ? `${best[1]}${best[0] === pc ? "" : "+" + (pc - best[0])}` : "?";
    };
    const rows = [...fillPC.entries()].sort((a, b) => b[1].n - a[1].n);
    console.log(`\n⭐ WHO FILLS DISPLAY LINES ${fillArg} ON A REAL BBC — ${fillWrites} writes ` +
        `from ${rows.length} distinct PCs over frames 9..23:`);
    for (const [pc, e] of rows.slice(0, 14)) {
        const ls = [...e.lines].sort((a, b) => a - b);
        console.log(`   PC $${pc.toString(16).padStart(4, "0")}  ${String(e.n).padStart(6)} writes ` +
            `(${e.nonzero} non-zero)  lines ${ls.length > 8 ? ls[0] + ".." + ls[ls.length - 1] : ls.join(",")}` +
            `   ${nameOf(pc)}`);
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

    console.log(`\nground truth written (${paints} frames painted by the real Video chip):`);
    console.log(`   frame buffer $${BASE.toString(16)}+$${LEN.toString(16)} -> ${rawF}`);
    console.log(`   real display ${FB_W}x${FB_H} RGB   -> ${ppmF}`);
}
