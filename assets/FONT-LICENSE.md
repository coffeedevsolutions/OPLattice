# Fonts in this repo

## PoeVeticaNew.ttf
Vendored from the Open-PS2-Loader tree (`thirdparty/`). It is OPL's compiled-in
default font; the previewer embeds it so text metrics match the console.

## NotoSans-Bold.ttf
Noto Sans Bold, © 2022 The Noto Project Authors, licensed under the
SIL Open Font License 1.1 — https://openfontlicense.org

Source: https://github.com/notofonts/notofonts.github.io (`NotoSans/hinted/ttf`).

The copy in `assets/` and in each theme folder is **subset** to Latin-1 plus
Latin Extended-A and common punctuation (631 KB → 21 KB), because OPL's font
loader reads the whole file into EE RAM once *per font slot* — three slots of the
full font would cost 1.9 MB on a 32 MB console.

`assets/NotoSans-Bold-full.ttf` is the unmodified original, kept for
regeneration. Subset with:

    pyftsubset NotoSans-Bold-full.ttf --output-file=NotoSans-Bold.ttf \
      --unicodes="U+0020-007E,U+00A0-00FF,U+0100-017F,U+2013-2014,U+2018-201D,U+2022,U+2026,U+20AC" \
      --layout-features='' --no-hinting --drop-tables+=GSUB,GPOS,GDEF,DSIG

Neither font has a legacy `kern` table, so OPL (which reads only `kern`, not
GPOS) advances by pure glyph widths. The previewer reproduces that exactly.
