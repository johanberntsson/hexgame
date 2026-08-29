/*
 * fcio.c
 * MEGA65 full color mode console and bitmap display support
 *
 * Copyright (C) 2019-21 - Stephan Kleinert
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#define __fastcall__ // to silence stupid vscode warning

#include <fcio.h>
#include <memory.h>
#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

// KERNAL CHROUT, which is all this file ever wanted from cc65's <cbm.h>.
// Calling the ROM through a function pointer is the Calypsi MEGA65 SDK's own
// idiom for KERNAL entries -- see contrib/MEGA65-SDK/src/lib.h -- and the
// convention puts the byte in A, where CHROUT wants it.
#define cbm_k_bsout ((void (*)(uint8_t))0xffd2)

#define MAX_FCI_BLOCKS 16
#define MAX_WINDOWS 8

#define TOPBORDER_PAL 0x58
#define BOTTOMBORDER_PAL 0x1e8

#define TOPBORDER_NTSC 0x27
#define BOTTOMBORDER_NTSC 0x1b7

char *fcbuf = (char *)0x0400; // general purpose buffer
// DMAlist is at 0x500
fciInfo **infoBlocks = (fciInfo **)0x0600; // pointers to fci info blocks
textwin *defaultWin = (textwin *)0x0700;
textwin *gCurrentWin;
fcioConf *gFcioConfig;
byte winCount = MAX_WINDOWS;

fcioConf stdConfig = {
    0x12000l,   // location of 16 bit screen
    0x14000l,   // reserved bitmap graphics graphics
    0x15000l,   // reserved system palette
    0x15300l,   // loaded palettes base
    0x40000l,   // loaded bitmaps base
    0xff81000l, // attribute/color ram
};

#define VIC_BASE 0xD000UL

#define VIC2CTRL (*(unsigned char *)(0xd016))
#define VIC4CTRL (*(unsigned char *)(0xd054))
#define VIC3CTRL (*(unsigned char *)(0xd031))
#define CHARSTEP_LO (*(unsigned char *)(0xd058))
#define CHARSTEP_HI (*(unsigned char *)(0xd059))
#define CHRCOUNT (*(unsigned char *)(0xd05e))
#define HOTREG (*(unsigned char *)(0xd05d))

#define SCNPTR_0 (*(unsigned char *)(0xd060))
#define SCNPTR_1 (*(unsigned char *)(0xd061))
#define SCNPTR_2 (*(unsigned char *)(0xd062))
#define SCNPTR_3 (*(unsigned char *)(0xd063))

// special graphics characters
#define H_COLUMN_END 4
#define H_COLUMN_START 5
#define CURSOR_CHARACTER 0x5f

#define bitset(byte, nbit) ((byte) |= (1 << (nbit)))
#define bitclear(byte, nbit) ((byte) &= ~(1 << (nbit)))
#define bitflip(byte, nbit) ((byte) ^= (1 << (nbit)))
#define bitcheck(byte, nbit) ((byte) & (1 << (nbit)))

#define COLOR_RAM_OFFSET (gFcioConfig->colorBase - 0xff80000l)

byte gScreenRows;          // number of screen rows (in characters)
byte gScreenColumns;       // number of screen columns (in characters)
byte gScreenRRW;           // size of raster rewrite buffer (in characters)
word gScreenSize;          // screen size (in characters, including rrw if any)
himemPtr nextFreeGraphMem; // location of next free graphics block in banks 4 & 5
himemPtr nextFreePalMem;   // location of next free palette memory block
byte infoBlockCount;       // number of info blocks
byte cgi;                  // universal loop var

#define BITMAP_MIRROR 0x1a000 // TODO: crashed on $19000
byte uniqueTileMode;       // if set, uses BITMAP_MIRROR - $5ffff for tiles
                           // that can be modified independently
int gTopBorder;
int gBottomBorder;

// flags
bool csrflag; // cursor on/off
bool autoCR;

// **There is no file I/O in this program.** readExt, loadExt and
// fc_loadReservedBitmap were here, and the game's resources now arrive in
// memory before it starts -- see src/stage1.c. fc_loadFCI below takes the
// address of an FCI image instead of the name of one.

void fc_init(byte h640, byte v400, fcioConf *config, byte rows, byte rrw_size)
{
    mega65_io_enable();

    // **No KERNAL calls.** There were three here -- a puts to cancel leftover
    // quote mode, and BSOUT for lowercase and clear -- and they were tidying
    // up a screen BASIC had been using. Nothing has been using it now: stage 1
    // banks the ROM out before the game starts, so $E000 is a C64 KERNAL this
    // program never initialised, and BSOUT into it hangs the machine on the
    // first line of fc_init. The screen this sets up below is its own.
    gFcioConfig = config ? config : &stdConfig;

    if ((PEEK(53359U) & 128) == 0)
    {
        gTopBorder = TOPBORDER_PAL;
        gBottomBorder = BOTTOMBORDER_PAL;
    }
    else
    {
        gTopBorder = TOPBORDER_NTSC;
        gBottomBorder = BOTTOMBORDER_NTSC;
    }
    infoBlockCount = 0;
    for (cgi = 0; cgi < MAX_FCI_BLOCKS; ++cgi)
    {
        infoBlocks[cgi] = NULL;
    }
    fc_freeGraphAreas();
    fc_bgcolor(FC_COLOR_BLACK);
    fc_bordercolor(FC_COLOR_BLACK);

    fc_screenmode(h640, v400, rows, rrw_size);
    autoCR = true;

    fc_textcolor(FC_COLOR_GREEN);
}

// The palette registers hold each nybble in the other half of the byte. This
// was six lines of cc65 inline assembly (the asl/adc/rol swap idiom) writing
// through a static; it is the same six instructions written as what they mean.
unsigned char nyblswap(unsigned char in)
{
    return (unsigned char)((in >> 4) | (in << 4));
}

void fc_flash(byte f)
{
    if (f)
        bitset(gCurrentWin->extAttributes, 4);
    else
        bitclear(gCurrentWin->extAttributes, 4);
}

void fc_revers(byte f)
{
    if (f)
        bitset(gCurrentWin->extAttributes, 5);
    else
        bitclear(gCurrentWin->extAttributes, 5);
}

void fc_bold(byte f)
{
    if (f)
        bitset(gCurrentWin->extAttributes, 6);
    else
        bitclear(gCurrentWin->extAttributes, 6);
}

void fc_underline(byte f)
{
    if (f)
        bitset(gCurrentWin->extAttributes, 7);
    else
        bitclear(gCurrentWin->extAttributes, 7);
}

void fc_resetPalette()
{
    mega65_io_enable();
    fc_loadPalette(gFcioConfig->reservedPaletteBase, 255, false);
}

void fc_fatal(const char *format, ...)
{
    char buf[160];
    va_list args;

    mega65_io_enable();
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);
    // On the game's own screen, not through the KERNAL: fc_go8bit and puts
    // were here, and there is no KERNAL to put anything out through any more.
    // fc_init has run by the time anything calls this -- it is the loader's
    // error path -- so fc_puts has a screen to write to.
    fc_bordercolor(2);
    fc_bgcolor(0);
    fc_textcolor(FC_COLOR_WHITE);
    fc_gotoxy(0, 0);
    fc_puts("## fatal error ##");
    fc_puts(buf);
    while (1)
        ;
}

void fc_freeGraphAreas(void)
{
    for (cgi = 0; cgi < infoBlockCount; ++cgi)
    {
        if (infoBlocks[cgi] != NULL)
        {
            free(infoBlocks[cgi]);
            infoBlocks[cgi] = NULL;
        }
    }
    nextFreeGraphMem = gFcioConfig->dynamicBitmapBase;
    nextFreePalMem = gFcioConfig->dynamicPaletteBase;
    infoBlockCount = 0;
}

/*
very simple graphics memory allocation scheme:
try to find space in 128K beginning at GRAPHBASE, without
crossing bank boundaries. If everything's full, bail out.
*/

himemPtr fc_allocGraphMem(word size)
{
    himemPtr adr = nextFreeGraphMem;

    if (nextFreeGraphMem + size < gFcioConfig->dynamicBitmapBase + 0x10000)
    {
        nextFreeGraphMem += size;
        return adr;
    }
    if (nextFreeGraphMem < gFcioConfig->dynamicBitmapBase + 0x10000)
    {
        nextFreeGraphMem = gFcioConfig->dynamicBitmapBase + 0x10000;
        adr = nextFreeGraphMem;
    }
    if (nextFreeGraphMem + size < gFcioConfig->dynamicBitmapBase + 0x20000)
    {
        nextFreeGraphMem += size;
        return adr;
    }
    return 0;
}

himemPtr fc_allocPalMem(word size)
{
    himemPtr adr = nextFreePalMem;
    if (nextFreePalMem < 0x1e000) // TODO: don't hardcode boundaries
    {
        nextFreePalMem += size;
        return adr;
    }
    return 0;
}

// ASCII to VIC screen code, for the lower case character set this library
// switches to in fc_init.
//
// **This used to be a PETSCII to screen code map called asciiToPetscii**, and
// it was fed PETSCII because cc65 translated string literals on the way into
// the object file: 'a' in the source became $41, and $41 - 64 is screen code 1,
// which is a lower case a. Calypsi leaves literals as they are written, so the
// same function was handed $61, returned screen code 65, and drew an upper
// case A -- every string in the game came out with its case inverted.
//
// In the lower case character set the codes are: 0 is @, 1-26 are a-z, 27-31
// are the four brackets and the left arrow, 32-63 are punctuation and digits
// unchanged from ASCII, and 65-90 are A-Z, also unchanged.
char asciiToScreencode(byte c)
{
    if (c == '_')
    {
        return 100; // the underline glyph, not screen code 31
    }
    if (c >= 'a' && c <= 'z')
    {
        return c - 0x60; // 1-26
    }
    if (c == '@')
    {
        return 0;
    }
    if (c > 'Z' && c < 'a')
    {
        return c - 0x40; // [ \\ ] ^ _, at 27-31
    }
    if (c >= 0xc0)
    {
        return c - 0x80;
    }
    return c; // digits, punctuation, space and A-Z are already screen codes
}

void adjustBorders(byte extraRows, byte extraColumns)
{

    byte extraTopRows = 0;
    byte extraBottomRows = 0;
    int newBottomBorder;

    extraColumns++; // TODO: support for extra columns
    extraBottomRows = extraRows / 2;
    extraTopRows = extraRows - extraBottomRows;

    POKE(53320u, gTopBorder - (extraTopRows * 8)); // top border position
    POKE(53326u, gTopBorder - (extraTopRows * 8)); // top text position

    newBottomBorder = gBottomBorder + (extraBottomRows * 8);

    POKE(53322U, newBottomBorder % 256);
    POKE(53323U, newBottomBorder / 256);

    POKE(53371u, gScreenRows);
}

void fc_screenmode(byte h640, byte v400, byte rows, byte rrw_size)
{
    int extraRows = 0;
    gScreenRRW = rrw_size;
    uniqueTileMode = 0;

    mega65_io_enable();
    if (rows == 0)
    {
        gScreenRows = v400 ? 50 : 25;
    }
    else
    {
        gScreenRows = rows;
    }

    HOTREG |= 0x80;   // enable HOTREG if previously disabled
    VIC4CTRL |= 0x04; // enable full color for characters with high byte set
    VIC4CTRL |= 0x01; // enable 16 bit characters

    if (h640)
    {
        VIC3CTRL |= 0x80; // enable H640
        VIC2CTRL |= 0x01; // shift one pixel to the right (VIC-III bug)
        gScreenColumns = 80;
    }
    else
    {
        VIC3CTRL &= 0x7f; // disable H640
        gScreenColumns = 40;
    }

    if (v400)
    {
        VIC3CTRL |= 0x08;
        extraRows = gScreenRows - 50;
    }
    else
    {
        VIC3CTRL &= 0xf7;
        extraRows = (gScreenRows - 25) * 2;
    }

    gScreenSize = gScreenRows * (gScreenColumns + gScreenRRW);
    lfill_skip(gFcioConfig->screenBase, 32, gScreenSize, 2);
    lfill(gFcioConfig->colorBase, 0, gScreenSize * 2);

    // **The lower case character set, which the KERNAL used to pick.**
    // fc_init opened with BSOUT(14) and that is all that call ever did: set
    // bit 1 of $D018, which with the hot registers still live is what moves
    // the VIC-IV's character pointer to the second half of the character ROM.
    // Without it every capital comes out as a graphic, which is a strange way
    // to discover that a KERNAL call was load bearing. Bits 4-7 are the screen
    // base and SCNPTR below sets that properly anyway.
    POKE(53272u, PEEK(53272u) | 0x02);

    HOTREG &= 127; // disable hotreg

    if (extraRows > 0)
    {
        adjustBorders(extraRows, 0);
    }

    // move color RAM because of stupid CBDOS himem usage
    POKE(53348u, COLOR_RAM_OFFSET & 0xff);
    POKE(53349u, (COLOR_RAM_OFFSET >> 8) & 0xff);

    // set CHARCOUNT to the number of columns on screen
    CHRCOUNT = gScreenColumns + gScreenRRW;
    // making space for the number of columns + raster rewrite (if used)
    CHARSTEP_LO = (gScreenColumns + gScreenRRW) * 2; // *2 to have 2 screen bytes==1 character
    CHARSTEP_HI = 0;

    SCNPTR_0 = gFcioConfig->screenBase & 0xff; // screen to 0x12000
    SCNPTR_1 = (gFcioConfig->screenBase >> 8) & 0xff;
    SCNPTR_2 = (gFcioConfig->screenBase >> 16) & 0xff;
    SCNPTR_3 &= 0xF0 | ((gFcioConfig->screenBase) << 24 & 0xff);

    fc_resetwin();
    fc_clrscr();
}

void fc_go8bit()
{
    mega65_io_enable();
    VIC3CTRL = 96;    // quit bitplane mode if set
    POKE(53297L, 96); // quit bitplane mode
    SCNPTR_0 = 0x00;  // screen back to 0x800
    SCNPTR_1 = 0x08;
    SCNPTR_2 = 0x00;
    SCNPTR_3 &= 0xF0;
    VIC4CTRL &= 0xFA; // clear fchi and 16bit chars
    CHRCOUNT = 40;
    CHARSTEP_LO = 40;
    CHARSTEP_HI = 0;
    HOTREG |= 0x80;   // enable hotreg
    VIC3CTRL &= 0x7f; // disable H640
    VIC3CTRL &= 0xf7; // disable V400
    fc_setPalette(0, 0, 0, 0);
    fc_setPalette(1, 255, 255, 255);
    fc_setPalette(2, 255, 0, 0);
}

void fc_plotExtChar(byte x, byte y, byte c)
{
    word charIdx;
    long adr;
    charIdx = (gFcioConfig->reservedBitmapBase / 64) + c;
    adr = (x * 2) + (y * (gScreenColumns + gScreenRRW) * 2);
    lpoke(gFcioConfig->screenBase + adr, charIdx % 256);
    lpoke(gFcioConfig->screenBase + adr + 1, charIdx / 256);
}

void fc_addGraphicsRect(byte x0, byte y0, byte width, byte height,
                        himemPtr bitmapData)
{
    static byte x, y;
    long adr;
    word currentCharIdx;

    currentCharIdx = bitmapData / 64;

    for (y = y0; y < y0 + height; ++y)
    {
        for (x = x0; x < x0 + width; ++x)
        {
            adr = gFcioConfig->screenBase + (x * 2) + (y * (gScreenColumns + gScreenRRW) * 2);
            lpoke(adr + 1, currentCharIdx / 256); // set highbyte first to avoid blinking
            lpoke(adr, currentCharIdx % 256);     // while setting up the screeen
            currentCharIdx++;
        }
    }
}

// **The image is at `src` in 28 bit memory, not in a file.** It is the same
// bytes an .fci file holds and it is read in the same order; what has changed
// is where they come from. tools/mkprg.py packs res/hexgame.fci into
// hexgame.prg and src/stage1.c unpacks it to attic RAM before this runs, so
// the parsing below is unchanged and the machine never opens anything.
fciInfo *fc_loadFCI(himemPtr src, himemPtr address, himemPtr paletteAddress)
{
    static byte numColumns, numRows, lastcolorIndex;
    static byte fciOptions;
    static byte reservedSysPalette;

    word palsize;
    word imgsize;
    word bytesRead;
    himemPtr bitmampAdr;
    himemPtr palAdr;
    fciInfo *info;

    info = NULL;

    if (!address)
    {
        info = (fciInfo *)malloc(sizeof(fciInfo));
        infoBlocks[infoBlockCount++] = info;
    }

    lcopy(src, (long)fcbuf, 9);
    src += 9;

    numRows = fcbuf[5];
    numColumns = fcbuf[6];
    fciOptions = fcbuf[7];
    lastcolorIndex = fcbuf[8];
    reservedSysPalette = fciOptions & 2;

    palsize = (lastcolorIndex + 1) * 3;

    if (!paletteAddress)
    {
        palAdr = fc_allocPalMem(palsize);
        if (palAdr == 0)
        {
            fc_fatal("no room for palette");
        }
    }
    else
    {
        palAdr = paletteAddress;
    }

    // One DMA, where this used to be a loop through a 255 byte buffer: the
    // palette is 765 bytes and both ends of the copy are 28 bit addresses now,
    // so there is nothing to stream it through.
    lcopy(src, palAdr, palsize);
    src += palsize;

    imgsize = numColumns * numRows * 64;

    lcopy(src, (long)fcbuf, 3);
    src += 3;
    // "IMG", not "img": png2fci writes the marker in upper case ASCII. This
    // read it as "img" and matched, because cc65 compiled every string literal
    // into PETSCII, where lower case ASCII letters become the codes $41-$5A --
    // the same bytes upper case ASCII uses. Calypsi compiles literals as
    // written, so the case here has to be the case in the file.
    if (0 != memcmp(fcbuf, "IMG", 3))
    {
        fc_fatal("image marker not found at %lx", src);
    }

    if (!address)
    {
        bitmampAdr = fc_allocGraphMem(imgsize);
        if (bitmampAdr == 0)
        {
            fc_fatal("no memory for fci at %lx", src);
        }
    }
    else
    {
        bitmampAdr = address;
    }

    lcopy(src, bitmampAdr, imgsize);
    bytesRead = imgsize;

    if (info != NULL)
    {
        info->columns = numColumns;
        info->rows = numRows;
        info->size = bytesRead;
        info->baseAdr = bitmampAdr;
        info->paletteAdr = palAdr;
        info->paletteSize = lastcolorIndex;
        info->reservedSysPalette = reservedSysPalette;
    }

    mega65_io_enable(); // kernal has the disgusting habit of resetting vic personality

    return info;
}

void fc_zeroPalette(byte reservedSysPalette)
{
    byte start;

    mega65_io_enable();
    start = reservedSysPalette ? 16 : 0;
    for (cgi = start; cgi < 255; ++cgi)
    {
        POKE(0xd100u + cgi, 0); // palette[colAdr];
        POKE(0xd200u + cgi, 0); // palette[colAdr + 1];
        POKE(0xd300u + cgi, 0); // palette[colAdr + 2];
    }
}

void fc_loadPalette(himemPtr adr, byte size, byte reservedSysPalette)
{
    himemPtr colAdr;
    byte start;
    int i;
    start = reservedSysPalette ? 16 : 0;

    for (i = start; i <= size; ++i)
    {
        colAdr = i * 3;
        // fc_printf("\n%d (%lx) : %2x %2x %2x", i, adr + colAdr, lpeek(adr + colAdr), lpeek(adr + colAdr + 1), lpeek(adr + colAdr + 2));
        // fc_getkey();
        POKE(0xd100u + i, nyblswap(lpeek(adr + colAdr)));     //  palette[colAdr];
        POKE(0xd200u + i, nyblswap(lpeek(adr + colAdr + 1))); // palette[colAdr + 1];
        POKE(0xd300u + i, nyblswap(lpeek(adr + colAdr + 2))); // palette[colAdr + 2];
    }
}

void fc_fadePalette(himemPtr adr, byte size, byte reservedSysPalette, byte steps, bool fadeOut)
{
    byte i;
    byte startReg;
    byte *destPalette;
    byte *entry;

    byte start, end, step;

    startReg = reservedSysPalette ? 16 : 0;
    destPalette = malloc(size * 3);
    lcopy(adr, (long)destPalette, size * 3);

    if (fadeOut)
    {
        start = steps;
        end = 0;
        step = -1;
    }
    else
    {
        start = 0;
        end = steps;
        step = 1;
    }

    for (i = start; i != end; i += step)
    {
        entry = destPalette + (startReg * 3);
        for (cgi = startReg; cgi < size; ++cgi, entry += 3)
        {
            POKE(0xd100u + cgi, nyblswap((*(entry)*i) / steps));
            POKE(0xd200u + cgi, nyblswap((*(entry + 1) * i) / steps));
            POKE(0xd300u + cgi, nyblswap((*(entry + 2) * i) / steps));
        }
    }
    free(destPalette);
}

void fc_fadeFCI(fciInfo *info, byte x0, byte y0, byte steps)
{
    fc_zeroPalette(info->reservedSysPalette);
    fc_addGraphicsRect(x0, y0, info->columns, info->rows, info->baseAdr);
    fc_fadePalette(info->paletteAdr, info->paletteSize, info->reservedSysPalette, steps, false);
}

void fc_displayFCI(fciInfo *info, byte x0, byte y0, bool setPalette)
{
    fc_addGraphicsRect(x0, y0, info->columns, info->rows, info->baseAdr);
    if (setPalette)
    {
        fc_loadFCIPalette(info);
    }
}

void fc_loadFCIPalette(fciInfo *info)
{
    fc_loadPalette(info->paletteAdr, info->paletteSize,
                   info->reservedSysPalette);
}

void fc_scrollUp()
{
    static byte y;
    long bas0, bas1;
    for (y = gCurrentWin->y0; y < gCurrentWin->y0 + gCurrentWin->height - 1; y++)
    {
        bas0 = gFcioConfig->screenBase + (gCurrentWin->x0 * 2 + (y * (gScreenColumns + gScreenRRW) * 2));
        bas1 = gFcioConfig->screenBase + (gCurrentWin->x0 * 2 + ((y + 1) * (gScreenColumns + gScreenRRW) * 2));
        lcopy(bas1, bas0, (gCurrentWin->width + gScreenRRW) * 2);
        bas0 = gFcioConfig->colorBase + (gCurrentWin->x0 * 2 + (y * (gScreenColumns + gScreenRRW) * 2));
        bas1 = gFcioConfig->colorBase + (gCurrentWin->x0 * 2 + ((y + 1) * (gScreenColumns + gScreenRRW) * 2));
        lcopy(bas1, bas0, (gCurrentWin->width + gScreenRRW) * 2);
    }
    fc_line(0, gCurrentWin->height - 1, gCurrentWin->width, 32, gCurrentWin->textcolor);
}

void fc_scrollDown()
{
    signed char y;
    long bas0, bas1;
    for (y = gCurrentWin->y0 + gCurrentWin->height - 2;
         y >= gCurrentWin->y0; y--)
    {
        bas0 = gFcioConfig->screenBase + (gCurrentWin->x0 * 2 + (y * (gScreenColumns + gScreenRRW) * 2));
        bas1 = gFcioConfig->screenBase + (gCurrentWin->x0 * 2 + ((y + 1) * (gScreenColumns + gScreenRRW) * 2));
        lcopy(bas0, bas1, (gCurrentWin->width + gScreenRRW) * 2);
        bas0 = gFcioConfig->colorBase + (gCurrentWin->x0 * 2 + (y * (gScreenColumns + gScreenRRW) * 2));
        bas1 = gFcioConfig->colorBase + (gCurrentWin->x0 * 2 + ((y + 1) * (gScreenColumns + gScreenRRW) * 2));
        lcopy(bas0, bas1, (gCurrentWin->width + gScreenRRW) * 2);
    }

    fc_line(0, 0, gCurrentWin->width, 32, gCurrentWin->textcolor);
}

void cr()
{
    gCurrentWin->xc = 0;
    gCurrentWin->yc++;
    if (gCurrentWin->yc > gCurrentWin->height - 1)
    {
        fc_scrollUp();
        gCurrentWin->yc = gCurrentWin->height - 1;
    }
}

void fc_plotPetsciiChar(byte x, byte y, byte c, byte color, byte exAttr)
{
    word adrOffset;
    adrOffset = (x * 2) + (y * 2 * (gScreenColumns + gScreenRRW));
    lpoke(gFcioConfig->screenBase + adrOffset, c);
    lpoke(gFcioConfig->screenBase + adrOffset + 1, 0);
    lpoke(gFcioConfig->colorBase + adrOffset + 1, color | exAttr);
    lpoke(gFcioConfig->colorBase + adrOffset, 0);
}

byte fc_wherex() { return gCurrentWin->xc; }

byte fc_wherey() { return gCurrentWin->yc; }

void fc_setAutoCR(bool a)
{
    autoCR = a;
}

void fc_putc(char c)
{
    static char out;
    if (!c)
    {
        return;
    }
    if (c == '\n')
    {
        cr();
        return;
    }

    if (gCurrentWin->xc >= gCurrentWin->width)
    {
        return;
    }

    out = asciiToScreencode(c);

    fc_plotPetsciiChar(gCurrentWin->xc + gCurrentWin->x0, gCurrentWin->yc + gCurrentWin->y0, out,
                       gCurrentWin->textcolor, gCurrentWin->extAttributes);
    gCurrentWin->xc++;

    if (autoCR)
    {
        if (gCurrentWin->xc >= gCurrentWin->width)
        {
            gCurrentWin->yc++;
            gCurrentWin->xc = 0;
            if (gCurrentWin->yc >= gCurrentWin->height)
            {
                gCurrentWin->yc = gCurrentWin->height - 1;
                fc_scrollUp();
            }
        }
    }

    if (csrflag)
    {
        fc_plotPetsciiChar(gCurrentWin->xc + gCurrentWin->x0, gCurrentWin->yc + gCurrentWin->y0,
                           CURSOR_CHARACTER, gCurrentWin->textcolor, 16);
    }
}

void _debug_fc_puts(const char *s)
{
    const char *current = s;
    while (*current)
    {
        fc_putc(*current++);
    }
}

void fc_puts(const char *s)
{
    /* #ifdef DEBUG
    char out[16];
#endif */
    const char *current = s;
    while (*current)
    {
        fc_putc(*current++);
    }
    /* #ifdef DEBUG
    gCurrentWin->x0 = 0;
    gCurrentWin->y0 = 0;
    gCurrentWin->xc = gScreenColumns - 4;
    gCurrentWin->yc = 0;
    sprintf(out, "%x", _heapmaxavail());
    _debug_fc_puts(out);
#endif */
}

void fc_putsxy(byte x, byte y, char *s)
{
    fc_gotoxy(x, y);
    fc_puts(s);
}

void fc_putcxy(byte x, byte y, char c)
{
    fc_gotoxy(x, y);
    fc_putc(c);
}

void fc_cursor(byte onoff)
{
    csrflag = onoff;

    fc_plotPetsciiChar(gCurrentWin->xc + gCurrentWin->x0,
                       gCurrentWin->yc + gCurrentWin->y0,
                       (csrflag ? CURSOR_CHARACTER : 32),
                       gCurrentWin->textcolor,
                       (csrflag ? 16 : 0));
}

void fc_textcolor(byte c) { gCurrentWin->textcolor = c; }

void fc_gotoxy(byte x, byte y)
{
    gCurrentWin->xc = x;
    gCurrentWin->yc = y;
}

void fc_printf(const char *format, ...)
{
    char buf[160];
    va_list args;
    va_start(args, format);
    vsprintf(buf, format, args);
    va_end(args);
    fc_puts(buf);
}

void fc_clearUniqueTiles()
{
    lfill(0x18000, 0, 0x7800); // $18000 - $1f800 = 30 KB
    //  skip over DOS ($1f800 - $24000)
    lfill(0x24000, 0, 0x8000); // $24000 - $2c000 = 32 KB
    // skip over C64 kernal ($2c000 - $30000)
    lfill(0x30000, 0, 0x8000);
    lfill(0x38000, 0, 0x8000);
    lfill(0x40000, 0, 0x8000);
    lfill(0x48000, 0, 0x8000);
    lfill(0x50000, 0, 0x8000);
    lfill(0x58000, 0, 0x8000);
    // total: 30 + 7*32 = 254 KB (need 250 for 640x400x64 screen)
}

void fc_clrscr()
{
    fc_block(0, 0, gCurrentWin->width, gCurrentWin->height, 32,
             gCurrentWin->textcolor);
    fc_gotoxy(0, 0);
    if(uniqueTileMode) fc_clearUniqueTiles();
}

void fc_resetwin()
{
    gCurrentWin = defaultWin;
    gCurrentWin->x0 = 0;
    gCurrentWin->y0 = 0;
    gCurrentWin->width = gScreenColumns;
    gCurrentWin->height = gScreenRows;
    gCurrentWin->xc = 0;
    gCurrentWin->yc = 0;
    gCurrentWin->extAttributes = 0;
    gCurrentWin->textcolor = 5;
}

void fc_setwin(textwin *aWin)
{
    gCurrentWin = aWin;
}

textwin *fc_makeWin(byte x0, byte y0, byte width, byte height)
{
    textwin *aWin;
    aWin = malloc(sizeof(textwin));
    aWin->x0 = x0;
    aWin->y0 = y0;
    aWin->width = width;
    aWin->height = height;
    aWin->xc = 0;
    aWin->yc = 0;
    aWin->extAttributes = 0;
    aWin->textcolor = 5; // because I like green. your mileage may vary.
    return aWin;
}

byte fc_kbhit()
{
    return PEEK(0xD610U);
}

byte fc_cgetc()
{
    byte k;
    while ((k = PEEK(0xD610U)) == 0);
    POKE(0xD610U, 0);
    return k;
}

void fc_emptyBuffer(void)
{
    while (fc_kbhit())
    {
        fc_cgetc();
    }
}


char fc_getkey(void)
{
    fc_emptyBuffer();
    return fc_cgetc();
}

int fc_getnum(byte maxlen)
{
    int res;
    char *inptr;
    inptr = fc_input(maxlen);
    res = atoi(inptr);
    free(inptr);
    return res;
}

char *fc_input(byte maxlen)
{
    static byte len, ct;
    char current;
    char *ret;
    len = 0;
    ct = csrflag;
    fc_cursor(1);
    do
    {
        current = fc_cgetc();
        if (current != '\n')
        {
            if (current >= 32)
            {
                if (len < maxlen)
                {
                    // fix upper/lowercase
                    if (current >= 97)
                    {
                        current -= 32;
                    }
                    else if (current >= 65)
                    {
                        current += 32;
                    }
                    fcbuf[len] = current;
                    fcbuf[len + 1] = 0;
                    fc_putc(current);
                    ++len;
                }
            }
            else if (current == 20)
            {
                // del pressed
                if (len > 0)
                {
                    fc_cursor(0);
                    fc_gotoxy(gCurrentWin->xc - 1, gCurrentWin->yc);
                    fc_putc(' ');
                    fc_gotoxy(gCurrentWin->xc - 1, gCurrentWin->yc);
                    fc_cursor(1);
                    --len;
                    fcbuf[len] = 0;
                }
            }
        }
    } while (current != '\n');
    ret = (char *)malloc(++len);
    if (len == 1)
    {
        *ret = 0;
    }
    else
    {
        strncpy(ret, fcbuf, len);
    }
    fc_cursor(ct);
    return ret;
}

void fc_line(byte x, byte y, byte width, byte character, byte col)
{
    word bas;

    bas = (gCurrentWin->x0 + x) * 2 + ((gCurrentWin->y0 + y) * (gScreenColumns + gScreenRRW) * 2);

    // use DMAgic to fill FCM screens with skip byte... PGS, I love you!
    lfill_skip(gFcioConfig->screenBase + bas, character, width, 2);
    lfill_skip(gFcioConfig->screenBase + bas + 1, 0, width, 2);
    lfill_skip(gFcioConfig->colorBase + bas, 0, width, 2);
    lfill_skip(gFcioConfig->colorBase + bas + 1, col, width, 2);

    return;
}

void fc_block(byte x0, byte y0, byte width, byte height, byte character,
              byte col)
{
    static byte y;
    for (y = 0; y < height; ++y)
    {
        fc_line(x0, y0 + y, width, character, col);
    }
}

void fc_center(byte x, byte y, byte width, char *text)
{
    static byte l;
    l = strlen(text);
    if (l >= width - 2)
    {
        fc_gotoxy(x, y);
        fc_puts(text);
        return;
    }
    fc_gotoxy(-1 + x + width / 2 - l / 2, y);
    fc_puts(text);
}

void fc_setPalette(int num, byte red, byte green, byte blue)
{
    POKE(0xd100U + num, nyblswap(red));
    POKE(0xd200U + num, nyblswap(green));
    POKE(0xd300U + num, nyblswap(blue));
}

char fc_getkeyP(byte x, byte y, const char *prompt)
{
    fc_emptyBuffer();
    fc_gotoxy(x, y);
    fc_textcolor(FC_COLOR_WHITE);
    fc_puts(prompt);
    return fc_cgetc();
}

void fc_hlinexy(byte x, byte y, byte width, byte lineChar)
{
    for (cgi = x; cgi < x + width; cgi++)
    {
        fc_plotExtChar(gCurrentWin->x0 + x + cgi, gCurrentWin->y0 + y, lineChar);
    }
}

void fc_vlinexy(byte x, byte y, byte height, byte lineChar)
{
    for (cgi = y; cgi < y + height; cgi++)
    {
        fc_plotExtChar(gCurrentWin->x0 + x, gCurrentWin->y0 + y + cgi, lineChar);
    }
}


// Flattens the memory map so that $20000-$5FFFF is writable RAM. In
// mega65-libc-modified/src/fcio_asm.s, where it replaced an array of bytes
// hand-assembled in ACME because cc65 could not emit 45GS02 opcodes.
extern void fc_bank_out_rom(void);

void fc_setUniqueTileMode()
{
    if(uniqueTileMode == 0) {
        uniqueTileMode = 1;
        // Bank out the ROM, freeing $2xxxx and $3xxxx. The C64 KERNAL at
        // $2E000-$2FFFF stays mapped and is what the machine runs on
        // afterwards, so fc_clearUniqueTiles below steps around it.
        // See the MEGA65 book, page F-11.
        fc_bank_out_rom();
        // clear the new memory, but keep the C64 kernal
        fc_clearUniqueTiles();
    }
}

// Where a cell's 64 bytes of tile data really go. The store runs from
// BITMAP_MIRROR upwards, one 64 byte block per screen cell, but two spans in
// the middle of it are not ours: the DOS work area at $1F800 and the C64
// KERNAL at $2C000, which is the KERNAL this program is running on. Both are
// stepped over, which makes the raw-to-real mapping a monotone staircase --
// contiguous everywhere except at those two steps.
//
// **The second test looks at the address the first one already moved**, which
// is what makes the two skips add up rather than the later one replacing the
// earlier. This was written out twice in the loops below; it is one function
// now because the row-at-a-time path has to ask the same question about the
// first and the last cell of a row.
static long fc_tileStoreAddr(long raw)
{
    if (raw + 64 >= 0x1f800L) raw += 0x4800L;
    if (raw + 64 >= 0x2c000L) raw += 0x4000L;
    return raw;
}

// One row of a tile's screen words, built here and pushed out in a single DMA
// rather than a pair per cell. 80 cells is the widest screen fc_init offers.
static byte tileRowWords[160];

void fc_displayTile(fciInfo *info, byte x0, byte y0, byte t_x, byte t_y, byte t_w, byte t_h, byte mergeTiles)
{
    static byte x, y;
    long screenAddr;
    word charIndex;
    long toTileAddr;
    long fromTileAddr;
    long rawToTileAddr;
    long rowEndAddr;
    word rowBytes;

    for (y = t_y; y < t_y + t_h; ++y) {
        screenAddr = gFcioConfig->screenBase + 2* (x0 + (y0 + y - t_y) * (gScreenColumns + gScreenRRW));
        if(uniqueTileMode) {
            fromTileAddr = info->baseAdr + 64L*(t_x + (y * info->columns));
            // copy bitmap asset to location in $2xxxx - $5xxxx
            //
            // **The row here has to be the row the screen pointer above uses.**
            // It was (y + y0), which is the same thing only while t_y is 0: a
            // tile taken from further down the sheet was displayed in the right
            // place but written into the store belonging to a cell t_y rows
            // lower, so it came out correct and quietly destroyed that cell.
            // fc_displayTile(tiles, 19, 0, 0, 8, 40, 17, 0) -- the title logo,
            // the only caller in this game that passes a non-zero t_y -- was
            // clearing 40x17 characters of tile store below itself on the way
            // past, which nothing happened to be using.
            rawToTileAddr = BITMAP_MIRROR + 64L * (x0 + ((y0 + y - t_y) * gScreenColumns));

            // **A whole row of a tile is one DMA job, not three per cell.**
            // Source and destination are both 64 byte blocks laid end to end,
            // so t_w cells are one contiguous copy -- unless one of the two
            // skips above falls inside the row, which the comparison below is
            // asking about: the last cell has to land exactly t_w-1 blocks
            // past the first. The screen words go the same way, built in
            // tileRowWords and written with one more job.
            //
            // It is worth the trouble because this is the game's whole cost of
            // drawing: the per-cell path is three DMA jobs and a rebuilt DMA
            // list each time, and a full 81 hexagon board came to just over a
            // second of it, a second in which nothing polls the joystick. See
            // the joystick note in src/input.c.
            toTileAddr = fc_tileStoreAddr(rawToTileAddr);
            rowBytes = 64u * t_w;
            rowEndAddr = fc_tileStoreAddr(rawToTileAddr + (long)(rowBytes - 64u));
            if (t_w != 0 && t_w <= sizeof(tileRowWords) / 2 &&
                rowEndAddr - toTileAddr == (long)(rowBytes - 64u)) {
                if (mergeTiles) {
                    lcopy_transparent(fromTileAddr, toTileAddr, rowBytes, 0);
                } else {
                    lcopy(fromTileAddr, toTileAddr, rowBytes);
                }
                charIndex = (word)(toTileAddr / 64L);
                for (x = 0; x < t_w; ++x) {
                    tileRowWords[2*x] = (byte)charIndex;
                    tileRowWords[2*x + 1] = (byte)(charIndex >> 8);
                    ++charIndex;
                }
                lcopy((long)(word)tileRowWords, screenAddr, 2u * t_w);
                continue;
            }
        } else {
            // use pointer directly to bitmap asset
            charIndex = info->baseAdr / 64L + t_x + (y * info->columns);
        }
        for (x = t_x; x < t_x + t_w; ++x)
        {
            if(uniqueTileMode) {
                toTileAddr = fc_tileStoreAddr(rawToTileAddr);
                if(mergeTiles) {
                    lcopy_transparent(fromTileAddr, toTileAddr, 64, 0);
                } else {
                    lcopy(fromTileAddr, toTileAddr, 64);
                }
                charIndex = toTileAddr / 64L;
                fromTileAddr += 64L;
                rawToTileAddr += 64L;
            }
            // set highbyte first to avoid blinking
            // while setting up the screeen
            lpoke(screenAddr + 1, charIndex / 256);
            lpoke(screenAddr, charIndex % 256);
            screenAddr += 2;
            ++charIndex;
        }
    }
}

long fc_rrw_allocate(byte row, byte size)
{
    int rowLength = gScreenColumns + gScreenRRW;
    int rowAddr = row * rowLength;
    int nextRowAddr = rowAddr + rowLength;
    int addr = gScreenColumns + rowAddr;
    // search for first free position in RRW
    while(lpeek(gFcioConfig->colorBase + 2*addr) != 0 || 
          lpeek(gFcioConfig->colorBase + 1 +  2*addr) != 0) {
        ++addr;
    }

    // check if enough space
    if((addr + size) > nextRowAddr) fc_fatal("RRW too small");
    
    // return memory offset
    return (long) (2 * addr);
}

void fc_rrw_begin()
{
    byte row;
    int rowAddr, nextRowAddr;
    int rowLength = gScreenColumns + gScreenRRW;

    for(row = 0; row < gScreenRows; row++) {
        rowAddr = row * rowLength;
        nextRowAddr = rowAddr + rowLength;
        rowAddr += gScreenColumns;
        // write 0 in all RRW color bytes for this row
        while(rowAddr < nextRowAddr) {
            lpoke(gFcioConfig->colorBase + 2*rowAddr, 0);
            lpoke(gFcioConfig->colorBase + 2*rowAddr + 1, 0);
            ++rowAddr;
        }
    }
}

void fc_rrw_end()
{
    byte row;
    long adr;
    int finalPos = gScreenColumns * 8;
    for(row = 0; row < gScreenRows; row++) {
        adr = fc_rrw_allocate(row, 1);
        lpoke(gFcioConfig->screenBase + adr, finalPos % 256);
        lpoke(gFcioConfig->screenBase + adr + 1, finalPos / 256);
        lpoke(gFcioConfig->colorBase + adr + 1, 0);
        lpoke(gFcioConfig->colorBase + adr, 0x10); // gotoX
    }
}

void fc_rrw_puts(byte x, byte y, byte color, const char *s)
{
    char out;
    long adr = fc_rrw_allocate(y, 1 + strlen(s));
    char extcolor;
    const char *current = s;

    // insert gotox
    lpoke(gFcioConfig->screenBase + adr, (x * 8) % 256);
    lpoke(gFcioConfig->screenBase + adr + 1, (x * 8) / 256);
    lpoke(gFcioConfig->colorBase + adr + 1, 0);
    lpoke(gFcioConfig->colorBase + adr, 0x90); // gotoX + transparent
    adr += 2;

    if(color == 0) color = gCurrentWin->textcolor;
    extcolor = color | gCurrentWin->extAttributes;

    while (*current)
    {
        out = asciiToScreencode(*current++);
        lpoke(gFcioConfig->screenBase + adr, out);
        lpoke(gFcioConfig->screenBase + adr + 1, 0);
        lpoke(gFcioConfig->colorBase + adr + 1, extcolor);
        lpoke(gFcioConfig->colorBase + adr, 0);
        adr += 2;
    }
}

void fc_rrw_tile(byte x, byte y, fciInfo *info, byte t_x, byte t_y, byte t_w, byte t_h)
{
    static byte xx, yy;
    long adr;
    word currentCharIdx;

    for (yy = 0; yy < t_h; ++yy)
    {
        currentCharIdx = info->baseAdr / 64 + t_x + ((yy + t_y) * info->columns);
        // insert gotox
        adr = fc_rrw_allocate(y + yy, 1 + t_w);
        lpoke(gFcioConfig->screenBase + adr, (x * 8) % 256);
        lpoke(gFcioConfig->screenBase + adr + 1, (x * 8) / 256);
        lpoke(gFcioConfig->colorBase + adr + 1, 0);
        lpoke(gFcioConfig->colorBase + adr, 0x90); // gotoX + transparent
        adr += 2;

        for (xx = t_x; xx < t_x + t_w; ++xx)
        {
            lpoke(gFcioConfig->screenBase + adr + 1, currentCharIdx / 256); // set highbyte first to avoid blinking
            lpoke(gFcioConfig->screenBase + adr, currentCharIdx % 256);     // while setting up the screeen
            lpoke(gFcioConfig->colorBase + adr + 1, 1); // set highbyte first to avoid blinking
            lpoke(gFcioConfig->colorBase + adr, 0);     // while setting up the screeen
            adr+=2;
            ++currentCharIdx;
        }
    }
}

