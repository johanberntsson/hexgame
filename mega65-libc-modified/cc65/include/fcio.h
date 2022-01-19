
/*! @file fcio.h
    @brief Full color mode i/o library for the MEGA65
    
    Details.
*/

#ifndef __FCIO
#define __FCIO

#include <memory.h>
#include <stdbool.h>

#define FCBUFSIZE 0xff

#define FC_COLOR_BLACK        0
#define FC_COLOR_WHITE        1
#define FC_COLOR_RED          2
#define FC_COLOR_CYAN         3
#define FC_COLOR_PURPLE       4
#define FC_COLOR_GREEN        5
#define FC_COLOR_BLUE         6
#define FC_COLOR_YELLOW       7
#define FC_COLOR_ORANGE       8
#define FC_COLOR_BROWN        9
#define FC_COLOR_PINK         10
#define FC_COLOR_GREY1        11
#define FC_COLOR_DARKGREY     11
#define FC_COLOR_GREY2        12
#define FC_COLOR_GREY         12
#define FC_COLOR_MEDIUMGREY   12
#define FC_COLOR_LIGHTGREEN   13
#define FC_COLOR_LIGHTBLUE    14
#define FC_COLOR_GREY3        15
#define FC_COLOR_LIGHTGREY    15

#ifndef byte
typedef unsigned char byte;
#endif

#ifndef himemPtr
typedef unsigned long himemPtr;
#endif

#ifndef word
typedef unsigned int word;
#endif

typedef struct _fcioConf {
    himemPtr screenBase;
    himemPtr reservedBitmapBase;
    himemPtr reservedPaletteBase;
    himemPtr dynamicPaletteBase;
    himemPtr dynamicBitmapBase;
    himemPtr colorBase;
} fcioConf;

extern char *fcbuf;

// --------------- graphics ------------------

typedef struct _fciInfo
{
    himemPtr baseAdr;        ///< bitmap base address
    himemPtr paletteAdr;     ///< palette data base address
    byte paletteSize;        ///< size of palette (in palette entries)
    bool reservedSysPalette; ///< if true, don't use colors 0-15
    byte columns;            ///< number of character columns for image
    byte rows;               ///< number of character rows
    word size;               ///< size of bitmap
} fciInfo;

typedef struct _textwin
{
    byte xc;            ///< current cursor X position
    byte yc;            ///< current cursor Y position
    byte x0;            ///< X origin
    byte y0;            ///< Y origin
    byte width;         ///< window width
    byte height;        ///< window height
    byte textcolor;     ///< current window text color
    byte extAttributes; ///< current window extended text attributes
} textwin;

extern byte gScreenColumns;  ///< number of screen columns (in characters)
extern byte gScreenRows;     ///< number of screen rows (in characters)
extern byte gScreenRRW;      ///< size of raster rewrite buffer (in characters)
extern textwin *gCurrentWin; ///< current window

// --- general & initializations ---

/**
 * @brief initialize fcio library with given initial screen mode
 * 
 * @param h640 horizontal resolution; true: 640px, false: 320px
 * @param v400 vertical resolution; true: 400px, false: 200px
 * @param config pointer to FCIO config block (or NULL for standard configuration) 
 * @param rows character rows (or 0 for default row count)
 * @param rrw_size raster rewrite size (or 0 no rrw)
 * @param reservedBitmapFile reserved bitmap to load upon initialization (or NULL for none)
 */
void fc_init(byte h640, byte v400, fcioConf *config, byte rows, byte rrw_size, char *reservedBitmapFile);

/**
 * @brief set new full color screen mode
 * 
 * @param h640 horizontal resolution; true: 640px, false: 320px
 * @param v400 vertical resolution; true: 400px, false: 200px
 * @param rows character rows (or 0 for standard configuration)
 * @param rrw_size raster rewrite size (or 0 no rrw)
 */
void fc_screenmode(byte h640, byte v400, byte rows, byte rrw_size);

/**
 * @brief fall back to 8 bit screen mode
 * 
 */
void fc_go8bit();

/**
 * @brief display fatal error message and stop execution
 * 
 * @param format printf-style format string
 * @param ... format string parameters
 */
void fc_fatal(const char *format, ...);

// ----------------------------------------------------------------------------
// lines and blocks
// ----------------------------------------------------------------------------

/**
 * @brief draw a block in the current window
 * 
 * @param x0 origin x
 * @param y0 origin y
 * @param width block width
 * @param height block height
 * @param character ascii character to plot
 * @param col color to plot
 */
void fc_block(byte x0, byte y0, byte width, byte height, byte character, byte col);

/**
 * @brief draw a horizontal line using extended (==fcm bitmap) characters
 * 
 * @param x origin x
 * @param y origin y
 * @param width width
 * @param lineChar bitmap character index to use
 */
void fc_hlinexy(byte x, byte y, byte width, byte lineChar);

/**
 * @brief draw a vertical line using extended (==fcm bitmap) characters
 * 
 * @param x origin x
 * @param y origin y
 * @param height height
 * @param lineChar bitmap character index to use
 */
void fc_vlinexy(byte x, byte y, byte height, byte lineChar);
void fc_line(byte x, byte y, byte width, byte character, byte col);

// ----------------------------------------------------------------------------
// --- keyboard input ---
// ----------------------------------------------------------------------------

void fc_emptyBuffer(void);
unsigned char fc_cgetc(void);
char fc_getkey(void);
char fc_getkeyP(byte x, byte y, const char *prompt);

/**
 * @brief get user input
 * 
 * @param maxlen maximum length
 * @return char* a newly allocated area in memory containing the input string
 */
char *fc_input(byte maxlen);
int fc_getnum(byte maxlen);

// ----------------------------------------------------------------------------
// --- string and character output ---
// ----------------------------------------------------------------------------

/**
 * @brief clears the current text window
 * 
 */
void fc_clrscr();

/**
 * @brief put character at current cursor position
 * 
 * @param c the character
 */
void fc_putc(char c);

/**
 * @brief put string at current cursor position
 * 
 * @param s the string
 */
void fc_puts(const char *s);

/**
 * @brief put string at given position in current window
 * 
 * @param x 
 * @param y 
 * @param s the string
 */
void fc_putsxy(byte x, byte y, char *s);

/**
 * @brief put character at given position in current window
 * 
 * @param x 
 * @param y 
 * @param c the character
 */
void fc_putcxy(byte x, byte y, char c);

/**
 * @brief print string at current cursor position
 * 
 * @param format format string
 * @param ... format parameters
 */
void fc_printf(const char *format, ...);

/**
 * @brief set cursor position in current window
 * 
 * @param x 
 * @param y 
 */
void fc_gotoxy(byte x, byte y);

/**
 * @brief get cursor x position
 * 
 * @return byte cursor y position
 */
byte fc_wherex();

/**
 * @brief get cursor y position
 * 
 * @return byte cursor y position
 */
byte fc_wherey();

/**
 * @brief set auto CR
 * 
 * @param a whether to automatically issue a CR when the text cursor reaches the right border
 */
void fc_setAutoCR(bool a);


/**
 * @brief set active window
 * 
 * @param aWin window to activate
 */
void fc_setwin(textwin *aWin);

/**
 * @brief convenience method to create a window structure
 * 
 * @param x0 x origin
 * @param y0 y origin
 * @param width window width
 * @param height window height
 * @return textwin* the created window structure
 */
textwin *fc_makeWin(byte x0, byte y0, byte width, byte height);

/**
 * @brief reset text window to whole screen
 * 
 */
void fc_resetwin();

/**
 * @brief cursor control
 * 
 * @param onoff 1=on, 0=off
 */
void fc_cursor(byte onoff);

/**
 * @brief center string at given position
 * 
 * @param x x origin
 * @param y y origin
 * @param width width of area
 * @param text text to center
 */
void fc_center(byte x, byte y, byte width, char *text);

/**
 * @brief scrolls the content of the current window up
 * 
 */
void fc_scrollUp();

/**
 * @brief scrolls the contents of the current window down
 * 
 */
void fc_scrollDown();

// ----------------------------------------------------------------------------
// color, attributes and palette handling
// ----------------------------------------------------------------------------

/**
 * @brief set or reset the revers attribute
 * 
 * @param f reverse attribute flag
 */
void fc_revers(byte f);

/**
 * @brief set or reset the flash attribute
 * 
 * @param f flash attribute flag
 */
void fc_flash(byte f);

/**
 * @brief set or reset the bold attribute
 * 
 * @param f bold attribute flag
 */
void fc_bold(byte f);

/**
 * @brief set or reset the underline attribute
 * 
 * @param f underline attribute flag
 */
void fc_underline(byte f);

/**
 * @brief load palette data into the VIC
 * 
 * @param adr address palette data (de-nybellized triplets)
 * @param size size (in palette entries)
 * @param reservedSysPalette whether to overwrite colors 0-15
 */
void fc_loadPalette(himemPtr adr, byte size, byte reservedSysPalette);

/**
 * @brief set palette entry
 * 
 * @param num palette index (0-255)
 * @param red red (0-255)
 * @param green green (0-255)
 * @param blue blue (0-255)
 */
void fc_setPalette(int num, byte red, byte green, byte blue);

/**
 * @brief load FCI palette into the VIC
 * 
 * @param info pointer to FCI image info block
 */
void fc_loadFCIPalette(fciInfo *info);

/**
 * @brief set palette entries to black
 * 
 * @param reservedSysPalette exclude colors 0-15
 */
void fc_zeroPalette(byte reservedSysPalette);

/**
 * @brief fade palette to or from the desired values
 * 
 * @param adr address of palette data
 * @param size number of palette entries
 * @param reservedSysPalette exclude colors 0-15
 * @param steps number of steps for fade (max. 255)
 * @param fadeOut if true, fade out rather than in
 */
void fc_fadePalette(himemPtr adr, byte size, byte reservedSysPalette, byte steps, bool fadeOut);

/**
 * @brief reset palette to reserved FCI palette
 * 
 * If a reserved FCI is used (see @a fc_loadReservedFCI), reset the current
 * palette to the reserved one.
 */
void fc_resetPalette();

/**
 * @brief set text color of current window
 * 
 * @param c text color index
 */
void fc_textcolor(byte c);

/**
 * @brief set border color
 * 
 */
#define fc_bordercolor(C) POKE(0xd020u, C)

/**
 * @brief set background color
 * 
 */
#define fc_bgcolor(C) POKE(0xd021u, C)

// ----------------------------------------------------------------------------
// graphic areas
// ----------------------------------------------------------------------------

/**
 * @brief free automatically allocated graphic areas
 * 
 */
void fc_freeGraphAreas(void);

/**
 * @brief Adds a graphics rectangle to the screen.
 * 
 * @param x0 x origin (in characters)
 * @param y0 y origin (in characters)
 * @param width width (in characters)
 * @param height height (in characters)
 * @param bitmapData address of bitmap data
 * 
 * Display a bitmap that has been loaded with @a fc_loadFCI without
 * setting the palette (useful for fading in images after loading)
 * 
 */
void fc_addGraphicsRect(byte x0, byte y0, byte width, byte height,
                        himemPtr bitmapData);

/**
 * @brief allocate memory for FCI file and load it
 *
 * @param filename name of FCI file to load
 * @param address address to load bitmap (or 0 for automatic allocation)
 * @param pAddress address to load palette (or 0 for automatic allocation)
 * @return fciInfo* info block containging start address and metadata
 * 
 * This function is used to load an FCI image into memory. If used with automatic
 * memory allocation (0 for @a address and @a pAddress), memory is taken from banks 4 
 * and 5 to store the bitmap data.
 * 
 * When automatically alloacting graphic areas, it is quite possible to run 
 * out of memory. Therefore, it is advisable to always clear previously allocated
 * graphic areas with @a fc_freeGraphicAreas after usage.
 * 
 * @warning only loads the picture, doesn't display it!
 * 
 */
fciInfo *fc_loadFCI(char *filename, himemPtr address, himemPtr paletteAddress);

/**
 * @brief display FCI image
 * 
 * @param info FCI image info block
 * @param x0 x origin (in characters)
 * @param y0 y origin (in characters)
 * @param setPalette also overwrites current palette with image palette if true
 */
void fc_displayFCI(fciInfo *info, byte x0, byte y0, bool setPalette);

/**
 * @brief fade in FCI image
 * 
 * @param info FCI image info block
 * @param x0 x origin (in characters)
 * @param y0 y origin (in characters)
 * @param steps number of steps for fade (255=max)
 */
void fc_fadeFCI(fciInfo *info, byte x0, byte y0, byte steps);

/**
 * @brief load FCI file and display it
 *
 * @param filename FCI file to load
 * @param x0 origin x
 * @param y0 origin y
 * @return fciInfo* associated fciInfo block for file
 */
fciInfo *fc_displayFCIFile(char *filename, byte x0, byte y0);

/**
 * @brief plot extended (==full color) character
 * 
 * @param x screen column
 * @param y screen row
 * @param c character index
 */
void fc_plotExtChar(byte x, byte y, byte c);

/**
 * @brief plot petscii character
 * 
 * @param x screen column
 * @param y screen row
 * @param c character code
 * @param color character color
 * @param exAttr extended attributes
 */
void fc_plotPetsciiChar(byte x, byte y, byte c, byte color, byte exAttr);

/**
 * @brief display tile (part of a FCI image)
 * 
 * @param info FCI image info block
 * @param x0 x origin (in characters)
 * @param y0 y origin (in characters)
 * @param xt x origin of tile (in characters)
 * @param yt y origin of tile (in characters)
 * @param xw width of tile (in characters)
 * @param xh height of tile (in characters)
 * @param mergeTiles if transparancy is used when adding tiles
 */
void fc_displayTile(fciInfo *info, byte x0, byte y0, byte xt, byte yt, byte yw, byte yh, byte mergeTiles);

/**
 * @brief start adding raster rewrite buffer data
 * 
 */
void fc_rrw_begin();

/**
 * @brief add string to raster rewrite buffer
 * 
 * @param row screen row
 * @param s the string
 */
void fc_rrw_puts(byte x, byte y, byte color, const char *s);

/**
 * @brief add tile to raster rewrite buffer
 * 
 * @param x screen column
 * @param y screen row
 * @param info FCI image info block
 * @param t_x x origin of tile (in characters)
 * @param t_y y origin of tile (in characters)
 * @param t_w width of tile (in characters)
 * @param t_h height of tile (in characters)
 */
void fc_rrw_tile(byte x, byte y, fciInfo *info, byte t_x, byte t_y, byte t_w, byte t_h);


/**
 * @brief stop adding raster rewrite buffer data
 * 
 */
void fc_rrw_end();

/**
 * @brief use $20000 - $5ffff for tiles that can be modified independently
 * 
 */
void fc_setUniqueTileMode();


#endif
