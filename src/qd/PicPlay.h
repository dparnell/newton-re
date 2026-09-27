/*
	File:		qd/PicPlay.h

	Contains:	Playing a QuickDraw picture back into the current port.

				A 'picture binary is a Macintosh QuickDraw picture, kept as
				it came - big-endian, packed: a size word, the frame at +2,
				then opcodes.  DrawPicture maps the frame onto the rectangle
				it is given, sets the port up (an empty clip until the
				picture gives one, black pen and white background, a one
				pixel pen in patCopy) and has ParsePicCodes play one opcode
				at a time out of qdGlobals.fPicHandle/fPicOffset (StdGetPic)
				until one answers 0.  A version 1 picture has byte opcodes,
				version 2 (0x02ff) word opcodes kept word aligned; the
				opcode's low three bits are the verb (frame, paint, erase,
				invert, fill) and bit 3 says "the same rectangle again".
				Bitmaps and pixel maps (0x90-0x9b) go through GetPicBits,
				which unpacks only the rows that the clip leaves showing.

				On top of Apple's opcodes the Newton has its own: curves
				(0x0c80-0x0c84, 0x8088-0x808c), paths (0x8190-0x8194) and
				styled text (0x81a0-0x81a4).

				NOT YET RECONSTRUCTED: text (the 0x28-0x2b opcodes and the
				Newton's 0x81a0-0x81a4 are read and not drawn - NewText,
				CallDrawText, DrawPicText, TextCleanup), curves and paths
				(read, not drawn - MapCurve/CallCurve, MapPaths/CallPaths),
				pixel patterns (0x12-0x14 type 1: read and the pattern left
				as it was - ConvertPixPat's converters) and the recording
				side (OpenPicture, ClosePicture, PutPic*).

				The picture turned into NewtonScript shapes (DrawPicture's
				toShapes, which PictToShape asks for) plays the same opcodes
				through a table of procs, one per sixteen opcodes
				(OpcodeProcs): ParsePicCodes leaves what it read - mapped -
				in the fProc* fields and calls the opcode's proc instead of
				drawing, and the procs make the shapes
				(views/PictureShapes.cpp, since they are the view system's
				shapes).  An opcode under 0x20 calls its proc twice, with
				the opcode negated before it is played and as it is after.

	Reconstructed from the MP2x00 US ROM (0x00330068-0x00335130); each
	function cites its origin.
*/

#ifndef __PICPLAY_H
#define __PICPLAY_H

#include "Ports.h"
#include "Fonts.h"
#include "FixedGeometry.h"

struct PicPlay;
typedef void	(*OpcodeProc)(long opcode, PicPlay* play, GrafPort* port);

// The state of a picture being played (the ROM's is 0x13c bytes on the
// stack of DrawPicture; the fields are named by what the code does with
// them, the offsets the ROM's).  (Host: it holds Refs, so it is made
// field by field rather than cleared.)
struct PicPlay
{
	Rect		fRect = {};				// +0x00  the last rectangle read ("the same" opcodes use it again)
	char		fCurve[0x18] = {};		// +0x08  the last curve read
	Point		fLastPt = {};			// +0x20  where the last line ended, unmapped
	Point		fTextLoc = {};			// +0x24  where text goes, unmapped
	Point		fOvalSize = {};			// +0x28  a round rectangle's corners
	Rect		fFromRect = {};			// +0x2c  the picture's frame (moved by Origin)
	Rect		fToRect = {};			// +0x34  where it is drawn
	Fixed		fHScale = 0;			// +0x3c  the one over the other
	Fixed		fVScale = 0;			// +0x40
	Fixed		fTextHScale = 0;		// +0x44  that times TxRatio
	Fixed		fTextVScale = 0;		// +0x48
	RgnHandle	fPlayClip = nil;		// +0x4c  the picture's own clip, unmapped
	RgnHandle	fSavedClip = nil;		// +0x50  the port's clip it was called with
	long		fVersion = 0;			// +0x54  1, or 0x2ff
	StyleRecord	fXStyle;				// +0x74  0x81a1's style (the family, the size, the face; the rest as read)
	long		fStyleCount = 0;		// +0xa0  0x81a2's styles
	long		fXTextCount = 0;		// +0xa4  0x81a3's characters
	long		fTextFlags = 0;			// +0xa8  0x81a3's flags
	UniChar*	fXText = nil;			// +0xac  0x81a3's text (a temporary block)
	FPoint		fXTextLoc = {};			// +0xb0  where it goes, mapped
	long		fTextMode = 0;			// +0xc8  TxMode
	RefStruct	fTextFamily;			// +0xd4  the text style's family (TxFont's, or 0x81a1's once a text has used it)
	Fixed		fTextSize = 0;			// +0xd8  TxSize
	long		fTextFace = 0;			// +0xdc  TxFace
	long		fTextFont = 0;			// host: TxFont's Mac font id, as read
	long		fSpaceExtra = 0;		// +0xf4  SpExtra
	Point		fProcPt = {};			// +0xf8  for the procs: a line's end, mapped (the pen is its start); where text goes
	Rect		fProcRect = {};			// +0xfc  a rectangle, oval, arc's box, or a bitmap's destination, mapped
	long		fProcMode = 0;			// +0x104 an arc's start angle, a bitmap's mode, a comment's kind
	long		fProcArc = 0;			// +0x108 an arc's angle
	Handle		fProcHandle = nil;		// +0x10c a polygon or region, mapped
	PixelMap*	fProcBits = nil;		// +0x110 a bitmap's pixels, as unpacked
	UniChar*	fProcText = nil;		// +0x114 the text of 0x28-0x2b, terminated
	ULong		fFgGray = 0;			// +0x118 the gray the last RGBFgCol came to (a pattern opcode after it is made in it)
	ULong		fBgGray = 0;			// +0x11c RGBBkCol's
	PolyHandle	fInkPoly = nil;			// +0x120 lines gathered into a polygon while they join end to end
	Point		fInkLastPt = {};		// +0x124 where the last of them ended
	RefStruct	fShape;					// +0x128 the shape waiting to be added (a frame of the same bounds may replace it)
	RefStruct	fStyle;					// +0x12c its style frame
	RefStruct	fLastStyle;				// +0x130 the style frame added last (one only goes in when it changes)
	RefStruct	fShapes;				// +0x134 the shapes and styles: DrawPicture's answer
	char		fInlineFamily[256] = {};	// host: which of 0x81a2's styles name their family in 0x81a4 (the ROM's +0x9c styles)
};

// The procs that turn a picture into shapes (views/PictureShapes.cpp sets
// it: the ROM's DrawPicture names its OpcodeProcs table, which is the
// view system's; the host's qd does not link the views).
extern const OpcodeProc*	gOpcodeProcs;								// ROM 0x00380a9c OpcodeProcs

// A picture played into the rectangle, or (toShapes) turned into an array
// of NewtonScript shapes with a style frame in front of each run of them
// that draws alike; ==> the shapes, nil when it was drawn.
Ref			DrawPicture(PicHandle picture, Rect* dstRect, Boolean toShapes);	// ROM 0x003337fc DrawPicture__FPP7PictureP4RectUc
Boolean		ImpossibleToDraw(GrafPort* port);								// ROM 0x00330068 ImpossibleToDraw__FP8GrafPort - no clip left, or the pen mode 0x17 (nil: possible)
void		MapFPoint(FPoint* pt, const Rect* src, const Rect* dst);		// ROM 0x0033519c MapFPoint__FP6FPointP4RectT2
// One opcode; ==> 0 at the end (or when the picture cannot go on).
long		ParsePicCodes(PicPlay* play, const OpcodeProc* procs);			// ROM 0x0033249c ParsePicCodes__FP7PicPlayPCPFlT1P8GrafPort_v
OpcodeProc	LookupOpcodeEntry(ULong opcode, const OpcodeProc* procs);		// ROM 0x00332470 LookupOpcodeEntry__FUlPCPFlP7PicPlayP8GrafPort_v
long		GetPicBits(long opcode, PicPlay* play, const OpcodeProc* procs);	// ROM 0x003346b4 GetPicBits__FlP7PicPlayPCPFT1T2P8GrafPort_v
PatternHandle	GetPicPixPat(long type);									// ROM 0x00333dc0 GetPicPixPat__Fl
long		GetPicGrayTable(long depth, UChar** table);					// ROM 0x00334398 GetPicGrayTable__FlPPUc

// the picture's bytes
void		GetPicData(char* data, long count);							// ROM 0x00334498 GetPicData__FPcl
void		GetPicDiscard(long count);										// ROM 0x00334450 GetPicDiscard__Fl
long		GetPicHandle(Handle* h);										// ROM 0x003344d8 GetPicHandle__FPPP10GenericRec - a region or polygon; ==> 0 when it was read
long		GetPicWord(void);												// ROM 0x0033455c GetPicWord__Fv - signed
long		GetPicLong(void);												// ROM 0x003345d4 GetPicLong__Fv
long		GetPicSByte(void);												// ROM 0x003345f8 GetPicSByte__Fv
long		GetPicUByte(void);												// ROM 0x00334620 GetPicUByte__Fv
Point*		GetPicPoint(Point* pt);										// ROM 0x00334644 GetPicPoint__FP5Point
long		GetPicResvOpcode(long count, Boolean readCount);				// ROM 0x0033467c GetPicResvOpcode__FlUc
void		PicComment(short kind, short size, Handle data);				// ROM 0x00334584 PicComment__FsT1PPc
extern "C" void	StdGetPic(Ptr data, long count);							// ROM 0x00334f68 StdGetPic
extern "C" void	StdComment(short kind, short size, Handle data);			// ROM 0x00334fb8 StdComment

// PackBits and its sixteen-bit form: one row of *dst's count bytes out of
// the packed bytes at *src, both pointers moved on.
void		UnpackBits(char** src, char** dst, long count);				// ROM 0x002aefc0 UnpackBits__FPPcT1l
void		UnpackWords(char** src, char** dst, long count);				// ROM 0x002af03c UnpackWords__FPPcT1l

#endif	/* __PICPLAY_H */
