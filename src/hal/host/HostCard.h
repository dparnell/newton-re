/*
	File:		hal/host/HostCard.h

	Contains:	A PC card on a host: a card image file in Einstein's
				container (TLinearCard, docs/stores/README.md "Einstein's
				files") - the common memory as the machine reads it, the
				CIS, an optional icon, the card's name and a 52-byte
				big-endian footer - put into a socket and taken out again.

				The attribute memory is laid out as Einstein presents it,
				which is how the card's 8-bit CIS reaches the 32-bit bus:
				attribute byte o is CIS byte (o / 2) ^ 1, so the ROM's
				(address ^ 3) reads of the even addresses find CIS byte
				i at 2i.  The common memory is the file's data section,
				byte for byte.  What the card's flash chips do - a write
				that can only clear bits, a block erase that sets them all
				- is HostCardFlashWrite and HostCardFlashErase, which the
				host's card TFlash calls (stores/flash/host/); each change
				is written through to the file (buffered by the C library,
				flushed when the card is removed or the program ends).

				An ATA card (a fixed disk: the image's type 0xD, a
				function-specific CISTPL_DEVICE; tools/cards/atacard.py
				makes one) is different: its data section is the disk, a
				512-byte sector after another, and its common memory is a
				0x800-byte register window that hal/host/HostATA.cpp stands
				a model of the card's task file behind (hal/CardBus.h).

				A card put in or taken out is a change of the socket's
				pins; the socket (hal/host/HostCardSocket.cpp) is told
				through the proc HostCardSetChangeProc installs, which
				only latches the change: the socket's own interrupt source
				delivers it, as the card-detect interrupt on the machine.

	Written by:	the reconstruction (DEVIATION: a card is hardware)
*/

#ifndef __HAL_HOSTCARD_H
#define __HAL_HOSTCARD_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

const ULong	kHostCardSockets		= 2;			// an MP2x00 has two slots
const ULong	kHostCardAttrSize		= 0x40000;		// the attribute window the host keeps (128K bytes of attribute memory: a CIS, and a package a card carries there - pcmcia/CardPipe.h)
const ULong	kHostCardImageInfoSize	= 52;			// Einstein's ImageInfo footer

// A blank linear flash card made as a file: sizeMB of erased common memory
// (2, 4, 8, ... 64), a CIS describing an Intel Series 2 flash card of that
// size (CISTPL_DEVICE, CISTPL_JEDEC_C 0x89 0xA0, CISTPL_DEVICE_GEO - 64 KB
// blocks on a 16-bit bus, which the ROM's TFlashSeries2 takes its geometry
// from - and CISTPL_VERS_1 with the name), no icon, the name, and the footer.
NewtonErr	HostCardCreate(const char* path, ULong sizeMB, const char* name);

// The card in a file put into the socket (the machine sees it at the next
// interrupt check).  readOnly: the write-protect switch set (also when the
// file cannot be opened for writing).  kError_Bad_Parameters for a file
// that is not a card image.
NewtonErr	HostCardInsert(ULong socket, const char* path, Boolean readOnly = false);
// The card taken out (its file flushed and closed).
void		HostCardRemove(ULong socket);

Boolean		HostCardIsInserted(ULong socket);
Boolean		HostCardIsWriteProtected(ULong socket);
const char*	HostCardName(ULong socket);					// the image's name ("" without one)
ULong		HostCardType(ULong socket);					// the image's type (CISTPL_DEVICE's high nibble)
const ULong	kHostCardTypeATA		= 0xD;				// DTYPE_FUNCSPEC: an ATA card
const ULong	kHostCardATAWindowSize	= 0x800;			// an ATA card's register window

// An ATA card's disk: its size in sectors, and sectors read and written
// (each write goes straight to the file).  kError_Bad_Parameters for a
// sector past the end or a card that is not ATA; a write to a
// write-protected card too.
Boolean		HostCardIsATA(ULong socket);
ULong		HostCardATASectors(ULong socket);
NewtonErr	HostCardATARead(ULong socket, ULong sector, void* buffer);
NewtonErr	HostCardATAWrite(ULong socket, ULong sector, const void* buffer);
// Which ATA card's register window an address is in: its socket and the
// offset into the window; false for none.
Boolean		HostCardATAWindow(const volatile void* address, ULong* socket, ULong* offset);

// The windows, as host addresses (nil without a card)
Ptr			HostCardAttributeMemory(ULong socket);
Ptr			HostCardCommonMemory(ULong socket);
ULong		HostCardCommonSize(ULong socket);
ULong		HostCardCISSize(ULong socket);

// The flash chips' rules at an offset into the common memory.  A write ANDs
// the bytes into what is there (a chip only clears bits); an erase sets
// `size` bytes to 0xFF.  Both refuse a write-protected card.
NewtonErr	HostCardFlashWrite(ULong socket, ULong offset, const void* bytes, ULong count);
NewtonErr	HostCardFlashErase(ULong socket, ULong offset, ULong size);
void		HostCardFlush(ULong socket);

// Told when a socket's card has come or gone (from whatever thread put it
// in or took it out: it may only latch the change).
typedef void	(*HostCardChangeProc)(ULong socket, Boolean inserted);
void		HostCardSetChangeProc(HostCardChangeProc proc);

#endif	/* __HAL_HOSTCARD_H */
