/* The board and the rules. See src/hexboard.h for what this is separate for. */

#include "hexboard.h"

Board board;

// breadth-first search helpers
int direction[6][2] = {
    {0,1}, {1, -1}, {1, 0}, // adjacent tiles
    {-1, 0}, {-1, 1}, {0,-1}
};

byte num_empty;
char empty_x[MAX_SIZE * MAX_SIZE];
char empty_y[MAX_SIZE * MAX_SIZE];
char perm_x[MAX_SIZE * MAX_SIZE];
char perm_y[MAX_SIZE * MAX_SIZE];

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

    // **Nobody wins by joining up the empty cells.** Handed a cell that has no
    // stone on it -- or one off the board, whose byte is really the next
    // member of the struct -- the search below would flood every gap on the
    // board instead of a chain, and a board with a gap running from one edge
    // to the opposite one is nearly every board there is. It would then
    // announce a win in the middle of the game, which is precisely what
    // guard_edge() used to make it do; see src/hexgame_ai.c and design.md.
    // Two comparisons a win check is a cheap price for never being told a
    // false winner again.
    if(!is_inside_board(x, y)) return NOWINNER;
    stone_tile = (board.tile[x][y] & (255 - HEX_CURSOR));
    if(stone_tile == HEX_EMPTY) return NOWINNER;

    // add the current stone to the queue
    board.queue_head = 1;
    board.queue_x[0] = x;
    board.queue_y[0] = y;
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
                if((board.tile[xx][yy] & (255 - HEX_CURSOR)) == stone_tile && board.visited[xx][yy] == false) {
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

// A 16 bit xorshift. Cheap -- three shifts and three exclusive ors -- with a
// period of 65535, which is far longer than any one search needs, and with
// successive values that are actually independent of each other.
static word rng_state = 0xa55a;

void hex_seed_random(word seed)
{
    rng_state = seed ? seed : 0xa55a; // zero is the one state it cannot leave
}

byte hex_random(void)
{
    rng_state ^= rng_state << 7;
    rng_state ^= rng_state >> 9;
    rng_state ^= rng_state << 8;
    return (byte)(rng_state & 0xff);
}
