#!/usr/bin/env python3
"""Translate the ACME sources in music/ into Calypsi assembler.

The SID player and its tune were written for ACME, and the rest of this
program is built by Calypsi. Rather than keep two copies of a thousand lines
of 6502 by hand, this converts one into the other -- so `music/` stays the
place a tune is written and `src/music_asm.s` is a build product that happens
to be checked in.

    python3 tools/acme2calypsi.py music/player.asm src/music_asm.s \\
            --zp ZP_PTR:2,ZP_ARP:2

The two syntaxes are close enough that most lines pass straight through. What
differs:

    !byte / !fill       ->  .byte / .space
    $ff / %1010         ->  0xff / 0b1010
    NAME = expr         ->  NAME: .equ expr
    <expr / >expr       ->  .byte0 (expr) / .byte1 (expr)   (Calypsi has no
                            < and > prefix operators at all)
    .local              ->  local$        (same scoping in both: a local label
                            lives between two ordinary ones)
    - and + labels      ->  aN$           named here rather than trusting two
                            assemblers to agree on which one a bare `+` means
                            -- and ACME allows `jmp -`, which Calypsi's own
                            sign-style labels do not
    !for v, a, b { }    ->  the body, repeated with v substituted
    !macro / +call      ->  the body, with the parameters substituted
    !if expr { !error } ->  checked here and reported, since it is a build
                            time assertion with nothing to emit

`--public NAME,...` names the symbols the linker is to see, and defaults to
the two the player has always had. `--zp NAME:SIZE` is the one thing that is
not a translation. The player picks
its two zero page pointers by hand, which a C64 program may do and a program
sharing zero page with a C compiler and a live Kernal may not: those constants
are dropped and the names defined in a zzpage bss section instead, for the
linker to place. Everything else is byte for byte what ACME assembles -- see
tools/checkmusic.py, which proves it.

Anything the converter does not recognise stops it rather than being passed
through and mis-assembled.
"""

import os
import re
import sys


class Error(Exception):
    pass


# ---------------------------------------------------------------------------
# Reading, with !source pulled in where it stands.
def read_source(path, stack=()):
    if path in stack:
        raise Error("!source loop at %s" % path)
    lines = []
    with open(path) as f:
        for n, raw in enumerate(f, 1):
            text = raw.rstrip("\n")
            m = re.match(r'\s*!source\s+"([^"]+)"', text)
            if m:
                child = os.path.join(os.path.dirname(path), m.group(1))
                lines += read_source(child, stack + (path,))
            else:
                lines.append((path, n, text))
    return lines


def split_comment(text):
    i = text.find(";")
    return (text, "") if i < 0 else (text[:i], text[i:])


def split_labelled_blocks(lines):
    """A label on the same line as a block directive, onto a line of its own.

    `freq_lo  !for oct, 0, 6 {` is one line to ACME and two things to do.
    """
    out = []
    for path, n, text in lines:
        code, comment = split_comment(text)
        m = re.match(r'^([.\w]+)(\s+)(![a-z]+.*\{)\s*$', code)
        if m:
            out.append((path, n, m.group(1)))
            out.append((path, n, m.group(2) + m.group(3) + comment))
        else:
            out.append((path, n, text))
    return out


# ---------------------------------------------------------------------------
# Blocks: a line ending in `{` opens one, a line that is only `}` closes it.
def take_block(lines, i):
    """lines[i] opens a block. Return (body, index after the closing brace)."""
    depth = 1
    body = []
    i += 1
    while i < len(lines):
        code, _ = split_comment(lines[i][2])
        stripped = code.strip()
        if stripped == "}":
            depth -= 1
            if depth == 0:
                return body, i + 1
        elif stripped.endswith("{"):
            depth += 1
        body.append(lines[i])
        i += 1
    raise Error("unterminated { block")


def substitute(text, params, args):
    for p, a in zip(params, args):
        # A macro parameter is written .name, which is also how a local label
        # is written, so match the whole token.
        text = re.sub(re.escape(p) + r'\b', a, text)
    return text


def split_operands(text):
    """Split on commas that are not inside brackets."""
    out, depth, cur = [], 0, ""
    for c in text:
        if c in "([":
            depth += 1
        elif c in ")]":
            depth -= 1
        if c == "," and depth == 0:
            out.append(cur.strip())
            cur = ""
        else:
            cur += c
    if cur.strip():
        out.append(cur.strip())
    return out


def convert(text, data=False):
    """Numbers, and the low/high byte operators."""
    text = re.sub(r'\$([0-9a-fA-F]+)', lambda m: "0x" + m.group(1), text)
    text = re.sub(r'(?<![\w$])%([01]+)', lambda m: "0b" + m.group(1), text)
    if data:
        m = re.match(r'^([<>])\s*(.+)$', text)
        if m:
            return "%s (%s)" % (".byte0" if m.group(1) == "<" else ".byte1",
                                m.group(2))
    return text


def tail(comment):
    return ("  " + comment) if comment.strip() else ""


class Converter:
    def __init__(self, zp=()):
        self.zp = dict(zp)     # name -> size, allocated by the linker instead
        self.consts = {}       # NAME -> int, for this tool's own arithmetic
        self.labels = {}       # label -> byte offset, for the !if assertions
        self.macros = {}       # name -> (params, body)
        self.locals = set()
        self.anon = {}         # top-level line index -> generated name
        self.top = []          # the top-level line list
        self.pc = 0            # bytes of data emitted; see count()
        self.out = []

    # -- expressions --------------------------------------------------------
    #
    # Only this tool's own arithmetic comes through here: !for bounds, !if
    # assertions, .space counts. Everything else is handed to Calypsi.
    def value(self, expr):
        e = expr.strip()
        e = re.sub(r'\$([0-9a-fA-F]+)', lambda m: str(int(m.group(1), 16)), e)
        e = re.sub(r'(?<![\w$])%([01]+)', lambda m: str(int(m.group(1), 2)), e)
        # `*` is the program counter. It is never a multiply in these files,
        # and this is where to notice if it ever becomes one.
        e = re.sub(r'(?<![\w)])\*(?![\w(])', str(self.pc), e)
        env = dict(self.consts)
        env.update(self.labels)
        try:
            return eval(e, {"__builtins__": {}}, env)
        except Exception as exc:
            raise Error("cannot evaluate %r: %s" % (expr, exc))

    # -- how many bytes a data directive emits ------------------------------
    #
    # Tracked only so the !if assertions in the tune ("this pattern is not 32
    # rows long") still mean something. Instructions are not counted, which is
    # harmless: every assertion compares two offsets inside one run of data,
    # so a common origin cancels out.
    def count(self, directive, operands):
        if directive == "!byte":
            self.pc += len(split_operands(operands))
        elif directive == "!fill":
            self.pc += int(self.value(operands.split(",")[0]))

    def emit(self, text):
        self.out.append(text)

    # -- the conversion -----------------------------------------------------
    def run(self, lines):
        lines = split_labelled_blocks(lines)
        self.top = lines
        self.name_anonymous(lines)
        self.collect_locals(lines)
        self.process(lines, top=True)
        self.emit_zp()
        return self.out

    def name_anonymous(self, lines):
        n = 0
        for i, (_, _, text) in enumerate(lines):
            code, _ = split_comment(text)
            if re.match(r'^[-+](\s|$)', code):
                self.anon[i] = "a%d$" % n
                n += 1

    def find_anon(self, i, direction):
        step = -1 if direction == "-" else 1
        j = i + step
        while 0 <= j < len(self.top):
            if j in self.anon:
                return self.anon[j]
            j += step
        raise Error("no `%s' label to match" % direction)

    def collect_locals(self, lines):
        for _, _, text in lines:
            code, _ = split_comment(text)
            m = re.match(r'^\.(\w+)', code)
            if m:
                self.locals.add(m.group(1))

    def emit_zp(self):
        if not self.zp:
            return
        self.emit("")
        self.emit("; Zero page, placed by the linker rather than by the tune "
                  "-- see --zp.")
        self.emit("            .section zzpage,bss")
        for name, size in self.zp.items():
            self.emit("%-15s .space  %d" % (name + ":", size))

    def process(self, lines, top=False):
        i = 0
        while i < len(lines):
            path, lineno, text = lines[i]
            code, comment = split_comment(text)
            stripped = code.strip()

            try:
                if not stripped:
                    self.emit(comment)
                    i += 1
                    continue

                m = re.match(r'^\s*!macro\s+(\w+)\s*(.*?)\s*\{$', code)
                if m:
                    params = [p.strip() for p in m.group(2).split(",")
                              if p.strip()]
                    body, i = take_block(lines, i)
                    self.macros[m.group(1)] = (params, body)
                    continue

                m = re.match(r'^\s*!for\s+(\w+)\s*,\s*([^,]+),\s*(.+?)\s*\{$',
                             code)
                if m:
                    body, i = take_block(lines, i)
                    var = m.group(1)
                    for v in range(int(self.value(m.group(2))),
                                   int(self.value(m.group(3))) + 1):
                        self.process([(p, n, re.sub(r'\b%s\b' % var, str(v), t))
                                      for p, n, t in body])
                    continue

                m = re.match(r'^\s*!if\s+(.+?)\s*\{$', code)
                if m:
                    body, i = take_block(lines, i)
                    if self.value(m.group(1)):
                        for _, _, t in body:
                            e = re.match(r'\s*!error\s+"(.*)"', t)
                            if e:
                                raise Error(e.group(1))
                        raise Error("!if fired: %s" % m.group(1))
                    continue

                m = re.match(r'^\s*\+(\w+)\s*(.*)$', code)
                if m and m.group(1) in self.macros:
                    params, body = self.macros[m.group(1)]
                    args = split_operands(m.group(2))
                    self.process([(p, n, substitute(t, params, args))
                                  for p, n, t in body])
                    i += 1
                    continue

                self.line(i if top else -1, code, comment)
            except Error as exc:
                raise Error("%s:%d: %s" % (path, lineno, exc))
            i += 1

    # -- one ordinary line --------------------------------------------------
    def line(self, index, code, comment):
        label = ""
        rest = code

        if index in self.anon:
            label = self.anon[index] + ":"
            rest = code.strip()[1:]
        elif code[:1] not in (" ", "\t", ""):
            m = re.match(r'^([.\w]+)', code)
            if not m:
                raise Error("cannot read a label from %r" % code)
            label = m.group(1)
            rest = code[m.end():].lstrip()
            if rest.startswith(":"):  # ACME's colon is optional; ours is not
                rest = rest[1:]
            if label.startswith("."):
                label = label[1:] + "$"

            eq = re.match(r'^\s*=\s*(.+)$', rest)
            if eq:  # NAME = value: a constant, not a label
                self.consts[label] = self.value(eq.group(1))
                if label in self.zp:
                    self.emit("; %s: taken out of the tune's hands by --zp"
                              % label)
                else:
                    self.emit("%-15s .equ    %s"
                              % (label + ":", convert(eq.group(1)))
                              + tail(comment))
                return

            self.labels[label] = self.pc
            label += ":"

        rest = rest.strip()
        if not rest:
            self.emit("%s%s" % (label, tail(comment)))
            return

        m = re.match(r'^(!?[\w.]+)\s*(.*)$', rest)
        if not m:
            raise Error("cannot read an instruction from %r" % rest)
        op, operands = m.group(1), m.group(2)

        if op.startswith("!"):
            if op not in ("!byte", "!fill"):
                raise Error("unsupported directive %s" % op)
            self.count(op, operands)
            operands = ", ".join(convert(o, data=True)
                                 for o in split_operands(operands))
            op = ".byte" if op == "!byte" else ".space"
        else:
            operands = self.operand(index, operands)

        self.emit(("%-15s %-7s %s" % (label, op, operands)).rstrip()
                  + tail(comment))

    def operand(self, index, operands):
        text = operands.strip()
        if text in ("-", "+"):
            return self.find_anon(index, text)
        for name in sorted(self.locals, key=len, reverse=True):
            text = re.sub(r'\.%s\b' % name, name + "$", text)
        # A zero page symbol the linker places has no value the assembler can
        # see, so it has to be told. Not inside brackets: (ptr),y is zero page
        # indirect by definition and the prefix is not allowed there.
        for name in self.zp:
            text = re.sub(r'(?<![\w:(])%s\b' % name, "zp:" + name, text)
        return convert(text)


HEADER = """\
; Generated from %s by tools/acme2calypsi.py -- do not edit.
;
; The tune and the player are written in ACME under music/; this is the same
; thing in the syntax the rest of the build speaks, and tools/checkmusic.py
; proves the two assemble to the same bytes. Change the music there and run
; the converter, not here.

            .section code,text
            .public %s
"""

DEFAULT_PUBLIC = "music_init,music_play"


def main():
    args = sys.argv[1:]
    public = DEFAULT_PUBLIC
    if "--public" in args:
        i = args.index("--public")
        public = args[i + 1]
        del args[i:i + 2]
    zp = []
    if "--zp" in args:
        i = args.index("--zp")
        for item in args[i + 1].split(","):
            name, _, size = item.partition(":")
            zp.append((name, int(size)))
        del args[i:i + 2]
    if len(args) != 2:
        sys.exit(__doc__.strip().splitlines()[0])

    src, dst = args
    try:
        conv = Converter(zp)
        body = conv.run(read_source(src))
    except Error as exc:
        sys.exit("acme2calypsi: %s" % exc)

    with open(dst, "w") as f:
        f.write(HEADER % (src, ", ".join(n.strip()
                                         for n in public.split(","))))
        for line in body:
            f.write(line.rstrip() + "\n")
    print("%s -> %s: %d lines, %d bytes of tune data"
          % (src, dst, len(body), conv.pc))


if __name__ == "__main__":
    main()
