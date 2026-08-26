#!/usr/bin/env python3
"""Pack hexgame.prg: the boot stub, two exomizer streams, and stage 1.

The game used to be two files on a D81 -- autoboot.c65 and hexgame.fci -- and
it is one PRG now. Uncompressed the pair is 94 KB, which does not fit in a
6502's address space at all; compressed they are about 39 KB, which does, with
room to unpack from.

    hexgame.prg
      $2001  boot stub          src/boot.s: a BASIC line and the twenty bytes
                                that bank the ROM out
      $2027  stream 0           the tile sheet, for attic RAM
             stream 1           the game, for $2001 -- over the top of all of
                                the above, which is why stage 1 lives high
             padding
      $B800  stage 1            src/stage1.c, and the table below patched into
                                the front of it

Nothing here is clever about the streams: exomizer does the compressing and
src/stage1.c does the decompressing. What this file does is decide where
everything goes, write that down in stage 1's table, and then **check the two
things that would otherwise fail silently**:

  - that each stream decrunches back to exactly the bytes that went in. There
    is a decruncher below for the purpose -- the same algorithm as exomizer's
    own `rawdecrs/exodecrunch.c`, which is what stage 1 is a copy of.
  - that unpacking the game over the top of the streams is safe. The game is
    written to $2001 upwards while it is being read from further up the same
    memory, and if the write pointer ever caught the read pointer the tail of
    the file would decrunch from bytes it had already destroyed. The check
    below is exact rather than a rule of thumb: it walks the stream and
    compares the two pointers at every byte.

The decruncher is a transliteration of exomizer's:

  Copyright (c) 2002 - 2018 Magnus Lind. Provided 'as-is', without any express
  or implied warranty; permission is granted to anyone to use it for any
  purpose and to alter and redistribute it freely, provided the origin is not
  misrepresented, altered versions are marked as such, and this notice is not
  removed. This is an altered version.
"""

import argparse
import os
import subprocess
import sys
import tempfile

BASE = 0x2001          # where a C65 BASIC program starts, and so the PRG


class Error(Exception):
    pass


# ---------------------------------------------------------------------------
# exomizer, and the decruncher that checks it.
def crunch(data, max_offset):
    """One raw exomizer stream, no load address and no decruncher in front."""
    with tempfile.TemporaryDirectory(prefix="mkprg-") as tmp:
        src = os.path.join(tmp, "in.bin")
        dst = os.path.join(tmp, "out.exo")
        with open(src, "wb") as f:
            f.write(data)
        r = subprocess.run(["exomizer", "raw", "-q", "-m", str(max_offset),
                            "-o", dst, src], capture_output=True, text=True)
        if r.returncode:
            raise Error("exomizer: %s%s" % (r.stdout, r.stderr))
        with open(dst, "rb") as f:
            return f.read()


class Decruncher:
    """exomizer's streaming decruncher, a byte at a time.

    Yields (byte, bytes of input consumed so far), which is what makes the
    overlap check possible.
    """

    def __init__(self, stream, window_size):
        self.s = stream
        self.pos = 0
        self.window = bytearray(window_size)
        self.wsize = window_size
        self.wpos = 0
        self.bitbuf = self.byte()
        self.lengths = self.table(16)
        self.offsets3 = self.table(16)
        self.offsets2 = self.table(16)
        self.offsets1 = self.table(4)

    def byte(self):
        b = self.s[self.pos]
        self.pos += 1
        return b

    def rotate(self, carry):
        out = (self.bitbuf & 0x80) != 0
        self.bitbuf = ((self.bitbuf << 1) | (1 if carry else 0)) & 0xff
        return out

    def bits(self, count):
        byte_copy = count & 8
        v = 0
        for _ in range(count & 7):
            carry = self.rotate(0)
            if self.bitbuf == 0:
                self.bitbuf = self.byte()
                carry = self.rotate(1)
            v = (v << 1) | (1 if carry else 0)
        if byte_copy:
            v = (v << 8) | self.byte()
        return v

    def table(self, size):
        out = []
        base = 1
        for _ in range(size):
            bits = self.bits(3) | (self.bits(1) << 3)
            out.append((bits, base))
            base += 1 << bits
        return out

    def gamma(self):
        n = 0
        while self.bits(1) == 0:
            n += 1
        return n

    def emit(self, c):
        self.window[self.wpos] = c
        self.wpos = (self.wpos + 1) % self.wsize
        return c, self.pos

    def __iter__(self):
        yield self.emit(self.byte())           # implicit first literal
        while True:
            if self.bits(1) == 1:
                yield self.emit(self.byte())
                continue
            index = self.gamma()
            if index == 17:                    # a run of literal bytes
                n = (self.byte() << 8) | self.byte()
                for _ in range(n):
                    yield self.emit(self.byte())
                continue
            if index == 16:                    # end of stream
                return
            bits, base = self.lengths[index]
            length = base + self.bits(bits)
            if length == 1:
                bits, base = self.offsets1[self.bits(2)]
            elif length == 2:
                bits, base = self.offsets2[self.bits(4)]
            else:
                bits, base = self.offsets3[self.bits(4)]
            offset = base + self.bits(bits)
            for _ in range(length):
                yield self.emit(self.window[(self.wpos - offset) % self.wsize])


def check(name, original, stream, window, dest=None, src=None):
    """Decrunch it back, and if it lands in its own source, prove that it can."""
    out = bytearray()
    worst = None
    for n, (c, consumed) in enumerate(Decruncher(stream, window)):
        out.append(c)
        if dest is not None:
            # The write pointer must never pass the read pointer, or the rest
            # of the stream is decrunched out of bytes it has overwritten.
            slack = (src + consumed) - (dest + n + 1)
            if worst is None or slack < worst:
                worst = slack
    if bytes(out) != original:
        raise Error("%s: %d bytes in, %d out, and they differ"
                    % (name, len(original), len(out)))
    if worst is not None and worst < 0:
        raise Error("%s: unpacking it over its own stream overruns by %d bytes"
                    % (name, -worst))
    return worst


# ---------------------------------------------------------------------------
def sys_address(prg):
    """The address the game's own BASIC line SYSes to."""
    i = prg.find(0x9e, 2, 20)           # the SYS token
    if i < 0:
        raise Error("no SYS in the game's BASIC stub")
    i += 1
    while prg[i] == 0x20:
        i += 1
    digits = ""
    while prg[i:i + 1].isdigit():
        digits += chr(prg[i])
        i += 1
    if not digits:
        raise Error("no address after the SYS")
    return int(digits)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--boot", required=True)
    p.add_argument("--stage1", required=True)
    p.add_argument("--stage1-addr", required=True, type=lambda s: int(s, 0))
    p.add_argument("--game", required=True)
    p.add_argument("--fci", required=True)
    p.add_argument("--fci-addr", required=True, type=lambda s: int(s, 0))
    p.add_argument("--window", type=lambda s: int(s, 0), default=4096)
    p.add_argument("-o", "--output", required=True)
    a = p.parse_args()

    boot = open(a.boot, "rb").read()
    stage1 = bytearray(open(a.stage1, "rb").read())
    game = open(a.game, "rb").read()
    fci = open(a.fci, "rb").read()

    load = game[0] | (game[1] << 8)
    if load != BASE:
        raise Error("the game loads at $%04x, not $%04x" % (load, BASE))
    entry = sys_address(game)
    game = game[2:]

    # The tile sheet first and the game last, because the game is unpacked over
    # the streams: whatever is read last has to be the thing highest in memory.
    fci_exo = crunch(fci, a.window - 1)
    game_exo = crunch(game, a.window - 1)

    at = BASE + len(boot)
    fci_src, at = at, at + len(fci_exo)
    game_src, at = at, at + len(game_exo)
    if at > a.stage1_addr:
        raise Error("the streams reach $%04x and stage 1 is at $%04x: "
                    "move stage 1 up (stage1.scm and the Makefile) or make "
                    "something smaller" % (at, a.stage1_addr))

    check("tile sheet", fci, fci_exo, a.window)
    slack = check("game", game, game_exo, a.window, dest=BASE, src=game_src)

    # Stage 1's table: two eight byte stream descriptors and the entry point.
    # The layout is written out in src/stage1.c as well, which reads it.
    def descriptor(src, dest, length):
        return bytes([src & 0xff, src >> 8,
                      dest & 0xff, (dest >> 8) & 0xff,
                      (dest >> 16) & 0xff, (dest >> 24) & 0xff,
                      length & 0xff, length >> 8])

    table = (descriptor(fci_src, a.fci_addr, len(fci))
             + descriptor(game_src, BASE, len(game))
             + bytes([entry & 0xff, entry >> 8]))
    if len(table) > 32:
        raise Error("the table outgrew the 32 bytes src/stage1_tab.s reserves")
    stage1[:len(table)] = table

    pad = a.stage1_addr - at
    out = (bytes([BASE & 0xff, BASE >> 8]) + boot + fci_exo + game_exo
           + bytes(pad) + bytes(stage1))
    with open(a.output, "wb") as f:
        f.write(out)

    print("%s: %d bytes" % (a.output, len(out)))
    print("  $%04x boot     %6d" % (BASE, len(boot)))
    print("  $%04x tiles    %6d  <- %d, to $%07x"
          % (fci_src, len(fci_exo), len(fci), a.fci_addr))
    print("  $%04x game     %6d  <- %d, to $%04x, entry $%04x, %d bytes of "
          "slack unpacking it over itself"
          % (game_src, len(game_exo), len(game), BASE, entry, slack))
    print("  $%04x stage 1  %6d  after %d bytes of padding"
          % (a.stage1_addr, len(stage1), pad))


if __name__ == "__main__":
    try:
        main()
    except Error as exc:
        sys.exit("mkprg: %s" % exc)
