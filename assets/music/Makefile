all:
	~/commodore/sidreloc/src/sidreloc -v -r c0-cf -p 80 Music.sid music.sid
	#~/commodore/sidreloc/src/sidreloc -v -z 02-03 -r 08-13 -p c0 Music.sid music.sid
	~/commodore/psid64/src/psid64 -n music.sid
	#sidplay2 -v music.prg
	cp music.prg ../../res
	cp Snare1 ../../res/sample1
	cp SynClaves ../../res/sample2
	xxd -i music.prg > ../../src/music.c
	xxd -i SynClaves > ../../src/sample01.c
	xxd -i Snare1 > ../../src/sample02.c

