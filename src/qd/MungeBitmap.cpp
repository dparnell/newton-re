/*
	File:		qd/MungeBitmap.cpp

	Contains:	A bitmap turned or flipped in place: MungeBitmap(bitmap,
				operation, options) and the five routines under it.

				The bits are worked on as the ARM sees them, which is as
				big-endian words (the leftmost pixel in the top bit), so
				the host reads and writes them through toolbox/ByteOrder.h.
				A quarter turn makes a new 'pixels object (the rows as
				long as the old bitmap is high) and puts it in the frame's
				`data` slot, the bounds and the resolution turned with it;
				the other three work in the bits the bitmap has.  An
				options frame's `callback` is told how far a half turn has
				got, in percent.

				NOT YET RECONSTRUCTED: a quarter turn of a fax page
				(Tilable) is RotTiledBitmap 0x00040b54, over TTile
				(0x002540c0; RotateTilesR 0x00254668, RotateTilesL
				0x00254bac): the turned copy is made on the
				same store and with the same compander as the page's own
				large binary (FGetBinaryStore, FGetBinaryCompander,
				FGetBinaryCompanderData into MakePixelsObject) and filled a
				tile at a time, so a page is never all in memory at once -
				and the VAddrToStore/FlushLargeObject calls that keep a
				large binary's bits in step in RotBitmap180.  Nothing in
				the host reaches it: MakePixelsObject's store arm
				(FLBAllocCompressed) is NOT YET and there are no large
				binaries (IsLargeBinary answers false), and the fax
				receiver that makes such pages is part of the comms stack,
				which is NOT YET.  Only a script's own heap bitmap of
				exactly a fax page's size gets there, and it is left as it
				was.

	Reconstructed from the MP2x00 US ROM (0x0003f764-0x00040b54,
	0x00040ee0); each function cites its origin.
*/

#include "Pictures.h"
#include "Ports.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "ByteOrder.h"
#include <string.h>

extern const unsigned char	bitFlip[256];		// the bits of each byte reversed

static const char kGrafException[] = "evt.ex.graf";
const long kGrafErrBadOperation = -8802;		// the ROM's 0xffffdd9e: no such munge

static inline long	RowsOf(const PixelMap* pm)		{ return pm->bounds.bottom - pm->bounds.top; }
static inline long	ColumnsOf(const PixelMap* pm)	{ return pm->bounds.right - pm->bounds.left; }


// ROM 0x00040ee0 Tilable__FP8PixelMap
// Whether the bitmap is a fax page, and so is turned a tile at a time: the
// four sizes are 216-byte rows (1728 pixels, a G3 fax line) by 1146,
// 2292, 1152 or 2304 rows - standard and fine resolution, two page
// lengths.  (Not the screen: the MP2x00's is 40 bytes by 480 rows.)
Boolean
Tilable(PixelMap* pm)
{
	ULong size = (ULong) (long) pm->rowBytes * (ULong) RowsOf(pm);
	return size == 0x3c6f0 || size == 0x78de0 || size == 0x3cc00 || size == 0x79800;
}


// ROM 0x0003f93c RotBitmap180__FRC6RefVarT1
// A half turn: the bytes swapped end for end with their bits reversed,
// in eight pieces with the options' callback told the percentage before
// each (0, 13, ... 91) and 100 at the end.  ROM QUIRKS: the padding at the
// end of each row ends up at its start, moving the picture right by it
// when the width is not a multiple of 32; and eight sixteenths of the
// bytes are swapped, which for a size that is not a multiple of sixteen
// leaves a few in the middle as they were - and a bitmap of fewer than
// sixteen bytes is not turned at all.
Ref
RotBitmap180(RefArg bitmap, RefArg options)
{
	long percent = 0;
	TPixelObj obj;
	newton_try
	{
		obj.Init(bitmap);
		RefVar callback;
		RefVar args;
		Boolean tellCallback = false;
		if (NOTNIL(options) && FrameHasSlot(options, RSSYMcallback))
		{
			tellCallback = true;
			callback = GetFrameSlot(options, RSSYMcallback);
			args = MakeArray(1);
			// NOT YET RECONSTRUCTED: VAddrToStore/FlushLargeObject (a
			// bitmap in a large binary on a store)
		}
		PixelMap* pm = obj.Pixels();
		long size = RowsOf(pm) * pm->rowBytes;
		long left = size >> 1;
		UByte* front = (UByte*) GetPixelMapBits(pm);
		UByte* back = front + size - 1;
		long piece = size >> 4;
		for (int k = 8; k != 0; k--)
		{
			long n = piece;
			if (left < piece)
				n = left;
			if (tellCallback)
			{
				SetArraySlot(args, 0, RefVar(MAKEINT(percent)));
				NSCall(callback, args);
				percent += 0xd;
			}
			left -= n;
			for ( ; n != 0; n--)
			{
				UByte b = *front;
				*front++ = bitFlip[*back];
				*back-- = bitFlip[b];
			}
		}
		if (tellCallback)
		{
			SetArraySlot(args, 0, RefVar(MAKEINT(100)));
			NSCall(callback, args);
		}
	}
	cleanup
	{
		obj.~TPixelObj();
	}
	end_try;
	return bitmap;
}


// ROM 0x0003fbf8 FlipBitmapH__FRC6RefVar
// Flipped left to right: each row's bytes swapped end for end with their
// bits reversed - first moved right by the row's padding (through a row
// of their own) so that the picture lands at the left.  ROM BUG: that row
// is not given back.
Ref
FlipBitmapH(RefArg bitmap)
{
	UByte* temp = nil;
	TPixelObj obj;
	newton_try
	{
		obj.Init(bitmap);
		PixelMap* pm = obj.Pixels();
		long rows = RowsOf(pm);
		long words = pm->rowBytes >> 2;
		UByte* row = (UByte*) GetPixelMapBits(pm);
		ULong pad = (ULong) (pm->rowBytes * 8 - ColumnsOf(pm));
		if (pad != 0 && (temp = (UByte*) NewPtr(pm->rowBytes)) == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
		for ( ; 0 < rows; rows--, row += words * 4)
		{
			UByte* from = row;
			if (pad != 0)
			{
				uint32_t carry = 0;
				for (long w = 0; w < words; w++)
				{
					uint32_t high = carry << (32 - pad);
					carry = GetBigEndianWord(row + w * 4);
					PutBigEndianWord(temp + w * 4, high | (carry >> pad));
				}
				from = temp;
			}
			UByte* fromEnd = from + words * 4;
			UByte* to = row;
			UByte* toEnd = row + words * 4;
			for (long n = words * 2; 0 < n; n--)
			{
				--toEnd;
				--fromEnd;
				UByte b = bitFlip[*from];
				*to = bitFlip[*fromEnd];
				*toEnd = b;
				from++;
				to++;
			}
		}
	}
	cleanup
	{
		if (temp != nil)
			DisposPtr((Ptr) temp);
		obj.~TPixelObj();
	}
	end_try;
	return bitmap;
}


// ROM 0x0003fdc0 FlipBitmapV__FRC6RefVar
// Flipped top to bottom: the rows swapped a word at a time.
Ref
FlipBitmapV(RefArg bitmap)
{
	TPixelObj obj;
	newton_try
	{
		obj.Init(bitmap);
		PixelMap* pm = obj.Pixels();
		long rows = RowsOf(pm);
		long words = pm->rowBytes >> 2;
		uint32_t* top = (uint32_t*) GetPixelMapBits(pm);
		uint32_t* bottom = top + words * (rows - 1);
		for (long n = rows >> 1; 0 < n; n--)
		{
			for (long w = words; 0 < w; w--)
			{
				uint32_t t = *top;
				*top++ = *bottom;
				*bottom++ = t;
			}
			bottom -= words * 2;
		}
	}
	cleanup
	{
		obj.~TPixelObj();
	}
	end_try;
	return bitmap;
}


// A quarter turn's new pixels: a 'pixels object as wide as the bitmap is
// high (its rows rounded up to longs), the resolution swapped over.
static Ref
TurnedPixels(RefArg bitmap, PixelMap* pm, long* rowBytes)
{
	long height = RowsOf(pm);
	long width = ColumnsOf(pm);
	long depth = pm->pixMapFlags & 0xff;
	*rowBytes = depth * ((height + 0x1f) >> 5) * 4;
	Rect bounds;
	bounds.top = 0;
	bounds.left = 0;
	bounds.bottom = (short) width;
	bounds.right = (short) height;
	RefVar store(GetFrameSlot(bitmap, RSSYMstore));
	return MakePixelsObject(bounds, depth, *rowBytes, pm->deviceRes.v, pm->deviceRes.h, store, RefVar(NILREF), RefVar(NILREF));
}


// A quarter turn's end: the new pixels made the bitmap's data, its bounds
// turned about their top left, and the resolution as it was.
static void
TurnedBitmap(RefArg bitmap, RefArg pixels, PixelMap* pm)
{
	long hRes = pm->deviceRes.h;
	long vRes = pm->deviceRes.v;
	RefVar boundsRef(GetFrameSlot(bitmap, RSSYMbounds));
	SetFrameSlot(bitmap, RSSYMdata, pixels);
	Rect* bounds = (Rect*) BinaryData(boundsRef);
	short right = bounds->right;
	short left = bounds->left;
	bounds->right = (short) (left + (short) (bounds->bottom - bounds->top));
	bounds->bottom = (short) (bounds->top + (short) (right - left));
	RefVar resolution(AllocateArray(RSSYMresolution, 2));
	SetArraySlot(resolution, 0, RefVar(MAKEINT(hRes)));
	SetArraySlot(resolution, 1, RefVar(MAKEINT(vRes)));
	SetFrameSlot(bitmap, RSSYMresolution, resolution);
}


// ROM 0x0003fec0 RotBitmapL__FRC6RefVarT1
// A quarter turn to the left: the bits taken 32 columns by 8 rows at a
// time, each column of eight becoming a byte of the new bitmap - the
// source's right-hand column its top row.
Ref
RotBitmapL(RefArg bitmap, RefArg options)
{
	TPixelObj obj;
	newton_try
	{
		obj.Init(bitmap);
		PixelMap* pm = obj.Pixels();
		if (!Tilable(pm))
		{
			long height = RowsOf(pm);
			long width = ColumnsOf(pm);
			long words = pm->rowBytes >> 2;
			long rowBytes;
			RefVar pixels(TurnedPixels(bitmap, pm, &rowBytes));
			PixelMap* turned = (PixelMap*) BinaryData(pixels);
			long groups = ((height - 1) >> 3) + 1;
			long bands = ((width - 1) >> 5) + 1;
			UByte* from = (UByte*) GetPixelMapBits(pm);
			UByte* to = (UByte*) GetPixelMapBits(turned) + rowBytes * (width - 1);
			long bits = 0x20;
			long lastBits = width & 0x1f;
			long lastRows = height & 7;
			for ( ; 0 < bands; bands--)
			{
				if (bands == 1 && lastBits != 0)
					bits = lastBits;
				UByte* nextBand = from + 4;
				for (long g = groups; 0 < g; g--)
				{
					uint32_t r[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
					long n = (g != 1 || lastRows == 0) ? 8 : lastRows;
					for (long k = 0; k < n; k++)
					{
						r[k] = GetBigEndianWord(from);
						from += words * 4;
					}
					UByte* next = to + 1;
					for (long b = bits; 0 < b; b--)
					{
						UByte v = 0;
						for (int k = 0; k < 8; k++)
						{
							v = (UByte) ((v << 1) | (r[k] >> 31));
							r[k] <<= 1;
						}
						*to = v;
						to -= rowBytes;
					}
					to = next;
				}
				to -= rowBytes * bits + groups;
				from = nextBand;
			}
			TurnedBitmap(bitmap, pixels, pm);
		}
		else
		{
			// NOT YET RECONSTRUCTED: RotTiledBitmap(bitmap, pm, 0, options)
		}
	}
	cleanup
	{
		obj.~TPixelObj();
	}
	end_try;
	return bitmap;
}


// ROM 0x0004056c RotBitmapR__FRC6RefVarT1
// A quarter turn to the right: as RotBitmapL, the rows read into the
// byte the other way round and the new bitmap filled from its top right,
// down each column and leftwards - the source's top rows are its
// right-hand columns, the part group of rows when the height is not a
// multiple of eight coming first.
Ref
RotBitmapR(RefArg bitmap, RefArg options)
{
	TPixelObj obj;
	newton_try
	{
		obj.Init(bitmap);
		PixelMap* pm = obj.Pixels();
		if (!Tilable(pm))
		{
			long height = RowsOf(pm);
			long width = ColumnsOf(pm);
			long words = pm->rowBytes >> 2;
			long rowBytes;
			RefVar pixels(TurnedPixels(bitmap, pm, &rowBytes));
			PixelMap* turned = (PixelMap*) BinaryData(pixels);
			long groups = ((height - 1) >> 3) + 1;
			long bands = ((width - 1) >> 5) + 1;
			UByte* from = (UByte*) GetPixelMapBits(pm);
			UByte* to = (UByte*) GetPixelMapBits(turned) + groups - 1;
			long bits = 0x20;
			long lastBits = width & 0x1f;
			long firstRows = height & 7;
			// (the registers keep what the last group read when a band's
			// first group is whole - as the ROM's do)
			uint32_t r[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };
			for ( ; 0 < bands; bands--)
			{
				if (bands == 1 && lastBits != 0)
					bits = lastBits;
				UByte* nextBand = from + 4;
				long part = firstRows;
				for (long g = groups; 0 < g; g--)
				{
					// r[7] is the byte's top bit, r[0] its bottom; a part
					// group fills only the top ones
					long n = 8;
					if (part != 0)
					{
						for (int k = 0; k < 8; k++)
							r[k] = 0;
						n = part;
					}
					for (long k = 8 - n; k < 8; k++)
					{
						r[k] = GetBigEndianWord(from);
						from += words * 4;
					}
					part = 0;
					UByte* next = to - 1;
					for (long b = bits; 0 < b; b--)
					{
						UByte v = 0;
						for (int k = 7; k >= 0; k--)
						{
							v = (UByte) ((v << 1) | (r[k] >> 31));
							r[k] <<= 1;
						}
						*to = v;
						to += rowBytes;
					}
					to = next;
				}
				to += rowBytes * bits + groups;
				from = nextBand;
			}
			TurnedBitmap(bitmap, pixels, pm);
		}
		else
		{
			// NOT YET RECONSTRUCTED: RotTiledBitmap(bitmap, pm, 1, options)
		}
	}
	cleanup
	{
		obj.~TPixelObj();
	}
	end_try;
	return bitmap;
}


// ROM 0x0003f764 FMungeBitmap
// MungeBitmap(bitmap, operation, options): the bitmap turned ('rotateLeft,
// 'rotateRight, 'rotate180) or flipped ('flipHorizontal, 'flipVertical)
// in place.
Ref
FMungeBitmap(RefArg /*rcvr*/, RefArg bitmap, RefArg operation, RefArg options)
{
	RefVar op(operation);
	if (EQ(op, RSSYMfliphorizontal))
		return FlipBitmapH(bitmap);
	if (EQ(op, RSSYMflipvertical))
		return FlipBitmapV(bitmap);
	if (EQ(op, RSSYMrotateleft))
		return RotBitmapL(bitmap, options);
	if (EQ(op, RSSYMrotateright))
		return RotBitmapR(bitmap, options);
	if (EQ(op, RSSYMrotate180))
		return RotBitmap180(bitmap, options);
	Throw((ExceptionName) kGrafException, (void*) kGrafErrBadOperation, nil);
	return bitmap;
}


void
RegisterMungeBitmapNatives(void)
{
	RegisterNativeFunction("FMungeBitmap", (void*) FMungeBitmap, 3);
}
