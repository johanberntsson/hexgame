/* Implementation of the Hex game, using an AI
   agent based on Monte Carlo simulation (mcs). The
   game state is checked with a Breath-First search (bfs)

   Written by Johan Berntsson, 10-20 January 2022, and 19-26 August 2026.

   The game uses the VIC-IV full color character mode
   on the MEGA65, where each graphic tile has an
   unique memory area for pixel data, to enable 
   merging and mixing tiles copied from the "tiles" asset,
   converted to fci format from png pictures with transparency.
   Merging tiles with transparency allows mixing irregular
   shaped tiles, such as the hexagons used in the game board.
*/

#include <fcio.h>
#include <calypsi/intrinsics6502.h>

#include "hexboard.h"
#include "hexgame_ai.h"
#include "input.h"

extern unsigned int loadExt(char *filename, himemPtr addr, byte skipCBMAddressBytes); // from fcio.c

// **This program opens no files, and there is no disk for it to open one on.**
// It used to boot off a D81 as autoboot.c65 and read HEXGAME.FCI, MUSIC.PRG,
// MARBA.WAV and DOWNLEAD.WAV off it -- and the names had to be upper case,
// because a CBM directory holds PETSCII and Calypsi passes string literals
// through as written. All four are gone. The tune and the sound effects are
// assembled into the program (music/, via tools/acme2calypsi.py), and the tile
// sheet arrives in attic RAM before main() runs: hexgame.prg carries it
// compressed and src/stage1.c unpacks it. See tools/mkprg.py.

// Where stage 1 leaves the tile sheet -- the same bytes res/hexgame.fci holds,
// at an address instead of in a file. **The Makefile passes this same number
// to tools/mkprg.py** and the two have to agree; attic RAM is 8 MB and this is
// a megabyte clear of the $8000000 that fcio hands out bitmaps from.
#define FCI_SOURCE 0x8100000l

// music/player.asm and music/sfx.asm, via build/music_asm.s
void music_init(byte song);
void sfx_start(byte effect);
// the effect numbers are music/sfx.asm's own, and have to agree with it
#define SFX_CLICK 0     // a stone going down
#define SFX_BUZZ  1     // that hexagon is taken
// src/music_irq.s
void music_install(void);

#define TEXT_DELAY 6

// The SID: the tune and the sound effects both, since they are one player and
// one interrupt. F1 switches the tune off at run time; the effects are the
// game telling the player what just happened and are not part of that.
#define ENABLE_MUSIC

// global options. option_difficulty belongs to the AI and lives in
// hexgame_ai.c; the values it takes are in hexgame_ai.h.
byte option_music;
#define OPTION_MUSIC_ON  0
#define OPTION_MUSIC_OFF 1
char option_music_text[][4] = { "ON ", "OFF" };
char option_difficulty_text[][7] = { "EASY  ", "NORMAL", "HARD  " };

// offsets on the title screen
#define TITLE_TEXT_Y 22
#define TITLE_BOARD_X 25
#define TITLE_BOARD_Y 26
#define PROGRESS_TEXT_X 65
#define PROGRESS_TEXT_Y 4

// empty line used to hide previous texts
char *empty40 = "                                        ";


// Graphic assets
fciInfo *tiles;

// Grafic configuration
fcioConf myConfig = {
    0x12000l,   // location of 16 bit screen (2*80*50 = $1f40)
    0x14000l,   // reserved bitmap graphics graphics
    0x15000l,   // reserved system palette
    0x15300l,   // loaded palettes base
    //0x16000l,   // loaded bitmaps base
    0x8000000l, // loaded bitmaps base
    0xff81000l, // attribute/colour ram
};

// **This used to be the one place that touched the disk, and the ordering rule
// that went with it is gone.** The rule was that enter_tile_mode() flattens
// the memory map and leaves a C64 KERNAL this program never initialised, so
// nothing could read a file after it -- and everything had to be loaded here,
// first. There are no files now, so all that is left is setting the screen up
// and pointing fcio at the tile sheet stage 1 has already put in attic RAM.
void load_resources() {
    fc_init(1, 1, &myConfig, 0, 47);

    fc_textcolor(FC_COLOR_WHITE);
    fc_putsxy(0, 0, "loading...");

    tiles = fc_loadFCI(FCI_SOURCE, 0, 0);
    fc_loadFCIPalette(tiles);

}

// **The ROM is put somewhere safe before the game eats it.** enter_tile_mode()
// takes $20000-$5FFFF for per-character tile data and fc_clearUniqueTiles()
// wipes it -- and $20000-$3FFFF is the 128 KB ROM image the machine booted
// from. By the time the title screen is up, C64 BASIC at $2A000 and most of
// the C65 ROM around it are zeroes, so there is nothing left to quit *into*:
// the KERNAL's reset entry runs and lands in wiped memory.
//
// It cannot be worked around by clearing less. A cell's tile address is fixed
// by where the cell is on the screen -- see fc_displayTile -- and the board
// alone reaches past $2A000, so BASIC's 8 KB cannot simply be stepped over the
// way the DOS and the KERNAL are. And a program cannot ask the hypervisor to
// load the ROM again either: the traps a write can reach are $D640-$D67F, and
// reset is not one of them.
//
// So the 128 KB is copied to attic RAM first, where there are megabytes going
// spare, and copied back on the way out. Four DMA jobs each way, because
// lcopy counts in 16 bits.
#define ROM_IMAGE  0x20000l     // the ROM the machine booted from
#define ROM_SAVE   0x8200000l   // clear of the tile sheet and the FCI source
#define ROM_CHUNKS 4
#define ROM_CHUNK  32768

static void save_rom(void) {
    byte i;
    for(i = 0; i < ROM_CHUNKS; i++)
        lcopy(ROM_IMAGE + (long)ROM_CHUNK * i,
              ROM_SAVE + (long)ROM_CHUNK * i, ROM_CHUNK);
}

// **Quitting means resetting the machine**, because the game has been running
// on top of the ROM rather than under it: the way out is to put the ROM back
// and then take the KERNAL's reset entry, which brings the machine up again
// from scratch. fc_bank_out_rom() asked the Hypervisor to lift the write
// protection from this area at startup and nothing has put it back, which is
// what makes the copy below possible at all.
//
// **And putting it back is this function's job, because that request is a
// toggle and it outlives the program.** Nothing in a KERNAL reset restores it,
// so a run that quits with the protection still lifted leaves the next run's
// fc_bank_out_rom() turning it back *on*: the tile writes into $20000-$3FFFF
// are then dropped, and the cells whose tile data lives there -- most of the
// logo, and a band across the board -- draw whatever ROM bytes are underneath.
// The machine boots protected, so every run has to leave it protected too.
//
// The VIC-IV is put back by hand first. A reset writes the VIC-II registers,
// and with the hot registers enabled those recompute most of the VIC-IV side
// -- but only most. `~/commodore/ozmoo-z6` found on a **real MEGA65** that
// CHARPTR, the row stride and the character count survive a reset, so a screen
// left in 16 bit full colour mode comes back up in the game's font at twice
// the row stride and BASIC is unreadable. **xemu puts them back itself, so a
// clean screen there is not evidence**: the list below is that project's
// `leave_fcm_mode`, which is confirmed on the machine, plus the colour RAM
// offset this game moves and the two sprites it owns.
#define KERNAL_RESET 0xe4b8     // the C65 KERNAL's reset entry

// mega65-libc-modified/src/fcio_asm.s, the second half of fc_bank_out_rom
void fc_toggle_rom_write_protect(void);

void quit_to_basic(void) {
    byte i;

    // The tune is about to stop existing along with the interrupt that drives
    // it, so take the SID down rather than leave a note ringing over the boot.
    // 25 registers, not the 24 the F1 handler writes: the master volume is the
    // one that matters here.
    __disable_interrupts();
    for(i = 0; i < 25; i++) POKE(0xd400u + i, 0);

    // The ROM, back where the reset below expects to find it. The write
    // protection goes back on after the copy, not before it -- it stops DMA
    // as surely as it stops a store.
    for(i = 0; i < ROM_CHUNKS; i++)
        lcopy(ROM_SAVE + (long)ROM_CHUNK * i,
              ROM_IMAGE + (long)ROM_CHUNK * i, ROM_CHUNK);
    fc_toggle_rom_write_protect();

    mega65_io_enable();
    POKE(0xd05du, PEEK(0xd05du) | 0x80);  // hot registers on, so the reset's
    POKE(0xd031u, 0xe0);                  // own VIC-II writes recompute the
    POKE(0xd016u, 0xc9);                  // VIC-IV side from these two

    mega65_io_enable();
    POKE(0xd054u, 0x40);        // no 16 bit characters and no full colour
    POKE(0xd015u, 0);           // the arrow and its outline off
    POKE(0xd064u, 0);           // colour RAM offset, back where BASIC has it
    POKE(0xd065u, 0);
    POKE(0xd058u, 80);          // row stride: one byte a cell, not two
    POKE(0xd059u, 0);
    POKE(0xd05eu, 80);          // cells a row
    POKE(0xd068u, 0x00);        // CHARPTR $001000, or every glyph BASIC
    POKE(0xd069u, 0x10);        // prints comes out of the game's tile data
    POKE(0xd06au, 0x00);
    POKE(0xd06cu, 0xf8);        // SPRPTRADR $000ff8, and 8 bit pointers --
    POKE(0xd06du, 0x0f);        // input.c moved the list and made it 16 bit
    POKE(0xd06eu, 0x00);

    ((void (*)(void))KERNAL_RESET)();
    for(i = 0;; i++);           // not reached
}

// Hand the machine over to the game: $20000-$5FFFF becomes writable RAM for
// the per-character tile data, and the interrupt becomes ours.
//
// Interrupts stay off across the whole of it. fc_setUniqueTileMode() leaves
// the C65 ROM unmapped while $0314 still points into it, so an interrupt taken
// between the two calls lands in whatever is at that address now.
void enter_tile_mode() {
    __disable_interrupts();

    // Before anything is allowed to overwrite it -- see quit_to_basic().
    save_rom();

    // this makes $20000 - $5ffff for character data (so tiles can be modified)
    fc_setUniqueTileMode();

#ifdef ENABLE_MUSIC
    // Rewind the tune and then let the interrupt at it. This used to happen up
    // in load_resources(), because that was where the tune was read off the
    // disk; there is nothing to read now, and the player's state -- including
    // the two zero page pointers the linker places for it -- is better set up
    // here, past the last KERNAL call, than left to survive one.
    music_init(0);
    music_install();
#endif

    __enable_interrupts();
}


void draw_board(byte x0, byte y0) {
    byte x, y, xx, yy;
    for(y = 0; y < board.size; y++) {
        for(x = 0; x < board.size; x++) {
            if(board.redraw[x][y]) {
                board.redraw[x][y] = false;
                xx = x0 + y*3+x*6;
                yy = y0 + y*5;
                fc_displayTile(tiles, xx, yy, 0, 0, 6, 7, 1); // hexagon
                if(board.tile[x][y] & HEX_WHITE) 
                    fc_displayTile(tiles, xx, yy, 6, 0, 6, 7, 1);
                if(board.tile[x][y] & HEX_BLACK)
                    fc_displayTile(tiles, xx, yy, 12, 0, 6, 7, 1);
                // The fourth tile in the sheet is the old drawn cursor. It is
                // sprite 0 now (see player_turn), so nothing sets HEX_CURSOR
                // and nothing draws it.
            }
        }
    }
}

void show_win_screen() {
    byte i;
    fc_revers(true);
    fc_textcolor(FC_COLOR_WHITE);
    for(i = 0; i < 8; i++) {
        fc_center(0, 20+i, 80, empty40);
    }
    if(board.side == WHITE_PLAYER) {
        fc_center(0, 22, 80, "What?!?! How did you win???");
        fc_center(0, 23, 80, "That was a fluke! Let me try again!");
    } else {
        fc_center(0, 22, 80, "I win! Robots > Mankind!!!");
    }
    fc_center(0, 25, 80, "Press any key");
    fc_revers(false);
    POKE(0xD610U, 0);         // drop the key or click that ended the game
    while(input_poll() == 0); // the mouse button counts as a key here too
}

void update_options(byte *key) {
#ifdef ENABLE_MUSIC
    byte i;
#endif

    // check if ESC, F1 etc for global options
    if(*key == KEY_F1) {
        if(option_music == OPTION_MUSIC_ON) {
            option_music = OPTION_MUSIC_OFF;
#ifdef ENABLE_MUSIC
            for(i = 0; i < 24; i++) {
                POKE(0xd400+i, 0); // reset SID
            }
#endif
        } else {
            option_music = OPTION_MUSIC_ON;
#ifdef ENABLE_MUSIC
            music_init(0); // reinit song
#endif
        }
        *key = 0;
    }
    if(*key == KEY_F2) {
        ++option_difficulty;
        if(option_difficulty > OPTION_DIFFICULTY_HARD)
            option_difficulty = OPTION_DIFFICULTY_EASY;
        *key = 0;
    }
}

// **The cursor is a hardware sprite, not a hexagon.** It used to be a fourth
// tile drawn over the board (HEX_CURSOR), which meant it could only ever be
// on a hexagon, that two hexagons had to be redrawn every time it moved, and
// that the arrow jumped a whole hexagon at a time -- no use at all for a
// mouse. Sprite 0 goes wherever the mouse puts it, and snaps to the middle of
// a hexagon when a key or the joystick moves it. See src/input.c.
byte player_turn() {
    // add a stone, return true if this was a winning move
    byte key;
    byte kx, ky;       // where the cursor was before a key moved it
    byte px = board.white_last_x;
    byte py = board.white_last_y;

    // The arrow starts on the hexagon the player left it on rather than
    // wherever the last turn's last movement happened to end.
    input_set_cell(px, py);
    input_show_pointer(true);

    key = 0;
    while(key != KEY_ENTER) {
        // The loop no longer blocks on a key: the mouse has to be read even
        // while nothing is being pressed, or the arrow would only move when
        // the player also happened to touch the keyboard.
        key = input_poll();
        input_moved(&px, &py); // the mouse may have picked a hexagon of its own
        if(key) update_options(&key); // check if a global option command
        if(key == KEY_SPACE) key = KEY_ENTER;
        kx = px;
        ky = py;
        switch(key) {
            case KEY_ESC:
            case KEY_RUNSTOP:
                input_show_pointer(false);
                return ABORT;
            case KEY_ENTER:
                // only allowed if this hexagon is empty
                if(board.tile[px][py] != HEX_EMPTY) {
#ifdef ENABLE_MUSIC
                    sfx_start(SFX_BUZZ);
#endif
                    key = 0;
                }
                break;
            case KEY_LEFT:
                if(px > 0) --px;
                break;
            case KEY_RIGHT:
                if(px < board.size_minus_1) ++px;
                break;
            case KEY_UP:
                if(py > 0) --py;
                break;
            case KEY_DOWN:
                if(py < board.size_minus_1) ++py;
        }
        // A key or a joystick step moved the cursor, so snap the arrow onto
        // the hexagon it landed on -- and take the mouse pointer with it, or
        // the next twitch of the mouse would throw the cursor back to
        // wherever the pointer had been left.
        if(px != kx || py != ky) input_set_cell(px, py);
    }
    input_show_pointer(false);

    // put a white stone here
    board.tile[px][py] = HEX_WHITE;
    board.redraw[px][py] = true;
    draw_board(BOARD_X0, BOARD_Y0);

#ifdef ENABLE_MUSIC
    sfx_start(SFX_CLICK);
#endif

    board.white_last_x = px;
    board.white_last_y = py;
    return check_win(px, py);
}

void show_progress_bar() {
    fc_textcolor(FC_COLOR_WHITE);
    fc_putsxy(65,2, "Thinking...");
    fc_textcolor(FC_COLOR_GREEN);
    fc_revers(true);
    fc_putsxy(PROGRESS_TEXT_X, PROGRESS_TEXT_Y, "          ");
    fc_revers(false);
}

void set_progress_bar(byte position) {
    byte x;
    fc_gotoxy(PROGRESS_TEXT_X, PROGRESS_TEXT_Y);
    fc_textcolor(FC_COLOR_RED);
    fc_revers(true);
    for(x = 0; x < position; x++) fc_puts(" ");
    fc_revers(false);
}

void hide_progress_bar() {
    fc_putsxy(65,2, "             ");
    fc_revers(false);
    fc_putsxy(PROGRESS_TEXT_X, PROGRESS_TEXT_Y, "             ");
}

// The AI calls this from inside its search so that a key pressed during a long
// think is acted on rather than dropped. See hexgame_ai.h.
void ai_poll_input(void) {
    // Keyboard only: a click while the computer is thinking is not a move,
    // and reading the mouse from inside the search would only put a pointer
    // somewhere the player cannot see it.
    byte key = PEEK(0xD610U);
    if(key) {
        POKE(0xD610U, 0);
        update_options(&key);
    }
}

void show_options() {
    fc_textcolor(FC_COLOR_WHITE);
    fc_putsxy(32, 49, option_music_text[option_music]);
    fc_putsxy(57, 49, option_difficulty_text[option_difficulty]);
    fc_textcolor(FC_COLOR_GREEN);
}

byte  delay(byte sec) {
    // usleep etc from mega65_stdlib don't seem to work in the emulator
    byte n, i, j,  c;
    byte raster_temp;
    for(n = 0; n < sec; n++) {
        for(i = 0; i < 100; i++) {
            for(j = 0; j < 200; j++) {
                // wait for the next raster line
                raster_temp=PEEK(0xD052);
                while(PEEK(0xD052)==raster_temp) continue;
            }
            c = input_poll();
            if(c) {
                update_options(&c);
                show_options();
                // **ESC on the title screen leaves the game.** In a turn it
                // means "give up and go back to the title", which is why it
                // was the one key this loop threw away; here there is nothing
                // left to back out to, so it quits.
                if(c == KEY_ESC || c == KEY_RUNSTOP) quit_to_basic();
                if(c) return c;
            }
        }
    }
    return 0;
}

byte show_title_text(char *text, byte timeout) {
    fc_putsxy(0, TITLE_TEXT_Y, empty40);
    fc_putsxy(40, TITLE_TEXT_Y, empty40);
    fc_center(0, TITLE_TEXT_Y, 80, text);
    return delay(timeout);
}

void add_white_stone(byte x, byte y) {
    board.tile[x][y] = HEX_WHITE;
    board.redraw[x][y] = true;
}

void add_black_stone(byte x, byte y) {
    board.tile[x][y] = HEX_BLACK;
    board.redraw[x][y] = true;
}

void show_title_screen() {
    fc_clrscr();

    fc_bgcolor(FC_COLOR_BLACK);
    fc_bordercolor(FC_COLOR_BLACK);
    fc_textcolor(FC_COLOR_GREEN);
    fc_displayTile(tiles, 19, 0, 0, 8, 40, 17, 0);
    fc_textcolor(FC_COLOR_YELLOW);
    fc_center(0, 19, 80, "Release 2, 2026 by Johan Berntsson");
    fc_textcolor(FC_COLOR_GREEN);
    fc_putsxy(20, 49, "Music (F1): ");
    fc_putsxy(40, 49, "Difficulty (F2): ");

    for(;;) {
        init_game(4);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);

        show_options();

        add_white_stone(0,0);
        add_black_stone(2,3);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(show_title_text("In this game two players place stones on the board", TEXT_DELAY)) return;

        if(show_title_text("White tries to connect the left and right edges", 0)) return;
        add_white_stone(1,0);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;
        add_white_stone(1,1);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;
        add_white_stone(2,1);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;
        add_white_stone(3,0);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(TEXT_DELAY)) return;

        if(show_title_text("And black tries to connect the top and bottom edges", 0)) return;

        add_black_stone(3,2);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;
        add_black_stone(3,1);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(TEXT_DELAY)) return;

        if(show_title_text("But you can only add stones on empty tiles", TEXT_DELAY)) return;

        if(show_title_text("You play white, the computer is black", TEXT_DELAY)) return;

        if(show_title_text("Pick an empty tile with the mouse, a joystick or the keys", TEXT_DELAY)) return;

        if(show_title_text("and place your stone with the button, fire or enter", TEXT_DELAY)) return;

        if(show_title_text("Select difficulty level with F2", TEXT_DELAY)) return;

        // if(show_title_text("But even HARD isn't that difficult. Sorry", TEXT_DELAY)) return;

        if(show_title_text("Now press any key to start", TEXT_DELAY*2)) return;
    }
}

void show_game_screen() {
    fc_clrscr();
    fc_bgcolor(FC_COLOR_GREY1);
    fc_bordercolor(FC_COLOR_BLACK);
    init_game(MAX_SIZE);
    draw_board(BOARD_X0, BOARD_Y0);
}

int main(void) {
    word turn;
    byte game_state;

    option_music = OPTION_MUSIC_ON;
    option_difficulty = OPTION_DIFFICULTY_NORMAL;

    load_resources();
    enter_tile_mode();

    // clears the keyboard buffer and seeds the mouse counters
    input_init();

    for(;;) {
        show_title_screen();

        show_game_screen();

        turn = 0;
        board.white_last_x = 0;
        board.white_last_y = 0;
        game_state = NOWINNER;
        while(game_state == NOWINNER) {
            ++turn;
            board.side = (turn % 2);
            if(board.side == WHITE_PLAYER)
                game_state = player_turn();
            else
                game_state = computer_turn(turn/2);
        }
        if(game_state != ABORT) show_win_screen();
    }
}


