/*
	File:		qd/PicRecord.h

	Contains:	Recording a QuickDraw picture: what is drawn into a port
				while a picture is open is written into the picture too.

				OpenPicture makes the picture - a handle of 0x100 bytes to
				begin with, its size word and frame, then the version
				opcodes (0x0011 0x02ff: every picture the Newton records is
				version 2, word opcodes) - and a PicSave record (the port's
				picSave) that remembers what the picture already says: the
				clip region, the origin, the pen's size, mode and patterns,
				the last rectangle and curve and oval size, the text style.
				The standard procs (StdRect, StdOval, StdLine, ...) ask
				CheckPic whether a picture is open (it writes the origin and
				clip first when they have changed), write the pen state the
				verb needs (PutPicVerb: only what changed since last time),
				then their own opcode and data; a rectangle or curve the
				same as the last one is written as the opcode alone with
				bit 3 set ("the same").  Everything goes through the port's
				putPicProc, or StdPutPic, which grows the picture 256 bytes
				at a time; if that fails the picture is cut back to an
				empty one (size 0xffff) and nothing more is written.
				ClosePicture writes the end opcode and trims the handle.
				The pen is hidden while a picture is open, so what is
				recorded is not drawn.

	Reconstructed from the MP2x00 US ROM (0x00331980-0x00332470,
	0x00334e88-0x00335130); each function cites its origin.
*/

#ifndef __PICRECORD_H
#define __PICRECORD_H

#include "Ports.h"
#include "Fonts.h"
#include "Text.h"

class RefHandle;

// The text style a picture remembers: the ROM's StyleRecord with its font
// family held by a RefHandle (a StyleRecord's own RefStruct cannot live in
// a relocatable block).
struct PicTextStyle
{
	RefHandle*		fFontFamily;	// +0x00
	Fixed			fFontSize;		// +0x04
	long			fFontFace;		// +0x08
	Ref				fFontPattern;	// +0x0c
	long			fTransferMode;	// +0x10
	long			fReserved14;	// +0x14
	long			fReserved18;	// +0x18
	PatternHandle	fPattern;		// +0x1c
};

// What a picture being recorded already says (the port's picSave; the
// ROM's is a 0x9c-byte handle, the offsets its).
struct PicSave
{
	Handle			fPicture;		// +0x00  the picture
	long			fAllocated;		// +0x04  the handle's size
	long			fSize;			// +0x08  the bytes written (0: the recording failed)
	RgnHandle		fClip;			// +0x0c  the clip region it has
	PatternHandle	fPnPat;			// +0x10  the pen pattern it has
	PatternHandle	fBkPat;			// +0x14  the background pattern it has
	Fixed			fField18;		// +0x18  1.0
	Fixed			fField1c;		// +0x1c  1.0
	Point			fPnLoc;			// +0x20  where the last line it has ended
	Point			fPnSize;		// +0x24  the pen size it has
	long			fPnMode;		// +0x28  the pen mode it has (patCopy to begin with)
	Rect			fRect;			// +0x2c  the last rectangle it has
	Point			fOvalSize;		// +0x34  the round rectangles' corners it has
	Point			fOrigin;		// +0x38  the port's origin it has
	char			fField3c[8];	// +0x3c
	curve			fCurve;			// +0x44  the last curve it has
	Boolean			fMacPicture;	// +0x5c  made for the Macintosh (MakePict's style has macPict)
	TextOptions		fTextOptions;	// +0x60  the text options it has (kPicDefaultTextOptions to begin with)
	PicTextStyle	fTextStyle;		// +0x7c  the text style it has
};

PicHandle	OpenPicture(Rect* frame, Boolean macPicture);			// ROM 0x00331980 OpenPicture__FP4RectUc - nil when one is open already, or no memory
void		ClosePicture(void);										// ROM 0x00331c94 ClosePicture__Fv
void		KillPicture(PicHandle picture);							// ROM 0x00332088 KillPicture__FPP7Picture

// what the standard procs record with
Boolean		CheckPic(void);											// ROM 0x00335030 CheckPic__Fv - a picture is open and recording: the origin and clip written when they changed
void		PutPicVerb(GrafVerb verb);								// ROM 0x00331d10 PutPicVerb__FUc - the pen state the verb draws with, what changed
void		PutPicData(const char* data, long count);				// ROM 0x00335130 PutPicData__FPcl
void		PutPicByte(long value);									// ROM 0x00331e7c PutPicByte__Fc
void		PutPicOpcode(long opcode);								// ROM 0x00331e9c PutPicOpcode__Fl - word aligned
void		PutPicWord(long value);									// ROM 0x00331ee4 PutPicWord__Fl
void		PutPicLong(long value);									// ROM 0x00331f10 PutPicLong__Fl
void		PutPicRect(long opcode, const Rect* r);					// ROM 0x00331f2c PutPicRect__FlP4Rect - opcode + 8 alone for the last rectangle again
void		PutPicPoint(Point pt);									// ROM 0x00331f90 PutPicPoint__F5Point
void		PutPicRgn(RgnHandle rgn);								// ROM 0x00331e30 PutPicRgn__FPP6Region - a region or polygon
void		PutPicPat(PatternHandle pattern);						// ROM 0x00331fb8 PutPicPat__FPP8PixelMap - PnPat (0x09), or FillPixPat (0x14) for a gray one
void		PutPixPat(PixelMap* pm);								// ROM 0x00332040 PutPixPat__FP8PixelMap
void		PutPat1Data(PixelMap* pm);								// ROM 0x0033208c PutPat1Data__FP8PixelMap - the pattern as eight bytes of one bit
void		PutPixMap(PixelMap* pm);								// ROM 0x00332160 PutPixMap__FP8PixelMap
void		PutColorTable(long depth);								// ROM 0x00332208 PutColorTable__Fl - a gray ramp, white first
void		PutGrayTable(PixelMap* pm);								// ROM 0x003322a0 PutGrayTable__FP8PixelMap
void		PutPixData(PixelMap* pm);								// ROM 0x003323a0 PutPixData__FP8PixelMap
extern "C" void	StdPutPic(const char* data, long count);			// ROM 0x00334e88 StdPutPic

Boolean		EqualPat(PatternHandle a, PatternHandle b);				// ROM 0x00328628 EqualPat__FPP8PixelMapT1
void		PackBits(char** src, char** dst, long count);			// ROM 0x002aeed0 PackBits__FPPcT1l - one row packed, both pointers moved on

#endif	/* __PICRECORD_H */
