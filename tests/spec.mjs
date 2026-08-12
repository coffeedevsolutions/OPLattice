/**
 * Shared test specs for the OPL Theme Previewer core.
 *
 * Run headless:  node tests/run.mjs
 * Run in a page: open tests/tests.html
 *
 * Both runners extract the core straight out of opl-theme-previewer.html, so
 * these assertions pin the behaviour of the code that actually ships.
 * Section references (§x.y) point at docs/THEME-FORMAT.md.
 */

export function specs(C, fixtures, t) {
  const { eq, ok, deepEq, throws } = t;
  const P = C.parseConfig, W = C.writeConfig;

  /* ------------------------------------------------- 0. measurement contract */

  t.group("fontSpec: a slot resolves the same either way");

  // This exists because it silently did not. fontSpec read `el.font`, so a bare
  // slot number became undefined and fell through to the default face -- it
  // answered 17px PoeVetica where the caller meant a 10px theme slot. Every
  // width measured through it came out ~1.6x too wide, and since the check used
  // the same call, the numbers agreed with each other and with nothing that was
  // actually drawn on the canvas. A wrong answer returned quietly is worse than
  // a thrown one, so this pins both call forms to the same result.
  const previewerSrc = fixtures["opl-theme-previewer.html"];

  const loadFontSpec = () => {
    const src = previewerSrc.match(/^function fontSpec\(el\) \{[\s\S]*?^\}/m);
    if (!src) throw new Error("fontSpec not found in the previewer");
    const S = {
      theme: { fonts: { 0: { size: 17 }, 3: { size: 10, file: "NotoSans-Bold.ttf" } } },
      themeFonts: new Map([[0, "ThemeFont0"], [3, "ThemeFont3"]]),
      files: new Map([["notosans-bold.ttf", {}]]),
    };
    return new Function("S", "FNT_DEFAULT_SIZE", "FONT_FAMILY",
      src[0] + "\nreturn fontSpec;")(S, 17, "OPLPoeVetica");
  };

  t.test("slot number and element agree", () => {
    const fontSpec = loadFontSpec();
    deepEq(fontSpec(3), fontSpec({ font: 3 }), "fontSpec(3) === fontSpec({font:3})");
  });

  t.test("a theme slot does not fall back to the default face", () => {
    const fontSpec = loadFontSpec();
    eq(fontSpec(3).size, 10, "slot 3 is the theme's 10px face");
    eq(fontSpec(3).family, "ThemeFont3", "slot 3 uses the theme family");
  });

  t.test("slot 0 and no argument still give the default", () => {
    const fontSpec = loadFontSpec();
    eq(fontSpec(0).size, 17, "slot 0 size");
    eq(fontSpec(undefined).size, 17, "undefined falls back to slot 0");
  });

  /* ------------------------------------------------------------ 1. parsing */

  t.group("parse: flat and prefix forms");

  t.test("flat key=value", () => {
    const d = P("main0_type=Background\nmain0_x=12\n");
    eq(C.getStr(d, "main0_type"), "Background");
    eq(C.getStr(d, "main0_x"), "12");
  });

  t.test("prefix form composes <prefix>_<key>", () => {
    const d = P("main0:\n\ttype=Background\n\tx=12\n");
    eq(C.getStr(d, "main0_type"), "Background");
    eq(C.getStr(d, "main0_x"), "12");
  });

  t.test("an unindented assignment clears the active prefix (§1.2)", () => {
    const d = P("main0:\n\ttype=Background\nx=99\n\ty=5\n");
    eq(C.getStr(d, "main0_type"), "Background");
    eq(C.getStr(d, "x"), "99", "column-0 assignment is a bare key");
    eq(C.getStr(d, "main0_y"), null, "prefix was cleared, so y is not main0_y");
    eq(C.getStr(d, "y"), "5");
  });

  t.test("blank and comment lines do NOT clear the prefix (§1.2)", () => {
    const d = P("main0:\n\ttype=Background\n\n# a note\n\tx=7\n");
    eq(C.getStr(d, "main0_x"), "7");
  });

  t.test("a comment containing '=' really is a key (§1.4)", () => {
    const d = P("# note = hello\n");
    eq(C.getStr(d, "# note "), " hello");
  });

  t.test("value keeps everything after the first '=' verbatim", () => {
    const d = P("main0_value=a=b # not a comment  \n");
    eq(C.getStr(d, "main0_value"), "a=b # not a comment  ");
  });

  t.test("only the first '=' splits", () => {
    const d = P("k=1=2\n");
    eq(C.getStr(d, "k"), "1=2");
  });

  t.test("CRLF is stripped and remembered", () => {
    const d = P("main0_type=Background\r\nmain0_x=1\r\n");
    eq(C.getStr(d, "main0_type"), "Background");
    eq(d.eol, "\r\n");
  });

  t.test("UTF-8 BOM is skipped and remembered (§1.1)", () => {
    const d = P("﻿main0_type=Background\n");
    eq(C.getStr(d, "main0_type"), "Background");
    ok(d.hadBOM);
  });

  t.test("duplicate keys: last value wins, first position kept (§11.4)", () => {
    const d = P("k=a\nk=b\n");
    eq(C.getStr(d, "k"), "b");
    ok(d.diags.some(x => x.code === "DUP_KEY"));
  });

  t.test("malformed lines survive as records", () => {
    const d = P("hello world\n");
    eq(d.lines[0].kind, "other");
    eq(C.getStr(d, "hello world"), null);
  });

  t.test("a comment containing ':' becomes a section header (§1.4)", () => {
    const d = P("main0:\n\ttype=Background\n\t# note: careful\n\tx=7\n");
    eq(C.getStr(d, "main0_x"), null, "the comment stole the prefix");
    ok(d.diags.some(x => x.code === "COMMENT_PREFIX"), "and the previewer says so");
  });

  t.test("a colon at position 0 is not a prefix (config.c:106)", () => {
    const d = P(":oops\n\tx=1\n");
    eq(d.lines[0].kind, "other");
    eq(C.getStr(d, "x"), "1", "no prefix was set, so the key is bare");
  });

  /* ------------------------------------------------------ 2. round-tripping */

  t.group("write: round-trip fidelity");

  const roundTrips = [
    ["empty", ""],
    ["no trailing newline", "a=1\nb=2"],
    ["trailing newline", "a=1\nb=2\n"],
    ["CRLF", "a=1\r\nb=2\r\n"],
    ["BOM", "﻿a=1\n"],
    ["blank lines and comments", "# top\n\nmain0:\n\ttype=Background\n\n# tail comment\n"],
    ["mixed flat and prefix", "a=1\nmain0:\n\ttype=Background\nb=2\nmain1_type=ItemsList\n"],
    ["space indentation", "main0:\n    type=Background\n    x=1\n"],
    ["weird whitespace", "  \t \nmain0:\n\t\ttype=Background   \n"],
  ];
  for (const [name, text] of roundTrips)
    t.test(`write(parse(x)) === x — ${name}`, () => eq(W(P(text)), text));

  for (const [name, text] of Object.entries(fixtures))
    t.test(`write(parse(x)) === x — ${name}`, () => eq(W(P(text)), text));

  t.group("write: surgical edits");

  t.test("editing an existing key changes exactly one line", () => {
    const src = "# keep me\nmain0:\n\ttype=Background\n\tx=10\n\ty=20\n";
    const d = P(src);
    C.setKey(d, "main0_x", "44");
    eq(W(d), "# keep me\nmain0:\n\ttype=Background\n\tx=44\n\ty=20\n");
  });

  t.test("edit preserves the original indentation style", () => {
    const d = P("main0:\n    type=Background\n    x=1\n");
    C.setKey(d, "main0_x", "2");
    eq(W(d), "main0:\n    type=Background\n    x=2\n");
  });

  t.test("a new key joins its existing section with matching indent", () => {
    const d = P("main0:\n\ttype=Background\nmain1:\n\ttype=ItemsList\n");
    C.setKey(d, "main0_x", "5");
    eq(W(d), "main0:\n\ttype=Background\n\tx=5\nmain1:\n\ttype=ItemsList\n");
  });

  t.test("a new key in a flat file stays flat", () => {
    const d = P("main0_type=Background\nmain1_type=ItemsList\n");
    C.setKey(d, "main0_x", "5");
    eq(W(d), "main0_type=Background\nmain0_x=5\nmain1_type=ItemsList\n");
  });

  t.test("a key for an absent section starts a new block", () => {
    const d = P("main0:\n\ttype=Background\n");
    C.setKey(d, "il_x", "42");
    eq(W(d), "main0:\n\ttype=Background\n\nil:\n\tx=42\n");
  });

  t.test("editing the last duplicate is what OPL would read", () => {
    const d = P("k=a\nk=b\n");
    C.setKey(d, "k", "c");
    eq(W(d), "k=a\nk=c\n");
    eq(C.getStr(P(W(d)), "k"), "c");
  });

  t.test("deleteKey removes every occurrence and nothing else", () => {
    const d = P("# c\nmain0:\n\ttype=Background\n\tx=1\n\tx=2\n");
    C.deleteKey(d, "main0_x");
    eq(W(d), "# c\nmain0:\n\ttype=Background\n");
  });

  t.test("edits survive a re-parse (idempotent)", () => {
    const d = P("main0:\n\ttype=Background\n");
    C.setKey(d, "main0_x", "9");
    const again = P(W(d));
    eq(C.getStr(again, "main0_x"), "9");
    eq(W(again), W(d));
  });

  /* ---------------------------------------------------------- 3. scalars */

  t.group("scalars: atoi and colours");

  t.test("atoi matches C semantics (§1.6)", () => {
    eq(C.atoi("12"), 12);
    eq(C.atoi("  -7abc"), -7);
    eq(C.atoi("POS_MID"), 0, "non-numeric is 0, which is why x/y are read as strings");
    eq(C.atoi(""), 0);
    eq(C.atoi("+3"), 3);
  });

  t.test("#RRGGBB parses", () => {
    const c = C.parseColor("#1B2430");
    deepEq([c.r, c.g, c.b], [0x1B, 0x24, 0x30]);
    ok(c.ok);
  });

  t.test("a colour with no '#' is BLACK, not an error (§1.5)", () => {
    const c = C.parseColor("FFFFFF");
    deepEq([c.r, c.g, c.b], [0, 0, 0]);
    eq(c.ok, false);
    eq(c.reason, "nohash");
  });

  t.test("a short colour fills what it can", () => {
    const c = C.parseColor("#F");
    deepEq([c.r, c.g, c.b], [0x0F, 0, 0]);
    eq(c.reason, "short");
  });

  t.test("parsing stops at the first non-hex digit", () => {
    const c = C.parseColor("#FFzz00");
    deepEq([c.r, c.g, c.b], [0xFF, 0, 0]);
  });

  /* -------------------------------------------------- 4. element building */

  t.group("build: coordinates and dimensions");

  const build = text => C.buildTheme(P(text));
  // The element the fixture declares, skipping any background OPL injected for us.
  const main = (text, i = 0) => build(text).chains.main.filter(e => !e.injected)[i];

  t.test("POS_MID resolves to the screen centre (§6.1)", () => {
    const e = main("main0_type=StaticText\nmain0_value=x\nmain0_x=POS_MID\nmain0_y=POS_MID\n");
    eq(e.posX, 320); eq(e.posY, 240);
  });

  t.test("POS_MID is a 7-char prefix match (§10 row 10)", () => {
    const e = main("main0_type=StaticText\nmain0_value=x\nmain0_x=POS_MIDDLE\n");
    eq(e.posX, 320);
  });

  t.test("negative x/y are right/bottom-edge relative (§6.1)", () => {
    const e = main("main0_type=StaticText\nmain0_value=x\nmain0_x=-86\nmain0_y=-296\n");
    eq(e.posX, 640 - 86); eq(e.posY, 480 - 296);
  });

  t.test("DIM_INF is the full screen; -1 means 'use the texture size' (§10 row 3)", () => {
    const a = main("main0_type=StaticImage\nmain0_default=x\nmain0_width=DIM_INF\nmain0_height=DIM_INF\n");
    eq(a.width, 640); eq(a.height, 480);
    const b = main("main0_type=StaticImage\nmain0_default=x\nmain0_width=-1\n");
    eq(b.width, C.DIM_UNDEF);
  });

  t.test("aligned is 0 or centre, never a bitfield (§6.1)", () => {
    eq(main("main0_type=ItemText\nmain0_aligned=0\n").aligned, C.ALIGN_NONE);
    eq(main("main0_type=ItemText\nmain0_aligned=1\n").aligned, C.ALIGN_CENTER);
    eq(main("main0_type=ItemText\nmain0_aligned=7\n").aligned, C.ALIGN_CENTER);
  });

  t.test("font=0 is rejected by the >0 test (§6.1)", () => {
    eq(main("main0_type=ItemText\nmain0_font=0\n").font, 0);
    eq(main("main0_type=ItemText\nmain0_font=3\n").font, 3);
    eq(main("main0_type=ItemText\nmain0_font=99\n").font, 0, "out of range is ignored");
  });

  t.test("per-type defaults match the §7 table", () => {
    const t1 = main("main0_type=MenuText\n");
    eq(t1.posX, 320); eq(t1.posY, 20); eq(t1.width, 200); eq(t1.height, 20);
    const t2 = main("main0_type=HintText\n");
    eq(t2.posX, 16); eq(t2.posY, 448, "-32 resolves from the bottom edge");
    const t3 = main("main0_type=BdmIndex\n");
    eq(t3.posX, 320); eq(t3.posY, 355);
    const t4 = main("main0_type=ItemIcon\n");
    eq(t4.width, 64); eq(t4.height, 64, "forced to 64x64 since 2024-08-19");
    eq(t4.extended.pattern, "ICO"); eq(t4.extended.count, 20);
    const t5 = main("main0_type=Background\nmain1_type=ItemCover\n", 1);
    eq(t5.extended.pattern, "COV"); eq(t5.extended.count, 10);
  });

  t.test("image elements default to the neutral tint, text to text_color (§10 row 7)", () => {
    deepEq(main("main0_type=StaticImage\nmain0_default=x\n").color, C.TINT);
    deepEq(main("main0_type=ItemText\n").color, C.THEME_DEFAULTS.text_color);
  });

  t.group("build: the scan loop (§2.2)");

  t.test("a numbering gap ends the scan and orphans everything after it", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain3_type=ItemText\n");
    eq(th.chains.main.length, 2);
    const d = th.diags.find(x => x.code === "NUMBER_GAP");
    ok(d, "NUMBER_GAP raised");
    ok(d.message.includes("main3"), "the orphan is named");
  });

  t.test("an unknown type does NOT stop the scan", () => {
    const th = build("main0_type=Background\nmain1_type=Nonsense\nmain2_type=ItemText\n");
    eq(th.chains.main.filter(e => !e.injected).length, 2);
    ok(th.diags.some(x => x.code === "UNKNOWN_TYPE"));
    ok(!th.diags.some(x => x.code === "NUMBER_GAP"));
  });

  t.test("a case error is diagnosed as a case error", () => {
    const th = build("main0_type=Background\nmain1_type=itemslist\n");
    const d = th.diags.find(x => x.code === "UNKNOWN_TYPE");
    ok(d.message.includes("ItemsList"), "suggests the correct casing");
  });

  t.test("enabled=0 skips one element without stopping the scan", () => {
    const th = build("main0_type=Background\nmain1_type=ItemText\nmain1_enabled=0\nmain2_type=MenuIcon\n");
    const names = th.chains.main.map(e => e.name);
    ok(!names.includes("main1"));
    ok(names.includes("main2"));
    ok(!th.diags.some(x => x.code === "NUMBER_GAP"));
  });

  t.test("types are case-sensitive", () => {
    eq(build("main0_type=background\n").chains.main.filter(e => !e.injected).length, 0);
  });

  t.group("build: Background and ItemsList rules");

  t.test("a Background after index 0 is discarded, then a default is injected (§3.1)", () => {
    const th = build("main0_type=ItemsList\nmain1_type=Background\nmain2_type=ItemText\n");
    ok(th.diags.some(x => x.code === "BG_NOT_FIRST"));
    eq(th.chains.main[0].typeName, "Background");
    ok(th.chains.main[0].injected);
    eq(th.chains.main.filter(e => e.typeName === "Background").length, 1);
  });

  t.test("Background is allowed at main1 if main0 produced nothing", () => {
    const th = build("main0_type=Bogus\nmain1_type=Background\n");
    ok(!th.diags.some(x => x.code === "BG_NOT_FIRST"));
    ok(!th.chains.main[0].injected);
  });

  t.test("a missing ItemsList is injected as the second element (§5.3)", () => {
    const th = build("main0_type=Background\nmain1_type=ItemText\n");
    ok(th.diags.some(x => x.code === "IL_INJECTED"));
    eq(th.chains.main[1].typeName, "ItemsList");
    eq(th.chains.main[1].width, 373);
    eq(th.chains.main[1].extended.displayedItems, 16);
  });

  t.test("an empty info page stays empty (no injected background)", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\n");
    eq(th.chains.info.length, 0);
  });

  t.test("displayedItems = height / 19, and there is no items= key (§10 row 2)", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain1_height=304\nmain1_items=3\n");
    eq(th.chains.main[1].extended.displayedItems, 16);
    eq(Math.floor(304 / C.MENU_ITEM_HEIGHT), 16);
  });

  t.test("an ItemsList with no height defaults to 398 => 20 rows", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\n");
    eq(th.chains.main[1].height, 398);
    eq(th.chains.main[1].extended.displayedItems, 20);
  });

  t.test("the second ItemsList uses different defaults; a third is dropped", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain2_type=ItemsList\nmain3_type=ItemsList\n");
    eq(th.chains.main[2].width, 400);
    eq(th.chains.main[2].height, 360);
    ok(th.diags.some(x => x.code === "THIRD_ITEMSLIST"));
  });

  t.group("build: decorators (§5.2)");

  const decoTheme = count => build(
    "main0_type=Background\n" +
    "main1_type=ItemIcon\nmain1_count=" + count + "\n" +
    "main2_type=ItemsList\nmain2_height=190\nmain2_decorator=ICO\n");

  t.test("a decorator attaches when count >= rows", () => {
    const th = decoTheme(20);
    eq(th.chains.main[2].extended.displayedItems, 10);
    ok(th.chains.main[2].extended.decoratorImage, "decorator attached");
  });

  t.test("a decorator is silently dropped when count < rows", () => {
    const th = decoTheme(4);
    eq(th.chains.main[2].extended.decoratorImage, null);
    const d = th.diags.find(x => x.code === "DECORATOR_DROPPED");
    ok(d);
    ok(d.message.includes("10"), "states the row count the author needs to match");
  });

  t.test("a decorator with no matching pattern is reported", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain1_decorator=ZZZ\n");
    ok(th.diags.some(x => x.code === "DECORATOR_NO_TARGET"));
  });

  t.test("a GameImage with no pattern does not crash the decorator search (§5.3)", () => {
    const th = build("main0_type=Background\nmain1_type=GameImage\nmain2_type=ItemsList\nmain2_decorator=ICO\n");
    ok(th.diags.some(x => x.code === "MISSING_REQUIRED"));
    ok(th.diags.some(x => x.code === "DECORATOR_NO_TARGET"));
  });

  t.group("build: required attributes and text sizing");

  t.test("required attributes are reported per type", () => {
    for (const [type, extra, missing] of [
      ["StaticText", "", "value"],
      ["AttributeText", "", "attribute"],
      ["GameImage", "", "pattern"],
      ["StaticImage", "", "default"],
      ["AttributeImage", "", "attribute"],
    ]) {
      const th = build(`main0_type=Background\nmain1_type=${type}\n${extra}`);
      const d = th.diags.find(x => x.code === "MISSING_REQUIRED");
      ok(d, `${type} without ${missing} is reported`);
      ok(d.message.includes(missing), `${type}: names the missing key`);
    }
  });

  t.test("setting either width or height promotes text to CLIP and fills the other (§6.2)", () => {
    const a = main("main0_type=StaticText\nmain0_value=hi\n");
    eq(a.extended.sizingMode, C.SIZING_NONE);
    eq(a.width, C.DIM_UNDEF);
    const b = main("main0_type=StaticText\nmain0_value=hi\nmain0_width=200\n");
    eq(b.extended.sizingMode, C.SIZING_CLIP);
    eq(b.height, 480, "the unset dimension is filled with the screen height");
  });

  t.test("wrap=1 sets WRAP; wrap only matters when a box exists", () => {
    const a = main("main0_type=StaticText\nmain0_value=hi\nmain0_width=200\nmain0_wrap=1\n");
    eq(a.extended.sizingMode, C.SIZING_WRAP);
    const b = main("main0_type=StaticText\nmain0_value=hi\nmain0_wrap=1\n");
    eq(b.extended.sizingMode, C.SIZING_NONE, "no width/height ⇒ no sizing at all");
  });

  t.test("AttributeText label defaults to the attribute minus a leading '#'", () => {
    eq(main("main0_type=AttributeText\nmain0_attribute=#Size\n").extended.alias, "Size: ");
    eq(main("main0_type=AttributeText\nmain0_attribute=Genre\nmain0_title=Kind\n").extended.alias, "Kind: ");
    eq(main("main0_type=AttributeText\nmain0_attribute=Genre\nmain0_width=100\nmain0_wrap=1\n").extended.alias,
       "Genre:\n", "wrapped labels break after the colon");
  });

  t.group("build: theme settings and apps inheritance");

  t.test("theme colours override the defaults, and a bad one is diagnosed", () => {
    const th = build("text_color=#102030\nsel_text_color=ABC\nmain0_type=Background\n");
    deepEq(th.textColor, { r:0x10, g:0x20, b:0x30 });
    deepEq(th.selTextColor, { r:0, g:0, b:0 });
    ok(th.diags.some(x => x.code === "COLOR_NO_HASH"));
  });

  t.test("font slots and sizes (§8)", () => {
    const th = build("default_font=A.ttf\ndefault_font_size=22\nfont1=B.ttf\nmain0_type=Background\n");
    eq(th.fonts[0].file, "A.ttf"); eq(th.fonts[0].size, 22);
    eq(th.fonts[1].file, "B.ttf"); eq(th.fonts[1].size, 17, "unset size falls back to 17");
  });

  t.test("font size <= 0 falls back to 17", () => {
    eq(build("default_font=A.ttf\ndefault_font_size=0\nmain0_type=Background\n").fonts[0].size, 17);
  });

  t.test("the apps page mirrors main by index unless overridden (§2.3)", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain2_type=ItemText\n" +
                     "appsMain2_type=MenuIcon\n");
    const apps = th.chains.appsMain.map(e => e.typeName);
    deepEq(apps.slice(0, 3), ["Background", "ItemsList", "MenuIcon"]);
    eq(th.chains.appsMain[0].inheritedFrom, "main0");
  });

  t.test("appsMainN_enabled=0 removes an element from the apps page only", () => {
    const th = build("main0_type=Background\nmain1_type=ItemsList\nmain2_type=ItemText\n" +
                     "appsMain2_enabled=0\n");
    ok(th.chains.main.some(e => e.typeName === "ItemText"));
    ok(!th.chains.appsMain.some(e => e.typeName === "ItemText"));
  });

  /* -------------------------------------------------------- 5. validation */

  t.group("validate: assets and attributes");

  const assets = names => ({ names: new Set(names.map(n => n.toLowerCase())) });
  const val = (text, a) => { const d = P(text); return C.validateTheme(d, C.buildTheme(d), a); };

  t.test("an extension in a filename is flagged (§6.4)", () => {
    const r = val("main0_type=Background\nmain0_default=logo.png\n", null);
    ok(r.diags.some(x => x.code === "EXT_IN_FILENAME"));
  });

  t.test("a missing asset is an error", () => {
    const r = val("main0_type=Background\nmain0_default=backdrop\n", assets(["other.png"]));
    ok(r.diags.some(x => x.code === "MISSING_ASSET"));
  });

  t.test("a jpg-only asset gets the PNG-only explanation (§10 row 1)", () => {
    const r = val("main0_type=Background\nmain0_default=backdrop\n", assets(["backdrop.jpg"]));
    const d = r.diags.find(x => x.code === "JPG_ASSET");
    ok(d);
    ok(d.message.includes("png"));
  });

  t.test("a present asset is silent", () => {
    const r = val("main0_type=Background\nmain0_default=backdrop\n", assets(["backdrop.png"]));
    ok(!r.diags.some(x => x.code === "MISSING_ASSET" || x.code === "JPG_ASSET"));
  });

  t.test("an attribute the type never reads is reported (§6.6)", () => {
    const r = val("main0_type=Background\nmain1_type=ItemText\nmain1_pattern=COV\n", null);
    ok(r.diags.some(x => x.code === "INVALID_ATTR" && x.message.includes("main1_pattern")));
  });

  t.test("a missing font file is reported", () => {
    const r = val("default_font=Inter.ttf\nmain0_type=Background\n", assets(["backdrop.png"]));
    ok(r.diags.some(x => x.code === "MISSING_ASSET" && x.message.includes("Inter.ttf")));
  });

  t.group("validate: PS2 art cache estimate (§6.5)");

  const withArt = (text, sizes) => {
    const d = P(text);
    return C.validateTheme(d, C.buildTheme(d), { names: new Set(), artSize: new Map(Object.entries(sizes)) });
  };

  t.test("bytes are count x SOURCE w x h x 3, measured from the art", () => {
    const r = withArt("main0_type=Background\nmain1_type=ItemCover\nmain1_count=10\n",
                      { COV: { w:140, h:200 } });
    eq(r.memory.rows.find(x => x.pattern === "COV").bytes, 10 * 140 * 200 * 3);
  });

  t.test("the element's declared size does NOT reduce the estimate", () => {
    // The cache holds the decoded source image; width/height only scale the
    // draw quad. Sizing off the element would under-report the real risk.
    const big = { COV: { w:512, h:512 } };
    const a = withArt("main0_type=Background\nmain1_type=ItemCover\nmain1_count=10\n", big);
    const b = withArt("main0_type=Background\nmain1_type=ItemCover\nmain1_count=10\nmain1_width=40\nmain1_height=60\n", big);
    eq(a.memory.total, b.memory.total);
    eq(a.memory.total, 10 * 512 * 512 * 3);
  });

  t.test("a fat cache trips the danger level", () => {
    const r = withArt("main0_type=Background\nmain1_type=ItemCover\nmain1_count=40\n",
                      { COV: { w:512, h:512 } });
    eq(r.memory.level, "err");
    ok(r.memory.total > 30e6);
  });

  t.test("VRAM peak counts one texture per tile, at source size x4", () => {
    // 4 MB minus two 640x480 CT24 framebuffers.
    eq(C.VRAM_FOR_TEXTURES, 4 * 1024 * 1024 - 2 * 640 * 480 * 4);
    const r = withArt("main0_type=Background\nmain1_type=ItemCover\nmain1_count=12\n" +
                      "main2_type=ItemsList\nmain2_columns=6\nmain2_cell_width=88\nmain2_cell_height=137\n" +
                      "main2_height=274\nmain2_decorator=COV\n",
                      { COV: { w:300, h:450 } });
    const grid = r.vram.rows.find(x => x.concurrent === 12);
    ok(grid, "the grid contributes one texture per tile");
    eq(grid.bytes, 12 * 300 * 450 * 4);
    eq(r.vram.level, "err", "12 covers at 300x450 cannot be resident at once");
  });

  t.test("smaller source art brings the same grid inside the VRAM budget", () => {
    const cfg = "main0_type=Background\nmain1_type=ItemCover\nmain1_count=12\n" +
                "main2_type=ItemsList\nmain2_columns=6\nmain2_cell_width=88\nmain2_cell_height=137\n" +
                "main2_height=274\nmain2_decorator=COV\n";
    ok(withArt(cfg, { COV: { w:300, h:450 } }).vram.total > C.VRAM_FOR_TEXTURES);
    ok(withArt(cfg, { COV: { w:100, h:150 } }).vram.total < C.VRAM_FOR_TEXTURES);
  });

  t.test("a shared pattern is counted once (findDuplicate, §6.4)", () => {
    const r = val("main0_type=Background\nmain1_type=ItemCover\nmain1_width=100\nmain1_height=100\n" +
                  "main2_type=ItemCover\nmain2_width=100\nmain2_height=100\n", null);
    eq(r.memory.rows.filter(x => x.pattern === "COV").length, 1);
    eq(r.memory.rows.find(x => x.pattern === "COV").shared, true);
  });

  /* ------------------------------------------------------------ 6. layout */

  t.group("layout: geometry and text");

  t.test("aligned=0 anchors the top-left, aligned=1 the centre (§9.1)", () => {
    deepEq(C.setupQuad(100, 50, C.ALIGN_NONE, 40, 20, C.SCALING_NONE, 4), { x:100, y:50, w:40, h:20 });
    deepEq(C.setupQuad(100, 50, C.ALIGN_CENTER, 40, 20, C.SCALING_NONE, 4), { x:80, y:40, w:40, h:20 });
  });

  t.test("scaled=1 narrows width to 3/4 in 16:9; scaled=0 does not (§9.2)", () => {
    eq(C.setupQuad(0, 0, C.ALIGN_NONE, 100, 10, C.SCALING_RATIO, 3).w, 75);
    eq(C.setupQuad(0, 0, C.ALIGN_NONE, 100, 10, C.SCALING_RATIO, 4).w, 100);
    eq(C.setupQuad(0, 0, C.ALIGN_NONE, 100, 10, C.SCALING_NONE, 3).w, 100, "positions/sizes untouched when scaled=0");
  });

  t.test("widescreen never moves x (§9.2)", () => {
    eq(C.setupQuad(100, 0, C.ALIGN_NONE, 40, 10, C.SCALING_RATIO, 3).x, 100);
  });

  t.test("baseline offsets match fntRenderString (§9.3)", () => {
    eq(C.textBaselineOffset(C.ALIGN_NONE, 17), 15);
    eq(C.textBaselineOffset(C.ALIGN_CENTER, 17), 6);
    eq(C.textBaselineOffset(C.ALIGN_CENTER, 22), 9);
  });

  t.test("fitString wraps on spaces", () => {
    const measure = s => s.length * 10;              // 10px per character
    eq(C.fitString(measure, "aaa bbb ccc", 80), "aaa bbb\nccc");
  });

  t.test("fitString honours existing newlines", () => {
    const measure = s => s.length * 10;
    eq(C.fitString(measure, "aa\nbb cc", 100), "aa\nbb cc");
  });

  t.test("a word longer than the box overflows rather than being cut (§9.3)", () => {
    const measure = s => s.length * 10;
    eq(C.fitString(measure, "aaaaaaaaaa bb", 50), "aaaaaaaaaa\nbb");
  });

  /* ---------------------------------------------------------- 7. fixtures */

  t.group("fixtures");

  t.test("minimal: two elements, no warnings that matter", () => {
    const th = C.buildTheme(P(fixtures["fixtures/minimal/conf_theme.cfg"]));
    eq(th.chains.main.length, 2);
    eq(th.chains.main[0].typeName, "Background");
    eq(th.chains.main[1].typeName, "ItemsList");
    eq(th.chains.main[1].extended.displayedItems, 16);
    ok(!th.diags.some(x => x.severity === "error"));
  });

  t.test("midrange: POS_MID, negative coords, decorator and overlay all resolve", () => {
    const d = P(fixtures["fixtures/midrange/conf_theme.cfg"]);
    const th = C.buildTheme(d);
    eq(th.chains.main.length, 9);
    const title = th.chains.main[1];
    eq(title.posX, 320); eq(title.font, 1);
    const list = th.chains.main[3];
    ok(list.extended.decoratorImage, "decorator attached (24 cached >= 16 rows)");
    const cover = th.chains.main[4];
    eq(cover.posX, 640 - 118); eq(cover.posY, 480 - 300);
    eq(cover.extended.overlay, "case");
    eq(cover.extended.overlayRect.lry, 214);
    eq(th.chains.info.length, 7);
    eq(th.chains.appsMain[1].extended.value, "APPLICATIONS", "appsMain1 overrides main1");
    eq(th.chains.appsMain[2].inheritedFrom, "main2", "the rest is inherited");
    ok(!th.diags.some(x => x.severity === "error"));
  });

  t.test("broken: every intended failure is diagnosed", () => {
    const d = P(fixtures["fixtures/broken/conf_theme.cfg"]);
    const th = C.buildTheme(d);
    const r = C.validateTheme(d, th, assets(["logo.png", "backdrop.png"]));
    const codes = new Set([...th.diags, ...r.diags].map(x => x.code));
    for (const want of ["COLOR_NO_HASH", "EXT_IN_FILENAME", "DECORATOR_DROPPED", "BG_NOT_FIRST",
                        "NUMBER_GAP", "MISSING_REQUIRED", "MISSING_ASSET", "INVALID_ATTR"])
      ok(codes.has(want), `broken fixture raises ${want}`);
    eq(th.chains.main.filter(e => !e.injected).length, 5, "the scan stops at the main7 gap");
    ok([...th.diags, ...r.diags].some(x => x.code === "NUMBER_GAP" && x.message.includes("main8")),
       "the orphaned elements are named");
    eq(r.memory.level, "err", "the 512x512 x40 cache is flagged");
  });

  t.test("OPL's own conf_theme_OPL.cfg builds cleanly", () => {
    const d = P(fixtures["docs/reference-conf_theme_OPL.cfg"]);
    const th = C.buildTheme(d);
    eq(th.chains.main.length, 8, "main0..main8, less main8 which is a LoadingIcon");
    eq(th.chains.main[0].typeName, "Background");
    eq(th.chains.info.length, 18, "info0..info17");
    ok(th.loadingIcon, "LoadingIcon is captured off-chain (§3.2)");
    ok(!th.chains.main.some(e => e.typeName === "LoadingIcon"), "…and never appears in the chain");
    eq(th.chains.appsMain[3].name, "appsMain3", "appsMain3 overrides main3");
    eq(th.chains.appsMain[0].inheritedFrom, "main0");
    ok(!th.diags.some(x => x.severity === "error"),
       "no errors: " + th.diags.filter(x => x.severity === "error").map(x => x.code).join(","));
  });
}

