/*
	File:		hal/host/HostFlash.h

	Contains:	The internal flash's chips on a host: a file holding the
				banks' bytes as the processor sees them - big-endian words,
				0xFF where erased - laid out as Einstein keeps its flash:
				bank 1 (physical 0x02000000, 4 MB) at the start of the file
				and, in an 8 MB file, bank 2 (physical 0x10000000) after it.

				The banks are registered as physical memory
				(HostRegisterPhysicalMemory), so the windows TNewInternalFlash
				maps onto 0x02000000 reach them.  What the chips do - a write
				that can only clear bits, a block erase that sets them all -
				is HostFlashWrite and HostFlashErase, which the host's flash
				driver (stores/flash/host/HostFlashDriver.cpp) calls; each
				change is written through to the file (buffered by the C
				library, and flushed when the file is closed or the program
				ends), so the flash survives the process.

	Written by:	the reconstruction (DEVIATION: the machine's chips are hardware)
*/

#ifndef __HAL_HOSTFLASH_H
#define __HAL_HOSTFLASH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

const PAddr	kHostFlashBank1		= 0x02000000;
const PAddr	kHostFlashBank2		= 0x10000000;
const ULong	kHostFlashBankSize	= 0x00400000;		// 4 MB each

// Open (creating, erased, if it does not exist) the file that stands for
// the internal flash.  size: 4 or 8 MB for a new file (one bank or two); an
// existing file keeps its own size, which must be one of the two.  Without
// a file, path nil, the banks are kept in memory only.
NewtonErr	HostFlashOpen(const char* path, ULong size = kHostFlashBankSize);
void		HostFlashClose(void);
Boolean		HostFlashIsOpen(void);
ULong		HostFlashSize(void);			// 0, 4 MB or 8 MB

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
