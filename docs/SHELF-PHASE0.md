# SHELF — Phase 0 reconnaissance

Audit of `ps2-mmce/Open-PS2-Loader @ OPL-MMCE-beta-2` (`bb415d2`) plus the patches
in `patches/`, which is the tree the console runs. Read from source; line
references are to that tree.

**Headline: two of the campaign's premises are already satisfied by the
codebase.** The prompt anticipated exactly this ("if the loader already does
CLUT, surface it and re-plan") so I have stopped rather than writing Phase 1.

---

## 1. The two contradictions

### 1.1 The loader already does palettized textures

`src/textures.c:502` handles `PNG_COLOR_TYPE_PALETTE` and has since before this
fork. It reads the PLTE and tRNS chunks, allocates a CLUT, and sets the PSM:

```c
case PNG_COLOR_TYPE_PALETTE:
    png_get_PLTE(pngPtr, infoPtr, &pngTexture.palette, &pngTexture.numPalette);
    png_get_tRNS(pngPtr, infoPtr, &pngTexture.trans, &pngTexture.numTrans, NULL);
    texture->ClutPSM = GS_PSM_CT32;
    if (bitDepth == 4)       texture->PSM = GS_PSM_T4;   // + 8x2 CLUT
    else if (bitDepth == 8)  texture->PSM = GS_PSM_T8;   // + 16x16 CLUT
```

`texPrepare` (`textures.c:247`) initialises `Clut`, `ClutPSM`, `VramClut` and
`ClutStorageMode = GS_CLUT_STORAGE_CSM1` as first-class fields, and `texFree`
releases the CLUT alongside the pixels. `renderman.c:325` already branches on
`q->txt->Clut` when deciding blend setup.

**Phase 1's loader work does not exist to be done.** What remains of Phase 1 is
real but much smaller: the art pipeline has to *emit* indexed PNGs, and the
dimension constants have to be raised to take advantage.

### 1.2 Streaming already exists, in gsKit

The prompt describes OPL as loading "everything resident up front, silent
failure on overflow", with Phase 3 building an upload-on-visibility manager.
OPL already uses gsKit's texture manager:

| call | where | gsKit's own description |
|---|---|---|
| `gsKit_TexManager_bind` | `renderman.c:333`, per quad | "Bind a texture to VRAM, will automatically transfer the texture" |
| `gsKit_TexManager_nextFrame` | `renderman.c:135`, per frame | "updates texture usage statistics" |
| `gsKit_TexManager_free` | `renderman.c:97` | "The texture will be automatically freed if not used" |
| `gsKit_TexManager_invalidate` | `renderman.c:92` | re-transfer on next bind |

That is upload-on-visibility with usage-driven eviction — the architecture
Phase 3 proposes to build. `VramClut` is a GSTEXTURE field (`gsInit.h:960`)
tracked next to `Vram`, so the CLUT pairing Phase 3 calls out as a first-class
concern is already modelled.

I could not read gsKit's *implementation* — it ships as `libgskit.a` with no
source in the image — so I cannot state its eviction policy is LRU rather than
round-robin, nor confirm it frees a texture and its CLUT atomically. That is the
one open question worth answering before any Phase 3 scope is set, and it is
answerable by fetching gsKit's source rather than by inference.

---

## 2. VRAM budget

Framebuffers are **640×448** on NTSC (`renderman.c:50`), `GS_PSM_CT24`
(`renderman.c:197`), double-buffered, **Z-buffering off** (`renderman.c:203`).
CT24 occupies a 32-bit word in VRAM, so the arithmetic is the same as CT32.

| | bytes | |
|---|---|---|
| VRAM total | 4,194,304 | `__VRAM_SIZE`, `renderman.c:15` |
| 2 × 640×448 CT24 | −2,293,760 | |
| Z-buffer | 0 | disabled |
| **available for textures** | **1,900,544** | **1.81 MB** |

A 16-bit framebuffer (CT16S) would halve the framebuffer cost to 1,146,880 and
raise the texture budget to **3,047,424** (2.91 MB). That is the Phase 1.5
option, and the code already switches to CT16S automatically above 704×576
(`renderman.c:200`), so the path is proven — it is a policy change, not new code.

### The per-texture ceiling, and where your 300k came from

```c
static int maxSize = 720 * 512 * 4;                    // textures.c:102 = 1,474,560
if (width > 1024 || height > 1024) return -1;          // GS hard limit
if (gsKit_texture_size(width, height, psm) > maxSize) return -1;
```

`gsKit_texture_size` is **PSM-aware**, so the ceiling is a byte budget, not a
pixel count:

| format | max pixels under `maxSize` | effective cap |
|---|---|---|
| CT32 / CT24 | 368,640 | **≈ your observed 300k** |
| T8 | 1,474,560 | 1024×1024 dimension cap binds first |
| T4 | 2,949,120 | 1024×1024 binds first |

Your empirical ceiling is this constant. Palettized assets already get four
times the room without touching it.

### Asset costs, current vs palettized

| asset | today | palettized T8 |
|---|---|---|
| cover 300×450 | 540,000 (CT32) | 135,000 + 1 KB CLUT |
| cover 420×630 | 1,058,400 — over `maxSize` | 264,600 |
| background 460×215 | 395,600 | 98,900 |
| background 640×448 full-screen | 1,146,880 — over `maxSize` | **286,720** |

The claim in the prompt holds: a full-screen palettized background costs less
than today's stretched 460×215 at 32-bit, and removes the upscale blur.

**Correction to my own tooling:** the previewer's VRAM panel assumes 640×480
framebuffers and reports a 1,736,704-byte budget. NTSC is 640×448, so the real
figure is **1,900,544** — the panel under-reports headroom by 163,840 bytes. I
will fix that before it informs another sizing decision.

---

## 3. The rest of the audit

**Texture load path.** `texDiscoverLoad` (`textures.c:552`) appends `.png` and
nothing else — PNG only, since `c5a12a0`. Decode is libpng into EE RAM
(`texReadData`), then upload is deferred entirely to `gsKit_TexManager_bind` at
draw time. There is no separate "upload" step to restructure.

**The silent failure is in the theme layer, not the loader.** The loader returns
`ERR_BAD_DIMENSION`. But `themes.c:395` reads
`if (texDiscoverLoad(&texture->source, path, texId) >= 0)` and simply does not
use the texture otherwise — no log, no placeholder. That single `if` is the
whole silent-failure mode, and giving it an else branch satisfies the campaign's
"never let a texture failure be silent again" rule almost for free.

**The cover-art embryo** is `cacheGetTexture` (`texcache.c:119`) over an
`image_cache_t` of `cache_entry_t`, keyed by UID, with async load requests
through `ioPutRequest(IO_CACHE_LOAD_ART, ...)`. It is an EE-RAM decoded-asset
cache with its own eviction — which is the *other* half of what Phase 3's design
doc proposes, already built and already proven on the cover path.

**Theme engine.** Elements resolve to textures through `thmLoadResource` and the
`mutable_image_t` / `image_cache_t` pair; `findDuplicate` (`themes.c:334`) shares
one cache between elements naming the same pattern. A "texture handle"
abstraction would replace `GSTEXTURE *` returned from `cacheGetTexture`.

**Render loop and screens.** `guiSwitchScreen(int)` with `GUI_SCREEN_MAIN /
MENU / INFO / GAME_MENU / APP_MENU` (`gui.h:56-62`). Pad polling is centralised
in `guiReadPads` (`gui.c:1526`, called from the main loop at `gui.c:1627`) —
that is the sidebar's interception point. Apps enumeration is `appsupport.c`.

**CFG scan for Home.** `sbPopulateConfig` (`supportbase.c:755`) is the per-game
read the info screen already uses. Your library is 28 CFGs of roughly 150 bytes
— about 4 KB total. A boot-time scan is not a performance question at this size;
the design question is only when to invalidate it.

---

## 4. Re-plan

The campaign's ordering logic — palettize first as reconnaissance for streaming
— was sound reasoning from the stated premises. Both premises turn out to be
false in your favour, which changes what the phases contain rather than whether
they are worth doing.

**Phase 1 shrinks to an art-pipeline change.** No loader work. Emit indexed
PNGs, raise the constants, verify. Genuinely a weekend, probably an evening.

**Phase 3 needs re-scoping before it is designed.** Its EE-RAM cache exists
(`texcache.c`) and its VRAM residency manager exists (gsKit). The open questions
are narrower and worth answering before committing to a rewrite: what gsKit's
eviction policy actually is, whether it handles CLUT pairs atomically, and
whether a prefetch hint can be expressed through `bind` or genuinely needs a
replacement. Building a manager alongside gsKit's when gsKit's may already
suffice is the expensive mistake available here.

**Phases 4–8 are unaffected.** They sit on plumbing this audit found intact.

Recommended next step is not Phase 1 but a short addendum: pull gsKit's source,
read `gsKit_TexManager_bind` and `nextFrame`, and settle the eviction and CLUT
questions. That is an hour, and it decides whether Phase 3 is a rewrite, a
wrapper, or nothing at all.
