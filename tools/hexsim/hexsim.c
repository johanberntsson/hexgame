/* hexsim -- play the game's own computer player against itself, on the PC.
 *
 * It links src/hexboard.c and src/hexgame_ai.c, the same two files the MEGA65
 * build compiles, so what it measures is the shipping AI and not a model of
 * it. The four hooks in hexgame_ai.h -- draw_board and the three progress bar
 * calls -- are implemented here as nothing, which is the whole reason those
 * hooks exist.
 *
 * **The AI only knows how to play black.** White is the same code looking at a
 * transposed, colour swapped board: Hex is symmetric under swapping the axes
 * and the colours together, so black connecting top to bottom on the mirror is
 * white connecting left to right on the real board. That means both sides in
 * every game below are the real computer player, and a match between two
 * difficulty settings is a fair one.
 *
 * See README.md in this directory for what the numbers mean.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../../src/hexboard.h"
#include "../../src/hexgame_ai.h"

/* --------------------------------------------------------------- the hooks */

void draw_board(byte x0, byte y0) { (void)x0; (void)y0; }
void show_progress_bar(void) {}
void set_progress_bar(byte position) { (void)position; }
void hide_progress_bar(void) {}
void ai_poll_input(void) {}

/* ------------------------------------------------------------- the players */

/* Difficulties 0-2 are the game's own; this one is the yardstick. A player
 * that is beaten by a coin flip is not an opponent, and knowing how far above
 * one an AI is says more than knowing it beats the level below it. */
#define PLAYER_RANDOM 3

static const char *player_name(int p)
{
    switch (p) {
    case OPTION_DIFFICULTY_EASY:   return "easy";
    case OPTION_DIFFICULTY_NORMAL: return "normal";
    case OPTION_DIFFICULTY_HARD:   return "hard";
    default:                       return "random";
    }
}

/* Swap the two colours in one tile, leaving any other bits alone. */
static char swap_colour(char t)
{
    char rest = t & ~(HEX_WHITE | HEX_BLACK);
    switch (t & (HEX_WHITE | HEX_BLACK)) {
    case HEX_WHITE: return rest | HEX_BLACK;
    case HEX_BLACK: return rest | HEX_WHITE;
    default:        return t;
    }
}

/* Reflect the board in its main diagonal and swap the colours, so that the
 * position white faces becomes the position black faces. Its own inverse. */
static void mirror_board(void)
{
    byte x, y, t;

    for (x = 0; x < board.size; x++) {
        for (y = x; y < board.size; y++) {
            char a = swap_colour(board.tile[x][y]);
            char b = swap_colour(board.tile[y][x]);
            board.tile[x][y] = b;
            board.tile[y][x] = a;
        }
    }
    t = board.black_last_x;
    board.black_last_x = board.white_last_y;
    board.white_last_y = t;
    t = board.black_last_y;
    board.black_last_y = board.white_last_x;
    board.white_last_x = t;
}

static void random_move(byte *x, byte *y)
{
    get_empty_tiles(1, true);
    *x = empty_x[0];
    *y = empty_y[0];
}

/* One move for black. Returns what check_win made of it. */
static byte black_move(int difficulty, byte move_number)
{
    board.side = BLACK_PLAYER;

    if (difficulty == PLAYER_RANDOM) {
        byte x, y;
        random_move(&x, &y);
        board.tile[x][y] = HEX_BLACK;
        board.black_last_x = x;
        board.black_last_y = y;
        return check_win(x, y);
    }

    option_difficulty = (byte)difficulty;
    return computer_turn(move_number);
}

/* One move for white, played by the black AI on the mirrored board. The result
 * comes back as BLACK_PLAYER and is translated on the way out. */
static byte white_move(int difficulty, byte move_number)
{
    byte result;

    mirror_board();
    result = black_move(difficulty, move_number);
    mirror_board();

    return (result == BLACK_PLAYER) ? WHITE_PLAYER : result;
}

/* --------------------------------------------------------------- the match */

static void print_board(void)
{
    byte x, y;

    for (y = 0; y < board.size; y++) {
        printf("%*s", y + 1, "");
        for (x = 0; x < board.size; x++) {
            char t = board.tile[x][y] & (HEX_WHITE | HEX_BLACK);
            putchar(t == HEX_WHITE ? 'O' : t == HEX_BLACK ? '#' : '.');
            putchar(' ');
        }
        putchar('\n');
    }
}

/* White moves first, as in the game, where white is the human.
 * Returns BLACK_PLAYER or WHITE_PLAYER, and sets *moves. */
static byte play_game(byte size, int white_ai, int black_ai, int *moves)
{
    byte turn, state = NOWINNER;
    byte white_moves = 0, black_moves = 0;

    init_game(size);
    board.white_last_x = 0;
    board.white_last_y = 0;
    board.black_last_x = 0;
    board.black_last_y = 0;

    for (turn = 1; turn <= (byte)(size * size); turn++) {
        if (turn % 2) {
            state = white_move(white_ai, ++white_moves);
        } else {
            state = black_move(black_ai, ++black_moves);
        }
        if (state != NOWINNER) {
            break;
        }
    }
    *moves = turn;
    return state;
}

static void usage(const char *argv0)
{
    fprintf(stderr,
        "usage: %s [-n games] [-s size] [-w white] [-b black] [-r seed] [-v]\n"
        "\n"
        "  -n  number of games to play (default 200)\n"
        "  -s  board size, 2 to %d (default 9, the size the game plays)\n"
        "  -w  white's player, and -b black's: easy, normal, hard or random\n"
        "      (default: white random, black hard)\n"
        "  -r  seed for the AI's own generator (default 1; 0 uses the clock)\n"
        "  -v  print the final position of every game\n"
        "\n"
        "White moves first, as the human does in the game.\n",
        argv0, MAX_SIZE);
    exit(2);
}

static int parse_player(const char *s, const char *argv0)
{
    if (!strcmp(s, "easy"))   return OPTION_DIFFICULTY_EASY;
    if (!strcmp(s, "normal")) return OPTION_DIFFICULTY_NORMAL;
    if (!strcmp(s, "hard"))   return OPTION_DIFFICULTY_HARD;
    if (!strcmp(s, "random")) return PLAYER_RANDOM;
    fprintf(stderr, "%s: unknown player '%s'\n", argv0, s);
    usage(argv0);
    return 0;
}

int main(int argc, char **argv)
{
    int games = 200, size = 9, seed = 1, verbose = 0;
    int white_ai = PLAYER_RANDOM, black_ai = OPTION_DIFFICULTY_HARD;
    int i, black_wins = 0, total_moves = 0;
    clock_t started;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-v")) { verbose = 1; continue; }
        if (i + 1 >= argc) usage(argv[0]);
        if      (!strcmp(argv[i], "-n")) games = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-s")) size  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r")) seed  = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-w")) white_ai = parse_player(argv[++i], argv[0]);
        else if (!strcmp(argv[i], "-b")) black_ai = parse_player(argv[++i], argv[0]);
        else usage(argv[0]);
    }
    if (size < 2 || size > MAX_SIZE || games < 1) usage(argv[0]);

    hex_seed_random(seed ? (word)seed : (word)time(NULL));

    started = clock();
    for (i = 0; i < games; i++) {
        int moves;
        byte winner = play_game((byte)size, white_ai, black_ai, &moves);

        total_moves += moves;
        if (winner == BLACK_PLAYER) black_wins++;
        if (verbose) {
            printf("game %d: %s won in %d moves\n", i + 1,
                   winner == BLACK_PLAYER ? "black" : "white", moves);
            print_board();
        }
    }

    printf("%dx%d, %d games: black (%s) %d, white (%s) %d"
           "  -- black wins %.1f%%, %.1f moves a game, %.2f s\n",
           size, size, games,
           player_name(black_ai), black_wins,
           player_name(white_ai), games - black_wins,
           100.0 * black_wins / games,
           (double)total_moves / games,
           (double)(clock() - started) / CLOCKS_PER_SEC);
    return 0;
}
