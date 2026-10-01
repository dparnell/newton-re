/*
	File:		comms/Beamer.h

	Contains:	Beaming's C++ side: TBeamer and the natives the Beam
				transport calls (BeamSend, BeamReceive, BeamCancel - the
				ROM's ZapSend, ZapReceive, ZapCancel: "Zap" was beaming's
				first name).  docs/comms/README.md, "Beaming - the plan".

				The transport (the ROM's NewtonScript frame 0x4d2565, over
				protoTransport) hands BeamSend a request and BeamReceive
				nothing; each makes a TBeamer over the transport frame and
				the beamer does the rest synchronously, in a fork of the
				NewtonScript world (TForkWorld::Fork, as the dock does) so
				the machine carries on meanwhile:

				  - Open chooses the IR protocol.  With the user preference
				    zapCommToolId unset it opens the probe service 'pkir'
				    first, asks it what it found (IdentifyProtocol: the
				    'irpt' option) and opens the real service - 'irda' when
				    the other side spoke IrDA, 'slir' (Sharp IR) otherwise;
				    set, the preference names the service outright.  A
				    sender connects, a receiver listens and accepts.
				  - A pipe over the endpoint (TEndpointPipe, framed for
				    'slir'), or, to a Sharp Wizard organiser, the frame
				    translators PWizard9Out/PWizard9In (not in the ROM: a
				    package supplies them).
				  - Send: with the Senior protocol or IrDA the number of
				    items first; then each item the transport's
				    BeamNextItem gives: a header frame (appSymbol, title,
				    the owner's name, the item's size, its type) and, if
				    the other side answers 1 - it has the room -, the item
				    as NSOF, the progress told to the transport's status
				    dialog through TBeamerCallback.  BeamCommitSend is told
				    how each went.
				  - Receive: the number of items, then for each the header,
				    the answer (whether the default store has the room),
				    the item read onto the store, and BeamCommitRecv.

				Receiving beams automatically (the In/Out Box's preference
				zapAutoReceive) is StartIRSniffing/StopIRSniffing, which
				open and close an endpoint on the IR sniffer
				(SniffIRTool.h, 'snif') - kept in gSnifferEndpoint.

				TBeamer's first word is the transport frame, which the
				NewtonScript messages are sent to; the field names are
				ours, their order the ROM's (offsets noted).

	Reconstructed from the MP2x00 US ROM (TBeamer 0x0003b6f0-0x0003d2ec,
	ZapSend 0x0003d818, ZapReceive 0x0003d9b4, ZapCancel 0x0003dac4,
	TBeamerCallback::Status 0x0003daf0, TBeamer::TBeamer 0x0003db74,
	SendSniffCommand 0x0003b5d8, StartIRSniffing 0x0003b6d4, StopIRSniffing
	0x0003c38c); each
	function cites its origin.
*/

#ifndef __COMMS_BEAMER_H
#define __COMMS_BEAMER_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

#ifndef __PIPES_H
#include "Pipes.h"
#endif

class TEndpoint;
class TBeamer;
class PFrameSink;
class PFrameSource;


// the pipe's progress, told to the transport's status dialog
class TBeamerCallback : public PipeCallBack
{
public:
	virtual Boolean	Status(long bytesRead, long bytesWritten);

	TBeamer*		fBeamer;				// +0x0c
	ULong			fReceiving;				// +0x10
};


class TBeamer
{
public:
						TBeamer(RefArg transport);
						~TBeamer();

	NewtonErr			Open(Boolean receiving);
	NewtonErr			Close(void);
	NewtonErr			Send(RefArg request);
	NewtonErr			Receive(void);

	NewtonErr			SendSetup(void);
	NewtonErr			ReceiveSetup(void);
	NewtonErr			OpenEndpoint(ULong service, Boolean receiving);
	NewtonErr			CloseEndpoint(void);
	NewtonErr			OpenPipe(Boolean receiving);
	NewtonErr			ClosePipe(void);
	NewtonErr			IdentifyProtocol(void);
	void				UpdateProgress(long percent, Boolean receiving);
	NewtonErr			SendNewton(RefArg item);
	NewtonErr			SendWizard(RefArg item);
	NewtonErr			ReceiveNewton(void);
	NewtonErr			ReceiveWizard(void);

	RefVar				fTransport;				// +0x00  the transport frame
	TEndpoint*			fEndpoint;				// +0x04
	CPipe*				fPipe;					// +0x08  a TEndpointPipe
	PFrameSink*			fWizardOut;				// +0x0c
	PFrameSource*		fWizardIn;				// +0x10
	ULong				fService;				// +0x14  'pkir', 'slir', 'irda'
	ULong				fProtocol;				// +0x18  what the tool agreed (irUsing...)
	ULong				fProtocolOptions;		// +0x1c
	long				fIndex;					// +0x20  the item in hand, from 1
	long				fCount;					// +0x24
	TBeamerCallback		fCallback;				// +0x28
	RefVar				fProgress;				// +0x3c  the status dialog's frame, while an item moves
};


// the Beam transport's natives
Ref		ZapSend(RefArg rcvr, RefArg transport, RefArg request);	// BeamSend
Ref		ZapReceive(RefArg rcvr, RefArg transport);				// BeamReceive
Ref		ZapCancel(RefArg rcvr, RefArg endpoint);				// BeamCancel

// receiving beams automatically: the IR sniffer's endpoint opened (true)
// or closed
NewtonErr	SendSniffCommand(UChar start);
Ref		StartIRSniffing(RefArg rcvr);
Ref		StopIRSniffing(RefArg rcvr);

void	RegisterBeamerNatives(void);

#endif	/* __COMMS_BEAMER_H */
