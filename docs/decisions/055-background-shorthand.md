# ADR-055: CSS shorthand `background:` (color forms)

**Status:** Accepted
**Date:** 2026-11-01
**Deciders:** Amr Muhammad (maintainer)
**Relates to:** ADR-026 (var() resolution), ADR-029 (flex/flex-flow shorthand pattern), ADR-040 (border shorthand), ADR-054 (fixture that surfaced the gap)

## Context

**ADR-054's finding was imprecise.** ADR-054's fixture used `background: #d35400; border: 2px solid #333` and both silently produced no visible fill, so the ADR recorded "shorthand background: and border: don't parse yet." That was wrong.

### Corrected measurement (2026-11-01)

Four-row isolation fixture with longhand / shorthand mixed:

| Row | Background | Border | Rendered |
|---|---|---|---|
| 1 | longhand `background-color` | longhand `border-*` | **GREEN fill + border** |
| 2 | **shorthand `background:`** | longhand `border-*` | **white fill + border** |
| 3 | longhand `background-color` | **shorthand `border:`** | **orange fill + border** |
| 4 | both shorthand | | white fill + border |

**`border:` shorthand works** -- ADR-040 shipped `try_expand_border` at `stylesheet.cpp:482`, tested at 889-1002. ADR-054's border was rendered (thin 2px line) but invisible against the white page at the small size, so the fixture looked like it failed entirely.

**`background:` shorthand fails.** No `try_expand_background` exists in the `expand_shorthand` dispatcher. `background: #2980b9` silently drops; `background-color: #2980b9` works.

### Wikipedia `background:` usage (measured 2026-11-01, site.styles)

11 unique `background:` values in the Vector 2022 CSS bundle:

| Form | Example | Count |
|---|---|---|
| Color-only | `background:#fff`, `#ffffff`, `var(--x, #fff)`, `!important` | 5 |
| `none` | `background:none`, `none !important` | 2 |
| `url(...)` + extras | `#fff url(...) repeat`, `url(...) no-repeat right`, `url(...) 100% 50% no-repeat` | 3 |
| `linear-gradient(...)` | | 1 |

**Color-only + none is 7/11 forms** -- the most common, and the cheapest to parse.

`var()` is free: cascade resolves it before the parser sees the value (ADR-026). By the time `try_expand_background` runs, `var(--x, #fff)` is `#fff`.

## Decision

Add `try_expand_background` covering **color-only** and **`none`** forms, hooked into the existing `expand_shorthand` dispatcher. Defer URL forms and gradients.

### Grammar (v1)

```
background: <color>          -> background-color: <color>
background: none             -> background-color: transparent (or no-op)
background: <color> !important -> background-color: <color> !important
background: <var-to-color>   -> resolved by cascade, same as <color>
```

Any value containing `url(`, `linear-gradient(`, `radial-gradient(`, `repeating-` or a comma (multi-layer) → **drop declaration entirely** (matching `border` / `grid-template` / `flex` malformed behavior).

### Where it plugs in

`expand_shorthand` at `stylesheet.cpp:520` -- add a new branch before the box-shorthand default:

```cpp
// ADR-055: background shorthand (color forms only).
if (property == "background") {
    if (try_expand_background(value, important, out)) return;
    // Malformed or url/gradient/multi-layer: drop entirely.
    return;
}
```

`try_expand_background` calls `parse_color` (existing) and pushes one declaration:

```cpp
bool try_expand_background(std::string_view value, bool important,
                           std::vector<Declaration>& out) {
    // Defer url / gradient / multi-layer forms.
    if (value.find("url(") != std::string_view::npos) return false;
    if (value.find("gradient(") != std::string_view::npos) return false;
    if (value.find(',') != std::string_view::npos) return false;

    // none -> transparent (matches CSS spec)
    std::string_view trimmed = trim(value);
    if (trimmed == "none") {
        out.push_back({"background-color", "transparent", important});
        return true;
    }

    // Try color parse; unknown values fail.
    Color c = parse_color(trimmed);
    if (c.a == 0.0f && trimmed != "transparent") {
        // Distinguish "unparsed" from "transparent" -- parse_color
        // returns transparent for unknowns.
        return false;
    }

    out.push_back({"background-color", std::string(trimmed), important});
    return true;
}
```

### Why `!important` is free

`split_declarations` already extracts `!important` into the `Declaration::important` field before `expand_shorthand` runs. Every shorthand path (`border`, `flex`, `grid-template`) preserves it. `try_expand_background` uses the same `important` parameter.

### Why var() is free

`cascade.cpp` runs `var()` substitution before `apply_rule` sees declarations (ADR-026). By the time `expand_shorthand` fires, `var(--x, #fff)` is `#fff`. Same code path, no extra work.

### Why url / gradient / multi-layer are dropped entirely

Half-parsing `background: #fff url(x) repeat` by extracting only the color produces a **worse** state than dropping it: the box gets the color but no image, whereas today it gets neither. Consistency with `border`/`grid-template`/`flex` malformed handling: drop the whole declaration, let the cascade fall through to whatever was set earlier.

### Why color-only v1 (not url v1)

URL forms need `background-image` + `background-repeat` + `background-position` + `background-size` coordination -- four longhands, each with its own keyword grammar (`no-repeat`, `right`, `100% 50%`, `1.23em`, `/ size`). That's an ADR of its own, comparable to ADR-040's border work. Color-only is `parse_color` (already shipped) + one push. Smallest honest deliverable.

## Deferred

- **URL forms** -- `background: url(...) no-repeat right` etc., 4 forms in Wikipedia CSS, needs 4-longhand expansion coordination
- **linear-gradient / radial-gradient** -- 1 form in Wikipedia CSS, no gradient support anywhere in paint
- **Multi-layer** -- `background: url(a), url(b)` -- rare in Wikipedia, valid CSS
- **`background` with color + url** -- `#fff url(...) repeat` -- part of URL forms work
- **`background-size` / `background-position` / `background-repeat` shorthand sub-parsing** -- inside URL forms work

## Consequences

**Better:**
- `background: #fff`, `#ffffff`, `none`, `!important`, `var(--x, #fff)` -- **the 7 most common forms render** for the first time
- `docs/demo.html` and other existing fixtures that use `background: ...` shorthand get their intended fill
- Wikipedia's white content backgrounds, article chrome backgrounds render correctly
- ADR-054's mis-statement corrected in docs
- Test count 651 -> ~656

**Accepted limitations:**
- **URL forms still drop entirely** -- 4 forms in Wikipedia CSS remain unfilled (same state as today, not worse)
- **linear-gradient still drops** -- 1 form, no gradient support
- **Multi-layer drops** -- rare
- **Compound forms partial** -- `#fff url(...)` drops entirely, not color-only fallback

**Possible regression risks (verify in Commit 1):**
- `background: none` currently no-ops (declaration dropped by unwhitelisted shorthand); after ADR-055 it sets `background-color: transparent` explicitly. Should be equivalent -- transparent fill is a no-op in paint.
- Existing tests using `background-color:` directly unaffected
- `docs/demo.html` screenshot -- the .card has `background-color: #ffffff` (longhand), so no change from shorthand. Other demo elements might use shorthand and gain fill.

## Implementation plan

Two commits:

1. **try_expand_background + dispatcher hook + tests + fixture**:
   - `stylesheet.cpp`: `try_expand_background` function (~30 lines)
   - `stylesheet.cpp`: dispatcher branch for `"background"` (~5 lines)
   - `tests/test_stylesheet.cpp`: 4-6 tests
   - `docs/adr055-background-shorthand-test.html`: permanent fixture (reuse isolation test pattern)
   - `docs/README.md`: fixture list update

2. **Docs**: ADR-055 -> Implemented, ADR-054 Post-ADR-055 correction, README + CHANGELOG

## Verification

**Unit tests (~5 new):**
- `background: #fff` -> `background-color: #fff`
- `background: #2980b9` -> `background-color: #2980b9`
- `background: none` -> `background-color: transparent`
- `background: #fff !important` -> `background-color: #fff !important`
- `background: url(x) #fff` -> declaration dropped (url form deferred)
- `background: linear-gradient(...)` -> dropped

**Synthetic fixture verification:**
`docs/adr055-background-shorthand-test.html` -- the four-row isolation fixture repurposed as permanent asset:
- Row 1: longhand bg + longhand border (control, must remain green)
- Row 2: shorthand bg + longhand border (**new: must render blue fill**)
- Row 3: longhand bg + shorthand border (must remain orange)
- Row 4: both shorthand (**new: must render purple fill**)

Before ADR-055: rows 2 and 4 empty/white.
After ADR-055: rows 2 and 4 render their intended background color.

**Production verification:** `docs/demo.html` -- any element using shorthand `background:` now fills correctly. Screenshot diff before/after (small delta expected if demo uses shorthand).

**Wikipedia zero-regression check:** the `#fff` color forms now render. Boxes with `background: url(...) #fff` (URL forms) still drop entirely (no change). Boxes with `background: #fff` (color-only) now get white fill instead of transparent -- visual delta, but the **correct** one.

## Acceptance criteria

ADR-055 is complete when:

- [ ] `try_expand_background` handles color-only + none + !important + var-resolved
- [ ] URL / gradient / multi-layer forms drop declaration entirely
- [ ] Dispatcher branch wired in before box-shorthand default
- [ ] ~5 new tests pass (651 -> ~656)
- [ ] Fixture shows rows 2 and 4 now filled
- [ ] No regression on docs/demo.html or other existing fixtures
- [ ] ADR-054 Post-ADR-055 note corrects the "shorthand background: and border: don't parse" claim
- [ ] Post-ADR-055 note in ADR-040 (border shorthand confirmed working; sibling background shipped)
- [ ] README row + CHANGELOG
- [ ] ADR-055 status -> Implemented
