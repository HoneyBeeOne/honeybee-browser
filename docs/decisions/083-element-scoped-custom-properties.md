# ADR-083: Element-scoped CSS custom properties

**Status:** Accepted design; implementation deferred
**Date:** 2026-11-27
**Supersedes:** The stylesheet-global custom-property resolution model in ADR-026 and the extraction behavior in ADR-078
**Related:** ADR-010 (cascade), ADR-021 (incremental restyle), ADR-022 (targeted cascade), ADR-026 (CSS variables), ADR-027 (@media), ADR-059 (selector index), ADR-078 (media-scoped extraction)

## Context

ADR-026 introduced parse-time `var()` substitution from a stylesheet-global `Stylesheet::variables` map. ADR-078 widened which root rules contribute to that map and made custom properties inheritable, but custom-property values are still not computed in each element's cascade context. This cannot represent element-scoped declarations, inherited computed values, or the specified CSS computed-value timing of `var()` substitution.

The current source confirms this legacy design remains in place: stylesheet parsing fills `Stylesheet::variables` and calls `resolve_declarations`; inline style parsing receives the same map; the cascade independently chooses declarations per element and `propagate_inheritance()` runs afterward. ADR-022 also establishes targeted subtree restyling and the `[render-pipeline][algorithmic]` guard against cascading unaffected descendants. This ADR records the approved destination architecture; it does not claim that destination is implemented.

## Decision

### 1. Per-element custom-property cascade and computed values

Custom properties participate in the cascade on each element using that element's matching declarations, including inline declarations and the established cascade priority metadata. After the winning specified custom-property declarations for an element are known, compute their values on that element. Ordinary declarations containing `var()` are then computed against that element's computed custom-property set.

A child's `var()` references use the child's computed custom properties, including inherited *computed values*. They do not re-evaluate an ancestor's specified expression in the child's environment.

### 2. Required computation and inheritance order

For each element, process the cascade and computation in this strict order:

1. Filter rules by the current media environment and viewport before selecting declaration winners.
2. Select the winning specified declarations, retaining custom-property and ordinary-property winners and their cascade metadata.
3. Compute this element's custom-property values, resolving references, fallbacks, and cycles. Missing or invalid values become guaranteed-invalid.
4. Compute ordinary-property values using this element's computed custom properties. A failed winning declaration becomes invalid at computed-value time and does not cause selection of a lower-priority candidate.
5. Propagate inherited computed values to children. Child-local specified declarations and computed values take precedence over inherited values according to the cascade/inheritance model.

This order ensures custom properties are computed where specified before their computed values are inherited, while each element's ordinary declarations resolve in its own computed custom-property environment.

### 3. Separate custom-property computed storage

Computed custom properties must be stored separately from ordinary `ComputedStyle` declarations. Their storage must represent both a computed token value and a guaranteed-invalid state; absence alone must not conflate an unset property with a property whose winning value computed invalid. Inheritance propagates the parent's computed custom-property value or guaranteed-invalid state as appropriate, subject to child declarations and CSS-wide keyword behavior supported by the implementation.

### 4. Preserve specified winners for targeted recomputation

Keep the winning specified ordinary declarations distinct from their computed ordinary declarations. Preserve the raw specified values needed to re-run substitution, together with sufficient winner metadata (importance, origin, inline status, specificity, source order, and specifying element) to identify and reuse the same winner.

Targeted restyle must be able to recompute affected elements from cached winners and updated inherited computed custom-property inputs without rematching stylesheet rules for unaffected descendants. The full-cascade and targeted-restyle paths must produce equivalent computed results. Cached winner reuse must preserve the existing `[render-pipeline][algorithmic]` performance guard: the targeted path must not devolve into a full descendant rule rematch. Extend regression coverage for both value equivalence and the existing apply-rule-call-count style guard when implementation is undertaken.

### 5. Invalid-at-computed-value behavior

Cascade selection happens before `var()` substitution. If the winning declaration becomes invalid at computed-value time because a referenced custom property is guaranteed-invalid and no fallback applies, that declaration is invalid; the cascade does not resurrect a lower-priority declaration. The property's applicable invalid-value behavior is then applied (including inheritance/initial behavior as defined by the property's semantics), rather than selecting a losing candidate retroactively.

### 6. References, fallbacks, cycles, and inheritance

Resolve nested and recursive `var()` references against the element's custom-property dependency graph. A usable fallback handles an unavailable or guaranteed-invalid referenced value; a failed reference without a usable fallback invalidates the containing computed value. Detect cycles in the dependency graph and mark cyclic custom properties guaranteed-invalid; references to them may use a fallback. Preserve inherited *computed* values, so descendants receive the parent's resolved value rather than re-resolving the parent's raw expression against child-local definitions.

### 7. Media filtering

Media applicability must be evaluated before custom-property winner selection. A declaration inside a non-matching media rule is not a candidate and cannot affect the element's computed custom properties. This replaces parse-time global extraction that has to omit width-constrained rules because it lacks the current viewport.

### 8. Full cascade and targeted restyle consistency

Full cascade and targeted restyle must share the same winner and computed-value semantics. The full path computes parent custom properties before descendants. The targeted path reuses cached winning specified declarations where their selector/cascade inputs remain valid, recomputes affected custom-property and ordinary computed values as inheritance inputs change, and propagates the results through the required affected subtree without descendant rule rematching. Tests must establish equality with full cascade and retain the `[render-pipeline][algorithmic]` performance guard.

### 9. CSS-wide keywords

Stage 3B.2 is approved to recognize CSS-wide keywords during custom-property computation. These rules describe future behavior; they are not implemented by Stage 3B.1.

For custom-property declarations, `initial` produces the guaranteed-invalid value. `inherit` uses the parent's computed custom-property value, including its guaranteed-invalid state. `unset` behaves like `inherit`, because custom properties inherit by default. These same rules apply when `var()` substitution produces one of these keywords as the final value.

`revert` and `revert-layer` must be recognized and represented by a distinct internal `DeferredKeyword` state. `DeferredKeyword` is distinct from `Tokens`, `GuaranteedInvalid`, and an absent custom-property entry. Stage 3B.2 assigns no CSS computed-value, inheritance, or `var()` fallback semantics to this state. It must never be silently converted to tokens, absence, or `GuaranteedInvalid`; implementations must not invent behavior for declarations or references that encounter it.

CSS-wide keyword handling for ordinary-property declarations remains outside Stage 3B.2. This section does not claim that any of these custom-property behaviors are currently implemented.

### 10. Remove stylesheet-global substitution

Migrate away from using `Stylesheet::variables` as the source of truth for `var()` substitution. Custom properties are resolved from each element's cascaded and computed custom-property storage, including applicable UA/author/inline contributions, rather than a parse-time stylesheet-global map. Remove the legacy global resolution path as part of implementation while preserving compatible parsing and public behavior where intended. The old ADR-026 and ADR-078 behavior remains historical context, not the target architecture.

## Approved parent/child regression example

```css
.parent { --message: red; color: var(--message); }
.child { --message: blue; }
```

```html
<div class="parent">
  <span class="child">text</span>
</div>
```

Expected result: the child's computed `color` is **red**. The parent computes `--message: red` and then computes `color: red`. The child computes its own `--message: blue`, but inherits the parent's already-computed `color`; the child's local custom property does not retroactively recompute that inherited value. This is the critical regression for preserving computed-value and inheritance ordering.

## Deferred implementation and validation

This ADR approves the design only. Source, tests, and the rendering pipeline must be updated in a later explicitly approved implementation stage. That work must add parent/child, fallback, invalid-winning-declaration, cycle, media-filtering, full-versus-targeted-restyle, and algorithmic-guard regression tests. No implementation or tests are asserted by this decision record.
