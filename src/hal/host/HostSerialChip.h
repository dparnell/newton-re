/*
	File:		hal/host/HostSerialChip.h

	Contains:	The host's serial port: a TSerialChip (hal/HALSerialChip.h)
				registered as the MessagePad's external serial port ('extr')
				whose wire is a TCP socket.  It listens on a port (3679 by
				default, as Einstein does); a desktop that connects is a
				cable plugged in - DCD, DSR and CTS asserted - and the bytes
				either side sends are the serial line's.  That is how a
				desktop reaches a MessagePad over a network: NCX and the
				like dock with an emulated Newton this way, speaking the
				serial dock protocol (MNP) over the socket.

				DEVIATION (hardware): the ROM's external port is the Voyager
				chip's first serial channel (TSerialChipVoyager, registered by
				InitializeCommHardware 0x000ea0b4); this one stands in for it.
				It is a simple chip - no DMA - so the serial tool drives it a
				byte at a time from its interrupts: the receive interrupt
				when bytes have arrived (the tool reads them while RxBufFull),
				the transmit-empty one while it has room for more (the tool
				puts a byte or says it has none, ResetTxBEmpty), and the
				external-status one when a desktop connects or goes.  The
				interrupts are a host interrupt source
				(HostInterruptSources.h), polled every few milliseconds while
				a tool has the chip.

				docs/comms/README.md, "The desktop connection (Dock)".
*/

#ifndef __HOSTSERIALCHIP_H
#define __HOSTSERIALCHIP_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#define kHostSerialPort		3679		// Einstein's

// The registry made (if it is not yet), the host's chip made and registered
// as 'extr', listening on the port (0: any - HostSerialChipPort says which).
// ==> noErr, or why not.
NewtonErr	HostSerialChipInstall(unsigned short port);
unsigned short	HostSerialChipPort(void);

#endif
