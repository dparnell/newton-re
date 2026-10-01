/*
	File:		comms/Beamer.cpp

	Contains:	TBeamer, TBeamerCallback and the Beam transport's natives
				(Beamer.h).

	Reconstructed from the MP2x00 US ROM (0x0003b6f0-0x0003dc10); each
	function cites its origin.
*/

#include "Beamer.h"
#include "Endpoint.h"
#include "EndpointPipe.h"
#include "CommServices.h"
#include "Translators.h"
#include "Options.h"
#include "SerialOptions.h"
#include "ObjectStreamer.h"
#include "Frames.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "Locale.h"
#include "Unicode.h"
#include "NewtWorld.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

extern const ExceptionName exPipeException;
extern const ExceptionName exTranslatorException;

// the transport's errors (the ROM's numbers; no header names them)
#define kBeamErrNoItem				(ERRBASE_NEWT - 303)	// the transport gave no item, or failed to
#define kBeamErrItemMissing			(ERRBASE_NEWT - 305)	// the transport said the item was missing
#define kWizardErrTranslator		(ERRBASE_WIZARD - 1)
#define kWizardErrNoTranslator		(ERRBASE_WIZARD - 6)


// NEWTON_TRACE_BEAM: what the beamer does, and how each step went
static bool
TracingBeam(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_BEAM") != nil;
	return tracing != 0;
}

#define BEAMTRACE(...)	do { if (TracingBeam()) { fprintf(stderr, "[beam] " __VA_ARGS__); fflush(stderr); } } while (0)


// The error an exception the beamer caught stands for: a pipe exception's
// data, anything else kIRErrGeneric.
static NewtonErr
BeamException(Exception* exception)
{
	if (Subexception(exception->name, exPipeException))
		return (NewtonErr) (Long) exception->data;
	BEAMTRACE("exception %s\n", exception->name);
	return kIRErrGeneric;
}


// ROM 0x0003db74 __ct__7TBeamerFRC6RefVar
// Over the transport frame; the callback set up for sending.  (fIndex and
// fCount the ROM leaves as they were: both are set before they are read.)
TBeamer::TBeamer(RefArg transport)
	: fTransport(transport),
	  fProgress(NILREF)
{
	fCallback.fReceiving = true;
	fCallback.fBeamer = nil;
	fEndpoint = nil;
	fPipe = nil;
	fWizardIn = nil;
	fWizardOut = nil;
	fProtocol = 0;
	fProtocolOptions = 0;
	fService = 0;
	fIndex = 0;
	fCount = 0;
}


// ROM 0x0003b6f0 __dt__7TBeamerFv
TBeamer::~TBeamer()
{ }


// ROM 0x0003b734 Open__7TBeamerFUc
// The status "waiting"; the endpoint opened and connected (or listening and
// accepted) over the protocol chosen - by the probe unless the preference
// zapCommToolId names a service; then the pipe.
NewtonErr
TBeamer::Open(Boolean receiving)
{
	fCallback.fBeamer = this;
	fCallback.fReceiving = receiving;
	NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(receiving ? RSSYMrecvwaiting : RSSYMsendwaiting), RefVar(NILREF));
	NewtonErr err;
	if (GetPreference(RefVar(RSSYMzapcommtoolid)) == NILREF)
	{
		err = OpenEndpoint('pkir', receiving);
		if (err != noErr)
			return err;
		err = IdentifyProtocol();
		if (err != noErr)
			return err;
		CloseEndpoint();
		err = OpenEndpoint((fProtocol & irUsingIrDA) ? 'irda' : 'slir', receiving);
		if (err != noErr)
			return err;
		err = IdentifyProtocol();
	}
	else
	{
		RefVar tool(GetPreference(RefVar(RSSYMzapcommtoolid)));
		char name[4];
		ConvertUnicodeCharacters(GetCString(tool), name, kMacRomanEncoding, 4);
		// (the four characters as the ROM reads them: a big-endian word)
		ULong service = ((ULong) (UByte) name[0] << 24) | ((ULong) (UByte) name[1] << 16)
					  | ((ULong) (UByte) name[2] << 8) | (UByte) name[3];
		fProtocol = 0;
		err = OpenEndpoint(service, receiving);
		if (err != noErr)
			return err;
		err = IdentifyProtocol();
	}
	if (err != noErr)
		return err;
	err = OpenPipe(receiving);
	BEAMTRACE("open: service %.4s, protocol %lu options %lu, pipe %ld\n", (char*) &fService, (unsigned long) fProtocol, (unsigned long) fProtocolOptions, (long) err);
	return err;
}


// ROM 0x0003b8d4 Close__7TBeamerFv
NewtonErr
TBeamer::Close(void)
{
	NewtonErr err = ClosePipe();
	if (err != noErr)
		return err;
	return CloseEndpoint();
}


// ROM 0x0003b8fc SendSetup__7TBeamerFv
// With the Senior protocol or IrDA the number of items goes first.
NewtonErr
TBeamer::SendSetup(void)
{
	volatile NewtonErr err = noErr;
	if ((fProtocol & irUsingSeniorIR) || (fProtocol & irUsingIrDA))
	{
		RefVar count(MAKEINT(fCount));
		TObjectWriter writer(count, *fPipe, false);
		newton_try
		{
			writer.Write();
			fPipe->FlushWrite();
		}
		newton_catch_all
		{
			err = BeamException(CurrentException());
		}
		end_try;
	}
	return err;
}


// ROM 0x0003b9e0 Send__7TBeamerFRC6RefVar
// Each of the request's items the transport gives (BeamNextItem - in 1.x
// form, 'oneO, to a Newton that speaks protocol 2), sent and committed
// (BeamCommitSend).  An item that fails ends the send, the transport having
// been told: the answer is then no error.
NewtonErr
TBeamer::Send(RefArg request)
{
	fCount = RINT(NSSend(fTransport, RefVar(RSSYMitemcount), request));
	SetFrameSlot(fTransport, RSSYMbeamtotal, RefVar(MAKEINT(fCount)));
	RefVar form(NILREF);
	RefVar item(NILREF);
	volatile NewtonErr err = SendSetup();
	BEAMTRACE("Send: %ld items, setup %ld\n", fCount, (long) err);
	if (err != noErr)
		return err;
	if (fProtocol & irUsingNewtIR)
		form = RSSYMoneo;
	for (fIndex = 1; fIndex <= fCount; fIndex++)
	{
		newton_try
		{
			item = NSSend(fTransport, RefVar(RSSYMbeamnextitem), request, form);
		}
		newton_catch_all
		{
			err = kBeamErrNoItem;
		}
		end_try;
		if (EQRef(item, RSSYMitemmissing))
			return kBeamErrItemMissing;
		if (ISNIL(item))
			return kBeamErrNoItem;
		if (err != noErr)
			continue;
		SetFrameSlot(fTransport, RSSYMbeamindex, RefVar(MAKEINT(fIndex)));
		err = (fProtocol & irUsingSharpIR) ? SendWizard(item) : SendNewton(item);
		NSSend(fTransport, RefVar(RSSYMbeamcommitsend), item, RefVar(err == noErr ? NILREF : MAKEINT(err)));
		if (err != noErr)
			return noErr;
	}
	return err;
}


// ROM 0x0003bc30 ReceiveSetup__7TBeamerFv
// With the Senior protocol or IrDA the number of items comes first; else one.
NewtonErr
TBeamer::ReceiveSetup(void)
{
	volatile NewtonErr err = noErr;
	if ((fProtocol & irUsingSeniorIR) || (fProtocol & irUsingIrDA))
	{
		TObjectReader reader(*fPipe);
		newton_try
		{
			fCount = RINT(reader.Read());
		}
		newton_catch_all
		{
			err = BeamException(CurrentException());
		}
		end_try;
	}
	else
		fCount = 1;
	return err;
}


// ROM 0x0003bd18 Receive__7TBeamerFv
NewtonErr
TBeamer::Receive(void)
{
	NewtonErr err = ReceiveSetup();
	BEAMTRACE("Receive: %ld items, setup %ld\n", fCount, (long) err);
	if (err != noErr)
		return err;
	SetFrameSlot(fTransport, RSSYMbeamtotal, RefVar(MAKEINT(fCount)));
	for (fIndex = 1; fIndex <= fCount; fIndex++)
	{
		SetFrameSlot(fTransport, RSSYMbeamindex, RefVar(MAKEINT(fIndex)));
		err = (fProtocol & irUsingSharpIR) ? ReceiveWizard() : ReceiveNewton();
		if (err != noErr)
			break;
	}
	return err;
}


// ROM 0x0003bdf4 OpenEndpoint__7TBeamerFUlUc
// In a fork of the world (the endpoint's calls are synchronous): an
// endpoint for the service - Sharp IR told the probe may already have
// agreed the protocol, IrDA given the beamer's discovery, its class name
// ("BMW" both ends), one 1K receive buffer and a 20-second link timeout -,
// kept in the transport's 'ep slot (so BeamCancel can abort it), opened,
// bound and connected or listening and accepted.
NewtonErr
TBeamer::OpenEndpoint(ULong service, Boolean receiving)
{
	fService = service;
	NewtonErr err = ((TForkWorld*) GetGlobals())->Fork(nil);
	BEAMTRACE("OpenEndpoint %c%c%c%c (%s): fork %ld\n", (char) (service >> 24), (char) (service >> 16), (char) (service >> 8), (char) service, receiving ? "receiving" : "sending", (long) err);
	if (err != noErr)
		return err;
	{
		TOptionArray options;
		err = options.Init();
		if (err != noErr)
			return err;
		{
			TOption serviceOption(kOptionType);
			serviceOption.SetAsService(service);
			err = options.InsertOptionAt(options.GetArrayCount(), &serviceOption);
			if (err != noErr)
				return err;
		}
		if (service == 'slir')
		{
			TCMOSlowIRConnect connect;
			TCMOSlowIRProtocolType protocolType;
			connect.connectOptions = irNegotiatedConnection;
			err = options.InsertOptionAt(options.GetArrayCount(), &connect);
			if (err != noErr)
				return err;
			protocolType.protocol = fProtocol;
			protocolType.options = fProtocolOptions;
			err = options.InsertOptionAt(options.GetArrayCount(), &protocolType);
			if (err != noErr)
				return err;
		}
		else if (service == 'irda')
		{
			TCMOIrDADiscovery discovery;
			TCMOIrDAConnectionInfo info;
			TCMOIrDAReceiveBuffers buffers;
			TCMOIrDALinkDisconnect disconnect;
			discovery.fProbeSlots = 1;
			discovery.fMediaBusyCheck = 0;
			err = options.InsertOptionAt(options.GetArrayCount(), &discovery);
			if (err != noErr)
				return err;
			info.fMyLSAPId = 0;
			info.fMyNameLength = 3;
			info.fPeerLSAPId = 0;
			info.fPeerNameLength = 3;
			memcpy(info.fClassNames, "BMW\0BMW\0", 8);
			err = options.InsertOptionAt(options.GetArrayCount(), &info);
			if (err != noErr)
				return err;
			buffers.fSize = 0x400;
			buffers.fCount = 1;
			err = options.InsertOptionAt(options.GetArrayCount(), &buffers);
			if (err != noErr)
				return err;
			disconnect.fTimeout = 20;
			err = options.InsertOptionAt(options.GetArrayCount(), &disconnect);
			if (err != noErr)
				return err;
		}
		err = CMGetEndpoint(&options, &fEndpoint, true);
		if (err != noErr)
			return err;
	}
	SetFrameSlot(fTransport, RSSYMep, RefVar(AddressToRef(fEndpoint)));
	err = fEndpoint->Open(0);
	if (err == noErr)
	{
		err = fEndpoint->nBind(nil, kNoTimeout, true);
		if (err == noErr)
		{
			if (receiving)
			{
				err = fEndpoint->nListen(nil, nil, nil, kNoTimeout, true);
				if (err == noErr)
					err = fEndpoint->nAccept(fEndpoint, nil, nil, 0, kNoTimeout, true);
			}
			else
				err = fEndpoint->nConnect(nil, nil, nil, kNoTimeout, true);
			BEAMTRACE("OpenEndpoint: %s %ld\n", receiving ? "listen/accept" : "connect", (long) err);
			if (err == noErr)
				return noErr;
			fEndpoint->nUnBind(kNoTimeout, true);
		}
		fEndpoint->Close();
	}
	fEndpoint->Delete();
	fEndpoint = nil;
	RemoveSlot(fTransport, RSSYMep);
	return err;
}


// ROM 0x0003c154 OpenPipe__7TBeamerFUc
// To a Sharp Wizard, its frame translators; else a pipe over the endpoint,
// framed for Sharp IR (a frame's end is what starts both ends' packet
// numbers again).
NewtonErr
TBeamer::OpenPipe(Boolean receiving)
{
	volatile NewtonErr err = noErr;
	if (fProtocol & irUsingSharpIR)
	{
		if (receiving)
		{
			fWizardIn = (PFrameSource*) NewByName("PFrameSource", "PWizard9In");
			if (fWizardIn == nil)
				err = kWizardErrNoTranslator;
		}
		else
		{
			fWizardOut = (PFrameSink*) NewByName("PFrameSink", "PWizard9Out");
			if (fWizardOut == nil)
				err = kWizardErrNoTranslator;
		}
		return err;
	}
	TEndpointPipe* pipe = new TEndpointPipe;
	fPipe = pipe;
	if (pipe == nil)
		return MemError();
	newton_try
	{
		pipe->Init(fEndpoint, 0x200, 0x200, 0, fService == 'slir', &fCallback);
	}
	newton_catch(exPipeException)
	{
		err = (NewtonErr) (Long) CurrentException()->data;
	}
	end_try;
	return err;
}


// ROM 0x0003c2bc IdentifyProtocol__7TBeamerFv
// What the tool agreed: its 'irpt' option (the protocol and its options).
NewtonErr
TBeamer::IdentifyProtocol(void)
{
	TOptionArray options;
	TCMOSlowIRProtocolType protocolType;
	NewtonErr err = options.Init();
	if (err != noErr)
		return err;
	protocolType.SetOpCode(opGetCurrent);
	err = options.InsertOptionAt(options.GetArrayCount(), &protocolType);
	if (err != noErr)
		return err;
	err = fEndpoint->nOptMgmt(opProcess, &options, kNoTimeout, true);
	if (err != noErr)
		return err;
	TCMOSlowIRProtocolType* agreed = (TCMOSlowIRProtocolType*) options.OptionAt(0);
	if (agreed == nil)
		return -1;
	err = (NewtonErr) (signed char) agreed->GetOpCodeResults();
	if (err == noErr)
	{
		fProtocol = agreed->protocol;
		fProtocolOptions = agreed->options;
	}
	return err;
}


// ROM 0x0003c3a8 UpdateProgress__7TBeamerFli
// The status dialog's gauge moved on, while an item moves.
void
TBeamer::UpdateProgress(long percent, Boolean receiving)
{
	if (ISNIL(fProgress))
		return;
	if (percent > 0)
		SetFrameSlot(fProgress, RSSYMgauge, RefVar(MAKEINT(percent)));
	NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(receiving ? RSSYMreceiving : RSSYMsending), fProgress);
}


// ROM 0x0003c434 SendNewton__7TBeamerFRC6RefVar
// One item to a Newton: (protocol 2: each item its own connection) a header
// frame - appSymbol, title, the owner's name, the item's size as NSOF, its
// type - then, if the other side answers that it will take it, the item
// itself with the gauge moving.  Refused: kIRErrCancel.
NewtonErr
TBeamer::SendNewton(RefArg item)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		RefVar title(GetFrameSlot(item, RSSYMtitle));
		if ((fProtocol & irUsingNewtIR) && fIndex > 1)
		{
			NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(RSSYMsendwaiting), RefVar(NILREF));
			err = fEndpoint->Connect(nil, nil, nil, nil, kNoTimeout);
			if (err != noErr)
				Throw(exPipeException, (void*) (Long) err, nil);
		}
		NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(RSSYMconfirming), RefVar(NILREF));
		long size;
		{
			TObjectWriter sizer(item, *fPipe, false);
			newton_try
			{
				sizer.SetCompressLargeBinaries();
				size = sizer.Size();
			}
			cleanup
			{
				sizer.~TObjectWriter();
			}
			end_try;
		}
		RefVar header(AllocateFrame());
		SetFrameSlot(header, RSSYMappsymbol, RefVar(GetFrameSlot(item, RSSYMappsymbol)));
		SetFrameSlot(header, RSSYMtitle, title);
		SetFrameSlot(header, RSSYMname, RefVar(GetPreference(RefVar(RSSYMname))));
		SetFrameSlot(header, RSSYMsize, RefVar(MAKEINT(size)));
		RefVar type(GetFrameSlot(item, RSSYMtype));
		if (ISNIL(type))
			type = GetFrameSlot(item, RSSYMcurrentformat);
		SetFrameSlot(header, RSSYMtype, type);
		{
			TObjectWriter writer(header, *fPipe, false);
			newton_try
			{
				writer.Write();
			}
			cleanup
			{
				writer.~TObjectWriter();
			}
			end_try;
		}
		fPipe->FlushWrite();
		Boolean accepted;
		{
			TObjectReader reader(*fPipe);
			newton_try
			{
				RefVar answer(reader.Read());
				accepted = (RINT(answer) != 0);
			}
			cleanup
			{
				reader.~TObjectReader();
			}
			end_try;
		}
		if (accepted)
		{
			fCallback.fWriteTotal = size;
			fProgress = AllocateFrame();
			SetFrameSlot(fProgress, RSSYMgauge, RefVar(MAKEINT(0)));
			SetFrameSlot(fProgress, RSSYMtitletext, title);
			fPipe->ResetWrite();
			NSSend(fTransport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMsending), RefVar(RSSYMvgauge), fProgress);
			{
				TObjectWriter writer(item, *fPipe, false);
				newton_try
				{
					writer.SetCompressLargeBinaries();
					writer.Write();
				}
				cleanup
				{
					writer.~TObjectWriter();
				}
				end_try;
			}
			fPipe->FlushWrite();
			NSSend(fTransport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMsending), RefVar(RSSYMvstatus), RefVar(NILREF));
			fProgress = NILREF;
		}
		else
			err = kIRErrCancel;
		if ((fProtocol & irUsingNewtIR) && fIndex < fCount)
		{
			err = fEndpoint->Disconnect(nil, 0, 0);
			if (err != noErr)
				Throw(exPipeException, (void*) (Long) err, nil);
		}
	}
	newton_catch_all
	{
		err = BeamException(CurrentException());
	}
	end_try;
	return err;
}


// the Wizard translators' context: the endpoint (out) or pipe (in), and the
// item
struct WizardOutParms
{
	TEndpoint*	fEndpoint;		// +0x00
	RefVar		fItem;			// +0x04
};

struct WizardInParms
{
	RefVar		fItem;			// +0x00
	CPipe*		fPipe;			// +0x04
};


// The error a Wizard translator's exception stands for: a pipe's or a
// translator's data, anything else kWizardErrTranslator.
static NewtonErr
WizardException(Exception* exception)
{
	if (Subexception(exception->name, exPipeException) || Subexception(exception->name, exTranslatorException))
		return (NewtonErr) (Long) exception->data;
	return kWizardErrTranslator;
}


// ROM 0x0003ca34 SendWizard__7TBeamerFRC6RefVar
// One item to a Sharp Wizard, through its translator.
NewtonErr
TBeamer::SendWizard(RefArg item)
{
	volatile NewtonErr err = noErr;
	newton_try
	{
		NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(RSSYMsending), RefVar(NILREF));
		WizardOutParms parms;
		parms.fEndpoint = fEndpoint;
		parms.fItem = item;
		err = (NewtonErr) (Long) fWizardOut->Translate(&parms, nil);
	}
	newton_catch_all
	{
		err = WizardException(CurrentException());
	}
	end_try;
	return err;
}


// ROM 0x0003cb4c ReceiveNewton__7TBeamerFv
// One item from a Newton: its header (the transport told, BeamStartRecv,
// when there is room for it on the default store - or when it says no
// size), the answer (1: send it; 0: no room, kSError_StoreFull), and the
// item read onto that store with the gauge moving (or the barber pole,
// its size not known), then committed - BeamCommitRecv(item, 'oneO from a
// protocol 2 Newton, nil).
NewtonErr
TBeamer::ReceiveNewton(void)
{
	volatile NewtonErr err = noErr;
	RefVar form(NILREF);
	RefVar header(NILREF);
	if (fProtocol & irUsingNewtIR)
		form = RSSYMoneo;
	newton_try
	{
		RefVar store(NSSend(fTransport, RefVar(RSSYMgetdefaultownerstore)));
		Boolean room = true;
		long size = 0;
		NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(RSSYMconfirming), RefVar(NILREF));
		{
			TObjectReader reader(*fPipe);
			newton_try
			{
				header = reader.Read();
			}
			cleanup
			{
				reader.~TObjectReader();
			}
			end_try;
		}
		RefVar sizeRef(GetFrameSlot(header, RSSYMsize));
		if (ISNIL(sizeRef))
			NSSend(fTransport, RefVar(RSSYMbeamstartrecv), header);
		else
		{
			size = RINT(sizeRef);
			long total = RINT(NSSend(store, RefVar(RSSYMtotalsize)));
			long used = RINT(NSSend(store, RefVar(RSSYMusedsize)));
			if (total - used < size)
				room = false;
			if (room)
				NSSend(fTransport, RefVar(RSSYMbeamstartrecv), header);
		}
		{
			RefVar answer(MAKEINT(room ? 1 : 0));
			TObjectWriter writer(answer, *fPipe, false);
			newton_try
			{
				writer.Write();
			}
			cleanup
			{
				writer.~TObjectWriter();
			}
			end_try;
		}
		fPipe->FlushWrite();
		if (!room)
			err = kSError_StoreFull;
		else
		{
			fProgress = AllocateFrame();
			SetFrameSlot(fProgress, RSSYMtitletext, RefVar(GetFrameSlot(header, RSSYMtitle)));
			fPipe->ResetRead();
			if (size > 0)
			{
				fCallback.fReadTotal = size;
				SetFrameSlot(fProgress, RSSYMgauge, RefVar(MAKEINT(0)));
				NSSend(fTransport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMreceiving), RefVar(RSSYMvgauge), fProgress);
			}
			else
			{
				fCallback.fReadTotal = -1;
				SetFrameSlot(fProgress, RSSYMbarber, RefVar(MAKEINT(6)));
				NSSend(fTransport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMreceiving), RefVar(RSSYMvbarber), fProgress);
			}
			RefVar item(NILREF);
			{
				TObjectReader reader(*fPipe, store);
				newton_try
				{
					item = reader.Read();
				}
				cleanup
				{
					reader.~TObjectReader();
				}
				end_try;
			}
			NSSend(fTransport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMreceiving), RefVar(RSSYMvstatus), RefVar(NILREF));
			fProgress = NILREF;
			newton_try
			{
				NSSend(fTransport, RefVar(RSSYMbeamcommitrecv), item, form, RefVar(NILREF));
			}
			newton_catch_all
			{
				err = kBeamErrNoItem;
			}
			end_try;
		}
	}
	newton_catch_all
	{
		err = BeamException(CurrentException());
	}
	end_try;
	return err;
}


// ROM 0x0003d0ac ReceiveWizard__7TBeamerFv
// One item from a Sharp Wizard, through its translator over a pipe of its
// own; the transport told (WizardCommit).
NewtonErr
TBeamer::ReceiveWizard(void)
{
	volatile NewtonErr err = noErr;
	TEndpointPipe pipe;
	newton_try
	{
		pipe.Init(fEndpoint, 0x200, 0x200, 0, true, nil);
		WizardInParms parms;
		parms.fPipe = &pipe;
		NSSend(fTransport, RefVar(RSSYMsetstatus), RefVar(RSSYMreceiving), RefVar(NILREF));
		RefVar item(fWizardIn->Translate(&parms, nil));
		NSSend(fTransport, RefVar(RSSYMwizardcommit), item);
	}
	newton_catch_all
	{
		err = WizardException(CurrentException());
	}
	end_try;
	return err;
}


// ROM 0x0003d200 CloseEndpoint__7TBeamerFv
NewtonErr
TBeamer::CloseEndpoint(void)
{
	NewtonErr err = noErr;
	if (fEndpoint != nil)
	{
		RemoveSlot(fTransport, RSSYMep);
		fEndpoint->nDisconnect(nil, 0, 0, kNoTimeout, true);
		fEndpoint->nUnBind(kNoTimeout, true);
		err = fEndpoint->Close();
		fEndpoint->Delete();
		fEndpoint = nil;
	}
	return err;
}


// ROM 0x0003d284 ClosePipe__7TBeamerFv
NewtonErr
TBeamer::ClosePipe(void)
{
	if (fPipe != nil)
	{
		delete fPipe;
		fPipe = nil;
	}
	else if (fWizardOut != nil)
	{
		fWizardOut->Delete();
		fWizardOut = nil;
	}
	else if (fWizardIn != nil)
	{
		fWizardIn->Delete();
		fWizardIn = nil;
	}
	return noErr;
}


// ROM 0x0003daf0 Status__15TBeamerCallbackFlT1
// The percentage of the item moved (-1: not known).
Boolean
TBeamerCallback::Status(long bytesRead, long bytesWritten)
{
	long percent = -1;
	long total, done;
	if (fReceiving)
	{
		total = fReadTotal;
		done = bytesRead;
	}
	else
	{
		total = fWriteTotal;
		done = bytesWritten;
	}
	if (total > 0 && done >= 0)
		percent = (done * 100) / total;
	fBeamer->UpdateProgress(percent, fReceiving);
	return true;
}


/*------------------------------------------------------------------------------
	The Beam transport's natives
------------------------------------------------------------------------------*/

// ROM 0x0003d818 ZapSend
// BeamSend(transport, request): the items sent.  An endpoint that would not
// open fails the first item.  ==> nil, or the error (also handed to the
// transport's HandleError).
Ref
ZapSend(RefArg rcvr, RefArg transport, RefArg request)
{
	TBeamer beamer(transport);
	NewtonErr err = beamer.Open(false);
	BEAMTRACE("BeamSend: open %ld\n", (long) err);
	if (err != noErr)
	{
		RefVar item(NSSend(transport, RefVar(RSSYMbeamnextitem), request, RefVar(NILREF)));
		NSSend(transport, RefVar(RSSYMbeamcommitsend), item, RefVar(MAKEINT(err)));
	}
	if (err == noErr)
		err = beamer.Send(request);
	BEAMTRACE("BeamSend: %ld\n", (long) err);
	beamer.Close();
	NSSend(transport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMidle), RefVar(RSSYMvstatus), RefVar(NILREF));
	NSSend(transport, RefVar(RSSYMsetstatus), RefVar(RSSYMidle), RefVar(NILREF));
	if (err != noErr)
		NSSend(transport, RefVar(RSSYMhandleerror), RefVar(MAKEINT(err)));
	return err != noErr ? MAKEINT(err) : NILREF;
}


// ROM 0x0003d9b4 ZapReceive
// BeamReceive(transport): the items received.  ==> nil, or the error.
Ref
ZapReceive(RefArg rcvr, RefArg transport)
{
	TBeamer beamer(transport);
	NewtonErr err = beamer.Open(true);
	BEAMTRACE("BeamReceive: open %ld\n", (long) err);
	if (err == noErr)
		err = beamer.Receive();
	BEAMTRACE("BeamReceive: %ld\n", (long) err);
	beamer.Close();
	NSSend(transport, RefVar(RSSYMsetstatusdialog), RefVar(RSSYMidle), RefVar(RSSYMvstatus), RefVar(NILREF));
	NSSend(transport, RefVar(RSSYMsetstatus), RefVar(RSSYMidle), RefVar(NILREF));
	if (err != noErr)
		NSSend(transport, RefVar(RSSYMhandleerror), RefVar(MAKEINT(err)));
	return err != noErr ? MAKEINT(err) : NILREF;
}


// ROM 0x0003dac4 ZapCancel
// BeamCancel(ep): the beamer's endpoint (the transport's 'ep slot) aborted,
// which ends whatever it is waiting for.
Ref
ZapCancel(RefArg rcvr, RefArg endpoint)
{
	TEndpoint* ep = (TEndpoint*) RefToAddress(endpoint);
	if (ep != nil)
		ep->nAbort(true);
	return NILREF;
}


/*------------------------------------------------------------------------------
	R e c e i v i n g   b e a m s   a u t o m a t i c a l l y
------------------------------------------------------------------------------*/

TEndpoint*	gSnifferEndpoint = nil;			// ROM 0x0c1008d4 gSnifferEndpoint

// ROM 0x0003b5d8 SendSniffCommand__FUc
// Start: an endpoint on the IR sniffer ('snif') made and opened - which
// connects it, so it starts sniffing - unless there is one already.  Stop:
// the endpoint closed and freed, if there is one.
NewtonErr
SendSniffCommand(UChar start)
{
	NewtonErr err = noErr;
	if (!start)
	{
		if (gSnifferEndpoint != nil)
		{
			gSnifferEndpoint->EasyClose();
			gSnifferEndpoint->Delete();
			gSnifferEndpoint = nil;
		}
	}
	else if (gSnifferEndpoint == nil)
	{
		TOptionArray options;
		err = options.Init();
		if (err == noErr)
		{
			TOption service(kOptionType);
			service.SetAsService(kCMSSniffIR);
			err = options.InsertOptionAt(options.GetArrayCount(), &service);
			if (err != noErr)
				return err;
			err = CMGetEndpoint(&options, &gSnifferEndpoint, false);
			if (err == noErr)
				err = gSnifferEndpoint->EasyOpen(0);
		}
	}
	return err;
}


// ROM 0x0003b6d4 StartIRSniffing
// StartIRSniffing(): what the In/Out Box's preferences call when "Receive
// beams automatically" is turned on, and the Beam receive when it is done.
Ref
StartIRSniffing(RefArg rcvr)
{
	SendSniffCommand(true);
	return NILREF;
}


// ROM 0x0003c38c StopIRSniffing
Ref
StopIRSniffing(RefArg rcvr)
{
	SendSniffCommand(false);
	return NILREF;
}


void
RegisterBeamerNatives(void)
{
	RegisterNativeFunction("ZapSend", (void*) ZapSend, 2);
	RegisterNativeFunction("ZapReceive", (void*) ZapReceive, 1);
	RegisterNativeFunction("ZapCancel", (void*) ZapCancel, 1);
	RegisterNativeFunction("StartIRSniffing", (void*) StartIRSniffing, 0);
	RegisterNativeFunction("StopIRSniffing", (void*) StopIRSniffing, 0);
}
