# The tile sheet.  tiles.png is the 40x7 character strip at the top of
# hexgame.png; hex.png is the title logo below it.
#
#   t_x  0  goldtile     6x7   the empty hexagon
#   t_x  6  whitestone   6x7
#   t_x 12  blackstone   6x7
#   t_x 18  borders      22x7  the four edge rims -- see readme.txt
#
# cursor.png is no longer in the sheet: the pointer is sprite 0 (src/input.c)
# and nothing has drawn the tile since.  Its 6 columns are what the rims use.
#
# Everything the board is drawn from is in the top seven rows, so every tile
# above is read with t_y 0.  The title logo below them (t_y 8) is the only
# caller that reaches further down -- which used to corrupt the tile store of
# the cells beneath it, until fc_displayTile was fixed in 2026.

all: hexgame.png

borders.png:
	@echo "borders.png is hand art -- see readme.txt for the layout"

tiles.png: goldtile.png whitestone.png blackstone.png borders.png
	convert +append $^ $@

hexgame.png: tiles.png hex.png
	convert -append tiles.png hex.png $@
	@echo "Now re-index it into img-src/hexgame.png -- see readme.txt."

.PHONY: all
