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

ELF      = $(BUILD)/hexgame.elf
# The MEGA65 ROM autoboots a file called autoboot.c65 and nothing else.
PRG      = $(BUILD)/autoboot.c65
D81      = $(BUILD)/hexgame.d81

# Everything in res/ goes on the disk, and the names the game opens are these
# names -- see the loads in src/hexgame.c. They are all checked in, so a clone
# builds the disk without pypng or psid64; the rules below only fire when a
# source asset is actually newer than what it produces.
RES      = res/hexgame.fci res/music.prg res/marba.wav res/downlead.wav

all: $(D81)

run: $(D81)
	xemu-xmega65 -besure -8 $(D81)

# No disk, no resources: boots the program straight into memory. It gets as far
# as the first fopen and stops, which is enough to see that the build links and
# starts, and nothing more.
prg: $(PRG)
	xemu-xmega65 -besure -prg $(PRG)

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

# The list file is the memory map, and it is the only place the program's real
# size is written down -- worth reading after any change that might have pushed
# it towards $7FFF.
$(ELF): $(OBJS)
	ln6502 $(LDFLAGS) --cstack-size $(CSTACK) --heap-size $(HEAP) \
	    --list-file $(BUILD)/hexgame.lst -o $@ $(LINKFILE) $(OBJS)

# -o names the ELF; ln6502 writes the PRG beside it under the same stem.
$(PRG): $(ELF)
	cp $(BUILD)/hexgame.prg $@

$(D81): $(PRG) $(RES)
	tools/calypsi/builddisc.sh $@ $(PRG) $(RES)

# ---------------------------------------------------------------- resources

# The tiles. png2fci wants pypng, which a clone need not have: res/hexgame.fci
# is checked in and this rule only runs when img-src/hexgame.png is newer.
# -0 keeps palette entry 0 and -r reserves the system palette entries, which is
# what leaves the source PNG its 239 usable colours (assets/tile/readme.txt).
res/%.fci: img-src/%.png tools/png2fci.py
	python3 tools/png2fci.py -v0r $< $@

# The tune: a SID relocated to $8000 by sidreloc, turned into raw data at that
# address by psid64 -n. No driver -- the game calls init and play itself from
# src/music_irq.s, and the addresses there ($8000 and $8059) are this file's.
res/music.prg: assets/music/music.sid
	psid64 -n -o $@ $<

# The disk to hand out, which is the one the README tells people to run and the
# one that is checked in. Kept out of $(D81)'s own rule so that an ordinary
# build does not rewrite a file under version control every time.
RELEASE = disc/hexgame.d81

release: $(D81)
	mkdir -p $(dir $(RELEASE))
	cp $(D81) $(RELEASE)
	@echo "release: $(RELEASE)"

clean:
	rm -rf $(BUILD)

.PHONY: all run prg release splint clean
