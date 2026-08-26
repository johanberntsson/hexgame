; ===========================================================================
;  player.asm -- small 3 voice SID replay routine
;
;  Call music_init once with the song number in A, then music_play exactly
;  once per frame (PAL, 50 Hz) from a raster IRQ.  The routine takes roughly
;  3-6 raster lines.
;
;  Entry points are the first two instructions so that the whole block can be
;  wrapped in a PSID header (init = base+0, play = base+3).  PSID passes the
;  subtune number in A too, so multi song works without a shim.
; ===========================================================================

SID             = $d400

NUM_VOICES      = 3
NUM_SONGS       = 1             ; 0 = "Gambit"

                                ; frames per pattern row (row = 1/16 note) is
                                ; per song, see song_speed in music.asm:
                                ; 50 / speed rows per second, x 60 / 4 = BPM

; --- pattern row, byte 0 ---------------------------------------------------
CMD_NONE        = $00           ; let the current note ring on
CMD_OFF         = $60           ; gate off -> release phase
CMD_END         = $ff           ; end of pattern, pull next one from the order
NOTE_MAX        = $54           ; 84 playable notes (C-0 .. B-6)

; --- order list ------------------------------------------------------------
ORD_JMP         = $ff           ; $ff,pos = jump to order position pos

KEEP            = $ff           ; row byte 1: keep the current instrument

ZP_PTR          = $fb           ; pattern read pointer
ZP_ARP          = $fd           ; arpeggio read pointer


; ---------------------------------------------------------------------------
;  jump table -- must stay at the very start of the block
; ---------------------------------------------------------------------------
                jmp music_init          ; base + 0
                jmp music_play          ; base + 3


; ---------------------------------------------------------------------------
;  music_init -- A = song number.  Select the song, silence the SID and
;  rewind all three voices.  Safe to call again to switch songs.
; ---------------------------------------------------------------------------
music_init:
                cmp #NUM_SONGS
                bcc +
                lda #0                  ; out of range: fall back to song 0
+               tax
                lda song_speed,x
                sta speed
                lda song_ord,x          ; where this song's three order
                sta tmp1                ; lists start in song_ord_lo/hi

                ldx #NUM_VOICES-1       ; no multiply: song_ord is the base
.ord            txa                     ; index, the voice number the offset
                clc
                adc tmp1
                tay
                lda song_ord_lo,y
                sta ord_lo,x
                lda song_ord_hi,y
                sta ord_hi,x
                dex
                bpl .ord

                lda #0
                ldx #$18
-               sta SID,x
                dex
                bpl -

                ldx #NUM_VOICES-1
.voice          lda #0
                sta v_ordpos,x
                sta v_wave,x
                sta v_wave2,x
                sta v_wdelay,x
                sta v_instr,x
                sta v_note,x
                sta v_pwd,x
                sta v_sl_l,x
                sta v_sl_h,x
                sta v_slacc_l,x
                sta v_slacc_h,x
                sta v_vibdep,x
                sta v_vibacc_l,x
                sta v_vibacc_h,x
                sta v_vibpos,x
                sta v_arppos,x
                lda #$ff
                sta v_arp,x
                jsr set_pattern
                dex
                bpl .voice

                lda #$0f
                sta SID+$18             ; volume, filter off
                lda #1
                sta tick                ; first call lands on row 0
                rts


; ---------------------------------------------------------------------------
;  music_play -- call once per frame
; ---------------------------------------------------------------------------
music_play:
                dec tick
                bne .norow

                lda speed               ; row boundary: fetch a new row
                sta tick
                ldx #NUM_VOICES-1
-               jsr do_row
                dex
                bpl -
                jmp .effects

.norow          lda tick                ; one frame before the next row:
                cmp #1                  ; hard restart the voices that are
                bne .effects            ; about to retrigger
                ldx #NUM_VOICES-1
-               jsr hard_restart
                dex
                bpl -

.effects        ldx #NUM_VOICES-1
-               jsr do_effects
                dex
                bpl -
                rts


; ---------------------------------------------------------------------------
;  set_pattern -- X = voice.  Pull the next pattern number from the order
;  list and point the voice at it.
; ---------------------------------------------------------------------------
set_pattern:
                lda ord_lo,x
                sta ZP_PTR
                lda ord_hi,x
                sta ZP_PTR+1
-               ldy v_ordpos,x
                lda (ZP_PTR),y
                cmp #ORD_JMP
                bne +
                iny                     ; $ff,pos -> restart at pos
                lda (ZP_PTR),y
                sta v_ordpos,x
                jmp -
+               inc v_ordpos,x
                tay
                lda patt_lo,y
                sta v_pat_l,x
                lda patt_hi,y
                sta v_pat_h,x
                rts


; ---------------------------------------------------------------------------
;  set_ptr -- X = voice.  ZP_PTR = address of the current row.
; ---------------------------------------------------------------------------
set_ptr:
                lda v_pat_l,x
                sta ZP_PTR
                lda v_pat_h,x
                sta ZP_PTR+1
                rts


; ---------------------------------------------------------------------------
;  next_row -- X = voice.  Step one row on, skipping straight over an end
;  marker so that the row ahead is always a real row (the hard restart peeks
;  at it).
; ---------------------------------------------------------------------------
next_row:
                lda v_pat_l,x
                clc
                adc #2
                sta v_pat_l,x
                bcc +
                inc v_pat_h,x
+               jsr set_ptr
                ldy #0
                lda (ZP_PTR),y
                cmp #CMD_END
                bne +
                jsr set_pattern
+               rts


; ---------------------------------------------------------------------------
;  hard_restart -- X = voice.  If the next row starts a note, drop the
;  envelope to zero now so the attack is clean and always in phase.
; ---------------------------------------------------------------------------
hard_restart:
                jsr set_ptr
                ldy #0
                lda (ZP_PTR),y
                beq .done               ; CMD_NONE
                cmp #NOTE_MAX+1
                bcs .done               ; CMD_OFF / CMD_END
                ldy sid_ofs,x
                lda #0
                sta SID+5,y             ; AD = 0000
                sta SID+6,y             ; SR = 0000
                lda v_wave,x
                and #$fe                ; gate off
                sta SID+4,y
.done           rts


; ---------------------------------------------------------------------------
;  do_row -- X = voice.  Act on the current row, then step on.
; ---------------------------------------------------------------------------
do_row:
                jsr set_ptr
                ldy #0
                lda (ZP_PTR),y
                beq .adv                ; CMD_NONE
                cmp #CMD_OFF
                beq .off
                cmp #NOTE_MAX+1
                bcs .adv                ; anything unknown: ignore

                sec                     ; note: 1..84 -> index 0..83
                sbc #1
                sta v_note,x
                ldy #1
                lda (ZP_PTR),y
                cmp #KEEP
                beq +
                sta v_instr,x
+               jsr note_on
                jmp .adv

.off            lda v_wave,x
                and #$fe
                sta v_wave,x
                ldy sid_ofs,x
                sta SID+4,y

.adv            jmp next_row


; ---------------------------------------------------------------------------
;  note_on -- X = voice.  Copy the instrument into the voice state and start
;  the note.
; ---------------------------------------------------------------------------
note_on:
                ldy v_instr,x
                lda ins_wave,y
                sta v_wave,x
                lda ins_wave2,y
                sta v_wave2,x
                lda ins_wdelay,y
                sta v_wdelay,x
                lda ins_pwlo,y
                sta v_pw_l,x
                lda ins_pwhi,y
                sta v_pw_h,x
                lda ins_pwd,y
                sta v_pwd,x
                lda ins_sl_l,y
                sta v_sl_l,x
                lda ins_sl_h,y
                sta v_sl_h,x
                lda ins_arp,y
                sta v_arp,x
                lda ins_vibdep,y
                sta v_vibdep,x
                lda ins_vibdel,y
                sta v_vibdel,x
                lda ins_ad,y
                sta tmp1
                lda ins_sr,y
                sta tmp2

                lda #0                  ; reset the per note effect state
                sta v_slacc_l,x
                sta v_slacc_h,x
                sta v_vibacc_l,x
                sta v_vibacc_h,x
                sta v_vibpos,x
                sta v_arppos,x

                ldy v_note,x            ; base pitch, so the gate never opens
                lda freq_lo,y           ; on the pitch of the previous note
                sta tmp3
                lda freq_hi,y
                sta tmp4

                ldy sid_ofs,x
                lda tmp3
                sta SID+0,y
                lda tmp4
                sta SID+1,y
                lda v_pw_l,x
                sta SID+2,y
                lda v_pw_h,x
                sta SID+3,y
                lda tmp1
                sta SID+5,y             ; attack / decay
                lda tmp2
                sta SID+6,y             ; sustain / release
                lda v_wave,x
                sta SID+4,y             ; waveform + gate on
                rts


; ---------------------------------------------------------------------------
;  do_effects -- X = voice.  Runs every frame: waveform switch, arpeggio,
;  pitch slide, vibrato, pulse sweep.
; ---------------------------------------------------------------------------
do_effects:
                lda v_wdelay,x          ; --- second waveform (noise attack)
                beq .wdone
                dec v_wdelay,x
                bne .wdone
                lda v_wave2,x
                sta v_wave,x
                ldy sid_ofs,x
                sta SID+4,y

.wdone          ldy v_note,x            ; --- arpeggio
                lda v_arp,x
                cmp #$ff
                beq .freq
                tay
                lda arp_lo,y
                sta ZP_ARP
                lda arp_hi,y
                sta ZP_ARP+1
                ldy v_arppos,x
                lda (ZP_ARP),y
                bpl +
                ldy #0                  ; $80 = loop back to the start
                lda (ZP_ARP),y
+               sta tmp1
                iny
                tya
                sta v_arppos,x
                lda tmp1
                clc
                adc v_note,x
                tay

.freq           lda freq_lo,y           ; --- base pitch
                sta tmp1
                lda freq_hi,y
                sta tmp2

                lda v_sl_l,x            ; --- pitch slide (drums, effects)
                ora v_sl_h,x
                beq .slide
                clc
                lda v_slacc_l,x
                adc v_sl_l,x
                sta v_slacc_l,x
                lda v_slacc_h,x
                adc v_sl_h,x
                sta v_slacc_h,x

.slide          clc
                lda tmp1
                adc v_slacc_l,x
                sta tmp1
                lda tmp2
                adc v_slacc_h,x
                sta tmp2
                lda v_slacc_h,x         ; clamp instead of wrapping around
                bmi .down
                bcc .vib                ; sliding up, no overflow
                lda #$ff
                sta tmp1
                sta tmp2
                bne .vib                ; always
.down           bcs .vib                ; sliding down, no underflow
                lda #0
                sta tmp1
                sta tmp2

.vib            lda v_vibdep,x          ; --- vibrato
                beq .write
                lda v_vibdel,x
                beq +
                dec v_vibdel,x
                jmp .write
+               lda v_vibpos,x
                clc
                adc #1
                and #$0f
                sta v_vibpos,x
                cmp #4                  ; 0-3 up, 4-11 down, 12-15 up
                bcc .up
                cmp #12
                bcs .up
                sec
                lda v_vibacc_l,x
                sbc v_vibdep,x
                sta v_vibacc_l,x
                lda v_vibacc_h,x
                sbc #0
                sta v_vibacc_h,x
                jmp +
.up             clc
                lda v_vibacc_l,x
                adc v_vibdep,x
                sta v_vibacc_l,x
                lda v_vibacc_h,x
                adc #0
                sta v_vibacc_h,x
+               clc
                lda tmp1
                adc v_vibacc_l,x
                sta tmp1
                lda tmp2
                adc v_vibacc_h,x
                sta tmp2

.write          lda v_pwd,x             ; --- pulse width sweep
                beq .out
                bmi +
                clc                     ; sweeping up
                lda v_pw_l,x
                adc v_pwd,x
                sta v_pw_l,x
                lda v_pw_h,x
                adc #0
                sta v_pw_h,x
                jmp .limit
+               clc                     ; sweeping down (sign extended)
                lda v_pw_l,x
                adc v_pwd,x
                sta v_pw_l,x
                lda v_pw_h,x
                adc #$ff
                sta v_pw_h,x
.limit          lda v_pw_h,x            ; bounce between $0100 and $0e00
                beq +
                cmp #$0e
                bcc .out
+               lda #0
                sec
                sbc v_pwd,x
                sta v_pwd,x

.out            ldy sid_ofs,x
                lda tmp1
                sta SID+0,y
                lda tmp2
                sta SID+1,y
                lda v_pw_l,x
                sta SID+2,y
                lda v_pw_h,x
                and #$0f
                sta SID+3,y
                rts


; ---------------------------------------------------------------------------
;  note frequency table, PAL (985248 Hz).  84 notes, C-0 .. B-6.
;  Octave 0 is measured, the rest is a shift left per octave.
; ---------------------------------------------------------------------------
nf_c            = 278
nf_cs           = 295
nf_d            = 313
nf_ds           = 331
nf_e            = 351
nf_f            = 372
nf_fs           = 394
nf_g            = 417
nf_gs           = 442
nf_a            = 468
nf_as           = 496
nf_b            = 526

freq_lo         !for oct, 0, 6 {
                    !byte <(nf_c<<oct),  <(nf_cs<<oct), <(nf_d<<oct)
                    !byte <(nf_ds<<oct), <(nf_e<<oct),  <(nf_f<<oct)
                    !byte <(nf_fs<<oct), <(nf_g<<oct),  <(nf_gs<<oct)
                    !byte <(nf_a<<oct),  <(nf_as<<oct), <(nf_b<<oct)
                }
freq_hi         !for oct, 0, 6 {
                    !byte >(nf_c<<oct),  >(nf_cs<<oct), >(nf_d<<oct)
                    !byte >(nf_ds<<oct), >(nf_e<<oct),  >(nf_f<<oct)
                    !byte >(nf_fs<<oct), >(nf_g<<oct),  >(nf_gs<<oct)
                    !byte >(nf_a<<oct),  >(nf_as<<oct), >(nf_b<<oct)
                }

sid_ofs         !byte 0, 7, 14


; ---------------------------------------------------------------------------
;  the tune: instruments, patterns and order lists
; ---------------------------------------------------------------------------
                !source "music.asm"


; ---------------------------------------------------------------------------
;  player state
; ---------------------------------------------------------------------------
tick            !byte 0
speed           !byte 0                 ; frames per row, from song_speed
tmp1            !byte 0
tmp2            !byte 0
tmp3            !byte 0
tmp4            !byte 0

ord_lo          !fill NUM_VOICES, 0     ; order list of the selected song
ord_hi          !fill NUM_VOICES, 0
v_ordpos        !fill NUM_VOICES, 0     ; position in the order list
v_pat_l         !fill NUM_VOICES, 0     ; pointer to the current row
v_pat_h         !fill NUM_VOICES, 0
v_note          !fill NUM_VOICES, 0     ; note index 0..83
v_instr         !fill NUM_VOICES, 0
v_wave          !fill NUM_VOICES, 0     ; waveform as last written to the SID
v_wave2         !fill NUM_VOICES, 0
v_wdelay        !fill NUM_VOICES, 0
v_pw_l          !fill NUM_VOICES, 0
v_pw_h          !fill NUM_VOICES, 0
v_pwd           !fill NUM_VOICES, 0
v_sl_l          !fill NUM_VOICES, 0     ; pitch slide per frame, signed
v_sl_h          !fill NUM_VOICES, 0
v_slacc_l       !fill NUM_VOICES, 0
v_slacc_h       !fill NUM_VOICES, 0
v_arp           !fill NUM_VOICES, 0
v_arppos        !fill NUM_VOICES, 0
v_vibdep        !fill NUM_VOICES, 0
v_vibdel        !fill NUM_VOICES, 0
v_vibpos        !fill NUM_VOICES, 0
v_vibacc_l      !fill NUM_VOICES, 0
v_vibacc_h      !fill NUM_VOICES, 0
