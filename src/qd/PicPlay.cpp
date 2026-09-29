/*
	File:		qd/PicPlay.cpp

	Contains:	Playing a QuickDraw picture back.  See PicPlay.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.

	DEVIATION: the picture's words are big-endian and the host reads them
	with toolbox/ByteOrder.h - GetPicWord and GetPicLong assemble them,
	and every Rect read whole out of the picture (the ROM's GetPicData of
	eight bytes straight into a TRect) and every region or polygon
	(GetPicHandle) has its halfwords turned round after it is read.
*/

#include "PicPlay.h"
#include "PicRecord.h"
#include "Draw.h"
#include "Rects.h"
#include "Regions.h"
#include "Shapes.h"
#include "Polygons.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "objects.h"
#include "OSErrors.h"
#include "FixedMath.h"
#include "ByteOrder.h"
#include "Unicode.h"
#include "Fonts.h"
#include "ObjHeader.h"
#include "TextObject.h"
#include "Curves.h"
#include "Paths.h"
#include "Frames.h"
#include <new>
#include <string.h>

const OpcodeProc*	gOpcodeProcs = nil;		// ROM 0x00380a9c OpcodeProcs (set by views/PictureShapes.cpp)

// GetPicGrayTable's answer when there is no memory for the table
const long kPicErrNoGrayTable = -7000;			// the ROM's 0xffffe4a8


/*------------------------------------------------------------------------------
	R e a d i n g   t h e   p i c t u r e
------------------------------------------------------------------------------*/

// ROM 0x00334f68 StdGetPic
// The next bytes of the picture being played (qdGlobals.fPicHandle, read
// from fPicOffset on).
extern "C" void
StdGetPic(Ptr data, long count)
{
	BlockMove(*qdGlobals.fPicHandle + qdGlobals.fPicOffset, data, count);
	qdGlobals.fPicOffset += count;
}


// ROM 0x00334498 GetPicData__FPcl
// Through the port's getPicProc when it has procs.  (Host: a port whose
// procs leave getPicProc nil gets the standard one; the ROM would call
// through nil.)
void
GetPicData(char* data, long count)
{
	GrafPort* port = GetCurrentPort();
	GetPicDataProc proc = (port->grafProcs != nil && port->grafProcs->getPicProc != nil) ? port->grafProcs->getPicProc : StdGetPic;
	proc(data, count);
}


// ROM 0x00334450 GetPicDiscard__Fl
// Bytes skipped, 64 at a time.
void
GetPicDiscard(long count)
{
	char buffer[64];
	for (; (ULong) count > 0x40; count -= 0x40)
		GetPicData(buffer, 0x40);
	if (count != 0)
		GetPicData(buffer, count);
}


// ROM 0x0033455c GetPicWord__Fv
long
GetPicWord(void)
{
	unsigned char bytes[2];
	GetPicData((char*) bytes, 2);
	return (short) GetBigEndianHalf(bytes);
}


// ROM 0x003345d4 GetPicLong__Fv
long
GetPicLong(void)
{
	unsigned char bytes[4];
	GetPicData((char*) bytes, 4);
	return (long) (Long32) GetBigEndianWord(bytes);
}


// ROM 0x003345f8 GetPicSByte__Fv
long
GetPicSByte(void)
{
	signed char byte;
	GetPicData((char*) &byte, 1);
	return byte;
}


// ROM 0x00334620 GetPicUByte__Fv
long
GetPicUByte(void)
{
	unsigned char byte;
	GetPicData((char*) &byte, 1);
	return byte;
}


// ROM 0x00334644 GetPicPoint__FP5Point
// A point: v, then h.
Point*
GetPicPoint(Point* pt)
{
	pt->v = (short) GetPicWord();
	pt->h = (short) GetPicWord();
	return pt;
}


// host: a rectangle read whole (the ROM's GetPicData of eight bytes into
// a TRect), its halfwords put into the host's order
static void
GetPicRect(Rect* r)
{
	unsigned char bytes[8];
	GetPicData((char*) bytes, 8);
	r->top = (short) GetBigEndianHalf(bytes);
	r->left = (short) GetBigEndianHalf(bytes + 2);
	r->bottom = (short) GetBigEndianHalf(bytes + 4);
	r->right = (short) GetBigEndianHalf(bytes + 6);
}


// ROM 0x003344d8 GetPicHandle__FPPP10GenericRec
// A region or polygon: its size word (the size in the picture, packed),
// then the rest into a handle two bytes bigger, the box at +4 where the
// ARM's structures keep it.  ==> 0 when it was read, 1 when there was no
// memory.
long
GetPicHandle(Handle* h)
{
	long size = GetPicWord();
	long total = size + 2;
	*h = NewHandle(total);
	if (*h != nil)
	{
		HLock(*h);
		*(short*) **h = (short) total;
		GetPicData(**h + 4, size - 2);
		// DEVIATION: the box and the data are halfwords, big-endian in the
		// picture
		for (long i = 4; i + 1 < total; i += 2)
			*(short*) (**h + i) = (short) GetBigEndianHalf(**h + i);
		HUnlock(*h);
		if (*h != nil)
			return 0;
	}
	return 1;
}


// ROM 0x0033467c GetPicResvOpcode__FlUc
// A reserved opcode's data skipped: a length word (count 2) or long
// (count 4) read first when asked, else the count itself.
long
GetPicResvOpcode(long count, Boolean readCount)
{
	if (readCount)
		count = count == 2 ? GetPicWord() : GetPicLong();
	if (count != 0)
		GetPicDiscard(count);
	return 1;
}


// ROM 0x00334fb8 StdComment
// A comment is only of use to a picture being recorded: ShortComment
// (0xa0) and its kind, or LongComment (0xa1), its kind, size and data.
extern "C" void
StdComment(short kind, short size, Handle data)
{
	if (GetCurrentPort()->picSave == nil)
		return;
	PutPicOpcode(size < 1 ? 0xa0 : 0xa1);
	PutPicWord(kind);
	if (size < 1)
		return;
	PutPicWord(size);
	HLock(data);
	PutPicData(*data, size);
	HUnlock(data);
}


// ROM 0x00334584 PicComment__FsT1PPc
void
PicComment(short kind, short size, Handle data)
{
	GrafPort* port = GetCurrentPort();
	PicCommentProc proc = (port->grafProcs != nil && port->grafProcs->commentProc != nil) ? port->grafProcs->commentProc : StdComment;
	proc(kind, size, data);
}


/*------------------------------------------------------------------------------
	P a c k B i t s
------------------------------------------------------------------------------*/

// ROM 0x002aefc0 UnpackBits__FPPcT1l
// A count byte n: 0..127, n+1 bytes as they are; -1..-127, the next byte
// 1-n times; -128, nothing.
void
UnpackBits(char** src, char** dst, long count)
{
	char* d = *dst;
	char* end = d + count;
	char* s = *src;
	while (d < end)
	{
		long n = (signed char) *s++;
		if (n == -0x80)
			continue;
		if (n < 0)
		{
			n = 1 - n;
			char value = *s++;
			do
				*d++ = value;
			while (--n != 0);
		}
		else
		{
			n = n + 1;
			do
				*d++ = *s++;
			while (--n != 0);
		}
	}
	*src = s;
	*dst = d;
}


// ROM 0x002af03c UnpackWords__FPPcT1l
// The same, a unit being two bytes.
void
UnpackWords(char** src, char** dst, long count)
{
	char* d = *dst;
	char* end = d + count;
	char* s = *src;
	while (d < end)
	{
		long n = (signed char) *s++;
		if (n == -0x80)
			continue;
		if (n < 0)
		{
			n = 1 - n;
			char hi = s[0];
			char lo = s[1];
			s += 2;
			do
			{
				d[0] = hi;
				d[1] = lo;
				d += 2;
			}
			while (--n != 0);
		}
		else
		{
			n = n + 1;
			do
			{
				d[0] = s[0];
				d[1] = s[1];
				d += 2;
				s += 2;
			}
			while (--n != 0);
		}
	}
	*src = s;
	*dst = d;
}


/*------------------------------------------------------------------------------
	P i x e l s
------------------------------------------------------------------------------*/

// ROM 0x00334398 GetPicGrayTable__FlPPUc
// A pixel map's colour table made a gray table: the seed and flags
// passed over, then each entry's RGB as a gray of the depth.  ==> 0, or
// -7000 when there was no memory (the entries passed over).
long
GetPicGrayTable(long depth, UChar** table)
{
	GetPicLong();
	GetPicWord();
	long count = GetPicWord() + 1;
	*table = (UChar*) QDNewTempPtr(count);
	if (*table == nil)
	{
		GetPicDiscard(count * 8);
		return kPicErrNoGrayTable;
	}
	for (long i = 0; i < count; i++)
	{
		GetPicWord();
		ULong red = GetPicWord() & 0xffff;
		ULong green = GetPicWord() & 0xffff;
		ULong blue = GetPicWord() & 0xffff;
		(*table)[i] = (UChar) RGBtoGray(red, green, blue, depth, depth);
	}
	return 0;
}


// ROM 0x00333dc0 GetPicPixPat__Fl
// A pixel pattern (BkPixPat, PnPixPat, FillPixPat): type 2 is an old
// pattern and an RGB, made a gray pattern of the port's depth; type 1 a
// pattern and a pixel map of its own - its header, a colour table for an
// indexed one, and the rows, unpacked as GetPicBits does.  ==> the
// pattern, nil for anything else.
//
// NOT YET RECONSTRUCTED: the type 1 pattern itself - the ROM converts the
// pixels to the screen's four bits (ConvertPixPat over ConvertIndex2/4,
// ConvertIndex8to4, ConvertDirect16to4, ConvertDirect32to4...); here its
// bytes are read and nil answered, so the port keeps the pattern it had.
PatternHandle
GetPicPixPat(long type)
{
	if (type == 2)
	{
		long depth = GetCurrentPort()->portBits.pixMapFlags & 0xff;
		char rows[8];
		GetPicData(rows, 8);
		ULong red = GetPicWord() & 0xffff;
		ULong green = GetPicWord() & 0xffff;
		ULong blue = GetPicWord() & 0xffff;
		ULong gray = RGBtoGray(red, green, blue, depth, depth);
		return MakeSimpleGrayPattern(rows, gray, 0);
	}
	if (type != 1)
		return nil;
	long unpacked;
	GetPicDiscard(8);
	unsigned char header[0x32];
	GetPicData((char*) header, 0x32);
	long rowBytes = GetBigEndianHalf(header + 4) & 0x7fff;
	unpacked = rowBytes < 8;
	long pixelType = (short) GetBigEndianHalf(header + 0x1e);
	long pixelSize = (short) GetBigEndianHalf(header + 0x20);
	UChar* grayTable = nil;
	if (pixelType == 0)
	{
		if (pixelSize != 1 && pixelSize != 2 && pixelSize != 4 && pixelSize != 8)
			return nil;
		if (GetPicGrayTable(pixelSize, &grayTable) != 0)
			return nil;
	}
	else
	{
		if (pixelSize != 0x10 && pixelSize != 0x20)
			return nil;
		if (rowBytes >= 8)
		{
			switch ((short) GetBigEndianHalf(header + 0x10))
			{
			case 0:
				break;
			case 1:
				unpacked = 1;
				break;
			case 2:
				unpacked = 1;
				rowBytes = (rowBytes * 3) >> 2;
				break;
			case 3:
				unpacked = 3;
				break;
			case 4:
				rowBytes = (rowBytes * 3) >> 2;
				break;
			default:
				return nil;
			}
		}
	}
	long pad = (rowBytes & 3) != 0 ? 2 : 0;
	long rows = (short) GetBigEndianHalf(header + 0xa) - (short) GetBigEndianHalf(header + 6);
	if (unpacked == 1)
	{
		if (pad == 0)
			GetPicDiscard(rows * (rowBytes + pad));
		else
			for (long n = rows; n > 0; n--)
				GetPicDiscard(rowBytes);
	}
	else
	{
		for (long n = rows; n > 0; n--)
			GetPicDiscard(rowBytes < 0xfb ? GetPicUByte() : GetPicWord());
	}
	if (grayTable != nil)
		QDDisposeTempPtr(grayTable);
	return nil;
}


// ROM 0x003346b4 GetPicBits__FlP7PicPlayPCPFT1T2P8GrafPort_v
// BitsRect (0x90), BitsRgn (0x91), PackBitsRect (0x98), PackBitsRgn
// (0x99), DirectBitsRect (0x9a), DirectBitsRgn (0x9b): a bitmap (row
// bytes under 0x8000) or a pixel map (an indexed one with its colour
// table made a gray table, or a direct one), its source and destination
// rectangles, the mode, and for the odd ones a mask region; then the
// rows, as they are or packed a row at a time.  Only the rows that the
// destination's part inside the port's visible and clip regions needs
// are unpacked: the source rectangle is cut down in proportion, the rest
// passed over.  The pixels then go through CallBits.  ==> 1; 0 ends the
// picture - no memory, a pixel map the Newton cannot draw, a destination
// with no height, or an exception while the rows were read (which is
// swallowed).
long
GetPicBits(long opcode, PicPlay* play, const OpcodeProc* procs)
{
	char* temp = nil;
	long pixelSize = 1;
	long unpacked = 0;
	RgnHandle mask = nil;
	void (*unpack)(char**, char**, long) = UnpackBits;
	if ((opcode >= 0x92 && opcode <= 0x97) || (opcode >= 0x9c && opcode <= 0x9f))
	{
		GetPicResvOpcode(2, true);
		return 1;
	}
	long byComponent = 0;
	long noPad = 0;
	long hasGray = 0;
	if (opcode == 0x9a || opcode == 0x9b)
		GetPicLong();
	else if (opcode == 0x90 || opcode == 0x91)
		unpacked = 1;
	long word = GetPicWord();
	long isPixMap = word & 0x8000;
	long rowBytes = word & 0x7fff;
	if (rowBytes < 8)
		unpacked = 1;
	PixelMap map;
	memset(&map, 0, sizeof(map));
	GetPicRect(&map.bounds);
	UChar* grayTable = nil;
	if (isPixMap)
	{
		GetPicDiscard(2);
		long packType = GetPicWord();
		GetPicDiscard(0xc);
		long pixelType = GetPicWord();
		pixelSize = GetPicWord();
		if (pixelType == 0)
		{
			if (pixelSize != 1 && pixelSize != 2 && pixelSize != 4 && pixelSize != 8)
				return 0;
			GetPicDiscard(0x10);
			if (GetPicGrayTable(pixelSize, &grayTable) != 0)
				return 0;
			hasGray = 1;
			goto rows;
		}
		if (pixelSize != 0x10 && pixelSize != 0x20)
			return 0;
		GetPicWord();
		GetPicWord();
		if (rowBytes >= 8)
		{
			switch (packType)
			{
			case 0:
				break;
			case 1:
				unpacked = 1;
				break;
			case 2:
				noPad = 1;
				unpacked = 1;
				rowBytes = (rowBytes * 3) >> 2;
				break;
			case 3:
				unpacked = 3;
				unpack = UnpackWords;
				break;
			case 4:
				byComponent = 1;
				rowBytes = (rowBytes * 3) >> 2;
				break;
			default:
				return 0;
			}
		}
		GetPicDiscard(0xc);
	}
rows:
	long pad = (rowBytes & 3) != 0 ? 2 : 0;
	map.rowBytes = (short) rowBytes;
	Rect srcRect, dstRect;
	GetPicRect(&srcRect);
	GetPicRect(&dstRect);
	MapRect(&dstRect, &play->fFromRect, &play->fToRect);
	long mode = GetPicWord();
	if ((opcode & 1) != 0 && GetPicHandle((Handle*) &mask) != 0)
	{
		if (hasGray)
			QDDisposeTempPtr(grayTable);
		return 0;
	}
	if (mask != nil)
		MapRgn(mask, &play->fFromRect, &play->fToRect);
	GrafPort* port = GetCurrentPort();
	Rect clipped;
	RSect(&clipped, 3, &dstRect, &(*port->visRgn)->rgnBBox, &(*port->clipRgn)->rgnBBox);
	long dstHeight = dstRect.bottom - dstRect.top;
	if (dstHeight == 0)
	{
		if (mask != nil)
			DisposHandle((Handle) mask);
		if (hasGray)
			QDDisposeTempPtr(grayTable);
		return 0;
	}
	// the source cut down to the rows the clipped destination shows
	long srcHeight = srcRect.bottom - srcRect.top;
	srcRect.top = (short) (srcRect.top + (srcHeight * (clipped.top - dstRect.top) + (dstHeight >> 1)) / dstHeight);
	srcRect.bottom = (short) (srcRect.bottom - (srcHeight * (dstRect.bottom - clipped.bottom) + (dstHeight >> 1)) / dstHeight);
	long size = srcRect.bottom > srcRect.top ? (srcRect.bottom - srcRect.top) * (rowBytes + pad) : 0;
	char* bits = (char*) QDNewTempPtr(size);
	if (bits == nil)
	{
		if (mask != nil)
			DisposHandle((Handle) mask);
		if (hasGray)
			QDDisposeTempPtr(grayTable);
		return 0;
	}
	long rows = map.bounds.bottom - map.bounds.top;
	long skip = srcRect.top - map.bounds.top;
	if (skip < 0)
		skip = 0;
	map.bounds.top = srcRect.top;
	map.bounds.bottom = srcRect.bottom;
	dstRect.top = clipped.top;
	dstRect.bottom = clipped.bottom;
	char* lastRow = bits + size - rowBytes - pad;
	Boolean threw = false;
	newton_try
	{
		char* p = bits;
		if (unpacked == 1)
		{
			if (pad != 0)
			{
				long step = rowBytes + pad;
				for (long n = rows; n > 0; n--)
				{
					if ((ULong) p > (ULong) lastRow || skip-- > 0)
						GetPicDiscard(rowBytes);
					else
					{
						GetPicData(p, rowBytes);
						p += step;
					}
				}
			}
			else
			{
				skip = rowBytes * skip;
				GetPicDiscard(skip);
				GetPicData(bits, size);
				GetPicDiscard(rowBytes * rows - size - skip);
			}
		}
		else if (rowBytes > 0xfa)
		{
			temp = (char*) QDNewTempPtr(rowBytes);
			if (temp == nil)
				Throw(exOutOfMemory, (void*) (long) kError_No_Memory, nil);
			for (long n = rows; n > 0; n--)
			{
				GetPicData(temp, GetPicWord());
				if ((ULong) p <= (ULong) lastRow && !(skip-- > 0))
				{
					char* src = temp;
					unpack(&src, &p, rowBytes);
					p += pad;
				}
			}
		}
		else
		{
			temp = (char*) QDNewTempPtr(0x100);
			if (temp == nil)
				Throw(exOutOfMemory, (void*) (long) kError_No_Memory, nil);
			for (long n = rows; n > 0; n--)
			{
				GetPicData(temp, GetPicUByte());
				if ((ULong) p <= (ULong) lastRow && !(skip-- > 0))
				{
					char* src = temp;
					unpack(&src, &p, rowBytes);
					p += pad;
				}
			}
		}
		map.rowBytes = (short) (map.rowBytes + pad);
		map.pixMapFlags = pixelSize + kPixMapPtr;
		if (hasGray)
			map.pixMapFlags |= kPixMapGrayTable;
		else
		{
			if (noPad)
				map.pixMapFlags |= kPixMapNoPad;
			if (byComponent)
				map.pixMapFlags |= kPixMapByComponent;
		}
		map.deviceRes.v = kDefaultDPI;
		map.deviceRes.h = kDefaultDPI;
		map.baseAddr = bits;
		map.grayTable = grayTable;
		OpcodeProc proc = LookupOpcodeEntry(opcode, procs);
		if (proc == nil)
			CallBits(&map, &srcRect, &dstRect, mode, mask);
		else
		{
			// the pixels as unpacked (only the rows that show), where they go
			play->fProcMode = mode;
			play->fProcRect = dstRect;
			play->fProcBits = &map;
			proc(opcode, play, GetCurrentPort());
		}
	}
	newton_catch_all
	{
		threw = true;
	}
	end_try;
	// ROM BUG, kept: after an exception the row buffer is not given back
	if (!threw && temp != nil)
		QDDisposeTempPtr(temp);
	QDDisposeTempPtr(bits);
	if (hasGray)
		QDDisposeTempPtr(grayTable);
	if (threw)
	{
		if (mask != nil)
			DisposHandle((Handle) mask);
		return 0;
	}
	if (mask != nil)
		DisposHandle((Handle) mask);
	return 1;
}


/*------------------------------------------------------------------------------
	P l a y i n g
------------------------------------------------------------------------------*/

// ROM 0x00332470 LookupOpcodeEntry__FUlPCPFlP7PicPlayP8GrafPort_v
// The table's handler for the opcode's sixteen (0..0xf, and everything
// from 0x100 on in the seventeenth); nil without a table.
OpcodeProc
LookupOpcodeEntry(ULong opcode, const OpcodeProc* procs)
{
	ULong group = (opcode & 0xffff) >> 4;
	if (group > 0xf)
		group = 0x10;
	return procs != nil ? procs[group] : nil;
}


// the rectangle shapes (0x30 rect, 0x40 round rect, 0x50 oval, 0x60 arc):
// the verbs are base..base+4 with the rectangle, base+8..base+0xc "the
// same one again"
static Boolean
IsShapeVerb(long opcode, long base)
{
	return (opcode >= base && opcode <= base + 4) || (opcode >= base + 8 && opcode <= base + 0xc);
}


/*------------------------------------------------------------------------------
	T e x t
------------------------------------------------------------------------------*/

// (host) A TextOptions as the ROM reads it, seven words straight into
// memory: big-endian in the picture.
static void
GetPicTextOptions(TextOptions* options)
{
	options->fJustification = (Fixed) GetPicLong();
	options->fAlignment = (Fixed) GetPicLong();
	options->fWidth = (Fixed) GetPicLong();
	options->fReserved = GetPicLong();
	options->fTransferMode = GetPicLong();
	options->fFittedWidth = (Fixed) GetPicLong();
	options->fReserved2 = GetPicLong();
}


// (host) A style's seven words after its family, as the ROM reads them
// straight into memory.  DEVIATION: two of them are the font pattern (a
// Ref) and the pattern (a PatternHandle) the recording had, which mean
// nothing now - in the ROM a stale pointer the text is drawn with, and
// which DrawPicture disposes; the host keeps nought and nil.
static void
GetPicStyleWords(StyleRecord* style)
{
	style->fFontSize = (Fixed) GetPicLong();
	style->fFontFace = GetPicLong();
	GetPicLong();
	style->fFontPattern = 0;
	style->fTransferMode = GetPicLong();
	style->fReserved14 = GetPicLong();
	style->fReserved18 = GetPicLong();
	GetPicLong();
	style->fPattern = nil;
}


// ROM 0x003336ec DrawPicText__FP7PicPlay
// 0x81a3's text drawn as a text object at the picture's text scales: in
// 0x81a2's styles and runs (flag 0x80) or 0x81a1's one style, with 0x81a0's
// options (flag 0x40) - their width, if they have one, scaled by the
// picture's horizontal scale meanwhile.
//
// ROM QUIRK, kept: the width is put back only if the scaled one is not
// nought.  (Host: an integer family 0x800000 - 0x81a1's with the picture
// carrying it, which 0x81a4 never fills in, see DoPutText - is drawn with
// nothing: DEVIATION, the ROM's font engine reading whatever lies at that
// address.)
void
DrawPicText(PicPlay* play)
{
	Fixed width = 0;
	Boolean multiStyle = (play->fTextFlags & 0x80) != 0;
	Boolean withOptions = (play->fTextFlags & 0x40) != 0;
	if (withOptions && play->fTextOptions.fWidth != 0)
	{
		Fixed scale = FixedDivide((Fixed) ((ULong32) ((unsigned short) play->fToRect.right - (unsigned short) play->fToRect.left) << 16),
								  (Fixed) ((ULong32) ((unsigned short) play->fFromRect.right - (unsigned short) play->fFromRect.left) << 16));
		width = play->fTextOptions.fWidth;
		play->fTextOptions.fWidth = FixedMultiply(width, scale);
	}
	StyleRecord* style = &play->fXStyle;
	TextOptions* options = withOptions ? &play->fTextOptions : nil;
	StyleRecord** styles;
	short* runLengths;
	if (!multiStyle)
	{
		runLengths = nil;
		styles = &style;
	}
	else
	{
		runLengths = play->fXRunLengths;
		styles = play->fXStyles;
	}
	if (!multiStyle && ISINT((Ref) style->fFontFamily) && RINT((Ref) style->fFontFamily) == 0x800000)
		;											// (host: see above)
	else
	{
		TextObjectRef text = NewText(play->fXText, play->fXTextCount, styles, runLengths, play->fXTextLoc, options);
		if (text != 0)
		{
			CallDrawText(text, play->fTextHScale, play->fTextVScale);
			DisposeText(text);
		}
	}
	if (withOptions && play->fTextOptions.fWidth != 0)
		play->fTextOptions.fWidth = width;
}


// ROM 0x00333cd0 TextCleanup__FP7PicPlayPc
// What 0x81a2-0x81a4 allocated given back: the families 0x81a4 read (with
// one style, the block it was handed), the text, and 0x81a2's styles and
// runs.
//
// ROM BUG, not kept: with several styles the ROM gives back each integer
// family's block - the first is the start of 0x81a4's block, the others
// inside it, which the ROM hands to QDDisposeTempPtr as if each were a
// block of its own; the host gives the block back once (DEVIATION: a
// pointer inside a block would damage the host's heap).
void
TextCleanup(PicPlay* play, char* families)
{
	Boolean multiStyle = (play->fTextFlags & 0x80) != 0;
	if ((play->fTextFlags & 0x20) != 0)
	{
		if (!multiStyle)
		{
			if (families != nil)
				QDDisposeTempPtr(families);
		}
		else
		{
			Boolean first = true;
			StyleRecord* style = play->fXStyleRecs + play->fStyleCount;
			for (long i = play->fStyleCount; i > 0; i--)
			{
				style--;
				if (ISINT((Ref) style->fFontFamily))
				{
					char* block = (char*) RefToAddress(style->fFontFamily);
					if (block == families && first)
					{
						QDDisposeTempPtr(block);
						first = false;
					}
				}
			}
		}
	}
	QDDisposeTempPtr(play->fXText);
	play->fXText = nil;
	if (!multiStyle)
		return;
	StyleRecord* style = play->fXStyleRecs;
	for (long i = play->fStyleCount; i > 0; i--, style++)
		style->~StyleRecord();
	DisposPtr((Ptr) play->fXStyles);
	play->fXStyles = nil;
	DisposPtr((Ptr) play->fXRunLengths);
	play->fXRunLengths = nil;
	QDDisposeTempPtr(play->fXStyleRecs);
	play->fXStyleRecs = nil;
}


// (host) A curve opcode's work in ParsePicCodes (0x0c80-0x0c84 and
// 0x8088-0x808c): the curve read unless it is "the same" as the last, then
// a copy of it mapped onto the destination and handed to CallCurve.
//
// ROM BUGS, kept: the copy is mapped twice, so a picture drawn at another
// size has its curves scaled twice over; and the procs are never asked -
// the curve is drawn even while the picture is being made into shapes.
// And StdCurve records a curve the same as the last as 0x0c88 + the verb,
// which this reads as a reserved opcode of 0x18 bytes: a curve drawn twice
// running leaves a picture that cannot be read past it.
static long
PlayCurve(PicPlay* play, GrafVerb verb, Boolean same)
{
	if (!same)
	{
		Fixed* p = &play->fCurve.first.x;
		for (long i = 0; i < 6; i++)
			p[i] = (Fixed) GetPicLong();
	}
	curve c = play->fCurve;
	MapCurve(&c, &play->fFromRect, &play->fToRect);
	MapCurve(&c, &play->fFromRect, &play->fToRect);
	CallCurve(verb, &c);
	return 1;
}


// ROM 0x0033249c ParsePicCodes__FP7PicPlayPCPFlT1P8GrafPort_v
// One opcode played: a byte for a version 1 picture, a word (word aligned)
// for version 2.  ==> 1 to go on, 0 at the end (0xff) or when the picture
// cannot go on.
long
ParsePicCodes(PicPlay* play, const OpcodeProc* procs)
{
	GrafPort* port = GetCurrentPort();
	long result = 1;
	ULong opcode;
	if (play->fVersion == 1)
		opcode = GetPicUByte();
	else
	{
		if (qdGlobals.fPicOffset & 1)
			GetPicUByte();
		opcode = GetPicWord() & 0xffff;
	}
	GrafVerb verb = (GrafVerb) (opcode & 7);
	Boolean same = (opcode & 8) != 0;
	OpcodeProc proc = LookupOpcodeEntry(opcode, procs);
	if (opcode == 0xff)
	{
		if (proc != nil)
			proc(0xff, play, port);
		return 0;
	}
	Rect* from = &play->fFromRect;
	Rect* to = &play->fToRect;
	if (opcode < 0x20)
	{
		if (proc != nil)
			proc(-(long) opcode, play, port);
		switch (opcode)
		{
		case 0x00:									// NOP
			break;
		case 0x01:									// ClipRgn
		{
			RgnHandle rgn;
			if (GetPicHandle((Handle*) &rgn) != 0)
			{
				result = 0;
				break;
			}
			CopyRgn(rgn, play->fPlayClip);
			MapRgn(rgn, from, to);
			SectRgn(rgn, play->fSavedClip, port->clipRgn);
			DisposHandle((Handle) rgn);
			break;
		}
		case 0x02:									// BkPat
		case 0x09:									// PnPat
		case 0x0a:									// FillPat
		{
			char rows[8];
			GetPicData(rows, 8);
			ULong fg = play->fFgGray;
			ULong bg = play->fBgGray;
			play->fFgGray = 0;
			play->fBgGray = 0;
			PatternHandle pattern = fg == 0 ? MakeSimplePattern(rows) : MakeSimpleGrayPattern(rows, fg, bg);
			if (opcode == 0x02)
			{
				DisposePattern(port->bgPat);
				port->bgPat = pattern;
			}
			else
			{
				DisposePattern(port->fgPat);
				port->fgPat = pattern;
			}
			break;
		}
		case 0x03:									// TxFont
			play->fTextStyle.fFontFamily = SearchFont(GetPicWord(), nil);
			break;
		case 0x04:									// TxFace
			play->fTextStyle.fFontFace = (short) GetPicUByte();
			break;
		case 0x05:									// TxMode: both kinds of text's
		{
			long mode = GetPicWord();
			play->fMacTextOptions.fTransferMode = mode;
			play->fTextOptions.fTransferMode = mode & 0xffff;
			break;
		}
		case 0x06:									// SpExtra
			play->fSpaceExtra = GetPicLong();
			break;
		case 0x07:									// PnSize
			ScalePt(GetPicPoint(&port->pnSize), from, to);
			break;
		case 0x08:									// PnMode
			port->pnMode = (short) GetPicWord();
			break;
		case 0x0b:									// OvSize
			ScalePt(GetPicPoint(&play->fOvalSize), from, to);
			break;
		case 0x0c:									// Origin
		{
			long dh = (short) GetPicWord();
			long dv = (short) GetPicWord();
			OffsetRect(from, dh, dv);
			port->patAlign.v = (short) (port->patAlign.v + dv);
			port->patAlign.h = (short) (port->patAlign.h + dh);
			RgnHandle rgn = NewRgn();
			CopyRgn(play->fPlayClip, rgn);
			MapRgn(rgn, from, to);
			SectRgn(rgn, play->fSavedClip, port->clipRgn);
			DisposeRgn(rgn);
			break;
		}
		case 0x0d:									// TxSize
			play->fTextStyle.fFontSize = (Fixed) ((ULong) GetPicWord() << 16);
			break;
		case 0x0e:									// FgColor
		case 0x0f:									// BkColor
			GetPicLong();
			break;
		case 0x10:									// TxRatio
		{
			Point numer, denom;
			GetPicPoint(&numer);
			GetPicPoint(&denom);
			play->fTextHScale = FixedMultiply(play->fHScale, FixedDivide((Fixed) ((ULong) (unsigned short) numer.h << 16), (Fixed) ((ULong) (unsigned short) denom.h << 16)));
			play->fTextVScale = FixedMultiply(play->fVScale, FixedDivide((Fixed) ((ULong) (unsigned short) numer.v << 16), (Fixed) ((ULong) (unsigned short) denom.v << 16)));
			break;
		}
		case 0x11:									// Version
		{
			long first = (short) GetPicUByte();
			if (first != 1)
			{
				long version = (short) (GetPicUByte() | (first << 8));
				play->fVersion = version;
				if (version != 0x2ff)
					result = 0;
			}
			break;
		}
		case 0x12:									// BkPixPat
		case 0x13:									// PnPixPat
		case 0x14:									// FillPixPat
		{
			PatternHandle pattern = GetPicPixPat(GetPicWord());
			if (pattern == nil)
				break;
			if (opcode == 0x12)
			{
				DisposePattern(port->bgPat);
				port->bgPat = pattern;
			}
			else
			{
				DisposePattern(port->fgPat);
				port->fgPat = pattern;
			}
			break;
		}
		case 0x15:									// PnLocHFrac
		case 0x16:									// ChExtra
			GetPicWord();
			break;
		case 0x1a:									// RGBFgCol
		case 0x1b:									// RGBBkCol
		case 0x1d:									// HiliteColor
		case 0x1f:									// OpColor
		{
			ULong flags = port->portBits.pixMapFlags;
			if (flags & 0x300)
				flags = qdGlobals.fScreenBits.pixMapFlags;
			long depth = flags & 0xff;
			ULong red = GetPicWord() & 0xffff;
			ULong green = GetPicWord() & 0xffff;
			ULong blue = GetPicWord() & 0xffff;
			ULong gray = RGBtoGray(red, green, blue, depth, depth);
			PatternHandle pattern = GetStdGrayPattern(red, green, blue);
			if (pattern != nil)
			{
				if (opcode == 0x1b)
				{
					play->fBgGray = gray;
					DisposePattern(port->bgPat);
					port->bgPat = pattern;
				}
				else
				{
					play->fFgGray = gray;
					DisposePattern(port->fgPat);
					port->fgPat = pattern;
				}
			}
			break;
		}
		default:									// 0x17..0x19, 0x1c, 0x1e
			break;
		}
		if (proc != nil)
			proc((long) opcode, play, port);			// and again once it is played
		return result;
	}

	if (opcode <= 0x100)
	{
		ULong group = opcode & 0xfff0;
		switch (group)
		{
		case 0x20:
			if (opcode <= 0x23)
			{
				// Line (0x20), LineFrom (0x21), ShortLine (0x22),
				// ShortLineFrom (0x23): from a point read or from where the
				// last line ended, to a point read or a byte each way
				if ((opcode & 1) == 0)
					GetPicPoint(&port->pnLoc);
				else
					port->pnLoc = play->fLastPt;
				Point end = port->pnLoc;
				MapPt(&port->pnLoc, from, to);
				if ((opcode & 2) == 0)
					GetPicPoint(&end);
				else
				{
					end.h = (short) (GetPicSByte() + end.h);
					end.v = (short) (GetPicSByte() + end.v);
				}
				play->fLastPt = end;
				MapPt(&end, from, to);
				if (proc == nil)
				{
					LineTo(end.h, end.v);
					return 1;
				}
				play->fProcPt = end;					// (the pen is where it starts)
				proc((long) opcode, play, port);
				return 1;
			}
			if (opcode >= 0x28 && opcode <= 0x2b)
			{
				// LongText, DHText, DVText, DHDVText: where the text goes,
				// then a count and the Mac Roman characters, drawn as a text
				// object in the style TxFont, TxSize and TxFace made (and
				// the old opcodes' options: only TxMode's transfer mode)
				if (opcode == 0x28)
					GetPicPoint(&play->fTextLoc);
				else if (opcode == 0x29)
					play->fTextLoc.h = (short) (GetPicUByte() + (unsigned short) play->fTextLoc.h);
				else if (opcode == 0x2a)
					play->fTextLoc.v = (short) (GetPicUByte() + (unsigned short) play->fTextLoc.v);
				else
				{
					play->fTextLoc.h = (short) (GetPicUByte() + (unsigned short) play->fTextLoc.h);
					play->fTextLoc.v = (short) (GetPicUByte() + (unsigned short) play->fTextLoc.v);
				}
				long count = GetPicUByte();
				port->pnLoc = play->fTextLoc;
				if (count == 0)
					return 1;
				// DEVIATION: the ROM's 256 bytes are whatever its stack held,
				// and the conversion runs to the first nought in them; the
				// host's start clear, so it stops at the text's end
				char chars[256];
				UniChar text[0x11c];
				memset(chars, 0, sizeof(chars));
				GetPicData(chars, count);
				FPoint loc;
				loc.x = (Fixed) ((ULong) (unsigned short) play->fTextLoc.h << 16);
				loc.y = (Fixed) ((ULong) (unsigned short) play->fTextLoc.v << 16);
				ConvertToUnicode(chars, text, kMacRomanEncoding, 0x7fffffff);
				MapFPoint(&loc, from, to);
				StyleRecord* style = &play->fTextStyle;
				TextObjectRef textObj = NewText(text, count, &style, nil, loc, &play->fMacTextOptions);
				if (proc == nil)
					CallDrawText(textObj, play->fTextHScale, play->fTextVScale);
				else
				{
					text[count] = 0;
					play->fProcPt = play->fTextLoc;
					MapPt(&play->fProcPt, from, to);
					play->fProcText = text;
					proc((long) opcode, play, port);
				}
				DisposeText(textObj);
				return 1;
			}
			return GetPicResvOpcode(2, true);

		case 0x30:
		case 0x40:
		case 0x50:
			if (IsShapeVerb(opcode, group))
			{
				if (!same)
					GetPicRect(&play->fRect);
				Rect r = play->fRect;
				MapRect(&r, from, to);
				if (proc == nil)
				{
					if (group == 0x30)
						CallRect(verb, &r);
					else if (group == 0x40)
						CallRRect(verb, &r, play->fOvalSize.h, play->fOvalSize.v);
					else
						CallOval(verb, &r);
					return 1;
				}
				play->fProcRect = r;
				proc((long) opcode, play, port);
				return 1;
			}
			if (!same)
				GetPicDiscard(8);
			return 1;

		case 0x60:
			if (IsShapeVerb(opcode, 0x60))
			{
				if (!same)
					GetPicRect(&play->fRect);
				Rect r = play->fRect;
				MapRect(&r, from, to);
				long startAngle = GetPicWord();
				long arcAngle = GetPicWord();
				if (proc == nil)
				{
					CallArc(verb, &r, startAngle, arcAngle);
					return 1;
				}
				play->fProcRect = r;
				play->fProcArc = arcAngle;
				play->fProcMode = startAngle;
				proc((long) opcode, play, port);
				return 1;
			}
			// ROM QUIRK, kept: the reserved 0x6d-0x6f are read as eight
			// bytes where Apple's picture format gives them four
			GetPicDiscard(same ? 8 : 0xc);
			return 1;

		case 0x70:
			if (IsShapeVerb(opcode, 0x70))
			{
				PolyHandle poly;
				if (GetPicHandle((Handle*) &poly) != 0)
					return 0;
				MapPoly(poly, from, to);
				if (proc == nil)
					CallPoly(verb, poly);
				else
				{
					play->fProcHandle = (Handle) poly;
					proc((long) opcode, play, port);
				}
				DisposHandle((Handle) poly);
				return 1;
			}
			return GetPicResvOpcode(2, true);

		case 0x80:
			if (IsShapeVerb(opcode, 0x80))
			{
				RgnHandle rgn;
				if (GetPicHandle((Handle*) &rgn) != 0)
					return 0;
				MapRgn(rgn, from, to);
				if (proc == nil)
					CallRgn(verb, rgn);
				else
				{
					play->fProcHandle = (Handle) rgn;
					proc((long) opcode, play, port);
				}
				DisposHandle((Handle) rgn);
				return 1;
			}
			return GetPicResvOpcode(2, true);

		case 0x90:									// 0x90..0x9f
			return GetPicBits(opcode, play, procs);

		case 0xa0:
			if (opcode > 0xa1)
				return GetPicResvOpcode(2, true);
			{
				// ShortComment (0xa0), LongComment (0xa1)
				long kind = GetPicWord();
				if (proc != nil)
				{
					play->fProcMode = kind;
					proc((long) opcode, play, port);
				}
				if (verb == 0)
				{
					PicComment((short) kind, 0, nil);
					return 1;
				}
				long size = GetPicWord();
				Handle data = NewHandle(size);
				if (data == nil)
					return 0;
				HLock(data);
				GetPicData(*data, size);
				HUnlock(data);
				PicComment((short) kind, (short) size, data);
				DisposHandle(data);
				return 1;
			}

		case 0xd0:
		case 0xe0:
		case 0xf0:
			if (proc != nil)
				proc((long) opcode, play, port);
			GetPicResvOpcode(4, true);
			return 1;

		default:									// 0xb0..0xcf, 0x100: no data
			return 1;
		}
	}

	if (opcode < 0x8000)
	{
		if (opcode >= 0xc80 && opcode - 0xc80 <= 4)
			return PlayCurve(play, verb, same);
		return GetPicResvOpcode((opcode >> 8) << 1, false);
	}
	if (opcode - 0x8000 >= 0x88 && opcode - 0x8000 <= 0x8c)
		return PlayCurve(play, verb, same);
	if (opcode - 0x8100 >= 0x90 && opcode - 0x8100 <= 0x94)
	{
		// the Newton's paths: the handle's size and bytes, mapped and drawn
		long size = GetPicLong();
		pathsHandle paths = (pathsHandle) NewHandle(size);
		if (paths == nil)
			return 0;
		HLock((Handle) paths);
		GetPicData((char*) *paths, size);
		// (host: every word of them big-endian in the picture)
		Long32* words = (Long32*) *paths;
		for (long i = 0; i < size / 4; i++)
			words[i] = (Long32) GetBigEndianWord(&words[i]);
		HUnlock((Handle) paths);
		MapPaths(paths, from, to);
		CallPaths(verb, paths);
		DisposHandle((Handle) paths);
		return 1;
	}
	switch (opcode)
	{
	case 0x81a0:									// the text options
		if (GetPicLong() != 0x1c)
			return 0;
		GetPicTextOptions(&play->fTextOptions);
		play->fTextOptions.fFittedWidth = 0;
		play->fTextOptions.fReserved2 = 0;
		return 1;
	case 0x81a1:									// the text style
	{
		if (GetPicLong() != 0x20)
			return 0;
		// the family: a Mac font id, or with 0x800000 set one the picture
		// carries (which 0x81a4 fills in only for 0x81a2's styles)
		ULong family = (ULong) GetPicLong();
		if ((family & 0x800000) == 0)
			play->fXStyle.fFontFamily = SearchFont(family, nil);
		else
			play->fXStyle.fFontFamily = (Ref) (family << 2);
		GetPicStyleWords(&play->fXStyle);
		play->fXStyle.fFontPattern = 0;				// (the ROM clears the word it read: an integer nought, not nil)
		return 1;
	}
	case 0x81a2:									// the style runs
	{
		long size = GetPicLong();
		long runs = GetPicUByte();
		long styles = GetPicUByte();
		play->fStyleCount = styles;
		if (runs * 4 + styles * 0x20 + 2 != size)
			return 0;
		play->fXStyles = (StyleRecord**) NewPtr(runs * sizeof(StyleRecord*));
		if (play->fXStyles == nil)
			Throw(exOutOfMemory, nil, nil);
		play->fXRunLengths = (short*) NewPtr(runs * 2);
		if (play->fXRunLengths == nil)
		{
			DisposPtr((Ptr) play->fXStyles);
			Throw(exOutOfMemory, nil, nil);
		}
		play->fXStyleRecs = (StyleRecord*) QDNewTempPtr(styles * sizeof(StyleRecord));
		if (play->fXStyleRecs == nil)
		{
			DisposPtr((Ptr) play->fXStyles);
			DisposPtr((Ptr) play->fXRunLengths);
			Throw(exOutOfMemory, nil, nil);
		}
		// each run: its length, and which style (host: read a word at a
		// time where the ROM reads them all in and rewrites them in place)
		for (long i = 0; i < runs; i++)
		{
			ULong run = (ULong) GetPicLong();
			play->fXRunLengths[i] = (short) run;
			play->fXStyles[i] = &play->fXStyleRecs[(long) (Long32) run >> 16];
		}
		// each style: the family (a Mac font id, or 0x800000 for one 0x81a4
		// names), then the rest as it was recorded
		for (long i = 0; i < styles; i++)
		{
			StyleRecord* style = new (&play->fXStyleRecs[i]) StyleRecord;
			ULong family = (ULong) GetPicLong();
			GetPicStyleWords(style);
			if ((family & 0x800000) == 0)
				style->fFontFamily = SearchFont(family, nil);
			else
				style->fFontFamily = (Ref) (family << 2);
		}
		return 1;
	}
	case 0x81a3:									// the text itself
	{
		long size = GetPicLong();
		play->fXTextCount = GetPicWord();
		play->fXTextLoc.x = (Fixed) GetPicLong();
		play->fXTextLoc.y = (Fixed) GetPicLong();
		MapFPoint(&play->fXTextLoc, from, to);
		play->fTextFlags = GetPicUByte();
		long count = GetPicWord();
		if (count + 0xd != size)
			return 0;
		// DEVIATION: two bytes more than the ROM's block, which a proc
		// reads one character past; and the characters are big-endian in
		// the picture, turned into the host's order
		UniChar* text = (UniChar*) QDNewTempPtr(count + 2);
		play->fXText = text;
		if (text == nil)
			return 0;
		GetPicData((char*) text, count);
		for (long i = 0; i < count / 2; i++)
			text[i] = GetBigEndianHalf((const unsigned char*) &text[i]);
		if ((play->fTextFlags & 0x20) != 0)
			return 1;								// drawn once 0x81a4 has named the families
		if (proc == nil)
		{
			DrawPicText(play);
			TextCleanup(play, nil);
			return 1;
		}
		// ROM BUG, kept: with the procs nothing is given back (TextCleanup
		// is only on the drawing side)
		proc((long) opcode, play, port);
		return 1;
	}
	case 0x81a4:									// the families the styles name
	{
		long size = GetPicLong();
		long count = GetPicWord();
		char* families = (char*) QDNewTempPtr(count * 3 + size - 2);
		if (families == nil)
		{
			TextCleanup(play, nil);
			return 0;
		}
		// each of 0x81a2's styles whose family is an integer (0x800000: one
		// the picture carries) is given the address of a block holding it
		// - a length halfword and that many bytes, as an integer family
		// always points at (the host's halfword in its own order, as a rich
		// string writes it)
		char* p = families;
		StyleRecord* style = play->fXStyleRecs;
		for (long i = play->fStyleCount; i > 0; i--, style++)
		{
			if (ISINT((Ref) style->fFontFamily))
			{
				style->fFontFamily = AddressToRef(p);
				long length = GetPicWord();
				*(UniChar*) p = (UniChar) length;
				GetPicData(p + 2, length);
				p += 2 + length;
				if (((uintptr_t) p & 3) != 0)
					p += 4 - ((uintptr_t) p & 3);
			}
		}
		if (proc == nil)
		{
			DrawPicText(play);
			TextCleanup(play, families);
			return 1;
		}
		proc((long) opcode, play, port);
		return 1;
	}
	default:
		if (opcode >= 0x8100)
			GetPicResvOpcode(4, true);
		return 1;
	}
}


// the text options a picture begins with (QDTables.cpp, generated)
extern const unsigned int	kPicDefaultTextOptions[7];


// ROM 0x003337fc DrawPicture__FPP7PictureP4RectUc
// The picture played into the rectangle: its frame mapped onto it (a
// negative scale draws nothing), the port set up and put back afterwards.
// With toShapes nothing is drawn: the opcodes go to the OpcodeProcs, which
// gather the shapes the picture is made of, and those are the answer.
//
// The Newton's text starts in the system font at 12 points with the
// options at 0x00380ca0 (kPicDefaultTextOptions, as a recording picture
// starts); LongText's in the system font with no options.
//
// ROM QUIRK, kept: LongText's style starts at a size of 0xc - twelve
// sixty-five-thousandths of a point, where the other style is made at
// 0xc0000 - so text with no TxSize before it comes out at a size of
// nought.  (Host: SearchFont wants the fonts in vars.)
Ref
DrawPicture(PicHandle picture, Rect* dstRect, Boolean toShapes)
{
	GrafPort* port = GetCurrentPort();
	if (picture == nil)
		return NILREF;
	Rect frame;
	const unsigned char* data = (const unsigned char*) *picture;
	frame.top = (short) GetBigEndianHalf(data + 2);
	frame.left = (short) GetBigEndianHalf(data + 4);
	frame.bottom = (short) GetBigEndianHalf(data + 6);
	frame.right = (short) GetBigEndianHalf(data + 8);
	Fixed hScale = FixedDivide((Fixed) ((ULong) (dstRect->right - dstRect->left) << 16), (Fixed) ((ULong) (frame.right - frame.left) << 16));
	Fixed vScale = FixedDivide((Fixed) ((ULong) (dstRect->bottom - dstRect->top) << 16), (Fixed) ((ULong) (frame.bottom - frame.top) << 16));
	if (hScale < 0 || vScale < 0)
		return NILREF;
	PicPlay play;
	{
		RefVar systemFont(SearchFont(0, nil));
		MakeSimpleStyle(&play.fXStyle, systemFont, 0xc0000, 0);
		CopyStyle(&play.fXStyle);
	}
	play.fToRect = *dstRect;
	play.fFromRect = frame;
	play.fHScale = hScale;
	play.fVScale = vScale;
	play.fTextHScale = hScale;
	play.fTextVScale = vScale;
	GrafPort saved = *port;
	SetEmptyRect(&play.fRect);
	play.fLastPt.h = 0;
	play.fLastPt.v = 0;
	play.fTextLoc.h = 0;
	play.fTextLoc.v = 0;
	play.fOvalSize.h = 0;
	play.fOvalSize.v = 0;
	play.fSavedClip = port->clipRgn;
	play.fPlayClip = NewRgn();
	play.fVersion = 1;
	play.fTextOptions.fJustification = (Fixed) kPicDefaultTextOptions[0];
	play.fTextOptions.fAlignment = (Fixed) kPicDefaultTextOptions[1];
	play.fTextOptions.fWidth = (Fixed) kPicDefaultTextOptions[2];
	play.fTextOptions.fReserved = (long) kPicDefaultTextOptions[3];
	play.fTextOptions.fTransferMode = (long) kPicDefaultTextOptions[4];
	play.fTextOptions.fFittedWidth = (Fixed) kPicDefaultTextOptions[5];
	play.fTextOptions.fReserved2 = (long) kPicDefaultTextOptions[6];
	port->clipRgn = NewRgn();
	SetFgPattern(stdPatterns[blackPat]);
	SetBgPattern(stdPatterns[whitePat]);
	port->pnLoc.h = 0;
	port->pnLoc.v = 0;
	port->pnSize.h = 1;
	port->pnSize.v = 1;
	port->pnMode = patCopy;
	memset(&play.fMacTextOptions, 0, sizeof(play.fMacTextOptions));
	{
		RefVar systemFont(SearchFont(0, nil));
		MakeSimpleStyle(&play.fTextStyle, systemFont, 0xc, 0);
	}
	play.fSpaceExtra = 0;
	port->patAlign.v = 0;
	port->patAlign.h = 0;
	qdGlobals.fPicOffset = 10;
	qdGlobals.fPicHandle = (Handle) picture;
	play.fShape = NILREF;
	play.fStyle = NILREF;
	play.fLastStyle = NILREF;
	play.fShapes = NILREF;
	play.fInkPoly = nil;
	play.fFgGray = 0;
	play.fBgGray = 0;
	while (ParsePicCodes(&play, toShapes ? gOpcodeProcs : nil) != 0)
		;
	DisposeRgn(play.fPlayClip);
	DisposeRgn(port->clipRgn);
	DisposePattern(port->fgPat);
	DisposePattern(port->bgPat);
	*port = saved;
	qdGlobals.fPicHandle = nil;
	qdGlobals.fPicOffset = 0;
	if (play.fTextStyle.fPattern != nil)
		DisposePattern(play.fTextStyle.fPattern);
	if (play.fXStyle.fPattern != nil)
		DisposePattern(play.fXStyle.fPattern);
	return toShapes ? (Ref) play.fShapes : NILREF;
}


// ROM 0x00330068 ImpossibleToDraw__FP8GrafPort
// Whether nothing the port drew would show: its clip is empty, or its pen
// mode is 0x17.  A nil port is taken as one that can.
Boolean
ImpossibleToDraw(GrafPort* port)
{
	if (port == nil)
		return false;
	return EmptyRgn(port->clipRgn) || port->pnMode == 0x17;
}


// ROM 0x0033519c MapFPoint__FP6FPointP4RectT2
// A 16.16 point mapped from one rectangle onto another: scaled by the
// ratio of their sizes and moved by their corners.
void
MapFPoint(FPoint* pt, const Rect* src, const Rect* dst)
{
	Fixed hScale = FixedDivide(dst->right - dst->left, src->right - src->left);
	Fixed vScale = FixedDivide(dst->bottom - dst->top, src->bottom - src->top);
	Fixed x = FixedMultiply((Fixed) ((ULong32) pt->x - ((ULong32) (unsigned short) src->left << 16)), hScale);
	Fixed y = FixedMultiply((Fixed) ((ULong32) pt->y - ((ULong32) (unsigned short) src->top << 16)), vScale);
	pt->x = (Fixed) (ULong32) ((ULong32) x + ((ULong32) (unsigned short) dst->left << 16));
	pt->y = (Fixed) (ULong32) ((ULong32) y + ((ULong32) (unsigned short) dst->top << 16));
}
