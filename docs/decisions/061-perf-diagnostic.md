# ADR-061: Layout perf diagnostic — typed-field hypothesis disproven

**Status:** Implemented
**Date:** 2026-11-02
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-058 (split-timer), ADR-059 (cascade index), ADR-060 (layout copy elimination)

## Context

The ADR-060 profile (1099 samples, pre-fix) identified `fill_sides`
at 25.13% of total render time, with allocation churn at ~9.5%.
That profile's post-fix residuals were **unknown** — ADR-060
eliminated the copies but not the map lookup itself.

The ADR-061 plan was to close the residual by shipping **typed
`ComputedStyle` fields** (e.g. `float margin_top`,
`float padding_left`) alongside the `unordered_map`, eliminating
the `unordered_map::find` cost on every property read. Projected
win: **~25% layout**, based on ADR-060's pre-fix profile's
"`fill_sides` at 25.13% + `unordered_map::find` inline" reading.

### What the diagnostic found (2026-11-02, post-ADR-060)

Re-profiled with `perf record -g --call-graph dwarf` on
`build-release/bin/honeybee` after ADR-060 landed. 1135 samples
on Wikipedia HTML article.

**Top self-time symbols:**

| Symbol | Self % | Category |
|---|---|---|
| `contains_word` | 5.43% | Cascade `[class~=]` matching |
| `do_lookup_x` | 1.79% | Dynamic linker (first-load, **ignore**) |
| `matches_simple` | 1.34% | Cascade simple selector |
| `SelectorList::matches` | 1.33% | Cascade list iteration |
| `matches_from` | 1.31% | Cascade recursive match |
| `_Hashtable<string,string>::find` | ~1.2% nested | `Stylesheet::variables` (**cascade**, not layout) |
| `_Hashtable<string,CascadeValue>::find` | **0.18–0.26%** | **Typed-fields target** |

**The typed-fields hypothesis is disproven by its own target's
post-fix cost.** The exact lookup typed fields would eliminate
(`unordered_map<string, CascadeValue>::find`) is **0.25% of
total, not 25%.** ADR-060's copy elimination captured ~99% of
the map-lookup cost by removing the string allocations around it.

**Cumulative structure (`--children`, top frames):**

```
35.56%  layout_element (whole layout phase)
  |-31.43%  lambda()#2 (self ~0.12%)  -- inlined callers of hot leaf functions
  |-22.41%  lambda()#1 (self ~0.12%)  -- same
  |-12.79%  recursive layout_element children
```

Both lambdas have **cumulative 31% / 22% but near-zero self
time** -- they call the hot functions, they're not themselves
the cost. The 31% path includes `parse_length` + hash + compare
+ parse residuals.

### Individual candidate verdicts

**Typed `ComputedStyle` fields — DISPROVEN**
- Target cost: 0.25% (measured)
- Effort: ~200-500 lines (data model change)
- Projected original win: 25% (from ADR-060's pre-fix profile)
- **Actual win if shipped: ~0.25%.** Not worth the refactor risk.

**String→string `unordered_map::find` at ~1.2% nested — NOT
LAYOUT**
- Traced to `Stylesheet::variables` (CSS custom properties,
  cascade-side)
- Cascade is only ~7% of total after ADR-059; even perfect fix
  is ~1% of total
- Different ADR (cascade perf), not this one

**`parse_length` fast path — NOT IN TOP-10**
- `fill_sides`'s lambda self-time dropped from 25.13% to
  ~0.12% after ADR-060. The `parse_length` calls embedded in it
  are no longer visible as separate self-time.
- Most `Npx` values presumably hit an existing fast path.
- No measured justification for a fast-path ADR.

**`contains_word` at 5.43% — REAL BUT SMALL**
- Called from `matches_attribute` (line 686) and one other site
  (line 712) -- both `[attr~="value"]` matching
- Pre-splitting element class/attr values would drop O(n) to
  O(1) or O(log n)
- **Maximum possible win: ~3-5% of total** (5.43% self-time ×
  maybe 80% eliminable)
- At 5.43%, `contains_word` is the largest remaining self-time
  symbol. **But 3-5% is polish, not a headline cycle.**

### The honest profile shape

When no function exceeds ~5% self-time and the top-10 covers
~15% total, **the profile is flat.** That's the expected shape
**after** a successful optimization cycle — the big fish has
been caught, what remains is a school of small fish each worth
3-5%.

The two ADRs that shipped perf wins this session:

| ADR | Target | Pre | Post | Win |
|---|---|---|---|---|
| 059 | Cascade (selector index) | 1058ms | ~190ms | **5.6×** |
| 060 | Layout (copy elimination) | 2678ms | 2357ms | **12%** |
| Combined | Wikipedia total | 3437ms | ~2555ms | **25.5%** |

**There is no third big win available at the current
architecture.** All identified targets are <5% individually.

## Decision

**Do not ship typed `ComputedStyle` fields.** The measurement
disproves the justification.

**Do not ship `contains_word` optimization as a standalone
ADR-061.** At 3-5% total win and 60 lines of change, it's
better bundled into a broader "cascade perf round 2" cycle
(which would also touch selector matching) than as a
standalone cycle.

**Record ADR-061 as a decision record** documenting:
1. The typed-fields hypothesis was projected at 25%, measured
   at 0.25% — ADR-060 had already captured the real cost
2. Three alternate targets individually disproven (string map
   not layout, `parse_length` not visible, `contains_word` real
   but small)
3. **Diminishing returns signal**: the profile is flat because
   ADR-059 + ADR-060 captured everything available at the
   current architecture
4. Future cascade-perf cycles (if any) should bundle
   `contains_word` with selector matching optimization, not
   ship it standalone

## Design decisions

### Why record a disproved hypothesis as a full ADR

**Disproven hypotheses are valuable.** The ADR-060 profile
*looked* like typed fields would win 25%. Without the
re-profile, a future contributor reading ADR-060 would
reasonably propose typed fields as the next cycle. ADR-061
short-circuits that mistake.

The cost of ADR-061 (one docs commit) is dwarfed by the cost of
a future contributor starting a typed-fields cycle based on
stale numbers. **This ADR is 100% future-contributor-value.**

### Why "flat profile" is a success signal, not a failure

The ADR-061 plan projected a 25% target based on ADR-060's
profile. Finding that target to be 0.25% could be read as "we
misevaluated." It's actually "ADR-060 was so effective that
the residual it left is 100× smaller than predicted."

**Both are true.** The projection was reasonable given the
information available at the time. The re-measurement corrects
it. **The methodology (measure-then-fix, re-measure before next
cycle) is what made the correction possible.**

### Why not ship `contains_word` "while we're here"

Tempting because it's the last measurable target. Rejected
because:
- 3-5% is below the threshold where "one cycle = one ADR"
  provides good ROI
- `contains_word` sits inside cascade's selector matching,
  which is a subsystem that deserves its own comprehensive
  pass (indexes are already there; matching itself is still
  O(simples × element attributes) per selector)
- Bundling `contains_word` with other cascade work would make
  ADR-062 a real cycle, not a polish cycle

### Why not `parse_length` fast path

No measured evidence. `parse_length` doesn't appear in the
top-10 self-time symbols. Adding a fast path without seeing the
target is exactly the "speculation" pattern that ADR-060's
lesson explicitly warned against (ADR-060 disproved two
hypotheses before measuring).

### Why not "keep going with smaller cycles"

Diminishing returns are **the correct stopping signal.** The
progression from "25.5% total win in 2 cycles" (ADR-059+060)
to "3-5% for one cycle" (hypothetical ADR-061) is not a
decline — it's **convergence to the floor.** Continuing past
the floor with sub-5% cycles spends more effort for less
measurable improvement, and each polish cycle risks introducing
bugs (each cycle ships code changes and needs full test
coverage).

**The right move when the profile flattens** is to stop
optimizing, ensure the current wins are durable (which
ADR-061's decision record does), and wait for either a real
new bottleneck (e.g. adding JS, adding images, adding grid
support would shift the profile) or for a user-visible problem
the current speed doesn't satisfy.

## Deferred

- **`contains_word` optimization** -- bundled with a future
  cascade-perf round, not standalone
- **Typed `ComputedStyle` fields** -- stay as `unordered_map`
  until a new architectural need arises (e.g. if incremental
  restyle gets slow, or if a specific property starts showing
  up in profiles)
- **`parse_length` fast path** -- no measured justification
- **`Stylesheet::variables` lookup** -- cascade-side, separate
  cycle if a CSS-variable-heavy page appears
- **Library-internal costs** (~10% from harfbuzz, pixman,
  GTK, fontconfig) -- not our code

## Consequences

**Better:**
- **Record correct.** Future contributors see the disproven
  hypothesis, skip the wasted cycle
- **Diminishing returns signal documented** -- a genuine
  "we're done with this stream" marker
- **Methodology validated**: measure → fix → re-measure before
  next cycle → ship the fix or record the disproval. Three
  cycles (059, 060, 061) demonstrate all three outcomes
  (measured win, measured win, measured disproval)
- **ADR-062+ scoped cleanly**: next perf cycle (if any) targets
  cascade round 2 (`contains_word` + selector matching), not
  layout

**Accepted limitations:**
- **`contains_word` at 5.43% remains.** Acknowledge as "known
  small target, deferred"
- **No code changes in this ADR.** Purely diagnostic record
- **No test additions.** Existing 683 remain the full suite

**Unchanged:**
- ADR-005 through ADR-060 behavior
- Wikipedia render performance (~2555ms after ADR-060)
- All 683 tests

**Possible future triggers for revisiting this decision:**
1. Adding a feature (JS, images, grid) that shifts the profile
   and makes `unordered_map::find` a larger fraction again
2. A user-visible perf regression that points back to layout
3. Implementation of an incremental-restyle path (would make
   typed fields more valuable for diffing)
4. A new architecture (multi-process, JIT) where flat data
   structures matter more

## The methodology record

The three perf cycles this session:

| ADR | Cycle | Outcome |
|---|---|---|
| 058 | Split-timer infrastructure | Measured cascade 1058ms / layout 2379ms (no fix, just measurement) |
| 059 | Cascade index (targeted cascade) | **5.6× cascade win**, measured 3-run stable |
| 060 | Copy elimination (targeted `fill_sides`) | **12% layout win**, measured median-of-3 |
| **061** | **Diagnostic (targeted typed fields)** | **Hypothesis disproven, 0.25% target not 25%** |

**Each cycle targeted a measured hotspot. Each cycle produced
either a measured win or a measured disproval. No cycle
optimized without measuring.**

That's the complete perf stream for this architecture. It ends
because there's nothing left >5% individually, not because
we're tired.

## Acceptance criteria

ADR-061 is complete when:

- [ ] Decision record documents the disproven typed-fields
      hypothesis
- [ ] Post-ADR-060 profile recorded (self-time + cumulative
      + structural)
- [ ] Three candidate targets individually disproven
      (typed fields / string map / `parse_length`)
- [ ] `contains_word` documented as deferred to bundled
      cascade-perf cycle
- [ ] Methodology record for ADR-058/059/060/061 complete
- [ ] README row + CHANGELOG entry (decision record, no
      implementation)
- [ ] ADR-061 status -> Implemented (as decision record)
