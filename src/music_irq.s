; The interrupt that drives the tune.
;
; The tune and its player are ACME sources under music/, translated into
; build/music_asm.s by tools/acme2calypsi.py and linked into the program like
; any other object -- so `music_init` and `music_play` are ordinary symbols
; here, not addresses in a file loaded off the disk. `music_init` takes the
; song number in A, which is where Calypsi puts a byte argument, so C calls it
; directly and there is no shim in this file for it any more.
;
; What is left is the interrupt. There used to be a note here about the tune
; living at $9000 because $C000 is the C65's interface ROM; that is no longer a
; question the program has to answer -- the linker places the player wherever
; it likes inside the program.

CINV:       .equ    0x0314          ; the KERNAL's IRQ vector, in RAM
KERNAL_IRQ: .equ    0xea31          ; where the C64 KERNAL's handler resumes

            .extern option_music    ; OPTION_MUSIC_ON is 0, OFF is 1
            .extern music_play      ; build/music_asm.s, from music/player.asm
            .extern sfx_tick        ; ... and music/sfx.asm

            .section code,text
            .public music_install, music_irq

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
;
; **The sound effects are ticked whether or not the tune is playing, and after
; it.** F1 switches the music off, not the game's own noises; and an effect
; borrows one of the tune's voices, so it has to have the last word on that
; voice's registers in any frame where both of them run. See music/sfx.asm.
music_irq:  lda     option_music
            bne     sfx$            ; non-zero is OPTION_MUSIC_OFF
            jsr     music_play

sfx$:       jsr     sfx_tick

            lda     #0xff
            sta     0xd019          ; acknowledge the raster interrupt
            jmp     KERNAL_IRQ
