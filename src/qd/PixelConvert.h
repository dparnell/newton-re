/*
	File:		qd/PixelConvert.h

	Contains:	The row converters that turn pixels of another kind into
				the Newton's grays, in place: indexed pixels through a gray
				table of their own depth (ConvertIndex2, ConvertIndex4,
				ConvertIndex8, and ConvertIndex8to4, which packs two eight-
				bit pixels into a byte of four-bit ones), and direct colour
				- 16-bit (1-5-5-5), 32-bit (x-8-8-8), 24-bit with no pad
				byte, and 32-bit stored a component plane at a time - made
				four-bit grays two to a byte (RGBtoGray).  Each takes a row,
				the gray table (nil for direct pixels) and the row's bytes.
				A pixel pattern out of a picture goes through them
				(ConvertPixPat, PicPlay.cpp), and so does every source row
				StretchBits converts (Stretch.cpp's SetupConversion; its
				CombineX variants fold further rows into it).

	Reconstructed from the MP2x00 US ROM (0x00074c08-0x000755e0); each
	function cites its origin.
*/

#ifndef __PIXELCONVERT_H
#define __PIXELCONVERT_H

#include "Ports.h"

typedef void	(*PixelConverter)(char* row, const UChar* table, long count);

void	ConvertDirect16to4(char* row, const UChar* table, long count);			// ROM 0x00074c08 ConvertDirect16to4__FPcPUcl
void	ConvertDirect32to4(char* row, const UChar* table, long count);			// ROM 0x00074dbc ConvertDirect32to4__FPcPUcl
void	ConvertDirectComp32to4(char* row, const UChar* table, long count);		// ROM 0x00074f90 ConvertDirectComp32to4__FPcPUcl
void	ConvertDirectNoPad32to4(char* row, const UChar* table, long count);		// ROM 0x0007510c ConvertDirectNoPad32to4__FPcPUcl
void	ConvertIndex2(char* row, const UChar* table, long count);				// ROM 0x000752d0 ConvertIndex2__FPcPUcl
void	ConvertIndex4(char* row, const UChar* table, long count);				// ROM 0x00075430 ConvertIndex4__FPcPUcl
void	ConvertIndex8(char* row, const UChar* table, long count);				// ROM 0x00075574 ConvertIndex8__FPcPUcl
void	ConvertIndex8to4(char* row, const UChar* table, long count);			// ROM 0x00075598 ConvertIndex8to4__FPcPUcl

// (host) the colour screen's: rows made eight-bit palette entries (qd/Colour.h)
void	ConvertDirect16to8(char* row, const UChar* table, long count);
void	ConvertDirect32to8(char* row, const UChar* table, long count);
void	ConvertDirectNoPad32to8(char* row, const UChar* table, long count);
void	ConvertDirectComp32to8(char* row, const UChar* table, long count);
void	ConvertIndex1to8(char* row, const UChar* table, long count);
void	ConvertIndex2to8(char* row, const UChar* table, long count);
void	ConvertIndex4to8(char* row, const UChar* table, long count);

#endif	/* __PIXELCONVERT_H */
