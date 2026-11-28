# ADR-084: Bilingual Documentation and Worldwide Contributor Policy

**Status:** Accepted
**Date:** 2026-11-28
**Related:** ADR-002 (Language Policy), ADR-003 (README Language)

## Context

HoneyBee was founded by Cairo University students, and students from Cairo
University remain the project's founding and primary community. Existing
contributor-facing wording presents contribution eligibility as limited to
Cairo University students. The project intends to welcome contributors
worldwide, regardless of their location or institutional affiliation.

The existing documentation-language decisions establish Arabic as the current
language for documentation while assigning English to the root README. These
rules predate the plan for a substantial technical handbook to be developed in
both English and Arabic. A coherent policy is needed for technical
documentation that supports both language communities while preserving the
meaning and status of project decisions.

## Decision

### A. Official languages for technical documentation

English and Arabic are both official languages for HoneyBee technical
documentation.

- Major handbook volumes and significant technical-documentation sections
  should have corresponding English and Arabic editions.
- Both editions should communicate the same technical meaning and project
  policy. Translation must not silently change the meaning or status of a
  technical decision.
- The synchronization status of corresponding editions must be visible.
- Where practical, both editions should be updated together. If a translation
  is delayed, its status must make the delay clear rather than implying that an
  outdated edition is current.

This decision establishes policy; it does not claim that the planned handbook
or translated documentation already exists.

### B. Preservation of technical syntax

Translation must preserve technical syntax, including source-code identifiers,
commands and command-line arguments, filenames and paths, API names and
symbols, configuration keys, code examples, and other machine-readable
content. These items must not be translated or altered in a way that changes
their technical meaning.

Arabic explanatory prose should use appropriate right-to-left presentation.
Code and technical syntax should remain left-to-right. This decision does not
prescribe a PDF toolchain or publishing system.

### C. Worldwide contributor eligibility

Contributors are welcome regardless of country, university, or institutional
affiliation. Cairo University students remain HoneyBee's founding and primary
community, and international contributors are part of the project's intended
contributor community. Eligibility is based on the project's contribution
requirements and community standards, not university enrollment.

This policy decision does not itself update existing README or CONTRIBUTING
wording.

## Relationship to ADR-002 and ADR-003

ADR-002 states that documentation is Arabic “for now,” while also setting
Arabic for user-facing text, preferring Arabic comments while accepting
English, and requiring English identifiers and commit messages. This ADR
specializes ADR-002's current-language rule for technical documentation by
establishing English and Arabic as official languages and requiring paired
editions for major volumes and significant sections. The other ADR-002
language rules remain unchanged.

ADR-003 assigns English to `README.md`, Arabic to `CONTRIBUTING.md` and
`docs/*.md` / `docs/decisions/*.md`, and says ADR-002 remains active except
where specialized. This ADR specializes the Arabic-only rule for major
technical documentation and significant technical sections, including
technical decision documentation that is brought into the paired-edition
policy. It does not change the current language assignments for the root
README or CONTRIBUTING guide; their future eligibility wording and language
policy remain migration work. Other documentation not covered by the paired
technical-documentation policy remains governed by the existing decisions
until separately reviewed.

ADR-003 also directs that existing ADRs not be edited in place. This ADR
respects that rule: it records the new policy without rewriting ADR-002,
ADR-003, or earlier decisions. Any future translation of an ADR must
faithfully preserve the original decision's status and history.

## Explicit exclusions

This ADR makes no decision about mandatory AI use or disclosure,
AI-generated contribution attribution, privacy rules for external AI
services, issue assignment or response-time commitments, contributor
recruitment quotas, operating-system support, or PDF generation tools and
publishing infrastructure. Those topics remain separate decisions or
implementation work.

## Consequences and migration

Subsequent work will need to:

- Update the root README and CONTRIBUTING guide to reflect worldwide
  contributor eligibility.
- Establish a bilingual handbook structure and conventions for visible
  translation synchronization status.
- Review and progressively translate existing technical documentation.
- Keep translations of ADRs faithful to the original decision's meaning,
  status, and history.

These are future tasks and are not completed by this policy proposal.

## Record of changes

- **2026-11-28:** Accepted.
