# hexsim — the computer player, measured

`hexsim` builds `src/hexboard.c` and `src/hexgame_ai.c` — the same two files the
MEGA65 build compiles — with the host compiler, and plays the AI against itself
a few hundred times a second. It exists because "this move looks better" is not
a claim anybody can check by watching one game on a 40 MHz machine.

```sh
make
./hexsim -n 400 -b hard -w normal
```

```
9x9, 400 games: black (hard) 378, white (normal) 22  -- black wins 94.5%, 25.9 moves a game, 0.16 s
```

| flag | |
|---|---|
| `-n` | games to play (default 200) |
| `-s` | board size, 2 to 9 (default 9, the size the game plays) |
| `-b` / `-w` | black's and white's player: `easy`, `normal`, `hard`, `random` |
| `-r` | seed for the AI's own generator; 0 takes it from the clock |
| `-v` | print the final position of every game |

## How both sides can be the AI

The game's AI only knows how to play black, connecting top to bottom. Hex is
symmetric under swapping the two axes and the two colours together, so white is
the same code shown a transposed, colour-swapped board: black connecting top to
bottom on the mirror *is* white connecting left to right on the real board.
Nothing about the AI is reimplemented here, which is the point — a match
between two difficulty settings is between the two settings that ship.

**White moves first**, as the human does in the game, and Hex gives the first
player a real edge, so a level played against itself lands near 53/47 in
white's favour rather than exactly even. Compare a change by running it in both
seats.

## What it does not measure

Time. The `0.16 s` above is 400 games on a PC and says nothing about how long a
move takes on a MEGA65 — only that one AI is far cheaper than another. For
wall-clock behaviour on the machine, run the game.

Strength against a person. `random` is a floor, not an opponent; a player that
beats it 100% of the time may still be beaten by anyone who has read the rules.

## The four hooks

`src/hexgame_ai.h` declares everything the AI needs from the program around it:
`draw_board`, the three progress bar calls, and `ai_poll_input`. The game
implements them; `hexsim.c` implements them as nothing. That short list is what
keeps the AI portable to a host with no screen, and it is worth keeping short.
