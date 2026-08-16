# SHELF — Phase 1: art dimensions, pipeline, and two safety fixes

Re-scoped after Phase 0 found the loader already handles indexed PNGs. No loader
changes. What follows is the art contract, plus the `maxSize` guard and the
`themes.c:395` else branch.

---

## 0. A correction to my own Phase 0 numbers

Phase 0 quoted asset costs as `width × height × bytes`. That is wrong — the GS
allocates in 256-byte blocks and gsKit rounds block counts up to a 4×8 alignment
(`gsKit_texture_size`, `gsTexture.c`). Real costs are higher:

| asset | Phase 0 said | actually |
|---|---|---|
| background 640×448 T8 | 286,720 | **327,680** |
| background 460×215 CT32 (current) | 395,600 | **491,520** |
| cover 300×450 T8 | 135,000 | **163,840** |

Everything below uses the faithful figures. The framebuffer check confirms the
model: 640×448 CT24 computes to exactly 1,146,880, the value the budget assumed.

---

## 1. Dimension recommendations

Constraints: `maxSize` = **1,474,560** per texture (`textures.c:102`); texture
pool = **1,900,544**. Margin below is against `maxSize`, texture + CLUT.

### Per-asset ceiling

| asset | recommended | format | bytes + CLUT | margin under maxSize |
|---|---|---|---|---|
| background, full screen | **640×448** | T8 | 328,704 | **1,145,856** |
| cover, details page | **420×630** | T8 | 287,744 | **1,186,816** |
| cover, library grid | **160×240** | T8 | 50,176 | 1,424,384 |
| logo | **400×240** | T8 | 115,712 | 1,358,848 |
| screenshot | **320×240** | T8 | 82,944 | 1,391,616 |

Every recommendation clears `maxSize` by at least 1.1 MB. Nothing here is close
to the ceiling — the binding constraint is simultaneity, not any single asset.

### Simultaneity is the real limit

**Details page**, one of everything at the sizes above:

```
background 640x448    328,704
cover 420x630         287,744
logo 400x240          115,712
screenshot 320x240     82,944
screenshot 320x240     82,944
                    ---------
                      898,048   of 1,900,544 pool   headroom 1,002,496
```

**Library grid** is where the two-cover-size split becomes necessary. The mockup
shows 7×2 = 14 visible, and Phase 6 wants a prefetch row, so budget 21:

| grid cover | each | ×14 | ×21 |
|---|---|---|---|
| 420×630 | 287,744 | 4,028,416 | — |
| 300×450 | 164,864 | 2,308,096 | — |
| **160×240** | **50,176** | **702,464** | **1,053,696** |
| 128×192 | 33,792 | 473,088 | 709,632 |

**420×630 covers cannot tile.** Fourteen of them is 4 MB against a 1.9 MB pool.
The grid needs its own smaller pattern, exactly as this theme already does with
`COV` (100×150, twelve tiles) versus `COVHD` (300×450, one on the details page).
That split is not a workaround; it is the correct shape for the constraint, and
it already exists in the tree.

160×240 at 14 tiles is 702 KB, leaving room for a background and UI; at 21 with
prefetch it is 1.05 MB, still inside the pool with the background. That is the
recommendation.

**A note on grid cover sizing:** 128×192, 112×168 and 96×144 all cost the same
33,792 bytes, because block alignment rounds them to the same block count. If
160×240 proves tight in Phase 6, 128×192 is the next step down and there is no
point going below it.

---

## 2. `ps2art.sh` — ImageMagick invocations

Indexed PNG is what the loader wants: `PNG_COLOR_TYPE_PALETTE` at bit depth 8
gives `GS_PSM_T8` with a 256-entry CLUT. Bit depth 4 gives T4 and 16 colours,
which is not worth the quality cost for photographic art.

> **Settled: no dithering.** The three-way comparison was run and Riemersma
> lost. See the campaign ledger's dithering verdict. The `-dither Riemersma`
> invocation below is kept for the record; `tools/palettize-art.py` implements
> the decision and takes `--dither` to re-run any of the three.

**Backgrounds — undithered, per the verdict.** The premise below turned out to
be false for this library: the art is cel-shaded key art with large flat regions
and few long gradients, so 256 colours does not band and dithering only adds
grain. The original reasoning, kept because it was sound and simply did not
match the content:

```bash
magick "$src" \
  -resize 640x448^ -gravity center -extent 640x448 \
  -dither Riemersma -colors 256 \
  -define png:color-type=3 -define png:bit-depth=8 \
  "PNG8:$out"
```

**Covers — no dithering.** Cover art is mostly flat colour and type; dithering
adds visible noise to lettering and buys little. Quantise cleanly:

```bash
magick "$src" \
  -resize 420x630^ -gravity center -extent 420x630 \
  +dither -colors 256 \
  -define png:color-type=3 -define png:bit-depth=8 \
  "PNG8:$out"
```

**Grid covers** — same, at `160x240`.

**Logos — preserve alpha.** These have transparency, and the loader reads `tRNS`
alongside `PLTE`, so palette alpha survives. Do not flatten:

```bash
magick "$src" \
  -resize 400x240 -background none -gravity center -extent 400x240 \
  +dither -colors 255 \
  -define png:color-type=3 -define png:bit-depth=8 \
  "PNG8:$out"
```

`-colors 255` rather than 256 leaves one palette slot for fully transparent,
which ImageMagick allocates itself; asking for 256 with an alpha channel can
push the result to RGBA and silently defeat the whole exercise.

**Verify the output is actually indexed** — this is the check worth putting in
`ps2art.sh`, because an RGBA fallback is invisible until VRAM runs out.

> **Correction.** The `%[channels]` check below does not work. ImageMagick 7
> reports `srgb` for a genuine palette PNG, so this recipe fails a file that is
> in fact correct. Read the IHDR instead — colour type 3 is what libpng reports
> and what `textures.c:502` branches on. Nothing else is authoritative:
>
> ```bash
> python3 -c "import sys,struct;d=open(sys.argv[1],'rb').read(26);print('PALETTE' if d[25]==3 else f'NOT indexed (colour type {d[25]})')" "$out"
> ```

```bash
magick identify -format '%f %[channels] %[bit-depth] %k\n' "$out"
# unreliable -- see the correction above
```

**On the PS2 alpha convention:** 0x80 is fully opaque, not 0xFF. The loader
already handles this (`texReadPixels*` in `textures.c`), so the pipeline should
emit ordinary 0–255 alpha and let the loader convert. Do not pre-scale alpha in
ImageMagick.

---

## 3. `maxSize` guard

`_blockAlloc` in gsKit loops `while (block == NULL)` with no failure exit. A
single request larger than the texture pool can never be satisfied by eviction,
so it spins forever — a hung console, not a rejected asset. Nothing in gsKit
prevents this; OPL's `maxSize` is the only thing standing between us and it.

An init-time assertion, failing loudly:

```
pool     = 4 MB − 2 × gsKit_texture_size(width, height, PSM)
required = maxSize + gsKit_texture_size(16, 16, GS_PSM_CT32)   // worst-case CLUT
assert(required <= pool)
```

Today: 1,475,584 required against a 1,900,544 pool — **424,960 of margin**. The
assertion exists so that anyone raising `maxSize`, or switching to a taller
video mode that shrinks the pool, gets a loud failure at boot instead of a
freeze the first time a large asset loads.

It must be init-time rather than compile-time: the pool depends on the video
mode, which is a runtime setting.

---

## 4. `themes.c:395` else branch

```c
if (texDiscoverLoad(&texture->source, path, texId) >= 0)
    /* use it */;
/* ...and nothing here */
```

The loader returns `ERR_BAD_DIMENSION` on an oversized asset and a distinct code
on a missing file. Both are discarded at this line, which is the entire
silent-failure mode the campaign set out to remove.

Adding an else that logs the path and the reason, and substitutes a visible
placeholder, satisfies "never let a texture failure be silent again". The
placeholder matters more than the log: `LOG` output is invisible without a debug
build, and the failure mode we are fixing is one you can only see on a TV.

---

## 5. Previewer VRAM panel

Corrected in the Phase 0 commit: the budget was computed from 640×480
framebuffers when NTSC is 640×448, understating the texture pool by 163,840
bytes. Now **1,900,544**, and the test that pinned the wrong value carries the
reason.

Still outstanding, and worth doing while the numbers are fresh: the panel
computes cost as `width × height × 4`, which is the same naive arithmetic
corrected in §0. It should use the block-aligned figure and account for the CLUT
when a source is indexed. That would make the panel agree with the console
rather than approximate it — the same class of fix as the `fontSpec` bug.

---

## Deliverables status

| item | state |
|---|---|
| dimension recommendations with margins | this document |
| `ps2art.sh` invocations per asset class | this document |
| previewer budget correction | **done**, committed in Phase 0 |
| previewer block-alignment accounting | **not done** — proposed above |
| `maxSize` guard | specified; needs implementing |
| `themes.c:395` else branch | specified; needs implementing |

The last three are code. They are small and I can carry them straight into a
build, but Phase 1's gate is verification, so this is where I stop for your read
on the numbers and the ImageMagick contract before touching the tree.
