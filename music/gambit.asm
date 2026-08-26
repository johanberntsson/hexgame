; ===========================================================================
;  gambit.asm -- song 0, "Gambit"
;
;  Board game music: E minor, 93.75 BPM, in the classical/celesta tradition
;  (think the Tetris title screens).  Slow harmonic motion so it can sit
;  under a game of chess for an hour, but a continuous 1/16 note broken
;  chord underneath so it never turns into a dirge.
;
;      voice 1  pizzicato bass, all in octave 2
;      voice 2  the harp: an eight note up-and-down arpeggio per two beats
;      voice 3  the melody
;
;  24 bars, about 61 seconds, behind a 4 bar intro of bass and harp alone
;  that the order lists skip on the repeat (ORD_JMP back to position 2).
;
;      bars   1-8    A      Em  B/D#  G/D  A/C#  C  Am  B7  Em
;      bars   9-16   A'     same chords, the melody varied and ornamented
;      bars  17-24   B      G  D  Em  C  G  D  Am  B7   -- relative major
;
;  The A section is built on the chromatic descent E D# D C# C, which is what
;  gives it the "thinking" character; B lifts into G major for contrast and
;  the walking bass doubles up to quarter notes there.
; ===========================================================================

; ---------------------------------------------------------------------------
;  order lists -- positions 0-1 are the intro, the loop is positions 2-13
; ---------------------------------------------------------------------------
ord1_bass !byte PB_EM_B,  PB_G_A                        ; intro, played once
          !byte PB_EM_B,  PB_G_A,  PB_C_AM, PB_B7_EM    ; A
          !byte PB_EM_B,  PB_G_A,  PB_C_AM, PB_B7_EM    ; A'
          !byte PB_G_D,   PB_EM_C, PB_G_D,  PB_AM_B7    ; B
          !byte ORD_JMP, 2

ord1_harp !byte PH_EM_B,  PH_G_A
          !byte PH_EM_B,  PH_G_A,  PH_C_AM, PH_B7_EM
          !byte PH_EM_B,  PH_G_A,  PH_C_AM, PH_B7_EM
          !byte PH_G_D,   PH_EM_C, PH_G_D,  PH_AM_B7
          !byte ORD_JMP, 2

ord1_mel  !byte PM_REST,  PM_REST                       ; melody waits out
          !byte PM_A1,    PM_A2,   PM_A3,   PM_A4       ; the intro
          !byte PM_A5,    PM_A6,   PM_A7,   PM_A8
          !byte PM_B1,    PM_B2,   PM_B3,   PM_B4
          !byte ORD_JMP, 2


; ===========================================================================
;  voice 1 -- pizzicato bass
;
;  A section: root on 1, fifth on 3, root on 4, so the bar leans forward into
;  the next one without filling it in.  B section: straight quarter notes,
;  root and fifth, which is the lift into the major.
; ===========================================================================
pb_em_b                                                 ; Em | B/D#
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_ds+o2,IPIZZ, 0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_fs+o2,IPIZZ, 0,0, 0,0, 0,0
        !byte n_ds+o2,IPIZZ, 0,0, 0,0, 0,0
        +endpat pb_em_b

pb_g_a                                                  ; G/D | A/C#
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_cs+o2,IPIZZ, 0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_cs+o2,IPIZZ, 0,0, 0,0, 0,0
        +endpat pb_g_a

pb_c_am                                                 ; C | Am
        !byte n_c+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_c+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        +endpat pb_c_am

pb_b7_em                                                ; B7 | Em
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_fs+o2,IPIZZ, 0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        +endpat pb_b7_em

pb_g_d                                                  ; G | D  -- walking
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_d+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        +endpat pb_g_d

pb_em_c                                                 ; Em | C  -- walking
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_c+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_c+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_g+o2,IPIZZ,  0,0, 0,0, 0,0
        +endpat pb_em_c

pb_am_b7                                                ; Am | B7 -- walking
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_a+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_e+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_fs+o2,IPIZZ, 0,0, 0,0, 0,0
        !byte n_b+o2,IPIZZ,  0,0, 0,0, 0,0
        !byte n_fs+o2,IPIZZ, 0,0, 0,0, 0,0
        +endpat pb_am_b7


; ===========================================================================
;  voice 2 -- the harp
;
;  One 1/16 note per row throughout, in eight note cells: up through the
;  chord for five notes and back down for three, two cells to the bar.  Every
;  cell is written out as real notes rather than left to an arpeggio table,
;  which is what makes it a figure instead of a buzz.
; ===========================================================================
ph_em_b                                                 ; Em | B
        !byte n_e+o3,IHARP,  n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        !byte n_fs+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP, n_fs+o4,KEEP
        !byte n_b+o4,KEEP,   n_fs+o4,KEEP, n_ds+o4,KEEP, n_b+o3,KEEP
        !byte n_fs+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP, n_fs+o4,KEEP
        !byte n_b+o4,KEEP,   n_fs+o4,KEEP, n_ds+o4,KEEP, n_b+o3,KEEP
        +endpat ph_em_b

ph_g_a                                                  ; G | A
        !byte n_g+o3,IHARP,  n_b+o3,KEEP,  n_d+o4,KEEP,  n_g+o4,KEEP
        !byte n_b+o4,KEEP,   n_g+o4,KEEP,  n_d+o4,KEEP,  n_b+o3,KEEP
        !byte n_g+o3,KEEP,   n_b+o3,KEEP,  n_d+o4,KEEP,  n_g+o4,KEEP
        !byte n_b+o4,KEEP,   n_g+o4,KEEP,  n_d+o4,KEEP,  n_b+o3,KEEP
        !byte n_e+o3,KEEP,   n_a+o3,KEEP,  n_cs+o4,KEEP, n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_cs+o4,KEEP, n_a+o3,KEEP
        !byte n_e+o3,KEEP,   n_a+o3,KEEP,  n_cs+o4,KEEP, n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_cs+o4,KEEP, n_a+o3,KEEP
        +endpat ph_g_a

ph_c_am                                                 ; C | Am
        !byte n_e+o3,IHARP,  n_g+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_a+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_a+o3,KEEP
        !byte n_e+o3,KEEP,   n_a+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_a+o3,KEEP
        +endpat ph_c_am

ph_b7_em                                                ; B7 | Em
        !byte n_fs+o3,IHARP, n_a+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP
        !byte n_fs+o4,KEEP,  n_ds+o4,KEEP, n_b+o3,KEEP,  n_a+o3,KEEP
        !byte n_fs+o3,KEEP,  n_a+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP
        !byte n_fs+o4,KEEP,  n_ds+o4,KEEP, n_b+o3,KEEP,  n_a+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        +endpat ph_b7_em

ph_g_d                                                  ; G | D
        !byte n_g+o3,IHARP,  n_b+o3,KEEP,  n_d+o4,KEEP,  n_g+o4,KEEP
        !byte n_b+o4,KEEP,   n_g+o4,KEEP,  n_d+o4,KEEP,  n_b+o3,KEEP
        !byte n_g+o3,KEEP,   n_b+o3,KEEP,  n_d+o4,KEEP,  n_g+o4,KEEP
        !byte n_b+o4,KEEP,   n_g+o4,KEEP,  n_d+o4,KEEP,  n_b+o3,KEEP
        !byte n_d+o3,KEEP,   n_fs+o3,KEEP, n_a+o3,KEEP,  n_d+o4,KEEP
        !byte n_fs+o4,KEEP,  n_d+o4,KEEP,  n_a+o3,KEEP,  n_fs+o3,KEEP
        !byte n_d+o3,KEEP,   n_fs+o3,KEEP, n_a+o3,KEEP,  n_d+o4,KEEP
        !byte n_fs+o4,KEEP,  n_d+o4,KEEP,  n_a+o3,KEEP,  n_fs+o3,KEEP
        +endpat ph_g_d

ph_em_c                                                 ; Em | C
        !byte n_e+o3,IHARP,  n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_b+o3,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_b+o3,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_g+o3,KEEP
        !byte n_e+o3,KEEP,   n_g+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_g+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_g+o3,KEEP
        +endpat ph_em_c

ph_am_b7                                                ; Am | B7
        !byte n_e+o3,IHARP,  n_a+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_a+o3,KEEP
        !byte n_e+o3,KEEP,   n_a+o3,KEEP,  n_c+o4,KEEP,  n_e+o4,KEEP
        !byte n_a+o4,KEEP,   n_e+o4,KEEP,  n_c+o4,KEEP,  n_a+o3,KEEP
        !byte n_fs+o3,KEEP,  n_a+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP
        !byte n_fs+o4,KEEP,  n_ds+o4,KEEP, n_b+o3,KEEP,  n_a+o3,KEEP
        !byte n_fs+o3,KEEP,  n_a+o3,KEEP,  n_b+o3,KEEP,  n_ds+o4,KEEP
        !byte n_fs+o4,KEEP,  n_ds+o4,KEEP, n_b+o3,KEEP,  n_a+o3,KEEP
        +endpat ph_am_b7


; ===========================================================================
;  voice 3 -- the melody
;
;  ILEAD through the A sections, IFLUTE through B, and ITRILL on the two long
;  held E's that close each half of A.
; ===========================================================================
pm_rest                                                 ; the 4 bar intro
        !byte CMD_OFF,0,     0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        !byte 0,0,           0,0, 0,0, 0,0
        +endpat pm_rest

pm_a1                                                   ; bars 1-2
        !byte n_b+o4,ILEAD,  0,0, n_e+o5,ILEAD,  0,0    ; B4 E5
        !byte n_g+o5,ILEAD,  0,0, 0,0,           0,0    ; G5
        !byte n_fs+o5,ILEAD, 0,0, n_e+o5,ILEAD,  0,0    ; F#5 E5
        !byte n_d+o5,ILEAD,  0,0, 0,0,           0,0    ; D5
        !byte n_ds+o5,ILEAD, 0,0, 0,0,           0,0    ; D#5
        !byte n_fs+o5,ILEAD, 0,0, n_ds+o5,ILEAD, 0,0    ; F#5 D#5
        !byte n_b+o4,ILEAD,  0,0, 0,0,           0,0    ; B4
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a1

pm_a2                                                   ; bars 3-4
        !byte n_b+o4,ILEAD,  0,0, n_d+o5,ILEAD,  0,0    ; B4 D5
        !byte n_g+o5,ILEAD,  0,0, 0,0,           0,0    ; G5
        !byte n_fs+o5,ILEAD, 0,0, n_e+o5,ILEAD,  0,0    ; F#5 E5
        !byte n_d+o5,ILEAD,  0,0, 0,0,           0,0    ; D5
        !byte n_cs+o5,ILEAD, 0,0, 0,0,           0,0    ; C#5
        !byte n_e+o5,ILEAD,  0,0, n_cs+o5,ILEAD, 0,0    ; E5 C#5
        !byte n_a+o4,ILEAD,  0,0, 0,0,           0,0    ; A4
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a2

pm_a3                                                   ; bars 5-6
        !byte n_e+o5,ILEAD,  0,0, n_g+o5,ILEAD,  0,0    ; E5 G5
        !byte n_b+o5,ILEAD,  0,0, 0,0,           0,0    ; B5
        !byte n_a+o5,ILEAD,  0,0, n_g+o5,ILEAD,  0,0    ; A5 G5
        !byte n_e+o5,ILEAD,  0,0, 0,0,           0,0    ; E5
        !byte n_a+o5,ILEAD,  0,0, 0,0,           0,0    ; A5
        !byte n_g+o5,ILEAD,  0,0, n_e+o5,ILEAD,  0,0    ; G5 E5
        !byte n_c+o5,ILEAD,  0,0, 0,0,           0,0    ; C5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a3

pm_a4                                                   ; bars 7-8
        !byte n_ds+o5,ILEAD, 0,0, n_fs+o5,ILEAD, 0,0    ; D#5 F#5
        !byte n_a+o5,ILEAD,  0,0, 0,0,           0,0    ; A5
        !byte n_fs+o5,ILEAD, 0,0, n_ds+o5,ILEAD, 0,0    ; F#5 D#5
        !byte n_b+o4,ILEAD,  0,0, 0,0,           0,0    ; B4
        !byte n_e+o5,ITRILL, 0,0, 0,0,           0,0    ; E5, trilled
        !byte 0,0,           0,0, 0,0,           0,0
        !byte 0,0,           0,0, 0,0,           0,0
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a4

pm_a5                                                   ; bars 9-10
        !byte n_e+o5,ILEAD,  0,0, n_fs+o5,ILEAD, 0,0    ; E5 F#5
        !byte n_g+o5,ILEAD,  0,0, n_a+o5,ILEAD,  0,0    ; G5 A5
        !byte n_b+o5,ILEAD,  0,0, 0,0,           0,0    ; B5
        !byte n_g+o5,ILEAD,  0,0, 0,0,           0,0    ; G5
        !byte n_a+o5,ILEAD,  0,0, n_fs+o5,ILEAD, 0,0    ; A5 F#5
        !byte n_ds+o5,ILEAD, 0,0, 0,0,           0,0    ; D#5
        !byte n_fs+o5,ILEAD, 0,0, 0,0,           0,0    ; F#5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a5

pm_a6                                                   ; bars 11-12
        !byte n_g+o5,ILEAD,  0,0, n_a+o5,ILEAD,  0,0    ; G5 A5
        !byte n_b+o5,ILEAD,  0,0, 0,0,           0,0    ; B5
        !byte n_a+o5,ILEAD,  0,0, n_g+o5,ILEAD,  0,0    ; A5 G5
        !byte n_fs+o5,ILEAD, 0,0, 0,0,           0,0    ; F#5
        !byte n_e+o5,ILEAD,  0,0, n_cs+o5,ILEAD, 0,0    ; E5 C#5
        !byte n_a+o4,ILEAD,  0,0, 0,0,           0,0    ; A4
        !byte n_cs+o5,ILEAD, 0,0, 0,0,           0,0    ; C#5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a6

pm_a7                                                   ; bars 13-14
        !byte n_e+o5,ILEAD,  0,0, 0,0,           0,0    ; E5
        !byte n_g+o5,ILEAD,  0,0, n_b+o5,ILEAD,  0,0    ; G5 B5
        !byte n_a+o5,ILEAD,  0,0, 0,0,           0,0    ; A5
        !byte 0,0,           0,0, 0,0,           0,0
        !byte n_g+o5,ILEAD,  0,0, n_e+o5,ILEAD,  0,0    ; G5 E5
        !byte n_a+o5,ILEAD,  0,0, 0,0,           0,0    ; A5
        !byte n_e+o5,ILEAD,  0,0, 0,0,           0,0    ; E5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_a7

pm_a8                                                   ; bars 15-16
        !byte n_fs+o5,ILEAD, 0,0, n_ds+o5,ILEAD, 0,0    ; F#5 D#5
        !byte n_b+o4,ILEAD,  0,0, n_ds+o5,ILEAD, 0,0    ; B4 D#5
        !byte n_fs+o5,ILEAD, 0,0, 0,0,           0,0    ; F#5
        !byte n_a+o5,ILEAD,  0,0, 0,0,           0,0    ; A5
        !byte n_e+o5,ITRILL, 0,0, 0,0,           0,0    ; E5, trilled
        !byte 0,0,           0,0, 0,0,           0,0
        !byte 0,0,           0,0, 0,0,           0,0
        !byte n_b+o4,ILEAD,  0,0, n_d+o5,ILEAD,  0,0    ; pickup into G
        +endpat pm_a8

pm_b1                                                   ; bars 17-18
        !byte n_b+o5,IFLUTE, 0,0, 0,0,           0,0    ; B5
        !byte 0,0,           0,0, 0,0,           0,0
        !byte n_a+o5,IFLUTE, 0,0, 0,0,           0,0    ; A5
        !byte n_g+o5,IFLUTE, 0,0, 0,0,           0,0    ; G5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        !byte 0,0,           0,0, 0,0,           0,0
        !byte n_a+o5,IFLUTE, 0,0, 0,0,           0,0    ; A5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        +endpat pm_b1

pm_b2                                                   ; bars 19-20
        !byte n_g+o5,IFLUTE, 0,0, 0,0,           0,0    ; G5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        !byte n_e+o5,IFLUTE, 0,0, 0,0,           0,0    ; E5
        !byte 0,0,           0,0, 0,0,           0,0
        !byte n_c+o5,IFLUTE, 0,0, n_e+o5,IFLUTE, 0,0    ; C5 E5
        !byte n_g+o5,IFLUTE, 0,0, 0,0,           0,0    ; G5
        !byte n_e+o5,IFLUTE, 0,0, 0,0,           0,0    ; E5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_b2

pm_b3                                                   ; bars 21-22
        !byte n_d+o5,IFLUTE, 0,0, n_g+o5,IFLUTE, 0,0    ; D5 G5
        !byte n_b+o5,IFLUTE, 0,0, 0,0,           0,0    ; B5
        !byte n_a+o5,IFLUTE, 0,0, n_g+o5,IFLUTE, 0,0    ; A5 G5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        !byte n_a+o5,IFLUTE, 0,0, 0,0,           0,0    ; A5
        !byte n_fs+o5,IFLUTE,0,0, n_d+o5,IFLUTE, 0,0    ; F#5 D5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        !byte 0,0,           0,0, 0,0,           0,0
        +endpat pm_b3

pm_b4                                                   ; bars 23-24
        !byte n_e+o5,IFLUTE, 0,0, n_g+o5,IFLUTE, 0,0    ; E5 G5
        !byte n_a+o5,IFLUTE, 0,0, 0,0,           0,0    ; A5
        !byte n_g+o5,IFLUTE, 0,0, n_e+o5,IFLUTE, 0,0    ; G5 E5
        !byte n_c+o5,IFLUTE, 0,0, 0,0,           0,0    ; C5
        !byte n_ds+o5,IFLUTE,0,0, 0,0,           0,0    ; D#5
        !byte n_fs+o5,IFLUTE,0,0, 0,0,           0,0    ; F#5
        !byte n_a+o5,IFLUTE, 0,0, n_fs+o5,IFLUTE,0,0    ; A5 F#5
        !byte n_ds+o5,IFLUTE,0,0, 0,0,           0,0    ; D#5
        +endpat pm_b4
