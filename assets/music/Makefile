# The tune, prepared by hand and checked in: ../../res/music.prg is what the
# game loads, and the Makefile at the top of the tree only regenerates it when
# music.sid here is newer.
#
# Music.sid is the Armalyte tune from the High Voltage SID Collection, at the
# $C000 it was relocated to for the cc65 build. It is relocated again here:
#
#   -r c0-cf  the range it currently occupies, which sidreloc must be told
#   -p 90     the page it moves to -- the hole above the program that
#             ../../mega65-hexgame.scm holds back for it
#   -z 7c-7f  the four zero page bytes it may use, taken from the top of the
#             range the C compiler would otherwise hand out. The same file
#             stops the linker at $7B; the two have to agree.
#
# **Read sidreloc's warnings.** References it reports as out of range are not
# converted, and will write over the program at run time.
#
# psid64 -n emits the data with no player in front of it. src/music_irq.s
# calls init and play itself, at $9000 and $9059 -- if a different tune goes in
# here, those two addresses are what change.
SIDRELOC ?= ~/commodore/sidreloc/src/sidreloc
PSID64   ?= psid64

all: music.sid
	$(MAKE) -C ../.. res/music.prg

music.sid: Music.sid
	$(SIDRELOC) -v -r c0-cf -p 90 -z 7c-7f $< $@

# The samples, played by the MEGA65's audio DMA rather than the SID. Copied
# rather than converted: they are already raw 8-bit signed data.
samples:
	cp Marba.q11.wav ../../res/marba.wav
	cp DownLead.wav ../../res/downlead.wav

.PHONY: all samples
