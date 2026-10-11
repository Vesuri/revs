# Build: make clean && make AUDIORESTARTTEST=1
# Run: EXTRA_ARGS="--warp_mode=1" GDBSCRIPT=audio_stop.gdb ./diag_run.sh 40
# Flush the real MOS scheduler after the steady periodic engine sound starts.
set pagination off
set confirm off
tbreak revs_audio_stop_marker
continue
printf "BEFORE volumes=%u,%u,%u,%u\n", g_audioPaulaVol[0],g_audioPaulaVol[1],g_audioPaulaVol[2],g_audioPaulaVol[3]
if g_audioPaulaVol[2] == 0 || g_audioPaulaVol[3] == 0
  echo FAIL: engine fixture was not audible\n
  quit 1
end
# Periodic engine samples advance at half the tone-toggle rate.
set $noise_divider = 2 * g_sndChip.tone[2]
set $noise_period = $noise_divider * 14 + (($noise_divider * 3) / 16)
printf "PITCH divider=%u noise periods=%u,%u expected=%u\n", g_sndChip.tone[2],g_audioPaulaPer[2],g_audioPaulaPer[3],$noise_period
if g_audioPaulaPer[2] != $noise_period || g_audioPaulaPer[3] != $noise_period
  echo FAIL: engine noise sample rate is wrong\n
  quit 1
end
tbreak revs_audio_stop_marker
continue
printf "AFTER chip=%u,%u,%u,%u Paula=%u,%u,%u,%u quiet=%u\n", g_sndChip.vol[0],g_sndChip.vol[1],g_sndChip.vol[2],g_sndChip.vol[3],g_audioPaulaVol[0],g_audioPaulaVol[1],g_audioPaulaVol[2],g_audioPaulaVol[3],g_sndQuiet
if g_sndChip.vol[0] != 15 || g_sndChip.vol[1] != 15 || g_sndChip.vol[2] != 15 || g_sndChip.vol[3] != 15
  echo FAIL: scheduler did not silence all channels\n
  quit 1
end
if g_audioPaulaVol[0] != 0 || g_audioPaulaVol[1] != 0 || g_audioPaulaVol[2] != 0 || g_audioPaulaVol[3] != 0
  echo FAIL: Paula retained sound after the flush\n
  quit 1
end
echo PASS: engine and mirror silenced after the flush\n
kill
quit
