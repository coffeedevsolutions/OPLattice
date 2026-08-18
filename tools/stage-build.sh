#!/usr/bin/env bash
#
# Copy a finished build into the repo: ELF, sources, and the regenerated patch.
#
#   tools/stage-build.sh <build-tree>
#
# Written after the staging step lied for six commits.
#
# It used to be one line inlined into a shell invocation:
#
#     (cd $BUILD && git diff -- src include Makefile) > patches/09-shelf-sidebar.patch
#
# with nothing checking git's exit status. The build tree lived under
# /private/tmp, /private/tmp was pruned, and its .git went with it -- so git
# printed a fatal to stderr, wrote nothing to stdout, and the surrounding command
# carried on copying the ELF and reporting success. The patch went stale while the
# binary beside it stayed current, and nothing said so.
#
# So every step here is checked, and the checks are the point:
#
#   - the build tree must be a git repository, and the diff must succeed
#   - the patch must be non-empty and must mention every sentinel below, which
#     are symbols from the newest work; a diff that builds cleanly but has
#     silently lost half the tree still fails here
#   - shelf.c and shelf.h are NEW files, so `git diff` never sees them and they
#     ship whole rather than as a diff. They get their own sentinel list checked
#     against the copy, which is what actually goes in the box
#   - the ELF must be newer than the newest source it claims to contain, because
#     a stale binary staged beside fresh sources is the same class of lie
#
# Sentinels are cheap to maintain and catch the failure that is otherwise
# invisible: add one when a change introduces a symbol you would notice missing.
set -euo pipefail

B=${1:?usage: tools/stage-build.sh <build-tree>}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

PATCH=patches/09-shelf-sidebar.patch
ELF=$B/OPNPS2LD.ELF
PATCH_SENTINELS=(rmSetPanX shelfPushX menuGetActiveList ELEM_TYPE_ATTRIBUTE_LIST rmDrawGradV oplRecentPrune MENU_CHEAT_SETTINGS forceGlobalCheat)
SHELF_SENTINELS=(libDrawAlphabet LIB_ALPHA_LABELS RAIL_ICON_W shelfSyncFonts shelfHint shelfRenderInfo infDrawButton homeDateOk INF_DESC_Y METACRITIC COVXL LIB_FTR_H_DRAWN)

[ -d "$B" ]      || { echo "no such build tree: $B" >&2; exit 1; }
[ -f "$ELF" ]    || { echo "no ELF in $B -- build first" >&2; exit 1; }
git -C "$B" rev-parse --git-dir >/dev/null 2>&1 \
  || { echo "$B is not a git repository, so the patch cannot be regenerated." >&2
       echo "Re-clone per patches/README.md and copy patches/shelf/tree/ over it." >&2; exit 1; }

# --- 1. the ELF must not be older than the sources it supposedly contains -----
newest=$(find "$B/src" "$B/include" -name '*.c' -o -name '*.h' | xargs ls -t 2>/dev/null | head -1)
if [ -n "$newest" ] && [ "$newest" -nt "$ELF" ]; then
    echo "STALE: $newest is newer than the ELF. Rebuild before staging." >&2
    exit 1
fi

# --- 2. regenerate the patch, and refuse a silent empty one ------------------
tmp=$(mktemp)
trap 'rm -f "$tmp"' EXIT
git -C "$B" diff -- src include Makefile > "$tmp"
[ -s "$tmp" ] || { echo "the regenerated patch is empty -- refusing to write it" >&2; exit 1; }
for s in "${PATCH_SENTINELS[@]}"; do
    grep -q "$s" "$tmp" || { echo "patch is missing sentinel '$s' -- refusing" >&2; exit 1; }
done
for s in "${SHELF_SENTINELS[@]}"; do
    grep -q "$s" "$B/src/shelf.c" \
      || { echo "shelf.c is missing sentinel '$s' -- refusing" >&2; exit 1; }
done
mv "$tmp" "$PATCH"
trap - EXIT

# --- 3. sources, whole, so the patch is never the only record ----------------
for f in $(git -C "$B" diff --name-only -- src include Makefile) src/shelf.c include/shelf.h; do
    [ -f "$B/$f" ] || continue
    mkdir -p "patches/shelf/tree/$(dirname "$f")"
    cp "$B/$f" "patches/shelf/tree/$f"
done
cp "$B/src/shelf.c" patches/shelf/src-shelf.c

# --- 4. the binary, to all three places that must never disagree -------------
#
# _deploy/ is gitignored, so anything hand-authored under it is one `rm -rf`
# from gone -- which is how APPS/OPL/title.cfg came to exist only in a card
# backup. The authoritative copy lives in patches/build/ and is laid down here,
# so a wiped _deploy is fully reconstructible from the repo.
mkdir -p _deploy/APPS/OPL
cp patches/build/APPS-OPL-title.cfg _deploy/APPS/OPL/title.cfg
cp "$ELF" patches/build/OPNPS2LD-MMCE.ELF
cp "$ELF" _deploy/APPS/OPNPS2LD.ELF
cp "$ELF" _deploy/APPS/OPL/OPNPS2LD.ELF

printf "staged  ELF %s bytes\n" "$(stat -f%z "$ELF")"
printf "        patch %s bytes, %s files\n" \
    "$(wc -c < "$PATCH" | tr -d ' ')" \
    "$(grep -c '^diff --git' "$PATCH")"
printf "        tree %s files\n" "$(find patches/shelf/tree -type f | wc -l | tr -d ' ')"
