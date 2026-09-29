/*
	File:		text/TXBinaryChars.h

	Contains:	TXBinaryChars: a document's characters kept in one
				NewtonScript string - the storage a protoTXView uses when
				it has no store to keep its text on (TXView.h).

				The string is the text and its terminating nought, so
				Count is the length less one; Replace grows or shrinks it
				and slides the rest along.  The questions a line asks
				(GetLineChars, AcquireCharChunk) answer straight into the
				string, locked, since it is all one chunk; GetLineChars'
				`chunk` and AcquireCharChunk's are left as they were -
				ReleaseCharChunk unlocks whatever it is given.

				TXVBOChars (the text in a large binary on a store, over
				TXChunkedChars) is NOT YET.

	Reconstructed from the MP2x00 US ROM (0x0023e7ec-0x0023ebb0); each
	function cites its origin.
*/

#ifndef __TXBINARYCHARS_H
#define __TXBINARYCHARS_H

#ifndef __TXCHARS_H
#include "TXChars.h"
#endif
#include "objects.h"

// The ROM's object is 8 bytes.
class TXBinaryChars : public TXChars
{
public:
	// nil: a new empty string.
					TXBinaryChars(RefArg string);					// ROM 0x0023e7ec __ct__13TXBinaryCharsFRC6RefVar

	virtual long	Count(void) const;								// ROM 0x0023e874 Count__13TXBinaryCharsCFv
	virtual NewtonErr Replace(long at, long count, TXTextDescriptor* source);	// ROM 0x0023e898 Replace__13TXBinaryCharsFlT1P16TXTextDescriptor
	virtual NewtonErr CopyTo(TXTextDescriptor* to, long at, long count);	// ROM 0x0023e9c4 CopyTo__13TXBinaryCharsFP16TXTextDescriptorlT2
	virtual UniChar* AcquireCharChunk(long at, long* chunk, long* count);	// ROM 0x0023ea2c AcquireCharChunk__13TXBinaryCharsFlPlT2
	virtual void	ReleaseCharChunk(long chunk);					// ROM 0x0023ea90 ReleaseCharChunk__13TXBinaryCharsFl
	virtual UniChar* GetLineChars(long at, long count, long* chunk);	// ROM 0x0023ea9c GetLineChars__13TXBinaryCharsFlT1Pl
	virtual UniChar	GetChar(long at);								// ROM 0x0023ead4 GetChar__13TXBinaryCharsFl
	virtual long	SearchChar(UniChar c, long at, long count);		// ROM 0x0023eb00 SearchChar__13TXBinaryCharsFUslT2
	virtual long	SearchCharBack(UniChar c, long at, long count);	// ROM 0x0023eb3c SearchCharBack__13TXBinaryCharsFUslT2
	virtual long	GetCtrlCharOffset(long at, long count, UniChar* found);	// ROM 0x0023eb78 GetCtrlCharOffset__13TXBinaryCharsFlT1PUs

	RefStruct		fString;		// +0x04
};

#endif	/* __TXBINARYCHARS_H */
