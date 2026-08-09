/**
 * Headless test runner.  node tests/run.mjs
 *
 * No dependencies, no package.json. The core is extracted from
 * opl-theme-previewer.html between the OPL-CORE markers and imported as a
 * module, so there is no second copy of the logic to drift out of sync.
 */
import { readFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { specs } from "./spec.mjs";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

export function extractCore(html) {
  const a = html.indexOf("// ===== OPL-CORE-BEGIN =====");
  const b = html.indexOf("// ===== OPL-CORE-END =====");
  if (a < 0 || b < 0) throw new Error("core markers not found in opl-theme-previewer.html");
  return html.slice(a, b);
}

const html = readFileSync(join(ROOT, "opl-theme-previewer.html"), "utf8");
const source = extractCore(html) + "\nexport default OPLCore;\n";
const C = (await import("data:text/javascript;base64," + Buffer.from(source, "utf8").toString("base64"))).default;

const FIXTURE_FILES = [
  "fixtures/minimal/conf_theme.cfg",
  "fixtures/midrange/conf_theme.cfg",
  "fixtures/broken/conf_theme.cfg",
  "docs/reference-conf_theme_OPL.cfg",
];
const fixtures = Object.fromEntries(FIXTURE_FILES.map(f => [f, readFileSync(join(ROOT, f), "utf8")]));

/* ------------------------------------------------------------- harness */

const state = { group: "", pass: 0, fail: 0, failures: [] };
const show = v => typeof v === "string" ? JSON.stringify(v) : JSON.stringify(v) ?? String(v);

const t = {
  group(name) { state.group = name; console.log(`\n\x1b[1m${name}\x1b[0m`); },
  test(name, fn) {
    try {
      fn();
      state.pass++;
      console.log(`  \x1b[32m✓\x1b[0m ${name}`);
    } catch (e) {
      state.fail++;
      state.failures.push({ group: state.group, name, error: e });
      console.log(`  \x1b[31m✗ ${name}\x1b[0m\n      ${e.message.replace(/\n/g, "\n      ")}`);
    }
  },
  eq(actual, expected, msg) {
    if (!Object.is(actual, expected))
      throw new Error(`${msg ? msg + "\n      " : ""}expected ${show(expected)}\n      actual   ${show(actual)}`);
  },
  ok(v, msg) { if (!v) throw new Error(msg || "expected a truthy value"); },
  deepEq(actual, expected, msg) {
    const a = JSON.stringify(actual), b = JSON.stringify(expected);
    if (a !== b) throw new Error(`${msg ? msg + "\n      " : ""}expected ${b}\n      actual   ${a}`);
  },
  throws(fn, msg) { try { fn(); } catch { return; } throw new Error(msg || "expected a throw"); },
};

specs(C, fixtures, t);

console.log(`\n${state.fail === 0 ? "\x1b[32m" : "\x1b[31m"}${state.pass} passed, ${state.fail} failed\x1b[0m`);
process.exit(state.fail === 0 ? 0 : 1);
