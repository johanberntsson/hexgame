all:
	../../../sidreloc/src/sidreloc -r 08-14 -p c0 Model.sid themodel.sid
	../../../psid64/src/psid64 -n themodel.sid
	xxd -i themodel.prg > ../themodel.c
	xxd -i SynClaves > ../sample01.c
	xxd -i Snare1 > ../sample02.c

