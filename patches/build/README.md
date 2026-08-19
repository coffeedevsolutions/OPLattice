# `patches/build/`

Build output. The compiled loader binaries (`OPNPS2LD-mainline.ELF`,
`OPNPS2LD-MMCE.ELF`) are **not tracked in git** — they were rebuilt on nearly
every commit, which cost ~110 MB of history for two 1.3 MB files.

They ship as **GitHub Release assets** instead, so you can download a build
without a PS2 toolchain and without cloning that weight.

To build them yourself, see `patches/README.md`.
