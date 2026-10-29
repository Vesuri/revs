#!/usr/bin/env bash
# Source this for the shared Amiga environment that AmigaXDev installs (~/.local/share/amiga):
# the toolchain, FS-UAE and the shared Python on PATH, and the Kickstarts, Workbench, SetPatch,
# WHDLoad and LHa paths.  Every variable can be overridden from the environment.
#   . amiga/env.sh
# or from the amiga/ directory:
#   . env.sh
. "${AMIGA_ENV:-$HOME/.local/share/amiga/env.sh}"
# The native arm64 FS-UAE with the gdb stub; FSUAE=fs-uae picks the x86_64 build on PATH.
export FSUAE="${FSUAE:-$FSUAE_ARM}"
