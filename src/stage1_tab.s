; The table tools/mkprg.py fills in, and nothing else.
;
; It is a section of its own so that it lands at a known address -- the first
; thing in stage 1, at the address stage1.scm pins it to -- because the packer
; has to write it into an already linked binary and needs to know where. Two
; stream descriptors of eight bytes and a two byte entry point; the layout is
; written out in src/stage1.c, which reads it, and in tools/mkprg.py, which
; writes it.
;
; `text` and not `data`: an initialised data section in Calypsi is a copy made
; at startup from an image somewhere else, and what is wanted here is bytes at
; an address, in the file, that the packer can patch.

            .section bootinfo,text
            .public boot_table

boot_table: .space  32, 0
