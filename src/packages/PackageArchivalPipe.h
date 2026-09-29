/*
	File:		packages/PackageArchivalPipe.h

	Contains:	CPackageArchivalPipe - a buffer pipe over a soup of package
				chunks, which is how a package too big to keep in one
				entry is archived: written through the pipe, each time the
				4K write buffer fills (Overflow) its contents become a new
				soup entry whose `PackageEntry` slot is a binary of those
				bytes, and the entry's unique id is added to a list of
				keys; read back through the pipe, each time the read buffer
				runs dry (Underflow) the next key's entry is found through
				a cursor over the soup and its bytes copied into the
				buffer.  store:RestoreSegmentedPackage(soup, keys) reads a
				package so archived onto a store.

				Init(soup, keys, forReading, forWriting) makes whichever of
				the two 4K segments is wanted (and, for reading, the cursor
				- a query of the soup's default index).  The ROM's object is
				0x20 bytes: the CBufferPipe, then the soup, the keys, the
				index of the next key to read and the cursor, the three
				Refs each in a RefHandle.

	Reconstructed from the MP2x00 US ROM (0x0010d190-0x0010d87c); each
	function cites its origin.
*/

#ifndef __PACKAGEARCHIVALPIPE_H
#define __PACKAGEARCHIVALPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif
#include "Frames.h"

class CPackageArchivalPipe : public CBufferPipe
{
public:
					CPackageArchivalPipe();							// ROM 0x0010d190 __ct__20CPackageArchivalPipeFv
	virtual			~CPackageArchivalPipe();						// ROM 0x0010d204 __dt__20CPackageArchivalPipeFv

	void			Init(RefArg soup, RefArg keys, Boolean forReading, Boolean forWriting);	// ROM 0x0010d27c Init__20CPackageArchivalPipeFRC6RefVarT1UcT3
	Ref				MakeNewPackageChunk(long size);					// ROM 0x0010d40c MakeNewPackageChunk__20CPackageArchivalPipeFl - a new entry with a binary of size bytes
	void			UpdateKeyList(RefArg entry);					// ROM 0x0010d488 UpdateKeyList__20CPackageArchivalPipeFRC6RefVar - the entry's unique id added to the keys
	void			GetPackageChunk(UByte** data, ULong* size);		// ROM 0x0010d4dc GetPackageChunk__20CPackageArchivalPipeFPPUcPUl - the next key's bytes

	virtual void	Reset(void);									// ROM 0x0010d25c Reset__20CPackageArchivalPipeFv
	virtual void	FlushRead(void);								// ROM 0x0010d5a0 FlushRead__20CPackageArchivalPipeFv
	virtual void	FlushWrite(void);								// ROM 0x0010d5bc FlushWrite__20CPackageArchivalPipeFv
	virtual void	Overflow(void);									// ROM 0x0010d5c4 Overflow__20CPackageArchivalPipeFv
	virtual void	Underflow(long count, Boolean& eof);			// ROM 0x0010d778 Underflow__20CPackageArchivalPipeFlRUc

	RefStruct*		fSoup;			// +0x10
	RefStruct*		fKeys;			// +0x14  the entries' unique ids, in order
	long			fIndex;			// +0x18  the next key to read
	RefStruct*		fCursor;		// +0x1c
};

#endif	/* __PACKAGEARCHIVALPIPE_H */
