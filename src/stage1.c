// Stage 1: the thing that runs first, unpacks everything, and is then thrown
// away.
//
// **There is no disk any more.** The game used to be an autoboot.c65 and a
// hexgame.fci on a D81; it is one hexgame.prg now, and this is what makes that
// possible. The file holds two exomizer streams -- the tile sheet and the game
// itself -- and about 40 KB of compressed data is the only way both of them
// fit in a 64 KB address space at once. See tools/mkprg.py for the layout and
// CLAUDE.md for why it is shaped like this.
//
// This program lives at $B800, above everything it writes, so it survives
// unpacking the game over the top of the streams and can jump into it. Its two
// working buffers are the free RAM below the program area, which nothing else
// wants until the game is running.
//
// The decruncher is exomizer's own, from `rawdecrs/exodecrunch.c` in the
// exomizer distribution, with the malloc and the context struct taken out --
// there is one of these and it runs once. It is the *streaming* decruncher,
// which is the whole reason this is simple: it hands back one byte at a time
// out of a bounded back-reference window, so a 64000 byte tile sheet can go
// straight up to attic RAM a bufferful at a time and never has to exist in the
// 64 KB map at all. The block decruncher would have needed the whole thing
// somewhere first, and there is nowhere.
//
//   Copyright (c) 2002 - 2018 Magnus Lind, zlib licence. This is an altered
//   version of that software: see the notice in tools/mkprg.py.

#include <memory.h>

typedef unsigned char byte;
typedef unsigned int word;

// **The window is 4096 bytes and the streams are crunched with -m 4095.** The
// two have to agree: a back reference further than the window has been
// overwritten. It costs about 2.8% over exomizer's unbounded 64 KB window
// (38992 bytes against 37934) and it buys a window that fits below the program
// with room for the output buffer beside it.
#define WINDOW   ((byte *)0x0800)
#define WINSIZE  4096
#define OUTBUF   ((byte *)0x1800)
#define OUTSIZE  1024

// Filled in by tools/mkprg.py, which patches the assembled bytes -- see
// src/stage1_tab.s. Two streams of eight bytes each and then the game's entry
// point; read a byte at a time rather than through a struct, because the one
// thing both sides of this have to agree about is the layout, and this way it
// is written down in both of them.
//
//      +0  src   2 bytes   where the crunched bytes are in the 64 KB map
//      +2  dest  4 bytes   28 bit destination for the decrunched ones
//      +6  len   2 bytes   how many of them there are
//
#define NSTREAMS  2
#define STREAM_SZ 8
extern byte boot_table[];

// ---------------------------------------------------------------- decruncher

struct entry {
    byte bits;
    word base;
};

static struct entry lengths[16];
static struct entry offsets1[4];
static struct entry offsets2[16];
static struct entry offsets3[16];

static const byte *crunched;    // the next crunched byte to be read
static byte bitbuf;
static word window_pos;

static word seq_len;
static word seq_off;
static byte state;

#define STATE_FIRST_LITERAL 0
#define STATE_NEXT_BYTE     1
#define STATE_LITERAL_RUN   2
#define STATE_SEQUENCE      3

static byte get_crunched_byte(void)
{
    return *crunched++;
}

// A rol through the bit buffer: the top bit falls out and `carry` goes in at
// the bottom. An empty buffer means the next crunched byte, with a sentinel
// bit under it -- which is how the encoding says "this byte is spent".
static byte rotate(byte carry)
{
    byte out = (bitbuf & 0x80) != 0;
    bitbuf <<= 1;
    if (carry)
        bitbuf |= 1;
    return out;
}

static word read_bits(byte bit_count)
{
    byte byte_copy = bit_count & 8;
    word bits = 0;

    bit_count &= 7;
    while (bit_count--) {
        byte carry = rotate(0);
        if (bitbuf == 0) {
            bitbuf = get_crunched_byte();
            carry = rotate(1);
        }
        bits <<= 1;
        bits |= carry;
    }
    if (byte_copy) {
        bits <<= 8;
        bits |= get_crunched_byte();
    }
    return bits;
}

// The tables are at the front of the stream, four bits of width per entry, and
// the bases follow from them. `base` is deliberately 32 bit here: the widest
// entry is 15 bits, and 1 << 15 overflows the 16 bit int this compiler has.
static void generate_table(struct entry *table, byte size)
{
    unsigned long base = 1;
    byte i;

    for (i = 0; i < size; ++i) {
        table[i].base = (word)base;
        table[i].bits = (byte)read_bits(3);
        table[i].bits |= (byte)read_bits(1) << 3;
        base += 1UL << table[i].bits;
    }
}

static void decrunch_begin(word src)
{
    crunched = (const byte *)src;
    state = STATE_FIRST_LITERAL;
    window_pos = 0;
    bitbuf = get_crunched_byte();

    generate_table(lengths, 16);
    generate_table(offsets3, 16);
    generate_table(offsets2, 16);
    generate_table(offsets1, 4);
}

// Unary: count the zero bits before the next one bit.
static byte get_gamma_code(void)
{
    byte gamma = 0;
    while (read_bits(1) == 0)
        ++gamma;
    return gamma;
}

static byte read_from_window(word offset)
{
    word pos = window_pos - offset;
    if (pos >= WINSIZE)         // wrapped past zero
        pos += WINSIZE;
    return WINDOW[pos];
}

static byte decrunch_byte(void)
{
    byte c = 0;
    byte length_index;
    struct entry *e;

    for (;;) {
        if (state == STATE_FIRST_LITERAL) {
            c = get_crunched_byte();
            state = STATE_NEXT_BYTE;
            break;
        }
        if (state == STATE_LITERAL_RUN) {
            if (--seq_len == 0)
                state = STATE_NEXT_BYTE;
            c = get_crunched_byte();
            break;
        }
        if (state == STATE_SEQUENCE) {
            if (--seq_len == 0)
                state = STATE_NEXT_BYTE;
            c = read_from_window(seq_off);
            break;
        }

        // STATE_NEXT_BYTE
        if (read_bits(1) == 1) {
            c = get_crunched_byte();
            break;
        }
        length_index = get_gamma_code();
        if (length_index == 17) {   // a run of literal bytes, length first
            seq_len = (word)get_crunched_byte() << 8;
            seq_len |= get_crunched_byte();
            state = STATE_LITERAL_RUN;
            continue;
        }
        if (length_index == 16)     // end of stream; the caller counts the
            return 0;               // bytes out, so this is never reached

        e = lengths + length_index;
        seq_len = e->base + read_bits(e->bits);
        if (seq_len == 1)
            e = offsets1 + read_bits(2);
        else if (seq_len == 2)
            e = offsets2 + read_bits(4);
        else
            e = offsets3 + read_bits(4);
        seq_off = e->base + read_bits(e->bits);
        state = STATE_SEQUENCE;
    }

    WINDOW[window_pos++] = c;
    if (window_pos == WINSIZE)
        window_pos = 0;
    return c;
}

// ---------------------------------------------------------------- the work

// One stream, decrunched a bufferful at a time and DMAd where it belongs. The
// destination is 28 bit, so this is the same call whether the bytes are going
// to attic RAM or back into the 64 KB map.
static void unpack(byte *t)
{
    unsigned long dest;
    word left;
    word n;

    // Byte at a time, low first, because that is how a 6502 stores a long and
    // because shifting a 32 bit value by 24 costs a runtime call to do it.
    ((byte *)&dest)[0] = t[2];
    ((byte *)&dest)[1] = t[3];
    ((byte *)&dest)[2] = t[4];
    ((byte *)&dest)[3] = t[5];
    left = (word)t[6] | ((word)t[7] << 8);

    decrunch_begin((word)t[0] | ((word)t[1] << 8));
    while (left) {
        n = 0;
        while (n < OUTSIZE && n < left)
            OUTBUF[n++] = decrunch_byte();
        lcopy((long)OUTBUF, dest, n);
        dest += n;
        left -= n;
    }
}

int main(void)
{
    byte i;

    for (i = 0; i < NSTREAMS; ++i)
        unpack(boot_table + i * STREAM_SZ);

    // And into the game, which is now at $2001 where its own linker script
    // says it is. Nothing here is needed again; the buffers below are about to
    // become the game's BSS and its screen.
    ((void (*)(void))((word)boot_table[NSTREAMS * STREAM_SZ]
                    | ((word)boot_table[NSTREAMS * STREAM_SZ + 1] << 8)))();
    return 0;
}
