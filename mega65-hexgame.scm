;;; The game's memory map: the stock mega65-plain.scm with the top 4 KB of the
;;; program area held back for the music, and the top of zero page held back
;;; for the same.
;;;
;;; **The program stops at $8FFF, not $9FFF.** The tune is a relocated SID
;;; loaded off the disk at run time (assets/music/music.sid: load $9000, init
;;; $9000, play $9059), so the space it lands in has to be space the linker
;;; never places anything in. There is no section here for it on purpose: a
;;; hole the linker does not know about cannot be filled by accident, and a BSS
;;; section big enough to reserve it would be 4 KB of startup zeroing for
;;; nothing. The build is at about 24 KB of the 28 this leaves, and the linker
;;; says so -- read build/hexgame.lst after anything that might have grown it.
;;;
;;; **Zero page stops at $7B for the same reason.** A relocated SID needs four
;;; zero page bytes of its own and sidreloc was pointed at $7C-$7F, the top of
;;; what the tool chain would otherwise hand out; see the sidreloc invocation
;;; in assets/music/Makefile. The two ranges have to be changed together.
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
;;; This is why the music moved off $C000, where the cc65 build ran it: that
;;; build was a C64-mode program behind a wrapper, and had the RAM there.
(define memories
  '((memory program
            (address (#x2001 . #x8fff)) (type any)
            (section (programStart #x2001) (startup #x200e)))
    (memory zeroPage (address (#x2 . #x7b)) (type ram) (qualifier zpage)
            (section (registers #x2)))
    (memory stackPage (address (#x100 . #x1ff)) (type ram))
    (memory freeSpace (address (#x1600 . #x1eff)) (section zpsave zdata))
    ))
