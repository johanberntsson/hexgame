/* The computer player.

   Everything the AI needs from the program around it is declared here and
   nowhere else, which is what lets tools/hexsim link the same AI against a
   host that has no screen: the four hooks below are the entire interface, and
   the simulator implements them as nothing at all.
*/

#ifndef HEXGAME_AI_H
#define HEXGAME_AI_H

#include "hexboard.h"

#define OPTION_DIFFICULTY_EASY 0
#define OPTION_DIFFICULTY_NORMAL 1
#define OPTION_DIFFICULTY_HARD 2
extern byte option_difficulty;

// Provided by the host: the game draws, the simulator does not.
void draw_board(byte x0, byte y0);
void show_progress_bar(void);
void set_progress_bar(byte position);
void hide_progress_bar(void);
// Called from inside the search, often enough that a key press during a long
// think is not lost. The game reads $D610 and acts on F1/F2 here.
void ai_poll_input(void);

// Places the computer's stone and returns what check_win made of it.
// `turn` counts the computer's own moves, from 1.
byte computer_turn(byte turn);

#endif /* HEXGAME_AI_H */
