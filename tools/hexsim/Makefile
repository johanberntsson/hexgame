# The AI, played against itself on the PC. See README.md.
#
# -I stub is what makes src/hexboard.h's `#include <fcio.h>` resolve to a host
# header instead of the MEGA65 library's.
CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -Wno-unused-parameter -Wno-char-subscripts -I stub
SRCS     = hexsim.c ../../src/hexboard.c ../../src/hexgame_ai.c

hexsim: $(SRCS) ../../src/hexboard.h ../../src/hexgame_ai.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f hexsim

.PHONY: clean
