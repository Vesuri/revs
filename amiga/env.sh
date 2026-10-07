#!/usr/bin/env bash
# Source this to put the ~/.local Amiga toolchain on PATH.
#   . amiga/env.sh
# or from the amiga/ directory:
#   . env.sh
TC="$HOME/.local"
export PATH="$TC/opt/bin:$TC:$TC/fs-uae:$PATH"
# KS 3.1 (A600, kick40063) from the shared ~/.local/share/amiga; override with $KICKSTART.
export KICKSTART="${KICKSTART:-$HOME/.local/share/amiga/Kickstarts/kick40063.A600}"
