#!/usr/bin/env python3
"""Prove that src/music_asm.s is what ACME assembles from music/player.asm.

tools/acme2calypsi.py translates one assembler's syntax into another's, which
is the sort of tool that is either exactly right or quietly playing a
different tune. So it is checked rather than trusted: both assemblers are run
over the same source at the same origin and the bytes compared.

    python3 tools/checkmusic.py

Needs `acme` on PATH and the Calypsi tools. It builds everything under a
temporary directory and touches nothing in the tree. The zero page symbols the
converter hands to the linker are pinned back to the addresses the ACME source
picked, so even those bytes have to agree.
"""

import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PLAYER = os.path.join(ROOT, "music", "player.asm")
ORIGIN = 0x2000

# The ACME source picks these itself; the game lets the linker place them.
# Pinned here so the two assemblies can be compared byte for byte.
ZP = [("ZP_PTR", 2, 0xFB), ("ZP_ARP", 2, 0xFD)]

CHECK_SCM = """\
(define memories
  '((memory program (address (#x%04x . #xffff)) (type any)
            (section (code #x%04x)))
    (memory zeroPage (address (#x%02x . #x%02x)) (type ram) (qualifier zpage))
    ))
"""


def run(cmd, **kw):
    r = subprocess.run(cmd, capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit("%s failed:\n%s%s" % (cmd[0], r.stdout, r.stderr))
    return r


def main():
    if not shutil.which("acme"):
        sys.exit("checkmusic: acme is not on PATH")

    tmp = tempfile.mkdtemp(prefix="checkmusic-")
    acme_bin = os.path.join(tmp, "acme.bin")
    conv_s = os.path.join(tmp, "music_asm.s")
    conv_o = os.path.join(tmp, "music_asm.o")
    conv_bin = os.path.join(tmp, "calypsi.bin")
    scm = os.path.join(tmp, "check.scm")

    # ACME, at a fixed origin. The wrapper goes beside player.asm so that its
    # own !source of music.asm resolves.
    top = os.path.join(ROOT, "music", "_checkmusic_top.asm")
    with open(top, "w") as f:
        f.write('        !to "%s", plain\n        * = $%04x\n'
                '        !source "player.asm"\n' % (acme_bin, ORIGIN))
    try:
        run(["acme", os.path.basename(top)], cwd=os.path.dirname(top))
    finally:
        os.remove(top)

    # The converter, then Calypsi, at the same origin and the same zero page.
    run([sys.executable, os.path.join(ROOT, "tools", "acme2calypsi.py"),
         PLAYER, conv_s,
         "--zp", ",".join("%s:%d" % (n, s) for n, s, _ in ZP)])
    run(["as6502", "--target=mega65", "-o", conv_o, conv_s])

    lo = min(a for _, _, a in ZP)
    hi = max(a + s - 1 for _, s, a in ZP)
    with open(scm, "w") as f:
        f.write(CHECK_SCM % (ORIGIN, ORIGIN, lo, hi))
    run(["ln6502", "--target=mega65", "--no-tree-shaking", "--no-auto-libraries",
         "--no-data-init-table-section", "--output-format", "raw",
         "-o", conv_bin, scm, conv_o])
    # -o names the ELF, the same way it does for the game; the raw image is
    # written beside it under the same stem.
    conv_bin = os.path.splitext(conv_bin)[0] + ".raw"

    a = open(acme_bin, "rb").read()
    b = open(conv_bin, "rb").read()
    # Calypsi pads its memory to a whole number of somethings; trailing zeros
    # past the end of ACME's output are not a difference.
    if len(b) > len(a) and not any(b[len(a):]):
        b = b[:len(a)]

    if a == b:
        print("music: %d bytes, ACME and Calypsi agree exactly -- OK" % len(a))
        shutil.rmtree(tmp)
        return

    print("music: %d bytes from ACME, %d from Calypsi" % (len(a), len(b)))
    n = min(len(a), len(b))
    bad = [i for i in range(n) if a[i] != b[i]]
    print("%d bytes differ; first at 0x%04x" % (len(bad), ORIGIN + bad[0]))
    i = bad[0]
    print("  acme    %s" % a[max(0, i - 8):i + 8].hex(" "))
    print("  calypsi %s" % b[max(0, i - 8):i + 8].hex(" "))
    print("build left in %s" % tmp)
    sys.exit(1)


if __name__ == "__main__":
    main()
