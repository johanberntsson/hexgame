Tiles must be sized to a multiple of 8 pixels for both width and height

I use 48/56 pixels

convert +append goldtile.png whitestone.png blackstone.png cursor.png tiles.png

Once the tiles.png is created it should be changed to indexed format
in Gimp: Image/Mode/Indexed...

Set max number of colors to 239 or less (need space for the std palette)
