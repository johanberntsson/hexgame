Tiles must be sized to a multiple of 8 pixels for both width and height.

The hexagons are 48x56 (6x7 characters). They are pointy-top: vertices at the
top and bottom centre, vertical sides down each flank. A row steps 48 pixels
right; the next row steps 24 right and 40 down, so rows overlap by 16 pixels
and the transparent corners of one tile are filled by its neighbours.

  make            rebuild tiles.png and hexgame.png from the parts

tiles.png is the top 7 character rows of hexgame.png, and holds four things:

  t_x  0   goldtile.png    6x7   the empty hexagon
  t_x  6   whitestone.png  6x7
  t_x 12   blackstone.png  6x7
  t_x 18   borders.png    22x7   the four edge rims

cursor.png is no longer part of the sheet. The pointer is hardware sprite 0
(src/input.c) and nothing has drawn the tile since; its six columns are what
the rims are made of.

borders.png -- the edge rims
----------------------------

Hex is won by connecting two opposite edges, and which two is the whole game:
white goes left to right, black top to bottom. The board is 78 of the screen's
80 columns wide and 47 of its 50 rows tall, so there is no room for a border
*around* it. The rims go on the outer face of the edge hexagons instead, and
draw_board puts them on after the stone.

Layout inside the 176x56 image (every strip is in the sheet's top row, so all
of them are read with t_y 0):

  x   0.. 23   rows  0..55   left rim    3x7   drawn at the cell's own column
  x  24.. 47   rows  0..55   right rim   3x7   drawn 3 characters further right
  x  48.. 95   rows  0..23   top rim     6x3   drawn on the cell's own row
  x  96..143   rows  0..23   bottom rim  6x3   drawn 4 rows down
  x 144..175                 spare

**Each rim is exactly two faces, and only the ones that are on the board's
boundary.** A hexagon has six, and a cell in column 0 still has neighbours
across four of them -- (0,y-1) sits on its upper-left face and (0,y+1) on its
lower-right -- so a rim drawn there is inside the board, and the edge grows
teeth pointing inwards. What is exposed is:

  left    L + LL    the left flank and the lower-left face
  right   R + UR    the right flank and the upper-right, the rhombus leaning
                    the other way
  top     UL + UR   both faces of the upper cap
  bottom  LL + LR   both faces of the lower cap

Two of the four corners overlap on one face -- (8,0) on UR and (0,8) on LL --
and draw_board puts the black rims on last, so those two read as black. The
other two corners, (0,0) and (8,8), take both colours on separate faces.

**Keep the rim within 5 pixels of the outline.** whitestone.png starts 5
pixels in from the sides and 8 down from the apex; a thicker rim is drawn over
the stone rather than beside it.

Re-indexing
-----------

img-src/hexgame.png is the same image in indexed colour, and it is what
png2fci reads. **Palette entry 0 is the transparent colour** -- fc_displayTile
merges with lcopy_transparent(..., 0) -- so after Image/Mode/Indexed... in Gimp
check that entry 0 is still the transparent colour and that no visible artwork
landed on it, or every merged tile will have holes in it. Set the maximum
number of colours to 239 or less: png2fci -r reserves the rest for the system
palette.

Indexing also thresholds the antialiased edge of the hexagon a pixel or two in,
so the outline that reaches the screen is the one in img-src/hexgame.png, not
the one in goldtile.png. Draw the rims against the former.
