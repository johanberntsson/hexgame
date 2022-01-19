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
        i = PEEK(0xD610U);
        if(i) {
            POKE(0xD610U, 0);
            update_options(&i);
            return 0;
        }

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

void mcs_next_turn(byte *xx, byte *yy, byte num_empty, byte num_permutations) {
    // check the number of potential/predicted wins for each empty
    // tile, pick the best as the next turn, and place the stone there.
    // return true if it was a winning move.
    // 
    // parameters to control time/CPU usage:
    // num_empty: max number of empty tiles to consider
    // num_permutations: number of random games to play from each empty tile
    //
    byte progress_range, num_tiles;
    byte wins, most_wins = 0;
    byte i, x, y;

    show_progress_bar();
    get_empty_tiles(num_empty, true);
    num_tiles = num_empty;
    progress_range =  num_tiles / 10;
    if(progress_range == 0) progress_range = 1;

    for(i = 0; i < num_tiles; i++) {
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
// heuristics
//
void check_soon_connected() {
    // if the player is only one stone from making a connection
    // to another stone, then return the location of this
    // potential connection
}

//
// computer turn handlers
//
void computer_turn_hard(byte *x, byte *y) {
    mcs_next_turn(x, y, 81, 20);
}

void computer_turn_normal(byte *x, byte *y) {
    mcs_next_turn(x, y, 30, 20);
}

void computer_turn_easy(byte *x, byte *y) {
    get_empty_tiles(1, true);
    *x = empty_x[0];
    *y = empty_y[0];
}

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

