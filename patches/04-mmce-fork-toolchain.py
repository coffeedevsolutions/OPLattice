#!/usr/bin/env python3
"""Bring ps2-mmce/Open-PS2-Loader @ OPL-MMCE-beta-2 up to a current ps2dev toolchain.

    python3 patches/04-mmce-fork-toolchain.py <path-to-opl-mmce-checkout>

The fork branched in January 2025 and has not been rebased since. Every change
here is one mainline already made; none is specific to the theme patches and
none alters runtime behaviour. Run this BEFORE applying patches 01-03.

Against ps2dev/ps2dev:latest (GCC 15.2, __STDC_VERSION__ 202311L):

  renderman.c    gsKit's vsync callback gained a `cause` argument.
  *support.h     C23 makes `f()` declare *no* parameters rather than an
                 unspecified list, so four Init declarations now conflict with
                 definitions taking an item_list_t *. The other Init functions
                 are `()` in both header and definition and stay consistent.
  exports.tab    iopfixup refuses a module whose exported symbol sits at .text
                 offset 0. Defining the stub above the export table puts it
                 there; upstream moved those definitions below it.
  imgdrv.c       Same C23 rule against the device-driver function pointers;
                 upstream replaced the shared stub with the SDK's
                 IOMAN_RETURN_VALUE macros.

Note the SDK itself is fine: mmceman.irx, mmcedrv.irx and mmceigr.irx were
upstreamed into ps2sdk and are present in current images. Only the older pinned
image in the fork's own CI lacks them.

Idempotent -- each edit asserts its target appears exactly once, and reports
"already applied" rather than failing on a second run.
"""
import os
import sys

root = sys.argv[1] if len(sys.argv) > 1 else "."
applied, already, missing = [], [], []


def edit(rel, old, new, why):
    p = os.path.join(root, rel)
    if not os.path.exists(p):
        missing.append(rel)
        return
    s = open(p).read()
    if old not in s and new in s:
        already.append(why)
        return
    if s.count(old) != 1:
        raise SystemExit(f"FAILED {rel}: {why} -- expected 1 match of the "
                         f"pre-image, found {s.count(old)}. Wrong base revision?")
    open(p, "w").write(s.replace(old, new))
    applied.append(why)


# --- gsKit vsync callback -----------------------------------------------------
edit("src/renderman.c",
     "static int rmOnVSync(void)",
     "static int rmOnVSync(int cause)",
     "renderman: rmOnVSync(int cause)")

# --- C23 prototypes -----------------------------------------------------------
for hdr, fn in (("appsupport", "appInit"), ("bdmsupport", "bdmInit"),
                ("hddsupport", "hddInit"), ("mmcesupport", "mmceInit")):
    edit(f"include/{hdr}.h", f"void {fn}();",
         f"void {fn}(item_list_t *itemList);", f"{hdr}.h: {fn} parameter")

# --- iopfixup: exported symbol at .text offset 0 ------------------------------
# The stub block differs per module (cdvdman has _ret0 and returns 1, cdvdfsv
# does not and returns 0), so split structurally on the export table rather than
# matching a fixed body.
def move_stubs_below_table(rel):
    p = os.path.join(root, rel)
    if not os.path.exists(p):
        missing.append(rel)
        return
    s = open(p).read()
    i = s.find("DECLARE_EXPORT_TABLE(")
    if i < 0:
        missing.append(f"{rel} (no export table)")
        return
    # Walk back over the comment and blank lines that introduce the table.
    lines = s[:i].split("\n")
    while lines and (not lines[-1].strip() or lines[-1].lstrip().startswith("//")):
        lines.pop()
    preamble = "\n".join(lines).strip()
    if not preamble:
        already.append(f"{rel}: stubs already below the table")
        return
    rest = s[len("\n".join(lines)):].lstrip("\n")
    open(p, "w").write(rest.rstrip("\n") + "\n\n" + preamble + "\n")
    applied.append(f"{rel}: stubs moved below the export table")


# Every module with a stub above its table has the same problem, and the stub is
# named _retonly in some and _funcret in others, so sweep rather than enumerate.
for dirpath, _, files in sorted(os.walk(os.path.join(root, "modules"))):
    if "exports.tab" in files:
        move_stubs_below_table(
            os.path.relpath(os.path.join(dirpath, "exports.tab"), root))

# --- imgdrv device-driver function pointers -----------------------------------
edit("modules/iopcore/imgdrv/imgdrv.c",
     "int close_fs()",
     "int close_fs(iop_file_t *fd)",
     "imgdrv: close_fs parameter")
edit("modules/iopcore/imgdrv/imgdrv.c",
     "iop_device_ops_t my_device_ops =\n    {\n        dummy_fs, // init\n"
     "        dummy_fs, // deinit\n        NULL,     // dummy_fs,// format\n"
     "        dummy_fs, // open_fs,// open",
     "IOMAN_RETURN_VALUE_IMPL(0);\n\niop_device_ops_t my_device_ops =\n    {\n"
     "        IOMAN_RETURN_VALUE(0), // init\n        IOMAN_RETURN_VALUE(0), // deinit\n"
     "        NULL,                  // format\n        IOMAN_RETURN_VALUE(0), // open",
     "imgdrv: stubs replaced with IOMAN_RETURN_VALUE")

for label, items in (("applied", applied), ("already applied", already),
                     ("absent (skipped)", missing)):
    if items:
        print(f"{label}:")
        for i in items:
            print(f"  {i}")
if not applied and already:
    print("\nnothing to do -- tree is already ported")
