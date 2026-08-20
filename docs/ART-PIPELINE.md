# The art pipeline

How to get cover art onto the console, starting from nothing.

**This repository ships no art.** The images are publisher-owned and are not
ours to redistribute, so `_art-truecolor/`, `_cover-truecolor/` and
`_hero-truecolor/` are tracked as empty folders with a README each. Everything
below is how you fill them from your own sources.

If you skip this entirely, the themes still work — OPL draws its own placeholder
for anything missing, and the [previewer](THEME-PREVIEWER.md) draws labelled
boxes at the right aspect. You just get a grid of blanks instead of covers.

---

## Prerequisites

| Tool | For | Install |
|---|---|---|
| **ImageMagick** (`magick`) | every resize and quantise step | `brew install imagemagick` |
| **Python 3** | the `tools/*.py` scripts | preinstalled on macOS |
| **rsync** | `sync-card.sh` | preinstalled |
| `sips` | a few conversion steps | macOS built-in |
| Node | the test suite only | `brew install node` |

> **The tooling is macOS-oriented.** `sips` is macOS-only and the sync scripts
> assume `/Volumes/…` mount points. The Python tools are portable; the shell
> ones will need adapting elsewhere.

## What you need to supply

Three things, none of which live in this repository:

| | Default location | What it is |
|---|---|---|
| **Your library** | `~/Documents/PS2` | The ISOs |
| **`artmap.txt`** | `~/Documents/PS2/artmap.txt` | Maps your art files to disc serials |
| **Art masters** | `~/Downloads` | The full-resolution images you downloaded |

Every default is overridable — see [Overriding the paths](#overriding-the-paths).

### `artmap.txt`

One mapping per line, `key:SERIAL`:

```
gran-turismo-4:SCUS_974.36
katamari-damacy:SLUS_210.08
shadow-of-the-colossus:SCUS_974.72
```

- **`key`** is your own short name for the game. It only has to match the
  filenames of your masters.
- **`SERIAL`** is the disc's real serial, in OPL's punctuated form
  (`SLUS_213.55`, not `SLUS-21355`).

Blank lines and lines without a `:` are ignored.

**Get the serial right.** This is the single most common way the pipeline
fails silently — see [Why the serial matters](#why-the-serial-matters).

### Master filenames

The tools look in your masters directory for:

| Pattern | Built by | Becomes |
|---|---|---|
| `<key>-gameArt.<ext>` | `make-cover.py` | `<SERIAL>_COVXL.png`, 216×432 |
| `<key>-gameArt-BG.<ext>` | `make-bg.py` | `<SERIAL>_BG.png`, 418×180 |

Accepted extensions: `png`, `jpg`, `jpeg`, `webp`, `avif`. Covers want a 2:3
master (600×900 is ideal); backgrounds want something wide, typically 1920×620.

A key with no matching file is reported at the end of the run rather than
failing the batch, so you can fill gaps incrementally.

---

## Step by step

### 1. Check your serials first

```bash
tools/check-art.py ~/Documents/PS2
```

Run this **before** building anything. It mounts each ISO the way OPL does —
reading `BOOT2` out of `SYSTEM.CNF` (`supportbase.c:335`) — and reports the real
serial for every disc. That is the value your `artmap.txt` needs.

### 2. Build the art

```bash
tools/make-cover.py            # COVXL 216x432  -> _cover-truecolor/
tools/make-bg.py               # BG     418x180 -> its out-dir
tools/make-logos.py            # LGO
```

Each takes `[masters-dir] [out-dir]` if you want to override the defaults.

### 3. Convert to what the console actually wants

```bash
tools/palettize-art.py --dither none <art-dir>
```

This is the step that matters for stability, not just size. Each art cache costs
`count × source width × height × 3` bytes of decoded texture held resident, and
the drawn size does not reduce it — a 512×512 cover with `count=40` is ~31 MB
against a 32 MB console.

Originals are banked into `_art-truecolor/` on first run and every conversion is
derived from *there*, never from the previous output. Quantising an
already-quantised image compounds error; this cannot, so changing dither mode is
a free re-run. A file already banked is never re-banked.

Modes are `riemersma`, `o8x8` and `none`. **They cost the same VRAM** — the
choice is purely what it looks like, so it can be re-decided at any point.

### 4. Or do the whole library at once

```bash
tools/stage-hdd.sh ~/Documents/PS2 ~/Documents/PS2/art-out
```

Reads every disc's real serial, builds all three art files for each, fills gaps
with placeholders, and proves the result is complete. Prefer this to running the
individual tools by hand — it checks `artmap.txt` against the discs in **both**
directions, so it catches both a map entry with no disc and a disc with no map
entry.

### 5. Stage and sync

```bash
tools/stage-device.sh [art-dir] [out-dir]     # assembles _deploy/
tools/sync-card.sh [volume]                   # copies and verifies
```

`stage-device.sh` needs the loader at `patches/build/OPNPS2LD-MMCE.ELF`; it is a
Release asset, so download it there first. Details in [INSTALL.md](INSTALL.md).

**Art must live on the same device as the games.** OPL builds both paths from
the device prefix, so an `ART` folder on the memory card serves only games
launched from the memory card.

---

## Why the serial matters

OPL does not care what your files are called on disk. It reads `BOOT2` from the
disc's `SYSTEM.CNF` and looks for `ART/<STARTUP>_COV.png` under **that exact
name**. A wrong serial produces art with a perfectly valid-looking filename that
no game ever loads, and the only symptom is a grid of placeholder tiles.

The common way to get it wrong is taking the first regex match from `strings`
over the whole ISO. That is usually the boot serial, but any disc that mentions
another game's serial in a string table — demos, sequels with shared assets,
anything with a save-data compatibility list — hands back the wrong one.

`check-art.py` and `stage-hdd.sh` both read `SYSTEM.CNF` properly. Use them
rather than trusting a filename.

## Overriding the paths

Nothing requires you to use `~/Documents/PS2` or `~/Downloads`:

```bash
tools/make-cover.py /path/to/masters /path/to/out
tools/stage-hdd.sh  /path/to/library /path/to/art-out
ARTMAP=/path/to/artmap.txt tools/stage-hdd.sh /path/to/library
tools/stage-device.sh /path/to/art /path/to/deploy
FONT_DIR=/path/to/fonts tools/font-vram.py
```

`make-cover.py` and `make-bg.py` currently read `artmap.txt` from
`~/Documents/PS2/artmap.txt` unconditionally; symlink it if your library lives
elsewhere.

## Naming and dimensions

The full pattern table — every suffix, its texel count, and whether it is
quantised, exact or left truecolor — is in
[`../_art-truecolor/README.md`](../_art-truecolor/README.md).

## Getting the art itself

Not covered here, deliberately. Cover art, hero images and logos are publisher
property; where you obtain them, and whether you have the right to, is your call
and your responsibility.
