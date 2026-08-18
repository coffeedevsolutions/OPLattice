# Waiting on a mounted volume

## HDD (`/Volumes/PS2`) -- DONE 2026-08-17
- 27 metadata CFGs merged in from `_card-backup/20260817-192736/CFG/`. Merged, not
  copied over: the HDD's own copies carry keys OPL writes itself (Playtime,
  PlayCount, size) and those were kept. `SLPS_200.01` (Ridge Racer V) was skipped --
  it has no ISO on the drive.
- All 46 ISOs renamed to display titles. The title IS the filename: `#Name` is a
  runtime-derived key (sbPopulateConfig overwrites it from the filename after
  reading the config, and configWrite skips every key starting with `#`), so a CFG
  can never supply it. The reverse map is `_hdd-backup/20260817-203034/iso-renames.json`.
- `DVD/games.bin` deleted. It is OPL's own scan cache of base_game_info_t records
  and held the old names; removing it forces one clean rescan, which re-reads
  SYSTEM.CNF from each image. First boot after this will be slower.
- `APPS/OPNPS2LD.ELF` -- 1,385,636 bytes.
- macOS AppleDouble sidecars (`._*`) and `.DS_Store` removed; xattrs stripped.
  Set `COPYFILE_DISABLE=1` before any future `cp` to this volume.

19 games still have no metadata CFG at all (the US set was never authored):
SCUS_971.02 SCUS_972.78 SCUS_974.79 SLUS_208.98 SLUS_209.12 SLUS_209.57
SLUS_210.12 SLUS_210.83 SLUS_211.18 SLUS_211.51 SLUS_212.19 SLUS_212.40
SLUS_213.31 SLUS_213.55 SLUS_214.09 SLUS_214.28 SLUS_214.84 SLUS_216.11
SLUS_217.46. Their details pages will show only the fields OPL derives itself
(media, format, size, widescreen).

## Memory card -- STILL PENDING
- `OPNPS2LD.ELF` -- 1,385,636 bytes, from `patches/build/OPNPS2LD-MMCE.ELF`.
- `OPL/conf_last.cfg` -- delete every `recent*_id` / `recent*_title` key so Home
  starts empty. `oplRecentPrune` clears entries that resolve to nothing on its own,
  but the games kept their serials through the rename, so entries that still match
  will survive the prune; only the wipe actually empties Home.

## Console settings still to enable
BDM Start Mode -> Auto; BDM HDD -> On; Enable PS2RD Cheat Engine -> On;
Cheat Engine Mode -> Auto-select (now under Settings -> Cheat Settings, global tier,
then Save Changes).
