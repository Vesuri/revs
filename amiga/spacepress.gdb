# The double-press instrument (docs/controls.md §The double-press).
#
# HOW TO USE IT: run the game, get to any menu or "press SPACE" page, press SPACE ONCE with a
# normal tap, and read these.  Then press it again if the page did not advance.
#
#   g_spaceEdges     physical SPACE presses (rising edges, counted in the CIA handler)
#   g_spacePolls     times the engine asked about SPACE
#   g_spaceAnswered  times this backend answered "held"
#
# READING IT — the whole diagnosis is one comparison:
#   edges = 2 to advance one page, answered > 0 during the first  -> the port SAW the press and
#     the engine discarded it: either menu_wait_key's downward scan matched the number key still
#     held ($657a), or wait_dismiss's up-then-down debounce ($34D9/$34E0) ate it.  Both are
#     faithful 6502 logic, so the defect is TIMING at the seam, not either routine.
#   edges = 2, answered = 0 during the first -> the input layer lost the press.  A port bug.
#   answered climbing with edges = 1 and the page not advancing -> a STUCK key: a key-up code
#     that never arrived, so g_keyDown[$40] is latched at 1.
set pagination off
printf "space: edges=%lu polls=%lu answered=%lu\n", g_spaceEdges, g_spacePolls, g_spaceAnswered
printf "rawkey SPACE currently down = %d   (key events seen: %lu)\n", g_keyDown[0x40], g_keyEvents
