#!/usr/bin/env python3
"""Merge _deploy/CFG onto a device's CFG, preserving anything OPL wrote.

    tools/merge-cfg.py <device-CFG-dir> [source-CFG-dir]

Never rsync CFG. OPL owns the $-prefixed keys in the per-game config --
$Compatibility, $ConfigSource -- and configWrite rewrites the whole file, so a
plain copy silently discards a compatibility mode the user set on the console.
That is not hypothetical: the card had $Compatibility=2 on Shadow of the Colossus
when this was written.

OPL writes its own keys into the per-game CFG -- $Compatibility, $ConfigSource
and friends -- and configWrite rewrites the whole file, so a plain copy would
throw away a compatibility mode the user set on the console. This keeps the
card's $-prefixed keys and takes the descriptive keys from _deploy.

Key order follows the card's file where a key already exists there, so a merged
file is a minimal diff rather than a reshuffle.
"""
import os, sys
card = sys.argv[1]
src = sys.argv[2] if len(sys.argv) > 2 else "_deploy/CFG"
changed = preserved = untouched = 0
for name in sorted(os.listdir(src)):
    if not name.endswith(".cfg"):
        continue
    s = os.path.join(src, name)
    c = os.path.join(card, name)
    new = {}
    order = []
    for line in open(s, encoding="utf-8", errors="replace"):
        line = line.rstrip("\n")
        if "=" in line and not line.startswith("#"):
            k = line.split("=", 1)[0]
            new[k] = line
            order.append(k)
    kept = []
    if os.path.exists(c):
        out, seen = [], set()
        for line in open(c, encoding="utf-8", errors="replace"):
            line = line.rstrip("\n")
            k = line.split("=", 1)[0] if "=" in line else None
            if k is None:
                out.append(line)
            elif k.startswith("$"):
                out.append(line)          # OPL's own -- never ours to change
                kept.append(k)
                seen.add(k)
            elif k in new:
                out.append(new[k])        # refresh from _deploy, keep position
                seen.add(k)
            else:
                out.append(line)          # unknown: leave it alone
                seen.add(k)
        for k in order:
            if k not in seen:
                out.append(new[k])
    else:
        out = [new[k] for k in order]
    text = "\n".join(out) + "\n"
    old = open(c, encoding="utf-8", errors="replace").read() if os.path.exists(c) else None
    if old == text:
        untouched += 1
        continue
    open(c, "w", encoding="utf-8").write(text)
    changed += 1
    if kept:
        preserved += 1
        print(f"  {name}: updated, preserved {' '.join(kept)}")
print(f"\n{changed} written, {untouched} already current, {preserved} had OPL keys preserved")
