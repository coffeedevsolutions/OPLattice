# `minimal/` — the smallest theme that works

`Background` + `ItemsList`, nothing else. Written in the **flat** `key=value`
form rather than the `main0:` prefix form, so it covers the other parser path
(both produce identical keys — see THEME-FORMAT §1.2).

What it demonstrates:

* `width=DIM_INF` / `height=DIM_INF` — the literal strings that mean "full
  screen". Note `-1` would mean the *opposite*: use the texture's own size.
* `height=304` on the ItemsList ⇒ `304 / 19 = 16` rows. There is no `items=`
  attribute; row count comes from the height.
* Everything else falls back to the per-type defaults, so this is a good file to
  select elements in and watch the inspector show "defaulted" vs "explicit".

Assets: `backdrop.png` (640×480).
