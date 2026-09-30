# ⭐ DOES THE GAME FIT THE TARGET — 512 KB chip + 512 KB slow — AND WHAT IS LEFT?
# ⚠ Run on Kickstart 3.1: FS-UAE cannot boot 1.3 from a directory drive, and 3.1 itself takes
# MORE of both pools than 1.3, so a fit here is conservative.  The 1.3 run is the WHDLoad one.
# Walks exec's free-memory lists once the race is running (every allocation the game makes is
# done by then: RevsScreen/RevsAudio/RevsInput allocate at initialize()) and prints each region's
# free bytes and largest free chunk.  Pair it with a moving race so "it fits" means "it plays":
#   make clean && make STRAIGHT_TO_RACE=1 FPSCOUNT=1
#   AMIGA_MODEL=A500 \
#     CHIP_KB=512 FAST_KB=0 EXTRA_ARGS="--slow_memory=512 --warp_mode=1" \
#     GDBSCRIPT=mem_target.gdb ./diag_run.sh 200
set pagination off
set confirm off
tbreak Revs::render if g_vbiCount >= 900
continue
if $pc != (unsigned long)&_ZN4Revs6renderEv
  printf "MEM FAIL: the race never rendered (stopped at %p, field %u)\n", $pc, g_vbiCount
else
  printf "race: field %u  engine$61=%02x speed$63=%02x  KS %u\n", g_vbiCount, mem[0x61], mem[0x63], *(unsigned short *)((char *)SysBase + 20)
  # Raw exec offsets (the ELF's debug info has no MemHeader type): ExecBase.MemList at $142;
  # MemHeader: ln_Succ 0, mh_Attributes 14 (word), mh_First 16, mh_Lower 20, mh_Upper 24,
  # mh_Free 28; MemChunk: mc_Next 0, mc_Bytes 4.
  set $mh = *(unsigned long *)((char *)SysBase + 0x142)
  while *(unsigned long *)$mh != 0
    set $big = 0
    set $mc = *(unsigned long *)($mh + 16)
    while $mc != 0
      if *(unsigned long *)($mc + 4) > $big
        set $big = *(unsigned long *)($mc + 4)
      end
      set $mc = *(unsigned long *)$mc
    end
    printf "region %08lx-%08lx attr=%04x  free=%lu  largest=%lu\n", *(unsigned long *)($mh + 20), *(unsigned long *)($mh + 24), *(unsigned short *)($mh + 14), *(unsigned long *)($mh + 28), $big
    set $mh = *(unsigned long *)$mh
  end
end
kill
quit
