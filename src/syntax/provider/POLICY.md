# C16 colouring-ownership policy (frozen before implementation)

Status: **frozen** for R2. This document is the normative description of how the
base highlighter (KSyntaxHighlighting) and the overlay (tree-sitter spans)
share one language document without double colouring. The API in
`owner_arbiter.h` implements exactly this policy; the verifier judges against
this text, not against implementation comments.

## 1. The invariant

For one language and one document, **every byte range has at most one colouring
owner at any moment**. An "owner" is one of:

| owner kind | owner id | who paints |
|---|---|---|
| `Base` | `ksyntaxhighlighting` | the base highlighter, while base highlighting is enabled for the language |
| `Overlay` | the overlay id (e.g. `tree-sitter`) | exactly one overlay component |
| `None` | — | nobody paints; the editor renders plain default text |

Consequences:

1. Two overlays may never own overlapping byte ranges. It is not "last writer
   wins": the second claim is **rejected** and the existing owner is reported.
2. The base highlighter owns all bytes that no overlay owns — but only while
   base highlighting is enabled for that language. Disabling base highlighting
   turns those bytes into `None`, never into a second overlay.
3. Ownership is per *language* (`verilog` and `systemverilog` are different
   namespaces even when both resolve to a Verilog-family definition).

## 2. Ownership transitions

```
            claim(overlay)                      release(overlay)
   Base  ─────────────────────▶  Overlay   ─────────────────────▶  Base
     │                              │
     │ setBaseEnabled(false)        │ setBaseEnabled(false)
     ▼                              ▼
    None ◀──────────────────────  None   (release with base disabled)
```

* `claim(language, overlay_id, start, end)`:
  * `start >= end` or empty overlay id → `InvalidRange`, nothing changes;
  * overlap with an existing overlay → `Conflict`, existing owner reported,
    nothing changes;
  * otherwise → `Granted`, ownership of exactly `[start,end)` transfers to the
    overlay (adjacent intervals of the same overlay merge).
* `release(language, overlay_id, start, end)`: removes that overlay's ownership
  inside the interval (it may split an interval); bytes fall back to `Base` or
  `None` according to the base switch. Releasing bytes the overlay does not own
  is a no-op and returns `false`.
* `releaseAll(language, overlay_id)`: used when tree-sitter is switched off
  (A1h) or the document/language changes — all overlay ownership is dropped, the
  base highlighter repaints.
* `setBaseEnabled(language, bool)`: the only way to make bytes ownerless. Used
  when the base engine cannot colour a language (e.g. a missing syntax
  definition) or when the base is switched off in settings.

## 3. Paint rule (who must call what)

1. The overlay layer computes candidate ranges from the SyntaxProvider
   (`highlightSpans()`), and for each range **must** `claim()` it before
   painting.
   * `Granted` → paint the range with the overlay attribute.
   * `Conflict` → **do not paint** and do not retry with a different owner; log
     it once per overlay pair (this is a defect in the overlay partitioning, not
     a user-visible error).
2. The base highlighter must paint only bytes whose owner is `Base` (or `None`
   with no overlay). The integration point is `ownerSpans()`: the editor's
   base-colour pass skips ranges owned by an overlay.
3. Removing or rebuilding the overlay (document reload, tree-sitter disabled,
   grammar switch, unsaved buffer swap) calls `releaseAll()` for that overlay
   first; the base repaints the released bytes.
4. Ownership is **view-independent**: multiple views of the same document share
   one arbiter entry; a split view must not claim again (the claim is idempotent
   by rejection — the second view skips painting because the range is already
   owned; only one view paints overlay colours is *not* acceptable, so the owner
   is recorded once and every view paints what the owner decided). The overlay
   layer therefore owns its ranges once per (language, document) and lets every
   view render them; `claim()` is called by the single overlay instance, never
   per view.

## 4. A1g judgement (queryable assertion)

`OwnerArbiter::assertSingleOwner(language, start, end)` is the acceptance
predicate for "逐 range 单一 owner":

* it returns true iff `ownerSpans()` partitions `[start,end)` exactly — every
  byte covered once, no gap, no overlap, every span non-empty;
* `detail` receives either `single owner across N span(s)` or a violation reason
  (`leading gap`, `trailing gap`, `gap or overlap between spans`, `empty span`);
* it reports *ownership*, not *paint*: a green assertion plus the paint rules of
  §3 is the double-colouring proof the editor tests assert.

Tests in `tests/test_syntax_provider.cpp` pin this down: default base ownership,
claim transfer, overlay/overlay conflict with owner reporting, adjacent claims,
partial release splitting an interval, base-disabled `None` ownership, and
`assertSingleOwner` staying true across all transitions.

## 5. Failure modes and required behaviour

| situation | required behaviour |
|---|---|
| tree-sitter switched off at runtime | `SyntaxService::createProvider()` returns null; overlay calls `releaseAll()`; base repaints (A1h) |
| grammar not compiled in / missing binding | provider null; `Base` ownership stays; no crash |
| parser/query error | provider reports `hasSyntaxErrors()`; overlay **must not** claim from a broken tree; `Base` stays |
| base definition missing for a language | `setBaseEnabled(language,false)`; bytes are `None` until an overlay claims; editor renders plain text |
| two overlays in one build | only one overlay id is configured per language (settings); if a second appears, its claims are rejected and logged |

## 6. Why this lives in the provider layer

C16 says the policy must be frozen *before* implementation and shared by the base
and the overlay. The arbiter therefore sits next to the `SyntaxProvider` seam
(same library), owns no painting code, and is the single source of truth for both
`src/syn-overlay` and the editor's base-colour pass. C16's "same language, same
range, one owner" is thus an enforceable invariant, not a convention.
