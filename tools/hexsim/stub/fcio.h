/* Stub of mega65-libc-modified/include/fcio.h for tools/hexsim.

   src/hexboard.h includes <fcio.h> for three typedefs and nothing else; on the
   MEGA65 that resolves to the real library, and here to this. Keeping the game
   sources free of #ifdefs is worth a twelve line header. */
#ifndef HEXSIM_FCIO_H
#define HEXSIM_FCIO_H

#include <memory.h>
#include <stdbool.h>

typedef unsigned char byte;
typedef unsigned int word;
typedef unsigned long himemPtr;

#endif
