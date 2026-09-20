# ⭐⭐ THE BLOCK-OP POSTCONDITION CHECK (needs `make FASTMEMCHECK=1`).
#
# Every wrapped memset/memcpy/memmove verifies its own postcondition against the source as it was
# at entry — see src/platform/amiga/fastmem.c §the target-side postcondition check for why a
# target pixel diff cannot gate this change and this can.
#
# ⚠ `g_fmSkipped` is calls longer than the 1024-byte snapshot, which are NOT checked.  A non-zero
# value is not a failure but it IS a hole: account for it against g_fmLongest before quoting a
# pass.  ⚠ This arm byte-loops over every byte moved, so its phase rows mean nothing.
set pagination off
set confirm off

tbreak Revs::render if g_vbiCount >= 900
continue

printf "=== FASTMEM checks=%lu mismatch=%lu skipped=%lu longest=%lu  (vbi=%u)\n", \
  g_fmChecks, g_fmMismatch, g_fmSkipped, g_fmLongest, g_vbiCount
printf "verdict: %s\n", g_fmMismatch ? "FAIL — a wrapped block op broke its postcondition" : "PASS"
kill
quit
