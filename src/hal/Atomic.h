/*
	File:		hal/Atomic.h

	Contains:	Critical sections.  On the Newton these disable interrupts
				(EnterAtomic masks IRQ, EnterFIQAtomic masks FIQ as well) with a
				nesting count, and are the kernel-internal versions of the
				EnterAtomicSWI/EnterFIQAtomicSWI system calls (SWI 3/4, 30/31).
				Each hardware or host port provides an implementation.

	ROM:		EnterAtomic 0x00389440, ExitAtomic 0x0038949c,
				EnterFIQAtomic 0x00389510, ExitFIQAtomic 0x00389530 (assembly)
*/

#ifndef __HAL_ATOMIC_H
#define __HAL_ATOMIC_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

extern "C" {
void	EnterAtomic(void);
void	ExitAtomic(void);
void	EnterFIQAtomic(void);
void	ExitFIQAtomic(void);

// true while any Enter*Atomic is outstanding (the SWI exit path skips
// scheduling then; the ROM reads the four gAtomic*NestCount globals)
Boolean	InAtomicSection(void);

// atomic exchange (the ARM swp / swpb instructions; ROM 0x003a4b84, 0x003a4b8c)
ULong	Swap(ULong* address, ULong value);
UChar	SwapByte(UChar* address, UChar value);
}

#endif	/* __HAL_ATOMIC_H */
