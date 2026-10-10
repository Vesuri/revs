#!/usr/bin/env bash
# Source-level debug the Amiga Revs build via FS-UAE's GDB stub.
#   ./debug.sh [path-to-kickstart] [script.gdb]
# Build first, then run this. `continue` at the gdb prompt runs the program.
# HOME/XDG_CACHE_HOME must be set for gdb; connect to 127.0.0.1 (not localhost).
set -uo pipefail
cd "$(dirname "$0")"
. ./env.sh
. "$FSUAE_COMMON"

GDB="${GDB:-m68k-amiga-elf-gdb}"
ROM="${1:-$KICKSTART}"
[ -f "$ROM" ] || { echo "Kickstart ROM not found: $ROM  (pass as \$1 or set \$KICKSTART)"; exit 1; }
[ -f out/Revs.elf ] || { echo "build first: make"; exit 1; }

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
DEBUG=1 AMIGA_MODEL=A500+ KICKSTART="$ROM" CHIP_KB=1024 FAST_KB=8192
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

PREAMBLE="$RUN/connect.gdb"
{ printf 'set pagination off\nset confirm off\nset remotetimeout 90\n'
  printf 'target remote 127.0.0.1:%s\n' "$DEBUG_PORT"   # $DEBUG_PORT: see fsuae_common.sh
  cat <<'EOF'
echo \n>>> connected. `continue` runs; Ctrl-C breaks back in. <<<\n
EOF
} > "$PREAMBLE"

if [ "${2:-}" ] && [ -f "${2:-}" ]; then
  exec env HOME="$GDBHOME" XDG_CACHE_HOME="$GDBHOME" \
    "$GDB" -q -l 10 -x "$PREAMBLE" -x "$2" out/Revs.elf
fi
exec env HOME="$GDBHOME" XDG_CACHE_HOME="$GDBHOME" \
  "$GDB" -q -l 10 -x "$PREAMBLE" out/Revs.elf
