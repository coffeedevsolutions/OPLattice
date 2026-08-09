/**
 * Embed assets/PoeVeticaNew.ttf into opl-theme-previewer.html as a base64
 * literal, so the tool renders text correctly when opened over file:// (where
 * Chrome blocks fetching a sibling file).
 *
 *   node tools/embed-font.mjs
 *
 * assets/PoeVeticaNew.ttf stays the source of truth — re-run this after
 * replacing it. Safe to run repeatedly: it replaces whatever literal is there.
 */
import { readFileSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");
const HTML = join(ROOT, "opl-theme-previewer.html");

const b64 = readFileSync(join(ROOT, "assets/PoeVeticaNew.ttf")).toString("base64");
const html = readFileSync(HTML, "utf8");

const re = /(const FONT_B64 = ")[^"]*(";)/;
if (!re.test(html)) { console.error("FONT_B64 literal not found"); process.exit(1); }

const out = html.replace(re, `$1${b64}$2`);
writeFileSync(HTML, out);
console.log(`embedded ${b64.length} base64 chars (${(b64.length / 1024).toFixed(1)} KB) into ${HTML}`);
