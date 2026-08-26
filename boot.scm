;;; The boot stub, at the address a C65 BASIC program starts. See src/boot.s.
;;;
;;; Linked on its own and emitted raw, because it is the only part of
;;; hexgame.prg that has to be at $2001 -- everything else in the file is data
;;; until it jumps. tools/mkprg.py puts the pieces together.
(define memories
  '((memory program (address (#x2001 . #x20ff)) (type any)
            (section (code #x2001)))
    (memory zeroPage (address (#x2 . #x7f)) (type ram) (qualifier zpage))
    ))
