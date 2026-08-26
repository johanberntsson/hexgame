; ===========================================================================
;  sfx.asm -- two sound effects, played on a voice borrowed from the tune
;
;  The game has two noises to make: a click when a stone goes down, and a
;  buzz when the player aims at a hexagon that is already taken.  They used
;  to be 8 bit samples fed to the MEGA65's audio DMA -- 13.5 KB of disk and
;  two more files to load -- and they are 24 bytes of parameters here.
;
;  Call sfx_start with the effect number in A, and sfx_tick once per frame
;  from the same interrupt that calls music_play, AFTER music_play and
;  whether or not the tune is running.  There is no sfx_init: sfx_start sets
;  up everything an effect needs.
;
;  **An effect borrows one of the tune's three voices.**  The SID has three
;  and the tune uses all three, so an effect takes one for as long as it
;  lasts and hands it back.  The player checks sfx_owner at each of the five
;  places it writes the SID and skips the write -- it goes on tracking that
;  voice's pattern, arpeggio and slide exactly as before, so nothing drifts
;  out of time; only the register writes are lost.  When the effect ends the
;  voice is left gated off and the tune picks it up again at its next note,
;  which is at most one row (8 frames) away.
;
;  **Voice 1, the pizzicato bass, is the one lent out.**  Voice 2 is the
;  harp, the continuous 1/16 note figure the whole tune is built on, and
;  voice 3 is the melody; losing a fifth of a second of either is heard as a
;  mistake, while the bass is a plucked note that has already decayed most of
;  the way by the time an effect is over.
; ===========================================================================

SFX_VOICE       = 0             ; which voice an effect borrows, 0..2
SFX_OFS         = 0             ; and its SID register offset: 0, 7 or 14.
                                ; Written out because the converter reads
                                ; `*` in a constant as the program counter,
                                ; so SFX_VOICE*7 is not available here --
                                ; keep the two in step by hand.

SFX_NONE        = $ff           ; sfx_owner when no effect is playing

NUM_SFX         = 2
SFX_CLICK       = 0             ; a stone going down
SFX_BUZZ        = 1             ; that hexagon is taken


; ---------------------------------------------------------------------------
;  the effects, as parallel tables -- one column each, the way the
;  instruments in music.asm are laid out.
;
;  wave      waveform with the gate bit set: $11 triangle, $21 sawtooth,
;            $41 pulse, $81 noise
;  ad/sr     the envelope, straight into SID registers 5 and 6
;  pw        pulse width, $0800 being a square wave; ignored by noise
;  f         starting frequency, x $0596 Hz -- $0700 is about 107 Hz
;  d         added to f every frame, signed: a downward sweep is negative
;  alt       added to the HIGH byte of f on every other frame, which is a
;            15 Hz wobble per unit at 25 Hz.  0 for a clean tone; this is
;            what makes a buzz buzz rather than beep
;  len       frames the gate is held open
;  tail      frames to go on after it closes, before the voice is handed
;            back.  Long enough for the release, or the note is cut off.
;
;  CLICK is noise with a fast decay swept downwards -- a stone on a board
;  rather than a beep.  BUZZ is a square wave low enough to be unpleasant,
;  held flat at full sustain and wobbled: the classic wrong-answer buzzer.
;
;  These were written and not listened to.  Every one of them is one number
;  in one place, so the way to tune the sound is to change it here and run
;  the game -- start with `wave` and `f`, which decide what it *is*, before
;  the envelope, which only decides how long it lasts.
; ---------------------------------------------------------------------------
;                       click  buzz
sfx_wave        !byte   $81,   $41
sfx_ad          !byte   $03,   $00
sfx_sr          !byte   $00,   $f0
sfx_pwlo        !byte   $00,   $00
sfx_pwhi        !byte   $08,   $08
sfx_flo         !byte   $00,   $00
sfx_fhi         !byte   $30,   $07
sfx_dlo         !byte   $00,   $f0     ; -$0400 a frame          -$0010
sfx_dhi         !byte   $fc,   $ff
sfx_alt         !byte   $00,   $01
sfx_len         !byte   $02,   $10
sfx_tail        !byte   $04,   $02


; ---------------------------------------------------------------------------
;  sfx_start -- A = effect number.  Set an effect going; the next sfx_tick
;  starts it.  Called from the game, not from the interrupt.
;
;  Nothing is written to the SID here.  The interrupt may land in the middle
;  of this, and a half-set-up effect that has not claimed its voice yet is
;  harmless -- sfx_owner is stored last, and until it is, sfx_tick does
;  nothing and the tune still owns the voice.  Setting the registers here as
;  well would leave a window where the two of them were both writing it.
;
;  An effect already playing is replaced rather than queued.  These are two
;  pieces of feedback about what the player just did, and the newer one is
;  the one worth hearing.
; ---------------------------------------------------------------------------
sfx_start:
                cmp #NUM_SFX
                bcs .sfxbad             ; out of range: say nothing
                tax

                lda sfx_flo,x
                sta sfx_f_l
                lda sfx_fhi,x
                sta sfx_f_h
                lda sfx_dlo,x
                sta sfx_d_l
                lda sfx_dhi,x
                sta sfx_d_h
                lda sfx_pwlo,x
                sta sfx_p_l
                lda sfx_pwhi,x
                sta sfx_p_h
                lda sfx_ad,x
                sta sfx_a_d
                lda sfx_sr,x
                sta sfx_s_r
                lda sfx_wave,x
                sta sfx_w
                lda sfx_alt,x
                sta sfx_a
                lda sfx_len,x
                sta sfx_ctr
                lda sfx_tail,x
                sta sfx_rel

                lda #0
                sta sfx_phase
                lda #2                  ; two frames of starting up: see
                sta sfx_new             ; sfx_tick

                lda #SFX_VOICE          ; last, and the voice is ours
                sta sfx_owner
.sfxbad         rts


; ---------------------------------------------------------------------------
;  sfx_tick -- once per frame, after music_play.
; ---------------------------------------------------------------------------
sfx_tick:
                lda sfx_owner
                bmi .sfxidle            ; SFX_NONE: nothing playing
                ldy #SFX_OFS

                lda sfx_new
                beq .sfxrun
                dec sfx_new
                beq .sfxgo

                ; --- frame one: the hard restart.  The tune may have left
                ; this voice gated on, and an envelope only triggers on a
                ; 0 -> 1 edge of the gate bit: writing a waveform with the
                ; gate already high changes the tone and starts no note at
                ; all.  Worse, in the sustain phase a higher sustain does not
                ; raise the envelope, so the effect would be silent.  Drop it
                ; all to zero and open the gate next frame.
                lda #0
                sta SID+4,y
                sta SID+5,y
                sta SID+6,y
.sfxidle        rts

                ; --- frame two: set the voice up and let it go
.sfxgo          lda sfx_f_l
                sta SID+0,y
                lda sfx_f_h
                sta SID+1,y
                lda sfx_p_l
                sta SID+2,y
                lda sfx_p_h
                sta SID+3,y
                lda sfx_a_d
                sta SID+5,y
                lda sfx_s_r
                sta SID+6,y
                lda #$0f                ; the tune sets this once and never
                sta SID+$18             ; touches it again, and it is left at
                                        ; zero when the tune has been switched
                                        ; off -- so an effect sets it itself
                lda sfx_w
                sta SID+4,y             ; waveform, gate on
                rts

                ; --- every frame after that
.sfxrun         jsr sfx_sweep

                lda sfx_ctr
                beq .sfxfade

                dec sfx_ctr             ; still holding the note
                bne .sfxdone
                lda sfx_w               ; the frame the gate closes
                and #$fe
                sta SID+4,y
                rts

.sfxfade        dec sfx_rel
                bne .sfxdone
                lda #SFX_NONE           ; done: hand the voice back, silent,
                sta sfx_owner           ; for the tune to retake at its next
                lda #0                  ; note
                sta SID+4,y
                sta SID+5,y
                sta SID+6,y
.sfxdone        rts


; ---------------------------------------------------------------------------
;  sfx_sweep -- one step of the frequency, written to the SID.  Y is the
;  voice's register offset.  Runs through the fade as well as the hold, so
;  an effect goes on sliding while it releases.
; ---------------------------------------------------------------------------
sfx_sweep:
                clc
                lda sfx_f_l
                adc sfx_d_l
                sta sfx_f_l
                lda sfx_f_h
                adc sfx_d_h
                sta sfx_f_h

                lda sfx_f_h             ; the wobble is not accumulated: it
                ldx sfx_phase           ; is added to the frequency of every
                beq +                   ; other frame and taken off again
                clc
                adc sfx_a
+               pha
                lda #1
                eor sfx_phase
                sta sfx_phase

                lda sfx_f_l
                sta SID+0,y
                pla
                sta SID+1,y
                rts


; ---------------------------------------------------------------------------
;  effect state
; ---------------------------------------------------------------------------
sfx_owner       !byte SFX_NONE          ; the voice on loan, or SFX_NONE
sfx_new         !byte 0                 ; 2, 1 = the two starting frames
sfx_ctr         !byte 0                 ; frames left with the gate open
sfx_rel         !byte 0                 ; frames left after it closes
sfx_phase       !byte 0                 ; 0/1, which side of the wobble
sfx_f_l         !byte 0                 ; the running frequency
sfx_f_h         !byte 0
sfx_d_l         !byte 0                 ; and what is added to it per frame
sfx_d_h         !byte 0
sfx_p_l         !byte 0
sfx_p_h         !byte 0
sfx_a_d         !byte 0
sfx_s_r         !byte 0
sfx_w           !byte 0
sfx_a           !byte 0
