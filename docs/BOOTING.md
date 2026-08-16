# Booting this build — channel, L1, and why

Read from the card, not from memory. The facts below come from
`MemoryCards/PS2/BOOT/BootCard.ini` and the FMCB config inside
`BootCard-5.mcd`.

## Correction: L1 at boot is real

I previously said the L1 hold was unnecessary, having grepped OPL's source for
button-at-boot handling and found none. That grep was correct and the conclusion
drawn from it was wrong. **L1 is an FMCB binding, not an OPL feature** — it was
never going to appear in OPL's tree, so looking there proved nothing.

Channel 5's FMCB config binds launch keys like this:

```
LK_AUTO_E1 = mass:/APPS/OPNPS2LD.ELF        <- no button held
LK_AUTO_E2 = mc?:/APPS/OPNPS2LD.ELF
LK_AUTO_E3 = mc?:/OPL/OPNPS2LD.ELF

LK_L1_E1   = mmce?:/APPS/OPNPS2LD.ELF       <- L1 held
LK_L1_E2   = mc?:/APPS/OPNPS2LD.ELF
LK_L1_E3   = mc?:/OPL/OPNPS2LD.ELF
```

`E1/E2/E3` are tried in order until one exists.

The distinction that matters on an SD2PSX:

| prefix | resolves to |
|---|---|
| `mc0:` | inside the virtual memory card image — `BootCard-5.mcd` |
| `mmce0:` | the SD card's own filesystem — what appears as `/Volumes/PSxMemCard` |

So **L1 launches `mmce0:/APPS/OPNPS2LD.ELF`, which is the file this repo
deploys.** Booting with no button held runs `LK_AUTO`, which reaches for USB
mass storage first and then the ELF baked *inside* the card image — an older
build that knows nothing about any of this work.

That is a useful property, not a nuisance: **the no-button path is the fallback.**
If a build hangs, power-cycle without touching L1 and the console comes up on the
known-good OPL inside the card image.

## Channel

**Channel 5.** `BootCard.ini` names it `FMCB 1.953`.

Two warnings about that file. Its `[ChannelName]` entries have been observed
transposed relative to the images' actual contents, so trust the config inside
`BootCard-5.mcd` over the label. And there is a `BootCard-8.mcd` with no name
entry at all.

Channel 5 is the only channel whose config references `mmce?:` paths — the other
five point at `mass:` and `mc?:` only. It is the one that can see the SD card, so
it is the one to be on.

## Which build am I running?

Not answerable from the About screen. This build and the one inside the card
image report the same version string, because they are the same upstream commit.

**Look in Settings for a `SHELF UI` row.** Present means this build; absent means
the card image's.

## A note for future verification

`OPNPS2LD.ELF` is LZMA-packed, so `strings` on it returns compressed noise and
finds none of the feature strings. It is not evidence of a bad build. Verify
against `opl.elf` or `opl_stripped.elf` in the build tree, which are what the
packed file is produced from.
