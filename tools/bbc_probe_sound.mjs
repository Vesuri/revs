// ⭐ GROUND TRUTH FOR SOUND — what the MOS's sound scheduler actually writes to the SN76489,
// for the exact command shapes Revs issues.
//
//   cd tools/jsbeeb && volta run --node 24.15.0 -- node ../bbc_probe_sound.mjs [--dump=DIR]
//        [--only=pitch,amp,noise,env,revs]
//
// ── Why this probe exists ─────────────────────────────────────────────────────────────────
//
// Revs never addresses the sound chip.  Every note is an `OSWORD 7` (SOUND) command block and
// one `OSWORD 8` (ENVELOPE) definition (`sound_queue` $0B4A / `sound_envelope` $0B65), so the
// port has to reproduce the MOS's *scheduler*: the pitch→divider mapping, the amplitude→
// attenuation mapping, the noise-mode encoding, and the 100 Hz envelope stepper.  None of that
// is in the game binary, so it cannot be recovered by disassembling Revs — it has to be
// measured off a real MOS, which is what this does.
//
// It captures TWO streams and pairs them:
//
//   1. every OSWORD 7 / OSWORD 8 the machine issues, with its control block  — the INPUT, and
//   2. every write the MOS makes to the SN76489, decoded into chip state     — the OUTPUT.
//
// `tools/validate_sound.c` replays stream 1 through `src/platform/sound.c` and diffs stream 2.
//
// ⚠ Provenance: this records the OS's externally-specified BEHAVIOUR (SOUND/ENVELOPE are
// documented user interfaces, and the SN76489 is a published chip), never ROM bytes.  The
// pitch mapping is checked against the equal-tempered formula in the report below precisely so
// the port can ship the formula rather than a lifted table (docs/reference-sources.md).
//
// ⚠ The sweeps must PACE THEMSELVES.  The MOS starts a flushed sound on its next 100 Hz tick,
// so a tight `FOR P=0 TO 255:SOUND...:NEXT` outruns the scheduler and most pitches never reach
// the chip: the fixture would be short, plausible, and wrong.  Each step waits 4 centiseconds
// on TIME, and the probe asserts it got one chip programming per command.

import { TestMachine } from "./jsbeeb/tests/test-machine.js";
import { CaptureSoundChip, attachSoundCapture, hookOswordSound, writeFixture } from "./bbc_sound_capture.mjs";
import fs from "fs";
import path from "path";

const CPS = 2 * 1000 * 1000;

const argv = process.argv.slice(2);
const opt = (name, dflt) => {
    const hit = argv.find((a) => a.startsWith(`--${name}=`));
    return hit === undefined ? dflt : hit.slice(name.length + 3);
};
const dumpDir = opt("dump", null);
const only = opt("only", "pitch,amp,chan,noise,env,dur,flush,revs").split(",");

// fake6502 takes the sound chip through opts, so build the machine with ours in place.
const chip = new CaptureSoundChip();
const M = new TestMachine("B-DFS1.2", { soundChip: chip });
await M.initialise();
attachSoundCapture(M, chip);

const cmds = [];
hookOswordSound(M, chip, cmds);

await M.runUntilInput(20);
M.startCapture();
M.drainText();

// One BASIC statement per step, paced on TIME so the 100 Hz scheduler sees every one.
const step = (body) => `${body}:T=TIME:REPEAT UNTIL TIME>=T+4`;

async function run(program, secs) {
    await M.type(program);
    await M.runFor(secs * CPS);
    return M.drainText();
}

const marks = [];
const mark = (name) => marks.push({ name, cycle: chip.clock(), cmd: cmds.length, write: chip.writes.length });

console.log("── sweeping the MOS sound scheduler ──");

if (only.includes("pitch")) {
    mark("pitch");
    // Channel 1 (the channel Revs's noise generator borrows its frequency from), amplitude -15,
    // duration 255 = play until superseded.  Flush (&11) so each pitch takes effect at once.
    await run(`FOR P%=0 TO 255:${step("SOUND &11,-15,P%,255")}:NEXT`, 20);
    console.log(`  pitch sweep: ${cmds.length} commands, ${chip.writes.length} chip writes`);
}

if (only.includes("amp")) {
    mark("amp");
    await run(`FOR A%=0 TO 15:${step("SOUND &11,-A%,100,255")}:NEXT`, 4);
}

if (only.includes("chan")) {
    mark("chan");
    // ⭐ THE SAME PITCH IS NOT THE SAME DIVIDER ON EVERY CHANNEL.  The MOS adds the channel
    // index, so BBC channels 1/2/3 at one pitch come out one divider count apart — deliberate
    // detuning, and the port has to reproduce it or two channels an "interval of 28" apart beat
    // differently from the real machine.  Measured here rather than assumed.
    for (const p of [128, 130, 200]) {
        for (const ch of [1, 2, 3]) await run(`SOUND &1${ch},-15,${p},255`, 0.5);
    }
    await run(`SOUND &11,0,0,1:SOUND &12,0,0,1:SOUND &13,0,0,1`, 1);
}

if (only.includes("noise")) {
    mark("noise");
    // Channel 0 is the noise channel; pitch 0..7 selects the mode.  Revs uses 3 (periodic,
    // clocked from channel 1) and 6.  Channel 1 is left playing pitch 100 from the sweep above
    // so mode 3's borrowed divider is identifiable.
    await run(`SOUND &11,0,100,255`, 2);
    await run(`FOR P%=0 TO 7:${step("SOUND &10,-15,P%,255")}:NEXT`, 4);
}

if (only.includes("env")) {
    mark("env");
    // ⭐ REVS'S OWN ENVELOPE, byte for byte from $0B38 in the runtime image:
    //   01 01 02 FE FA 04 01 01 0A 00 00 00 48 00
    // step 1 cs, pitch +2 x4 then -2 x1 then -6 x1 (no auto-repeat), attack +10 to level 72,
    // decay/sustain/release 0.  Played on channel 3 at pitch 130 exactly as $0B28 does.
    await run(`ENVELOPE 1,1,2,-2,-6,4,1,1,10,0,0,0,72,0`, 1);
    await run(`SOUND &13,1,130,255`, 3);
    mark("env-quiet");
    await run(`SOUND &13,0,130,1`, 2);
}

if (only.includes("dur")) {
    mark("dur");
    // Duration units are twentieths of a second = 5 centisecond ticks, and a static-amplitude
    // sound just stops.  Revs's one finite sound is the $0B30 noise burst, duration 4.
    await run(`SOUND &10,-15,6,4`, 2);
    mark("dur-release");
    // The release phase (AR) after the duration ends.  ⚠ Revs's envelope has AR = 0, so this
    // segment exists only to pin down a path the game never takes.
    await run(`ENVELOPE 3,1,0,0,0,1,1,1,20,0,0,-20,100,100`, 1);
    await run(`SOUND &13,3,130,4`, 3);
}

const flushStates = {};
if (only.includes("flush")) {
    mark("flush");
    // ⭐ Does OSBYTE 21 (flush buffer 4+channel) SILENCE a playing sound, or only empty the
    // queue?  This is load-bearing: `sound_stop_channel` ($0E5A) is the ONLY way Revs ever stops
    // the noise channel, and every one of its sounds has duration 255 = forever.  If flushing
    // merely emptied a queue the engine noise would never stop.
    // A timeline cannot answer this — the answer is a STATE, and the ~70 centiseconds of typing
    // between the two commands would put the flush's own write next to the sound's.  So snapshot.
    await run(`SOUND &10,-15,6,255`, 1);
    flushStates.playing = chip.state();
    await run(`*FX21,4`, 1);
    flushStates.flushed = chip.state();
    await run(`SOUND &11,-15,100,255`, 1);
    flushStates.tone = chip.state();
    await run(`*FX21,5`, 1);
    flushStates.toneFlushed = chip.state();
}

if (only.includes("revs")) {
    mark("revs");
    // The engine's steady state: two tones 28 pitch units apart on channels 1 and 2 plus the
    // periodic-noise channel 0 borrowing channel 1's divider.  Revs's own block templates
    // ($0B10-$0B2F) with a representative rev count.
    await run(`SOUND &11,-10,100,255:SOUND &12,-10,128,255:SOUND &10,-10,3,255`, 3);
    mark("revs-rev");
    await run(`FOR P%=100 TO 140 STEP 4:${step("SOUND &11,-10,P%,255")}:NEXT`, 6);
}

mark("end");

// ── report ────────────────────────────────────────────────────────────────────────────────
const soundCmds = cmds.filter((c) => c.osword === 7);
const envCmds = cmds.filter((c) => c.osword === 8);
console.log(`\nOSWORD 7: ${soundCmds.length}   OSWORD 8: ${envCmds.length}   chip writes: ${chip.writes.length}`);

// Pair each SOUND command with the chip state that followed it (the last write before the next
// command).  This is the table the port has to reproduce.
function pairs(fromCmd, toCmd) {
    const out = [];
    for (let i = fromCmd; i < toCmd; i++) {
        const c = cmds[i];
        if (c.osword !== 7) continue;
        const next = i + 1 < cmds.length ? cmds[i + 1].cycle : Infinity;
        let last = null;
        for (const w of chip.writes) if (w.cycle > c.cycle && w.cycle < next) last = w;
        out.push({ cmd: c, after: last });
    }
    return out;
}

const seg = (name) => {
    const i = marks.findIndex((m) => m.name === name);
    if (i < 0) return null;
    const next = marks[i + 1];
    return {
        from: marks[i].cmd,
        to: next ? next.cmd : cmds.length,
        wFrom: marks[i].write,
        wTo: next ? next.write : chip.writes.length,
    };
};

// A tone divider is programmed with two bytes (low nibble, then upper six bits), so half the
// writes hold a half-updated value.  Only the second of a pair is a real divider.
function dividerSequence(wFrom, wTo, chipReg) {
    const out = [];
    for (let i = wFrom; i < wTo; i++) {
        const w = chip.writes[i];
        if (w.byte & 0x90) continue; // volume, or the first byte of a pair
        if (!out.length || out[out.length - 1] !== w.tone[chipReg]) out.push(w.tone[chipReg]);
    }
    return out;
}

const chan = (b) => b & 3;
const amp = (lo, hi) => (hi & 0x80 ? (lo | (hi << 8)) - 0x10000 : lo | (hi << 8));

if (only.includes("pitch")) {
    const s = seg("pitch");
    const p = pairs(s.from, s.to);
    console.log(`\n── pitch → chip divider (channel 1 = chip tone ?, ${p.length} pairs) ──`);
    const table = [];
    for (const { cmd, after } of p) {
        const pitch = cmd.bytes[4];
        if (!after) {
            table.push([pitch, null]);
            continue;
        }
        // Which chip tone register moved?  Report all three the first time so the BBC-channel →
        // chip-channel mapping is measured, not assumed.
        table.push([pitch, after.tone.slice(), after.vol.slice()]);
    }
    const miss = table.filter((t) => t[1] === null).length;
    console.log(`  unprogrammed pitches: ${miss}`);
    for (const p2 of [0, 1, 2, 4, 48, 96, 100, 144, 192, 240, 252, 255]) {
        const row = table.find((t) => t[0] === p2);
        if (row && row[1]) console.log(`  pitch ${String(p2).padStart(3)} → tone=[${row[1]}] vol=[${row[2]}]`);
    }
    // Which register is BBC channel 1?  The one that varies across the sweep.
    const varies = [0, 1, 2].filter((i) => new Set(table.filter((t) => t[1]).map((t) => t[1][i])).size > 4);
    console.log(`  chip tone registers that track pitch: ${varies}`);
    if (varies.length === 1) {
        const ci = varies[0];
        console.log(`  ⇒ BBC channel 1 = chip tone register ${ci}`);
        // Check the equal-tempered formula: f = 125000/divider, 48 pitch units per octave.
        const rows = table.filter((t) => t[1] && t[1][ci] > 0).map((t) => [t[0], t[1][ci]]);
        let best = null;
        for (let refIdx = 0; refIdx < rows.length; refIdx++) {
            const [p0, d0] = rows[refIdx];
            let worst = 0;
            for (const [pi, di] of rows) {
                const want = d0 / Math.pow(2, (pi - p0) / 48);
                worst = Math.max(worst, Math.abs(Math.round(want) - di));
            }
            if (!best || worst < best.worst) best = { p0, d0, worst };
        }
        console.log(
            `  equal-tempered fit (48 units/octave): pitch ${best.p0} → divider ${best.d0}, worst |err| = ${best.worst}`,
        );
        // ⭐ The whole 256-entry mapping is one octave of 48 dividers shifted right by the octave
        // number.  Verify that here, and print the 48 in the form src/platform/sound.c holds them:
        // a failure means the port's table has to be re-derived, not silently trusted.
        const byPitch = new Map(rows);
        const base = [];
        for (let p2 = 0; p2 < 48; p2++) base.push(byPitch.get(p2));
        let shiftOk = true;
        for (let p2 = 0; p2 < 256; p2++) {
            const want = base[p2 % 48] >> (p2 / 48);
            if (byPitch.get(p2) !== want) {
                shiftOk = false;
                console.log(`  ⚠ shift rule broken at pitch ${p2}: ${byPitch.get(p2)} vs ${want}`);
            }
        }
        console.log(`  one-octave table + octave shift explains all 256 pitches: ${shiftOk}`);
        for (let r = 0; r < 4; r++) console.log(`    ${base.slice(r * 12, r * 12 + 12).join(", ")},`);
    }
}

if (only.includes("chan")) {
    const s = seg("chan");
    console.log(`\n── the same pitch on each channel ──`);
    for (const { cmd, after } of pairs(s.from, s.to)) {
        const ch = chan(cmd.bytes[0]);
        if (ch === 0 || !after) continue;
        console.log(`  BBC ch${ch} pitch ${cmd.bytes[4]} → chip tone[${3 - ch}] = ${after.tone[3 - ch]}`);
    }
}

if (only.includes("amp")) {
    const s = seg("amp");
    console.log(`\n── amplitude → chip attenuation ──`);
    for (const { cmd, after } of pairs(s.from, s.to)) {
        const a = amp(cmd.bytes[2], cmd.bytes[3]);
        console.log(`  amplitude ${String(a).padStart(3)} → vol=[${after ? after.vol : "—"}]`);
    }
}

if (only.includes("noise")) {
    const s = seg("noise");
    console.log(`\n── noise pitch → chip noise register ──`);
    for (const { cmd, after } of pairs(s.from, s.to)) {
        if (chan(cmd.bytes[0]) !== 0) continue;
        console.log(
            `  noise pitch ${cmd.bytes[4]} → noise=${after ? after.noise : "—"} vol=[${after ? after.vol : "—"}] tone=[${after ? after.tone : "—"}]`,
        );
    }
}

// The interesting part of an envelope is a per-tick timeline, not a final state: print the
// attenuation and the divider as they step.
function timeline(label, s, chipReg, limit) {
    if (!s) return;
    console.log(`\n── ${label} ──`);
    const t0 = chip.writes[s.wFrom] ? chip.writes[s.wFrom].cycle : 0;
    let shown = 0;
    for (let i = s.wFrom; i < s.wTo && shown < limit; i++) {
        const w = chip.writes[i];
        if (w.byte & 0x80 && !(w.byte & 0x10)) continue; // first byte of a divider pair
        const cs = ((w.cycle - t0) / 20000).toFixed(1);
        const kind = w.byte & 0x90 ? `att=${w.byte & 0xf}` : `divider=${w.tone[chipReg]}`;
        console.log(`  +${cs.padStart(6)} cs  ${kind}`);
        shown++;
    }
}

if (only.includes("env")) {
    timeline("Revs's envelope 1 on channel 3 (attack +10 to level 72, pitch cycle of 6)", seg("env"), 0, 40);
    timeline("...and the amplitude-0 command that silences it", seg("env-quiet"), 0, 6);
}

if (only.includes("dur")) {
    timeline("duration 4 = 20 centiseconds, static amplitude (the $0B30 noise burst)", seg("dur"), 0, 10);
    timeline("release: AR = -20 after the duration ends", seg("dur-release"), 0, 20);
}

if (only.includes("flush")) {
    console.log(`\n── OSBYTE 21: does flushing a sound buffer SILENCE the channel? ──`);
    const show = (k) => console.log(`  ${k.padEnd(12)} noise=${flushStates[k].noise} tone=[${flushStates[k].tone}] vol=[${flushStates[k].vol}]`);
    for (const k of ["playing", "flushed", "tone", "toneFlushed"]) if (flushStates[k]) show(k);
    const stopped = flushStates.flushed && flushStates.flushed.vol[3] === 15 && flushStates.toneFlushed.vol[2] === 15;
    console.log(stopped
        ? "  ⇒ YES: the channel goes to attenuation 15 and its divider resets to the pitch-0 value.\n" +
          "     So `sound_stop_channel` ($0E5A) really does stop Revs's duration-255 engine sounds."
        : "  ⚠ NO — then Revs's engine noise could never stop, and the model above is wrong.");
}

if (only.includes("revs")) {
    const s = seg("revs-rev");
    if (s) {
        console.log(`\n── the engine tone sweeping (channel 1, chip tone 2) ──`);
        console.log(`  dividers: ${dividerSequence(s.wFrom, s.wTo, 2).join(", ")}`);
    }
}

if (dumpDir) {
    fs.mkdirSync(dumpDir, { recursive: true });
    const json = path.join(dumpDir, "sound.json");
    fs.writeFileSync(json, JSON.stringify({ marks, cmds, writes: chip.writes }, null, 1));
    // The fixture `make sound` replays.  ⚠ Written LAST: quantiseToTicks annotates the events in
    // place, so the JSON above is the raw cycle-stamped capture and this is the derived form.
    const fixture = path.join(dumpDir, "sound_events.txt");
    const q = writeFixture(fixture, cmds, chip.writes, fs);
    console.log(`\nwrote ${json}  (${cmds.length} commands, ${chip.writes.length} writes)`);
    console.log(`wrote ${fixture}  (${q.events} events over ${q.ticks} ticks, ` +
        `worst grid residual ${q.worstResidualCycles} cycles)`);
}
