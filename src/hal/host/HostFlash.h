/*
	File:		hal/host/HostFlash.h

	Contains:	The internal flash's chips on a host: a file holding the
				banks' bytes as the processor sees them - big-endian words,
				0xFF where erased.  The flash is one bank at physical
				0x02000000 or two, the second at 0x10000000, each bank a
				pair of 16-bit chips on the 32-bit bus; the file holds bank 1
				and then bank 2, as Einstein keeps its flash.

				Sizes: 4 MB (one bank of 2 MB chips: an MP2000/2100's), 8 MB
				(two such banks, the most an MP2x00 ever had), and, because
				the ROM's own flash code takes more (docs/stores/README.md,
				"Bigger flash"), 16, 32, 64 or 128 MB - two banks of half the
				size each, of chips a quarter of it.

				Two file formats, told apart by the first eight bytes:

				  - *flat*: the flash's bytes and nothing else, as big as the
				    flash (a 4 or 8 MB flat file is Einstein's own);
				  - *sparse* ("NewtFlsh"): only the parts of the flash that
				    are not erased.  The flash is cut into 1 KB chunks; a
				    header and a map of one word per chunk are followed by
				    the chunks that hold anything, each appended the first
				    time a bit in it is cleared and given back when an erase
				    leaves it all 0xFF (its slot is then reused).  So a new
				    image is a few kilobytes and grows with what is written.
				    The format, all words big-endian:

				      +0x00  'NewtFlsh' (8 bytes)
				      +0x08  version (1)
				      +0x0c  the header's size (0x40)
				      +0x10  the flash's size in bytes
				      +0x14  a bank's size (the second bank follows the first)
				      +0x18  the chunk size (0x400)
				      +0x1c  the chunk count (flash size / chunk size)
				      +0x20  the map's offset (0x40)
				      +0x24  the chunks' offset (after the map, to a chunk)
				      +0x28  0 (reserved to +0x40)
				      map:   chunk count words: 0 an erased chunk, n the
				             chunk kept in slot n, at chunks' offset +
				             (n - 1) * chunk size

				    A chunk's bytes are written (and the C library's buffer
				    emptied) before the map word that names them, and a
				    freed chunk's map word is cleared before its slot is
				    used again, so a program stopped part-way leaves a file
				    whose every map word names whole bytes; at open a map
				    word naming a slot past the end of the file (a chunk
				    that never got there) reads as erased, and of two words
				    naming one slot (only an operating-system crash that
				    lost the order of the writes could leave that) the
				    first is kept.  A torn write inside a chunk that is
				    already there is a torn flash write, which the flash
				    store's own transactions are made to survive.  (No
				    fsync: the C library has none.)

				The whole flash is held in memory (as many bytes as the
				flash has: 128 MB for the largest) and written through to
				the file at every word and erase.

				The banks are registered as physical memory
				(HostRegisterPhysicalMemory), so the windows TNewInternalFlash
				maps onto 0x02000000 reach them.  What the chips do - a write
				that can only clear bits, a block erase that sets them all -
				is HostFlashWrite and HostFlashErase, which the host's flash
				driver (stores/flash/host/HostFlashDriver.cpp) calls.

				tools/stores/flashimage.py converts between the two formats
				and describes an image.

	Written by:	the reconstruction (DEVIATION: the machine's chips are hardware)
*/

#ifndef __HAL_HOSTFLASH_H
#define __HAL_HOSTFLASH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

const PAddr	kHostFlashBank1		= 0x02000000;
const PAddr	kHostFlashBank2		= 0x10000000;
const ULong	kHostFlashBankSize	= 0x00400000;		// 4 MB: an MP2x00's bank
const ULong	kHostFlashMinSize	= 0x00400000;		// 4 MB
const ULong	kHostFlashMaxSize	= 0x08000000;		// 128 MB (docs/stores/README.md, "Bigger flash")
const ULong	kHostFlashChunkSize	= 0x400;			// a sparse image's unit

enum HostFlashFormat
{
	kHostFlashFlat,			// the bytes, as big as the flash (Einstein's, at 4 or 8 MB)
	kHostFlashSparse		// 'NewtFlsh': only what is not erased
};

// Whether a flash of this many bytes can be made: 4 MB times a power of two,
// up to kHostFlashMaxSize.
Boolean		HostFlashValidSize(ULong size);

// Open (creating, erased, if it does not exist) the file that stands for
// the internal flash.  size and format are for a new file; an existing one
// keeps its own, which its header (or, for a flat file, its length) gives.
// Without a file, path nil, the flash is kept in memory only.
NewtonErr	HostFlashOpen(const char* path, ULong size = kHostFlashBankSize, HostFlashFormat format = kHostFlashFlat);
void		HostFlashClose(void);
Boolean		HostFlashIsOpen(void);
ULong		HostFlashSize(void);			// 0 when not open
ULong		HostFlashBankSize(void);		// a bank's: the flash's, or half of it
ULong		HostFlashChipSize(void);		// a chip's: half a bank
HostFlashFormat	HostFlashFileFormat(void);

// Writes reach the file through the C library's buffer, which is emptied
// when the program ends; a test that reads the file meanwhile flushes it.
void		HostFlashFlush(void);

// whether a host address is a byte of the flash
Boolean		HostFlashContains(Ptr p);

// The chips' rules, at a host address inside a bank.  A write ANDs the
// big-endian word `value | ~mask` into the word there (a chip only clears
// bits); an erase sets the lanes' bytes of `size` bytes of words to 0xFF.
void		HostFlashWrite(Ptr word, ULong value, ULong mask);
void		HostFlashErase(Ptr start, ULong size, ULong lanes);

// how many words and erases the chips have taken (for tests)
extern ULong	gHostFlashWrites;
extern ULong	gHostFlashErases;

#endif	/* __HAL_HOSTFLASH_H */
