all:
	../../../sidreloc/src/sidreloc -r 08-14 -p c0 Model.sid themodel.sid
	../../../psid64/src/psid64 -n themodel.sid
	xxd -i themodel.prg > ../../src/themodel.c
	xxd -i SynClaves > ../../src/sample01.c
	xxd -i Snare1 > ../../src/sample02.c

