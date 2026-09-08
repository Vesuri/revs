/* The transliteration trap's runtime — see trans_trap.h.
 *
 * A hit is recorded by NAME, not counted per call site, because the question is binary: did
 * any transliterated body run at all?  The report goes to $REVS_TRANS_LOG (default
 * tmp/trans_hits.txt) at exit, one line per distinct body, so `make transtrap` can simply
 * check whether the file is empty.
 */
#ifdef REVS_TRANS_TRAP

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TRANS_MAX 512

static const char* g_names[TRANS_MAX];
static unsigned long g_hits[TRANS_MAX];
static int g_count;

static void revs_trans_report(void)
{
    const char* path = getenv("REVS_TRANS_LOG");
    FILE* f;
    int i;

    if (!path || !*path) path = "tmp/trans_hits.txt";
    f = fopen(path, "w");
    if (!f) return;
    for (i = 0; i < g_count; i++)
        fprintf(f, "%10lu  %s\n", g_hits[i], g_names[i]);
    fclose(f);
}

/* ⚠ The report is armed at START-UP, not on the first hit.  Armed lazily, a CLEAN run writes no
   file at all — indistinguishable from a run that died before exit, which is the difference the
   gate is made of. */
__attribute__((constructor)) static void revs_trans_arm(void) { atexit(revs_trans_report); }

void revs_trans_hit(const char* fn)
{
    int i;

    for (i = 0; i < g_count; i++)
        if (g_names[i] == fn || strcmp(g_names[i], fn) == 0) { g_hits[i]++; return; }
    if (g_count < TRANS_MAX) { g_names[g_count] = fn; g_hits[g_count++] = 1; }
}

#endif
