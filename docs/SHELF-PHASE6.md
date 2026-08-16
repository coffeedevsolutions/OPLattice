# SHELF — Phase 6: prefetch, instrumentation, and the Library grid

Split deliberately. The half that Phase 0's addendum folded in from struck
Phase 3 — the prefetch wrapper and the VRAM accounting — is **built and
visible**. The grid itself is designed below and stops at the gate, because it
needs a lifecycle decision that is not mine to make quietly.

---

## Shipped

### `rmPrefetchTexture(GSTEXTURE *)`

A named wrapper over `gsKit_TexManager_bind`, so grid code can express intent
without every call site knowing about gsKit.

It is honest about what it is not. gsKit has no asynchronous path — both
transfer routines put the upload in the current GS packet — so this means
*earlier in this frame's draw list*, never *during idle time*. What it buys is
that a texture needed at the bottom of a grid is resident when its quad is
issued, instead of causing a transfer mid-draw.

One caveat to design around, from the addendum: `bind` increments the use count,
and gsKit's predictor scores by binds per frame. A texture prefetched and then
not drawn ends the frame looking as wanted as one drawn twice. At a row of
lookahead that is noise. At aggressive lookahead it starts costing residency for
things actually on screen.

### `rmVramBoundBytes()` / `rmVramBoundCount()`

Reset each frame in `rmStartFrame`, incremented on every bind including the
overlay path, CLUT included when the texture has one.

Rendered in the Apps page footer as `~123 KB / 14 binds`. **The tilde is load
bearing.** This counts what was asked for; it cannot see evictions, because
gsKit's block list is file-scope in `gsTexManager.c` and reachable only through
an internal symbol. It answers "is this page anywhere near the pool" and does
not answer "how full is VRAM". No gsKit patch, per the ledger.

Putting it on a page that exists now rather than holding it for the grid means
it can be checked on hardware immediately, and means the wrapper it accompanies
is not shipping as dead code with nothing to show.

---

## The grid, designed

### Geometry

The rule established while correcting the art: a `GameImage` defaults to
`SCALING_RATIO`, so drawn width is three quarters of declared width in 16:9.
For a 2:3 cover to *display* as 2:3, drawn texels must be **1:2** —

```
(w/640 × 16) : (h/480 × 9) = 2 : 3   ⟹   w/h = 1/2
```

So a cover drawn at **72 × 144 texels** displays correctly. Six columns and two
rows fit inside 32px margins with room to spare, and twelve visible matches what
the classic grid already keeps resident.

| | bytes each | ×12 | ×18 (one prefetch row) |
|---|---|---|---|
| 72×144 T8 + CLUT | 40,960 | 491,520 | 737,280 |

Against a 1,900,544 pool with a 122,880 background, eighteen resident leaves
over a megabyte. The budget is not the constraint here; it never was.

**The existing `COV` art is 100×150**, shared with the classic grid, so the
Library page would resample it 1.39× horizontally. Correct would be a 72×144
source, but `COV` cannot change without changing the classic grid too — that is
a separate decision and not worth bundling.

### The decision this needs

`cacheGetTexture` requires per-item storage: `item->cache_id[cache->userId]` and
`item->cache_uid[...]`, both `int *` arrays sized by `theme->gameCacheCount`.
A hardcoded page cannot claim a slot without the theme knowing about it.

**Option A — parallel arrays owned by shelf.c.** Allocate `int *` storage
indexed by game id, allocated on first use of the page and freed when the list
is rebuilt. Self-contained, and with `SHELF UI` off nothing is allocated at all,
which keeps the toggle genuinely inert.

**Option B — reserve a slot in the theme's count.** Simpler code, but it makes
every theme pay for a cache the SHELF page may never open, and couples a
hardcoded page to the theme loader.

**I would take A**, but it carries the real question: the game list is rebuilt
on device refresh, and stale cache ids pointing at a rebuilt list is exactly the
class of bug that produces art drawn against the wrong game. The invalidation
hook needs choosing before any of it is written.

### The other open question

**Which list does Library show?** The classic screen is device-paged: L1/R1 move
between USB, MMCE, HDD and so on. A "Library" that silently shows only one
device is a lie by omission; one that merges them needs a rule for duplicates
and a way to indicate origin. Neither is a detail to settle mid-implementation.

---

## Recommendation

Take the grid as a separate step with those two answers settled first. What is
here now — the wrapper and the counter — is what the grid will consume, and both
are testable on hardware today.
