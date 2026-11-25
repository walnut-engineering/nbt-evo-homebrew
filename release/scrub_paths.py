#!/usr/bin/env python3
"""scrub_paths.py <package dir> <prefix>...  - take build-machine paths out of the binaries.

The unit's compiler (gcc 4.4.2) has no -ffile-prefix-map, so a source that uses
__FILE__ leaves the absolute path of the build directory in the binary. For every
ELF file under <package dir>, each C string that starts with one of the given
prefixes is shifted left to drop the prefix and the freed bytes are zero-filled:
the file's size and every offset stay the same, and the string reads as a relative
path.

Afterwards every file is searched again; if a prefix is still found anywhere the
script fails, so a release cannot go out naming the machine it was built on. One
string is allowed: the path QNX's own build server left inside the toolchain's
libgcc, which says nothing about this build.
"""
import os, sys

ALLOWED = (b"/hudson/650-gcc-4.4/",)

def scrub(data, prefix):
    n, i = 0, 0
    while True:
        j = data.find(prefix, i)
        if j < 0: return n
        k = data.find(b"\x00", j)
        if k < 0: k = len(data)
        s = bytes(data[j:k])
        if any(a in s for a in ALLOWED):
            i = k; continue
        new = s[len(prefix):]
        data[j:k] = new + b"\x00" * (len(s) - len(new))
        n += 1; i = k

def main():
    pkg = sys.argv[1]
    # longest first, so the repository path goes before the home directory it sits in
    prefixes = sorted({(p.rstrip("/") + "/").encode() for p in sys.argv[2:]}, key=len, reverse=True)
    left = []
    for root, _, files in os.walk(pkg):
        for name in sorted(files):
            path = os.path.join(root, name)
            data = bytearray(open(path, "rb").read())
            if data[:4] == b"\x7fELF":
                size, n = len(data), sum(scrub(data, p) for p in prefixes)
                assert len(data) == size
                if n:
                    open(path, "wb").write(data)
                    print("   %s: removed the build path from %d string(s)" % (os.path.relpath(path, pkg), n))
            for p in prefixes:
                i = data.find(p)
                while i >= 0:
                    k = data.find(b"\x00", i); k = len(data) if k < 0 else k
                    if not any(a in bytes(data[i:k]) for a in ALLOWED):
                        left.append("%s: %s" % (os.path.relpath(path, pkg), bytes(data[i:min(k, i + 120)]).decode("latin-1")))
                    i = data.find(p, k)
    if left:
        print("FATAL: build-machine paths are still in the package:")
        for l in left: print("   " + l)
        sys.exit(1)
    print("   no build-machine paths in the package")

main()
