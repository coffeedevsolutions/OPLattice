# Third-party notices

OPLucid is licensed under the GNU General Public License v3.0 or later
(see [`LICENSE`](LICENSE)). That choice is not arbitrary — it is the strongest
obligation already present in the tree, and it is explained below.

Everything here that is not ours keeps its own licence and its own copyright
notice. Nothing has been relicensed, and no notice has been stripped.

## Open PS2 Loader

<https://github.com/ps2homebrew/Open-PS2-Loader>

The patches in [`patches/`](patches/) are derivative works of Open PS2 Loader,
and [`patches/shelf/tree/`](patches/shelf/tree/) contains whole copies of the
files patch 09 modifies — kept because they are the source of truth the patch is
regenerated from.

Most of those files carry:

> Copyright 2009, Volca
> Licenced under Academic Free License version 3.0
> Review OpenUsbLd README & LICENSE files for further details.

The **Academic Free License 3.0** is permissive. Note that the Free Software
Foundation considers AFL-3.0 to be incompatible with the GPL in the strict
direction; this repository combines them the way Open PS2 Loader itself already
does, and the AFL notices are preserved verbatim in every file that carried one.

### `src/cheatman.c` — GPL-3.0-or-later

One vendored file is copyleft, and it is the reason the repository as a whole is
GPL-3.0-or-later rather than something more permissive:

> PS2rd is free software: you can redistribute it and/or modify it under the
> terms of the GNU General Public License as published by the Free Software
> Foundation, either version 3 of the License, or (at your option) any later
> version.

From [PS2rd](https://github.com/ps2dev/ps2rd). `patches/09-shelf-sidebar.patch`
modifies this file, so it is distributed here as a modified GPL-3.0-or-later
work and its terms attach.

### In-game screenshots

`ee_core/src/igs_api.c` in the upstream tree is GPL, from **maximus32** and
**doctorxyz**. OPLucid builds with `IGS=1`, which switches on code that was
already present upstream. That file is not vendored here.

## Fonts

| File | Origin | Licence |
|---|---|---|
| `assets/PoeVeticaNew.ttf` | Vendored from the Open-PS2-Loader tree (`thirdparty/`). It is OPL's built-in font and the source of truth for the previewer's embedded copy. | Its own; see upstream |
| `assets/NotoSans-Bold.ttf`, `assets/NotoSans-Bold-full.ttf` | [Noto Sans](https://fonts.google.com/noto) | SIL Open Font License 1.1 |

Details, including the Latin subsetting that takes the file from 631 KB to
21 KB, are in [`assets/FONT-LICENSE.md`](assets/FONT-LICENSE.md).

## Cover art and game imagery

**None is distributed here, deliberately.**

`_art-truecolor/`, `_cover-truecolor/` and `_hero-truecolor/` are working
directories. They are tracked as folders, with a README explaining the naming
convention and the exact texel dimensions, and their contents are gitignored.
The art itself is publisher-owned and is not ours to hand to anyone who clones
this repository. Supply your own.

The sample game metadata in `docs/game-metadata.txt` uses real disc serials and
sizes so that filenames and row text are representative. The attribute values
(genre, release, developer, description) are **marked sample strings, not a game
database**; they exist to exercise wrapping and clipping at realistic lengths.
