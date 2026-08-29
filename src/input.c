/* Keyboard, mouse and joystick, and the arrow that all three move. See
   src/input.h for the interface and for where the MEGA65 hardware details
   came from.

   The pointer is kept as "which hexagon, and how far into it", not as a
   position on the screen. The mouse pushes the offset about a pixel at a
   time and the hexagon changes when the offset crosses half a hexagon; the
   cursor keys and the joystick move the hexagon and put the offset back to
   zero, which is what makes them snap to the middle of a stone's place while
   the mouse does not. Everything the arrow needs to be drawn falls out of
   those four bytes.

   **Nothing in here reads a file or touches the memory map**, which matters:
   it all runs after enter_tile_mode() in src/hexgame.c has flattened the map,
   and the KERNAL left mapped then cannot open anything.
*/

#include <calypsi/intrinsics6502.h>
#include <memory.h>

#include "input.h"
#include "hexboard.h"

// The MEGA65's own keyboard buffer: one key, ASCII, cleared by writing 0.
// This is the machine's hardware scan of the real keyboard, not the KERNAL's,
// which is why a held joystick is not expected to arrive here as phantom
// keypresses the way it does in the C64 matrix the KERNAL scans -- the trap
// Ozmoo fell into. **That is an expectation, not a measurement**: it has not
// been tried on a real MEGA65 with a joystick plugged in.
#define KEYBOARD        0xD610U

// The 1351's counters. These are the VIC-IV's own pot registers, read
// directly -- no SID/CIA paddle multiplexing and no timing loop, which is the
// one part of reading a mouse on a C64 that is awkward. **Which physical
// control port this pair belongs to is not the same in xemu as it is on the
// machine** (Ozmoo found the mouse on port 1 on real hardware and on port 2
// under xemu, reading these same two registers), so the button is taken from
// both ports below rather than from the one that ought to be right.
#define POTX            0xD620U
#define POTY            0xD621U

// The joystick, and the fire buttons. $DC01 is control port 1, $DC00 is port
// 2 -- and also the keyboard column select, which is the whole reason
// read_ports() below exists. Every bit is active low: 0 up, 1 down, 2 left,
// 3 right, 4 fire.
#define JOY_PORT1       0xDC01U
#define JOY_PORT2       0xDC00U
#define JOY_COLUMNS     0xDC00U
#define JOY_UP          0x01
#define JOY_DOWN        0x02
#define JOY_LEFT        0x04
#define JOY_RIGHT       0x08
#define JOY_FIRE        0x10
#define JOY_DIRS        0x0f

// Sprite 0 is the pointer. **The pointer list has to be moved.** The VIC-IV
// reads the classic sprite pointers from the screen base plus $3F8, and this
// screen is 16 bit full colour characters at $12000, so that address is a
// character on the board rather than a spare byte. SPRPTRADR is the VIC-IV's
// own 24 bit address for the list, and its top byte's bit 7 asks for 16 bit
// pointers, which is what makes it work at all: a pointer is then the sprite
// data's address divided by 64, anywhere in the machine.
#define SPR_ENABLE      0xD015U
#define SPR0_X          0xD000U
#define SPR0_Y          0xD001U
#define SPR_XMSB        0xD010U
#define SPR_YEXPAND     0xD017U
#define SPR_PRIORITY    0xD01BU // 0 = the sprite is drawn in front
#define SPR_MULTICOLOUR 0xD01CU
#define SPR_XEXPAND     0xD01DU
#define SPR0_COLOUR     0xD027U // sprite 1's is the byte after it
#define SPRPTRADR       0xD06CU

// The C64 KERNAL's jiffy clock, low byte. The music interrupt chains through
// $EA31, which updates it fifty times a second, so it is the cheapest clock
// this program has -- and unlike the raster it does not need a busy wait.
#define JIFFY           0x00A2U

// A joystick is all or nothing, so it needs a rate limit or the cursor
// crosses the board before the player has let go. One hexagon every six
// jiffies is about eight a second, which is roughly how fast the key repeat
// moves it.
#define JOY_JIFFIES     6

// Ignore the button for this long after a click. A button that bounces
// chatters for a few milliseconds, and this loop polls far faster than that,
// so the settling of one press would otherwise read as press-release-press.
// Six jiffies is a tenth of a second: slower than any bounce, faster than
// anyone clicks twice on purpose, and it also thins out an autofire joystick.
#define CLICK_DEBOUNCE  6

// Where draw_board() puts hexagon (x, y): the character cell
// (x0 + 3*y + 6*x, y0 + 5*y), with the tile 6 characters wide and 7 tall.
// There are 8 pixels to the character on the 640x400 screen fc_init sets up,
// so one hexagon to the right is 48 pixels right, and one row down is 40
// pixels down and 24 right -- the rows interlock, which is what makes this a
// hexagonal grid and not a chequerboard.
#define CELL_W          48      // pixels between two hexagons in a row
#define CELL_H          40      // pixels between two rows
#define CELL_SKEW       24      // pixels each row is shifted right of the last

// ...so hexagon (x, y)'s centre is at the screen pixel
//     (ORIGIN_PX + CELL_W*x + CELL_SKEW*y,  ORIGIN_PY + CELL_H*y).
#define ORIGIN_PX       (BOARD_X0 * 8 + 24)
#define ORIGIN_PY       (BOARD_Y0 * 8 + 28)

// A sprite coordinate is not a screen pixel. The sprite hardware keeps the
// 320x200 coordinate space it has always had, so on this 640x400 screen one
// sprite unit is two pixels each way, and (24, 50) is the top left corner of
// the display window. **The vertical half is the part to distrust if the
// pointer ever sits at the wrong height**: it is what V400 does to the sprite
// coordinate space, and it was settled by putting the arrow on a known
// hexagon and looking at a screenshot, not from a manual.
#define SPR_X_OFFSET    24
#define SPR_Y_OFFSET    50

static byte cell_x, cell_y;     // the hexagon the pointer is standing on
static signed char off_x, off_y; // and where in it, in pixels from its centre
static byte cell_changed;       // set when the mouse moved it to another one
static byte pointer_shown;      // whether sprite 0 is on

// The arrow. **Two sprites, not one.** A sprite pixel is not square here: the
// screen is H640 and V400, so one sprite unit is two screen pixels each way
// and a monochrome sprite comes out 48x42 with square pixels -- but a
// multicolour one is four pixels wide and two tall, which turns every
// diagonal into a shallow ramp and made the first attempt look like a flag.
// So the arrow is monochrome, and its outline is a second monochrome sprite
// of the same shape grown by a pixel, sitting behind it: sprite 0 is the
// body, sprite 1 the outline, and a lower numbered sprite is in front. That
// buys the black edge that makes the arrow readable over a white stone, a
// black stone and the honeycomb alike, without a multicolour pixel.
//
// The tip of the body is at sprite pixel (1, 1), because the outline needs a
// pixel of room above and to the left of it; see place_pointer().
#define ARROW_HOT       1       // sprite pixels from the corner to the tip

static const byte arrow_body[63] = {
    0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x60, 0x00, 0x00,
    0x70, 0x00, 0x00, 0x78, 0x00, 0x00, 0x7c, 0x00, 0x00,
    0x7e, 0x00, 0x00, 0x7f, 0x00, 0x00, 0x7f, 0x80, 0x00,
    0x7f, 0xc0, 0x00, 0x7f, 0xe0, 0x00, 0x7f, 0xf0, 0x00,
    0x7f, 0x00, 0x00, 0x7f, 0x80, 0x00, 0x67, 0x80, 0x00,
    0x43, 0xc0, 0x00, 0x01, 0xe0, 0x00, 0x00, 0xe0, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static const byte arrow_outline[63] = {
    0xe0, 0x00, 0x00, 0xf0, 0x00, 0x00, 0xf8, 0x00, 0x00,
    0xfc, 0x00, 0x00, 0xfe, 0x00, 0x00, 0xff, 0x00, 0x00,
    0xff, 0x80, 0x00, 0xff, 0xc0, 0x00, 0xff, 0xe0, 0x00,
    0xff, 0xf0, 0x00, 0xff, 0xf8, 0x00, 0xff, 0xf8, 0x00,
    0xff, 0xf8, 0x00, 0xff, 0xc0, 0x00, 0xff, 0xe0, 0x00,
    0xff, 0xf0, 0x00, 0xe7, 0xf0, 0x00, 0x03, 0xf0, 0x00,
    0x01, 0xf0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

// **Everything the sprite hardware is pointed at has to be 64 byte aligned**,
// and there is no way to ask C for that, so this is one buffer with a spare
// block in it and the three things that need alignment are placed inside:
//
//      +0     the pointer list SPRPTRADR is given
//      +64    the arrow
//      +128   its outline
//
// A 16 bit sprite pointer is an address divided by 64, which is the obvious
// half of that: a shape that does not start on a boundary has no pointer that
// names it. **The list itself is the half that is easy to miss.** The VIC-IV
// does not use the low bits of the address in SPRPTRADR either: pointed at a
// list at $1703 it reads one at $1700, and the two bytes it finds there are
// not a sprite pointer but whatever the variable before it happens to hold.
//
// This worked for months by accident, because the linker happened to put the
// list on a boundary. Taking the file loading out of fcio.c in 2026 moved
// every one of these variables a few bytes, the list landed at $1703, the two
// bytes at $1700 were zero -- and sprite 0 dutifully drew the 63 bytes at
// $0000, which is zero page, which looks exactly like the noise it looked
// like. Nothing about the arrow had changed.
static byte sprite_ram[192 + 63];

static byte prev_potx, prev_poty;
static byte button_held;        // the button as it was at the last poll
static byte click_timer;        // jiffies left of the debounce window
static byte click_jiffy;        // the jiffy that window last counted down on
static byte joy_jiffy;          // the jiffy the last joystick step was taken on
static byte joy_port2;          // port 2's lines, filled in by read_ports()

// **The joystick is the only input the machine does not hold on to for us**,
// and that is why it alone appears to stop working while the game is busy.
// A key waits in $D610 until something reads it, and the mouse's counters go
// on counting in hardware, so both survive a stretch with no input_poll() in
// it -- the arrow simply jumps to where the mouse now is. A joystick is
// sampled and nothing more: a direction held while the board is being drawn
// or while the computer is thinking is never seen at all.
//
// Those stretches were seconds long. A full 81 hexagon board took just over a
// second to draw before fc_displayTile learnt to do a row per DMA job, and
// normal's Monte Carlo search takes about 1.2 seconds whenever it is reached,
// which is roughly one of its turns in three. **Both were reported as "the
// joystick takes one or two seconds to start reacting"**, from the title
// screen and from the middle of a game.
//
// So the busy loops call input_scan(), which samples the stick and remembers
// one step for the next input_poll() to hand over. One, not a queue: the point
// is that a push is not lost, not that the cursor should run across the board
// while the player cannot see it.
static byte joy_latch;          // a direction input_scan() picked up, or 0

// --------------------------------------------------------------- hardware

// Read the joystick and both fire buttons, and return control port 1's lines.
//
// $DC00 is the keyboard column select as well as control port 2, so a read of
// either port can be a key rather than a device: with a column driven low, a
// held key on it looks like a direction on port 1, and bit 4 of $DC00 itself
// reads low whenever the KERNAL's scan happens to have column 4 selected,
// which is indistinguishable from the button being down. So the columns are
// deselected across the read, with interrupts off so the scan cannot select
// one back in between.
//
// **But only when there is something to disambiguate.** Writing $FF to $DC00
// also puts the SID's paddle select into its "both ports" state, a few
// instructions from the pot reads above -- harmless on the machine, where the
// pot registers are sampled independently of the CIA, but not on an emulator
// that takes the C64 path for them. A false direction or a false button can
// only come from a line pulled *low*, so both ports are read raw first and
// the guarded read only happens when a bit we care about is low. Moving the
// mouse with no joystick held and no key down -- ordinary play -- never
// touches $DC00 at all.
static byte read_ports(void) {
    byte p1, save;

    p1 = PEEK(JOY_PORT1);
    if((p1 & (JOY_DIRS | JOY_FIRE)) == (JOY_DIRS | JOY_FIRE) &&
       (PEEK(JOY_PORT2) & JOY_FIRE) != 0) {
        joy_port2 = 0xff;       // both ports idle
        return 0xff;
    }

    __disable_interrupts();
    save = PEEK(JOY_COLUMNS);
    POKE(JOY_COLUMNS, 0xff);
    joy_port2 = PEEK(JOY_PORT2);
    p1 = PEEK(JOY_PORT1);
    POKE(JOY_COLUMNS, save);
    __enable_interrupts();
    return p1;
}

// One step from the joystick, or 0 if it is centred or the rate limit has not
// run out. **The limit lives here and nowhere else**, so a push cannot be
// taken twice -- once by input_scan() into the latch and again by the
// input_poll() that follows it.
static byte joy_step(byte joy, byte now) {
    if((joy & JOY_DIRS) == JOY_DIRS) return 0;
    if((byte)(now - joy_jiffy) < JOY_JIFFIES) return 0;
    joy_jiffy = now;
    if(!(joy & JOY_LEFT)) return KEY_LEFT;
    if(!(joy & JOY_RIGHT)) return KEY_RIGHT;
    if(!(joy & JOY_UP)) return KEY_UP;
    if(!(joy & JOY_DOWN)) return KEY_DOWN;
    return 0;
}

// The pot is a six bit counter that wraps, so what a reading means is the
// signed change since the last one. Fold cur - prev into -32..31.
static signed char pot_delta(byte cur, byte prev) {
    return (signed char)((byte)((byte)(cur - prev + 0x20) & 0x3f) - 0x20);
}

static byte read_pot(unsigned int reg) {
    return (byte)((PEEK(reg) >> 1) & 0x3f);
}

// --------------------------------------------------------------- geometry

// Move the pointer, and with it the hexagon it stands on.
//
// The pointer is not kept as a position on the screen but as an offset from
// the centre of the hexagon it is on, which is why none of this needs a
// multiplication, a division, or a single 16 bit value: a step to the next
// hexagon is a subtraction, and the offset never leaves a byte. **The program
// area is within a few hundred bytes of full** (see build/hexgame.lst), and
// the same thing written in screen pixels cost 1.3 KB.
//
// Rounding to a row and then to a hexagon within it makes the region that
// selects a hexagon a rectangle rather than the hexagon itself, so the four
// corners of that rectangle belong to the neighbour they touch. The corners
// are where nobody aims, and the alternative is a nearest-centre search over
// three rows with the arithmetic that implies.
static void move_pointer(signed char dx, signed char dy) {
    byte was_x = cell_x, was_y = cell_y;

    off_y += dy;
    if(off_y > CELL_H / 2) {
        if(cell_y < board.size_minus_1) {
            ++cell_y;
            off_y -= CELL_H;
            off_x -= CELL_SKEW;         // the row below sits to the right
        } else {
            off_y = CELL_H / 2;
        }
    } else if(off_y < -(CELL_H / 2)) {
        if(cell_y > 0) {
            --cell_y;
            off_y += CELL_H;
            off_x += CELL_SKEW;
        } else {
            off_y = -(CELL_H / 2);
        }
    }

    off_x += dx;
    if(off_x > CELL_W / 2) {
        if(cell_x < board.size_minus_1) {
            ++cell_x;
            off_x -= CELL_W;
        } else {
            off_x = CELL_W / 2;
        }
    } else if(off_x < -(CELL_W / 2)) {
        if(cell_x > 0) {
            --cell_x;
            off_x += CELL_W;
        } else {
            off_x = -(CELL_W / 2);
        }
    }

    if(cell_x != was_x || cell_y != was_y) cell_changed = true;
}

// ---------------------------------------------------------------- pointer

// Put the arrow where the pointer is. The hexagon's centre plus the offset
// within it is the pointer's screen pixel; halving that is the sprite
// coordinate, less the pixel of outline in front of the tip, and the ninth
// bit of X lives in a bit of its own.
static void place_pointer(void) {
    word px = ORIGIN_PX + (word)CELL_W * cell_x + (word)CELL_SKEW * cell_y;
    word py = ORIGIN_PY + (word)CELL_H * cell_y;
    byte y;

    px = (word)((int)px + off_x) / 2 + SPR_X_OFFSET - ARROW_HOT;
    py = (word)((int)py + off_y) / 2 + SPR_Y_OFFSET - ARROW_HOT;
    y = (byte)py;

    POKE(SPR0_X, (byte)px);
    POKE(SPR0_Y, y);
    POKE(SPR0_X + 2, (byte)px);          // sprite 1 rides on top of sprite 0
    POKE(SPR0_Y + 2, y);
    POKE(SPR_XMSB, (PEEK(SPR_XMSB) & 0xfc) | (px >> 8 ? 3 : 0));
}

static void pointer_init(void) {
    word list = ((word)sprite_ram + 63) & 0xffc0;
    word body = list + 64;
    word ptr = body / 64;
    byte i;

    for(i = 0; i < 63; i++) {
        POKE(body + i, arrow_body[i]);
        POKE(body + 64 + i, arrow_outline[i]);
    }
    // Every sprite points at the arrow; sprite 1, the outline, at the block
    // after it. The six the game never turns on are pointed somewhere valid
    // rather than left as whatever was in the buffer.
    for(i = 0; i < 8; i++) {
        POKE(list + 2 * i, (byte)ptr);
        POKE(list + 2 * i + 1, (byte)(ptr >> 8));
    }
    POKE(list + 2, (byte)(ptr + 1));
    POKE(list + 3, (byte)((ptr + 1) >> 8));

    POKE(SPRPTRADR, (byte)list);
    POKE(SPRPTRADR + 1, (byte)(list >> 8));
    POKE(SPRPTRADR + 2, 0x80);           // bank 0, and 16 bit pointers

    POKE(SPR0_COLOUR, FC_COLOR_RED);
    POKE(SPR0_COLOUR + 1, FC_COLOR_BLACK);
    POKE(SPR_MULTICOLOUR, PEEK(SPR_MULTICOLOUR) & 0xfc);
    POKE(SPR_PRIORITY, PEEK(SPR_PRIORITY) & 0xfc);   // in front of the board
    POKE(SPR_XEXPAND, PEEK(SPR_XEXPAND) & 0xfc);
    POKE(SPR_YEXPAND, PEEK(SPR_YEXPAND) & 0xfc);
    POKE(SPR_ENABLE, PEEK(SPR_ENABLE) & 0xfc); // off until a turn asks for it
}

void input_show_pointer(byte on) {
    pointer_shown = on;
    if(on) place_pointer();
    POKE(SPR_ENABLE, on ? (PEEK(SPR_ENABLE) | 3) : (PEEK(SPR_ENABLE) & 0xfc));
}

// ------------------------------------------------------------------- api

void input_init(void) {
    mega65_io_enable();
    // Seed the counters with what they read now, so the first poll sees no
    // movement rather than whatever the difference from zero happens to be.
    prev_potx = read_pot(POTX);
    prev_poty = read_pot(POTY);
    button_held = false;
    click_timer = 0;
    joy_latch = 0;
    pointer_init();
    input_set_cell(0, 0);
    POKE(KEYBOARD, 0);
}

void input_set_cell(byte x, byte y) {
    // Snapping to the middle of the hexagon is what makes the cursor keys and
    // the joystick feel like they are moving from stone to stone rather than
    // dragging the arrow about.
    cell_x = x;
    cell_y = y;
    off_x = 0;
    off_y = 0;
    cell_changed = false;
    if(pointer_shown) place_pointer();
}

byte input_moved(byte *x, byte *y) {
    if(!cell_changed) return false;
    cell_changed = false;
    *x = cell_x;
    *y = cell_y;
    return true;
}

byte input_poll(void) {
    byte pot, joy, now, key, pressed;
    signed char dx, dy;

    // --- the keyboard first: it is what most of the game is played with ---
    key = PEEK(KEYBOARD);
    if(key) {
        POKE(KEYBOARD, 0);
        return key;
    }

    // --- the mouse: difference the pots and push the pointer ---
    pot = read_pot(POTX);
    dx = pot_delta(pot, prev_potx);
    prev_potx = pot;
    pot = read_pot(POTY);
    dy = pot_delta(pot, prev_poty);
    prev_poty = pot;
    if(dx || dy) {
        move_pointer(dx, -dy);                  // screen y runs the other way
        if(pointer_shown) place_pointer();
    }

    joy = read_ports();
    now = PEEK(JIFFY);

    // --- the debounce window, counted down a jiffy at a time. Counting down
    // rather than differencing the clock against the click keeps the jiffy
    // byte's wrap out of it: a stale reading can only cost a step. ---
    if(click_timer && now != click_jiffy) {
        click_jiffy = now;
        --click_timer;
    }

    // --- either fire button, on either port, is the mouse button ---
    pressed = ((joy & JOY_FIRE) == 0 || (joy_port2 & JOY_FIRE) == 0);
    if(pressed) {
        if(!button_held) {
            button_held = true;
            if(!click_timer) {
                click_timer = CLICK_DEBOUNCE;
                click_jiffy = now;
                return KEY_ENTER;               // a press edge is a click
            }
        }
    } else {
        button_held = false;
    }

    // --- a joystick direction is a cursor key, one hexagon per step ---
    key = joy_step(joy, now);

    // ...or one input_scan() took while the game was busy elsewhere. The
    // latch is dropped either way: if joy_step() has just given a step, the
    // two are the same push, and a direction older than that is not worth
    // acting on any more.
    if(key == 0) key = joy_latch;
    joy_latch = 0;
    return key;
}

// Sample the joystick from a loop that is doing something else. Only the
// joystick: reading the pots here would move the arrow while the player
// cannot see it, and the counters need no help -- and a fire button is
// deliberately not latched either, because a click while the computer is
// thinking is not a move. See ai_poll_input() in src/hexgame.c.
void input_scan(void) {
    byte step = joy_step(read_ports(), PEEK(JIFFY));
    if(step) joy_latch = step;
}

