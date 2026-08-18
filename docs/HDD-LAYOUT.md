# The hard drive

1 TB SSD on the OEM network adapter, exFAT on MBR, read by OPL as a **BDM ATA
device** -- `mass0:` -- not as an APA/PFS internal HDD. Two settings make it
appear, and both default to off:

| setting | value | why |
|---|---|---|
| BDM Start Mode | Auto | `initSupport` skips every BDM device while this is disabled |
| BDM HDD | On | gates `hddLoadModules()` and the ATA device's visibility |

They are separate gates: the first decides whether BDM runs at all, the second
whether the ATA device among them is shown. Turning on only the second does
nothing. Enabling BDM HDD also greys out the classic HDD (APA) mode, which is
correct here -- this drive is exFAT, not APA.

## Layout

```
/DVD    the ISOs, 47 of them, ~153 GB
/ART    art, read from the device the game is on
/CHT    widescreen cheats, read from the device the game is on
/THM    the theme -- fonts and pslogo.png are runtime dependencies of the shell
/CFG /VMC /CD /LNG /APPS
```

**Art and cheats must live on the same device as the games.** OPL builds both
paths from the device prefix -- `sbLoadCheats(pDeviceData->bdmPrefix, ...)`,
`"%sCHT/%s.cht"` -- so a CHT folder on the memory card serves only games launched
from the memory card.

**The theme lives on both.** `thmInit` scans `mc?:OPL` at boot; devices add their
own `THM/` only when the scan reaches them. Home is the boot screen and draws
immediately, so a theme that exists only here may not be loaded for the first
frames. The card copy covers boot, this one covers the HDD being active.

**The ELF here is not a boot path.** FMCB's `LK_L1` binds
`mmce0:/APPS/OPNPS2LD.ELF` -- the memory card. The copy in `/APPS` is kept
current only so a stale binary is not sitting somewhere confusing.

## Widescreen cheats

From `PS2-Widescreen/OPL-Widescreen-Cheats`, all 3610 installed rather than only
the matching ones, so a game added later is covered without a second trip. Files
are named by the serial OPL reads from SYSTEM.CNF -- `SLUS_212.40.cht` -- because
`sbLoadCheats` is passed `game->startup`.

29 of the 47 titles here have a patch. Of the 18 without, only two exist upstream
at all (`SLPM_657.90` Metal Gear Solid 3 NTSC-J, `SLPS_254.78` Gundam Ichinen
Sensou) and both are blocked on a missing PS2RD type-9 mastercode, which a PCSX2
pnach does not carry. Checked against `madmodder123/OpenPS2Loader_Widescreen_Cheats`
(a strict subset for this library) and `PCSX2/pcsx2_patches` (4704 files).

Two more settings turn them on:

| setting | value |
|---|---|
| Enable PS2RD Cheat Engine | On |
| PS2RD Cheat Engine Mode | Auto-select cheats |

Mode 0 is "enable all"; mode 1 opens the per-game picker, which is the thing auto
mode exists to avoid.

## Backups

`_hdd-backup/<timestamp>/` holds everything except `DVD/`, which is 153 GB and
already exists in `~/Documents/PS2`. A manifest of the ISO filenames is kept
instead, so a restore knows what was there. Gitignored, like `_card-backup/`.

Cluster size is 128 KB, so 14 MB of cheats occupy about 900 MB of slack. On a
terabyte that is not worth optimising, and OPL opens cheat files by exact name,
so the file count costs nothing at runtime.
