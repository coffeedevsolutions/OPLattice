# Installing OPLattice

From nothing to a booting console. Read [Booting](BOOTING.md) alongside this —
it covers which FMCB channel actually launches what, and why the L1 hold
matters.

> **Test on something you can recover.** Keep a known-good OPL reachable. On an
> FMCB setup the no-button boot path is exactly that: it runs the ELF baked
> inside the card image, which is untouched by anything here. If a build hangs,
> power-cycle without holding L1 and you come up on the old one.

---

## 1. What you need

- A PS2 that can run homebrew — modchip, FMCB, or FHDB.
- Somewhere for OPL to live: memory card, SD2PSX/MMCE, or internal HDD.
- Somewhere for games to live: USB, MX4SIO, HDD, or SMB.
- Docker, **only if you want to build the ELF yourself**. There is no need for a
  local PS2 toolchain.

## 2. Get an ELF

### Option A — a prebuilt binary

Download `OPNPS2LD.ELF` from [Releases](../../releases).

Binaries are Release assets rather than files in this repository. Two 1.3 MB
ELFs rebuilt on nearly every commit had put ~110 MB of dead weight in the git
history — 40× what the files themselves weigh — so they were purged and now ship
where release artefacts belong.

**Pick the right one.** A mainline ELF has **no MMCE support at all** — no
`MMCE_MODE`, no `mmcesupport.c`. Flashing one on an SD2PSX or similar makes the
memory-card SD disappear entirely.

### Option B — build it

**For MMCE setups** (SD2PSX, MemCard PRO2):

```bash
git clone --depth 1 --branch OPL-MMCE-beta-2 \
  https://github.com/ps2-mmce/Open-PS2-Loader opl-mmce
cd opl-mmce
python3 ../patches/04-mmce-fork-toolchain.py .
for p in 01-opl-tile-grid 02-opl-sort-and-recent 03-opl-menu-tabs \
         05-mmce-device-integration; do git apply --3way ../patches/$p.patch; done
docker run --rm -v "$PWD":/src -w /src ps2dev/ps2dev:latest sh -c \
  'apk add --no-cache make git bash python3 py3-yaml >/dev/null && \
   git config --global --add safe.directory /src && make RELEASE=1'
```

`04` **must run first.** The fork branched in January 2025 and never rebased, so
it does not compile on a current toolchain: GCC 15 defaults to C23 (where `f()`
declares *no* parameters), gsKit's vsync callback gained an argument, and
`iopfixup` now rejects a module whose exported stub sits at `.text` offset 0.
Every fix in `04` is one mainline already made, and none changes behaviour.

The SDK is not the problem — `mmceman.irx`, `mmcedrv.irx` and `mmceigr.irx` were
upstreamed into ps2sdk and are in current images. Only the older digest pinned
in the fork's own CI predates them, which is why building with that image fails
on a missing `mmceman.irx`.

**Build serially.** Under `-j` the export-table steps race and fail spuriously.

**For mainline**, against `ps2homebrew/Open-PS2-Loader` @ `3e3f34e` (v1.2.0-Beta):

```bash
git clone https://github.com/ps2homebrew/Open-PS2-Loader && cd Open-PS2-Loader
git checkout 3e3f34e
for p in 01-opl-tile-grid 02-opl-sort-and-recent 03-opl-menu-tabs; do
  git apply ../patches/$p.patch; done
docker run --rm --network host -v "$PWD":/src -w /src ps2dev/ps2dev:latest sh -c '
  apk add --no-cache make python3 py3-yaml git curl bash
  bash ./download_lwNBD.sh; bash ./download_cfla.sh
  make -j4'
```

`TRANSLATIONS=` skips the language-pack download if it fails.

### Verifying a build

`OPNPS2LD.ELF` is LZMA-packed, so `strings` on it returns compressed noise and
finds none of the feature strings. **That is not evidence of a bad build.**
Check `opl.elf` or `opl_stripped.elf` in the build tree instead:

```bash
strings -a opl.elf | grep -cE 'mmce'          # expect ~165 on an MMCE build
strings -a opl.elf | grep -E '_cell_width|_x_scaled|label_mmce|MenuTabs|RecentImage'
find modules -name '*.irx' -newer .git/HEAD | wc -l   # must equal the total
```

That last one matters: a failed run can leave an `.irx` from another toolchain
behind, and `make` will happily link it.

## 3. Put the ELF where your chip boots from

Usually `mc0:/OPL/OPNPS2LD.ELF`. On an SD2PSX under FMCB it is more subtle, and
getting it wrong means you flash a build and boot a different one:

| prefix | resolves to |
|---|---|
| `mc0:` | inside the virtual memory card image |
| `mmce0:` | the SD card's own filesystem — what mounts as a volume on your PC |

FMCB tries launch keys in order. Holding **L1** at boot reaches
`mmce0:/APPS/OPNPS2LD.ELF`; booting with nothing held runs the `LK_AUTO` chain,
which reaches for USB mass storage first and then the ELF baked *inside* the
card image. Full detail, including which channel to be on, is in
[BOOTING.md](BOOTING.md).

**Which build am I running?** Not answerable from the About screen — this build
and the stock one report the same version string, because they are the same
upstream commit. Look in **Settings** for a `SHELF UI` row. Present means this
build.

## 4. Stage a device

`stage-device.sh` builds the folder tree OPL expects, ready to copy across:

```bash
tools/stage-device.sh [art-dir] [out-dir]        # default out: _deploy/
```

The layout comes from `supportbase.c:822`, which is also the list OPL creates
for itself the first time it sees a device:

```
CFG/   per-game settings        THM/   themes, one folder each
LNG/   translations             ART/   <STARTUP>_<PATTERN>.png
VMC/   virtual memory cards     CHT/   cheats
APPS/  homebrew
```

**ISOs are not staged.** They are far too large to copy twice — put them
straight into `<out-dir>/DVD` (or `CD`), or point rsync at the drive.

Covers are downscaled on the way in. Twelve are resident at once in the grid,
and VRAM rather than RAM is the binding constraint.

### Art first

```bash
tools/check-art.py <ps2-dir>
```

Run this before anything else. It mounts each ISO the way OPL does — reading
`BOOT2` out of `SYSTEM.CNF` (`supportbase.c:335`) — and checks that
`ART/<STARTUP>_COV.png` exists under that exact name. **Filenames on disk are
irrelevant to OPL.** This is the one mismatch that silently produces a grid of
placeholder tiles.

For a whole library at once, `tools/stage-hdd.sh` reads every disc's real serial,
builds all three art files for each, fills gaps with placeholders, and proves
the result is complete. It exists because the hand pipeline fails quietly in
three places, each of which looks exactly like success — chiefly that a serial
taken from `strings` is *guessed*, not read, and a wrong one produces art with a
valid-looking filename that no game ever loads.

Naming, patterns and exact texel dimensions:
[`_art-truecolor/README.md`](../_art-truecolor/README.md).

## 5. Install a theme

Copy a folder from [`themes/`](../themes/README.md) next to your other OPL
themes and pick it in **Settings → Theme**.

The `thm_` prefix must stay — OPL only scans directories containing it.
`README.md` and `ART-ico/` are ignored by OPL; delete them from the copy if you
like.

Three of the four themes need the grid patches; `thm_UnifiedLibrary` runs on
stock OPL. Check the table in the themes README before assuming a theme works on
your build — the [previewer](THEME-PREVIEWER.md) flags every patch-only key with
a `PATCHED_ONLY` note for exactly this reason.

## 6. Sync

```bash
tools/sync-card.sh [volume]        # default /Volumes/PSxMemCard
```

It refreshes the theme copy from source, syncs, **verifies the card against the
source afterwards, and refuses to eject if anything differs.** That verification
is the whole point: it was written after a sync reported success while quietly
shipping a stale theme, because `_deploy/THM/` is a copy and rsync had
faithfully copied exactly what it was pointed at.

What it will not touch:

| | why |
|---|---|
| `DVD/`, `CD/` | the games; several GB, and nothing here has any business there |
| `CFG/` | **merged, never copied** — see below |
| `MemoryCards/`, `.sd2psx/` | the card's own boot state |

### Why CFG is merged rather than copied

OPL owns the `$`-prefixed keys, and `configWrite` rewrites whole files. Copying
over `CFG/` would discard every setting made on the console — including
`LastPlayed`, `PlayCount` and `Rating`. `tools/merge-cfg.py` merges instead, so
console-side state survives.

### On macOS

```bash
export COPYFILE_DISABLE=1
```

Without it, macOS writes `._` sidecar files onto exFAT.

## 7. Console settings

For the build to behave as documented:

- **BDM Start Mode** → Auto
- **BDM HDD** → On
- **Enable PS2RD Cheat Engine** → On
- **Settings → Cheat Settings (global)** → Enable Cheats, Auto-select, Save Changes

In-game screenshots need **GSM enabled for that title**; then D-pad Up writes a
BMP to `mc1:`. Nothing in OPL reads those back — they are BMPs, on the memory
card, under a name the theme engine never looks for. `tools/import-igs.py`
converts them into the `<SERIAL>_SCR.png` and `_SCR2.png` the info page expects.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Grid of placeholder tiles | Art filenames don't match the disc's real `BOOT2` serial. Run `check-art.py`. |
| Black screen on hardware, fine in PCSX2 | Art cache over budget. Load the theme in the [previewer](THEME-PREVIEWER.md) and read the PS2 art cache estimate. |
| Memory-card SD vanished | Mainline ELF on an MMCE setup. Build from the fork. |
| Row icons missing entirely | A `decorator` target caches fewer images than the list has rows. |
| An element silently never draws | Missing required attribute, or a numbering gap — `main5_type` absent ends the scan, so `main6` onward is never read. |
| Settings changed on console keep reverting | Something copied over `CFG/` instead of merging it. |
| Flashed a new build, nothing changed | Booting a different launch key than you flashed. See [BOOTING.md](BOOTING.md). |
