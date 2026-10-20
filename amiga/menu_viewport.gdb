# Menu viewport and bitmap-rendering regression.
# Build clean with PROBES=1 COMPETITION=1 TTCHECK=1.
# Run: GDBSCRIPT=menu_viewport.gdb EXTRA_ARGS="--warp_mode=1" ./diag_run.sh 120
set pagination off
set confirm off
tbreak Revs::render if g_screenPlanes == 3 && g_screenHeight == 230
continue
printf "MENU height=%u bytes=%u front=%08x checks=%lu mismatch=%lu\n", g_screenHeight, g_screenBytes, g_screenFrontAddr, g_ttCheckRuns, g_ttCheckMismatch
set $p = g_screenCopperAddr
set $plane = ((*(unsigned int*)($p+20*4) & 65535) << 16) | (*(unsigned int*)($p+21*4) & 65535)
if $plane != g_screenFrontAddr || g_screenFrontAddr != (unsigned long)this->screen.m_ttBitmap->data + 2400 || g_screenBytes != 27600 || g_ttCheckRuns == 0 || g_ttCheckMismatch != 0
 printf "FAIL: menu viewport or bitmap oracle\n"
 quit 1
end
tbreak race_main_loop_core
continue
tbreak Revs::render if g_screenHeight == 208
continue
printf "GAME height=%u checks=%lu mismatch=%lu\n", g_screenHeight, g_ttCheckRuns, g_ttCheckMismatch
if g_screenHeight != 208 || g_ttCheckMismatch != 0
 printf "FAIL: bitmap oracle after menu traversal\n"
 quit 1
end
printf "PASS: shifted menu, complete bitmap oracle, return to gameplay\n"
detach
quit
