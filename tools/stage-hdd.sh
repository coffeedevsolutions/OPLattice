#!/usr/bin/env bash
#
# stage-hdd.sh — read every disc's real serial, build all three art files for
# each, fill the gaps with placeholders, and prove the result is complete.
#
#   tools/stage-hdd.sh [ps2-dir] [art-out]
#
# Defaults: ps2-dir=~/Documents/PS2, art-out=<ps2-dir>/art-out.
# Sources art from ~/Downloads. Nothing in Downloads is read-write; nothing
# outside art-out is written.
#
# ------------------------------------------------------------------------------
# WHY THIS EXISTS
#
# The hand pipeline (ripps2.sh -> artmap.txt -> ps2art.sh) fails quietly in
# three places, and each failure looks exactly like success:
#
#   1. THE SERIAL IS GUESSED, NOT READ. ripps2.sh takes the first regex match
#      from `strings` over the whole ISO. That is usually the boot serial, but
#      any disc that mentions another game's serial in a string table -- demos,
#      sequels with shared assets, anything with a save-data compatibility list
#      -- can hand back the wrong one. OPL does not care what `strings` found:
#      it reads BOOT2 from SYSTEM.CNF (supportbase.c:335) and names art from
#      that. A wrong serial in artmap.txt produces art with a valid-looking
#      filename that no game ever loads.
#
#   2. ps2art.sh ITERATES THE MAP, NOT THE LIBRARY. It walks artmap.txt, so a
#      slug pointing at a serial no disc has still reports COV/BG/LGO created.
#      Conversely a disc with no artmap entry is never mentioned at all -- it
#      just shows up on the console as a blank tile weeks later.
#
#   3. A MISSING SOURCE FILE PRINTS ONE LINE AND MOVES ON. `MISS BG` scrolls
#      past in a wall of successes and the run still exits 0.
#
# So this script inverts the direction: the discs are the source of truth, the
# map is checked against them, and the run fails loudly if any disc ends without
# a full set of art.
#
# ------------------------------------------------------------------------------
# WHAT IT WRITES  (art-out, the intermediate stage -- not the device layout)
#
#   <SERIAL>_COV.png   300x450   box art
#   <SERIAL>_BG.png    460x215   wide key art
#   <SERIAL>_LGO.png   400xN     logo, transparent ground
#
# These are the sizes tools/make-bg.py, tools/make-logos.py and
# tools/stage-device.sh expect to consume. Run this first, those after.
#
# Discs with no art get placeholders instead: a flat brown rectangle for COV and
# BG, and a fully transparent LGO. The point is that a game with no art looks
# deliberate on the grid rather than broken, and that coverage is total, so the
# next run's report is about what changed rather than a standing backlog.
# ==============================================================================

set -euo pipefail

PS2=${1:-$HOME/Documents/PS2}
OUT=${2:-$PS2/art-out}
SRC=${DOWNLOADS:-$HOME/Downloads}
ARTMAP=${ARTMAP:-$PS2/artmap.txt}

# Placeholder brown. Dark enough to sit in the theme's near-black page without
# reading as a loading state, light enough to be visibly a deliberate fill.
BROWN=${BROWN:-#4A3728}

COV_W=300 COV_H=450
BG_W=460  BG_H=215
LGO_W=400 LGO_H=120

[ -d "$PS2" ]      || { echo "no ps2 dir: $PS2" >&2; exit 1; }
[ -f "$ARTMAP" ]   || { echo "no artmap: $ARTMAP" >&2; exit 1; }
[ -d "$SRC" ]      || { echo "no downloads dir: $SRC" >&2; exit 1; }
command -v python3 >/dev/null || { echo "needs python3" >&2; exit 1; }
if   command -v magick  >/dev/null 2>&1; then IM=magick
elif command -v convert >/dev/null 2>&1; then IM=convert
else echo "needs ImageMagick -- brew install imagemagick" >&2; exit 1
fi

mkdir -p "$OUT"
work=$(mktemp -d); trap 'rm -rf "$work"' EXIT

# ==============================================================================
# 1. SERIALS — read BOOT2 out of SYSTEM.CNF, the way OPL does
# ==============================================================================
# Two sector layouts matter here. A DVD rip is a plain 2048-byte-sector ISO9660
# image. A CD rip taken as raw sectors (MODE1/2352 or MODE2/2352, which is what
# a .bin/.cue pair from a PS1-era or CD-based PS2 disc is) carries 2352 bytes per
# sector: sync and header first, then the same 2048 bytes of user data. Reading
# such a file as if it were 2048-byte sectors finds no volume descriptor at all,
# which is why check-art.py reports RidgeRacerV_JP.bin as unreadable when its
# art is in fact present and correct.
#
# Emits TSV: file <TAB> status <TAB> serial-or-note
echo "== 1. serials =================================================================="
python3 - "$PS2" > "$work/serials.tsv" <<'PY'
import os, re, struct, sys

USER = 2048

def layout(f):
    """Return (sector_size, data_offset) by finding CD001 at descriptor 16."""
    for size, off in ((2048, 0), (2352, 24), (2352, 16), (2336, 8)):
        f.seek(16 * size + off + 1)
        if f.read(5) == b"CD001":
            return size, off
    return None, None

def read(f, lba, n, size, off):
    """Read n bytes of user data starting at logical block lba."""
    out = bytearray()
    while len(out) < n:
        f.seek(lba * size + off)
        out += f.read(min(USER, n - len(out)))
        lba += 1
    return bytes(out[:n])

def root_entries(f, size, off):
    pvd = read(f, 16, USER, size, off)
    rec = pvd[156:156 + 34]
    lba = struct.unpack("<I", rec[2:6])[0]
    ln = struct.unpack("<I", rec[10:14])[0]
    data = read(f, lba, ln, size, off)
    i = 0
    while i < len(data):
        n = data[i]
        if n == 0:                      # padding to the end of this sector
            i = (i // USER + 1) * USER
            continue
        r = data[i:i + n]
        nlen = r[32]
        yield (r[33:33 + nlen],
               struct.unpack("<I", r[2:6])[0],
               struct.unpack("<I", r[10:14])[0])
        i += n

def serial_of(path):
    sz = os.path.getsize(path)
    if sz < 1 << 20:
        return "UNREADABLE", f"only {sz} bytes -- truncated or failed rip"
    with open(path, "rb") as f:
        size, off = layout(f)
        if size is None:
            return "UNREADABLE", "no ISO9660 volume descriptor"
        try:
            entries = list(root_entries(f, size, off))
        except Exception as e:
            return "UNREADABLE", f"root directory unreadable ({e})"
        for name, lba, ln in entries:
            if name.upper().startswith(b"SYSTEM.CNF"):
                cnf = read(f, lba, min(ln, USER), size, off).decode("latin-1")
                m = re.search(r"BOOT2\s*=\s*cdrom0:\\?([^;\s]+)", cnf, re.I)
                if not m:
                    return "NOSERIAL", "SYSTEM.CNF has no BOOT2"
                return "OK", m.group(1).strip()
        return "NOSERIAL", "no SYSTEM.CNF (video or data disc)"

root = sys.argv[1]
for fn in sorted(os.listdir(root)):
    p = os.path.join(root, fn)
    if not os.path.isfile(p) or not fn.lower().endswith((".iso", ".bin", ".img")):
        continue
    st, val = serial_of(p)
    print(f"{fn}\t{st}\t{val}")
PY

discs=$(grep -c . "$work/serials.tsv" || true)
ok=$(awk -F'\t' '$2=="OK"' "$work/serials.tsv" | wc -l | tr -d ' ')
awk -F'\t' '$2=="OK"     {printf "  %-14s %s\n", $3, $1}' "$work/serials.tsv"
awk -F'\t' '$2=="NOSERIAL"{printf "  %-14s %s  -- %s\n", "(none)", $1, $3}' "$work/serials.tsv"
awk -F'\t' '$2=="UNREADABLE"{printf "  %-14s %s  -- %s\n", "BAD", $1, $3}' "$work/serials.tsv"
echo "  $discs disc images, $ok with a bootable serial"

# Duplicate serials across two images means one of them is mislabelled, or the
# same game was ripped twice. Either way the second art write silently clobbers
# the first, so it is worth stopping on.
dupes=$(awk -F'\t' '$2=="OK"{print $3}' "$work/serials.tsv" | sort | uniq -d)
if [ -n "$dupes" ]; then
  echo
  echo "  !! two disc images share a serial:"
  for s in $dupes; do
    printf "     %s -> %s\n" "$s" "$(awk -F'\t' -v s="$s" '$3==s{printf "%s ", $1}' "$work/serials.tsv")"
  done
fi

# ==============================================================================
# 2. MAP — check artmap.txt against the discs, both directions
# ==============================================================================
echo
echo "== 2. artmap =================================================================="

# serial -> slug, skipping blanks and comments the same way ps2art.sh does
awk -F: '
  /^[[:space:]]*(#|$)/ {next}
  NF>=2 {gsub(/^[ \t]+|[ \t]+$/,"",$1); gsub(/^[ \t]+|[ \t]+$/,"",$2)
         if ($1!="" && $2!="") print $2 "\t" $1}
' "$ARTMAP" | sort > "$work/map.tsv"

awk -F'\t' '$2=="OK"{print $3}' "$work/serials.tsv" | sort -u > "$work/have.txt"
cut -f1 "$work/map.tsv" | sort -u > "$work/mapped.txt"

# A slug whose serial is on no disc here. Not fatal -- art may be staged ahead of
# a rip, and SLPS_200.01 legitimately belongs to a .bin whose serial now reads
# correctly -- but it is the shape a typo'd serial takes, so it gets named.
orphans=$(comm -23 "$work/mapped.txt" "$work/have.txt" || true)
# A disc with no artmap entry at all. This is the one that costs you a blank
# tile, because nothing in the old pipeline ever mentions it.
unmapped=$(comm -13 "$work/mapped.txt" "$work/have.txt" || true)

if [ -n "$orphans" ]; then
  echo "  art mapped to a serial no disc here has:"
  for s in $orphans; do printf "     %-14s (%s)\n" "$s" "$(awk -F'\t' -v s="$s" '$1==s{print $2}' "$work/map.tsv")"; done
else
  echo "  every artmap entry matches a disc"
fi
if [ -n "$unmapped" ]; then
  echo "  discs with NO artmap entry -- these get placeholders:"
  for s in $unmapped; do printf "     %-14s %s\n" "$s" "$(awk -F'\t' -v s="$s" '$3==s{print $1}' "$work/serials.tsv")"; done
else
  echo "  every disc has an artmap entry"
fi

# ==============================================================================
# 3. ART PASS — one disc at a time, keyed on the serial the disc actually boots
# ==============================================================================
echo
echo "== 3. art ====================================================================="

findsrc() {  # slug, suffix -> path on stdout, or non-zero
  local slug=$1 suf=$2 f
  [ -n "$slug" ] || return 1
  for ext in png jpg jpeg webp avif PNG JPG JPEG WEBP AVIF; do
    f="$SRC/${slug}-gameArt${suf}.${ext}"
    [ -f "$f" ] && { printf '%s' "$f"; return 0; }
  done
  return 1
}

real=0 filled=0
: > "$work/placeholders.txt"

while IFS=$'\t' read -r gid; do
  slug=$(awk -F'\t' -v s="$gid" '$1==s{print $2; exit}' "$work/map.tsv")
  line="  $gid" ; gaps=""

  # --- COV, box art -----------------------------------------------------------
  # PNG24:/PNG32: on every write, placeholder or not. A flat fill compresses to
  # a one-entry palette at 1 bit per pixel, which is a valid PNG that the hand
  # written readers in make-logos.py and make-playbtn.py refuse -- they support
  # 8-bit only. Pinning the format costs a few KB on three files and removes a
  # whole category of "works until the art is missing" failure.
  if src=$(findsrc "$slug" ""); then
    "$IM" "$src" -resize "${COV_W}x${COV_H}" -strip "PNG24:$OUT/${gid}_COV.png"
    line+="  COV" ; real=$((real+1))
  else
    "$IM" -size "${COV_W}x${COV_H}" "xc:$BROWN" -strip "PNG24:$OUT/${gid}_COV.png"
    line+="  cov*" ; gaps+="COV "
  fi

  # --- BG, wide key art -------------------------------------------------------
  if src=$(findsrc "$slug" "-BG"); then
    "$IM" "$src" -resize "${BG_W}x${BG_H}^" -gravity center -extent "${BG_W}x${BG_H}" \
      -strip "PNG24:$OUT/${gid}_BG.png"
    line+="  BG" ; real=$((real+1))
  else
    "$IM" -size "${BG_W}x${BG_H}" "xc:$BROWN" -strip "PNG24:$OUT/${gid}_BG.png"
    line+="  bg*" ; gaps+="BG "
  fi

  # --- LGO, logo on a transparent ground --------------------------------------
  # The placeholder is fully transparent rather than brown: the logo is drawn
  # over the hero, so a filled rect there would be a brown slab across the art,
  # whereas nothing at all just leaves the hero showing.
  #
  # PNG32: is load-bearing. Left to itself ImageMagick notices that an all-
  # transparent image needs no colour and writes 1-bit greyscale (IHDR colour
  # type 0, depth 1), which is a perfectly good PNG that make-logos.py's reader
  # rejects outright -- it handles 8-bit only. Forcing 32-bit RGBA keeps the
  # placeholder in the one format every tool downstream can open.
  if src=$(findsrc "$slug" "-LG"); then
    "$IM" "$src" -trim +repage -resize "${LGO_W}x" -strip "PNG32:$OUT/${gid}_LGO.png"
    line+="  LGO" ; real=$((real+1))
  else
    "$IM" -size "${LGO_W}x${LGO_H}" xc:none -strip "PNG32:$OUT/${gid}_LGO.png"
    line+="  lgo*" ; gaps+="LGO "
  fi

  if [ -n "$gaps" ]; then
    filled=$((filled+1))
    printf '%s\t%s\t%s\n' "$gid" "${slug:-(unmapped)}" "${gaps% }" >> "$work/placeholders.txt"
  fi
  echo "$line"
done < "$work/have.txt"

echo "  $real real files, $filled discs needed at least one placeholder (*)"

# ==============================================================================
# 4. VERIFY — every disc serial has all three, on disk, non-empty
# ==============================================================================
echo
echo "== 4. coverage ================================================================"

incomplete=0
while IFS= read -r gid; do
  for p in COV BG LGO; do
    f="$OUT/${gid}_${p}.png"
    if [ ! -s "$f" ]; then
      echo "  MISSING  $(basename "$f")"
      incomplete=$((incomplete+1))
    fi
  done
done < "$work/have.txt"

total=$(wc -l < "$work/have.txt" | tr -d ' ')
if [ "$incomplete" -eq 0 ]; then
  echo "  all $total serials have COV + BG + LGO"
else
  echo "  $incomplete files missing -- see above"
fi

# ==============================================================================
# 5. REPORT — what still has no real art
# ==============================================================================
echo
echo "== 5. still without art ======================================================="

if [ -s "$work/placeholders.txt" ]; then
  printf "  %-14s %-16s %-14s %s\n" SERIAL SLUG PLACEHOLDER DISC
  while IFS=$'\t' read -r gid slug gaps; do
    disc=$(awk -F'\t' -v s="$gid" '$3==s{print $1; exit}' "$work/serials.tsv")
    printf "  %-14s %-16s %-14s %s\n" "$gid" "$slug" "$gaps" "$disc"
  done < "$work/placeholders.txt"
  echo
  echo "  To fix one: download <slug>-gameArt.png / -BG.png / -LG.png into"
  echo "  $SRC, add '<slug>:<SERIAL>' to $ARTMAP if it is not there, re-run."
  echo
  # A blank LGO has no ink to measure. make-logos.py returns None for that case
  # rather than 0.0 and leaves the panel at the page colour, so these read as
  # deliberately empty instead of a bright white slab. Say so, because the
  # alternative is silently trusting that the guard is still there.
  if grep -q 'LGO' "$work/placeholders.txt"; then
    echo "  NOTE  those blank LGOs carry no ink, so make-logos.py leaves their"
    echo "        info-page panel at the page colour rather than plating white."
  fi
else
  echo "  nothing -- every disc has real COV, BG and LGO"
fi

echo
# Order is not a preference. stage-device.sh opens with rm -rf on its output
# dir, and the three make-* tools write into that same dir, so running it after
# any of them silently deletes their work.
echo "next, in this order:"
echo "  tools/stage-device.sh     wipes and builds _deploy: COV 100x150, COVHD 120x180"
echo "  tools/make-bg.py          re-cuts BG to 418x180 from the Downloads originals"
echo "  tools/make-logos.py       LGO onto one 150x120 canvas, writes PANEL"
echo "  tools/make-playbtn.py     BTN, sampled per game"

[ "$incomplete" -eq 0 ] || exit 1
