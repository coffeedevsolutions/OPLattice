# Waiting on a mounted volume

Neither the memory card nor the HDD is mounted, so these are queued rather than done.

## Memory card (`mc0:OPL/`)
- `conf_last.cfg` -- delete every `recent*_id` / `recent*_title` key. The code now
  prunes entries that resolve to nothing (`oplRecentPrune`, opl.c), which clears the
  microSD-era ghosts on the next boot; wiping the file is the belt to that braces, and
  is what actually empties Home if the HDD titles happen to share startup ids.
- `OPNPS2LD.ELF` -- 1,385,636 bytes, from `patches/build/OPNPS2LD-MMCE.ELF`.

## HDD (`CFG/`)
- The 28 per-game configs in `_card-backup/20260817-192736/CFG/` were never copied
  across. The HDD holds one auto-created `SLPM_657.90.cfg`, which is why the Library
  shows ISO filenames and the details page has nothing to put in its columns. These
  files carry `Genre` / `Release` / `Developer` / `Rating` / `Description` -- exactly
  the keys the three-column layout reads.
- None of them carries `#Name=`, so titles fall back to the ISO filename either way.
  Either add `#Name=` to each config or rename the ISOs; the configs are the smaller
  change and survive a re-copy of the games.
- `OPNPS2LD.ELF` -- same binary as the card.

## Console settings still to enable
BDM Start Mode -> Auto; BDM HDD -> On; Enable PS2RD Cheat Engine -> On;
Cheat Engine Mode -> Auto-select.
