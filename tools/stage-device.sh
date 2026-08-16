#!/usr/bin/env bash
#
# Build the folder tree OPL expects on a storage device, ready to copy across.
#
#   tools/stage-device.sh [art-dir] [out-dir]
#
# Everything except the ISOs, which are far too large to stage twice -- copy
# those straight to <out-dir>/DVD (or CD) yourself, or point rsync at the drive.
#
# Layout comes from supportbase.c:822, which is also the list OPL creates on its
# own the first time it sees a device:
#
#   CFG/  per-game settings      THM/  themes, one folder each
#   LNG/  translations           ART/  <STARTUP>_<PATTERN>.png
#   VMC/  virtual memory cards   CHT/  cheats
#   APPS/ homebrew
#
# Covers are downscaled on the way in. Twelve are resident at once in the grid
# and VRAM, not RAM, is the binding constraint -- see themes/README.md.

set -euo pipefail

THEME=thm_GridHard
COV_W=100
COV_H=150
COVHD_W=120        # the details-page rect, in texels
COVHD_H=180

root=$(cd "$(dirname "$0")/.." && pwd)
art=${1:-$HOME/Documents/PS2/art-out}
out=${2:-$root/_deploy}

[ -d "$art" ] || { echo "no art dir: $art" >&2; exit 1; }
command -v magick >/dev/null || { echo "needs ImageMagick" >&2; exit 1; }
command -v sips >/dev/null || { echo "needs sips (macOS)" >&2; exit 1; }

echo "staging into $out"
rm -rf "$out"
mkdir -p "$out"/{CFG,LNG,VMC,CHT,APPS,ART,CD,DVD,"THM/$THEME"}

# --- theme --------------------------------------------------------------------
# thmReadEntry only accepts a directory whose name contains "thm_", and displays
# it with the first four characters stripped, so this shows up as "GridHard".
cp "$root/themes/$THEME/"* "$out/THM/$THEME/"
echo "  theme     $(ls "$out/THM/$THEME" | wc -l | tr -d ' ') files"

# --- art ----------------------------------------------------------------------
# LGO is deliberately not staged. No element references it, and at 400x~200 it
# was the largest single texture in the theme.
cov=0 bg=0 other=0 skipped=0
for f in "$art"/*.png; do
    base=$(basename "$f")
    case "$base" in
        *_COV.png)
            # Two copies at two resolutions, each sized to the rect that draws
            # it. "Full size for the info page" was the old rule and it was
            # wrong: that rect samples 120 texels however large the texture is,
            # so a 300x450 source only bought a 2.5x hardware downscale and
            # 131 KB of VRAM. One Lanczos pass to the rect is sharper and
            # cheaper. See the art commit for why the same error was in BG,
            # LGO and SCR.
            magick "$f" -filter Lanczos -resize "${COV_W}x${COV_H}!" \
                +dither -colors 256 -define png:color-type=3 \
                -define png:bit-depth=8 -strip "PNG8:$out/ART/$base"
            magick "$f" -filter Lanczos -resize "${COVHD_W}x${COVHD_H}!" \
                +dither -colors 256 -define png:color-type=3 \
                -define png:bit-depth=8 -strip "PNG8:$out/ART/${base%_COV.png}_COVHD.png"
            cov=$((cov + 1)) ;;
        *_LGO.png)
            # Not copied as-is. tools/make-logos.py rewrites these onto one
            # 200x120 canvas, because the sources run 49 to 440 tall and a fixed
            # element size would squash them. Run it after this script.
            skipped=$((skipped + 1)) ;;
        *_BG.png)
            cp "$f" "$out/ART/$base"; bg=$((bg + 1)) ;;
        *)
            cp "$f" "$out/ART/$base"; other=$((other + 1)) ;;
    esac
done
echo "  art       $cov COV (-> ${COV_W}x${COV_H}) + $cov COVHD (full size), $bg BG, $skipped LGO skipped"

# --- the loader ---------------------------------------------------------------
# Left at the root for you to place. It goes wherever your chip already boots
# OPL from -- usually mc0:/OPL/OPNPS2LD.ELF -- not on the device itself.
#
# MMCE by default: a mainline build has no memory-card-SD support at all, so
# flashing one would make that device disappear. Pass BUILD=mainline only if
# you know you are not using MMCE.
build=${BUILD:-MMCE}
elf="$root/patches/build/OPNPS2LD-$build.ELF"
[ -f "$elf" ] || { echo "no such build: $elf" >&2; exit 1; }
cp "$elf" "$out/APPS/OPNPS2LD.ELF"
echo "  loader    $(du -h "$out/APPS/OPNPS2LD.ELF" | cut -f1 | tr -d ' ') ($build) -> APPS/"

echo
echo "size (without ISOs): $(du -sh "$out" | cut -f1 | tr -d ' ')"
echo
echo "still to do by hand:"
echo "  1. copy all of $out to the root of the drive"
echo "  2. copy your ISOs into DVD/ on the drive (CD/ only for CD-based games)"
echo "  3. first boot: Settings -> Interface -> Theme -> GridHard, then save"
echo
echo "The loader goes in APPS/ on the drive, not inside a memory card image."
echo "On an SD2PSX the games device is reached as mmce?:, not mass:, so the slot"
echo "that picks it up is PS2BBL's L1 binding -- hold L1 at boot. A normal boot"
echo "is unaffected and still runs whatever the mc? paths resolve to, so the two"
echo "loaders coexist and reverting is deleting one file."
