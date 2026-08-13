# SHELF — Phase 0 addendum: what gsKit's texture manager actually does

Source: `ps2dev/gsKit` at `43122eb` (2026-07-16). Confirmed to be the linked
version — `gsTexManager.h` in the repo is byte-identical to the one in
`ps2dev/ps2dev:latest`, whose `libgskit.a` was built 2026-08-02 with no commits
in between.

Implementation is `ee/gs/src/gsTexManager.c`, about 300 lines. All three
questions are answerable from it.

---

## Q1 — Eviction policy, and behaviour when the working set exceeds VRAM

**It is not LRU. It is a two-frame use-count predictor**, in `_blockGetWeight`:

| condition | weight | intent |
|---|---|---|
| `iUseCount == iUseCountPrev` | `iUseCountPrev` | done this frame, wanted next |
| `iUseCount < iUseCountPrev` | `(prev − cur) + prev` | still wanted this frame **and** next |
| `iUseCount > iUseCountPrev` | `1 + iUseCount` | unsure this frame, wanted next |

`nextFrame` rolls `iUseCount` into `iUseCountPrev` and zeroes it. So the manager
scores by *how often a texture was bound per frame*, and protects textures whose
binds are still arriving. That is strictly better than LRU for a menu: a
background bound every frame outranks a cover bound once.

**Allocation** (`_blockAlloc`) is first-fit over a doubly-linked block list. On
failure it raises an eviction threshold from 0 upward, freeing every block whose
weight is at or below it and coalescing neighbours, until a large enough hole
appears. So over-subscription degrades by evicting the least-predicted textures
first — no failure path, no silent drop. A texture evicted and re-bound simply
re-transfers.

**The one hazard.** `_blockAlloc` is `while (block == NULL)` with **no failure
exit**. If a single request exceeds the whole pool, eviction can never satisfy
it and the loop spins forever incrementing `weight`. Nothing in gsKit prevents
this:

```
gsKit pool                     1,900,544     4 MB − two 640×448 CT24 buffers
OPL maxSize (textures.c:102)   1,474,560
largest possible bind          1,475,584     texture + CLUT, allocated as one block
headroom                         424,960
```

We are safe only because OPL's `maxSize` is below the pool. **`maxSize` is the
guard, and it lives in OPL, not gsKit.** Raising it above ~1.9 MB converts a
too-large asset from a clean rejection into a hard hang. Directly relevant to
Phase 1, which is about to raise dimension recommendations — the recommendations
must stay inside `maxSize`, and `maxSize` must stay inside the pool.

## Q2 — Do texture and CLUT transfer and evict atomically?

**Yes, unambiguously.** `bind` sizes one allocation for both and places them
adjacently:

```c
block = _blockAlloc(tsize + csize);
...
tex->Vram     = block->iStart;
tex->VramClut = block->iStart + tsize;
```

One block, one owner, one eviction unit. `free` and the eviction path clear
`block->tex`, releasing both halves together; `invalidate` zeroes `Vram` and
`VramClut` together. CLUT width/height are derived from the PSM (16×16 for T8,
8×2 for T4), so `csize` is always right.

**The Phase 3 concern about CLUT pairing does not apply.** There is no way to
evict a texture and strand its palette in this design.

## Q3 — Do early `bind` calls work as prefetch?

**Yes mechanically, with one caveat worth designing around.**

`bind` transfers whenever `tex->Vram == 0`, so calling it before draw time does
upload early, and the `iUseCount++` protects the texture from eviction in the
same frame. That is a working prefetch.

Two honest limits:

**It is not asynchronous.** Both transfer paths (`gsKit_texture_send_inline`,
`gsKit_texture_send`) put the upload in the current GS packet. "Prefetch" means
*earlier in this frame's draw list*, not *during idle time*. There is no DMA you
can overlap with anything.

**It biases the predictor the wrong way.** A prefetched-but-not-drawn texture
ends the frame with `iUseCount = 1`, `iUseCountPrev = 0` → branch three, weight
`1 + 1 = 2`. Next frame, if it is genuinely drawn, `iUseCount` rises and it
stays protected — fine. But if the user reverses direction and it is never
drawn, it sits at weight 2 for a frame while a cover that *was* drawn twice and
is now done sits at weight 2 as well. Prefetching a row you then scroll away
from briefly protects it as strongly as something real. At one row of lookahead
this is noise; at aggressive lookahead it would start costing residency for
things actually on screen.

---

## Verdict: thin wrapper. Closer to nothing than to a rewrite.

Of the four things Phase 3's design doc was to cover:

| planned | status |
|---|---|
| EE-RAM decoded-asset cache with eviction | **exists** — `texcache.c`, `image_cache_t`, async loads via `IO_CACHE_LOAD_ART` |
| VRAM slot allocation | **exists** — first-fit with coalescing |
| CLUT pairing as a first-class object | **exists** — single-block allocation makes it structural |
| LRU eviction from per-frame visibility | **exists and is better than LRU** — two-frame use-count prediction |
| prefetch hints | **works via early `bind`**, with the caveat above |

Writing a manager alongside this would mean reimplementing a working allocator
to gain nothing, and would fight it for the same VRAM.

**What SHELF actually needs is small, and none of it is an allocator:**

1. **A prefetch call** — a named wrapper over `bind` so grid code expresses
   intent without every call site knowing about gsKit. Roughly ten lines.
2. **Accounting for the debug line.** `VRAM COVERS n · x.xx MB` cannot come from
   gsKit: `__head` is file-scope, exposed only through `F_gsTextManagerInternals`
   within the library, and is not in any public header. Two options — track it
   OPL-side by counting what we bind and its sizes (no gsKit change, slightly
   approximate because it cannot see evictions), or carry a small patch adding a
   stats accessor. I would start OPL-side; the number is a diagnostic, not a
   contract.
3. **A guard on `maxSize`** so it can never exceed the pool, since that is the
   difference between a rejected asset and a hung console.
4. **The `themes.c:395` else branch**, already approved for Phase 1.

Recommend Phase 3 be struck as a phase and its remnants folded into Phase 6,
where the grid that needs prefetch is being built. That removes the campaign's
largest block of speculative work and its longest gate.

The one thing worth keeping from Phase 3's framing is the instrumentation. The
debug line was described as "the acceptance test made visible", and that is
still true — it just measures a manager we already have rather than one we
write.
