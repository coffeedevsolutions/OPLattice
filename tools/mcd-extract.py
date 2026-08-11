#!/usr/bin/env python3
"""List or extract files from a PS2 memory card image (.mcd).

    tools/mcd-extract.py <card.mcd>                  # list everything
    tools/mcd-extract.py <card.mcd> <path> <out>     # extract one file

Useful for pulling the current OPNPS2LD.ELF off a boot channel before
overwriting it, so a rollback is one file rather than restoring a whole 8 MB
card image over everything else on that channel.

The card is a cluster-allocated filesystem with a two-level FAT. The superblock
gives the geometry; ifc_list points at indirect-FAT clusters, each holding
pointers to FAT clusters, each holding the actual chain entries. Directory
entries are 512 bytes and directories are themselves ordinary cluster chains.

Reads only. Nothing here writes to the image.
"""
import os
import struct
import sys

ATTR_SUBDIR = 0x0020
ATTR_EXISTS = 0x8000


class Card:
    def __init__(self, path):
        self.f = open(path, "rb")
        sb = self.f.read(0x160)
        if not sb.startswith(b"Sony PS2 Memory Card Format"):
            raise SystemExit(f"{path}: not a PS2 memory card image")
        (self.page_len, self.pages_per_cluster, self.pages_per_block,
         _unused) = struct.unpack("<HHHH", sb[0x28:0x30])
        (self.clusters_per_card, self.alloc_offset, self.alloc_end,
         self.rootdir_cluster) = struct.unpack("<IIII", sb[0x30:0x40])
        self.ifc_list = struct.unpack("<32I", sb[0x50:0xD0])
        self.cluster_len = self.page_len * self.pages_per_cluster

        # Some dumps carry 16 spare ECC bytes per page and some do not; the
        # difference shows up as a size that is not a whole number of clusters.
        size = os.path.getsize(path)
        raw = self.clusters_per_card * self.cluster_len
        self.spare = 0
        if size != raw:
            per_page = size // (self.clusters_per_card * self.pages_per_cluster)
            if per_page > self.page_len:
                self.spare = per_page - self.page_len

    def read_cluster(self, n):
        out = bytearray()
        for p in range(self.pages_per_cluster):
            idx = n * self.pages_per_cluster + p
            self.f.seek(idx * (self.page_len + self.spare))
            out += self.f.read(self.page_len)
        return bytes(out)

    def fat(self, cluster):
        """Next cluster in the chain, or None at the end."""
        per = self.cluster_len // 4
        ifc_i, rem = divmod(cluster, per * per)
        fat_i, ent_i = divmod(rem, per)
        indirect = self.read_cluster(self.ifc_list[ifc_i])
        fat_cluster = struct.unpack_from("<I", indirect, fat_i * 4)[0]
        entry = struct.unpack_from("<I", self.read_cluster(fat_cluster), ent_i * 4)[0]
        if not entry & 0x80000000:
            return None
        nxt = entry & 0x7FFFFFFF
        return None if nxt == 0x7FFFFFFF else nxt

    def chain(self, start):
        c, seen = start, set()
        while c is not None and c not in seen:
            seen.add(c)
            yield c
            c = self.fat(c)

    def read_file(self, start, length):
        out = bytearray()
        for c in self.chain(start):
            out += self.read_cluster(self.alloc_offset + c)
            if len(out) >= length:
                break
        return bytes(out[:length])

    def entries(self, start, count):
        """Exactly `count` records from a directory chain.

        A directory's length field is its number of entries, not a byte size.
        Reading to the end of the cluster instead walks off into stale records
        that still have ATTR_EXISTS set and produces garbage names.
        """
        seen = 0
        for c in self.chain(start):
            data = self.read_cluster(self.alloc_offset + c)
            for off in range(0, len(data), 512):
                if seen >= count:
                    return
                rec = data[off:off + 512]
                if len(rec) < 512:
                    return
                seen += 1
                mode, = struct.unpack_from("<H", rec, 0)
                length, = struct.unpack_from("<I", rec, 4)
                cluster, = struct.unpack_from("<I", rec, 16)
                name = rec[0x40:0x60].split(b"\x00")[0].decode("latin-1")
                if not (mode & ATTR_EXISTS) or name in (".", ".."):
                    continue
                yield name, mode, length, cluster

    def root_count(self):
        """The root's own "." record carries its entry count."""
        rec = self.read_cluster(self.alloc_offset + self.rootdir_cluster)[:512]
        return struct.unpack_from("<I", rec, 4)[0]

    def walk(self, cluster=None, count=None, prefix=""):
        if cluster is None:
            cluster, count = self.rootdir_cluster, self.root_count()
        for name, mode, length, start in self.entries(cluster, count):
            path = f"{prefix}/{name}"
            if mode & ATTR_SUBDIR:
                yield path, True, 0, start
                yield from self.walk(start, length, path)
            else:
                yield path, False, length, start


if len(sys.argv) < 2:
    raise SystemExit(__doc__)

card = Card(sys.argv[1])

if len(sys.argv) == 2:
    for path, isdir, length, _ in card.walk():
        print(f"  {'dir ' if isdir else f'{length:>9}'}  {path}")
    sys.exit(0)

want, out = sys.argv[2], sys.argv[3]
for path, isdir, length, start in card.walk():
    if not isdir and path.lower() == want.lower():
        open(out, "wb").write(card.read_file(start, length))
        print(f"extracted {path}  ({length} bytes) -> {out}")
        sys.exit(0)
raise SystemExit(f"not found: {want}")
