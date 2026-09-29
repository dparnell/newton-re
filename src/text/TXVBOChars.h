/*
	File:		text/TXVBOChars.h

	Contains:	TXVBOChars: a document's characters in a large binary on a
				store (stores/LargeBinaries.h) - what a protoTXView keeps its
				text in when SetStore gave it one (TXView.h).

				It is the chunked storage (TXChars.h) with every chunk the
				same 512 characters (0x400 bytes) of room, one after another
				in the binary: a chunk's characters are at chunk * 0x400,
				chunks are made and taken away by growing and shrinking the
				binary there (MungeLargeBinary), and the room a chunk no
				longer uses is cleared, so the compander packs it to
				nothing.  The binary - a 'text binary under a
				TLZStoreCompander - is made on the store the first time a
				chunk is wanted (`GetCharsVBO`); `SetCharsVBO` takes one
				that is already there and the store it is on.

	Reconstructed from the MP2x00 US ROM (0x0023ebb0-0x0023efc8); each
	function cites its origin.
*/

#ifndef __TXVBOCHARS_H
#define __TXVBOCHARS_H

#ifndef __TXCHARS_H
#include "TXChars.h"
#endif
#include "objects.h"

// The ROM's object is 0x18 bytes.
class TXVBOChars : public TXChunkedChars
{
public:
					TXVBOChars(RefArg store);						// ROM 0x0023ebb0 __ct__10TXVBOCharsFRC6RefVar

	virtual UniChar* GetChunkPtr(long chunk, Boolean forWrite, Boolean lock);	// ROM 0x0023ee20 GetChunkPtr__10TXVBOCharsFlUcT2
	virtual NewtonErr AllocateChunks(long at, long count);			// ROM 0x0023ee44 AllocateChunks__10TXVBOCharsFlT1
	virtual void	RemoveChunks(long at, long count);				// ROM 0x0023eed4 RemoveChunks__10TXVBOCharsFlT1
	virtual NewtonErr MungeChunk(long chunk, long at, long oldLen, TXTextDescriptor* source, long newLen);	// ROM 0x0023ef38 MungeChunk__10TXVBOCharsFlN21P16TXTextDescriptorT1

	void			SetCharsVBO(RefArg binary);						// ROM 0x0023ec2c SetCharsVBO__10TXVBOCharsFRC6RefVar
	Ref				GetCharsVBO(void);								// ROM 0x0023ed64 GetCharsVBO__10TXVBOCharsFv

	RefStruct		fBinary;		// +0x10  nil until a chunk is wanted
	RefStruct		fStore;			// +0x14
};

#endif	/* __TXVBOCHARS_H */
