#!/usr/bin/env python3
"""Generate tiny stand-in ISOs so the grid can be judged with a full library.

Each stub is a valid ISO9660 volume of 22 sectors (44 KB) whose only file is a
SYSTEM.CNF pointing BOOT2 at the real game's serial. That is all OPL reads to
list a game (supportbase.c:335), so the grid fills with real serials, real art
and real titles without moving the ISOs themselves.

THESE DO NOT BOOT. There is no ELF behind BOOT2. Launching one gets you a hang
and a power cycle -- they exist to look at, not to run.

    tools/make-stub-isos.py [ps2-dir] [out-dir]

Naming matters more than it looks. isValidIsoName (supportbase.c:61) treats a
filename as the legacy "SCUS_XXX.XX.Title.iso" format whenever it is >=17 chars
with '_' at index 4 and '.' at 8 and 11, and then trusts the filename for the
serial instead of reading the disc. Anything else is the modern format, where
the serial comes from SYSTEM.CNF and the filename becomes the on-screen title.
This writes modern-format names and refuses to emit one that would trip the
legacy test.
"""
import contextlib, io, os, re, struct, sys

SEC = 2048
NSECS = 22
PVD_LBA, TERM_LBA, LPATH_LBA, MPATH_LBA, ROOT_LBA, CNF_LBA = 16, 17, 18, 19, 20, 21


def both16(v):
    return struct.pack("<H", v) + struct.pack(">H", v)


def both32(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


def dirrec(name, lba, size, isdir):
    """A single ISO9660 directory record, padded to an even length."""
    rec = bytearray()
    rec += b"\x00\x00"                      # length + ext attr, length filled below
    rec += both32(lba)
    rec += both32(size)
    rec += bytes([125, 1, 1, 0, 0, 0, 0])   # 2025-01-01 00:00:00 GMT
    rec += bytes([2 if isdir else 0, 0, 0])  # flags, unit size, interleave
    rec += both16(1)                        # volume sequence number
    rec += bytes([len(name)]) + name
    if len(rec) % 2:
        rec += b"\x00"
    rec[0] = len(rec)
    return bytes(rec)


def pvd(volid):
    p = bytearray(b"\x00" * SEC)
    p[0:7] = bytes([1]) + b"CD001" + bytes([1])
    p[8:40] = b"PLAYSTATION".ljust(32)
    p[40:72] = volid[:32].encode("ascii", "replace").ljust(32)
    p[80:88] = both32(NSECS)
    p[120:124] = both16(1)
    p[124:128] = both16(1)
    p[128:132] = both16(SEC)
    p[132:140] = both32(10)                 # path table size
    p[140:144] = struct.pack("<I", LPATH_LBA)
    p[148:152] = struct.pack(">I", MPATH_LBA)
    root = dirrec(b"\x00", ROOT_LBA, SEC, True)
    p[156:156 + len(root)] = root
    for lo, hi in ((190, 318), (318, 446), (446, 574), (574, 702)):
        p[lo:hi] = b" " * (hi - lo)
    for lo in (702, 739, 776):
        p[lo:lo + 37] = b" " * 37
    for lo in (813, 830, 847, 864):
        p[lo:lo + 17] = b"2025010100000000\x00"
    p[881] = 1
    return bytes(p)


def build(serial, title):
    cnf = f"BOOT2 = cdrom0:\\{serial};1\r\nVER = 1.00\r\nVMODE = NTSC\r\n".encode()

    img = bytearray(b"\x00" * (SEC * NSECS))

    def put(lba, data):
        img[lba * SEC:lba * SEC + len(data)] = data

    put(PVD_LBA, pvd(re.sub(r"[^A-Z0-9_]", "_", title.upper())))
    put(TERM_LBA, bytes([255]) + b"CD001" + bytes([1]))

    # Path table: one entry for the root, little- then big-endian.
    put(LPATH_LBA, bytes([1, 0]) + struct.pack("<I", ROOT_LBA) + struct.pack("<H", 1) + b"\x00\x00")
    put(MPATH_LBA, bytes([1, 0]) + struct.pack(">I", ROOT_LBA) + struct.pack(">H", 1) + b"\x00\x00")

    root = dirrec(b"\x00", ROOT_LBA, SEC, True)
    root += dirrec(b"\x01", ROOT_LBA, SEC, True)
    root += dirrec(b"SYSTEM.CNF;1", CNF_LBA, len(cnf), False)
    put(ROOT_LBA, root)
    put(CNF_LBA, cnf)
    return bytes(img)


def looks_legacy(fname):
    """Mirror isValidIsoName's legacy test, so we never emit a trap."""
    return len(fname) >= 17 and fname[4] == "_" and fname[8] == "." and fname[11] == "."


def pretty(stem):
    stem = re.sub(r"_(JP|US|EU|PAL)$", "", stem)
    stem = re.sub(r"(?<=[a-z])(?=[A-Z0-9])|(?<=[0-9])(?=[A-Z])", " ", stem)
    return re.sub(r"[_\s]+", " ", stem).strip()


root = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser("~/Documents/PS2")
out = sys.argv[2] if len(sys.argv) > 2 else "_deploy/DVD"
os.makedirs(out, exist_ok=True)

# Re-read the discs rather than trusting any cached list.
sys.argv = ["check-art", root]
buf = io.StringIO()
mod_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "check-art.py")
ns = {"__name__": "ck"}
with contextlib.redirect_stdout(buf):
    exec(compile(open(mod_path).read(), mod_path, "exec"), ns)

made, refused = 0, []
for fn, serial in ns["found"]:
    title = pretty(os.path.splitext(fn)[0])
    name = f"{title}.iso"
    if looks_legacy(name):
        refused.append(name)
        continue
    with open(os.path.join(out, name), "wb") as f:
        f.write(build(serial, title))
    made += 1
    print(f"  {serial:<14} {name}")

print(f"\n{made} stubs, {made * SEC * NSECS // 1024} KB total, in {out}")
if refused:
    print(f"refused (would read as legacy serial-prefixed names): {', '.join(refused)}")
orphans = ns["orphan"]
if orphans:
    print(f"no stub for {', '.join(orphans)} -- art exists but no readable disc to take a title from")
print("\nThese do not boot. Do not launch one.")
