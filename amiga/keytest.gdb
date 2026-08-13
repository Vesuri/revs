# ⚠⚠ SUSPECT INSTRUMENT — DO NOT TRUST A RESULT FROM THIS SCRIPT WITHOUT RE-DERIVING IT.
#
# Everything below stimulates the game with `set var g_keyDown[…]`, and on 2026-08-13 gdb writes
# were measured NOT to reach this emulated machine at all: a plain global, a volatile array
# element and `mem[]` all read back unchanged immediately after assignment and after a continue,
# with no error printed (docs/headless-fsuae.md §gdb can READ but not WRITE).  Under that
# behaviour this script can only ever print unchanged values, i.e. report a failure it cannot
# distinguish from a broken key path.
#
# The working substitute is a build flag, not a poke: `make STRAIGHT_TO_RACE=1 FPSCOUNT=1` puts
# the key presses in the binary and `amiga/straight_to_race.gdb` reads the game's own response
# ($0061 engine, $0063 speed).  That path is verified on the target.
#
# End-to-end proof that a key reaches the game, on a headless target with no keyboard.
#
# A plain build parks in the FRONT END (nothing presses a key, so Revs::render is never
# reached — that alone is worth knowing).  So the test runs there: poke the rawkey state the
# CIA-A handler would set, let the emulated machine run, and watch the front end's own
# selection variable.  $0077 is where a menu stores its selection ($6591: LDA $77 / BEQ
# back-to-polling), so $0077 changing proves g_keyDown -> keyDown() -> OSBYTE 129 -> the
# game's menu logic.
#
# Not covered: the CIA-A serial handler itself (there is no keyboard on a headless run).
# That code is the RoF port's, verified there on this same framework.
#
# Run: . ./env.sh && make clean && make && GDBSCRIPT=keytest.gdb ./diag_run.sh 200
set pagination off
set confirm off

define runframes
  set $k = 0
  while $k < $arg0
    tbreak RevsInput::sampleMouse
    continue
    set $k = $k + 1
  end
end

tbreak RevsInput::sampleMouse
continue
printf "=== front end: 0077=%02x 0078=%02x 05F5=%02x\n", mem[0x77], mem[0x78], mem[0x05F5]

# Hold Amiga '1' (rawkey $01) = BBC -49, the menu's option-1 key.
set var g_keyDown[1] = 1
runframes 40
printf "=== '1' held:   0077=%02x 0078=%02x  unmapped=%lu\n", mem[0x77], mem[0x78], g_keyUnmapped

# Release, then hold SHIFT + F3 (= BBC SHIFT + f2) to select ANALOGUE (mouse) mode.
set var g_keyDown[1] = 0
set var g_keyDown[0x60] = 1
set var g_keyDown[0x52] = 1
runframes 40
printf "=== SHIFT+F3:   05F5=%02x (want $C0 or $80 = analogue mode)\n", mem[0x05F5]
detach
quit
