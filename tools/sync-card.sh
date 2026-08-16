#!/usr/bin/env bash
#
# Sync _deploy to the card, verify it landed, and only then eject.
#
#   tools/sync-card.sh [volume]        # default /Volumes/PSxMemCard
#
# Written after a sync that reported success while quietly writing a stale
# theme. The theme lives in themes/thm_GridHard/ and _deploy/THM/ is a *copy*
# made by stage-device.sh; editing the source and syncing _deploy therefore ships
# whatever the copy last held. Nothing warned, because rsync had faithfully
# copied exactly what it was pointed at.
#
# So this script refreshes that copy from source every run, and -- more
# importantly -- verifies the card against the source afterwards and refuses to
# eject if anything differs. A sync that cannot prove it worked is a sync that
# has to be repeated blind.
#
# What it will not touch:
#   DVD/ CD/   the games, several GB, nothing here has any business there
#   CFG/       merged, never copied: OPL owns the $-prefixed keys and configWrite
#              rewrites whole files, so rsync would discard settings made on the
#              console. See tools/merge-cfg.py.
#   MemoryCards/ .sd2psx/   the card's own boot state
set -euo pipefail

V=${1:-/Volumes/PSxMemCard}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
THEME=thm_GridHard

[ -d "$V" ] || { echo "not mounted: $V" >&2; exit 1; }
[ -d "$V/ART" ] || { echo "$V has no ART/ -- is that really the card?" >&2; exit 1; }

# --- 0. the step whose absence caused this script to exist ---------------------
rsync -a --delete --exclude '._*' --exclude '.DS_Store' \
      "themes/$THEME/" "_deploy/THM/$THEME/"
echo "theme refreshed from source into _deploy"

# --- 1. backup ----------------------------------------------------------------
B="_card-backup/$(date +%Y%m%d-%H%M%S)"
mkdir -p "$B"
for d in ART CFG THM APPS; do
    [ -d "$V/$d" ] && cp -R "$V/$d" "$B/" 2>/dev/null || true
done
echo "backup  $B  ($(du -sh "$B" | cut -f1))"

# --- 2. write -----------------------------------------------------------------
export COPYFILE_DISABLE=1
python3 tools/merge-cfg.py "$V/CFG" _deploy/CFG | tail -1
rsync -a --delete --no-perms --no-owner --no-group \
      --exclude '._*' --exclude '.DS_Store' _deploy/ART/ "$V/ART/"
rsync -a --delete --no-perms --no-owner --no-group \
      --exclude '._*' --exclude '.DS_Store' "_deploy/THM/$THEME/" "$V/THM/$THEME/"
cp _deploy/APPS/OPNPS2LD.ELF "$V/APPS/OPNPS2LD.ELF"

# A stray ._ file is not cosmetic: it walks into the use-after-free at
# supportbase.c:337 and corrupts the game list.
dot_clean -m "$V" 2>/dev/null || true
find "$V" -name '._*' -not -path '*/System Volume Information/*' -delete 2>/dev/null || true
find "$V" -name '.DS_Store' -delete 2>/dev/null || true

# --- 3. verify against SOURCE, not against _deploy ----------------------------
# Comparing the card to _deploy would have passed happily while both were stale.
fail=0
for f in _deploy/ART/*.png; do
    cmp -s "$f" "$V/ART/$(basename "$f")" || { echo "  ART differs: $(basename "$f")"; fail=1; }
done
for f in "themes/$THEME"/*; do
    cmp -s "$f" "$V/THM/$THEME/$(basename "$f")" || { echo "  THEME differs: $(basename "$f")"; fail=1; }
done
cmp -s patches/build/OPNPS2LD-MMCE.ELF "$V/APPS/OPNPS2LD.ELF" || { echo "  ELF differs"; fail=1; }

n_junk=$(find "$V" -name '._*' 2>/dev/null | wc -l | tr -d ' ')
[ "$n_junk" = 0 ] || { echo "  $n_junk resource-fork files left"; fail=1; }

src_elems=$(grep -c '^info[0-9]*:' "themes/$THEME/conf_theme.cfg")
card_elems=$(grep -c '^info[0-9]*:' "$V/THM/$THEME/conf_theme.cfg")
[ "$src_elems" = "$card_elems" ] || { echo "  info chain: source $src_elems, card $card_elems"; fail=1; }

echo
printf "  ART    %s files\n"  "$(ls "$V/ART" | wc -l | tr -d ' ')"
printf "  THM    %s files, %s info elements\n" "$(ls "$V/THM/$THEME" | wc -l | tr -d ' ')" "$card_elems"
printf "  ELF    %s bytes\n"  "$(stat -f%z "$V/APPS/OPNPS2LD.ELF")"
printf "  DVD    %s files, %s (untouched)\n" "$(ls "$V/DVD" | wc -l | tr -d ' ')" "$(du -sh "$V/DVD" | cut -f1)"

if [ "$fail" != 0 ]; then
    echo
    echo "VERIFY FAILED -- leaving the card mounted so it can be fixed and re-run." >&2
    exit 1
fi

sync
diskutil unmount "$V"
