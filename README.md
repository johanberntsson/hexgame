# hexgame
The Hex game for the MEGA65 computer, with an AI using Monte Carlo simulation and some heuristics.

![Tiles](screenshots/title.png)

# Getting started

Run the hexgame.d81 file in an emulator or on a real MEGA65.

A typical command to start the emulator is:

    > xemu-xmega65 -8 disc/hexgame.d81 

# Compiling and building

You need to install the cc65 compiler. scons is also needed

    > sudo apt install scons

pypng is needed for converting new graphics

    > sudo apt install python3-pip
    > pip install pypng

Build with "make". 

# Notes

The game is based on the mega65_libc, especially fcio.c for full color
graphics, but I've extended the library to allow tile blitting with
transparancy to allow overlapping bitmaps.

The computer player uses a mix of heuristics and a Monte Carlo simulation
to determine the next optimal move. Basically, how it works is we get all
the empty spots in the board and stores them into a permutation which is
then randomized. The AI goes through the permutations, placing stones
and looking for wins. If the computer won then we increment a win counter
that is used to determine the best move.

However, Hex has a large search space, and the MEGA65 cannot search well
in reasonable time, especially in the beginning of the game when most
positions are empty. This unfortunately means that even the hard level
is easy to beat for a human player.

It could be worth looking into a min-max algorithm for the AI instead.
This is left as an exercise to the reader :)

See [design.md](design.md) for more information.

Feedback and pull requests are always welcome and appreciated.
