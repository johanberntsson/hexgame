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
#include <c64.h>
#include <cbm.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

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

#define COLOR_RAM_OFFSET gFcioConfig->colorBase - 0xff80000l

byte gScreenRows;          // number of screen rows (in characters)
byte gScreenColumns;       // number of screen columns (in characters)
byte gScreenRRW;           // size of raster rewrite buffer (in characters)
word gScreenSize;          // screen size (in characters, including rrw if any)
himemPtr nextFreeGraphMem; // location of next free graphics block in banks 4 & 5
himemPtr nextFreePalMem;   // location of next free palette memory block
byte infoBlockCount;       // number of info blocks
byte cgi;                  // universal loop var
byte uniqueTileMode;       // if set, uses $20000 - $5ffff for tiles
                           // that can be modified independently
int gTopBorder;
int gBottomBorder;

// flags
bool csrflag; // cursor on/off
bool autoCR;

unsigned int readExt(FILE *inFile, himemPtr addr, byte skipCBMAddressBytes)
{

    unsigned int readBytes;
    unsigned int overallRead;
    unsigned long insertPos;

    insertPos = addr;
    overallRead = 0;

    if (skipCBMAddressBytes)
    {
        fread(fcbuf, 1, 2, inFile);
    }

    do
    {
        readBytes = fread(fcbuf, 1, FCBUFSIZE, inFile);
        if (readBytes)
        {
            overallRead += readBytes;
            lcopy((long)fcbuf, insertPos, readBytes);
            insertPos += readBytes;
        }
    } while (readBytes);

    return overallRead;
}

unsigned int loadExt(char *filename, himemPtr addr, byte skipCBMAddressBytes)
{

    FILE *inFile;
    word readBytes;

    inFile = fopen(filename, "r");
    readBytes = readExt(inFile, addr, skipCBMAddressBytes);
    fclose(inFile);

    if (readBytes == 0)
    {
        fc_fatal("0 bytes from %s", filename);
    }

    return readBytes;
}

void fc_loadReservedBitmap(char *name)
{
    fc_loadFCI(name, gFcioConfig->reservedBitmapBase, gFcioConfig->reservedPaletteBase);
    fc_resetPalette();
}

void fc_init(byte h640, byte v400, fcioConf *config, byte rows, byte rrw_size, char *reservedBitmapFile)
{
    mega65_io_enable();

    puts("\n");       // cancel leftover quote mode from wrapper or whatever
    cbm_k_bsout(14);  // lowercase
    cbm_k_bsout(147); // clr

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

    if (reservedBitmapFile)
    {
        fc_loadReservedBitmap(reservedBitmapFile);
    }
    fc_textcolor(FC_COLOR_GREEN);
}

static unsigned char swp;
unsigned char nyblswap(unsigned char in) // oh why?!
{
    swp = in;
    __asm__("lda %v", swp);
    __asm__("asl  a");
    __asm__("adc  #$80");
    __asm__("rol a");
    __asm__("asl a");
    __asm__("adc  #$80");
    __asm__("rol  a");
    __asm__("sta %v", swp);
    return swp;
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
    fc_go8bit();
    fc_bordercolor(2);
    fc_bgcolor(0);
    puts("## fatal error ##");
    puts(buf);
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

char asciiToPetscii(byte c)
{
    // TODO: could be made much faster with translation table
    if (c == '_')
    {
        return 100;
    }
    if (c >= 64 && c <= 95)
    {
        return c - 64;
    }
    if (c >= 96 && c < 192)
    {
        return c - 32;
    }
    if (c >= 192)
    {
        return c - 128;
    }
    return c;
}

void adjustBorders(byte extraRows, byte extraColumns)
{

    byte extraTopRows = 0;
    byte extraBottomRows = 0;
    int newBottomBorder;
return;
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
    cbm_k_bsout(14);  // lowercase charset
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

fciInfo *fc_loadFCI(char *filename, himemPtr address, himemPtr paletteAddress)
{
    static byte numColumns, numRows, lastcolorIndex;
    static byte fciOptions;
    static byte reservedSysPalette;

    FILE *fcifile;
    byte *palette;
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

    fcifile = fopen(filename, "rb");

    if (!fcifile)
    {
        fc_fatal("fci not found %s", filename);
    }
    fread(fcbuf, 1, 9, fcifile);

    numRows = fcbuf[5];
    numColumns = fcbuf[6];
    fciOptions = fcbuf[7];
    lastcolorIndex = fcbuf[8];
    reservedSysPalette = fciOptions & 2;

    palsize = (lastcolorIndex + 1) * 3;
    palette = (byte *)malloc(palsize);
    fread(palette, 3, lastcolorIndex + 1, fcifile);

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
    lcopy((long)palette, palAdr, palsize);
    free(palette);
    imgsize = numColumns * numRows * 64;

    fread(fcbuf, 1, 3, fcifile);
    if (0 != memcmp(fcbuf, "img", 3))
    {
        fc_fatal("image marker not found in %s", filename);
    }

    if (!address)
    {
        bitmampAdr = fc_allocGraphMem(imgsize);
        if (bitmampAdr == 0)
        {
            fc_fatal("no memory for %s", filename);
        }
    }
    else
    {
        bitmampAdr = address;
    }

    bytesRead = readExt(fcifile, bitmampAdr, false);
    fclose(fcifile);

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

fciInfo *fc_displayFCIFile(char *filename, byte x0, byte y0)
{
    fciInfo *info;
    info = fc_loadFCI(filename, 0, 0);
    fc_displayFCI(info, x0, y0, true);
    return info;
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

    out = asciiToPetscii(c);

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
    lfill(0x20000, 0, 0x8000);
    lfill(0x28000, 0, 0x6000); // avoid C64 kernal
    lfill(0x30000, 0, 0x8000);
    lfill(0x38000, 0, 0x8000);
    lfill(0x40000, 0, 0x8000);
    lfill(0x48000, 0, 0x8000);
    lfill(0x50000, 0, 0x8000);
    lfill(0x58000, 0, 0x8000);
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

void fc_setUniqueTileMode()
{
    if(uniqueTileMode == 0) {
        uniqueTileMode = 1;
        // bank out the C64/C65 ROM, freeing $2xxxx and $3xxxx.
        // But, assuming that the program is started from C64
        // mode, we still need the kernal, so avoid writing on
        // 2e000 - 2ffff. 
        // See the MEGA65 book, page F-11
        asm("lda #$70");
        asm("sta $d640");
        asm("nop");
        // clear the new memory, but keep the C64 kernal
        fc_clearUniqueTiles();
    }
}

void fc_displayTile(fciInfo *info, byte x0, byte y0, byte t_x, byte t_y, byte t_w, byte t_h, byte mergeTiles)
{
    static byte x, y;
    long toTileAddr;
    long fromTileAddr;
    long screenAddr;
    word charIndex;

    for (y = t_y; y < t_y + t_h; ++y) {
        screenAddr = gFcioConfig->screenBase + 2* (x0 + (y0 + y - t_y) * (gScreenColumns + gScreenRRW));
        if(uniqueTileMode) {
            fromTileAddr = info->baseAdr + 64L*(t_x + (y * info->columns));
            // copy bitmap asset to location in $2xxxx - $5xxxx
            toTileAddr = 0x20000 + 64L * (x0 + ((y + y0) * gScreenColumns));
            if(toTileAddr >= 0x2dd00) toTileAddr += 0x2300; // C64 kernal
            charIndex = toTileAddr / 64L;
            if(mergeTiles) {
                lcopy_transparent(fromTileAddr, toTileAddr, 64 * t_w, 0);
            } else {
                lcopy(fromTileAddr, toTileAddr, 64 * t_w);
            }
        } else {
            // use pointer directly to bitmap asset
            charIndex = info->baseAdr / 64L + t_x + (y * info->columns);
        }
        for (x = t_x; x < t_x + t_w; ++x)
        {
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
        out = asciiToPetscii(*current++);
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

