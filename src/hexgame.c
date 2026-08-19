/* Implementation of the Hex game, using an AI
   agent based on Monte Carlo simulation (mcs). The
   game state is checked with a Breath-First search (bfs)

   Written by Johan Berntsson, 10-20 January 2022.

   The game uses the VIC-IV full color character mode
   on the MEGA65, where each graphic tile has an
   unique memory area for pixel data, to enable 
   merging and mixing tiles copied from the "tiles" asset,
   converted to fci format from png pictures with transparency.
   Merging tiles with transparency allows mixing irregular
   shaped tiles, such as the hexagons used in the game board.

    TODO:
    1. can fci be embedded or compressed?
    2. escape to reset the machine?
    3. Problems when binary > $8000???
    4. Improve the AI
*/

#include <fcio.h>
#include <calypsi/intrinsics6502.h>

#include "hexboard.h"
#include "hexgame_ai.h"
#include "input.h"

extern unsigned int loadExt(char *filename, himemPtr addr, byte skipCBMAddressBytes); // from fcio.c

// **The resource names are upper case because that is what is on the disk.**
// CBM directory entries hold PETSCII, where the letters are $41-$5A -- the
// same codes upper case ASCII uses -- and the KERNAL compares the name it is
// given byte for byte. cc65's runtime translated ASCII to PETSCII on the way
// into open(); Calypsi passes the bytes through, so a lower case name here
// matches nothing on the disk and the open quietly yields an empty file.

// The two samples live in the free chip RAM of bank 1, above the screen, the
// reserved bitmap and the palettes (see myConfig below); the audio DMA reads
// them straight out of there. The tune is the exception: the CPU has to
// execute it, so it goes in the 4 KB the linker script holds back above the
// program. See mega65-hexgame.scm and src/music_irq.s.
#define MUSIC_ADDRESS   0x9000l
#define MARBA_ADDRESS   0x16000l
#define MARBA_LENGTH    7500
#define DOWNLEAD_ADDRESS 0x18000l
#define DOWNLEAD_LENGTH 6014

// src/music_irq.s
void music_init(byte song);
void music_install(void);

#define TEXT_DELAY 6

// add a song or not?
#define ENABLE_MUSIC
#define ENABLE_SAMPLES

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


#ifdef ENABLE_SAMPLES
//#include "sample01.c"
//#include "sample02.c"

void play_sample(unsigned char ch, unsigned long sample_address, unsigned short sample_length)
{
  unsigned ch_ofs = ch << 4;
  unsigned long time_base;

  if (ch > 3) return;

  // Start of sample
  POKE(0xD721 + ch_ofs, sample_address & 0xff);
  POKE(0xD722 + ch_ofs, (sample_address >> 8) & 0xff);
  POKE(0xD723 + ch_ofs, (sample_address >> 16) & 0xff);
  POKE(0xD72A + ch_ofs, sample_address & 0xff);
  POKE(0xD72B + ch_ofs, (sample_address >> 8) & 0xff);
  POKE(0xD72C + ch_ofs, (sample_address >> 16) & 0xff);
  // pointer to end of sample
  POKE(0xD727 + ch_ofs, (sample_address + sample_length) & 0xff);
  POKE(0xD728 + ch_ofs, (sample_address + sample_length) >> 8);
  // frequency
  time_base = 0x1a00;
  POKE(0xD724 + ch_ofs, time_base & 0xff);
  POKE(0xD725 + ch_ofs, time_base >> 8);
  POKE(0xD726 + ch_ofs, time_base >> 16);
  // volume
  //POKE(0xD729 + ch_ofs, 0x3F); // 1/4 Full volume
  POKE(0xD729 + ch_ofs, 0xFF); // 4/4 Full volume
  // Enable playback of channel 0, 8-bit samples, signed
  POKE(0xD720 + ch_ofs, 0xA2);
  POKE(0xD711 + ch_ofs, 0x80);

}
#endif 

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

#ifdef ENABLE_MUSIC
void load_music() {
    // The tune is raw SID data with no player in front of it, so it goes
    // straight to the address it was relocated for. Skip the two CBM load
    // address bytes.
    loadExt("MUSIC.PRG", MUSIC_ADDRESS, 1);
    music_init(0);
}
#endif

// **Everything that touches the disk happens here, and only here.**
// enter_tile_mode() below flattens the memory map into the C64 configuration,
// and the KERNAL that survives that is the C64 one at $2E000 -- which this
// program, booted by the C65 ROM, never initialised and cannot open a file
// through. The cc65 build could load whenever it liked because it was a C64
// mode program from the first instruction; this one gets one chance, before
// the map changes.
void load_resources() {
    fc_init(1, 1, &myConfig, 0, 47, 0);

    fc_textcolor(FC_COLOR_WHITE);
    fc_putsxy(0, 0, "loading...");

    tiles = fc_loadFCI("HEXGAME.FCI", 0, 0);
    fc_loadFCIPalette(tiles);

#ifdef ENABLE_MUSIC
    load_music();
#endif
#ifdef ENABLE_SAMPLES
    loadExt("MARBA.WAV", MARBA_ADDRESS, 0);
    loadExt("DOWNLEAD.WAV", DOWNLEAD_ADDRESS, 0);
#endif
}

// Hand the machine over to the game: $20000-$5FFFF becomes writable RAM for
// the per-character tile data, and the interrupt becomes ours.
//
// Interrupts stay off across the whole of it. fc_setUniqueTileMode() leaves
// the C65 ROM unmapped while $0314 still points into it, so an interrupt taken
// between the two calls lands in whatever is at that address now.
void enter_tile_mode() {
    __disable_interrupts();

    // this makes $20000 - $5ffff for character data (so tiles can be modified)
    fc_setUniqueTileMode();

#ifdef ENABLE_MUSIC
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
                input_show_pointer(false);
                return ABORT;
            case KEY_ENTER:
                // only allowed if this hexagon is empty
                if(board.tile[px][py] != HEX_EMPTY) {
#ifdef ENABLE_SAMPLES
                if(option_music == OPTION_MUSIC_OFF) play_sample(0, DOWNLEAD_ADDRESS, DOWNLEAD_LENGTH);
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

#ifdef ENABLE_SAMPLES
    if(option_music == OPTION_MUSIC_OFF) play_sample(0, MARBA_ADDRESS, MARBA_LENGTH);
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
                if(c && c != KEY_ESC) return c;
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
    fc_center(0, 19, 80, "Written in 2022 by Johan Berntsson");
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

        if(show_title_text("But even HARD isn't that difficult. Sorry", TEXT_DELAY)) return;

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


