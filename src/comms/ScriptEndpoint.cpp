/*
	File:		comms/ScriptEndpoint.cpp

	Contains:	TScriptEndpointClient and the CI... natives: protoEndpoint,
				the 1.x NewtonScript endpoint - ScriptEndpoint.h.

	Reconstructed from the MP2x00 US ROM (0x00067440-0x0006b5c0); each
	function cites its origin.
*/

#include "ScriptEndpoint.h"
#include "ModemOptions.h"
#include "ModemNavigator.h"
#include "HostOptionLayouts.h"
#include "CommManager.h"
#include "Pipes.h"
#include "ObjectStreamer.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "NewtonTime.h"
#include "SerialOptions.h"
#include "CommToolOptions.h"
#include "CommOptions.h"
#include "Ports.h"
#include "RootView.h"
#include "NewtWorld.h"
#include "Notebook.h"
#include "ConfigServer.h"
#include "AppWorld.h"
#include "toolbox/ByteOrder.h"

#include <new>
#include <string.h>

extern const ExceptionName exOutOfMemory;

#define OPTION_DATA_LENGTH(cls)	(sizeof(cls) - sizeof(TOption))

#define kOutputWindow				0x400			// bytes that may be outstanding
#define kCommScriptErrNoOptions		(-26004)		// Instantiate with no options and no configOptions

#define kCMOModemService			'mods'
#define kCMOModemProfile			'mpro'
#define kCMOAddress					'rout'
#define kCMOInputFlowControl		'iflc'
#define kCMOOutputFlowControl		'oflc'


// The ROM's malloc, operator new and operator delete are the pointer
// heap's; the client's blocks come from there on the host too, so a block
// one of them made is given back by the others (the options made in place).
static void*
CIMalloc(Size size)
{
	return NewPtr(size);
}

static void
CIFree(void* p)
{
	DisposPtr((Ptr) p);
}

template <class T>
static T*
NewOptionOf(void)
{
	void* block = NewPtr(sizeof(T));
	return block != nil ? new (block) T : nil;
}


// the client the frame keeps in its ciPrivate slot (GetClient 0x000ac358,
// system/ConfigServer.cpp)
static TScriptEndpointClient*
ScriptClient(RefArg endpoint)
{
	return (TScriptEndpointClient*) (void*) GetClient(endpoint);
}


// ROM 0x00069a04 (unnamed)
// A character as one byte in the encoding.
static UByte
CharToByte(UniChar c, long encoding)
{
	UniChar str[1];
	UByte bytes[4];
	str[0] = c;
	ConvertFromUnicode(str, bytes, encoding, 1);
	return bytes[0];
}


// DEVIATION (pointer size): an option's data as the MessagePad lays it out
// - a 'sid ' option as its two big-endian longs, a listed class through
// HostOptionLayouts.h - where the host's has wider fields
static const UByte*
OptionDeviceData(const TOption* option, UByte* buffer, long bufferSize, long* length)
{
	const UByte* data = (const UByte*) (option + 1);
	*length = ((TOption*) option)->Length();
	if (((TOption*) option)->Label() == kCMOServiceIdentifier && *length >= (long) (sizeof(TCMOServiceIdentifier) - sizeof(TOption)))
	{
		PutBigEndianWord(buffer, (unsigned int) ((const TCMOServiceIdentifier*) option)->fServiceId);
		PutBigEndianWord(buffer + 4, (unsigned int) ((const TCMOServiceIdentifier*) option)->fPortId);
		*length = 8;
		return buffer;
	}
	long deviceLength = HostOptionToDevice(option, buffer, bufferSize);
	if (deviceLength >= 0)
	{
		*length = deviceLength;
		return buffer;
	}
	return data;
}


// DEVIATION (pointer size): an option made from the MessagePad's bytes
// into the host's layout - a 'sid ' option's two big-endian longs into a
// TCMOServiceIdentifier, a listed class through HostOptionLayouts.h
static TOption*
OptionFromDeviceData(TOption* option)
{
	if (option->Label() == kCMOServiceIdentifier && option->Length() >= 8)
	{
		TCMOServiceIdentifier* sid = (TCMOServiceIdentifier*) NewPtrClear(sizeof(TCMOServiceIdentifier));
		if (sid != nil)
		{
			const UByte* bytes = (const UByte*) (option + 1);
			sid->SetLabel(kCMOServiceIdentifier);
			sid->SetAsService();
			sid->SetOpCode(option->GetOpCode());
			sid->SetLength(sizeof(TCMOServiceIdentifier) - sizeof(TOption));
			sid->fServiceId = GetBigEndianWord(bytes);
			sid->fPortId = GetBigEndianWord(bytes + 4);
		}
		DisposPtr((Ptr) option);
		return sid;
	}
	return HostOptionFromDevice(option);
}


/*------------------------------------------------------------------------------
	TScriptEndpointClient
------------------------------------------------------------------------------*/

// ROM 0x0006a688 __ct__21TScriptEndpointClientFv
TScriptEndpointClient::TScriptEndpointClient()
{
	fEndpointRef = NILREF;
	fOutputRoom = kOutputWindow;
	fInputSpec = NILREF;
	fInput = nil;
	fPartialFrequency = 0;
	fEndpoint = nil;
	fYielding = false;
	fEncoding = 1;
	fRecvFlags = 0;
	fRcvPending = false;
	fInRcvComplete = false;
	fInputEnd = 0;
	fByteCount = 0;
	fDiscardAfter = 0;
	fUseEndChar = false;
	fEndChar = 0;
	fFormString = fFormFrame = fFormRaw = false;
	fFrameNeedsLength = false;
	fInputSize = 0;
	fInputCount = 0;
	fPartialConsumed = 0;
	fPartialPending = false;
	fNullProxy = 0;
	fSevenBit = false;
}


// ROM 0x0006a918 __dt__21TScriptEndpointClientFv
TScriptEndpointClient::~TScriptEndpointClient()
{
	if (fEndpoint != nil)
		fEndpoint->Delete();
	if (fInput != nil)
		CIFree(fInput);
	// ROM BUG: the frame is tested against 0 rather than nil, so a client
	// whose frame is nil would throw here
	if ((Ref) fEndpointRef != 0)
		SetFrameSlot(fEndpointRef, RSSYMciprivate, RefVar(NILREF));
}


// ROM 0x0006abdc InitScriptEndpointClient__21TScriptEndpointClientFRC6RefVarT1P9TEndpoint
// The endpoint made from the options (the endpoint's configOptions when
// there are none; the modem navigator first when they name the modem) or
// the one given, the client registered for its events and kept in the
// frame's ciPrivate, and the endpoint opened - or, given, made
// asynchronous with the frame's nextInputSpec in place.
NewtonErr
TScriptEndpointClient::InitScriptEndpointClient(RefArg endpoint, RefArg options, TEndpoint* ep)
{
	fEndpointRef = endpoint;
	if (ISNIL(endpoint))
		ThrowMsg("nil endpoint passed to InitScriptEndpointClient");
	NewtonErr err = fYieldState.Init();
	if (err == noErr)
		err = fRcvBuffer.Init(0x200);
	Boolean made = (ep == nil);
	if (made && err == noErr)
	{
		TOptionArray array;
		err = array.Init();
		if (err != noErr)
			ThrowMsg("new TOptionArray failed");
		if (ISNIL(options))
		{
			RefVar config(GetVariable(endpoint, RSSYMconfigoptions, nil, 0));
			if (ISNIL(config))
				err = kCommScriptErrNoOptions;
			else
				err = ConvertToOptionArray(config, &array);
		}
		else
			err = ConvertToOptionArray(options, &array);
		if (err == noErr && ContainsModemService(&array))
			err = RunModemNavigator(&array);
		if (err == noErr)
		{
			err = CMGetEndpoint(&array, &ep, false);
			if (err != noErr)
				ThrowMsg("new TEndpoint failed");
		}
	}
	if (err == noErr)
		err = TEndpointClient::Init(ep, 'endp', 'newt');
	if (err == noErr)
	{
		SetFrameSlot(endpoint, RSSYMciprivate, RefVar(AddressToRef(this)));
		RefVar encoding(GetVariable(endpoint, RSSYMencoding, nil, 0));
		if (NOTNIL(encoding))
			fEncoding = RINT(encoding);
		err = InitIdler(0, kMilliseconds, 0, false);
	}
	if (err == noErr)
	{
		if (made)
			err = fEndpoint->Open((ULong) this);
		else
		{
			fEndpoint->SetSync(false);
			RefVar spec(GetVariable(fEndpointRef, RSSYMnextinputspec, nil, 0));
			SetInputSpec(spec);
		}
	}
	return err;
}


/*------------------------------------------------------------------------------
	Events
------------------------------------------------------------------------------*/

// ROM 0x00067440 AEHandlerProc__21TScriptEndpointClientFP10TUMsgTokenPUlP7TAEvent
// An event handled in the global port and the screen brought up to date; a
// NewtonScript exception out of it goes to the endpoint's exceptionHandler
// ({name, data}) or is reported.
void
TScriptEndpointClient::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	GrafPtr savedPort;
	GetPort(&savedPort);
	SetPort(&gGrafPort);
	newton_try
	{
		TEndpointClient::AEHandlerProc(token, size, event);
		gRootView->Update(nil);
	}
	newton_catch("evt.ex")
	{
		Exception* exception = CurrentException();
		if (ISNIL(fEndpointRef) || !FrameHasSlot(fEndpointRef, RSSYMexceptionhandler))
			ExceptionNotify(exception);
		else
		{
			RefVar info(AllocateFrame());
			SetFrameSlot(info, RSSYMname, RefVar(Intern((char*) exception->name)));
			// ROM QUIRK: the data is made an integer whatever it is
			SetFrameSlot(info, RSSYMdata, RefVar(MAKEINT((Long) exception->data)));
			RefVar args(MakeArray(1));
			SetArraySlotRef(args, 0, info);
			DoMessage(fEndpointRef, RSSYMexceptionhandler, args);
		}
	}
	end_try;
	SetPort(savedPort);
}


// ROM 0x0006b1bc DoException__21TScriptEndpointClientFl
// An error to the endpoint's exceptionHandler ({name: 'evt.ex.comm, data})
// or thrown.
void
TScriptEndpointClient::DoException(long error)
{
	if (NOTNIL(fEndpointRef) && GetVariable(fEndpointRef, RSSYMexceptionhandler, nil, 0) != NILREF)
	{
		RefVar info(AllocateFrame());
		SetFrameSlot(info, RSSYMname, RefVar(Intern((char*) "evt.ex.comm")));
		SetFrameSlot(info, RSSYMdata, RefVar(MAKEINT(error)));
		RefVar args(MakeArray(1));
		SetArraySlotRef(args, 0, info);
		DoMessage(fEndpointRef, RSSYMexceptionhandler, args);
		return;
	}
	Throw((ExceptionName) "evt.ex.comm", (void*) (Long) error, nil);
}


// ROM 0x0006b47c Default__21TScriptEndpointClientFP14TEndpointEvent
// Any other event to endpoint:eventHandler(eventCode, data).
void
TScriptEndpointClient::Default(TEndpointEvent* event)
{
	if (ISNIL(fEndpointRef) || GetVariable(fEndpointRef, RSSYMeventhandler, nil, 0) == NILREF)
		return;
	RefVar args(MakeArray(2));
	SetArraySlotRef(args, 0, RefVar(MAKEINT(event->fEventCode)));
	SetArraySlotRef(args, 1, RefVar(MAKEINT(event->fReserved)));
	DoMessage(fEndpointRef, RSSYMeventhandler, args);
}


// ROM 0x000693f4 SndComplete__21TScriptEndpointClientFP14TEndpointEvent
// The data sent given back (a segment deleted, bytes freed and their room
// counted back), a Yield waiting for it ended, an error reported.
void
TScriptEndpointClient::SndComplete(TEndpointEvent* event)
{
	TSndCompleteEvent* sent = (TSndCompleteEvent*) event;
	if (sent->fData != nil)
		delete sent->fData;
	else if (sent->fBuffer != nil)
	{
		CIFree(sent->fBuffer);
		fOutputRoom += sent->fCount;
	}
	StopYielding();
	if (event->fError != noErr)
		DoException(event->fError);
}


// ROM 0x0006a1b8 OptMgmtComplete__21TScriptEndpointClientFP14TEndpointEvent
void
TScriptEndpointClient::OptMgmtComplete(TEndpointEvent* event)
{
	StopYielding();
	if (event->fError != noErr)
		DoException(event->fError);
}


// ROM 0x00069f2c RcvComplete__21TScriptEndpointClientFP14TEndpointEvent
// What came moved into the input buffer a byte at a time (the zero byte's
// proxy put in, the eighth bit cleared if asked), a 'frame's length word
// taken off when it has come, the oldest 0x40 bytes thrown away when the
// buffer is full; an input complete (the byte count reached, the end
// character, the end of a packet) is posted; then the next receive.
void
TScriptEndpointClient::RcvComplete(TEndpointEvent* event)
{
	fRcvPending = false;
	if (event->fError == noErr)
	{
		fInRcvComplete = true;
		if (fInputEnd == 0)
			fPartialPending = true;
		int c;
		while ((c = fRcvBuffer.Get()) != -1)
		{
			if (c == 0 && fNullProxy != 0)
				c = fNullProxy;
			if (fSevenBit)
				c &= 0x7f;
			fInput[fInputCount++] = c;
			if (fFormFrame && fFrameNeedsLength && (ULong) fInputCount > 3)
			{
				// DEVIATION: the length word is big-endian on the wire,
				// which the ROM reads as it lies
				fByteCount = (long) GetBigEndianWord(fInput);
				fInputCount -= 4;
				BlockMove(fInput + 4, fInput, fInputCount);
				fFrameNeedsLength = false;
				Size size = fByteCount + 0x40;
				UByte* input = (UByte*) ReallocPtr((Ptr) fInput, size);
				if (MemError() != noErr)
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				fInputSize = size;
				fInput = input;
			}
			if (fInputCount == fInputSize)
			{
				BlockMove(fInput + 0x40, fInput, fInputSize - 0x40);
				fInputCount -= 0x40;
				if (fPartialConsumed > 0 && (fPartialConsumed -= 0x40) < 0)
					fPartialConsumed = 0;
				if (fInputEnd > 0 && (fInputEnd -= 0x40) < 0)
					fInputEnd = 0;
			}
			if ((fByteCount > 0 && fInputCount == fByteCount)
			||  (fUseEndChar && fEndChar == (UByte) c)
			||  ((fRecvFlags & 2) != 0 && (((TRcvCompleteEvent*) event)->fFlags & 1) == 0 && fRcvBuffer.Peek() == -1))
			{
				fInputEnd = fInputCount;
				PostInput();
			}
		}
		fInRcvComplete = false;
	}
	else
		DoException(event->fError);
	if (fInputEnd == 0 && NOTNIL(fInputSpec))
	{
		ULong flags = fRecvFlags;
		fRcvBuffer.Reset();
		if (fEndpoint->Rcv(&fRcvBuffer, 1, &flags, 0) == noErr)
			fRcvPending = true;
	}
}


// ROM 0x0006a1e8 IdleProc__21TScriptEndpointClientFP10TUMsgTokenPUlP7TAEvent
// The input so far to the input spec's partialScript(endpoint, data), and
// the idler set again while no input is complete.
void
TScriptEndpointClient::IdleProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	if (fPartialPending && NOTNIL(fInputSpec))
	{
		RefVar script(GetVariable(fInputSpec, RSSYMpartialscript, nil, 0));
		if (NOTNIL(script))
		{
			RefVar data(NILREF);
			if (fInputCount > 0)
			{
				data = ConvertBlock(fInput, fInputCount);
				fPartialConsumed = fInputCount;
				fPartialPending = false;
			}
			if (NOTNIL(data))
			{
				RefVar args(MakeArray(2));
				SetArraySlotRef(args, 0, fEndpointRef);
				SetArraySlotRef(args, 1, data);
				DoMessage(fInputSpec, RSSYMpartialscript, args);
				CheckForDeferredActions();
			}
		}
		fPartialPending = false;
	}
	if (fInputEnd == 0 && fPartialFrequency > 0)
		ResetIdle(fPartialFrequency, kMilliseconds);
}


// ROM 0x0006a330 Yield__21TScriptEndpointClientFv
// Waiting (the world forked meanwhile) until StopYielding.
void
TScriptEndpointClient::Yield(void)
{
	fYielding = true;
	fYieldState.Block(0);
	fYielding = false;
}


// ROM 0x0006a360 StopYielding__21TScriptEndpointClientFv
// (TPseudoSyncState::Unblock written out)
void
TScriptEndpointClient::StopYielding(void)
{
	if (!fYielding)
		return;
	TUnblockEvent unblock(&fYieldState);
	TAppWorld* world = (TAppWorld*) GetGlobals();
	world->ReleaseMutex();
	fYieldState.Send(&unblock, sizeof(unblock));
	world->AcquireMutex();
}


// ROM 0x0006a4a8 TranslateError__21TScriptEndpointClientFl
// The two "not connected" errors of the tool as one.
long
TScriptEndpointClient::TranslateError(long error)
{
	if (error == -16013 || error == -16005)
		error = -10039;
	return error;
}


/*------------------------------------------------------------------------------
	Options
------------------------------------------------------------------------------*/

// ROM 0x000675f4 DoSetOptions__21TScriptEndpointClientFRC6RefVar
// The options set (a Yield for an asynchronous endpoint's completion).
NewtonErr
TScriptEndpointClient::DoSetOptions(RefArg options)
{
	TOptionArray array;
	NewtonErr err = array.Init();
	if (err == noErr)
		err = ConvertToOptionArray(options, &array);
	if (err == noErr)
		err = fEndpoint->OptMgmt(opProcess, &array, 0);
	if (err == noErr && !fEndpoint->IsSync())
		Yield();
	return err;
}


// ROM 0x00067684 DoGetOptions__21TScriptEndpointClientFRC6RefVar
// Each option processed by its own opCode, and the array read back.
Ref
TScriptEndpointClient::DoGetOptions(RefArg options)
{
	RefVar result(NILREF);
	TOptionArray array;
	NewtonErr err = array.Init();
	if (err == noErr)
		err = ConvertToOptionArray(options, &array);
	if (err == noErr)
		err = fEndpoint->OptMgmt(opProcess, &array, 0);
	if (err == noErr)
	{
		if (!fEndpoint->IsSync())
			Yield();
		result = ConvertFromOptionArray(&array);
	}
	return result;
}


// ROM 0x00067738 DoGetOption__21TScriptEndpointClientFRC6RefVar
Ref
TScriptEndpointClient::DoGetOption(RefArg option)
{
	return NILREF;
}


// ROM 0x00067740 ConvertFromOptionArray__21TScriptEndpointClientFP12TOptionArray
Ref
TScriptEndpointClient::ConvertFromOptionArray(TOptionArray* array)
{
	RefVar frames(NILREF);
	if (array != nil)
	{
		TOptionIterator iter(array);
		frames = MakeArray(array->GetArrayCount());
		TOption* option = iter.FirstOption();
		while (iter.More())
		{
			ArrayIndex slot = iter.CurrentIndex();
			SetArraySlotRef(frames, slot, RefVar(ConvertFromOption(option)));
			option = iter.NextOption();
		}
	}
	return frames;
}


// ROM 0x00067818 ConvertToOptionArray__21TScriptEndpointClientFRC6RefVarP12TOptionArray
// An option frame, or an array of them (nested arrays too), appended; a
// frame that is not an option is passed over.
NewtonErr
TScriptEndpointClient::ConvertToOptionArray(RefArg options, TOptionArray* array)
{
	NewtonErr err = noErr;
	if (!IsArray(options))
	{
		TOption* option = ConvertToOption(options);
		if (option != nil)
		{
			err = array->InsertOptionAt(array->GetArrayCount(), option);
			CIFree(option);
		}
	}
	else
	{
		ArrayIndex count = Length(options);
		for (ArrayIndex i = 0; i < count; i++)
		{
			RefVar element(GetArraySlotRef(options, i));
			if ((err = ConvertToOptionArray(element, array)) != noErr)
				return err;
			err = noErr;
		}
	}
	return err;
}


// ROM 0x000678ec ConvertToAddressParms__21TScriptEndpointClientFRC6RefVar
// A 'rout address from {addressType, addressData}: a phone number (3,
// the characters) or an AppleTalk name (1, 'sltk and the UniChars).
// DEVIATION: made in the MessagePad's bytes, big-endian words, then into
// the host's layout (no host tool reads one yet).
TOption*
TScriptEndpointClient::ConvertToAddressParms(RefArg option)
{
	TOption* address = nil;
	RefVar data(GetFrameSlotRef(option, RSSYMdata));
	if (NOTNIL(data))
	{
		RefVar type(GetFrameSlotRef(data, RSSYMaddresstype));
		RefVar string(GetFrameSlotRef(data, RSSYMaddressdata));
		if (ISINT(type) && NOTNIL(string))
		{
			long addressType = RINT(type);
			long length = Ustrlen(GetCString(string));
			if (addressType == 3)
			{
				address = (TOption*) NewPtrClear(sizeof(TOption) + length + 9);
				address->SetAsAddress(kCMOAddress);
				address->SetOpCode(opSetNegotiate);
				address->SetLength(length + 8);
				UByte* bytes = (UByte*) (address + 1);
				PutBigEndianWord(bytes, 3);
				PutBigEndianWord(bytes + 4, length);
				ConvertFromUnicode(GetCString(string), bytes + 8, 1, length);
			}
			else if (addressType == 1)
			{
				length *= 2;
				address = (TOption*) NewPtrClear(sizeof(TOption) + length + 0x12);
				address->SetAsAddress(kCMOAddress);
				address->SetOpCode(opSetNegotiate);
				address->SetLength(length + 0x12);
				UByte* bytes = (UByte*) (address + 1);
				PutBigEndianWord(bytes, 1);
				PutBigEndianWord(bytes + 4, 1);
				PutBigEndianWord(bytes + 8, 'sltk');
				PutBigEndianWord(bytes + 12, length + 2);
				// DEVIATION: the UniChars big-endian, as the MessagePad's
				const UniChar* s = GetCString(string);
				for (long i = 0; i <= length / 2; i++)
				{
					bytes[16 + i * 2] = s[i] >> 8;
					bytes[16 + i * 2 + 1] = s[i];
				}
			}
			if (address != nil)
				address = OptionFromDeviceData(address);
		}
	}
	return address;
}


// ROM 0x00067ab4 ConvertToSerialIOParms__21TScriptEndpointClientFRC6RefVar
// 'siop from {opCode, data: {bps, parity, dataBits, stopBits}}.
TOption*
TScriptEndpointClient::ConvertToSerialIOParms(RefArg option)
{
	TCMOSerialIOParms* parms = NewOptionOf<TCMOSerialIOParms>();
	if (parms != nil && NOTNIL(option))
	{
		RefVar value(GetFrameSlotRef(option, RSSYMopcode));
		if (ISINT(value))
			parms->SetOpCode(RINT(value) & 0xff00);
		RefVar data(GetFrameSlotRef(option, RSSYMdata));
		if (NOTNIL(data))
		{
			value = GetFrameSlotRef(data, RSSYMbps);
			if (ISINT(value))
				parms->fSpeed = RINT(value);
			value = GetFrameSlotRef(data, RSSYMparity);
			if (ISINT(value))
				parms->fParity = RINT(value);
			value = GetFrameSlotRef(data, RSSYMdatabits);
			if (ISINT(value))
				parms->fDataBits = RINT(value);
			value = GetFrameSlotRef(data, RSSYMstopbits);
			if (ISINT(value))
				parms->fStopBits = RINT(value);
		}
	}
	return parms;
}


// ROM 0x00067c60 ConvertToFlowControlParms__21TScriptEndpointClientFRC6RefVarUc
// 'iflc/'oflc from {opCode, data: {xonChar, xoffChar, useSoftFlowControl,
// useHardFlowControl}}.
TOption*
TScriptEndpointClient::ConvertToFlowControlParms(RefArg option, Boolean input)
{
	TCMOFlowControlParms* parms;
	if (input)
		parms = NewOptionOf<TCMOInputFlowControlParms>();
	else
		parms = NewOptionOf<TCMOOutputFlowControlParms>();
	if (parms != nil && NOTNIL(option))
	{
		RefVar value(GetFrameSlotRef(option, RSSYMopcode));
		if (ISINT(value))
			parms->SetOpCode(RINT(value) & 0xff00);
		RefVar data(GetFrameSlotRef(option, RSSYMdata));
		if (NOTNIL(data))
		{
			value = GetFrameSlotRef(data, RSSYMxonchar);
			if (ISINT(value))
				parms->xonChar = RINT(value);
			value = GetFrameSlotRef(data, RSSYMxoffchar);
			if (ISINT(value))
				parms->xoffChar = RINT(value);
			parms->useSoftFlowControl = NOTNIL(GetFrameSlotRef(data, RSSYMusesoftflowcontrol));
			parms->useHardFlowControl = NOTNIL(GetFrameSlotRef(data, RSSYMusehardflowcontrol));
		}
	}
	return parms;
}


// ROM 0x00067ed0 ConvertToModemDialingOption__21TScriptEndpointClientFRC6RefVar
// 'mdo  from the preferences, then {opCode, data: {speakerOn,
// detectDialTone, detectBusy, dtmfToneDialing, manualDial, speakerVolume,
// waitForCarrier, waitBeforeBlindDial, commaDelay, ringToAnswerAfter}}.
TCMOModemDialing*
TScriptEndpointClient::ConvertToModemDialingOption(RefArg option)
{
	TCMOModemDialing* dialing = NewOptionOf<TCMOModemDialing>();
	if (dialing != nil && NOTNIL(option))
	{
		SetDialingOptionsFromPrefs(dialing);
		RefVar value(GetFrameSlotRef(option, RSSYMopcode));
		if (ISINT(value))
			dialing->SetOpCode(RINT(value) & 0xff00);
		RefVar data(GetFrameSlotRef(option, RSSYMdata));
		if (NOTNIL(data))
		{
			// ROM BUG: the five switches are given the low byte of the
			// Ref, not 0 or 1 - true is 0x1a and nil 2, both of them true
			if (FrameHasSlot(data, RSSYMspeakeron))
				dialing->fSpeakerOn = (UByte) (Ref) GetFrameSlotRef(data, RSSYMspeakeron);
			if (FrameHasSlot(data, RSSYMdetectdialtone))
				dialing->fDetectDialTone = (UByte) (Ref) GetFrameSlotRef(data, RSSYMdetectdialtone);
			if (FrameHasSlot(data, RSSYMdetectbusy))
				dialing->fDetectBusy = (UByte) (Ref) GetFrameSlotRef(data, RSSYMdetectbusy);
			if (FrameHasSlot(data, RSSYMdtmftonedialing))
				dialing->fDTMFToneDialing = (UByte) (Ref) GetFrameSlotRef(data, RSSYMdtmftonedialing);
			if (FrameHasSlot(data, RSSYMmanualdial))
				dialing->fManualDial = (UByte) (Ref) GetFrameSlotRef(data, RSSYMmanualdial);
			value = GetFrameSlotRef(data, RSSYMspeakervolume);
			if (ISINT(value))
				dialing->fSpeakerVolume = RINT(value);
			value = GetFrameSlotRef(data, RSSYMwaitforcarrier);
			if (ISINT(value))
				dialing->fWaitForCarrier = RINT(value);
			value = GetFrameSlotRef(data, RSSYMwaitbeforeblinddial);
			if (ISINT(value))
				dialing->fWaitBeforeBlindDial = RINT(value);
			value = GetFrameSlotRef(data, RSSYMcommadelay);
			if (ISINT(value))
				dialing->fCommaDelay = RINT(value);
			value = GetFrameSlotRef(data, RSSYMringtoanswerafter);
			if (ISINT(value))
				dialing->fRingToAnswerAfter = RINT(value);
		}
	}
	return dialing;
}


// ROM 0x0006a374 ConvertToServiceOption__FUl
// A 'sid ' service option, required, naming the service.
TOption*
ConvertToServiceOption(ULong serviceId)
{
	TCMOServiceIdentifier* sid = (TCMOServiceIdentifier*) NewPtrClear(sizeof(TCMOServiceIdentifier));
	if (sid == nil)
		return nil;
	sid->SetLabel(kCMOServiceIdentifier);
	sid->SetAsService();
	sid->SetOpCode(opSetRequired);
	sid->SetLength(OPTION_DATA_LENGTH(TCMOServiceIdentifier));
	sid->fServiceId = serviceId;
	sid->fPortId = 0;
	return sid;
}


// ROM 0x00068dc8 IsRawBinary__FRC6RefVar
// A binary that is not a string.
static Boolean
IsRawBinary(RefArg obj)
{
	if (ISNIL(obj))
		return false;
	return (ObjectFlags(obj) & 1) == 0 && !IsString(obj);		// (1: slotted)
}


// ROM 0x00068180 ConvertToOption__21TScriptEndpointClientFRC6RefVar
// An option frame made an option: by its label ('rout, 'siop, 'iflc,
// 'oflc, 'mdo ), a service by its name, otherwise from its type, opCode
// (default 0x100) and data (an integer as a word, a string's characters
// with their terminator, an array's bytes, or a binary).  Not a frame, a
// label that is not a string, or an unknown type: nil.
TOption*
TScriptEndpointClient::ConvertToOption(RefArg option)
{
	if (!IsFrame(option))
		return nil;
	RefVar value(GetFrameSlotRef(option, RSSYMlabel));
	if (!IsString(value))
		return nil;
	// (the ROM's four bytes are not cleared first: a label of fewer than
	// three characters takes whatever was there)
	UByte labelBytes[8] = { 0 };
	ConvertFromUnicode(GetCString(value), labelBytes, 1, 4);
	ULong label = GetBigEndianWord(labelBytes);
	value = GetFrameSlotRef(option, RSSYMtype);
	if (EQRef(value, RSSYMservice))
	{
		if (label != kCMOServiceIdentifier)
			return ConvertToServiceOption(label);
	}
	else if (label == kCMOAddress)
		return ConvertToAddressParms(option);
	else if (label == kCMOSerialIOParms)
		return ConvertToSerialIOParms(option);
	else if (label == kCMOInputFlowControl)
		return ConvertToFlowControlParms(option, true);
	else if (label == kCMOOutputFlowControl)
		return ConvertToFlowControlParms(option, false);
	else if (label == kCMOModemDialing)
		return ConvertToModemDialingOption(option);

	ULong opCode = opSetNegotiate;
	value = GetFrameSlotRef(option, RSSYMopcode);
	if (ISINT(value))
		opCode = RINT(value);
	long length = 0;
	RefVar data(GetVariable(option, RSSYMdata, nil, 0));
	if (IsString(data))
		length = Ustrlen(GetCString(data)) + 1;
	else if (ISINT(data))
		length = 4;
	else if (IsArray(data) || NOTNIL(data))
		length = Length(data);
	TOption* result = (TOption*) NewPtrClear(sizeof(TOption) + length);
	if (result == nil)
		return nil;
	result->SetOpCode(opCode & 0xff00);
	result->SetLength(length);
	value = GetFrameSlotRef(option, RSSYMtype);
	if (EQRef(value, RSSYMservice))
		result->SetAsService(label);
	else if (EQRef(value, RSSYMoption))
		result->SetAsOption(label);
	else if (EQRef(value, RSSYMconfig))
		result->SetAsConfig(label);
	else if (EQRef(value, RSSYMaddress))
		result->SetAsAddress(label);
	else
	{
		CIFree(result);
		return nil;
	}
	UByte* bytes = (UByte*) (result + 1);
	if (IsString(data))
		ConvertFromUnicode(GetCString(data), bytes, 1, length);
	else if (ISINT(data))
		PutBigEndianWord(bytes, RINT(data));		// (the MessagePad's word)
	else if (IsArray(data))
	{
		ArrayIndex count = Length(data);
		for (ArrayIndex i = 0; i < count; i++)
		{
			RefVar element(GetArraySlotRef(data, i));
			bytes[i] = ISINT(element) ? RINT(element) : 0;
		}
	}
	else if (IsRawBinary(data))
	{
		// ROM BUG: the bytes are copied from the option frame, not from
		// its data.  (The host copies no more than the frame's own bytes.)
		LockRef(option);
		long frameSize = Length(option) * sizeof(Ref);
		BlockMove(BinaryData(option), bytes, length < frameSize ? length : frameSize);
		UnlockRef(option);
	}
	return OptionFromDeviceData(result);
}


// ROM 0x00068640 ConvertFromOption__21TScriptEndpointClientFP7TOption
// An option as a frame {type, label, opCode, result, data}: a 'ctci as
// {errorFree, supportsCallback, viaAppleTalk, address {net, node,
// socket}, bps}, anything else as an array of its bytes.
Ref
TScriptEndpointClient::ConvertFromOption(TOption* option)
{
	RefVar frame(NILREF);
	if (option != nil)
	{
		frame = AllocateFrame();
		ULong label = option->Label();
		RefVar type;
		if (option->IsOption())
			type = RSSYMoption;
		else if (option->IsService())
			type = RSSYMservice;
		else if (option->IsAddress())
			type = RSSYMaddress;
		else
			type = RSSYMconfig;
		SetFrameSlot(frame, RSSYMtype, type);
		RefVar data(AllocateBinary(RSSYMstring, 10));
		UByte labelBytes[4];
		PutBigEndianWord(labelBytes, label);
		ConvertToUnicode(labelBytes, (UniChar*) BinaryData(data), 1, 4);
		SetFrameSlot(frame, RSSYMlabel, data);
		SetFrameSlot(frame, RSSYMopcode, RefVar(MAKEINT(option->GetOpCode())));
		SetFrameSlot(frame, RSSYMresult, RefVar(MAKEINT((signed char) option->GetOpCodeResults())));
		if (label == kCMOCTConnectInfo)
		{
			TCMOCTConnectInfo* info = (TCMOCTConnectInfo*) option;
			data = AllocateFrame();
			SetFrameSlot(data, RSSYMerrorfree, RefVar(info->fErrorFree ? TRUEREF : NILREF));
			SetFrameSlot(data, RSSYMsupportscallback, RefVar(info->fSupportsCallBack ? TRUEREF : NILREF));
			SetFrameSlot(data, RSSYMviaappletalk, RefVar(info->fViaAppleTalk ? TRUEREF : NILREF));
			if (info->fViaAppleTalk)
			{
				RefVar address(AllocateFrame());
				ULong at = info->fAppleTalkAddr;
				SetFrameSlot(address, RSSYMnet, RefVar(MAKEINT((at >> 16) & 0xffff)));
				SetFrameSlot(address, RSSYMnode, RefVar(MAKEINT((at >> 8) & 0xff)));
				SetFrameSlot(address, RSSYMsocket, RefVar(MAKEINT(at & 0xff)));
				SetFrameSlot(data, RSSYMaddress, address);
			}
			SetFrameSlot(data, RSSYMbps, RefVar(MAKEINT(info->fConnectBitsPerSecond)));
			SetFrameSlot(frame, RSSYMdata, data);
		}
		else
		{
			UByte buffer[64];
			long length;
			const UByte* bytes = OptionDeviceData(option, buffer, sizeof(buffer), &length);
			data = MakeArray(length);
			for (long i = 0; i < length; i++)
				SetArraySlotRef(data, i, RefVar(MAKEINT(bytes[i])));
			SetFrameSlot(frame, RSSYMdata, data);
		}
	}
	return frame;
}


/*------------------------------------------------------------------------------
	Requests
------------------------------------------------------------------------------*/

// ROM 0x000689fc DoState__21TScriptEndpointClientFv
Ref
TScriptEndpointClient::DoState(void)
{
	return MAKEINT(fEndpoint->GetState());
}


// ROM 0x00068a0c DoConnect__21TScriptEndpointClientFRC6RefVarT1
// Bound and connected synchronously, then asynchronous with the
// endpoint's nextInputSpec in place; unbound again if the connect fails.
NewtonErr
TScriptEndpointClient::DoConnect(RefArg address, RefArg options)
{
	NewtonErr err = noErr;
	TOptionArray* addressArray = nil;
	TOptionArray* optionArray = nil;
	if (NOTNIL(address))
	{
		addressArray = new TOptionArray;
		if ((err = MemError()) != noErr || (err = addressArray->Init()) != noErr
		||  (err = ConvertToOptionArray(address, addressArray)) != noErr)
			goto done;
	}
	if (NOTNIL(options))
	{
		optionArray = new TOptionArray;
		if ((err = MemError()) != noErr || (err = optionArray->Init()) != noErr
		||  (err = ConvertToOptionArray(options, optionArray)) != noErr)
			goto done;
	}
	fEndpoint->SetSync(true);
	if ((err = fEndpoint->Bind(nil, nil, 0)) == noErr)
	{
		if ((err = fEndpoint->Connect(addressArray, optionArray, nil, nil, 0)) == noErr)
		{
			fEndpoint->SetSync(false);
			RefVar spec(GetVariable(fEndpointRef, RSSYMnextinputspec, nil, 0));
			SetInputSpec(spec);
		}
		else
			fEndpoint->UnBind(0);
	}
done:
	if (addressArray != nil)
		delete addressArray;
	if (optionArray != nil)
		delete optionArray;
	return TranslateError(err);
}


// ROM 0x00068bb8 DoListen__21TScriptEndpointClientFRC6RefVar
// Bound, listened for and accepted synchronously, then asynchronous with
// the endpoint's nextInputSpec in place.
NewtonErr
TScriptEndpointClient::DoListen(RefArg options)
{
	NewtonErr err = noErr;
	TOptionArray* optionArray = nil;
	if (NOTNIL(options))
	{
		optionArray = new TOptionArray;
		if ((err = MemError()) != noErr || (err = optionArray->Init()) != noErr
		||  (err = ConvertToOptionArray(options, optionArray)) != noErr)
			goto done;
	}
	fEndpoint->SetSync(true);
	if ((err = fEndpoint->Bind(nil, nil, 0)) == noErr
	&&  (err = fEndpoint->Listen(nil, optionArray, nil, nil, 0)) == noErr
	&&  (err = fEndpoint->Accept(fEndpoint, nil, nil, nil, 0, 0)) == noErr)
	{
		fEndpoint->SetSync(false);
		RefVar spec(GetVariable(fEndpointRef, RSSYMnextinputspec, nil, 0));
		SetInputSpec(spec);
	}
done:
	if (optionArray != nil)
		delete optionArray;
	return TranslateError(err);
}


// ROM 0x00068d1c DoCaller__21TScriptEndpointClientFv
Ref
TScriptEndpointClient::DoCaller(void)
{
	return NILREF;
}


// ROM 0x00068d24 DoAccept__21TScriptEndpointClientFv
NewtonErr
TScriptEndpointClient::DoAccept(void)
{
	return fEndpoint->Accept(fEndpoint, nil, nil, nil, 0, 0);
}


// ROM 0x00068d5c DoReject__21TScriptEndpointClientFv
NewtonErr
TScriptEndpointClient::DoReject(void)
{
	return fEndpoint->Disconnect(nil, 0, 0);
}


// ROM 0x00068d70 DoRelease__21TScriptEndpointClientFv
// The output drained, then released, unbound and closed synchronously.
NewtonErr
TScriptEndpointClient::DoRelease(void)
{
	NewtonErr err = DoFlushOutput();
	if (err != noErr)
		return err;
	fEndpoint->SetSync(true);
	err = fEndpoint->Release(0);
	if (err == noErr)
		err = fEndpoint->UnBind(0);
	if (err != noErr)
		return err;
	return fEndpoint->Close();
}


// ROM 0x00068e18 DoDisconnect__21TScriptEndpointClientFv
NewtonErr
TScriptEndpointClient::DoDisconnect(void)
{
	NewtonErr err = fEndpoint->EasyClose();
	if (err != noErr)
		ThrowMsg("Endpoint EasyClose failed");
	return err;
}


// ROM 0x00068e5c DoAbort__21TScriptEndpointClientFv
NewtonErr
TScriptEndpointClient::DoAbort(void)
{
	NewtonErr err = fEndpoint->Abort();
	if (err != noErr)
		ThrowMsg("Endpoint Abort failed");
	return err;
}


/*------------------------------------------------------------------------------
	Output
------------------------------------------------------------------------------*/

// ROM 0x00068e9c DoOutput__21TScriptEndpointClientFRC6RefVarT1
// The data as bytes, sent when there is room for them among the 1024 that
// may be outstanding (a Yield until then); flags an integer, else 1.
NewtonErr
TScriptEndpointClient::DoOutput(RefArg data, RefArg flags)
{
	Size count = DoOutputOne(data, nil);
	if (count == 0)
		return noErr;
	while (fOutputRoom - count < 0 && fOutputRoom < kOutputWindow)
		Yield();
	UByte* buffer = (UByte*) CIMalloc(count + 1);
	if (buffer == nil)
		return MemError();
	DoOutputOne(data, buffer);
	NewtonErr err = fEndpoint->Snd(buffer, count, ISINT(flags) ? RINT(flags) : 1, 0);
	if (err != noErr)
		ThrowMsg("Endpoint snd failed");
	fOutputRoom -= count;
	return err;
}


// ROM 0x00068fb8 DoOutputOne__21TScriptEndpointClientFRC6RefVarPUc
// An object's bytes (written to buffer unless nil; ==> how many): a
// string's characters in the encoding, a character's byte, an integer's
// low byte, each element of an array, a binary's bytes; anything else
// none.
long
TScriptEndpointClient::DoOutputOne(RefArg data, UByte* buffer)
{
	if (IsString(data))
	{
		long length = Ustrlen(GetCString(data));
		if (buffer != nil)
			ConvertFromUnicode(GetCString(data), buffer, fEncoding, length);
		return length;
	}
	if (ISCHAR(data))
	{
		if (buffer == nil)
			return 1;
		*buffer = CharToByte(RCHAR(data), fEncoding);
		return 1;
	}
	if (ISINT(data))
	{
		if (buffer == nil)
			return 1;
		*buffer = RINT(data);
		return 1;
	}
	if (IsArray(data))
	{
		long total = 0;
		ArrayIndex count = Length(data);
		for (ArrayIndex i = 0; i < count; i++)
		{
			RefVar element(GetArraySlotRef(data, i));
			long length = DoOutputOne(element, buffer);
			if (buffer != nil)
				buffer += length;
			total += length;
		}
		return total;
	}
	if (!IsRawBinary(data))
		return 0;
	long length = Length(data);
	if (buffer == nil)
		return length;
	LockRef(data);
	BlockMove(BinaryData(data), buffer, length);
	UnlockRef(data);
	return length;
}


// ROM 0x00069194 DoFlushOutput__21TScriptEndpointClientFv
// Waiting until everything sent has gone.
NewtonErr
TScriptEndpointClient::DoFlushOutput(void)
{
	while (fOutputRoom < kOutputWindow)
		Yield();
	return noErr;
}


// ROM 0x000691cc DoOutputFrame__21TScriptEndpointClientFRC6RefVarT1
// An object flattened (NSOF) after a length word, in a buffer segment the
// send completion deletes.
NewtonErr
TScriptEndpointClient::DoOutputFrame(RefArg data, RefArg flags)
{
	CBufferSegment* segment = nil;
	NewtonErr err = DoFlushOutput();
	if (err == noErr)
	{
		CNullPipe pipe(0x100);
		segment = new CBufferSegment;
		err = MemError();
		if (segment != nil && err == noErr && (err = segment->Init(0x100)) == noErr)
		{
			pipe.Init(nil, segment, false);
			pipe.WriteSeek(4, -1);
			TObjectWriter writer(data, pipe, false);
			writer.Write();
			long size = pipe.WritePosition();
			if (size - 4 > 0)
			{
				pipe.WriteSeek(0, -1);
				pipe << (long) (size - 4);
				segment->Reset();
				segment->Hide(segment->GetSize() - size, 1);
				err = fEndpoint->Snd(segment, ISINT(flags) ? RINT(flags) : 1, 0);
				if (err != noErr)
					ThrowMsg("Endpoint snd failed");
			}
		}
	}
	if (err != noErr && segment != nil)
		delete segment;
	return err;
}


// ROM 0x0006a44c DoReadyForOutput__21TScriptEndpointClientFRC6RefVar
// Whether the data would be sent without waiting.
Ref
TScriptEndpointClient::DoReadyForOutput(RefArg data)
{
	long count = DoOutputOne(data, nil);
	return (fOutputRoom - count < 0 && fOutputRoom < kOutputWindow) ? NILREF : TRUEREF;
}


// ROM 0x0006a480 DoOutputDone__21TScriptEndpointClientFv
Ref
TScriptEndpointClient::DoOutputDone(void)
{
	return (fOutputRoom == kOutputWindow) ? TRUEREF : NILREF;
}


/*------------------------------------------------------------------------------
	Input
------------------------------------------------------------------------------*/

// ROM 0x00069464 DoInputSpec__21TScriptEndpointClientFRC6RefVar
void
TScriptEndpointClient::DoInputSpec(RefArg inputSpec)
{
	SetInputSpec(inputSpec);
}


// ROM 0x00069468 SetInputSpec__21TScriptEndpointClientFRC6RefVar
// The input spec read: {byteCount, recvFlags, nullProxy, sevenBit,
// discardAfter (1024), endCharacter, partialFrequency, inputForm}; the
// input buffer sized to keep discardAfter bytes and 0x40 more (a 'frame's
// to its length once that has come); input already there checked; a
// receive outstanding while no input is complete; the partial idler set.
void
TScriptEndpointClient::SetInputSpec(RefArg inputSpec)
{
	fInputSpec = inputSpec;
	if (ISNIL(fInputSpec))
	{
		fInputEnd = 0;
		fInputSpec = NILREF;
		fUseEndChar = false;
		fByteCount = 0;
		fPartialFrequency = 0;
		fNullProxy = 0;
		fSevenBit = false;
		return;
	}
	RefVar value(GetVariable(inputSpec, RSSYMbytecount, nil, 0));
	fByteCount = ISINT(value) ? RINT(value) : 0;
	value = GetVariable(inputSpec, RSSYMrecvflags, nil, 0);
	fRecvFlags = ISINT(value) ? RINT(value) : 0;
	value = GetVariable(inputSpec, RSSYMnullproxy, nil, 0);
	fNullProxy = ISINT(value) ? RINT(value) : 0;
	value = GetVariable(inputSpec, RSSYMsevenbit, nil, 0);
	fSevenBit = NOTNIL(value);
	value = GetVariable(inputSpec, RSSYMdiscardafter, nil, 0);
	fDiscardAfter = ISINT(value) ? RINT(value) : kOutputWindow;
	value = GetVariable(inputSpec, RSSYMendcharacter, nil, 0);
	if (ISCHAR(value))
	{
		fUseEndChar = true;
		fEndChar = CharToByte(RCHAR(value), fEncoding);
	}
	else
		fUseEndChar = false;
	value = GetVariable(inputSpec, RSSYMpartialfrequency, nil, 0);
	fPartialFrequency = ISINT(value) ? RINT(value) : 0;
	value = GetVariable(inputSpec, RSSYMinputform, nil, 0);
	fFormString = EQRef(value, RSSYMstring);
	fFormFrame = EQRef(value, RSSYMframe);
	fFormRaw = EQRef(value, RSSYMraw);
	if (!fFormFrame)
	{
		Size size = fDiscardAfter + 0x40;
		if (fInput == nil)
		{
			UByte* input = (UByte*) CIMalloc(size);
			if (MemError() != noErr)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			fInputCount = 0;
			fInputSize = size;
			fInput = input;
		}
		else if (fInputSize != size)
		{
			UByte* input = (UByte*) CIMalloc(size);
			if (MemError() != noErr)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			if (size < fInputCount)
			{
				BlockMove(fInput + (fInputCount - size), input, size);
				fInputCount = size;
			}
			else
				BlockMove(fInput, input, fInputCount);
			CIFree(fInput);
			fInputSize = size;
			fInput = input;
		}
		fInputEnd = 0;
		fPartialConsumed = 0;
		fPartialPending = false;
	}
	else
	{
		if (fInput == nil || (ULong) fInputCount < 4)
		{
			fFrameNeedsLength = true;
			if (fInput == nil)
			{
				fInput = (UByte*) CIMalloc(0x44);
				if (MemError() != noErr)
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				fInputCount = 0;
				fInputSize = 0x44;
			}
		}
		else
		{
			// DEVIATION: the length word is big-endian, as it came
			fByteCount = (long) GetBigEndianWord(fInput);
			fInputCount -= 4;
			BlockMove(fInput + 4, fInput, fInputCount);
			fFrameNeedsLength = false;
			Size size = fByteCount + 0x40;
			UByte* input = (UByte*) ReallocPtr((Ptr) fInput, size);
			if (MemError() != noErr)
				Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
			fInputSize = size;
			fInput = input;
		}
		fDiscardAfter = 0x7fffffff;
	}
	CheckForInput();
	ULong flags = fRecvFlags;
	if (fInputEnd == 0 && NOTNIL(fInputSpec) && !fRcvPending && !fInRcvComplete)
	{
		fRcvBuffer.Reset();
		if (fEndpoint->Rcv(&fRcvBuffer, 1, &flags, 0) == noErr)
			fRcvPending = true;
	}
	if (fInputEnd == 0 && fPartialFrequency != 0)
		ResetIdle(fPartialFrequency, kMilliseconds);
	else
		StopIdle();
}


// ROM 0x000699b0 DoFlushPartial__21TScriptEndpointClientFv
// What the partialScript was given thrown away, and the rest checked.
void
TScriptEndpointClient::DoFlushPartial(void)
{
	long rest = fInputCount - fPartialConsumed;
	if (rest > 0)
		BlockMove(fInput + fPartialConsumed, fInput, rest);
	fPartialConsumed = 0;
	fInputCount = rest;
	fPartialPending = false;
	fInputEnd = 0;
	CheckForInput();
}


// ROM 0x00069a3c DoFlushInput__21TScriptEndpointClientFv
void
TScriptEndpointClient::DoFlushInput(void)
{
	fInputCount = 0;
	fInputEnd = 0;
	fPartialConsumed = 0;
	fPartialPending = false;
}


// ROM 0x00069a54 CheckForInput__21TScriptEndpointClientFv
// Whether the input there is complete: to the end character, or the byte
// count (a 'frame's always); if so, posted.
void
TScriptEndpointClient::CheckForInput(void)
{
	long count = fInputCount;
	if (count > 0)
		fPartialPending = true;
	if (!fFormFrame || count < fByteCount)
	{
		if (fUseEndChar)
		{
			for (long i = 0; i < count; i++)
			{
				if (fInput[i] == fEndChar)
				{
					fInputEnd = i + 1;
					PostInput();
					return;
				}
			}
		}
		if (fByteCount < 1 || count < fByteCount)
			return;
	}
	fInputEnd = fByteCount;
	PostInput();
}


// ROM 0x00069ae4 PostInput__21TScriptEndpointClientFv
// The input to the input spec's inputScript(endpoint, data), then the
// endpoint's nextInputSpec in place.
void
TScriptEndpointClient::PostInput(void)
{
	if (ISNIL(fInputSpec))
		return;
	RefVar spec(fInputSpec);
	RefVar script(GetVariable(fInputSpec, RSSYMinputscript, nil, 0));
	if (NOTNIL(script))
	{
		RefVar data(DoInput());
		if (NOTNIL(data))
		{
			RefVar args(MakeArray(2));
			SetArraySlotRef(args, 0, fEndpointRef);
			SetArraySlotRef(args, 1, data);
			DoMessage(spec, RSSYMinputscript, args);
			RefVar next(GetVariable(fEndpointRef, RSSYMnextinputspec, nil, 0));
			SetInputSpec(next);
		}
	}
}


// ROM 0x00069c20 DoInput__21TScriptEndpointClientFv
// The complete input (its last discardAfter bytes) converted by the form
// and taken out of the buffer; the input spec is done with.
Ref
TScriptEndpointClient::DoInput(void)
{
	RefVar data(NILREF);
	long end = fInputEnd;
	if (end > 0)
	{
		long length = end;
		if (fDiscardAfter < end)
			length = fDiscardAfter;
		data = ConvertBlock(fInput + end - length, length);
		end = fInputEnd;
		if (end < fInputCount)
			BlockMove(fInput + end, fInput, fInputCount - end);
		fInputCount -= fInputEnd;
		fInputEnd = 0;
		fInputSpec = NILREF;
		fUseEndChar = false;
		fByteCount = 0;
		fPartialFrequency = 0;
		fFormString = false;
		fFormFrame = false;
		fFormRaw = false;
		fNullProxy = 0;
		fSevenBit = false;
	}
	return data;
}


// ROM 0x00069cec ConvertBlock__21TScriptEndpointClientFPUcl
// Bytes by the input form: a string in the encoding, a frame read as NSOF,
// a binary of no class ('raw), or an array of the bytes.
Ref
TScriptEndpointClient::ConvertBlock(UByte* bytes, long length)
{
	RefVar data(NILREF);
	if (fFormString)
	{
		data = AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar));
		if (NOTNIL(data))
			ConvertToUnicode(bytes, GetCString(data), fEncoding, length);
	}
	else if (fFormFrame)
	{
		CBufferSegment segment;
		if (segment.Init(bytes, length, false, 0, -1) == noErr)
		{
			CNullPipe pipe(0);
			pipe.Init(&segment, nil, false);
			pipe.ReadSeek(0, -1);
			TObjectReader reader(pipe);
			data = reader.Read();
		}
	}
	else if (fFormRaw)
	{
		data = AllocateBinary(RefVar(NILREF), length);
		LockRef(data);
		BlockMove(bytes, BinaryData(data), length);
		UnlockRef(data);
	}
	else
	{
		data = MakeArray(length);
		if (NOTNIL(data))
			for (long i = 0; i < length; i++)
				SetArraySlotRef(data, i, RefVar(MAKEINT(bytes[i])));
	}
	return data;
}


// ROM 0x0006a3dc DoInputAvailable__21TScriptEndpointClientFv
Ref
TScriptEndpointClient::DoInputAvailable(void)
{
	return (fInputEnd < 1) ? NILREF : TRUEREF;
}


// ROM 0x0006a3f0 DoPartial__21TScriptEndpointClientFv
// The input so far (not taken out of the buffer).
Ref
TScriptEndpointClient::DoPartial(void)
{
	RefVar data(NILREF);
	if (fInputCount > 0)
	{
		data = ConvertBlock(fInput, fInputCount);
		fPartialConsumed = fInputCount;
		fPartialPending = false;
	}
	return data;
}


// ROM 0x0006a494 DoBytesAvailable__21TScriptEndpointClientFv
Ref
TScriptEndpointClient::DoBytesAvailable(void)
{
	return (fInputCount < 1) ? NILREF : TRUEREF;
}


/*------------------------------------------------------------------------------
	The natives (protoEndpoint's methods)
------------------------------------------------------------------------------*/

static inline Ref
ErrorRef(NewtonErr err)
{
	return (err == noErr) ? NILREF : MAKEINT(err);
}


// ROM 0x0006a4c8 CIInstantiate
Ref
CIInstantiate(RefArg rcvr, RefArg endpoint, RefArg options)
{
	TScriptEndpointClient* client = (TScriptEndpointClient*) NewPtr(sizeof(TScriptEndpointClient));
	if (client == nil)
		return MAKEINT(-7000);
	client = new (client) TScriptEndpointClient;
	return ErrorRef(client->InitScriptEndpointClient(endpoint, options, nil));
}


// ROM 0x0006a518 CIInstantiateFromEndpoint
// An endpoint already made (from disposeLeavingTEndpoint) taken over.
Ref
CIInstantiateFromEndpoint(RefArg rcvr, RefArg endpoint, RefArg options, RefArg tEndpoint)
{
	TScriptEndpointClient* client = (TScriptEndpointClient*) NewPtr(sizeof(TScriptEndpointClient));
	if (client == nil)
		return MAKEINT(-7000);
	client = new (client) TScriptEndpointClient;
	return ErrorRef(client->InitScriptEndpointClient(endpoint, options, (TEndpoint*) RefToAddress(tEndpoint)));
}


static void
DeleteScriptClient(TScriptEndpointClient* client)
{
	client->~TScriptEndpointClient();
	DisposPtr((Ptr) client);
}


// ROM 0x0006a57c CIDispose
Ref
CIDispose(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client != nil)
		DeleteScriptClient(client);
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar(NILREF));
	return NILREF;
}


// ROM 0x0006a5dc CIDisposeLeavingTEndpoint
// The client deleted and its endpoint (aborted, with no client) answered,
// for another instantiateFromTEndpoint.
Ref
CIDisposeLeavingTEndpoint(RefArg rcvr)
{
	RefVar result(NILREF);
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client != nil)
	{
		TEndpoint* ep = client->fEndpoint;
		result = AddressToRef(ep);
		ep->SetClientHandler(0);
		ep->Abort();
		client->fEndpoint = nil;
		DeleteScriptClient(client);
	}
	SetFrameSlot(rcvr, RSSYMciprivate, RefVar(NILREF));
	return result;
}


// ROM 0x0006a744 CISetOptions
Ref
CISetOptions(RefArg rcvr, RefArg options)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoSetOptions(options));
}


// ROM 0x0006a780 CIGetOptions
Ref
CIGetOptions(RefArg rcvr, RefArg options)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoGetOptions(options);
}


// ROM 0x0006a7ac CIGetOption
// (not in the ROM's table of natives: nothing binds it)
Ref
CIGetOption(RefArg rcvr, RefArg option)
{
	return NILREF;
}


// ROM 0x0006a7b4 CIState
Ref
CIState(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoState();
}


// ROM 0x0006a7d8 CIConnect
Ref
CIConnect(RefArg rcvr, RefArg address, RefArg options)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoConnect(address, options));
}


// ROM 0x0006a81c CIListen
Ref
CIListen(RefArg rcvr, RefArg options)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoListen(options));
}


// ROM 0x0006a858 CICaller
Ref
CICaller(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoCaller();
}


// ROM 0x0006a87c CIAccept
Ref
CIAccept(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoAccept());
}


// ROM 0x0006a8b0 CIReject
Ref
CIReject(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoReject());
}


// ROM 0x0006a8e4 CIRelease
Ref
CIRelease(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoRelease());
}


// ROM 0x0006a9e0 CIDisconnect
Ref
CIDisconnect(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoDisconnect());
}


// ROM 0x0006aa14 CIAbort
Ref
CIAbort(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoAbort());
}


// ROM 0x0006aa48 CIOutput
Ref
CIOutput(RefArg rcvr, RefArg data, RefArg flags)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoOutput(data, flags));
}


// ROM 0x0006aa8c CIFlushOutput
Ref
CIFlushOutput(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoFlushOutput());
}


// ROM 0x0006aac0 CIOutputFrame
Ref
CIOutputFrame(RefArg rcvr, RefArg data, RefArg flags)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->DoOutputFrame(data, flags));
}


// ROM 0x0006ab04 CISetInputSpec
// The spec in place, and kept as the endpoint's nextInputSpec.
Ref
CISetInputSpec(RefArg rcvr, RefArg inputSpec)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client != nil)
		client->DoInputSpec(inputSpec);
	SetFrameSlot(rcvr, RSSYMnextinputspec, inputSpec);
	return NILREF;
}


// ROM 0x0006ab44 CIInput
Ref
CIInput(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoInput();
}


// ROM 0x0006ab68 CIFlushPartial
Ref
CIFlushPartial(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	client->DoFlushPartial();
	return NILREF;
}


// ROM 0x0006ab90 CIFlushInput
Ref
CIFlushInput(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	client->DoFlushInput();
	return NILREF;
}


// ROM 0x0006abb8 CIInputAvailable
Ref
CIInputAvailable(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoInputAvailable();
}


// ROM 0x0006af24 CIPartial
Ref
CIPartial(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoPartial();
}


// ROM 0x0006af48 CIReadyForOutput
Ref
CIReadyForOutput(RefArg rcvr, RefArg data)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoReadyForOutput(data);
}


// ROM 0x0006af74 CIOutputDone
Ref
CIOutputDone(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoOutputDone();
}


// ROM 0x0006af98 CIBytesAvailable
Ref
CIBytesAvailable(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return NILREF;
	return client->DoBytesAvailable();
}


static TPseudoSyncState* gCCLState = nil;		// 0x0c100b60

// ROM 0x0006afbc CIStartCCL
// Waiting (the world forked meanwhile) for stopCCL.  (The CCL modem
// scripts themselves are NOT YET.)
Ref
CIStartCCL(RefArg rcvr)
{
	TPseudoSyncState state;
	if (state.Init() == noErr)
	{
		gCCLState = &state;
		state.Block(0);
		// ROM BUG: set again rather than cleared, so the global is left
		// pointing at a state that is about to go
		gCCLState = &state;
	}
	return NILREF;
}


// ROM 0x0006b014 CIStopCCL
Ref
CIStopCCL(RefArg rcvr)
{
	gCCLState->Unblock();
	return NILREF;
}


// ROM 0x0006b038 CISetSync
// ==> what SetSync answers
Ref
CISetSync(RefArg rcvr, RefArg sync)
{
	Boolean isSync = NOTNIL(sync);
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client != nil && client->fEndpoint->SetSync(isSync))
		return TRUEREF;
	return NILREF;
}


// ROM 0x0006b084 CIJustBind
Ref
CIJustBind(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->Bind(nil, nil, 0));
}


// ROM 0x0006b0c8 CIJustUnBind
Ref
CIJustUnBind(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->UnBind(0));
}


// ROM 0x0006b104 CIJustListen
// ROM BUG: the options go to Listen as its address, which a serial
// endpoint refuses.
Ref
CIJustListen(RefArg rcvr, RefArg options)
{
	NewtonErr err;
	TOptionArray* array = nil;
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		err = -1;
	else
	{
		if (NOTNIL(options))
		{
			array = new TOptionArray;
			if ((err = MemError()) != noErr || (err = array->Init()) != noErr
			||  (err = client->ConvertToOptionArray(options, array)) != noErr)
				goto done;
		}
		err = client->fEndpoint->Listen(array, nil, nil, nil, 0);
	}
done:
	if (array != nil)
		delete array;
	return ErrorRef(err);
}


// ROM 0x0006b2e0 CIJustConnect
Ref
CIJustConnect(RefArg rcvr, RefArg address, RefArg options)
{
	NewtonErr err;
	TOptionArray* addressArray = nil;
	TOptionArray* optionArray = nil;
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		err = -1;
	else
	{
		if (NOTNIL(address))
		{
			addressArray = new TOptionArray;
			if ((err = MemError()) != noErr || (err = addressArray->Init()) != noErr
			||  (err = client->ConvertToOptionArray(address, addressArray)) != noErr)
				goto done;
		}
		if (NOTNIL(options))
		{
			optionArray = new TOptionArray;
			if ((err = MemError()) != noErr || (err = optionArray->Init()) != noErr
			||  (err = client->ConvertToOptionArray(options, optionArray)) != noErr)
				goto done;
		}
		err = client->fEndpoint->Connect(addressArray, optionArray, nil, nil, 0);
	}
done:
	if (addressArray != nil)
		delete addressArray;
	if (optionArray != nil)
		delete optionArray;
	return ErrorRef(err);
}


// ROM 0x0006b3fc CIJustDisconnect
Ref
CIJustDisconnect(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->Disconnect(nil, 0, 0));
}


// ROM 0x0006b440 CIJustRelease
Ref
CIJustRelease(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->Release(0));
}


// ROM 0x0006b550 CIJustOpen
Ref
CIJustOpen(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->Open((ULong) client));
}


// ROM 0x0006b588 CIJustClose
Ref
CIJustClose(RefArg rcvr)
{
	TScriptEndpointClient* client = ScriptClient(rcvr);
	if (client == nil)
		return MAKEINT(-1);
	return ErrorRef(client->fEndpoint->Close());
}


void
RegisterScriptEndpointNatives(void)
{
	RegisterNativeFunction("CIInstantiate", (void*) CIInstantiate, 2);
	RegisterNativeFunction("CIInstantiateFromEndpoint", (void*) CIInstantiateFromEndpoint, 3);
	RegisterNativeFunction("CIDispose", (void*) CIDispose, 0);
	RegisterNativeFunction("CIDisposeLeavingTEndpoint", (void*) CIDisposeLeavingTEndpoint, 0);
	RegisterNativeFunction("CISetOptions", (void*) CISetOptions, 1);
	RegisterNativeFunction("CIGetOptions", (void*) CIGetOptions, 1);
	RegisterNativeFunction("CIState", (void*) CIState, 0);
	RegisterNativeFunction("CIConnect", (void*) CIConnect, 2);
	RegisterNativeFunction("CIListen", (void*) CIListen, 1);
	RegisterNativeFunction("CICaller", (void*) CICaller, 0);
	RegisterNativeFunction("CIAccept", (void*) CIAccept, 0);
	RegisterNativeFunction("CIReject", (void*) CIReject, 0);
	RegisterNativeFunction("CIRelease", (void*) CIRelease, 0);
	RegisterNativeFunction("CIDisconnect", (void*) CIDisconnect, 0);
	RegisterNativeFunction("CIAbort", (void*) CIAbort, 0);
	RegisterNativeFunction("CIOutput", (void*) CIOutput, 2);
	RegisterNativeFunction("CIFlushOutput", (void*) CIFlushOutput, 0);
	RegisterNativeFunction("CIOutputFrame", (void*) CIOutputFrame, 2);
	RegisterNativeFunction("CISetInputSpec", (void*) CISetInputSpec, 1);
	RegisterNativeFunction("CIInput", (void*) CIInput, 0);
	RegisterNativeFunction("CIFlushPartial", (void*) CIFlushPartial, 0);
	RegisterNativeFunction("CIFlushInput", (void*) CIFlushInput, 0);
	RegisterNativeFunction("CIInputAvailable", (void*) CIInputAvailable, 0);
	RegisterNativeFunction("CIPartial", (void*) CIPartial, 0);
	RegisterNativeFunction("CIReadyForOutput", (void*) CIReadyForOutput, 1);
	RegisterNativeFunction("CIOutputDone", (void*) CIOutputDone, 0);
	RegisterNativeFunction("CIBytesAvailable", (void*) CIBytesAvailable, 0);
	RegisterNativeFunction("CIStartCCL", (void*) CIStartCCL, 0);
	RegisterNativeFunction("CIStopCCL", (void*) CIStopCCL, 0);
	RegisterNativeFunction("CISetSync", (void*) CISetSync, 1);
	RegisterNativeFunction("CIJustBind", (void*) CIJustBind, 0);
	RegisterNativeFunction("CIJustUnBind", (void*) CIJustUnBind, 0);
	RegisterNativeFunction("CIJustListen", (void*) CIJustListen, 1);
	RegisterNativeFunction("CIJustConnect", (void*) CIJustConnect, 2);
	RegisterNativeFunction("CIJustDisconnect", (void*) CIJustDisconnect, 0);
	RegisterNativeFunction("CIJustRelease", (void*) CIJustRelease, 0);
	RegisterNativeFunction("CIJustOpen", (void*) CIJustOpen, 0);
	RegisterNativeFunction("CIJustClose", (void*) CIJustClose, 0);
}
