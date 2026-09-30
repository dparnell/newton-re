/*
	File:		qd/Tile.h

	Contains:	TTile, a quarter turn of a bitmap too big to hold twice in
				memory - a fax page (Tilable): the page is read 64 rows at a
				time into 64 x 64-pixel tiles (512 bytes each, 27 of them
				across a 1728-pixel line, 36 made for a fine page), each
				tile turned into its place in the new bitmap, which is left
				as tiles one after another; then Untile puts each band of
				the new bitmap back into rows, using the same tiles as its
				scratch.  A bitmap in a large binary on a store is flushed
				as it goes (VAddrToStore, FlushLargeObject), and an options
				frame's callback is told how far it has got (0 to 50 while
				turning, 50 to 100 while untiling).

				The ROM's class; its declaration is not in the DDK, so the
				names of the fields are ours, their order the ROM's (offsets
				noted, for the ROM's layout of 0xc0 bytes).

	Reconstructed from the MP2x00 US ROM (0x002540c0-0x002550f8); each
	function cites its origin.  RotTiledBitmap, which drives it, is
	qd/MungeBitmap.cpp.
*/

#ifndef __QD_TILE_H
#define __QD_TILE_H

#include "Newton.h"
#include "NewtQD.h"
#include "Frames.h"

class TStore;

class TTile
{
public:
				TTile(PixelMap* pm, RefArg options);				// ROM 0x002540c0 __ct__5TTileFP8PixelMapRC6RefVar
				~TTile();											// ROM 0x00254294 __dt__5TTileFv

	void		TileBuffer(UChar* rows);							// ROM 0x00254304 TileBuffer__5TTileFPUc
	void		Untile(PixelMap* pm);								// ROM 0x0025449c Untile__5TTileFP8PixelMap
	void		RotateTilesR(PixelMap* from, PixelMap* to);		// ROM 0x00254668 RotateTilesR__5TTileFP8PixelMapT1
	void		RotateTilesL(PixelMap* from, PixelMap* to);		// ROM 0x00254bac RotateTilesL__5TTileFP8PixelMapT1

	void		TellCallback(long percent);

	long		fWordsPerRow;			// +0x00  of the bitmap being tiled
	long		fRowsLeft;				// +0x04  not yet read into tiles
	long		fTilesAcross;			// +0x08
	long		fTileCount;				// +0x0c  made: 36 for a fine page, 27 otherwise
	long		fLastTile;				// +0x10  fTilesAcross - 1
	long		fLastWords;				// +0x14  the words of a row in the last tile
	long		fLastSkip;				// +0x18  and the words it leaves over
	UChar*		fTiles[36];				// +0x1c  512 bytes each (NewPtr)
	TStore*		fStore;					// +0xac  the bitmap's large object's, if it is one
	ULong		fObjectId;				// +0xb0
	Boolean		fHasCallback;			// +0xb4
	RefStruct	fCallback;				// +0xb8  the options' callback
	RefStruct	fArgs;					// +0xbc  [percent]
};

void	QDPatchpoint(void);				// ROM 0x0033f548 QDPatchpoint__Fv - nothing

#endif	/* __QD_TILE_H */
