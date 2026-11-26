# Inline Layout Diagnostics

HoneyBee provides an opt-in `inline_layout` trace category for investigating inline formatting, line breaking, baseline alignment, fragment positioning, and text drawing.

## Enable

From the repository root, run:

    HONEYBEE_LOG=inline_layout HONEYBEE_LOG_LEVEL=trace HONEYBEE_LOG_FILE=/tmp/honeybee-inline-layout.log ./build/bin/honeybee

Navigate to the page, reproduce the layout, then close the browser to flush the log.

The category is independent of `layout`, `layout_detail`, `paint`, and `paint_detail`. It is opt-in because text-heavy pages can produce many entries.

## Events
| Event | Meaning |
|---|---|
| `INLINE_WORD` | A styled word after inline ancestry and vertical-align processing. |
| `INLINE_ANCESTOR` | The inline ancestor chain and computed vertical-align values. |
| `INLINE_MEASURE` | Measured word width, height, baseline and applied baseline offset. |
| `INLINE_WRAP` | A line-wrap decision and the width values that triggered it. |
| `INLINE_MERGE_DECISION` | Whether a word was merged into the preceding fragment. |
| `INLINE_MERGE_METRIC_MISMATCH` | Baseline or height differences detected during merging. |
| `INLINE_LINE` | Line width, height, ascent, descent and baseline after line breaking. |
| `INLINE_FRAGMENT` | Fragment metrics before final positioning. |
| `INLINE_POSITION` | Final fragment coordinates relative to its layout context. |
| `INLINE_POSITION_LINE` | Final line position and metrics. |
| `INLINE_DRAW` | Fragment coordinates and metrics immediately before Pango draws it. |

## Following a fragment

Use the element pointer, text, line index and fragment index to correlate events. Element pointers are process-local and are not stable between runs.

`phase=line_break` events describe fragments before final positioning. `INLINE_POSITION` and `INLINE_DRAW` describe later stages.

For a baseline investigation, compare:

1. `INLINE_MEASURE`: measured and adjusted baselines.
2. `INLINE_MERGE_DECISION` and `INLINE_MERGE_METRIC_MISMATCH`.
3. `INLINE_LINE`: baseline, ascent, descent and height.
4. `INLINE_FRAGMENT`: fragment baseline and dimensions.
5. `INLINE_POSITION`: final y-coordinate and both baselines.
6. `INLINE_DRAW`: values passed to Pango.

These diagnostics report layout decisions and drawing inputs; they do not independently establish the final pixels produced by the graphics backend.
