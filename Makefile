# The Hex game for the MEGA65, built with the Calypsi 6502 tool chain.
#
# This replaced an SConstruct driving cc65. The switch is not only a change of
# compiler: cc65 built a C64-mode program that a wrapper (c65bin/) launched out
# of C65 mode, and Calypsi's mega65 target builds a C65-mode program that the
# ROM boots directly. What the wrapper used to do, the file name autoboot.c65
# now does on its own.

TARGET   = --target=mega65
LIBINC   = mega65-libc-modified/include

# -O2 --speed over -Os: the Monte Carlo search is the whole reason the AI has a
# difficulty ceiling, and every playout is board array indexing. Size has never
# been the binding constraint here -- see mega65-hexgame.scm.
CFLAGS   = $(TARGET) -O2 --speed -I $(LIBINC)
ASFLAGS  = $(TARGET)
# --rtattr printf=medium, because the linker cannot pick the formatter itself
# here. It chooses the smallest variant that the format strings it can *see*
# need -- and fcio's fc_printf and fc_fatal take their format as an argument
# and pass it to vsprintf, so it sees none at all and picks `reduced`, which
# has no field widths and no flags. A "%02x" comes out as the literal text
# "%02x", which is a confusing thing to be told by a fatal error handler.
LDFLAGS  = $(TARGET) --output-format=prg --rtattr printf=medium
LINKFILE = mega65-hexgame.scm

# The C stack. The tool chain's default is 4096. The game's deepest call chain
# is main -> computer_turn -> mcs_next_turn -> mcs_get_wins -> check_win, all
# of them holding bytes rather than structs -- the board lives in one global --
# so this is many times what it needs. `make CSTACK=4096` if that stops being
# true; there is no canary here to measure it with.
CSTACK  ?= 512

# The heap. The tool chain's default is 256 bytes and nearly enough: what is
# on it is one stdio buffer per open file and a small block per loaded image.
# It used to need four times this, because fc_loadFCI allocated a whole 765
# byte palette in one piece -- under cc65 the heap was whatever RAM was left
# over and nobody had to think about it, while here the malloc returned NULL,
# was written to anyway, and hung the machine with "loading..." on the screen.
# fc_loadFCI streams the palette now, so this is headroom rather than a limit.
HEAP    ?= 512

BUILD    = build
RELEASE = release

# src/hexgame_ai.c used to be #included at the bottom of src/hexgame.c, so that
# it could see the game's globals. It is a translation unit of its own now, and
# the board it works on is src/hexboard.c -- which is separate so that
# tools/hexsim can build the AI with the host compiler and play it against
# itself. See tools/hexsim/README.md.
SRCS     = src/hexgame.c \
           src/hexboard.c \
           src/hexgame_ai.c \
           src/input.c \
           mega65-libc-modified/src/fcio.c \
           mega65-libc-modified/src/memory.c
ASRCS    = src/music_irq.s mega65-libc-modified/src/fcio_asm.s

OBJS     = $(patsubst %.c,$(BUILD)/%.o,$(notdir $(SRCS))) \
           $(patsubst %.s,$(BUILD)/%.o,$(notdir $(ASRCS)))
vpath %.c src mega65-libc-modified/src
vpath %.s src mega65-libc-modified/src

# The tune and its player are written in ACME under music/, and
# tools/acme2calypsi.py turns them into the assembler the rest of this build
# speaks. Generated into build/ rather than checked into src/, so music/ holds
# the only copy of the tune there is. The converter needs Python and nothing
# else; ACME itself is needed only by `make checkmusic`, which proves the two
# assemblers agree byte for byte.
MUSIC_SRC = music/player.asm music/music.asm music/gambit.asm music/sfx.asm
MUSIC_ASM = $(BUILD)/music_asm.s
OBJS     += $(BUILD)/music_asm.o

ELF      = $(BUILD)/hexgame.elf
GAME     = $(BUILD)/hexgame-game.prg      # the game on its own, before packing
PRG      = $(BUILD)/hexgame.prg           # what gets handed out
D81      = $(BUILD)/hexgame.prg           # what gets handed out

# **The one resource left, and it is not on a disk either.** res/hexgame.fci is
# packed into $(PRG) compressed and unpacked into attic RAM by stage 1 before
# the game runs -- see tools/mkprg.py. It is checked in, so a clone builds
# without pypng; the rule below only fires when img-src/hexgame.png is newer.
RES      = res/hexgame.fci

# Where stage 1 puts the tile sheet. **src/hexgame.c has this number too**, as
# FCI_SOURCE, and the two have to agree.
FCI_ADDR = 0x8100000

# Stage 1 lives above the streams so that it survives unpacking the game over
# them. **stage1.scm and src/boot.s have these numbers too.** mkprg.py pads the
# file out to the first and says so if the streams have grown into it.
STAGE1_ADDR  = 0xb800
STAGE1_ENTRY = 0xb820

all: $(PRG)

run: $(RELEASE)
	xemu-xmega65 -besure -8 $(RELEASE)/hexgame.d81

# The game on its own, with nothing in front of it. It gets as far as looking
# for the tile sheet stage 1 would have put in attic RAM, does not find it, and
# stops on fcio's fatal error screen -- which is enough to see that it links,
# starts, and sets the display up, and is the quick way to test a change to the
# game without waiting on exomizer.
game: $(GAME)
	xemu-xmega65 -besure -prg $(GAME)

$(BUILD):
	mkdir -p $(BUILD)

# Every object depends on every header. Crude, but fcio.h carries the screen
# geometry and the memory map both, and a stale object built against an older
# one fails as a corrupt display rather than as an error.
HDRS = $(wildcard src/*.h) $(wildcard $(LIBINC)/*.h)

$(BUILD)/%.o: %.c $(HDRS) Makefile | $(BUILD)
	cc6502 $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: %.s Makefile | $(BUILD)
	as6502 $(ASFLAGS) -o $@ $<

# The tune. --zp is the one thing the converter changes rather than
# translates: the ACME player picks its two zero page pointers by hand at $fb,
# which a C64 program may do and a program sharing zero page with a C compiler
# and a live KERNAL may not. The names go into a zzpage bss section instead and
# the linker places them -- see mega65-hexgame.scm. --public is the four
# entry points the rest of the program calls; everything else stays local to
# the generated object.
$(MUSIC_ASM): $(MUSIC_SRC) tools/acme2calypsi.py Makefile | $(BUILD)
	python3 tools/acme2calypsi.py music/player.asm $@ \
	    --zp ZP_PTR:2,ZP_ARP:2 --public music_init,music_play,sfx_start,sfx_tick

$(BUILD)/music_asm.o: $(MUSIC_ASM) Makefile | $(BUILD)
	as6502 $(ASFLAGS) -o $@ $<

# Both assemblers over the same tune, byte for byte. Needs acme on PATH.
# **Run it after touching anything under music/**: a translator between two
# assemblers is either exactly right or quietly playing a different tune.
checkmusic:
	python3 tools/checkmusic.py

# The list file is the memory map, and it is the only place the program's real
# size is written down -- worth reading after any change that might have pushed
# it towards $9FFF.
$(ELF): $(OBJS)
	ln6502 $(LDFLAGS) --cstack-size $(CSTACK) --heap-size $(HEAP) \
	    --list-file $(BUILD)/hexgame.lst -o $@ $(LINKFILE) $(OBJS)

# -o names the ELF; ln6502 writes the PRG beside it under the same stem. It is
# copied out from under that name because the packed file below is what gets to
# be called hexgame.prg.
$(GAME): $(ELF)
	cp $(BUILD)/hexgame.prg $@

# ------------------------------------------------------------ the one file
#
# Stage 1: the decruncher, its table, and the boot stub that hands over to it.
# Three small links of their own rather than part of the game, because they run
# at different addresses and at a different time -- see src/stage1.c.
# stage1.o, stage1_tab.o and boot.o come out of the pattern rules above --
# src/ is on the vpath. s1_memory.o needs its own, only because it is the same
# memory.c the game links and must not collide with the game's object.
S1_OBJS  = $(BUILD)/stage1_tab.o $(BUILD)/stage1.o $(BUILD)/s1_memory.o

$(BUILD)/s1_memory.o: mega65-libc-modified/src/memory.c $(HDRS) Makefile | $(BUILD)
	cc6502 $(CFLAGS) -c -o $@ $<

$(BUILD)/stage1.raw: $(S1_OBJS) stage1.scm
	ln6502 $(TARGET) --output-format=raw --cstack-size 256 --heap-size 0 \
	    --list-file $(BUILD)/stage1.lst -o $(BUILD)/stage1.elf \
	    stage1.scm $(S1_OBJS)

$(BUILD)/boot.raw: $(BUILD)/boot.o boot.scm
	ln6502 $(TARGET) --no-tree-shaking --no-auto-libraries \
	    --no-data-init-table-section --output-format raw \
	    -o $(BUILD)/boot.elf boot.scm $(BUILD)/boot.o

# The packer, which also checks its own work: it decrunches both streams back
# and compares, and proves that unpacking the game over the top of its own
# compressed bytes cannot overrun. See tools/mkprg.py.
$(PRG): $(GAME) $(RES) $(BUILD)/stage1.raw $(BUILD)/boot.raw tools/mkprg.py
	python3 tools/mkprg.py --boot $(BUILD)/boot.raw \
	    --stage1 $(BUILD)/stage1.raw --stage1-addr $(STAGE1_ADDR) \
	    --game $(GAME) --fci $(RES) --fci-addr $(FCI_ADDR) -o $@

# ---------------------------------------------------------------- resources

# The tiles. png2fci wants pypng, which a clone need not have: res/hexgame.fci
# is checked in and this rule only runs when img-src/hexgame.png is newer.
# -0 keeps palette entry 0 and -r reserves the system palette entries, which is
# what leaves the source PNG its 239 usable colours (assets/tile/readme.txt).
res/%.fci: img-src/%.png tools/png2fci.py
	python3 tools/png2fci.py -v0r $< $@

# The file to hand out, which is the one the README tells people to run and the
# one that is checked in. Kept out of $(PRG)'s own rule so that an ordinary
# build does not rewrite a file under version control every time.

release: $(PRG)
	mkdir -p $(dir $(RELEASE))
	cp $(PRG) $(RELEASE)/hexgame.prg
	c1541 -format "hexgame,hg" d81 $(RELEASE)/hexgame.d81 -write $(RELEASE)/hexgame.prg hexgame
	@echo "release: $(RELEASE)/hexgame.prg"

clean:
	rm -rf $(BUILD)

.PHONY: all run game release checkmusic splint clean
