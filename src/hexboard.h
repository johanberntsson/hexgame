/* The Hex board and the rules of the game: the state, who is adjacent to
   whom, and who has won. Nothing here draws anything or reads a key.

   **This is split out of hexgame.c so that the AI can be played against
   itself on a PC.** tools/hexsim builds this file and hexgame_ai.c with the
   host compiler and runs thousands of games in a few seconds, which is the
   only practical way to tell whether a change to the computer player made it
   stronger or merely different. See tools/hexsim/README.md.
*/

#ifndef HEXBOARD_H
#define HEXBOARD_H

#include <fcio.h>

// hexagon status (bitmask)
#define HEX_EMPTY 0
#define HEX_WHITE 1
#define HEX_BLACK 2
#define HEX_CURSOR 4

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
extern Board board;

// The six adjacent directions on the hex grid, in board coordinates.
extern int direction[6][2];

// Monte Carlo simulation helpers
// empty tiles when starting mcs
extern byte num_empty;
extern char empty_x[MAX_SIZE * MAX_SIZE];
extern char empty_y[MAX_SIZE * MAX_SIZE];
// permutations of empty tiles during mcs
extern char perm_x[MAX_SIZE * MAX_SIZE];
extern char perm_y[MAX_SIZE * MAX_SIZE];

void init_game(byte size);
void check_edges(byte x, byte y, byte *condition);
byte is_inside_board(int x, int y);
byte check_win(byte x, byte y);
void get_empty_tiles(byte max_tiles, bool shuffle);

// A 16 bit xorshift, seeded once from the CIA timer at startup.
//
// **This replaced `#define RND PEEK(0xdc04)`**, a read of CIA 1's free
// running timer. That is fine for one number now and then -- which is what the
// board shuffle wants -- but the Monte Carlo search read it hundreds of times
// from inside a tight loop, where consecutive reads differ by close to a
// constant and the "random" playouts were nothing of the kind.
void hex_seed_random(word seed);
byte hex_random(void);

#define RND hex_random()

#endif /* HEXBOARD_H */
