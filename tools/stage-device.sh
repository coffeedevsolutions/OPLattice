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

root=$(cd "$(dirname "$0")/.." && pwd)
art=${1:-$HOME/Documents/PS2/art-out}
out=${2:-$root/_deploy}

[ -d "$art" ] || { echo "no art dir: $art" >&2; exit 1; }
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
            cp "$f" "$out/ART/$base"
            sips -z "$COV_H" "$COV_W" "$out/ART/$base" >/dev/null
            cov=$((cov + 1)) ;;
        *_LGO.png)
            skipped=$((skipped + 1)) ;;
        *_BG.png)
            cp "$f" "$out/ART/$base"; bg=$((bg + 1)) ;;
        *)
            cp "$f" "$out/ART/$base"; other=$((other + 1)) ;;
    esac
done
echo "  art       $cov COV (-> ${COV_W}x${COV_H}), $bg BG, $other other, $skipped LGO skipped"

# --- the loader ---------------------------------------------------------------
# Left at the root for you to place. It goes wherever your chip already boots
# OPL from -- usually mc0:/OPL/OPNPS2LD.ELF -- not on the device itself.
cp "$root/patches/build/OPNPS2LD.ELF" "$out/OPNPS2LD.ELF"
echo "  loader    $(du -h "$out/OPNPS2LD.ELF" | cut -f1 | tr -d ' ')"

echo
echo "size (without ISOs): $(du -sh "$out" | cut -f1 | tr -d ' ')"
echo
echo "still to do by hand:"
echo "  1. copy OPNPS2LD.ELF to where your chip boots OPL from"
echo "  2. copy the rest of $out to the root of the drive"
echo "  3. copy your ISOs into DVD/ on the drive (CD/ only for CD-based games)"
echo "  4. first boot: Settings -> Interface -> Theme -> GridHard, then save"
