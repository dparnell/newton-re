/*
	File:		qd/PicRecord.cpp

	Contains:	Recording a QuickDraw picture - see PicRecord.h.

	Reconstructed from the MP2x00 US ROM; each function cites its
	origin.  The picture's words are big-endian on every host, as they are
	on the Newton (toolbox/ByteOrder.h).
*/

#include "PicRecord.h"
#include "PicPlay.h"
#include "Regions.h"
#include "Rects.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"
#include "Frames.h"
#include "Curves.h"
#include "TextObject.h"
#include "RichString.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include "objects.h"
#include <string.h>

// the text options a picture begins with (QDTables.cpp, generated: its
// transfer mode is srcOr, the rest nought)
extern const unsigned int	kPicDefaultTextOptions[7];


static PicSave*
CurrentPicSave(GrafPort* port)
{
	return (PicSave*) *port->picSave;
}


// the port's putPicProc, or the standard one
static PutPicDataProc
PutPicProc(GrafPort* port)
{
	if (port->grafProcs != nil && port->grafProcs->putPicProc != nil)
		return port->grafProcs->putPicProc;
	return (PutPicDataProc) StdPutPic;
}


/*------------------------------------------------------------------------------
	O p e n i n g   a n d   c l o s i n g
------------------------------------------------------------------------------*/

// ROM 0x00331980 OpenPicture__FP4RectUc
// A picture begun in the current port (nil when the port has one open
// already, or there is no memory): the PicSave made and set up as what an
// empty picture says - the standard white and black patterns, a one pixel
// pen in patCopy, the port's origin, the system font at 12 point, the
// default text options - and the picture's size word, frame and version
// written.  The pen is hidden until ClosePicture.
PicHandle
OpenPicture(Rect* frame, Boolean macPicture)
{
	GrafPort* port = GetCurrentPort();
	StyleRecord style;
	MakeSimpleStyle(&style, RefVar(SearchFont(0, nil)), 0xc0000, 0);
	if (port->picSave != nil)
	{
		DisposeStyleRecord(&style);
		return nil;
	}
	HidePen();
	Handle save = NewHandle(sizeof(PicSave));
	if (save == nil)
	{
		DisposeStyleRecord(&style);
		return nil;
	}
	HLock(save);
	PicSave* ps = (PicSave*) *save;
	memset(ps, 0, sizeof(PicSave));
	ps->fMacPicture = macPicture;
	ps->fPnPat = stdPatterns[blackPat];		// (the ROM's stdPatterns + 0x10)
	ps->fBkPat = stdPatterns[whitePat];
	ps->fTextHScale = 0x10000;
	ps->fTextVScale = 0x10000;
	SetPt(&ps->fPnLoc, 0, 0);
	SetPt(&ps->fPnSize, 1, 1);
	ps->fPnMode = patCopy;
	SetEmptyRect(&ps->fRect);
	SetPt(&ps->fOvalSize, 0, 0);
	SetPt(&ps->fOrigin, port->portRect.left, port->portRect.top);
	FPoint zero = { 0, 0 };
	SetCurve(&ps->fCurve, zero, zero, zero);
	ps->fAllocated = 0x100;
	ps->fClip = NewRgn();
	ps->fTextOptions.fJustification = (Fixed) kPicDefaultTextOptions[0];
	ps->fTextOptions.fAlignment = (Fixed) kPicDefaultTextOptions[1];
	ps->fTextOptions.fWidth = (Fixed) kPicDefaultTextOptions[2];
	ps->fTextOptions.fReserved = (long) kPicDefaultTextOptions[3];
	ps->fTextOptions.fTransferMode = (long) kPicDefaultTextOptions[4];
	ps->fTextOptions.fFittedWidth = (Fixed) kPicDefaultTextOptions[5];
	ps->fTextOptions.fReserved2 = (long) kPicDefaultTextOptions[6];
	Handle picture = NewHandle(ps->fAllocated);
	if (picture == nil)
	{
		// ROM BUG, kept: the PicSave's clip region is not given back
		DisposHandle(save);
		DisposeStyleRecord(&style);
		return nil;
	}
	ps->fTextStyle.fFontFamily = AllocateRefHandle(NILREF);
	CopyStyle(&style);
	ps->fTextStyle.fFontFamily->ref = style.fFontFamily;
	ps->fTextStyle.fFontSize = style.fFontSize;
	ps->fTextStyle.fFontFace = style.fFontFace;
	ps->fTextStyle.fFontPattern = style.fFontPattern;
	ps->fTextStyle.fTransferMode = style.fTransferMode;
	ps->fTextStyle.fReserved14 = style.fReserved14;
	ps->fTextStyle.fReserved18 = style.fReserved18;
	ps->fTextStyle.fPattern = nil;
	PutBigEndianHalf(*picture, 10);
	PutBigEndianHalf(*picture + 2, (unsigned short) frame->top);
	PutBigEndianHalf(*picture + 4, (unsigned short) frame->left);
	PutBigEndianHalf(*picture + 6, (unsigned short) frame->bottom);
	PutBigEndianHalf(*picture + 8, (unsigned short) frame->right);
	ps->fPicture = picture;
	ps->fSize = 10;
	port->picSave = save;
	PutPicOpcode(0x11);
	PutPicWord(0x2ff);
	DisposeStyleRecord(&style);
	return (PicHandle) picture;
}


// ROM 0x00331c94 ClosePicture__Fv
// The end opcode written and the picture trimmed to what was written;
// the PicSave given back and the pen shown again.
void
ClosePicture(void)
{
	GrafPort* port = GetCurrentPort();
	if (port->picSave == nil)
		return;
	PicSave* ps = CurrentPicSave(port);
	PutPicOpcode(0xff);
	if (ps->fSize != 0)
		SetHandleSize(ps->fPicture, ps->fSize);
	DisposeRefHandle(ps->fTextStyle.fFontFamily);
	DisposeRgn(ps->fClip);
	DisposePattern(ps->fPnPat);
	DisposePattern(ps->fBkPat);
	HUnlock(port->picSave);
	DisposHandle(port->picSave);
	port->picSave = nil;
	ShowPen();
}


// ROM 0x00332088 KillPicture__FPP7Picture
void
KillPicture(PicHandle picture)
{
	DisposHandle((Handle) picture);
}


/*------------------------------------------------------------------------------
	W r i t i n g
------------------------------------------------------------------------------*/

// ROM 0x00334e88 StdPutPic
// The bytes added to the picture, which grows 256 bytes at a time.  When
// it cannot grow the recording has failed: the picture is cut back to an
// empty one (size 0xffff, an empty frame) and nothing more is written.
extern "C" void
StdPutPic(const char* data, long count)
{
	GrafPort* port = GetCurrentPort();
	if (port->picSave == nil)
		return;
	PicSave* ps = CurrentPicSave(port);
	long at = ps->fSize;
	if (at == 0)
		return;
	Handle picture = ps->fPicture;
	long size = at + count;
	ps->fSize = size;
	if (ps->fAllocated < size)
	{
		size += 0x100;
		ps->fAllocated = size;
		if (SetHandleSize(picture, size) != noErr)
		{
			ps->fSize = 0;
			SetHandleSize(picture, 10);
			PutBigEndianHalf(*picture, 0xffff);
			memset(*picture + 2, 0, 8);			// SetEmptyRect's rectangle
			return;
		}
	}
	PutBigEndianHalf(*picture, (unsigned short) size);
	BlockMove(data, *picture + at, count);
}


// ROM 0x00335130 PutPicData__FPcl
// The bytes through the port's putPicProc; a proc of the port's own is not
// expected to keep the count, so it is kept here.
void
PutPicData(const char* data, long count)
{
	GrafPort* port = GetCurrentPort();
	PutPicDataProc proc = PutPicProc(port);
	proc((Ptr) data, count);
	if (proc != (PutPicDataProc) StdPutPic)
		CurrentPicSave(port)->fSize += count;
}


// ROM 0x00331e7c PutPicByte__Fc
void
PutPicByte(long value)
{
	char byte = (char) value;
	PutPicData(&byte, 1);
}


// ROM 0x00331e9c PutPicOpcode__Fl
// An opcode, on a word boundary (a byte of nought first when it is not).
void
PutPicOpcode(long opcode)
{
	unsigned char bytes[2];
	PutBigEndianHalf(bytes, (unsigned short) opcode);
	if (CurrentPicSave(GetCurrentPort())->fSize & 1)
		PutPicByte(0);
	PutPicData((const char*) bytes, 2);
}


// ROM 0x00331ee4 PutPicWord__Fl
void
PutPicWord(long value)
{
	unsigned char bytes[2];
	PutBigEndianHalf(bytes, (unsigned short) value);
	PutPicData((const char*) bytes, 2);
}


// ROM 0x00331f10 PutPicLong__Fl
void
PutPicLong(long value)
{
	unsigned char bytes[4];
	PutBigEndianWord(bytes, (unsigned int) value);
	PutPicData((const char*) bytes, 4);
}


// host: a rectangle's four halfwords in the picture's order
static void
PutPicRectData(const Rect* r)
{
	unsigned char bytes[8];
	PutBigEndianHalf(bytes, (unsigned short) r->top);
	PutBigEndianHalf(bytes + 2, (unsigned short) r->left);
	PutBigEndianHalf(bytes + 4, (unsigned short) r->bottom);
	PutBigEndianHalf(bytes + 6, (unsigned short) r->right);
	PutPicData((const char*) bytes, 8);
}


// ROM 0x00331f2c PutPicRect__FlP4Rect
// A shape's opcode and its rectangle - or, the same rectangle as last
// time, the opcode with bit 3 set ("the same") alone.
void
PutPicRect(long opcode, const Rect* r)
{
	PicSave* ps = CurrentPicSave(GetCurrentPort());
	if (EqualRect(r, &ps->fRect))
	{
		PutPicOpcode(opcode + 8);
		return;
	}
	ps->fRect = *r;
	PutPicOpcode(opcode);
	PutPicRectData(r);
}


// ROM 0x00331f90 PutPicPoint__F5Point
void
PutPicPoint(Point pt)
{
	PutPicWord(pt.v);
	PutPicWord(pt.h);
}


// ROM 0x00331e30 PutPicRgn__FPP6Region
// A region or polygon: its size (less the two bytes the ARM's structure
// pads its size word with), then its box and data from +4.
void
PutPicRgn(RgnHandle rgn)
{
	long size = (*rgn)->rgnSize;
	PutPicWord(size - 2);
	HLock((Handle) rgn);
	// host: the box and data are halfwords, big-endian in the picture
	const char* p = (const char*) *rgn + 4;
	for (long i = 0; i + 1 < size - 4; i += 2)
	{
		unsigned char bytes[2];
		PutBigEndianHalf(bytes, (unsigned short) *(const short*) (p + i));
		PutPicData((const char*) bytes, 2);
	}
	HUnlock((Handle) rgn);
}


// ROM 0x003323c4 PutPicCurve__FlP5curve
// A curve: the opcode + 8 alone when it is the one the picture has, else
// the opcode and its six 16.16 values, remembered.  (Host: the values
// big-endian in the picture.)
void
PutPicCurve(long opcode, const curve* c)
{
	PicSave* ps = CurrentPicSave(GetCurrentPort());
	if (EqualCurve(c, &ps->fCurve))
	{
		PutPicOpcode(opcode + 8);
		return;
	}
	ps->fCurve = *c;
	PutPicOpcode(opcode);
	const Fixed* p = &c->first.x;
	for (long i = 0; i < 6; i++)
		PutPicLong(p[i]);
}


// ROM 0x00332434 PutPicPaths__FPP5paths
// Paths: the handle's size, then its bytes.  (Host: every one of them is a
// 32-bit word, big-endian in the picture.)
void
PutPicPaths(pathsHandle p)
{
	long size = GetHandleSize((Handle) p);
	PutPicLong(size);
	HLock((Handle) p);
	const Long32* words = (const Long32*) *p;
	for (long i = 0; i < size / 4; i++)
		PutPicLong(words[i]);
	HUnlock((Handle) p);
}


// ROM 0x00331d10 PutPicVerb__FUc
// What the verb draws with, where the picture does not say it already:
// frame the pen size, then (frame and paint) the pen mode, then (frame,
// paint and fill) the pen pattern; erase the background pattern; invert
// nothing.
void
PutPicVerb(GrafVerb verb)
{
	GrafPort* port = GetCurrentPort();
	Point pnSize = port->pnSize;
	long pnMode = port->pnMode;
	PicSave* ps;
	switch (verb)
	{
	case frame:
		ps = CurrentPicSave(port);
		if (ps->fPnSize.v != pnSize.v || ps->fPnSize.h != pnSize.h)
		{
			PutPicOpcode(0x07);
			PutPicPoint(pnSize);
			CurrentPicSave(port)->fPnSize = pnSize;
		}
		// fall through
	case paint:
		ps = CurrentPicSave(port);
		if (ps->fPnMode != pnMode)
		{
			PutPicOpcode(0x08);
			PutPicWord(pnMode);
			CurrentPicSave(port)->fPnMode = pnMode;
		}
		// fall through
	case fill:
		if (!EqualPat(port->fgPat, CurrentPicSave(port)->fPnPat))
		{
			PutPicPat(port->fgPat);
			DisposePattern(CurrentPicSave(port)->fPnPat);
			CurrentPicSave(port)->fPnPat = CopyPattern(port->fgPat);
		}
		break;
	case erase:
		if (!EqualPat(port->bgPat, CurrentPicSave(port)->fBkPat))
		{
			// ROM BUG, kept: BkPat's opcode is written and then PutPicPat
			// writes an opcode of its own (PnPat, 0x09, or FillPixPat) in
			// front of the pattern, so a player reads the background
			// pattern out of that opcode and six bytes of the pattern and
			// is out of step from there on
			PutPicOpcode(0x02);
			PutPicPat(port->bgPat);
			DisposePattern(CurrentPicSave(port)->fBkPat);
			CurrentPicSave(port)->fBkPat = CopyPattern(port->bgPat);
		}
		break;
	default:									// invert
		break;
	}
}


/*------------------------------------------------------------------------------
	P a t t e r n s
------------------------------------------------------------------------------*/

// ROM 0x00331fb8 PutPicPat__FPP8PixelMap
// The pen pattern: one bit deep, or two or four deep in black and white
// only, as PnPat and eight bytes; any other gray pattern as a FillPixPat
// (a pixel pattern of type 1); an eight-bit one not at all.
void
PutPicPat(PatternHandle pattern)
{
	long depth = (*pattern)->pixMapFlags & 0xff;
	HLock((Handle) pattern);
	if (depth == 1 || ((depth == 2 || depth == 4) && BlackOrWhitePat(pattern) != 0))
	{
		PutPicOpcode(0x09);
		PutPicData((const char*) GetPixelMapBits(*pattern), 8);
	}
	else if (depth == 2 || depth == 4)
	{
		PutPicOpcode(0x14);
		PutPixPat(*pattern);
	}
	HUnlock((Handle) pattern);
}


// ROM 0x00332040 PutPixPat__FP8PixelMap
// A pixel pattern: type 1, the pattern as one bit, the pixel map (a base
// address of nought first), its colour table and its eight rows.
void
PutPixPat(PixelMap* pm)
{
	PutPicWord(1);
	PutPat1Data(pm);
	PutPicLong(0);
	PutPixMap(pm);
	PutColorTable(pm->pixMapFlags & 0xff);
	PutPixData(pm);
}


// ROM 0x0033208c PutPat1Data__FP8PixelMap
// The pattern as eight bytes one bit deep: a pixel of two or four bits is
// black when any of its bits is set.
void
PutPat1Data(PixelMap* pm)
{
	long depth = pm->pixMapFlags & 0xff;
	const unsigned char* bits = (const unsigned char*) GetPixelMapBits(pm);
	if (depth == 1)
	{
		PutPicData((const char*) bits, 8);
		return;
	}
	if (depth == 2)
	{
		// a row of eight two-bit pixels is two bytes, so a word holds two
		// rows: the next word is read every other row
		unsigned int mask = 0xc0000000;
		unsigned int word = 0;
		for (long row = 8; row != 0; row--)
		{
			if ((row & 1) == 0)
			{
				mask = 0xc0000000;
				word = GetBigEndianWord(bits);
				bits += 4;
			}
			unsigned int out = 0;
			for (unsigned int bit = 0x80; bit != 0; bit >>= 1)
			{
				if (word & mask)
					out |= bit;
				mask >>= 2;
			}
			PutPicByte((long) out);
		}
		return;
	}
	if (depth == 4)
	{
		for (long row = 8; row != 0; row--)
		{
			unsigned int word = GetBigEndianWord(bits);
			bits += 4;
			unsigned int mask = 0xf0000000;
			unsigned int out = 0;
			for (unsigned int bit = 0x80; bit != 0; bit >>= 1)
			{
				if (word & mask)
					out |= bit;
				mask >>= 4;
			}
			PutPicByte((long) out);
		}
	}
}


// ROM 0x00332160 PutPixMap__FP8PixelMap
// A pixel map's header as a picture has it (after the base address): the
// row bytes with the high bit set, the bounds, version and packing
// nought, the resolutions as 16.16, and one component of the depth.
void
PutPixMap(PixelMap* pm)
{
	long depth = pm->pixMapFlags & 0xff;
	PutPicWord(pm->rowBytes | 0x8000);
	PutPicRectData(&pm->bounds);
	PutPicWord(0);								// version
	PutPicWord(0);								// packType
	PutPicLong(0);								// packSize
	PutPicLong((long) ((unsigned int) (unsigned short) pm->deviceRes.h << 16));
	PutPicLong((long) ((unsigned int) (unsigned short) pm->deviceRes.v << 16));
	PutPicWord(0);								// pixelType
	PutPicWord(depth);							// pixelSize
	PutPicWord(1);								// cmpCount
	PutPicWord(depth);							// cmpSize
	PutPicLong(0);								// planeBytes
	PutPicLong(0);								// pmTable
	PutPicLong(0);								// pmReserved
}


// ROM 0x00332208 PutColorTable__Fl
// A colour table of grays for the depth, white first; none for one bit.
void
PutColorTable(long depth)
{
	if (depth == 1)
		return;
	PutPicLong(0x616373);						// the seed
	PutPicWord(0);								// the flags
	unsigned long last = (1UL << depth) - 1;
	PutPicWord((long) last);
	unsigned long step = 0xffff / last;			// (__rt_udiv)
	unsigned long value = 0xffff;
	for (unsigned long i = 0; i <= last; i++)
	{
		PutPicWord((long) i);
		PutPicWord((long) value);
		PutPicWord((long) value);
		PutPicWord((long) value);
		value -= step;
	}
}


// ROM 0x003322a0 PutGrayTable__FP8PixelMap
// A pixel map's own gray table as a colour table, or the gray ramp when
// it has none.
void
PutGrayTable(PixelMap* pm)
{
	long depth = pm->pixMapFlags & 0xff;
	const UChar* grays = (const UChar*) pm->grayTable;
	PutPicLong(0x616373);
	PutPicWord(0);
	unsigned long last = (1UL << depth) - 1;
	PutPicWord((long) last);
	if (grays != nil)
	{
		for (unsigned long i = 0; i <= last; i++)
		{
			ULong red, green, blue;
			PutPicWord((long) i);
			GrayToRGB(grays[i], &red, &green, &blue, depth);
			PutPicWord((long) red);
			PutPicWord((long) green);
			PutPicWord((long) blue);
		}
		return;
	}
	unsigned long step = 0xffff / last;
	unsigned long value = 0xffff;
	for (unsigned long i = 0; i <= last; i++)
	{
		PutPicWord((long) i);
		PutPicWord((long) value);
		PutPicWord((long) value);
		PutPicWord((long) value);
		value -= step;
	}
}


// ROM 0x003323a0 PutPixData__FP8PixelMap
// A pattern's eight rows: depth bytes each.
void
PutPixData(PixelMap* pm)
{
	long depth = pm->pixMapFlags & 0xff;
	PutPicData((const char*) GetPixelMapBits(pm), depth * 8);
}


// ROM 0x00328628 EqualPat__FPP8PixelMapT1
// Whether two patterns are the same: the same handle, or the same row
// bytes, size and depth and the same bytes.
Boolean
EqualPat(PatternHandle a, PatternHandle b)
{
	if (a == b)
		return true;
	PixelMap* pa = *a;
	PixelMap* pb = *b;
	long height = pa->bounds.bottom - pa->bounds.top;
	if (pa->rowBytes != pb->rowBytes
	 || pb->bounds.bottom - pb->bounds.top != height
	 || pb->bounds.right - pb->bounds.left != pa->bounds.right - pa->bounds.left
	 || (pb->pixMapFlags & 0xff) != (pa->pixMapFlags & 0xff))
		return false;
	return memcmp(GetPixelMapBits(pa), GetPixelMapBits(pb), (size_t) (height * pa->rowBytes)) == 0;
}


// ROM 0x002aeed0 PackBits__FPPcT1l
// One row of count bytes packed: a run of three or more of the same byte
// as a negative count and the byte, anything else as a count less one
// and the bytes as they are; at most 128 either way.  The ROM's is
// assembly, a machine over three bytes at a time (the one before last,
// the last and the next - -1 standing for none), transcribed as it is: it
// runs two steps past the row with nothing coming in to flush it.
void
PackBits(char** srcPtr, char** dstPtr, long count)
{
	const unsigned char* src = (const unsigned char*) *srcPtr;
	char* dst = *dstPtr;
	char* literal = nil;			// where the literal run's count goes
	long before = -1;				// r6: the last byte
	long next = -1;					// r12: the byte coming in
	long state = 0;					// r4: 0 nothing, 1 a run, 2 literals
	long n = 0;						// r3
	long left = count + 1;
	if (left >= 0)
	{
		do
		{
			long last = before;		// lr: the byte before that
			before = next;
			next = -1;
			if (left > 1)
				next = *src++;
			if (state == 0)
			{
				if (next == last && next == before)
				{
					state = 1;
					n = 2;
				}
				else if (last != -1)
				{
					state = 2;
					n = 0;
					literal = dst;
					dst[1] = (char) last;
					dst += 2;
				}
			}
			else if (state == 1)
			{
				if (next == before)
				{
					if (++n < 0x7f)
						continue;
					next = -1;
				}
				before = -1;
				*dst++ = (char) -n;
				*dst++ = (char) last;
				state = 0;
			}
			else
			{
				if (last == before && last == next)
				{
					*literal = (char) n;
					state = 1;
					n = 2;
				}
				else
				{
					*dst++ = (char) last;
					if (++n >= 0x7f)
					{
						*literal = (char) n;
						state = 0;
					}
				}
			}
		}
		while (--left >= 0);
		if (state == 2)
			*literal = (char) n;
	}
	*srcPtr = (char*) src;
	*dstPtr = dst;
}


/*------------------------------------------------------------------------------
	W h a t   t h e   s t a n d a r d   p r o c s   a s k
------------------------------------------------------------------------------*/

// ROM 0x00335030 CheckPic__Fv
// Whether what is drawn is to be recorded: a picture is open and the pen
// is not hidden beyond the picture's own hiding.  The origin is written
// when the port's has moved since the picture last said, and the clip
// region when it differs.
Boolean
CheckPic(void)
{
	GrafPort* port = GetCurrentPort();
	if (port->picSave == nil || port->pnVis < -1)
		return false;
	long top = port->portRect.top;
	long left = port->portRect.left;
	PicSave* ps = CurrentPicSave(port);
	if (top != ps->fOrigin.v || left != ps->fOrigin.h)
	{
		long dh = left - ps->fOrigin.h;
		ps->fOrigin.h = (short) ((unsigned short) ps->fOrigin.h + dh);
		long dv = top - ps->fOrigin.v;
		ps->fOrigin.v = (short) ((unsigned short) ps->fOrigin.v + dv);
		PutPicOpcode(0x0c);
		PutPicWord(dh);
		PutPicWord(dv);
	}
	if (!EqualRgn(port->clipRgn, CurrentPicSave(port)->fClip))
	{
		PutPicOpcode(0x01);
		PutPicRgn(port->clipRgn);
		CopyRgn(port->clipRgn, CurrentPicSave(port)->fClip);
	}
	return true;
}



/*------------------------------------------------------------------------------
	T e x t
------------------------------------------------------------------------------*/

// (host) A style's seven words after its family, as the ROM writes them
// straight out of memory.  DEVIATION: two of them are a Ref and a pointer,
// wider on a host; only their low 32 bits go into the picture.
static void
PutPicStyleWords(const StyleRecord* style)
{
	PutPicLong(style->fFontSize);
	PutPicLong(style->fFontFace);
	PutPicLong((long) (Long32) (Ref) style->fFontPattern);
	PutPicLong(style->fTransferMode);
	PutPicLong(style->fReserved14);
	PutPicLong(style->fReserved18);
	PutPicLong((long) (Long32) (uintptr_t) style->fPattern);
}


// (host) The family word a style is recorded with: its font's Mac font id,
// or 0x800000 for a family the picture carries itself - an ink word, or
// the address of a block (an integer: a rich string's ink) - named again in
// 0x81a4.  A family without an integer macFontID is nought.
static long
PicFamilyWord(RefArg family)
{
	if (!IsInkWord(family) && !ISINT(family))
	{
		Ref id = GetFrameSlotRef(family, RSSYMmacfontid);
		return ISINT(id) ? RINT(id) : 0;
	}
	return 0x800000;
}


// (host) How many characters the runs come to (the whole text for one
// style), counted as the ROM counts them.
static long
RunsTotal(const TextObject* obj)
{
	long total = 0;
	const short* runs = obj->fRunLengths;
	long run;
	for (long left = obj->fLength; left > 0; left -= run)
	{
		if (runs == nil)
			run = obj->fLength;
		else
			run = *runs++;
		total += run;
	}
	return total;
}


// ROM 0x0035a680 DoPutText__FlN21
// A text object drawn at these scales, recorded when a picture is open.
//
// A picture for the Macintosh gets its old opcodes: the first style's font
// (TxFont, its family's macFontID), size and face, then LongText at the
// location with the characters in Mac Roman - every time, whether they
// changed or not.
//
// A Newton picture gets the scales (TxRatio) when they differ from the
// last text's, the options - TxMode alone when only the transfer mode
// differs, else 0x81a0 with all of them - then the style: 0x81a1 for one
// style (when it is not the one the picture has), or 0x81a2 with every
// run's length and style index and every style for several; a family is
// its Mac font id, or 0x800000 when the picture carries it in 0x81a4 (an
// ink word's bytes, or the block an integer family points at).  0x81a3 is
// the text: its length, location, flags (0x80 several styles, 0x40
// options, 0x20 a 0x81a4 follows) and the UniChars.
//
// ROM BUGS, kept:
//  - 0x81a0 does not remember the options it wrote, so the same options
//    are written again every time.
//  - LongText's count is a byte, but every character is written: a text of
//    more than 255 characters leaves a picture that cannot be read.
//  - with one style, an integer family's block is copied without its
//    length halfword and two bytes short, and 0x81a4 still writes the
//    length's worth, the last two whatever the buffer held (host: nought);
//    playing it back, 0x81a4 fills in only the several-style records, so
//    the one style's family stays the integer 0x800000.
// DEVIATIONS: the Mac characters are converted for the text's length into
// a buffer as long, where the ROM converts to the first nought character
// into 236 bytes on its stack; the options' last word is never called as
// the text getter it is in the ROM (a host long cannot hold a proc), so
// the text is always written straight from memory; and the UniChars and
// an integer family's length halfword, the ROM's words in memory, are
// written big-endian.
void
DoPutText(TextObjectRef text, Fixed hScale, Fixed vScale)
{
	if (!CheckPic())
		return;
	GrafPort* port = GetCurrentPort();
	PicSave* ps = CurrentPicSave(port);
	long multiStyle = 0;
	long withOptions = 0;
	long inlineSize = 0;
	long inlineCount = 0;
	char* inlineData = nil;
	RefVar family;
	TextObject* obj = TextObj(text);
	if (obj->fLength == 0)
		return;

	if (ps->fMacPicture)
	{
		StyleRecord* style = obj->fStyles[0];
		RefVar macFamily(style->fFontFamily);
		long id = (short) RINT(GetFrameSlotRef(macFamily, RSSYMmacfontid));
		Fixed size = style->fFontSize;
		long face = style->fFontFace;
		PutPicOpcode(0x03);							// TxFont
		PutPicWord(id);
		PutPicOpcode(0x0d);							// TxSize
		PutPicWord((short) ((ULong32) (size + 0x8000) >> 16));
		PutPicOpcode(0x04);							// TxFace
		PutPicByte(face);
		PutPicOpcode(0x28);							// LongText
		PutPicWord((short) ((ULong32) (obj->fLocation.y + 0x8000) >> 16));
		PutPicWord((short) ((ULong32) (obj->fLocation.x + 0x8000) >> 16));
		long count = RunsTotal(obj);
		PutPicByte(count);
		char* chars = (char*) QDNewTempPtr(count + 1);
		if (chars == nil)
			Throw(exOutOfMemory, nil, nil);
		ConvertFromUnicode((const UniChar*) obj->fText, chars, kMacRomanEncoding, count);
		PutPicData(chars, count);
		QDDisposeTempPtr(chars);
		return;
	}

	char state = 0;
	if (obj->fFlags & kTextObjAllocated)
	{
		state = HGetState((Handle) text);
		HLock((Handle) text);
		obj = TextObj(text);
	}
	if (ps->fTextHScale != hScale || ps->fTextVScale != vScale)
	{
		PutPicOpcode(0x10);							// TxRatio
		PutPicWord(vScale >> 8);
		PutPicWord(hScale >> 8);
		PutPicWord(0x100);
		PutPicWord(0x100);
		ps->fTextHScale = hScale;
		ps->fTextVScale = vScale;
	}
	TextOptions* options = obj->fOptions;
	if (options != nil)
	{
		TextOptions* had = &ps->fTextOptions;
		Boolean same = had->fJustification == options->fJustification && had->fAlignment == options->fAlignment
					&& had->fWidth == options->fWidth && had->fReserved == options->fReserved;
		if (had->fTransferMode != options->fTransferMode && same)
		{
			PutPicOpcode(0x05);						// TxMode
			PutPicWord((short) options->fTransferMode);
			had->fTransferMode = options->fTransferMode;
		}
		else if (!same)
		{
			PutPicOpcode(0x81a0);
			PutPicLong(0x1c);
			PutPicLong(options->fJustification);
			PutPicLong(options->fAlignment);
			PutPicLong(options->fWidth);
			PutPicLong(options->fReserved);
			PutPicLong(options->fTransferMode);
			PutPicLong(options->fFittedWidth);
			PutPicLong(options->fReserved2);
		}
		withOptions = 1;
	}

	if (obj->fRunLengths == nil)
	{
		StyleRecord* style = obj->fStyles[0];
		family = style->fFontFamily;
		StyleRecord had;
		had.fFontFamily = ps->fTextStyle.fFontFamily->ref;
		had.fFontSize = ps->fTextStyle.fFontSize;
		had.fFontFace = ps->fTextStyle.fFontFace;
		had.fFontPattern = ps->fTextStyle.fFontPattern;
		had.fTransferMode = ps->fTextStyle.fTransferMode;
		had.fReserved14 = ps->fTextStyle.fReserved14;
		had.fReserved18 = ps->fTextStyle.fReserved18;
		had.fPattern = ps->fTextStyle.fPattern;
		if (!EqualStyle(style, &had))
		{
			PutPicOpcode(0x81a1);
			PutPicLong(0x20);
			ps->fTextStyle.fFontFamily->ref = style->fFontFamily;
			ps->fTextStyle.fFontSize = style->fFontSize;
			ps->fTextStyle.fFontFace = style->fFontFace;
			ps->fTextStyle.fFontPattern = style->fFontPattern;
			ps->fTextStyle.fTransferMode = style->fTransferMode;
			ps->fTextStyle.fReserved14 = style->fReserved14;
			ps->fTextStyle.fReserved18 = style->fReserved18;
			ps->fTextStyle.fPattern = style->fPattern;
			if (!IsInkWord(family) && !ISINT(family))
				PutPicLong(PicFamilyWord(family));
			else
			{
				const char* data;
				if (ISINT(family))
				{
					const UniChar* block = (const UniChar*) RefToAddress(family);
					inlineSize = (short) *block;
					data = (const char*) (block + 1);
				}
				else
				{
					data = (const char*) BinaryData(family);
					inlineSize = Length(family) + 2;
				}
				inlineData = (char*) QDNewTempPtr(inlineSize);
				if (inlineData == nil)
					Throw(exOutOfMemory, nil, nil);
				memset(inlineData, 0, inlineSize);
				char* p = inlineData;
				if (IsInkWord(family))
				{
					PutBigEndianHalf(p, (unsigned short) (inlineSize - 2));
					p += 2;
				}
				BlockMove(data, p, inlineSize - 2);
				PutPicLong(0x800000);
				inlineCount = 1;
			}
			PutPicStyleWords(style);
		}
	}
	else
	{
		multiStyle = 1;
		long runCount = 0;
		const short* runs = obj->fRunLengths;
		for (long left = obj->fLength; left > 0; left -= *runs++)
			runCount++;
		long runsSize = runCount * 4;
		unsigned char* runData = (unsigned char*) QDNewTempPtr(runsSize);
		if (runData == nil)
			Throw(exOutOfMemory, nil, nil);
		StyleRecord** styles = obj->fStyles;
		runs = obj->fRunLengths;
		for (long i = 0; i < runCount; i++)
		{
			family = styles[i]->fFontFamily;
			PutBigEndianWord(runData + i * 4, (unsigned int) ((long) runs[i] | (i << 16)));
			if (ISINT(family))
				inlineSize += (short) *(const UniChar*) RefToAddress(family) + 2;
			else if (IsInkWord(family))
				inlineSize += Length(family) + 2;
		}
		PutPicOpcode(0x81a2);
		PutPicLong(runsSize + runCount * 0x20 + 2);
		PutPicByte(runCount);
		PutPicByte(runCount);
		PutPicData((const char*) runData, runsSize);
		QDDisposeTempPtr(runData);
		for (long i = 0; i < runCount; i++)
		{
			family = styles[i]->fFontFamily;
			PutPicLong(PicFamilyWord(family));
			PutPicStyleWords(styles[i]);
		}
		if (inlineSize != 0)
		{
			inlineData = (char*) QDNewTempPtr(inlineSize);
			if (inlineData == nil)
				Throw(exOutOfMemory, nil, nil);
			char* p = inlineData;
			for (long i = 0; i < runCount; i++)
			{
				family = styles[i]->fFontFamily;
				if (ISINT(family))
				{
					// the block: its length halfword (the bytes after it), then those
					const UniChar* block = (const UniChar*) RefToAddress(family);
					long size = (short) (*block + 2);
					PutBigEndianHalf(p, *block);
					BlockMove(block + 1, p + 2, size - 2);
					p += size;
					inlineCount++;
				}
				else if (IsInkWord(family))
				{
					long size = Length(family);
					PutBigEndianHalf(p, (unsigned short) size);
					BlockMove(BinaryData(family), p + 2, (short) size);
					p += 2 + (short) size;
					inlineCount++;
				}
			}
		}
	}

	long bytes = RunsTotal(obj) * 2;
	PutPicOpcode(0x81a3);
	PutPicLong(bytes + 0xd);
	PutPicWord((short) obj->fLength);
	PutPicLong(obj->fLocation.x);
	PutPicLong(obj->fLocation.y);
	long flags = 0;
	if (multiStyle)
		flags = 0x80;
	if (withOptions)
		flags |= 0x40;
	if (inlineSize != 0)
		flags |= 0x20;
	PutPicByte(flags);
	PutPicWord((short) bytes);
	const UniChar* chars = (const UniChar*) obj->fText;
	for (long i = 0; i < bytes / 2; i++)
		PutPicWord(chars[i]);
	if (inlineSize != 0)
	{
		PutPicOpcode(0x81a4);
		PutPicLong(inlineSize + 2);
		PutPicWord(inlineCount);
		PutPicData(inlineData, inlineSize);
		QDDisposeTempPtr(inlineData);
	}
	if (TextObj(text)->fFlags & kTextObjAllocated)
		HSetState((Handle) text, state);
}
