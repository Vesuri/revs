/* view_span.h — ⭐⭐⭐ ONE DISPLAY LINE AS A LIST OF SOLID RUNS.
 *
 * THE SEAM the replacement renderer is built on (docs/span-render-plan.md §10b): the faithful
 * side turns the game's own per-scan-line road record into at most five solid runs, and the
 * platform painter fills them straight into the bitplanes.  Nothing in between — no forty
 * `$80`-spaced source blocks, no BBC frame buffer, no decode.
 *
 * WHY IT IS EXACT AND NOT A MODEL OF THE ROAD.  `surface_colour_at` ($1E9E) is the game's own
 * colour decision, and its only dependence on the position along the line is four `>=` tests
 * against `surface_edge_0..3[line]`.  So for a fixed line the colour is a PIECEWISE-CONSTANT
 * function of the cell column with AT MOST FOUR BREAKPOINTS, whatever order those four values
 * happen to be in.  Evaluating the game's classifier once per interval therefore returns
 * exactly what asking it per cell returns — so this reinvents no table and re-derives no
 * geometry.  ⭐ The cell chains at $3000..$4380 are the BBC's CACHE of that function; the
 * renderer asks the function instead.
 *
 * WHAT IT IS NOT.  It is the line's INTERIOR.  A boundary that falls inside a cell is a mixed
 * MODE 5 byte the span plotters compose at pixel precision, and an object (car, tree, marker)
 * is source bytes too — both arrive on top of the spans from the source blocks (§10b's
 * `edge_merge` + `object_layer`).  The span census measures that overlay at ~1.9 cells a line
 * against ~1.9 solid runs (shape.cpp §THE SPAN CENSUS), which is the whole reason this is a
 * trapezoid fill and not a region problem.
 */
#ifndef REVS_VIEW_SPAN_H
#define REVS_VIEW_SPAN_H

#ifdef __cplusplus
extern "C" {
#endif

/* A BBC MODE 5 view line is forty cells of four pixels (bbc_screen.h). */
#define VIEW_SPAN_CELLS 40u
/* Four breakpoints bound the interval count at five. */
#define VIEW_SPAN_MAX   5u

/* `start` is the first cell of the run; the run ends where the next one starts, and the last
   ends at VIEW_SPAN_CELLS.  ⭐ NO LENGTH FIELD — the same load-bearing choice revs_plot_span
   makes: a length is a second thing that can disagree with the list. */
typedef struct {
    unsigned char start;
    unsigned char colour;      /* the painted MODE 5 byte: one of surface_colours' four */
} ViewSpan;

/* Builds `line`'s runs into `out` (at least VIEW_SPAN_MAX entries) and returns how many.
   Always >= 1: a line past `horizon_extent` is one run of sky.  Implemented in
   src/gen/revs_native.c beside `surface_colour_at_core`, because the classifier is the
   routine's whole body and is `always_inline` there for measured reasons. */
unsigned view_span_line(unsigned char line, ViewSpan* out);

#ifdef __cplusplus
}
#endif

#endif /* REVS_VIEW_SPAN_H */
