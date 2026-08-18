; The tune, and the interrupt that drives it.
;
; The music is a SID relocated to $8000 and stripped of any player by
; `psid64 -n` (see the res/music.prg rule in the Makefile), so what is at
; $8000 is the tune's own init and play routines and nothing else. The two
; addresses below are that file's, and change with it.
;
; **$9000, where the cc65 build used $C000.** That build ran the game in C64
; mode behind a wrapper, and $C000 was free RAM. This one is a C65-mode program
; and $C000-$CFFF is the C65's interface ROM, so the tune moved down into the
; 4 KB the linker script holds back above the program. It also owns zero page
; $7C-$7F, which is held back the same way. See mega65-hexgame.scm.

MUSIC_INIT: .equ    0x9000
MUSIC_PLAY: .equ    0x9059

CINV:       .equ    0x0314          ; the KERNAL's IRQ vector, in RAM
KERNAL_IRQ: .equ    0xea31          ; where the C64 KERNAL's handler resumes

            .extern option_music    ; OPTION_MUSIC_ON is 0, OFF is 1

            .section code,text
            .public music_init, music_install, music_irq

; void music_init(uint8_t song) -- the song number arrives in A, which is
; where the tune's init routine wants it, so this is a jump and not a call.
music_init: jmp     MUSIC_INIT

; void music_install(void). Called once, after the ROM has been banked out and
; with interrupts already disabled by the caller -- $0314 holds the C65 ROM's
; handler until the store below, and that handler is no longer mapped.
;
; The CIA timer interrupt is switched off for a raster interrupt at line 140,
; which is what the cc65 build did: one interrupt a frame, at a line the board
; is never being drawn on.
music_install:
            lda     #.byte0 music_irq
            sta     CINV
            lda     #.byte1 music_irq
            sta     CINV+1

            lda     #0x7f
            sta     0xdc0d          ; CIA 1 timer interrupts off
            lda     0xd01a
            ora     #1
            sta     0xd01a          ; raster interrupt on
            lda     0xd011
            and     #0x7f           ; raster compare is a single byte
            sta     0xd011
            lda     #140
            sta     0xd012
            rts

; Fifty times a second. Reached through $0314 from the KERNAL, which has
; already saved the registers, so this may use them freely.
;
; It ends in the KERNAL's own handler rather than an RTI: that is what keeps
; the jiffy clock running and the stack balanced, and it is what the cc65 build
; did. The keyboard is not read through it -- the game takes keys straight off
; the MEGA65's $D610 -- so the only thing lost if this chain ever breaks is the
; clock.
music_irq:  lda     option_music
            bne     ack$            ; non-zero is OPTION_MUSIC_OFF
            jsr     MUSIC_PLAY

ack$:       lda     #0xff
            sta     0xd019          ; acknowledge the raster interrupt
            jmp     KERNAL_IRQ
