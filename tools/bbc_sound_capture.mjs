// The two halves of every sound measurement, shared by bbc_probe_sound.mjs (the MOS sweeps)
// and bbc_refloop_race.mjs (Revs's own stream in a real race):
//
//   CaptureSoundChip   — decodes the SN76489 write protocol into chip state, with timestamps
//   hookOswordSound()  — records every OSWORD 7 (SOUND) / OSWORD 8 (ENVELOPE) control block
//
// Keeping the decode in ONE place matters: the fixture is a statement about the CHIP, so if the
// two probes decoded it separately they could disagree and the disagreement would look like a
// port bug.  The protocol itself is published (bit 7 set = latch: bits 6..5 channel, bit 4
// volume-vs-tone, bits 3..0 data; bit 7 clear = the upper 6 bits of the latched tone divider;
// channel 3 is the noise generator and its register is 4 bits wide).

import { FakeSoundChip } from "./jsbeeb/src/soundchip.js";

const CPS = 2 * 1000 * 1000;

export class CaptureSoundChip extends FakeSoundChip {
    constructor() {
        super();
        this.tone = [0, 0, 0]; // 10-bit dividers, chip channels 0..2
        this.noise = 0; // 4-bit: bit 2 = white noise, bits 1..0 = rate
        this.vol = [15, 15, 15, 15]; // 4-bit attenuation, 15 = silent
        this.latched = 0;
        this.wasActive = false;
        this.writes = [];
        this.clock = () => 0;
        this.tag = () => ({});
    }

    // The VIA holds the byte on the slow data bus and pulses WE; latch once per pulse.
    updateSlowDataBus(bus, active) {
        if (active && !this.wasActive) this._poke(bus & 0xff);
        this.wasActive = active;
    }

    _poke(value) {
        let command;
        if (value & 0x80) {
            this.latched = value & 0x70;
            command = value & 0xf0;
        } else {
            command = this.latched;
        }
        const ch = (command >> 5) & 3;
        if (command & 0x10) this.vol[ch] = value & 0x0f;
        else if (ch === 3) this.noise = value & 0x0f;
        else if (value & 0x80) this.tone[ch] = (this.tone[ch] & ~0x0f) | (value & 0x0f);
        else this.tone[ch] = (this.tone[ch] & 0x0f) | ((value & 0x3f) << 4);
        this.writes.push({ cycle: this.clock(), byte: value, ...this.state(), ...this.tag() });
    }

    state() {
        return { tone: [...this.tone], noise: this.noise, vol: [...this.vol] };
    }
}

/** Attach a capturing chip to a machine and give it the machine's clock. */
export function attachSoundCapture(machine, chip) {
    chip.clock = () => machine.processor.cycleSeconds * CPS + machine.processor.currentCycles;
    return chip;
}

// ── the fixture ───────────────────────────────────────────────────────────────────────────
//
// `tools/validate_sound.c` needs events on the MOS's own 100 Hz TICK GRID, not in CPU cycles, so
// the quantisation happens here where the timing evidence is.  ⚠ It cannot be a single fitted
// grid across the whole capture: the System VIA's tick is 20000 CPU cycles nominal, and a
// fraction of a cycle of drift per tick accumulates over the ~50 seconds a fixture spans until a
// command lands in the wrong tick and every state after it looks wrong by one tick.
//
// So anchor LOCALLY.  The MOS programs the chip from its tick interrupt, which makes every chip
// write a direct observation of a tick instant: walk the writes in order, carry the tick number
// forward by the rounded gap, and re-anchor on each write.  A command is then placed at the first
// tick instant that can still see it — the next one after its cycle.
//
// The residual is reported.  If a write ever sits more than a fraction of a tick off the grid the
// assumption above is wrong, and the fixture says so instead of quietly encoding a lie.
const TICK_CYCLES = 20000; // 2 MHz / 100 Hz

export function quantiseToTicks(cmds, writes) {
    let worst = 0,
        offGrid = 0;
    if (writes.length) {
        let anchor = writes[0].cycle,
            anchorTick = 0;
        for (const w of writes) {
            const gap = w.cycle - anchor;
            const d = Math.round(gap / TICK_CYCLES);
            const res = gap - d * TICK_CYCLES;
            w.tick = anchorTick + d;
            // ⚠ RE-ANCHOR ONLY ON A WRITE THAT LOOKS LIKE A TICK.  Not every write is one: a
            // buffer flush and a sound's initial silencing come from FOREGROUND code, mid-tick,
            // and anchoring on those shifts the grid by up to half a tick — after which the next
            // rounding is wrong by a whole tick and every state from there on reads as a failure.
            // Measured: the tick spacing is 19995 cycles mean (median 20006, jitter +-250 from IRQ
            // latency), and the off-grid writes sit 2000-7600 cycles away.
            if (Math.abs(res) < TICK_CYCLES / 5) {
                worst = Math.max(worst, Math.abs(res));
                anchor = w.cycle;
                anchorTick = w.tick;
            } else {
                offGrid++;
                w.offGrid = true;
            }
        }
    }
    let wi = 0;
    for (const c of cmds) {
        while (wi + 1 < writes.length && writes[wi + 1].cycle <= c.cycle) wi++;
        const w = writes[wi];
        // An OSWORD 7 is QUEUED: the scheduler picks it up on the next tick instant, so place it
        // one tick on.  An OSBYTE 21 flush is not queued — foreground code silences the channel
        // there and then, in the tick it was issued in.  Getting this wrong moves the silence one
        // tick and reads as a failing model rather than a mislabelled fixture.
        const dt = w ? Math.floor((c.cycle - w.cycle) / TICK_CYCLES) : 0;
        c.tick = w ? w.tick + dt + (c.osbyte === 21 ? 0 : 1) : 0;
    }
    return {
        worstResidualCycles: worst,
        offGrid,
        ticks: writes.length ? writes[writes.length - 1].tick + 1 : 0,
    };
}

/**
 * Write the text fixture the C validator replays:
 *
 *   C <tick> <8 hex bytes>    an OSWORD 7 block, to be issued BEFORE that tick runs
 *   E <tick> <14 hex bytes>   an OSWORD 8 envelope definition
 *   F <tick> <buffer>         an OSBYTE 21 flush of sound buffer 4..7
 *   S <tick> t0 t1 t2 n v0 v1 v2 v3   the chip state at the END of that tick
 *
 * Only ticks in which the real machine actually wrote the chip carry an S line — a tick with no
 * write is a tick in which nothing changed, which the validator checks by requiring its own
 * state to be unchanged too.
 */
export function writeFixture(file, cmds, writes, fs) {
    const { worstResidualCycles, offGrid, ticks } = quantiseToTicks(cmds, writes);
    const lines = [
        `# BBC sound fixture v1 — MOS 1.20 under jsbeeb, ${ticks} ticks`,
        `# ${cmds.length} OS calls, ${writes.length} chip writes, ${offGrid} of them off the tick grid`,
        `# worst on-grid residual: ${worstResidualCycles} cycles of ${TICK_CYCLES}`,
    ];
    const hex = (b) => b.toString(16).padStart(2, "0");
    const cmdLine = (c) =>
        c.osbyte === 21 ? `F ${c.tick} ${c.buffer}` : `${c.osword === 7 ? "C" : "E"} ${c.tick} ${c.bytes.map(hex).join(" ")}`;
    const byTick = new Map();
    for (const w of writes) byTick.set(w.tick, w); // the last write of a tick wins
    const events = [
        ...cmds.map((c) => ({ tick: c.tick, order: 0, line: cmdLine(c) })),
        ...[...byTick.values()].map((w) => ({ tick: w.tick, order: 1, line: `S ${w.tick} ${w.tone.join(" ")} ${w.noise} ${w.vol.join(" ")}` })),
    ];
    events.sort((a, b) => a.tick - b.tick || a.order - b.order);
    for (const e of events) lines.push(e.line);
    fs.writeFileSync(file, lines.join("\n") + "\n");
    return { worstResidualCycles, offGrid, ticks, events: events.length };
}

/**
 * Record every SOUND / ENVELOPE command the machine issues.  These are the INPUT half of the
 * fixture: `tools/validate_sound.c` replays them through the port's own scheduler and diffs the
 * chip writes that come out.
 */
export function hookOswordSound(machine, chip, sink) {
    machine.processor.debugInstruction.add((addr) => {
        const cpu = machine.processor;
        if (addr === 0xfff1) {
            const reason = cpu.a;
            if (reason !== 7 && reason !== 8) return false;
            const blk = cpu.x | (cpu.y << 8);
            const bytes = [];
            for (let i = 0; i < (reason === 7 ? 8 : 14); i++) bytes.push(cpu.readmem((blk + i) & 0xffff));
            sink.push({ cycle: chip.clock(), osword: reason, bytes, ...chip.tag() });
            return false;
        }
        // ⚠ OSBYTE 21 on buffers 4..7 belongs in this stream too, and leaving it out was a real
        // trap: it is the ONLY way Revs stops a sound (`sound_stop_channel` $0E5A, and every one
        // of its sounds has duration 255), so a fixture without it replays as a game that never
        // goes quiet.  A flush also writes the chip from FOREGROUND code, i.e. off the tick grid.
        if (addr === 0xfff4 && cpu.a === 21 && (cpu.x & 0xfc) === 4) {
            sink.push({ cycle: chip.clock(), osbyte: 21, buffer: cpu.x, ...chip.tag() });
        }
        return false;
    });
}
