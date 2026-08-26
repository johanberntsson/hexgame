;;; The game's memory map: the stock mega65-plain.scm with the program's BSS
;;; moved out of the program area, and nothing else held back.
;;;
;;; **The program runs to $9FFF again.** It used to stop at $8FFF because the
;;; tune was a relocated SID loaded off the disk into the 4 KB above it, and
;;; the linker had to be kept out of a hole it could not see. The tune is now
;;; assembled into the program by tools/acme2calypsi.py (music/player.asm, via
;;; build/music_asm.s), so there is no hole to hold back and no ordering rule
;;; about when it may be loaded. Player and tune together are about 3 KB of the
;;; 32 this leaves, and the linker says how much is left -- read
;;; build/hexgame.lst after anything that might have grown it.
;;;
;;; **Zero page runs to $7F again for the same reason.** The relocated SID
;;; needed four bytes of its own at $7C-$7F, put there by hand with sidreloc.
;;; The ACME player picks its two pointers by hand as well, at $fb, which is a
;;; thing a C64 program may do and a program sharing zero page with a C
;;; compiler and a live KERNAL may not -- so the converter's --zp drops those
;;; constants and puts the names in a `zzpage` bss section instead, which the
;;; linker places in whatever it has left down here.
;;;
;;; **`zdata` is in the free RAM below the program, not in it.** The whole of
;;; the program's BSS -- about 1.5 KB -- lives at $1600-$1EFF, which the stock
;;; rules already declare as free space (it is where `zpsave` would go, and
;;; this program has none). It had to move: the 28 KB at $2001 was 98.9% full
;;; and there was no room left to add anything. BSS is not in the PRG, so
;;; nothing has to load it, and `load_resources()` does its disk I/O with it
;;; live -- the C65 KERNAL leaves that span alone. `cstack` and `heap` stay in
;;; the program area, where the linker puts them after the code.
;;;
;;; **$A000 and up are left alone.** $A000-$BFFF is the BASIC ROM, $C000-$CFFF
;;; the C65's interface ROM and $E000 the KERNAL. The game reads every one of
;;; its resources through the KERNAL before it touches the memory map at all
;;; (load_resources in src/hexgame.c), and after that its interrupt is its own.
;;; This is why the music no longer runs at $C000, where the cc65 build had it:
;;; that build was a C64-mode program behind a wrapper, and had the RAM there.
(define memories
  '((memory program
            (address (#x2001 . #x9fff)) (type any)
            (section (programStart #x2001) (startup #x200e)))
    (memory zeroPage (address (#x2 . #x7f)) (type ram) (qualifier zpage)
            (section (registers #x2)))
    (memory stackPage (address (#x100 . #x1ff)) (type ram))
    (memory freeSpace (address (#x1600 . #x1eff)) (section zpsave zdata))
    ))
