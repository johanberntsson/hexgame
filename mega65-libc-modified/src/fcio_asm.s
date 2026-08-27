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
; **That last step is a toggle, not a "switch it off", and it outlives the
; program.** Hypervisor function $70 flips the flag; nothing in a KERNAL reset
; puts it back. A run that leaves the flag lifted therefore hands the *next*
; run a machine where fc_bank_out_rom turns write protection back **on**, and
; then every write into $20000-$3FFFF is quietly dropped -- which, in this
; game, means the tile data for a band of the screen never arrives and the
; cells there show leftover ROM bytes as pixels. So it is a separate,
; published routine: quit_to_basic() in src/hexgame.c calls it a second time
; on the way out, and the two calls have to stay paired.
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
            .public fc_toggle_rom_write_protect

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
            ; fall through: lift the write protection from the ROM area

; Flip the ROM area's write protection. Call it once to make $20000-$3FFFF
; writable and once more to put it back; see the note above about pairing.
fc_toggle_rom_write_protect:
            lda     #0x70           ; Hypervisor trap: toggle ROM write protect
            sta     0xd640
            nop                     ; the trap needs this
            rts
