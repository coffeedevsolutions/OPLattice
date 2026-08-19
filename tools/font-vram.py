#!/usr/bin/env python3
"""Atlas count per font slot, by simulating OPL's own packer against real glyphs.

Replicates, exactly:
  fntsys.c   FT_Set_Char_Size(size*64, size*64, 72*ws, 72*hs)  with ws=0.75, hs=1.0
             (480p progressive anamorphic: hn/h = 1, PAR 1:1 * 0.75)
             glyphs placed at slot->bitmap.width x slot->bitmap.rows, NO padding
             ATLAS_MAX 4 atlases of 256x256 GS_PSM_T8 per slot
  atlas.c    allocPlace(): guillotine split, wider piece made longer
  gsKit      texture_size(): 256-byte blocks, block counts rounded 4x8
"""
import os

import freetype

ATLAS, AMAX = 256, 4
WS, HS = 0.75, 1.0


def gsize(w, h, psm):
    bw, bh = {"T8": (16, 16)}.get(psm, (8, 8))
    bx = -(-w // bw); bx = -(-bx // 4) * 4
    by = -(-h // bh); by = -(-by // 8) * 8
    return bx * by * 256


ATLAS_BYTES = gsize(ATLAS, ATLAS, "T8") + gsize(16, 16, "CT32")   # texture + CLUT


class Node:
    __slots__ = ("x", "y", "w", "h", "a", "b")

    def __init__(s, x, y, w, h):
        s.x, s.y, s.w, s.h, s.a, s.b = x, y, w, h, None, None


def place(n, w, h):
    if n is None or n.w < w or n.h < h:
        return None
    if n.a is None and n.b is None:
        dx, dy = n.w - w, n.h - h
        if dx < dy:
            n.a = Node(n.x + w, n.y, dx, h)
            n.b = Node(n.x, n.y + h, n.w, dy)
        else:
            n.a = Node(n.x, n.y + h, w, dy)
            n.b = Node(n.x + w, n.y, dx, n.h)
        return n
    return place(n.a, w, h) or place(n.b, w, h)


def atlases_for(path, size, chars, variation=None):
    f = freetype.Face(path)
    if variation:
        try:
            f.set_var_named_instance(variation)
        except Exception:
            pass
    f.set_char_size(size * 64, size * 64, int(72 * WS), int(72 * HS))
    boxes = []
    for c in chars:
        f.load_char(c, freetype.FT_LOAD_RENDER)
        bm = f.glyph.bitmap
        if bm.width and bm.rows:
            boxes.append((bm.width, bm.rows))
    roots, dropped = [], 0
    for w, h in boxes:
        for r in roots:
            if place(r, w, h):
                break
        else:
            if len(roots) < AMAX:
                r = Node(0, 0, ATLAS, ATLAS)
                roots.append(r)
                if not place(r, w, h):
                    dropped += 1
            else:
                dropped += 1
    return len(roots), dropped, boxes


ASCII = "".join(chr(c) for c in range(32, 127))

# Candidate faces to compare. The OPL font is read from this repository, so the
# reference point is fixed; the rest are downloads. Point FONT_DIR at wherever
# yours live -- these are Google Fonts archives, unpacked as they ship.
REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
D = os.path.join(os.environ.get("FONT_DIR", os.path.expanduser("~/Downloads")), "")
FONTS = {
    "PoeVeticaNew (embedded, today)":
        (os.path.join(REPO, "assets", "PoeVeticaNew.ttf"), None),
    "SUSEMono-Regular (static)": (D + "SUSE_Mono/static/SUSEMono-Regular.ttf", None),
    "SUSEMono variable (DEFAULT = Thin)": (D + "SUSE_Mono/SUSEMono-VariableFont_wght.ttf", None),
    "Asimovian-Regular": (D + "Asimovian/Asimovian-Regular.ttf", None),
    "BitcountGridSingle variable": (D + "Bitcount_Grid_Single/BitcountGridSingle-VariableFont_CRSV,ELSH,ELXP,slnt,wght.ttf", None),
    "LibreBarcode128Text": (D + "Libre_Barcode_128_Text/LibreBarcode128Text-Regular.ttf", None),
}

print(f"one atlas = {gsize(ATLAS,ATLAS,'T8'):,} tex + {gsize(16,16,'CT32'):,} clut "
      f"= {ATLAS_BYTES:,} bytes;  ATLAS_MAX={AMAX} -> {AMAX*ATLAS_BYTES:,} ceiling per slot\n")
print(f"{'face':<38}{'size':>5}{'glyphs':>8}{'atlas':>7}{'bytes':>10}  {'note'}")
for name, (path, var) in FONTS.items():
    for size in (9, 12, 17, 46):
        n, dropped, boxes = atlases_for(path, size, ASCII, var)
        note = f"!! {dropped} glyphs DROPPED" if dropped else ""
        mx = max((b[0] for b in boxes), default=0), max((b[1] for b in boxes), default=0)
        note = note or f"widest {mx[0]}x{mx[1]}"
        print(f"{name:<38}{size:>5}{len(boxes):>8}{n:>7}{n*ATLAS_BYTES:>10,}  {note}")
    print()

# Digits+colon only -- what the clock slot actually caches.
print("clock slot, digits and colon only (11 glyphs):")
for name in ("SUSEMono-Regular (static)", "Asimovian-Regular", "BitcountGridSingle variable"):
    path, var = FONTS[name]
    n, _, boxes = atlases_for(path, 46, "0123456789:", var)
    print(f"  {name:<38}{n:>3} atlas  {n*ATLAS_BYTES:>9,} bytes")

print("\natlas occupancy (glyph area / 65536 px), full printable ASCII:")
for name in ("PoeVeticaNew (embedded, today)", "SUSEMono-Regular (static)",
             "Asimovian-Regular", "BitcountGridSingle variable"):
    path, var = FONTS[name]
    row = []
    for size in (9, 12, 17, 46):
        _, _, b = atlases_for(path, size, ASCII, var)
        row.append(f"{size}px {sum(w*h for w,h in b)*100//65536:>3}%")
    print(f"  {name:<38}" + "   ".join(row))
