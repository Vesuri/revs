#!/usr/bin/env bash
# Launch FS-UAE gdb-stub, connect gdb, let Revs run, SIGINT gdb after a delay so it
# breaks in and prints the standby-build timing probes.
set -uo pipefail
cd "$(dirname "$0")"
. ./env.sh
. "$FSUAE_COMMON"
GDB="${GDB:-m68k-amiga-elf-gdb}"
ROM="$KICKSTART"
DELAY="${1:-14}"
# Emulated machine.  Default A500+ (the target, + ECS Denise for BPLCON3 border-blanking).
# $AMIGA_MODEL=A1200 re-runs the same probe on a 68020 to expose beam-timing races that a
# faster CPU moves into the danger window.
MODEL="${AMIGA_MODEL:-A500+}"
# $CHIP_KB / $FAST_KB / $SLOW_KB size the machine.
# Optional extra fs-uae args, e.g. EXTRA_ARGS="--cpu=68040 --jit_compiler=1".  FS-UAE keeps the
# FIRST value of a repeated option, and the shared launcher passes EXTRA_ARGS first, so they win.

# ⭐ SILENT by default.  A warp run is ~4.9x speed, so the game's audio comes out as an
# ear-piercing screech, and no probe this project takes reads the sound hardware.  The `dummy`
# SDL audio driver opens no device at all — verified with a control: a normal run maps
# /System/Library/Components/CoreAudio.component into the process, a `dummy` run maps nothing.
# ⚠ `--volume=0` is NOT the knob — this fsemu-core build's volume path is an unimplemented
# FIXME ("Set volume not implemented yet"), and FS-UAE echoes any option you pass it (including
# a misspelt one) into its log, so the config dump is not evidence an option DID anything.
# FSUAE_SOUND=1 puts the audio back, for an audio bug that has to be heard.
# (fsuae_options makes every debug run silent this way.)
if [ "${FSUAE_SOUND:-0}" = 1 ]; then SOUND=1; fi

RUN=.run; DH0="$RUN/dh0"; DH1="$RUN/dh1"; GDBHOME="$RUN/gdbhome"
mkdir -p "$DH0/s" "$DH1" "$RUN/state" "$GDBHOME"
printf 'cd dh1:\nRevs\n' > "$DH0/s/startup-sequence"
cp -f out/Revs.exe "$DH1/Revs"
# The engine image is read off the player's BBC disc at startup (src/platform/engine_image.h).
# $REVS_DISC picks another one, e.g. the Revs+ disc.
cp -f "${REVS_DISC:-../revs.ssd}" "$DH1/revs.ssd"

fsuae_claim_port
DEBUG=1 AMIGA_MODEL="$MODEL" KICKSTART="$ROM" CHIP_KB="${CHIP_KB:-1024}" FAST_KB="${FAST_KB:-8192}"
FSUAE_LOG="$RUN/fsuae-dbg.log"
fsuae_options
fsuae_launch \
  --hard_drive_0="$DH0" --hard_drive_1="$DH1" \
  --window_width=720 --window_height=568 \
  --remote_debugger_trigger=Revs --state_dir="$RUN/state"
echo "waiting for stub..."
for i in $(seq 1 60); do
  kill -0 "$FSUAE_PID" 2>/dev/null || { echo "FS-UAE exited early; see $RUN/fsuae-dbg.log"; exit 1; }
  lsof -nP -iTCP:"$DEBUG_PORT" -sTCP:LISTEN >/dev/null 2>&1 && break
  sleep 1
done

cat > "$RUN/connect.gdb" <<EOF
set pagination off
set confirm off
set remotetimeout 90
target remote 127.0.0.1:$DEBUG_PORT
EOF

env HOME="$GDBHOME" XDG_CACHE_HOME="$GDBHOME" \
  "$GDB" -q -l 10 -x "$RUN/connect.gdb" -x "${GDBSCRIPT:-diag_timing.gdb}" out/Revs.elf \
  > "$RUN/gdb-out.log" 2>&1 &
GDB_PID=$!
echo "gdb pid=$GDB_PID; running for ${DELAY}s..."
# ⭐ Returns as soon as gdb exits: a script that ends in `kill`/`quit` (every verdict-style gate)
# used to leave this wrapper sleeping out the whole DELAY, ~7 minutes a sabotage run.
for ((i = 0; i < DELAY; i++)); do
  kill -0 "$GDB_PID" 2>/dev/null || break
  sleep 1
done
kill -INT "$GDB_PID" 2>/dev/null || true
# give gdb time to print + detach
for i in $(seq 1 20); do kill -0 "$GDB_PID" 2>/dev/null || break; sleep 1; done
kill -INT "$GDB_PID" 2>/dev/null || true
sleep 2
kill -9 "$GDB_PID" 2>/dev/null || true
fsuae_stop
echo "=== gdb output (filtered) ==="
# ⚠⚠ $GDBTAIL: the default 40 lines is enough for a phase table and NOTHING ELSE — it cuts the
# `=== vbi=... loopFrames=... ===` header, `phase 0` and `FRAME = ... ms`.  The parked-comparison
# protocol (amiga/Makefile §SPANFILL) *requires* phase 0, whose tick count is bit-identical
# across runs of the same trajectory, so raise this for any run you intend to compare:
#   GDBTAIL=200 EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=phase4_prof.gdb ./diag_run.sh 30
# ⚠ and raise it for BOTH arms — never diff two runs captured with different amounts of output.
grep -v "Internal error: pc" "$RUN/gdb-out.log" | grep -vE "^warning:" | tail -"${GDBTAIL:-40}"
