# Streaming over SMB from a PC

For a slim PS2 — SCPH-70000 through SCPH-90000 — with no hard drive, its
Ethernet port wired to a network, and the ISOs sitting on a computer. OPL calls
this **ETH mode**, and the rest of this repository is written for a machine with
an internal drive, so this is the page that reconciles the two.

**The short answer: it works, and nothing here needs recompiling.** SHELF, the
patches and the themes are all indifferent to where a game lives. What differs is
one console setting — where [`INSTALL.md`](INSTALL.md) is actively wrong for this
setup — plus a share that has to speak a protocol Windows stopped enabling by
default.

---

## 1. Why nothing needs porting

The concern with a project built around an ATA drive is that the storage device
has leaked into the UI. Here it has not, and the code says so:

| | |
|---|---|
| `src/shelf.c:1724` | The Library page calls `menuGetActiveList()` and asks *that* for names, art and launches. There is no device-mode test anywhere in `shelf.c`'s 3,755 lines. |
| `src/ethsupport.c:638-639` | `oplRecentPush` and `oplStatsOnLaunch` are hooked into the SMB launch path exactly as they are into BDM, HDD and MMCE. Recently-played and Play Stats work. |
| `src/ethsupport.c:729` | `ethGetImage` builds `smb0:<prefix>ART\<STARTUP>_<PATTERN>.png`. Cover art comes off the share, with backslash separators already handled. |
| `src/opl.c:650` | The per-game CFG path picks `\` for `ETH_MODE` and `/` elsewhere. Favourites and per-game settings write through. |
| `patches/03-opl-menu-tabs.patch:415` | `MenuTabs` reads `label_eth`, so the Ethernet device gets its own tab caption on the classic screen. |

The themes and the previewer never touch a device at all.

One thing is *better* here than on the setup this repo was developed on. The
status bar's network pip (`src/shelf.c:887-893`) reads `gNetworkStartup` and shows
`NET` in green when the share is up — on an MMCE or USB machine that indicator is
decoration; on this one it is the single most useful thing on the bar.

## 2. Which ELF

Either release asset runs, and both carry SHELF. The MMCE build's extra
memory-card-SD support is inert on a console with no SD2PSX or MemCard PRO2 in
it, and it is the variant this project is developed against
([`FEASIBILITY-PROMPTS.md`](FEASIBILITY-PROMPTS.md) §5: "your games load from
MMCE"), so take `OPNPS2LD-MMCE.ELF` unless you have a reason not to.

Keep the ELF you are replacing. On a slim the fallback is whatever your boot
method already launches — see [`BOOTING.md`](BOOTING.md).

## 3. The one setting INSTALL.md gets wrong for you

[`INSTALL.md`](INSTALL.md) §7 and the README's Step 6 say **BDM Start Mode →
Auto** and **BDM HDD → On**. Both are for a machine with USB or an ATA drive.
On this setup:

| Setting | Value | |
|---|---|---|
| **ETH Start Mode** | **Auto** | Not mentioned anywhere else in these docs. Without it there is no SMB device at all. |
| **BDM Start Mode** | **Off** | See below. Leaving it on has a specific, confusing consequence. |
| BDM HDD | — | No effect. A slim has no ATA bus and no expansion bay. |
| Enable Write Operations | On | Play Stats is hidden without it, and favourites cannot be written back to the share. |
| Enable PS2RD Cheat Engine | On, if you want cheats | Unchanged. |

### Why BDM Start Mode has to be Off, and what it looks like if it isn't

SHELF boots straight to Home (`src/opl.c:2401`) rather than passing through the
classic device list. Which device is "active" at that moment is decided by
registration order, not by which one has games:

- `initAllSupport()` (`src/opl.c:441-447`) registers BDM first, then ETH, HDD,
  APPS, MMCE.
- `menuAppendItem` (`src/menusys.c:401-404`) makes the **first** menu registered
  the selected one.
- `refreshMenuPosition()` (`src/menusys.c:423`) is the only code that re-picks by
  *visibility* — and its single caller is the exit from Settings
  (`src/menusys.c:1160`).

With BDM Start Mode on anything but Off, `bdmInitDevicesData()`
(`src/bdmsupport.c:716-719`) registers `mass0` even when nothing is plugged in,
so `menuGetActiveList()` returns an empty USB device and holds it there. The
symptom is a Library page reading

> Nothing to show yet.
> This page follows the device the main list is on. Pick one there first.

**while the share is connected and the network pip is green.** The sidebar has no
device picker — Home, Library, Apps, Settings, and that is the whole of it — so
the only way out is Settings and back, which happens to call
`refreshMenuPosition()` and lands you on the first *visible* device.

Turning BDM Start Mode off removes the empty registration, and ETH becomes the
first menu appended and therefore the active list from the first frame. Its
support object is registered synchronously at boot even though the SMB logon is
still in flight, and `libSync` (`src/shelf.c:1722`) re-reads the count every
frame, so the grid fills itself in when the scan lands.

## 4. The share

### SMB1, and why your PC probably says no

OPL's `smbman` speaks **SMB1 / NT LM 0.12 only**. There is no SMB2 support. Both
ends of the modern world have turned that off by default — Windows 10/11 ship
with SMBv1 removed or disabled, and Samba 4.11 onward sets `server min protocol`
to SMB2 — so a share that every other device on your network can see will refuse
the PS2 with a network error and nothing else.

Three ways round it, in the order most people should try them:

1. **A dedicated SMB1 server for OPL** — the least invasive, because nothing
   about your PC's own file sharing changes.
2. **Samba with the old dialect re-enabled**: `server min protocol = NT1`,
   `ntlm auth = yes`, and a guest-writable share. The
   [PS2-HOME workaround thread](https://www.ps2-home.com/forum/viewtopic.php?t=9468)
   and [this Samba config gist](https://gist.github.com/mafredri/e88401c91489232e92e493d0e02912ef)
   are the two references worth reading before you edit `smb.conf`.
3. **Windows' own SMB1 feature**, re-enabled under *Turn Windows features on or
   off → SMB 1.0/CIFS File Sharing Support*, plus insecure guest logons. This
   works and it is the option that weakens your machine the most; if you take it,
   scope the share to a VLAN or a machine you do not mind exposing.

Give the PC a **static IP**, or a DHCP reservation. OPL stores the server address
as a number, and a share that moves is indistinguishable from a share that died.

### Layout

The share root *is* the device tree — the same one
[`stage-device.sh`](../tools/stage-device.sh) builds, and the same one OPL creates
for itself the first time it sees a device (`supportbase.c:822`):

```
<share>/
  DVD/     ISOs  (CD/ only for CD-based games)
  ART/     <STARTUP>_COV.png, _COVHD, _BG, _HERO, _LGO, _SCR…
  CFG/     per-game settings, favourites, play stats
  THM/     thm_GridHard/ etc.
  VMC/     virtual memory cards
  CHT/     cheats
  APPS/    homebrew
  LNG/
```

If you would rather not have those folders at the top of an existing share, put
them in a subfolder and set **ETH prefix** in OPL's settings to its name;
`ethPrefix` (`src/ethsupport.c:66`) is prepended to every path above.

The share must be **writable**. Favourites (`src/shelf.c:2487`) and Play Stats are
per-game keys in `CFG/<STARTUP>.cfg` on the share, and OPL gates both behind its
own *Enable Write Operations* switch as well.

### OPL's own settings do not live on the share

This catches people. `conf_opl.cfg` — the SHELF UI toggle, the network details,
the theme choice — and `conf_last.cfg`, which is where Home's recently-played row
comes from, go to OPL's config directory, which is `mc?:OPL` by default
(`src/opl.c:2021`). If that read fails, `tryAlternateDevice()`
(`src/opl.c:838-887`) falls back to the boot device, a BDM device, the internal
HDD, and a memory card, in that order. **The SMB share is not on that list at
all.** With no memory card, no USB stick and no HDD, OPL has nowhere to save: you
can turn SHELF on, and it will be off again after a power cycle.

On a slim booting from FMCB there is a card in slot 1 by definition, so this is
usually already true. It stops being true the moment you try to run the console
card-less off a boot disc.

## 5. Getting art and themes onto it

Two tools in this repository were macOS-only for no good reason and now are not:

- `tools/stage-device.sh` guarded on `sips`, which it never called. Removed — it
  needs `bash` and ImageMagick, both of which exist everywhere.
- `tools/make-bg.py` read image dimensions with `sips`. It now uses
  `magick identify`, which was already a hard dependency of every other line in
  the file.

`tools/check-art.py` — the one the README tells you to run first — assumed the
author's staging directory: ISOs flat in the root, art in `art-out/`. Pointed at
a share it found no discs and crashed looking for `art-out`. It now accepts
either layout, walking `CD/` and `DVD/` and reading art from `ART/`:

```bash
tools/check-art.py /path/to/share      # or /mnt/ps2share, or Z:\ under WSL
```

That check is not optional busywork. OPL names art after the `BOOT2` line inside
each ISO's `SYSTEM.CNF`, never after the filename, and a mismatch produces a full
grid of placeholder tiles with no error anywhere.

The rest of the pipeline —
[`stage-hdd.sh`](../tools/stage-hdd.sh) (despite the name it takes any directory
of ISOs), `make-cover.py`, `make-logos.py`, `palettize-art.py` — is portable
already. The full walkthrough is [`ART-PIPELINE.md`](ART-PIPELINE.md).

### Syncing, and the one thing not to copy

`tools/sync-card.sh` is macOS-specific (`/Volumes`, `diskutil`) and is about a
memory card, so it is not your tool. The equivalent is one rsync, and the
exclusion in it is the whole point:

```bash
rsync -av --delete --exclude CFG/ _deploy/ /path/to/share/
tools/merge-cfg.py _deploy/CFG /path/to/share/CFG      # merge, never copy
```

`configWrite` rewrites a per-game CFG whole. Copying `CFG/` over the share
discards every setting made on the console, including `LastPlayed`, `PlayCount`,
`Rating` and your favourites — which is exactly why `sync-card.sh` merges that
one directory instead of syncing it.

## 6. What to expect once it runs

**Games stream fine.** A PS2 DVD drive delivers 1.35–5.4 MB/s depending on the
game's read speed; 100 Mbit Ethernet carries more than that with room to spare,
and OPL loads `smb_cdvdman.irx` so in-game reads go over the same connection
(`src/ethsupport.c:641`, and `:717` hands the same module to the launcher).

**Art is the part that behaves differently.** The Library page allocates five
caches holding 32 textures between them — 24 covers, plus two each of hero,
background, logo and large cover (`src/shelf.c:1811-1850`) — and Home adds nine
more (`src/shelf.c:3389-3390`). Every one of those slots is a separate
open-and-read against the share rather than against a drive on the same board. Expect more pop-in on
first paint than a local device gives. The mitigations are already in the code:
the hero is requested before the covers so it does not queue behind them, and the
rows above and below the visible two are prefetched (`src/shelf.c:2153-2160`).

**One risk worth knowing about.** `cacheGetTexture` marks art it could not load
with `-2`, meaning "never ask again". `libHero` deliberately clears that and
retries (`src/shelf.c:1875-1884`); `libCover` does not (`src/shelf.c:1942-1951`).
On a drive a failed read is nearly always a genuinely missing file. Across an SMB
reconnect it might not be — and a tile that loses that race stays blank for the
rest of the session. If covers go missing after a network hiccup, **SELECT** on
the classic list rescans the device and rebuilds the table. Whether `-2` can
actually be reached by a transport error rather than a missing file is a question
for `src/texcache.c`, which this repository does not vendor; nobody has tested it
over SMB.

**ZSO is worth considering.** OPL reads compressed ISOs, and compression is a
straight reduction in bytes crossing a 100 Mbit link. The block cache that makes
it viable has its own setting — *SMB cache* in OPL's settings, `smb_cache` in
`conf_opl.cfg`, default 16 and adjustable from 4 to 32 (`src/dialogs.c:275`) —
which is handed to the in-game driver as `zso_cache` (`src/ethsupport.c:715`).
It is the one tuning knob on this page that exists specifically for SMB.

**VMCs work** (`src/ethsupport.c:784`) but a virtual memory card served over the
network is slower than `mc0:`. Games that save constantly are happier on a real
card.

## 7. Something this setup gets that the developer's does not

[`FEASIBILITY-PROMPTS.md`](FEASIBILITY-PROMPTS.md) §5 works through in-game
achievement telemetry and stops on a hard blocker: `system.c:472-485` picks the
in-game IRX set from the device mode, and only `ETH_MODE` pulls in
`CORE_IRX_ETH | CORE_IRX_SMB`. On the MMCE machine this repo was built on, the
in-game network stack is never loaded, and the whole idea needs a patch that has
not been written.

On this setup that stack is loaded already, for every game, as a side effect of
where the ISOs live. If the achievements work is ever attempted, an
SMB-streaming slim is the machine to attempt it on.

## 8. What would still need code

Nothing on this list blocks a working install; all four are the difference
between "works" and "was designed for this".

1. **A device picker in the sidebar**, or a `refreshMenuPosition()` when a device
   first becomes visible. That would make §3's BDM warning unnecessary rather
   than merely documented. It is a change to shared boot-path code and wants
   hardware testing, so it is proposed here, not shipped.
2. **A retry policy for cover art on a lossy transport** — give `libCover` the
   treatment `libHero` already has, or scope the `-2` marking so a transport
   error and a missing file are told apart.
3. **`tools/sync-share.sh`** — the verify-and-refuse-to-finish logic in
   `sync-card.sh` is the valuable half and is not macOS-specific. §5's rsync is
   the manual version.
4. **A free-space query for ETH.** The Apps status bar draws `— free`
   (`src/shelf.c:884`) because nothing in `bdmsupport`, `mmcesupport` or
   `ethsupport` reports capacity. SMB actually can: `smbman` exposes a
   disk-information devctl, so this is the one device class where the slot could
   be filled honestly.

---

## Sources

Protocol and platform facts that are not derived from this repository's own
source:

- [PS2-HOME — Samba deprecation of SMB1 and NT LM 0.12 for OPL](https://www.ps2-home.com/forum/viewtopic.php?t=9468)
- [PS2-HOME — Getting OPL SMB to work with Windows 11](https://www.ps2-home.com/forum/viewtopic.php?t=13913)
- [PS2-HOME — booting games off the network with OPL (novice guide)](https://www.ps2-home.com/forum/viewtopic.php?t=3692)
- [jimmikaelkael/ps2-smbman](https://github.com/jimmikaelkael/ps2-smbman) — the IOP module OPL's SMB support is built on
- [Samba configuration for OPL](https://gist.github.com/mafredri/e88401c91489232e92e493d0e02912ef)
