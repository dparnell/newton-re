/*
	File:		hal/CardBus.h

	Contains:	A PC card's registers as the processor reads and writes
				them: a byte, and a word, at an address in one of a socket's
				windows.

				On a MessagePad these are plain loads and stores - the ROM's
				card drivers write `ldrb`/`strb`/`ldr`/`str` straight at the
				window, and a card answers on the bus.  A card whose
				registers do something when they are read or written (an ATA
				card's task file and data port) cannot be host memory, so
				DEVIATION: the drivers that drive such registers
				(pcmcia/ATASimple.cpp) go through these, and a host routes an
				access in an I/O card's register window to its model of the
				card (hal/host/HostATA.cpp); anything else is memory, as on
				the machine.

				A word access keeps the ARM's semantics, because the ROM's
				ATA driver relies on them: `CardBusReadWord` at an address
				that is not a multiple of four answers the aligned word
				rotated right by eight bits per byte of misalignment (a
				StrongARM's `ldr`), and `CardBusWriteWord` writes the whole
				aligned word whatever the address's low bits.  Words are
				big-endian, as the bus is.
*/

#ifndef __CARDBUS_H
#define __CARDBUS_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

UByte		CardBusReadByte(volatile void* address);
void		CardBusWriteByte(volatile void* address, UByte value);
ULong32		CardBusReadWord(volatile void* address);
void		CardBusWriteWord(volatile void* address, ULong32 value);

#endif /* __CARDBUS_H */
