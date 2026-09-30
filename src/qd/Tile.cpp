/*
	File:		qd/Tile.cpp

	Contains:	TTile, a fax page turned a quarter a tile at a time
				(qd/Tile.h).

				The bits are the ARM's big-endian words: a word read from a
				tile goes through toolbox/ByteOrder.h, and the rows are
				moved into and out of the tiles as the bytes they are.

	Reconstructed from the MP2x00 US ROM (0x002540c0-0x002550f8, and
	QDPatchpoint 0x0033f548); each function cites its origin.
*/

#include "Tile.h"
#include "Ports.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include "OSErrors.h"
#include "ByteOrder.h"
#include "views/Application.h"
#include "stores/LargeObjects.h"
#include <string.h>

#define kTileBytes		0x200
#define kTileRows		64
#define kNotifyNoMemory	-7000		// the ROM's 0xffffe4a8: the alert ErrorNotify shows


// ROM 0x0033f548 QDPatchpoint__Fv
// (A place for a patch to hook; nothing.)
void
QDPatchpoint(void)
{ }


static void
TileOutOfMemory(void)
{
	ErrorNotify(kNotifyNoMemory, 3);
	Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
}


// ROM 0x002540c0 __ct__5TTileFP8PixelMapRC6RefVar
TTile::TTile(PixelMap* pm, RefArg options)
{
	fCallback = NILREF;
	fArgs = NILREF;
	long words = pm->rowBytes >> 2;
	fWordsPerRow = words;
	fRowsLeft = pm->bounds.bottom - pm->bounds.top;
	fTileCount = (pm->deviceRes.v == 196 || pm->deviceRes.h == 196) ? 36 : 27;
	long across = (pm->rowBytes * 8 + 63) / 64;
	fTilesAcross = across;
	fLastWords = 2 - (across * 2 - words);
	fLastSkip = 2 - fLastWords;
	fLastTile = across - 1;
	for (long i = 0; i < fTileCount; i++)
	{
		if ((fTiles[i] = (UChar*) NewPtr(kTileBytes)) == nil)
		{
			for (i--; i >= 0; i--)
				DisposPtr((Ptr) fTiles[i]);
			TileOutOfMemory();
		}
	}
	VAddrToStore(&fStore, &fObjectId, (ULong) pm);
	fHasCallback = false;
	if (NOTNIL(options) && FrameHasSlot(options, RSSYMcallback))
	{
		fHasCallback = true;
		fCallback = GetFrameSlot(options, RSSYMcallback);
		fArgs = MakeArray(1);
	}
}


// ROM 0x00254294 __dt__5TTileFv
TTile::~TTile()
{
	if (fTiles[0] != nil)
		for (long i = 0; i < fTileCount; i++)
			DisposPtr((Ptr) fTiles[i]);
}


void
TTile::TellCallback(long percent)
{
	SetArraySlot(fArgs, 0, RefVar(MAKEINT(percent)));
	NSCall(fCallback, fArgs);
}


// How far a turn has got: bands - left of bands, as 0 to 50, rounded.
static long
TurnPercent(long bands, long left, Fixed plus)
{
	Fixed done = FixedDivide((Fixed) ((bands - left) << 16), (Fixed) (bands << 16));
	return (short) ((FixedMultiply(done, 50 << 16) + plus) >> 16);
}


// ROM 0x00254304 TileBuffer__5TTileFPUc
// The next 64 rows (or what is left) read into the tiles, each tile's row
// eight bytes; rows all white leave the tiles white instead.  (A band of
// fewer than 64 rows leaves the rest of each tile as the last band left
// it.)
void
TTile::TileBuffer(UChar* rows)
{
	long count = (fRowsLeft < kTileRows) ? fRowsLeft : kTileRows;
	long words = fWordsPerRow * count;
	if ((fRowsLeft -= kTileRows) < 0)
		fRowsLeft = 0;
	Boolean blank = true;
	for (UChar* p = rows; words != 0; words--, p += 4)
		if (GetBigEndianWord(p) != 0)
		{
			blank = false;
			break;
		}
	if (blank)
	{
		for (long i = 0; i < fTilesAcross; i++)
			ZeroBytes(fTiles[i], kTileBytes);
		return;
	}
	UChar** into = (UChar**) new UChar*[fTileCount];
	if (into == nil)
		TileOutOfMemory();
	for (long i = 0; i < fTilesAcross; i++)
		into[i] = fTiles[i];
	for ( ; count != 0; count--)
	{
		long i;
		for (i = 0; i < fLastTile; i++)
		{
			memcpy(into[i], rows, 8);
			into[i] += 8;
			rows += 8;
		}
		memcpy(into[i], rows, fLastWords * 4);
		rows += fLastWords * 4;
		into[i] += (fLastWords + fLastSkip) * 4;
	}
	delete[] into;
	FlushLargeObject(fStore, fObjectId);
}


// ROM 0x0025449c Untile__5TTileFP8PixelMap
// The turned bitmap, tile after tile, put back into rows a band of 64 at a
// time: each tile's rows given out to the tiles across the band, then the
// tiles copied back over the band.
void
TTile::Untile(PixelMap* pm)
{
	long bands = (pm->bounds.bottom - pm->bounds.top) / kTileRows;
	long bandBytes = pm->rowBytes << 6;
	long across = (pm->rowBytes * 8 + 63) / 64;
	long chunks = across << 6;
	long words = pm->rowBytes >> 2;
	UChar* from = (UChar*) GetPixelMapBits(pm);
	UChar* band = from;
	FlushLargeObject(fStore, fObjectId);
	long left = bands - 1;
	if (bands == 0)
		return;
	do
	{
		long tile = 0, offset = 0, i;
		for (long n = chunks; n != 0; n--)
		{
			memcpy(fTiles[tile] + offset * 4, from, 8);
			from += 8;
			offset += words;
			if (offset >= 0x80)
			{
				if (++tile >= across)
				{
					tile = 0;
					offset += 2;
				}
				offset -= 0x80;
			}
		}
		UChar* to = band;
		for (i = 0; i < across; i++, to += kTileBytes)
			BlockMove(fTiles[i], to, kTileBytes);
		band += bandBytes;
		FlushLargeObject(fStore, fObjectId);
		if (fHasCallback && (left & 3) == 0)
			TellCallback(TurnPercent(bands, left, 0x328000));
	} while (left-- != 0);
}


// A group of eight rows of a tile's 32-bit column (rows eight bytes apart)
static inline Boolean
ReadColumn(const UChar* in, uint32_t row[8])
{
	uint32_t any = 0;
	for (int k = 0; k < 8; k++)
		any |= (row[k] = GetBigEndianWord(in + 8 * k));
	return any != 0;
}


// ROM 0x00254668 RotateTilesR__5TTileFP8PixelMapT1
// A quarter turn to the right: each band of the page (64 rows) read into
// the tiles, and each tile turned into its own 512 bytes of the new bitmap
// - the page's left-hand tile to the new bitmap's top band, the page's
// top band to its right-hand tiles - eight rows of a column making a byte.
// The rows past the last whole band are turned from the byte columns of
// their tiles.  ROM QUIRK: only whole groups of eight of those rows are
// turned; the rest are dropped.
void
TTile::RotateTilesR(PixelMap* from, PixelMap* to)
{
	long fromBand = from->rowBytes << 6;
	long toBand = ((to->rowBytes * 8 + 63) / 64) << 9;
	long bands = fRowsLeft / kTileRows;
	long leftover = fRowsLeft - bands * kTileRows;
	UChar* rows = (UChar*) GetPixelMapBits(from);
	UChar* place = (UChar*) GetPixelMapBits(to) + toBand;
	QDPatchpoint();
	for (long n = bands; n != 0; n--)
	{
		if (fHasCallback && (n & 3) == 0)
			TellCallback(TurnPercent(bands, n, 0x8000));
		TileBuffer(rows);
		rows += fromBand;
		place -= kTileBytes;
		UChar* column = place + 7;
		for (long t = 0; t < fTilesAcross; t++, column += toBand)
		{
			const UChar* in = fTiles[t];
			UChar* groupOut = column;
			UChar* out = column;
			for (int g = 8; g != 0; g--)
			{
				for (int w = 2; w != 0; w--, in += 4)
				{
					uint32_t row[8];
					if (!ReadColumn(in, row))
					{
						out += 0x100;
						continue;
					}
					for (int b = 32; b != 0; b--)
					{
						UChar v = 0;
						for (int k = 7; k >= 0; k--)
						{
							v = (UChar) ((v << 1) | (row[k] >> 31));
							row[k] <<= 1;
						}
						*out = v;
						out += 8;
					}
				}
				in += 0x38;
				out = --groupOut;
			}
		}
	}
	QDPatchpoint();
	if (leftover != 0)
	{
		TileBuffer(rows);
		UChar* column = place - kTileBytes + 7;
		long groups = leftover >> 3;
		for (long t = 0; t < fTilesAcross; t++, column += toBand)
		{
			const UChar* in = fTiles[t];
			UChar* groupOut = column;
			UChar* out = column;
			for (long g = groups; g != 0; g--)
			{
				for (int c = 8; c != 0; c--, in++)
				{
					UChar row[8];
					UChar any = 0;
					for (int k = 0; k < 8; k++)
						any |= (row[k] = in[8 * k]);
					if (any == 0)
					{
						out += 0x40;
						continue;
					}
					for (int b = 8; b != 0; b--)
					{
						UChar v = 0;
						for (int k = 7; k >= 0; k--)
						{
							v = (UChar) ((v << 1) | (row[k] >> 7));
							row[k] = (UChar) (row[k] << 1);
						}
						*out = v;
						out += 8;
					}
				}
				in += 0x38;
				out = --groupOut;
			}
		}
	}
	QDPatchpoint();
	if (fHasCallback)
		TellCallback(50);
	Untile(to);
	if (fHasCallback)
		TellCallback(100);
}


// ROM 0x00254bac RotateTilesL__5TTileFP8PixelMapT1
// A quarter turn to the left: as RotateTilesR the other way round - the
// page's top band to the new bitmap's left-hand tiles, its left-hand tile
// to the bottom band, each tile filled from its last row upwards.  ROM
// BUG: the rows past the last whole band are turned as RotateTilesR turns
// them - downwards from the last row of their tile, the bits the other way
// up, though a blank byte column moves back up - so they come out wrong,
// and are written past the end of their tile: for the page's left-hand
// tile, which lands in the new bitmap's last band, up to 512 bytes past
// the end of the new bitmap.  (A fax page's last 58 rows are usually
// white, and white rows write nothing; when they are not, turning the
// page left overwrites whatever follows the new bitmap.)
void
TTile::RotateTilesL(PixelMap* from, PixelMap* to)
{
	long fromBand = from->rowBytes << 6;
	long toBand = ((to->rowBytes * 8 + 63) / 64) << 9;
	long bands = fRowsLeft / kTileRows;
	long leftover = fRowsLeft - bands * kTileRows;
	UChar* rows = (UChar*) GetPixelMapBits(from);
	UChar* place = (UChar*) GetPixelMapBits(to) + toBand * (fTilesAcross - 1) - kTileBytes;
	QDPatchpoint();
	for (long n = bands; n != 0; n--)
	{
		if (fHasCallback && (n & 3) == 0)
			TellCallback(TurnPercent(bands, n, 0x8000));
		TileBuffer(rows);
		rows += fromBand;
		place += kTileBytes;
		UChar* column = place + 0x1f8;
		for (long t = 0; t < fTilesAcross; t++, column -= toBand)
		{
			const UChar* in = fTiles[t];
			UChar* groupOut = column;
			UChar* out = column;
			for (int g = 8; g != 0; g--)
			{
				for (int w = 2; w != 0; w--, in += 4)
				{
					uint32_t row[8];
					if (!ReadColumn(in, row))
					{
						out -= 0x100;
						continue;
					}
					for (int b = 32; b != 0; b--)
					{
						UChar v = 0;
						for (int k = 0; k < 8; k++)
						{
							v = (UChar) ((v << 1) | (row[k] >> 31));
							row[k] <<= 1;
						}
						*out = v;
						out -= 8;
					}
				}
				in += 0x38;
				out = ++groupOut;
			}
		}
	}
	QDPatchpoint();
	if (leftover != 0)
	{
		TileBuffer(rows);
		UChar* column = place + 0x3f8;
		long groups = leftover >> 3;
		// DEVIATION: the bytes the BUG above writes past the end of the new
		// bitmap are dropped.  On the device they landed in whatever block
		// followed it; on the host that is heap corruption (or, for a large
		// binary, past the host block the object is mapped into).  Nothing
		// in the bitmap changes: Untile reads only the bitmap's own bytes.
		UChar* bitsStart = (UChar*) GetPixelMapBits(to);
		UChar* bitsEnd = bitsStart + (long) to->rowBytes * (to->bounds.bottom - to->bounds.top);
		for (long t = 0; t < fTilesAcross; t++, column -= toBand)
		{
			const UChar* in = fTiles[t];
			UChar* groupOut = column;
			UChar* out = column;
			for (long g = groups; g != 0; g--)
			{
				for (int c = 8; c != 0; c--, in++)
				{
					UChar row[8];
					UChar any = 0;
					for (int k = 0; k < 8; k++)
						any |= (row[k] = in[8 * k]);
					if (any == 0)
					{
						out -= 0x40;
						continue;
					}
					for (int b = 8; b != 0; b--)
					{
						UChar v = 0;
						for (int k = 7; k >= 0; k--)
						{
							v = (UChar) ((v << 1) | (row[k] >> 7));
							row[k] = (UChar) (row[k] << 1);
						}
						if (out >= bitsStart && out < bitsEnd)
							*out = v;
						out += 8;
					}
				}
				in += 0x38;
				out = ++groupOut;
			}
		}
	}
	QDPatchpoint();
	if (fHasCallback)
		TellCallback(50);
	Untile(to);
	if (fHasCallback)
		TellCallback(100);
}
