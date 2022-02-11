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

extern unsigned int loadExt(char *filename, himemPtr addr, byte skipCBMAddressBytes); // from fcio.c

#define RND PEEK(0xdc04)
#define TEXT_DELAY 6

// add a song or not?
#define ENABLE_MUSIC
#define ENABLE_SAMPLES

// hexagon status (bitmask)
#define HEX_EMPTY 0
#define HEX_WHITE 1
#define HEX_BLACK 2
#define HEX_CURSOR 4

// Keyboard input values
#define KEY_F1 241
#define KEY_F2 242
#define KEY_UP 145
#define KEY_ESC 27
#define KEY_DOWN 17
#define KEY_LEFT 157
#define KEY_RIGHT 29
#define KEY_ENTER 13
#define KEY_SPACE 32

// global options
byte option_music;
byte option_difficulty;
#define OPTION_MUSIC_ON  0
#define OPTION_MUSIC_OFF 1
#define OPTION_DIFFICULTY_EASY 0
#define OPTION_DIFFICULTY_NORMAL 1
#define OPTION_DIFFICULTY_HARD 2
char option_music_text[][] = { "ON ", "OFF" };
char option_difficulty_text[][] = { "EASY  ", "NORMAL", "HARD  " };

// offsets on the title screen
#define TITLE_TEXT_Y 22
#define TITLE_BOARD_X 25
#define TITLE_BOARD_Y 26
#define PROGRESS_TEXT_X 65
#define PROGRESS_TEXT_Y 4

// empty line used to hide previous texts
char *empty40 = "                                        ";


// Board data
#define MAX_SIZE 9      // more won't fit on the screen
#define BLACK_PLAYER 0
#define WHITE_PLAYER 1
#define NOWINNER 2
#define ABORT 3

typedef struct {
    byte size;
    byte size_minus_1;

    // The board state
    byte side; // current player
    char tile[MAX_SIZE][MAX_SIZE];
    char redraw[MAX_SIZE][MAX_SIZE];
    byte white_last_x, white_last_y; // human player's last stone position
    byte black_last_x, black_last_y; // computer player's last stone position

    // breadth-first search helpers (for finding winner)
    char queue_head;
    char visited[MAX_SIZE][MAX_SIZE];
    // in worst case half of the board is white, half is black
    char queue_x[(MAX_SIZE * MAX_SIZE)/2]; 
    char queue_y[(MAX_SIZE * MAX_SIZE)/2]; 


} Board;
Board board;

// breadth-first search helpers
int direction[6][2] = {
    {0,1}, {1, -1}, {1, 0}, // adjacent tiles
    {-1, 0}, {-1, 1}, {0,-1}
};

// Monte Carlo simulation helpers
// empty tiles when starting mcs
byte num_empty;
char empty_x[MAX_SIZE * MAX_SIZE];
char empty_y[MAX_SIZE * MAX_SIZE];
// permutations of empty tiles during mcs
char perm_x[MAX_SIZE * MAX_SIZE];
char perm_y[MAX_SIZE * MAX_SIZE];

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

void init_graphics() {
    fc_init(1, 1, &myConfig, 0, 47, 0);

    tiles = fc_loadFCI("hexgame.fci", 0, 0);
    fc_loadFCIPalette(tiles);

    // this makes $20000 - $5ffff for character data (so tiles can be modified)
    fc_setUniqueTileMode();

}


#ifdef ENABLE_MUSIC
//#include "music.c"

void update_music() {
    // call music player updater
    if(option_music == OPTION_MUSIC_ON) __asm__("jsr $c059");

    // acknowledge IRQ
    POKE(0xd019, 0xff);
    // Return to normal IRQ handler 
    __asm__("jmp $ea31");
}


void init_music() {
    // skip the first 2 bytes (load address)
    //loadExt("themodel.prg", 0xc046, 1);
    //lcopy((unsigned short) &Armalyte_prg[2], 0xc000, Armalyte_prg_len - 2);
    loadExt("armalyte.prg", 0x16000, 1);
    lcopy(0x16000, 0xc000, 3970);

    // The Model: init $c046, play $c0fa
    __asm__("lda #0");
    __asm__("jsr $c000");

    // Suspend interrupts during init
    __asm__("sei");   
    //  Disable CIA
    POKE(0xdc0d, 0x7f); 
    // Enable raster interrupts
    POKE(0xd01a, PEEK(0xd01a) | 1);
    // High bit of raster line cleared, we're
    // only working within single byte ranges
    POKE(0xd011, PEEK(0xd011) & 0x7f);
    // We want an interrupt at the top line
    POKE(0xd012, 140);
    // Push low and high byte of our routine into IRQ vector addresses
    POKE(0x0314, ((unsigned int) &update_music) & 0xff);
    POKE(0x0315, ((unsigned int) &update_music) >> 8);
    // Enable interrupts again
    __asm__("cli");   
}
#endif

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
                if(board.tile[x][y] & HEX_CURSOR)
                    fc_displayTile(tiles, xx, yy, 18, 0, 6, 7, 1);
            }
        }
    }
}

void init_game(byte size) {
    byte x,y;
    board.size = size;
    board.size_minus_1 = size - 1;
    for(x = 0; x < size;  x++) {
        for(y = 0; y < size;  y++) {
            board.tile[x][y] = HEX_EMPTY;
            board.redraw[x][y] = true;
        }
    }
}

void check_edges(byte x, byte y, byte* condition) {
    // set condition[0] to true stone at top/left edge, and condition[1] if bottom/right
   if(board.side == BLACK_PLAYER) {
      if(y == 0) condition[0] = true;
      if(y == board.size_minus_1) condition[1] = true;
   } else {
      if(x == 0) condition[0] = true;
      if(x == board.size_minus_1) condition[1] = true;
   }
}

byte is_inside_board(int x, int y) {
    return (x >= 0 && y >= 0 && x < board.size && y < board.size);
}

byte check_win(byte x, byte y) {
    // do a breadth-first search from the latest placed stone (at x, y)
    int int_x, int_y; // since direction is int and has negative values
    byte i, j, xx, yy, stone_tile;
    byte condition[2];

    // first clear bfs history
    for(i = 0; i < board.size; i++) {
        for(j = 0; j < board.size; j++) {
            board.visited[i][j] = false;
        }
    }

    // add the current stone to the queue
    board.queue_head = 1;
    board.queue_x[0] = x;
    board.queue_y[0] = y;
    stone_tile = board.tile[x][y] & (255 - HEX_CURSOR);
    condition[0] = false; // any stone on the left/top edge?
    condition[1] = false; // any stone on the right/bottom edge?

    while(board.queue_head > 0) {
        // pop the head of the queue
        --board.queue_head;
        x = board.queue_x[board.queue_head];
        y = board.queue_y[board.queue_head];
        check_edges(x, y, condition);
        board.visited[x][y] = true;

        // add all unvisited adjacent tiles of the same colour
        for(i = 0; i < 6; i++) {
            int_x = x + direction[i][0];
            int_y = y + direction[i][1];
            if(is_inside_board(int_x, int_y)) {
            //if(int_x >= 0 && int_y >= 0 && int_x < board.size && int_y < board.size) {
                // the adjacent position is a valid board position
                xx = (byte) int_x;
                yy = (byte) int_y;
                if(board.tile[xx][yy] == stone_tile && board.visited[xx][yy] == false) {
                    board.visited[xx][yy] = true;
                    board.queue_x[board.queue_head] = xx;
                    board.queue_y[board.queue_head] = yy;
                    ++board.queue_head;
                }
            } else {
            }
        }
    }

    if(condition[0] && condition[1]) return board.side;
    return NOWINNER;
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
    fc_getkey();
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
            __asm__("jsr $c000"); // reinit song
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

byte player_turn() {
    // add a stone, return true if this was a winning move
    byte key;
    byte cx, cy = 255; // cursor pos, 255 forces cx/cy/tile init
    byte px = board.white_last_x;
    byte py = board.white_last_y;

    // display cursor and get new stone location
    key = 0;
    while(key != KEY_ENTER) {
        if(px != cx || py != cy) {
            if(cx < board.size) {
                board.redraw[cx][cy] = true;
                board.tile[cx][cy] &= 0xff-HEX_CURSOR;
            }
            board.redraw[px][py] = true;
            board.tile[px][py] |= HEX_CURSOR;
            draw_board(1, 1);
            cx = px;
            cy = py;
        }
        key = fc_getkey();
        if(key) update_options(&key); // check if a global option command
        if(key == KEY_SPACE) key = KEY_ENTER;
        switch(key) {
            case KEY_ESC:
                return ABORT;
            case KEY_ENTER:
                // only allowed if this hexagon is empty
                if(board.tile[cx][cy] != HEX_CURSOR) {
#ifdef ENABLE_SAMPLES
                if(option_music == OPTION_MUSIC_OFF) play_sample(0, 0xa000, 6014);
#endif
                    key = 0;
                }
                break;
            case KEY_LEFT:
                if(cx > 0) --px;
                break;
            case KEY_RIGHT:
                px = cx + 1;
                if(px >= board.size) --px;
                break;
            case KEY_UP:
                if(cy > 0) --py;
                break;
            case KEY_DOWN:
                py = cy + 1;
                if(py >= board.size) --py;
        }
    }

    // put a white stone here
    board.tile[px][py] = HEX_WHITE | HEX_CURSOR;
    board.redraw[px][py] = true;
    draw_board(1, 1);

#ifdef ENABLE_SAMPLES
    if(option_music == OPTION_MUSIC_OFF) play_sample(0, 0x16000, 7500);
#endif

    board.white_last_x = px;
    board.white_last_y = py;
    return check_win(px, py);
}

void get_empty_tiles(byte max_tiles, bool shuffle) {
    // creates a list of empty tiles in Board.empty_*
    // useful for mfs
    byte i, x, y, swap;
    num_empty = 0;
    for(x = 0; x < board.size; x++) {
        for(y = 0; y < board.size; y++) {
            if(board.tile[x][y] == HEX_EMPTY) {
                empty_x[num_empty] = x;
                empty_y[num_empty] = y;
                ++num_empty;
            }
        }
    }

    if(max_tiles > num_empty) max_tiles = num_empty;

    if(shuffle) {
        // shuffle the list using Knuth's algorithm P (shuffling)
        //for(x = board.num_empty - 1; x > 0; x--) 
        //    y =  RND % x;
        i = num_empty;
        for(x = 0; x < max_tiles ; x++) {
            y =  x + (RND % i);
            --i;
            swap = empty_x[x];
            empty_x[x] = empty_x[y];
            empty_x[y] = swap;
            swap = empty_y[x];
            empty_y[x] = empty_y[y];
            empty_y[y] = swap;
        }
    }
    num_empty = max_tiles;
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
            c = PEEK(0xD610U);
            if(c) {
                POKE(0xD610U, 0); // clear key pressed buffer
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

        if(show_title_text("Select an empty tile with the cursor keys and enter", TEXT_DELAY)) return;

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
    draw_board(1, 1);
}

#include "hexgame_ai.c"

void main() {
    word turn;
    byte game_state;

    option_music = OPTION_MUSIC_ON;
    option_difficulty = OPTION_DIFFICULTY_NORMAL;

    init_graphics();
#ifdef ENABLE_MUSIC
    init_music();
#endif
#ifdef ENABLE_SAMPLES
    loadExt("marba.wav", 0x16000, 0);
    loadExt("downlead.wav", 0xa000, 0);
#endif

    // clear keyboard buffer
    POKE(0xD610U, 0);

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


