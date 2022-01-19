import os

env = Environment(
    ENV={'PATH': os.environ['PATH']},
    CPPPATH='mega65-libc-modified/cc65/include',
    CC='cl65 -O')

hexgame = env.Program('bin/hexgame.c64', [
    'hexgame.c',
    'mega65-libc-modified/cc65/src/memory.c',
    'mega65-libc-modified/cc65/src/fcio.c'
])

buildDiscAction = Action('tools/buildDisc.sh')
env.AddPostAction(hexgame, buildDiscAction)
