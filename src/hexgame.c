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
    4. Fix the AI
*/

#include <fcio.h>

#define RND PEEK(0xdc04)

// add a song or not?
#define ENABLE_MUSIC
//#define ENABLE_SAMPLES

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

    // breadth-first search helpers (for finding winner)
    char queue_head;
    char visited[MAX_SIZE][MAX_SIZE];
    // in worst case half of the board is white, half is black
    char queue_x[(MAX_SIZE * MAX_SIZE)/2]; 
    char queue_y[(MAX_SIZE * MAX_SIZE)/2]; 

    // Monte Carlo simulation helpers
    // empty tiles when starting mcs
    byte num_empty;
    char empty_x[MAX_SIZE * MAX_SIZE];
    char empty_y[MAX_SIZE * MAX_SIZE];
    // empty tile permutations during mcs
    char perm_x[MAX_SIZE * MAX_SIZE];
    char perm_y[MAX_SIZE * MAX_SIZE];

} Board;
Board board;

// breadth-first search helpers
int direction[6][2] = {
    {-1, 0}, {-1, 1}, {0,-1}, {0,1}, {1, -1}, {1, 0} // adjacent tiles
};

#ifdef ENABLE_SAMPLES
#include "sample01.c"
#include "sample02.c"

void play_sample(unsigned char ch, unsigned short sample_address, unsigned short sample_length)
{
  unsigned ch_ofs = ch << 4;
  unsigned long time_base;

  if (ch > 3) return;

  // Start of sample
  POKE(0xD721 + ch_ofs, sample_address & 0xff);
  POKE(0xD722 + ch_ofs, sample_address >> 8);
  POKE(0xD723 + ch_ofs, 0);
  POKE(0xD72A + ch_ofs, sample_address & 0xff);
  POKE(0xD72B + ch_ofs, sample_address >> 8);
  POKE(0xD72C + ch_ofs, 0);
  // pointer to end of sample
  POKE(0xD727 + ch_ofs, (sample_address + sample_length) & 0xff);
  POKE(0xD728 + ch_ofs, (sample_address + sample_length) >> 8);
  // volume
  //POKE(0xD729 + ch_ofs, 0x3F); // 1/4 Full volume
  POKE(0xD729 + ch_ofs, 0xFF); // 1/4 Full volume
  // Enable playback of channel 0, 8-bit samples, signed
  POKE(0xD720 + ch_ofs, 0xA2);
  POKE(0xD711 + ch_ofs, 0x80);

  time_base = 0x1000;
  POKE(0xD724 + ch_ofs, time_base & 0xff);
  POKE(0xD725 + ch_ofs, time_base >> 8);
  POKE(0xD726 + ch_ofs, time_base >> 16);
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
#include "music.c"

void update_music() {
    // call music player updater
    if(option_music == OPTION_MUSIC_ON) __asm__("jsr $c0fa");

    // acknowledge IRQ
    POKE(0xd019, 0xff);
    // Return to normal IRQ handler 
    __asm__("jmp $ea31");
}

void init_music() {
    // skip the first 2 bytes (load address)
    lcopy((unsigned short) &themodel_prg[2], 0xc046, themodel_prg_len - 2);

    // The Model: init $c046, play $c0fa
    //__asm__("lda #0");
    //__asm__("ldx #0");
    //__asm__("ldy #0"); 
    __asm__("jsr $c046");

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
    stone_tile = board.tile[x][y];
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
            if(int_x >= 0 && int_y >= 0 && int_x < board.size && int_y < board.size) {
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
            __asm__("jsr $c046"); // reinit song
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

    static byte px = 0, py = 0; // remember last position
    byte key, cx, cy = 255; // forces cx/cy/tile init

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
                if(option_music == OPTION_MUSIC_OFF) play_sample(0, (unsigned short) Snare1, Snare1_len);
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
    board.tile[cx][cy] = HEX_WHITE;
    board.redraw[cx][cy] = true;
    draw_board(1, 1);

#ifdef ENABLE_SAMPLES
    if(option_music == OPTION_MUSIC_OFF) play_sample(0, (unsigned short) SynClaves, SynClaves_len);
#endif

    return check_win(cx, cy);
}

void get_empty_tiles(byte max_tiles, bool shuffle) {
    // creates a list of empty tiles in Board.empty_*
    // useful for mfs
    byte i, x, y, swap;
    board.num_empty = 0;
    for(x = 0; x < board.size; x++) {
        for(y = 0; y < board.size; y++) {
            if(board.tile[x][y] == HEX_EMPTY) {
                board.empty_x[board.num_empty] = x;
                board.empty_y[board.num_empty] = y;
                ++board.num_empty;
            }
        }
    }

    if(max_tiles > board.num_empty) max_tiles = board.num_empty;

    if(shuffle) {
        // shuffle the list using Knuth's algorithm P (shuffling)
        //for(x = board.num_empty - 1; x > 0; x--) 
        //    y =  RND % x;
        i = board.num_empty;
        for(x = 0; x < max_tiles ; x++) {
            y =  x + (RND % i);
            --i;
            swap = board.empty_x[x];
            board.empty_x[x] = board.empty_x[y];
            board.empty_x[y] = swap;
            swap = board.empty_y[x];
            board.empty_y[x] = board.empty_y[y];
            board.empty_y[y] = swap;
        }
    }
    board.num_empty = max_tiles;
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
    fc_putsxy(65,2, "           ");
    fc_revers(false);
    fc_putsxy(PROGRESS_TEXT_X, PROGRESS_TEXT_Y, "          ");
}

byte get_wins(byte skip_tile) {
    // make a list of empty tiles and add white/black pieces until
    // someone wins. Repeat this a number of times, doing a small
    // permuation of the list of empty tiles. Return the number
    // of wins found for the black (computer) player.
    byte i, n, x, y, turn;
    byte win_count = 0;

    for(i = 0, n = 0; i < board.num_empty; i++) {
        board.perm_x[n] = board.empty_x[i];
        board.perm_y[n] = board.empty_y[i];
        if(i != skip_tile) ++n;
    }
    for(n = 0; n < 10; n++) {
        turn = 0;
        for(i = 0; i < board.num_empty - 1; i++) {
            // place white/black until end of game
            x = board.perm_x[i];
            y = board.perm_y[i];
            turn = !turn;
            if(turn) {
                board.tile[x][y] = HEX_BLACK;
            } else {
                board.tile[x][y] = HEX_WHITE;
            }
        }
    }

    // restore the board (make all empty tiles empty again)
    for(i = 0; i < board.num_empty; i++) {
        board.tile[board.empty_x[i]][board.empty_y[i]] = HEX_EMPTY;
    }
    return win_count;

#if 0
    auto blank = board.getEmpty();
    int winCount = 0;
    vector<int> perm(blank.size());
    for (int i=0; i<perm.size(); i++)
        perm[i] = i;
    for (int n=0; n<1000; n++)
    {
        int turn = (player == Player::BLACK ? 0 : 1);
        for (int i=perm.size(); i>1; i--)
        {
            int swap = rand() % i;
            int temp = perm[i-1];
            perm[i-1] = perm[swap];
         perm[swap] = temp; // prand the permutation
        }
        for (int i=0; i<perm.size(); i++)
        {
            turn = !turn; //easy bool turn tracking
            int x = blank[perm[i]].first;
            int y = blank[perm[i]].second;
            if (turn)
            {
                board.place(x, y, Player::WHITE);
            }
            else
            {
                board.place(x, y, Player::BLACK);
            }
        }
        if (board.winner() == player)
            winCount++;

        for (auto itr = blank.begin(); itr != blank.end(); ++itr)
            board.badMove(itr->first, itr->second); // take back rand moves
    }
    return static_cast<double>(winCount) / 1000;
#endif
}

void mcs_next_turn(byte *xx, byte *yy) {
    // check the number of potential/predicted wins for each empty
    // tile, pick the best as the next turn, and place the stone there.
    // return true if it was a winning move.
    byte progress_range, num_tiles;
    byte wins, most_wins = 0;
    byte i, x, y;

    show_progress_bar();
    get_empty_tiles(81, true);
    // limit the number of tiles to consider to
    // speed up evaluation
    num_tiles = board.num_empty;
    //num_tiles = 1;
    if(num_tiles > 20) num_tiles = 20;

    progress_range =  num_tiles / 10;
    if(progress_range == 0) progress_range = 1;

    for(i = 0; i < num_tiles; i++) {
        // place the stone on the board
        x = board.empty_x[i];
        y = board.empty_y[i];
        board.tile[x][y] = HEX_BLACK;

        // estimate the number of wins from this new brick
        wins = get_wins(i);
        if(wins > most_wins) {
            // the best move found so far
            *xx = x;
            *yy = y;
            most_wins = wins;
        }

        // restore the brick
        board.tile[x][y] = HEX_EMPTY;

        set_progress_bar(i / progress_range);
    }

    hide_progress_bar();

    if(most_wins == 0) {
        // we didn't find any win at all, so just pick a random tile
        // as a last resort
        *xx = board.empty_x[0];
        *yy = board.empty_y[0];
    }
}

void computer_turn_hard(byte *x, byte *y) {
    mcs_next_turn(x, y);
}

void computer_turn_normal(byte *x, byte *y) {
    mcs_next_turn(x, y);
}

void computer_turn_easy(byte *x, byte *y) {
    get_empty_tiles(1, true);
    *x = board.empty_x[0];
    *y = board.empty_y[0];
}


#if 0
double AI::getWins(Board &board, Player player) {
// helper for next()
// all  empty spots on board stored on  permutation
// then rand. empty spot picked and played off of,
// wins are tracked and the win value is then returned.
    auto blank = board.getEmpty();
    int winCount = 0;
    vector<int> perm(blank.size());
    for (int i=0; i<perm.size(); i++)
        perm[i] = i;
    for (int n=0; n<1000; n++)
    {
        int turn = (player == Player::BLACK ? 0 : 1);
        for (int i=perm.size(); i>1; i--)
        {
            int swap = rand() % i;
            int temp = perm[i-1];
            perm[i-1] = perm[swap];
         perm[swap] = temp; // prand the permutation
        }
        for (int i=0; i<perm.size(); i++)
        {
            turn = !turn; //easy bool turn tracking
            int x = blank[perm[i]].first;
            int y = blank[perm[i]].second;
            if (turn)
            {
                board.place(x, y, Player::WHITE);
            }
            else
            {
                board.place(x, y, Player::BLACK);
            }
        }
        if (board.winner() == player)
            winCount++;

        for (auto itr = blank.begin(); itr != blank.end(); ++itr)
            board.badMove(itr->first, itr->second); // take back rand moves
    }
    return static_cast<double>(winCount) / 1000;
}
#endif

#if 0
pair<int, int> AI::next(Board &board, Player p) {
    // montecarlo simulation, with getWins() it finds the
    // value of moves by making random permutations and doing simulation moves
    // on each and tracks no. wins. The moves are given the no.wins as a move
    // value, the best value is the best move.
    auto blank = board.getEmpty();
    double bestMove = 0;
    pair<int, int> move = blank[0];

    for (int i=0; i<blank.size(); i++)
    {
        int x = blank[i].first;
        int y = blank[i].second;
        board.place(x, y, p);

        double moveValue = getWins(board, p);
        if (moveValue > bestMove)
        {
            move = blank[i];
            bestMove = moveValue;
        }

        board.badMove(x, y);
    }
    return move;
}
#endif

byte computer_turn() {
    // add a stone, return true if this was a winning move
    byte x, y;

    // just add a random black stone
    if(option_difficulty == OPTION_DIFFICULTY_EASY) 
        computer_turn_easy(&x, &y);
    else if(option_difficulty == OPTION_DIFFICULTY_NORMAL) 
        computer_turn_normal(&x, &y);
    else
        computer_turn_hard(&x, &y);

    // put a black stone here
    board.tile[x][y] = HEX_BLACK;
    board.redraw[x][y] = true;
    draw_board(1, 1);
    return check_win(x, y);
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
    fc_displayTile(tiles, 19, 0, 0, 7, 40, 17, 0);
    fc_textcolor(FC_COLOR_YELLOW);
    fc_center(0, 19, 80, "Written in 2022 by Johan Berntsson");
    fc_textcolor(FC_COLOR_GREEN);
    fc_putsxy(20, 49, "Music (F1): ");
    fc_putsxy(40, 49, "Difficulty (F2): ");

    for(;;) {
        init_game(4);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);

        show_options();

        if(show_title_text("Welcome to HEX!", 4)) return;

        add_white_stone(0,0);
        add_black_stone(2,3);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(show_title_text("In this game two players place stones the board", 4)) return;

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
        if(delay(1)) return;

        if(show_title_text("And black tries to connect the top and bottom edges", 0)) return;

        add_black_stone(3,2);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;
        add_black_stone(3,1);
        draw_board(TITLE_BOARD_X, TITLE_BOARD_Y);
        if(delay(1)) return;

        if(show_title_text("But you can only add stones on empty tiles", 4)) return;

        if(show_title_text("You play white, the computer is black", 4)) return;

        if(show_title_text("Select an empty tile with the cursor keys and enter", 4)) return;
        if(show_title_text("Press any key to start", 6)) return;
    }
}

void show_game_screen() {
    fc_clrscr();
    fc_bgcolor(FC_COLOR_GREY1);
    fc_bordercolor(FC_COLOR_BLACK);
    init_game(MAX_SIZE);
    draw_board(1, 1);
}

void main() {
    word turn;
    byte game_state;
    unsigned short i;

    option_music = OPTION_MUSIC_ON;
    option_difficulty = OPTION_DIFFICULTY_NORMAL;

    init_graphics();
#ifdef ENABLE_MUSIC
    init_music();
#endif

    for(;;) {
        show_title_screen();

        show_game_screen();

        turn = 0;
        game_state = NOWINNER;
        while(game_state == NOWINNER) {
            ++turn;
            board.side = (turn % 2);
            if(board.side == WHITE_PLAYER)
                game_state = player_turn();
            else
                game_state = computer_turn();
        }
        if(game_state != ABORT) show_win_screen();
    }
}


