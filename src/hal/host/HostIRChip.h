/*
	File:		hal/host/HostIRChip.h

	Contains:	The host's infrared port: a TSerialChip (hal/HALSerialChip.h)
				registered as the MessagePad's built-in IR ('infr') whose
				medium is a TCP connection to another host Newton - one
				listening, the other connecting - so that one host running
				the OS can beam to another (docs/comms/README.md, "Beaming").

				DEVIATION (hardware): the ROM's IR port is the Voyager chip's
				IR channel (TSerialChipVoyager, 0x001d6780, with its IR
				controller); this one stands in for it.  What the IR tools
				ask of the hardware is kept:
				  - the modulation.  The 'irlk' option (THMOSerIRLinkConfig)
				    switches the port between Sharp's ASK (mode 0) and IrDA
				    SIR (modes 1-3), and a receiver hears only what was sent
				    the way it is listening - with the auto-receive flag it
				    hears both and its status says which the last byte came
				    as (kSerIRLinkSts_IRDADetect, the Voyager's GPIO 8).  So
				    every byte crosses the socket as a pair: the modulation
				    it was sent with and the byte;
				  - half duplex.  ConfigureForOutput(true) turns the receiver
				    off and the transmitter on, (false) the other way round,
				    as the Voyager's does (0x001d7abc); a byte arriving while
				    the receiver is off, the chip is not claimed or its power
				    is off is lost, as light is - it is not kept for later;
				  - the speed.  Bytes reach the tool no faster than the
				    port's speed (ten bits a byte), as HostSerialChip paces
				    them.
				It is a simple chip - no DMA - driven a byte at a time from
				its interrupts, a host interrupt source polled every few
				milliseconds while a peer is connected.

				The medium is a TCP connection to one other newton
				(--ir-peer) or the LAN medium (--ir-lan): a UDP multicast
				group every newton on the network that was given --ir-lan
				joins, so one can beam to another without knowing where it
				is.  Each poll's bytes go out as one datagram (a burst,
				still (modulation, byte) pairs, behind a header naming the
				newton that sent it and numbering its datagrams); each
				newton hears every other's and drops its own.  It is a
				shared medium as the air in front of a MessagePad is: what
				one sends every other hears, so the protocols' own ways of
				finding who is there (IrDA's discovery, the probe's TEST
				frames, Sharp IR's offers) find whoever is listening - and,
				as a user points one MessagePad at another, a newton faces
				the first it hears and hears only that one, so a beam is
				taken by one receiver however many listen.  A
				datagram lost is light lost (the protocols retransmit); one
				overtaken by a later one is dropped as lost, never heard
				out of order (docs/comms/README.md, "Beaming over the
				network").
*/

#ifndef __HOSTIRCHIP_H
#define __HOSTIRCHIP_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TSerialChip;

// An IR chip made over a peer: "listen:PORT" listens on the loopback
// interface (PORT 0: any - HostIRChipPort says which) for the other host
// to connect; "HOST:PORT" connects to it (again and again until it
// answers, since the other may start later); "lan", "lan:PORT" or either
// with "@ADDRESS" is the LAN medium - the multicast group 239.255.78.119
// on PORT (3681 by default) joined on every IPv4 interface that can
// multicast, or only the one whose address is ADDRESS (127.0.0.1 keeps
// it to the machine).  NEWTON_IR_LAN_LOSS=N loses N per cent of the
// datagrams heard.  Not registered.
// ==> noErr and the chip, or why not.
NewtonErr		HostIRChipMake(const char* peer, TSerialChip** chip);
unsigned short	HostIRChipPort(TSerialChip* chip);
Boolean			HostIRChipConnected(TSerialChip* chip);

// The registry made (if it is not yet), a chip made over the peer and
// registered as 'infr' - newton's --ir-peer.  A nil peer is a port with
// nobody in front of it (newton without --ir-peer: what it sends goes
// nowhere, as light does, and a beam finds nobody).  A peer may also be
// nil for HostIRChipMake.  ==> noErr, or why not.
NewtonErr		HostIRChipInstall(const char* peer);
TSerialChip*	HostIRChipInstalled(void);

#endif
