; ===========================================================================
;  music.asm -- the song bank
;
;  Everything outside the notes themselves: note names, the instrument
;  tables, the arpeggio tables, and the two index tables the player walks
;  (patterns and songs).  The song itself is one file:
;
;      gambit.asm     song 0, "Gambit"  -- board game music
;
;  Instrument and pattern numbers are global across all songs, so a second
;  song can reuse anything this one defines.  Adding a pattern means touching
;  three places: the P* constant, patt_lo/patt_hi, and the order list.
;
;  Rows are two bytes each: note, instrument.
;  Notes are written as n_<name> + o<octave>, e.g. n_fs+o4.
;  0,0 = let the previous note ring on.  Every pattern is 32 rows / 2 bars.
; ===========================================================================

; --- note names ------------------------------------------------------------
n_c  = 1
n_cs = 2
n_d  = 3
n_ds = 4
n_e  = 5
n_f  = 6
n_fs = 7
n_g  = 8
n_gs = 9
n_a  = 10
n_as = 11
n_b  = 12

o0 = 0
o1 = 12
o2 = 24
o3 = 36
o4 = 48
o5 = 60
o6 = 72

; --- instruments -----------------------------------------------------------
IPIZZ   = 0             ; pizzicato bass
IHARP   = 1             ; the 1/16 note broken chords
ILEAD   = 2             ; melody, A sections
IFLUTE  = 3             ; melody, B section
ITRILL  = 4             ; ILEAD with a trill, for the long held notes

; --- pattern numbers -------------------------------------------------------
PB_EM_B  = 0            ; bass
PB_G_A   = 1
PB_C_AM  = 2
PB_B7_EM = 3
PB_G_D   = 4
PB_EM_C  = 5
PB_AM_B7 = 6
PH_EM_B  = 7            ; harp
PH_G_A   = 8
PH_C_AM  = 9
PH_B7_EM = 10
PH_G_D   = 11
PH_EM_C  = 12
PH_AM_B7 = 13
PM_REST  = 14           ; melody
PM_A1    = 15
PM_A2    = 16
PM_A3    = 17
PM_A4    = 18
PM_A5    = 19
PM_A6    = 20
PM_A7    = 21
PM_A8    = 22
PM_B1    = 23
PM_B2    = 24
PM_B3    = 25
PM_B4    = 26

; guards against a mistyped pattern: 32 rows of 2 bytes plus the end marker
!macro endpat .start {
        !byte CMD_END
        !if * - .start != 65 {
                !error "pattern is not 32 rows long"
        }
}


; ---------------------------------------------------------------------------
;  instruments (parallel tables, indexed by instrument number)
; ---------------------------------------------------------------------------
;  Five columns, one per instrument, and every table is one line -- keep the
;  columns lined up and they stay readable as an instrument each.
;
;  IPIZZ  pulse, no attack, decay to nothing: a plucked string.
;  IHARP  a narrow pulse with an even shorter decay, so 1/16 notes stay
;         separate instead of turning into a drone: a harpsichord pluck.
;         Triangle here is prettier but lands ~9 dB under the lead, and this
;         figure is the engine of the tune, not decoration.
;  ILEAD  narrow pulse that opens out over the note, delayed vibrato.
;  IFLUTE square, slower attack, no pulse movement: hollow and rounder than
;         ILEAD, which is the whole point of the B section.
;  ITRILL ILEAD plus arp_trill.
;
;                 pizz   harp   lead   flute  trill
ins_ad    !byte   $09,   $05,   $19,   $39,   $19
ins_sr    !byte   $00,   $00,   $a9,   $c9,   $a9
ins_wave  !byte   $41,   $41,   $41,   $41,   $41
ins_wave2 !byte   $41,   $41,   $41,   $41,   $41
ins_wdelay!byte   $00,   $00,   $00,   $00,   $00
ins_pwlo  !byte   $00,   $00,   $00,   $00,   $00
ins_pwhi  !byte   $08,   $04,   $05,   $08,   $05
ins_pwd   !byte   $08,   $00,   $0c,   $00,   $0c
ins_sl_l  !byte   $00,   $00,   $00,   $00,   $00
ins_sl_h  !byte   $00,   $00,   $00,   $00,   $00
ins_arp   !byte   $ff,   $ff,   $ff,   $ff,   $00
ins_vibdep!byte   $00,   $00,   $04,   $05,   $00
ins_vibdel!byte   $00,   $00,   $10,   $14,   $00

; --- arpeggio tables, semitone offsets, $80 = loop back --------------------
;  One step per frame.  arp_trill holds each note for three frames, so the
;  whole tone alternates at about 8 Hz -- a trill rather than the ring
;  modulator buzz a one-frame table would give.
arp_lo    !byte <arp_trill
arp_hi    !byte >arp_trill

arp_trill !byte 0, 0, 0, 2, 2, 2, $80


; ---------------------------------------------------------------------------
;  pattern address table
; ---------------------------------------------------------------------------
patt_lo   !byte <pb_em_b, <pb_g_a, <pb_c_am, <pb_b7_em
          !byte <pb_g_d, <pb_em_c, <pb_am_b7
          !byte <ph_em_b, <ph_g_a, <ph_c_am, <ph_b7_em
          !byte <ph_g_d, <ph_em_c, <ph_am_b7
          !byte <pm_rest
          !byte <pm_a1, <pm_a2, <pm_a3, <pm_a4
          !byte <pm_a5, <pm_a6, <pm_a7, <pm_a8
          !byte <pm_b1, <pm_b2, <pm_b3, <pm_b4
patt_hi   !byte >pb_em_b, >pb_g_a, >pb_c_am, >pb_b7_em
          !byte >pb_g_d, >pb_em_c, >pb_am_b7
          !byte >ph_em_b, >ph_g_a, >ph_c_am, >ph_b7_em
          !byte >ph_g_d, >ph_em_c, >ph_am_b7
          !byte >pm_rest
          !byte >pm_a1, >pm_a2, >pm_a3, >pm_a4
          !byte >pm_a5, >pm_a6, >pm_a7, >pm_a8
          !byte >pm_b1, >pm_b2, >pm_b3, >pm_b4


; ---------------------------------------------------------------------------
;  song table
;
;  song_speed is the frames per row, so it sets the tempo:
;      speed 6 -> 8.33 rows/s -> 125 BPM
;      speed 8 -> 6.25 rows/s -> 93.75 BPM
;
;  song_ord is where the song's three order lists start in song_ord_lo/hi;
;  music_init adds the voice number to it, which is why there is still no
;  multiplication anywhere in the player.
; ---------------------------------------------------------------------------
song_speed  !byte 8
song_ord    !byte 0

song_ord_lo !byte <ord1_bass, <ord1_harp, <ord1_mel
song_ord_hi !byte >ord1_bass, >ord1_harp, >ord1_mel


; ---------------------------------------------------------------------------
;  the songs
; ---------------------------------------------------------------------------
                !source "gambit.asm"
