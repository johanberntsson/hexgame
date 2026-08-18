/* Stub of mega65-libc-modified/include/memory.h for tools/hexsim.
   The rules and the AI include <fcio.h>, which includes this. On the host
   there is no MEGA65 to poke at, and nothing they use needs one. */
#ifndef HEXSIM_MEMORY_H
#define HEXSIM_MEMORY_H

/* Reading a hardware register on the host is not meaningful. Anything in the
   game that genuinely needs one has to live behind a hook in hexgame_ai.h,
   which is the point of the split -- so these exist to satisfy the include
   and should never actually be reached. */
#define POKE(X, Y) ((void)0)
#define PEEK(X)    (0)

#endif
