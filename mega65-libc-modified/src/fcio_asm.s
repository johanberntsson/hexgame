; Flatten the C65 memory map, so that $20000-$5FFFF is plain RAM the VIC-IV can
; be pointed at for unique tile data.
;
; **This used to be a hex dump.** cc65's assembler has no 45GS02 opcodes, so
; the C file carried this routine as an array of bytes hand-assembled in ACME,
; with a comment apologising for it. Calypsi assembles MAP and EOM directly.
;
; What it does, in order: clear all four MAP offset registers so nothing is
; mapped and the C64-style $01 banking governs; put $01 in the configuration
; the rest of the run needs (BASIC out, KERNAL in, I/O in); do the MEGA65 I/O
; knock so the VIC-IV registers are reachable; and ask the Hypervisor to lift
; the write protection from the ROM area, which is what makes $20000 writable
; at all.
;
; **It leaves the machine in the C64 configuration**, which is why nothing may
; read a file after it: see the ordering note in main() in src/hexgame.c. The
; KERNAL that stays mapped is the C64 one at $2E000, so $2E000-$2FFFF is the
; one part of the freed area that must never be written -- fc_clearUniqueTiles
; in fcio.c is the code that knows that.
;
; Interrupts cannot be taken between MAP and EOM, but they can immediately
; after, and at that point $0314 still holds the C65 ROM's handler, which is no
; longer mapped. The caller runs with interrupts disabled across this and does
; not enable them again until its own handler is in the vector.

            .section code,text
            .public fc_bank_out_rom

fc_bank_out_rom:
            lda     #0
            tax
            tay
            taz
            map
            lda     #0x36           ; BASIC out, KERNAL in, I/O in
            sta     0x01
            lda     #0x47           ; MEGA65 I/O knock
            sta     0xd02f
            lda     #0x53
            sta     0xd02f
            eom
            lda     #0x70           ; Hypervisor trap: un-write-protect the ROM
            sta     0xd640
            nop                     ; the trap needs this
            rts
