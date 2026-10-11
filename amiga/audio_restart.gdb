# Build: make clean && make AUDIORESTARTTEST=1
# Run: EXTRA_ARGS="--warp_mode=1 --uae_chipset_hacks=8" GDBSCRIPT=audio_restart.gdb ./diag_run.sh 40
# chipset_hacks=8 disables UAE's automatic repair of too-short audio DMA waits.
# Check: python3 tools/check_audio_restart.py amiga/.run/audio-restart.bin amiga/.run/gdb-out.log
# The fixture uses real MOS crash and low-rev engine commands in the quiet front end.
# Actual DMA addresses in the capture must leave the white buffer for both channels.
set pagination off
set confirm off
tbreak revs_audio_restart_marker
continue
printf "WAVES white=%p periodic=%p square=%p\n", s_white, s_periodic, s_square
monitor profile 6 "" ".run/audio-restart.bin"
printf "CHIP noise=%u tone2=%u volumes=%u,%u,%u,%u\n", g_sndChip.noise,g_sndChip.tone[2],g_sndChip.vol[0],g_sndChip.vol[1],g_sndChip.vol[2],g_sndChip.vol[3]
kill
quit
