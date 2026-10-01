/*
	File:		pcmcia/CardPipe.h

	Contains:	TCardPipe: a read-only pipe over a stretch of a card's
				memory - how the card server loads a package a card's CIS
				says it carries in attribute memory (TCardServer::
				LoadCardPackage), where only every other byte is there: an
				attribute-memory pipe reads the odd bytes, one at a time
				through CardAttrMemReadByte; a common-memory one reads them
				all.

				Writing does nothing and a write position is always nought;
				a bus fault while reading is a pipe exception.

	Reconstructed from the MP2x00 US ROM (0x0004fefc-0x000502f8); each
	function cites its origin.
*/

#ifndef __CARDPIPE_H
#define __CARDPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif

class TCardPipe : public CPipe
{
public:
					TCardPipe(void* base, ULong size, UChar attributeMemory);
	virtual			~TCardPipe();

	virtual long	ReadSeek(long offset, int mode);
	virtual long	ReadPosition(void) const;
	virtual long	WriteSeek(long offset, int mode);
	virtual long	WritePosition(void) const;
	virtual void	ReadChunk(void* data, long& count, Boolean& eof);
	virtual void	WriteChunk(const void* data, long count, Boolean flush);
	virtual void	FlushRead(void);
	virtual void	FlushWrite(void);
	virtual void	Reset(void);
	virtual void	Overflow(void);
	virtual void	Underflow(long count, Boolean& eof);

	UByte*			fBase;			// +0x04  the card memory's start
	ULong			fSize;			// +0x08  how many bytes (attribute memory: odd bytes)
	ULong			fPosition;		// +0x0c
	ULong			fAttributeMemory;	// +0x10  read the odd bytes
};

#endif	/* __CARDPIPE_H */
