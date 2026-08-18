# Sync log

Both volumes are current as of 2026-08-17. Nothing is queued.

## HDD (`/Volumes/PS2`) -- done
- 27 metadata CFGs merged in from the card backup. Merged, not copied over: the
  HDD's own copies carry keys OPL writes itself (Playtime, PlayCount, size) and
  those were kept. `SLPS_200.01` (Ridge Racer V) skipped -- no ISO on the drive.
- All 46 ISOs renamed to display titles. The title IS the filename and cannot be
  anything else: sbPopulateConfig reads the CFG and then overwrites `#Name` from
  the filename, and configWrite skips every key starting with `#`, so a config can
  neither supply the name nor keep one written into it. Renaming is what OPL's own
  Rename does (sbRename renames the file). Reverse map:
  `_hdd-backup/20260817-203034/iso-renames.json`.
  Re-parsed SYSTEM.CNF afterwards: all 46 still resolve to the same serials, so the
  art / config / cheat bindings are intact.
- `DVD/games.bin` deleted -- OPL's own scan cache of base_game_info_t records, which
  still held the old names. First boot after this rescans and will be slower.
- `APPS/OPNPS2LD.ELF` -- 1,385,636 bytes.

19 games have no metadata CFG (the US set was never authored). Their details pages
show only what OPL derives itself: media, format, size, widescreen.

## Memory card (`/Volumes/PSxMemCard`) -- done
- `APPS/OPNPS2LD.ELF` and `APPS/OPL/OPNPS2LD.ELF` -- 1,385,636 bytes, md5
  c70fab871114f569a44c78375caf0998, plus `APPS/OPL/title.cfg`.
- Recents wiped. OPL's config does NOT live on the FAT side: the SD2PSX presents
  virtual cards as `.mcd` images under `MemoryCards/PS2/`, and conf_opl.cfg /
  conf_game.cfg / conf_last.cfg are inside `BOOT/BootCard-5.mcd` (channel 5). The
  image is 8 MiB exactly -- 16384 pages of 512 with no spare area -- so there is no
  ECC to recompute and a same-length byte edit is safe. `recentN_id` / `recentN_title`
  and the stale `pending_*` pair were rewritten to `#ecentN_...` / `#ending_...`:
  same byte length, so nothing in the filesystem shifts; not found by
  oplRecentLoad, which breaks at recent0 and yields an empty list; and dropped
  entirely the next time OPL rewrites the file, because configWrite skips keys
  beginning with `#`. `last_played` was left alone -- Home reads the recent list,
  not that key.
  The stored titles were the pre-rename filenames (GranTurismo3_US, MGS3_JP...),
  so the wipe was needed regardless: those entries still resolve by serial and
  would have survived oplRecentPrune.

Backups: `_hdd-backup/20260817-203034/`, `_card-backup/20260817-204337/`
(the latter includes all seven BootCard images).

## Copying to either volume
Set `COPYFILE_DISABLE=1` first. macOS otherwise writes AppleDouble sidecars
(`._name`) onto exFAT, which are junk on the console; 32 were cleaned up here.

## Console settings still to enable
BDM Start Mode -> Auto; BDM HDD -> On; Enable PS2RD Cheat Engine -> On;
Cheat Engine Mode -> Auto-select (Settings -> Cheat Settings, the new global entry,
then Save Changes).
