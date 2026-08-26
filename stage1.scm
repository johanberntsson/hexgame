;;; Stage 1's memory map -- see src/stage1.c.
;;;
;;; **$B800 is above everything stage 1 writes, and that is the whole point.**
;;; It unpacks the tile sheet to attic RAM and then the game to $2001-$97FF,
;;; over the top of the compressed streams it is reading from, and then jumps
;;; into it. A stage 1 living at $2001 like an ordinary program would be
;;; unpacking the game over itself.
;;;
;;; It is also above the streams, which is what costs the padding tools/mkprg.py
;;; reports: the file runs from $2001 to the end of the streams, and then to
;;; here. The alternative is a stage 1 that relocates itself at run time, for
;;; the sake of a few hundred bytes of file.
;;;
;;; **The three constants in the Makefile have to agree with this file.**
;;; STAGE1_ADDR is where the packer pads to and where the table below lands,
;;; and STAGE1_ENTRY is what the boot stub jumps to. mkprg.py checks the size
;;; of what it is given against the first, and says so if the streams have
;;; grown into it.
;;;
;;; $D000 and up is I/O and the KERNAL, so $CFFF is the ceiling. What is left
;;; between here and there is about 5 KB, which is four times what this needs.
;;; The buffers are not in it: the window and the output buffer are at
;;; $0800-$1BFF, hardcoded in stage1.c, because that RAM is free until the game
;;; is running and nothing here should be competing with the streams for space.
(define memories
  '((memory program
            (address (#xb800 . #xcfff)) (type any)
            (section (bootinfo #xb800) (startup #xb820)))
    (memory zeroPage (address (#x2 . #x7f)) (type ram) (qualifier zpage)
            (section (registers #x2)))
    (memory stackPage (address (#x100 . #x1ff)) (type ram))
    ))
