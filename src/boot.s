; The first thing in hexgame.prg: a BASIC line, and the twenty bytes that get
; out from under the ROM so that stage 1 can run.
;
; **This is the only part of the file the machine loads and runs by itself.**
; Everything else -- the two compressed streams and stage 1 above them -- is
; data until this jumps into it. It has to be here at $2001 because that is
; where a C65 BASIC program starts, and it has to be tiny because the game is
; unpacked over the top of it.
;
; The banking is the same sequence as fc_bank_out_rom in
; mega65-libc-modified/src/fcio_asm.s, and for the same reason: the streams run
; up past $A000, and with the C65 ROM mapped there is BASIC there, not the
; bytes that were loaded. Clearing the MAP offsets and putting $36 in $01 hands
; the whole of $A000-$CFFF back as RAM, leaves I/O in for the DMA and leaves a
; KERNAL at $E000 for the game's raster interrupt to chain through later.
;
; SEI, and it stays set. $0314 points into a C65 ROM that is no longer mapped
; from the store to $01 onwards, and the game does not enable interrupts again
; until enter_tile_mode() has put its own handler there.

STAGE1_ENTRY: .equ 0xb820            ; must agree with stage1.scm

            .section code,text

; 10 SYS 8206  -- $200E, which is where the code below starts
            .word   nextline
            .word   10
            .byte   0x9e            ; SYS
            .byte   0x20
            .byte   '8', '2', '0', '6'
            .byte   0
nextline:   .word   0

            sei
            lda     #0
            tax
            tay
            taz
            map
            lda     #0x36           ; BASIC out, KERNAL in, I/O in
            sta     0x01
            lda     #0x47           ; MEGA65 I/O knock, for the DMA controller
            sta     0xd02f
            lda     #0x53
            sta     0xd02f
            eom

            ; **And the interface ROM, which $01 has nothing to do with.**
            ; $D030 is the C65's own ROM banking and it is not part of the C64
            ; configuration $01 selects: bit 5 maps the interface ROM over
            ; $C000-$CFFF whatever else has been asked for. Stage 1 lives at
            ; $B800 and runs up into $C000 -- its C stack was landing in ROM,
            ; the first function call with a local in it had nowhere to put it,
            ; and nothing happened at all. The other ROM bits here are already
            ; clear; bits 0-2 are CRAM2K, EXTSYNC and PAL and are the VIC's.
            lda     0xd030
            and     #0xdf
            sta     0xd030

            jmp     STAGE1_ENTRY
