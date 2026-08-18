/* The computer player: see src/hexgame_ai.h for the interface, and
   tools/hexsim for the harness that measures whether a change to any of this
   was an improvement.
*/

#include "hexgame_ai.h"

byte option_difficulty;

//
// Monte Carlo simulation routines
//
byte mcs_get_wins(byte skip_tile, byte num_permutations) {
    // make a list of empty tiles and add white/black pieces until
    // someone wins. Repeat this a number of times, doing a small
    // permuation of the list of empty tiles. Return the number
    // of wins found for the black (computer) player.
    byte i, n, x, y, turn, swap, temp;
    byte win_count = 0;

    for(i = 0, n = 0; i < num_empty; i++) {
        perm_x[n] = empty_x[i];
        perm_y[n] = empty_y[i];
        if(i != skip_tile) ++n;
    }
    for(n = 0; n < num_permutations; n++) {
        ai_poll_input(); // so F1/F2 still work during a long think

        // do random mutations
        for(i=num_empty - 1; i>1; i--) {
            swap = RND % i;
            temp = perm_x[i-1];
            perm_x[i-1] = perm_x[swap];
            perm_x[swap] = temp;
            temp = perm_y[i-1];
            perm_y[i-1] = perm_y[swap];
            perm_y[swap] = temp;
        }

        // place white/black until end of game (or black wins)
        turn = 0;
        for(i = 0; i < num_empty - 1; i++) {
            x = perm_x[i];
            y = perm_y[i];
            turn = !turn;
            if(turn) {
                board.tile[x][y] = HEX_BLACK;
            } else {
                board.tile[x][y] = HEX_WHITE;
            }
        }
        // check if black won by trying to find
        // paths from each black tile on the top edge
        for(i = 0; i < board.size; i++) {
            if( board.tile[i][0] == HEX_BLACK &&
                check_win(i, 0) == BLACK_PLAYER) {
                ++win_count;
                break;
            }
        }
    }
    //fc_gotoxy(0,0); fc_printf("%d   ", win_count);
    return win_count;
}

void mcs_next_turn(byte *xx, byte *yy, byte max_num_empty, byte num_permutations) {
    // check the number of potential/predicted wins for each empty
    // tile, pick the best as the next turn, and place the stone there.
    // return true if it was a winning move.
    // 
    // parameters to control time/CPU usage:
    // max_num_empty: max number of empty tiles to consider
    // num_permutations: number of random games to play from each empty tile
    //
    byte progress_range;
    byte wins, most_wins = 0;
    byte i, x, y;

    show_progress_bar();
    get_empty_tiles(max_num_empty, true);
    progress_range =  num_empty / 10;
    if(progress_range == 0) progress_range = 1;

    for(i = 0; i < num_empty; i++) {
        // place the stone on the board
        x = empty_x[i];
        y = empty_y[i];
        board.tile[x][y] = HEX_BLACK;

        // estimate the number of wins from this new brick
        wins = mcs_get_wins(i, num_permutations);
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
        *xx = empty_x[0];
        *yy = empty_y[0];
    }

    // restore the board (make all empty tiles empty again)
    for(i = 0; i < num_empty; i++) {
        board.tile[empty_x[i]][empty_y[i]] = HEX_EMPTY;
    }
}


//
// Shortest connection search
//
// The Monte Carlo player above asks "if I put a stone here and then both sides
// play at random, how often do I win". Twenty random fills of a 9x9 board is
// too few for that question to have a stable answer, and a MEGA65 cannot
// afford twenty thousand -- which is why the hard level was still easy to
// beat. This asks a different question, one that has an exact answer that can
// be computed in full: **how many more stones would each side need?**
//
// For a colour, give every cell a cost -- 0 for a stone of that colour, 1 for
// an empty cell, and unreachable for the opponent's -- and the cheapest path
// between the two edges is the number of stones that side still has to place.
// Four of those distance fields (from each of the four edges) are enough to
// score every empty cell at once: a cell that is on a short path for black is
// worth taking, and a cell that is on a short path for white is worth taking
// away.
//
// It is also **much cheaper than the Monte Carlo search it replaces**: four
// searches over 81 cells for a whole move, against twenty board fills and up
// to nine winner checks for each of up to 81 candidates.

#define DIST_INF 255

// Distances to each of the four edges. Black connects top to bottom, white
// left to right.
static byte dist_black_top[MAX_SIZE][MAX_SIZE];
static byte dist_black_bottom[MAX_SIZE][MAX_SIZE];
static byte dist_white_left[MAX_SIZE][MAX_SIZE];
static byte dist_white_right[MAX_SIZE][MAX_SIZE];

// The search queue. A cell is only ever in it once -- pf_queued says which are
// -- so 81 entries is the most it can hold and the ring never fills. 128
// rather than 82 so that the wrap is a mask and not a division.
#define PF_QUEUE_SIZE 128
#define PF_QUEUE_MASK 127
static byte pf_queue[PF_QUEUE_SIZE];
static byte pf_queued[MAX_SIZE][MAX_SIZE];
static byte pf_head, pf_tail;

// Cells travel through the queue packed one to a byte. The board is at most
// nine wide, so a nibble each is enough and the unpacking is a shift.
#define PF_PACK(x, y) (byte)(((x) << 4) | (y))

// What one more cell costs the given colour: nothing if it already has a stone
// there, one stone if it is empty, and unreachable if the opponent holds it.
static byte cell_cost(byte x, byte y, byte stone) {
    byte t = board.tile[x][y] & (HEX_WHITE | HEX_BLACK);
    if(t == HEX_EMPTY) return 1;
    return (t == stone) ? 0 : DIST_INF;
}

static void pf_push(byte x, byte y, byte front) {
    if(pf_queued[x][y]) return;
    pf_queued[x][y] = true;
    if(front) {
        pf_head = (pf_head - 1) & PF_QUEUE_MASK;
        pf_queue[pf_head] = PF_PACK(x, y);
    } else {
        pf_queue[pf_tail] = PF_PACK(x, y);
        pf_tail = (pf_tail + 1) & PF_QUEUE_MASK;
    }
}

// Fill `dist` with, for every cell, the number of stones `stone` would still
// have to place to link that cell to one edge.
//
// The weights are only ever 0 or 1, so this pushes the free steps to the front
// of the queue and the paid ones to the back and comes out with the cells in
// very nearly the right order -- a cell that is improved after it has been
// dealt with simply goes back in the queue, which on a board this size happens
// rarely and costs a few hundred cycles when it does.
//
// `vertical` is true for black, whose edges are the top and bottom rows;
// `far` picks the second of the two edges.
static void path_field(byte stone, byte vertical, byte far,
                       byte dist[MAX_SIZE][MAX_SIZE]) {
    byte x, y, i, packed, cost, nd;
    int int_x, int_y;

    for(x = 0; x < board.size; x++) {
        for(y = 0; y < board.size; y++) {
            dist[x][y] = DIST_INF;
            pf_queued[x][y] = false;
        }
    }
    pf_head = 0;
    pf_tail = 0;

    for(i = 0; i < board.size; i++) {
        if(vertical) {
            x = i;
            y = far ? board.size_minus_1 : 0;
        } else {
            x = far ? board.size_minus_1 : 0;
            y = i;
        }
        cost = cell_cost(x, y, stone);
        if(cost == DIST_INF) continue;
        dist[x][y] = cost;
        pf_push(x, y, cost == 0);
    }

    while(pf_head != pf_tail) {
        packed = pf_queue[pf_head];
        pf_head = (pf_head + 1) & PF_QUEUE_MASK;
        x = packed >> 4;
        y = packed & 15;
        pf_queued[x][y] = false;

        for(i = 0; i < 6; i++) {
            int_x = x + direction[i][0];
            int_y = y + direction[i][1];
            if(!is_inside_board(int_x, int_y)) continue;
            cost = cell_cost((byte) int_x, (byte) int_y, stone);
            if(cost == DIST_INF) continue;
            nd = dist[x][y] + cost;
            if(nd < dist[(byte) int_x][(byte) int_y]) {
                dist[(byte) int_x][(byte) int_y] = nd;
                pf_push((byte) int_x, (byte) int_y, cost == 0);
            }
        }
    }
}

// How many stones a side needs for a connection that goes through one cell:
// its distance to each of the two edges, less one because an empty cell is
// counted by both.
static byte connection_cost(byte to_near, byte to_far) {
    word sum;
    if(to_near == DIST_INF || to_far == DIST_INF) return DIST_INF;
    sum = (word) to_near + to_far;
    if(sum < 1) return 0;
    sum -= 1;
    return (sum > 250) ? 250 : (byte) sum;
}

// How far from the middle of the board, as a tie break. On an empty board
// every cell needs the same number of stones and every score below is equal;
// without this the opening move would be a corner as often as the centre.
static byte off_centre(byte x, byte y) {
    byte mid = board.size >> 1;
    byte dx = (x > mid) ? x - mid : mid - x;
    byte dy = (y > mid) ? y - mid : mid - y;
    return dx + dy;
}

void path_next_turn(byte *xx, byte *yy) {
    byte x, y;
    byte black_cost, white_cost, score;
    byte best_score = 255, best_black = 255, best_centre = 255;
    byte ties = 0;
    byte blocking = false;

    // No progress bar. Four searches over 81 cells is a few hundredths of a
    // second on a MEGA65, and "Thinking..." appearing and vanishing between
    // one frame and the next reads as a glitch rather than as progress.
    path_field(HEX_BLACK, true,  false, dist_black_top);
    path_field(HEX_BLACK, true,  true,  dist_black_bottom);
    path_field(HEX_WHITE, false, false, dist_white_left);
    path_field(HEX_WHITE, false, true,  dist_white_right);

    // A last resort that is never reached on a board with an empty cell on it,
    // but leaves *xx and *yy defined if one ever is not.
    *xx = 0;
    *yy = 0;

    for(x = 0; x < board.size; x++) {
        for(y = 0; y < board.size; y++) {
            if(board.tile[x][y] != HEX_EMPTY) continue;

            black_cost = connection_cost(dist_black_top[x][y],
                                         dist_black_bottom[x][y]);
            white_cost = connection_cost(dist_white_left[x][y],
                                         dist_white_right[x][y]);

            // This cell finishes the game. Nothing else is worth comparing.
            if(black_cost <= 1) {
                *xx = x;
                *yy = y;
                return;
            }

            // White finishes here next move unless it is taken away. Blocking
            // outranks every ordinary move, but not every block is as good as
            // every other, so the search goes on among the blocks alone.
            if(white_cost <= 1 && !blocking) {
                blocking = true;
                best_score = 255;
                best_black = 255;
                best_centre = 255;
                ties = 0;
            }
            if(blocking && white_cost > 1) continue;

            score = (black_cost > 125) ? 250 : black_cost;
            score += (white_cost > 125) ? 125 : white_cost;

            if(score < best_score ||
               (score == best_score &&
                (black_cost < best_black ||
                 (black_cost == best_black &&
                  off_centre(x, y) < best_centre)))) {
                best_score = score;
                best_black = black_cost;
                best_centre = off_centre(x, y);
                ties = 1;
                *xx = x;
                *yy = y;
            } else if(score == best_score && black_cost == best_black &&
                      off_centre(x, y) == best_centre) {
                // Choose evenly among equals, so that the computer does not
                // play the same game every time.
                ++ties;
                if((RND % ties) == 0) {
                    *xx = x;
                    *yy = y;
                }
            }
        }
    }
}

//
// heuristics
// (wouldn't be needed if the AI was better)
//
byte is_chain(byte x, byte y) {
    byte stone = (board.tile[x][y] & (255 - HEX_CURSOR));
    if(x == 0) return false;
    if(stone == HEX_EMPTY) return false;
    // check if the last white stone is part of a chain
    return ((board.tile[x - 1][y] & (255 - HEX_CURSOR)) == stone ||
            (y > 0 && (board.tile[x - 1][y - 1]  & (255 - HEX_CURSOR)) == stone) ||
            (y < board.size_minus_1 && (board.tile[x - 1][y + 1]  & (255 - HEX_CURSOR)) == stone));
}

byte guard_edge(byte x0, byte y0, byte *xx, byte *yy) {
    if(x0 > board.size/2) {
        if(is_chain(x0, y0) == false) return false;
        // block the chain if possible
        if(board.tile[x0 + 1][y0] == HEX_EMPTY) {
            *xx = x0 + 1;
            *yy = y0;
            return true;
        }
        if(y0 > 0 && board.tile[x0 + 1][y0 - 1] == HEX_EMPTY) {
            *xx = x0 + 1;
            *yy = y0 - 1;
            return true;
        }
        if(y0 < board.size_minus_1 && 
           board.tile[x0 + 1][y0 + 1] == HEX_EMPTY) {
            *xx = x0 + 1;
            *yy = y0 + 1;
            return true;
        }
    }
    return false;
}

byte check_soon_connected(byte x0, byte y0, byte *xx, byte *yy) {
    // if the player is only one stone from making a connection
    // to another stone, then return the location of this
    // potential connection
    // true if position found, otherwise false if no empty tile found
    byte i, j, x1, y1, x2, y2, stone_type;
    int int_x, int_y;

    stone_type = (board.tile[x0][y0] & (255 - HEX_CURSOR));

    // Call check_win to mark all stones that can be reached from
    // x0, y0 stone
    check_win(x0, y0);

    for(i = 0; i < 6; i++) {
        int_x = x0 + direction[i][0];
        int_y = y0 + direction[i][1];
        if(!is_inside_board(int_x, int_y)) continue;
        x1 = (byte) int_x;
        y1 = (byte) int_y;
        if(board.tile[x1][y1] != HEX_EMPTY) continue;
        // we found an adjacent empty tile. Now check if that
        // tile has an adjacent tile of the same colour
        for(j = 0; j < 6; j++) {
            int_x = x1 + direction[j][0];
            int_y = y1 + direction[j][1];
            if(!is_inside_board(int_x, int_y)) continue;
            x2 = (byte) int_x;
            y2 = (byte) int_y;
            if(x2 == x0 && y2 == y0) continue; // don't select the start stone
            if((board.tile[x2][y2] & (255 - HEX_CURSOR)) == stone_type) {
                // we found an empty tile that connects two stones

                // skip if these stones are already connected somehow
                if(board.visited[x2][y2]) continue;

                // not connects, fill it in
                *xx = x1;
                *yy = y1;
                return true;
            }
        }
    }
    return false;
}

byte build_chain(byte x0, byte y0, byte *xx, byte *yy) {
    // continue to add to the chain from the last black position
    // true if position found, otherwise false if no empty tile found
    byte i, n, x, y;
    int int_x, int_y;

    for(n = 0; n < 6; n++) {
        i = RND % 6;
        int_x = x0 + direction[i][0];
        int_y = y0 + direction[i][1];
        if(is_inside_board(int_x, int_y)) {
            x = (byte) int_x;
            y = (byte) int_y;
            if(board.tile[x][y] == HEX_EMPTY) {
                *xx = x;
                *yy = y;
                return true;
            }
        }
    }
    return false;
}

//
// computer turn handlers
//
void computer_turn_hard(byte *x, byte *y) {
    // **No heuristics in front of this one.** guard_edge and
    // check_soon_connected exist to stop the Monte Carlo player making its
    // worst moves; the connection search already sees everything they were
    // guessing at, and putting them first only overrides it with something
    // that looks one stone ahead.
    path_next_turn(x, y);
}

void computer_turn_normal(byte *x, byte *y) {
    // block if the human player is extending a chain on the right
    if(guard_edge(board.white_last_x, board.white_last_y, x, y))
        return;
    // try to block human player
    if(check_soon_connected(board.white_last_x, board.white_last_y, x, y))
        return;
    // add final stone to computer chain
    if(check_soon_connected(board.black_last_x, board.black_last_y, x, y))
        return;
    if(RND < 150) {
        // try adding to the current chain
        if(build_chain(board.black_last_x, board.black_last_y, x, y)) {
            return;
        }
    }
    mcs_next_turn(x, y, 30, 20);
}

void computer_turn_easy(byte *x, byte *y) {
    // block if the human player is extending a chain on the right
    if(guard_edge(board.white_last_x, board.white_last_y, x, y))
        return;
    // try to block human player
    if(check_soon_connected(board.white_last_x, board.white_last_y, x, y)) return;
    // add final stone to computer chain
    if(check_soon_connected(board.black_last_x, board.black_last_y, x, y)) return;
    // random empty position
    get_empty_tiles(1, true);
    *x = empty_x[0];
    *y = empty_y[0];
}

byte computer_turn(byte turn) {
    // add a stone, return true if this was a winning move
    byte x, y;

    if(turn == 1) {
        // just add a random black stone
        get_empty_tiles(1, true);
        x = empty_x[0];
        y = empty_y[0];
    } else if(option_difficulty == OPTION_DIFFICULTY_EASY) 
        computer_turn_easy(&x, &y);
    else if(option_difficulty == OPTION_DIFFICULTY_NORMAL) 
        computer_turn_normal(&x, &y);
    else
        computer_turn_hard(&x, &y);

    // put a black stone here
    board.tile[x][y] = HEX_BLACK;
    board.redraw[x][y] = true;
    draw_board(1, 1);
    board.black_last_x = x;
    board.black_last_y = y;
    return check_win(x, y);
}

