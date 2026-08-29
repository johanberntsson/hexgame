/* Keyboard, mouse and joystick: everything the player can hold, and the arrow
   sprite the three of them move.

   All three drive the same cursor. The mouse moves the arrow a pixel at a
   time and the hexagon under its tip is the one that gets the stone; the
   cursor keys and the joystick move it a whole hexagon at a time, snapped to
   the middle. The mouse button, a joystick's fire and Enter are the same
   thing, and every one of them comes back out of input_poll() as a key code,
   so the game has one place to read input from.

   The MEGA65 hardware details here (which registers a 1351 and a joystick are
   read through, and why the read has to be guarded) come from Johan's Ozmoo
   z6 branch, ~/commodore/ozmoo-z6/asm/mouse.asm, which is the version of this
   that has been tested on a real machine. Its CLAUDE.md has the long form.
*/

#ifndef INPUT_H
#define INPUT_H

#include <fcio.h>

// Where player_turn() draws the board, in character cells. The pointer works
// out where a hexagon is on the screen from these, so the call to draw_board()
// and the arrow have to agree.
#define BOARD_X0 1
#define BOARD_Y0 1

// Key codes as the MEGA65's own keyboard buffer at $D610 delivers them: ASCII
// for the printable keys, PETSCII's control codes for the rest.
#define KEY_F1 241
#define KEY_F2 242
#define KEY_F3 243
#define KEY_UP 145
#define KEY_ESC 27
#define KEY_DOWN 17
#define KEY_LEFT 157
#define KEY_RIGHT 29
#define KEY_ENTER 13
#define KEY_SPACE 32
#define KEY_RUNSTOP 3

// Call once, after the screen mode is set up.
void input_init(void);

// Which hexagon is selected right now. Call this whenever a key or a joystick
// step moves the cursor, so that the pointer goes with it -- otherwise the
// next twitch of the mouse throws the cursor back to wherever the pointer had
// been left.
void input_set_cell(byte x, byte y);

// Show or hide the arrow. It is the player's cursor, so it belongs on screen
// only while the player is choosing where to put a stone.
void input_show_pointer(byte on);

// One key's worth of input from whichever device produced it: the keyboard,
// a joystick direction, or the mouse button and the fire button (both of
// which come out as KEY_ENTER). 0 when there is nothing. Non-blocking, and
// meant to be called in a tight loop.
byte input_poll(void);

// Sample the joystick and remember one step, for the next input_poll() to
// hand over. **Call this from any loop that is going to be busy for longer
// than a frame or two**, or a direction held over it is lost: a key waits in
// $D610 and the mouse's counters go on counting, but a joystick is level
// sampled and nothing in the machine holds it. src/hexgame.c calls it from
// draw_board() and from ai_poll_input(); see the note in src/input.c.
void input_scan(void);

// True if the mouse has moved the pointer onto a different hexagon since the
// last time this was asked, in which case *x and *y are that hexagon. The
// caller is expected to move its cursor there.
byte input_moved(byte *x, byte *y);

#endif /* INPUT_H */
