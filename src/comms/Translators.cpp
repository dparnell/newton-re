/*
	File:		comms/Translators.cpp

	Contains:	The frame translators (Translators.h): the PFrameSink and
				PFrameSource glue, PScriptDataOut/In, POptionDataOut/In,
				GetDataForm and InitTranslators.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Translators.h"
#include "HostOptionLayouts.h"
#include "CommOptions.h"
#include "Marshalling.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "toolbox/ByteOrder.h"
#include "ObjectStreamer.h"
#include "RefPipe.h"
#include "EndpointPipe.h"

#include <stdlib.h>
#include <string.h>

extern const ExceptionName exTranslatorException;		// "evt.ex.translator"


/* -------------------------------------------------------------------------------
	The interfaces' glue
------------------------------------------------------------------------------- */

// ROM 0x00389f18 New__12PFrameSourceSFPc
PFrameSource*
PFrameSource::New(char* implementation)
{
	PFrameSource* source = (PFrameSource*) AllocInstanceByName("PFrameSource", implementation);
	return source != nil ? (PFrameSource*) source->GlueNew() : nil;
}

// ROM 0x00389f44 Delete__12PFrameSourceFv
void
PFrameSource::Delete()
{
	GlueDelete();
}

// ROM 0x00389f80 New__10PFrameSinkSFPc
PFrameSink*
PFrameSink::New(char* implementation)
{
	PFrameSink* sink = (PFrameSink*) AllocInstanceByName("PFrameSink", implementation);
	return sink != nil ? (PFrameSink*) sink->GlueNew() : nil;
}

// ROM 0x00389fac Delete__10PFrameSinkFv
void
PFrameSink::Delete()
{
	GlueDelete();
}


/* -------------------------------------------------------------------------------
	GetDataForm
------------------------------------------------------------------------------- */

// ROM 0x00139e40 GetDataForm__FRC6RefVar8FormUser
// A form symbol as a FormType.  nil is 'template for an option, 'string for
// data and 'export for anyone else; an option cannot be a 'frame, and
// neither an option nor data can be an 'export (both are kFormNone).
FormType
GetDataForm(RefArg form, FormUser user)
{
	FormType type = kFormNone;
	Ref r = form;
	if (r == NILREF)
	{
		if (user == kFormUserOption)
			type = kFormTemplate;
		else if (user == kFormUserData)
			type = kFormString;
		else
			type = kFormExport;
	}
	else if (EQRef(r, RSSYMraw) || EQRef(r, RSSYMbinary))
		type = kFormBinary;
	else if (EQRef(r, RSSYMtemplate))
		type = kFormTemplate;
	else if (EQRef(r, RSSYMstring))
		type = kFormString;
	else if (EQRef(r, RSSYMnumber))
		type = kFormNumber;
	else if (EQRef(r, RSSYMexport))
		type = kFormExport;
	else if (EQRef(r, RSSYMframe))
		type = kFormFrame;
	else if (EQRef(r, RSSYMbytes))
		type = kFormBytes;
	else if (EQRef(r, RSSYMchar))
		type = kFormChar;

	if (user == kFormUserOption)
	{
		if (type == kFormFrame)
			return kFormNone;
	}
	else if (user > kFormUserData)
		return type;
	if (type == kFormExport)
		return kFormNone;
	return type;
}


/* -------------------------------------------------------------------------------
	PScriptDataOut
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PScriptDataOut)
PROTOCOL_CLASSINFO(PScriptDataOut, "PFrameSink", "", 0, 0, nil)	// ROM 0x0038a2f0 ClassInfo__14PScriptDataOutSFv

// ROM 0x001cd864 New__14PScriptDataOutFv
PScriptDataOut*	PScriptDataOut::New()		{ return this; }
// ROM 0x001cd868 Delete__14PScriptDataOutFv
void			PScriptDataOut::Delete()	{ }


// ROM 0x001cd86c Translate__14PScriptDataOutFPvP12PipeCallBack
// The value (or each element of an array of values, one after another)
// into a new block - a Ptr, or a Handle if the context says - with
// fHeaderSize bytes in front of it.  A failure is thrown
// (evt.ex.translator, the error as its data) and the block given back.
void*
PScriptDataOut::Translate(void* context, PipeCallBack* callback)
{
	FrameSinkParms* parms = (FrameSinkParms*) context;
	if (parms == nil)
		return nil;
	void* result = nil;
	RefVar value(parms->fValue);
	if (ISNIL(value))
		return nil;

	Ptr block = nil;
	Handle blockHandle = nil;
	long* lengths = nil;
	ULong count = 0;
	long total = 0;
	newton_try
	{
		long err;
		if (!IsArray(value))
			err = ParseOutputLength(value, parms->fForm, parms->fEncoding, &total);
		else
		{
			count = Length(value);
			lengths = (long*) malloc(count * sizeof(long));
			err = MemError();
			if (err == noErr)
			{
				*lengths = 0;
				for (ULong i = 0; i < count; i++)
				{
					RefVar element(GetArraySlotRef(value, i));
					err = ParseOutputLength(element, parms->fForm, parms->fEncoding, &lengths[i]);
					if (err != noErr)
						break;
					total += lengths[i];
				}
			}
		}
		if (err != noErr)
			Throw(exTranslatorException, (void*) (Long) err, nil);

		if (!parms->fUseHandle)
		{
			block = NewPtrClear(parms->fHeaderSize + total);
			if (block == nil)
				Throw(exTranslatorException, (void*) (Long) MemError(), nil);
		}
		else
		{
			blockHandle = NewHandleClear(parms->fHeaderSize + total);
			if (blockHandle == nil)
				Throw(exTranslatorException, (void*) (Long) MemError(), nil);
			HLock(blockHandle);
			block = *blockHandle;
		}

		UByte* p = (UByte*) block + parms->fHeaderSize;
		if (lengths == nil)
			err = ParseOutput(value, parms->fForm, parms->fEncoding, &total, p);
		else
		{
			for (ULong i = 0; i < count; i++)
			{
				RefVar element(GetArraySlotRef(value, i));
				err = ParseOutput(element, parms->fForm, parms->fEncoding, &lengths[i], p);
				if (err != noErr)
					break;
				p += lengths[i];
			}
			free(lengths);
			lengths = nil;
		}
		if (err != noErr)
			Throw(exTranslatorException, (void*) (Long) err, nil);
	}
	newton_catch_all
	{
		if (blockHandle != nil)
			DisposHandle(blockHandle);
		else if (block != nil)
			DisposPtr(block);
		// (the ROM leaks the lengths array here)
		rethrow;
	}
	end_try;

	result = block;
	if (blockHandle != nil)
	{
		HUnlock(blockHandle);
		result = blockHandle;
	}
	return result;
}


// ROM 0x001cdbb8 ParseOutputLength__14PScriptDataOutFRC6RefVar8FormTypelPl
// How many bytes the value comes to in the form (none is an error).
long
PScriptDataOut::ParseOutputLength(RefArg value, FormType form, long encoding, long* length)
{
	*length = 0;
	Ref r = value;
	if (form == kFormNumber)
	{
		if (!ISINT(r))
			return kCommScriptErrBadForm;
		*length = 4;
		return noErr;
	}
	if (form == kFormTemplate)
	{
		RefVar args(GetVariable(value, RSSYMarglist, nil, 0));
		RefVar types(GetVariable(value, RSSYMtypelist, nil, 0));
		long err;
		if (ISNIL(args) || ISNIL(types) || !IsArray(args) || !IsArray(types))
			err = kCommScriptErrBadTemplate;
		else
			err = noErr;
		if (err == noErr)
		{
			ULong size;
			err = MarshalArgumentSize(args, types, &size, encoding);
			*length = size;
		}
		if (err != noErr)
			return err;
	}
	else if (IsString(value) && (form == kFormExport || form == kFormString))
		*length = Umbstrlen(GetCString(value));
	else if (ISCHAR(r) && (form == kFormExport || form == kFormChar))
	{
		UniChar c = RCHAR(r);
		*length = Umbstrnlen(&c, encoding, 1);
	}
	else if (ISINT(r) && (form == kFormExport || form == kFormBytes))
	{
		*length = 1;
		return noErr;
	}
	else if (IsBinary(value) && (form == kFormExport || form == kFormBinary))
		*length = Length(value);
	else
		return kCommScriptErrBadForm;
	return (*length == 0) ? kCommScriptErrNoData : noErr;
}


// ROM 0x001cddf0 ParseOutput__14PScriptDataOutFRC6RefVar8FormTypelPlPUc
// The value's *length bytes into buf.
long
PScriptDataOut::ParseOutput(RefArg value, FormType form, long encoding, long* length, UByte* buf)
{
	Ref r = value;
	if (form == kFormNumber)
	{
		// DEVIATION: the long is written in the MessagePad's byte order,
		// where the ROM stores it in its own
		PutBigEndianWord(buf, (unsigned int) RINT(r));
		return noErr;
	}
	if (form == kFormTemplate)
	{
		RefVar args(GetVariable(value, RSSYMarglist, nil, 0));
		RefVar types(GetVariable(value, RSSYMtypelist, nil, 0));
		return MarshalArguments(args, types, buf, *length, encoding);
	}
	if (IsString(value) && (form == kFormExport || form == kFormString))
		ConvertUnicodeCharacters(GetCString(value), (char*) buf, encoding, *length);
	else if (ISCHAR(r) && (form == kFormExport || form == kFormChar))
	{
		UniChar c = RCHAR(r);
		ConvertUnicodeCharacters(&c, (char*) buf, encoding, *length);
	}
	else if (ISINT(r) && (form == kFormExport || form == kFormBytes))
		*buf = (UByte) RINT(r);
	else if (IsBinary(value) && (form == kFormExport || form == kFormBinary))
	{
		LockRef(r);
		memmove(buf, BinaryData(r), *length);
		UnlockRef(r);
	}
	else
		return kCommScriptErrBadForm;
	return noErr;
}


/* -------------------------------------------------------------------------------
	PScriptDataIn
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PScriptDataIn)
PROTOCOL_CLASSINFO(PScriptDataIn, "PFrameSource", "", 0, 0, nil)	// ROM 0x0038a36c ClassInfo__13PScriptDataInSFv

// ROM 0x001cd53c New__13PScriptDataInFv
PScriptDataIn*	PScriptDataIn::New()		{ return this; }
// ROM 0x001cd540 Delete__13PScriptDataInFv
void			PScriptDataIn::Delete()		{ }


// ROM 0x001cd544 Translate__13PScriptDataInFPvP12PipeCallBack
// The bytes as a value (a failure thrown as evt.ex.translator).
Ref
PScriptDataIn::Translate(void* context, PipeCallBack* callback)
{
	FrameSourceParms* parms = (FrameSourceParms*) context;
	RefVar result;
	if (parms != nil)
	{
		long err = noErr;
		result = ParseInput(parms->fForm, parms->fEncoding, parms->fLength, parms->fData, parms->fTemplate, &err);
		if (err != noErr)
			Throw(exTranslatorException, (void*) (Long) err, nil);
	}
	return result;
}


// ROM 0x001cd5d0 ParseInput__13PScriptDataInF8FormTypelT2PUcRC6RefVarPl
// length bytes of data as a value of the form: a character, a string, a
// number, an array of byte integers, a binary, or a 'template frame whose
// arglist is the data read by the typelist.  Nothing is an error
// (kCommScriptErrNoData); after an error the answer is nil.
Ref
PScriptDataIn::ParseInput(FormType form, long encoding, long length, UByte* data, RefArg typelist, long* error)
{
	RefVar result;
	*error = noErr;
	newton_try
	{
		switch (form)
		{
		case kFormChar:
			{
				// ROM BUG: length characters are converted into a four-byte
				// stack word, which is overrun by more than one (and its
				// terminator); the host gives them the room
				UniChar* c = (UniChar*) calloc(length + 2, sizeof(UniChar));
				ConvertToUnicode(data, c, encoding, length);
				result = MAKECHAR(c[0]);
				free(c);
			}
			break;

		case kFormString:
			result = AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar));
			ConvertToUnicode(data, GetCString(result), encoding, length);
			break;

		case kFormNumber:
			// DEVIATION: the long is read in the MessagePad's byte order
			result = MAKEINT((long) (int) GetBigEndianWord(data));
			break;

		case kFormBytes:
			result = MakeArray(length);
			for (long i = 0; i < length; i++)
				SetArraySlotRef(result, i, MAKEINT(*data++));
			break;

		case kFormBinary:
			result = AllocateBinary(RSSYMbinary, length);
			LockRef(result);
			memmove(BinaryData(result), data, length);
			UnlockRef(result);
			break;

		case kFormTemplate:
			{
				// DEVIATION: read in the MessagePad's byte order, which is
				// what came (the ROM's ConstructReturnValue reads its own)
				RefVar args(ConstructReturnValueFromDevice(data, typelist, error, encoding));
				if (*error == noErr)
				{
					if (IsArray(args))
						SetClass(args, RSSYMarray);
					result = AllocateFrame();
					SetFrameSlot(result, RSSYMtypelist, typelist);
					SetFrameSlot(result, RSSYMarglist, args);
				}
			}
			break;

		default:		// (a 'frame too)
			*error = kCommScriptErrBadForm;
			break;
		}
	}
	newton_catch_all
	{
		*error = (long) (Long) CurrentException()->data;
	}
	end_try;

	if (*error == noErr)
	{
		if (ISNIL(result))
			*error = kCommScriptErrNoData;
	}
	else
		result = NILREF;
	return result;
}


/* -------------------------------------------------------------------------------
	POptionDataOut
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(POptionDataOut)
PROTOCOL_CLASSINFO(POptionDataOut, "PFrameSink", "", 0, 0, nil)	// ROM 0x0038a3e8 ClassInfo__14POptionDataOutSFv

// ROM 0x0014be90 New__14POptionDataOutFv
POptionDataOut*	POptionDataOut::New()		{ return this; }
// ROM 0x0014be94 Delete__14POptionDataOutFv
void			POptionDataOut::Delete()	{ }


// ROM 0x0014be98 Translate__14POptionDataOutFPvP12PipeCallBack
// The option frame(s) appended to the option array, which is the answer.
// (What the conversion answered is dropped: a frame that is not an option
// simply adds nothing.)
void*
POptionDataOut::Translate(void* context, PipeCallBack* callback)
{
	OptionDataParms* parms = (OptionDataParms*) context;
	if (parms == nil)
		return nil;
	ConvertToOptionArray(parms->fFrame, parms->fOptions, (PFrameSink*) parms->fXlator);
	return parms->fOptions;
}


// ROM 0x0014bf00 ConvertToOptionArray__14POptionDataOutFRC6RefVarP12TOptionArrayP10PFrameSink
// A frame, or each of an array (of arrays...) of them, as options on the end
// of the array.
long
POptionDataOut::ConvertToOptionArray(RefArg frame, TOptionArray* options, PFrameSink* sink)
{
	long err = noErr;
	if (!IsArray(frame))
	{
		TOption* option = ConvertToOption(frame, err, sink);
		if (option != nil && err == noErr)
		{
			err = options->InsertOptionAt(options->GetArrayCount(), option);
			DisposPtr((Ptr) option);
		}
	}
	else
	{
		ULong count = Length(frame);
		for (ULong i = 0; i < count; i++)
		{
			RefVar element(GetArraySlotRef(frame, i));
			err = ConvertToOptionArray(element, options, sink);
			if (err != noErr)
				return err;
		}
	}
	return err;
}


// The first four characters of a label string as the word the ROM reads
// them as: its four bytes in order, most significant first.
static ULong
LabelFromString(RefArg label)
{
	char bytes[4];
	ConvertUnicodeCharacters(GetCString(label), bytes, 1, 4);
	return GetBigEndianWord(bytes);
}


// ROM 0x0014bff8 ConvertToOption__14POptionDataOutFRC6RefVarRlP10PFrameSink
// An option frame - {label: "four", type: 'service/'option/'config/
// 'address, opCode: an op code (opSetNegotiate if none), form: the data's
// form, data: ...} - as a new option (a Ptr the caller gives back), its
// data made by the sink.  A 'service frame with any label but 'sid ' is a
// 'sid ' option naming the service.
TOption*
POptionDataOut::ConvertToOption(RefArg frame, long& error, PFrameSink* sink)
{
	TOption* option = nil;
	error = noErr;
	RefVar type;
	ULong label = 0;
	FormType form = kFormNone;
	if (!IsFrame(frame))
		error = kCommScriptErrNotAnOption;
	else
	{
		RefVar labelString(GetVariable(frame, RSSYMlabel, nil, 0));
		if (ISNIL(labelString) || !IsString(labelString))
			error = kCommScriptErrNotAnOption;
		else
		{
			label = LabelFromString(labelString);
			form = GetDataForm(RefVar(GetVariable(frame, RSSYMform, nil, 0)), kFormUserOption);
			if (form == kFormNone)
				error = kCommScriptErrBadForm;
		}
	}
	if (error != noErr)
		return nil;

	type = GetVariable(frame, RSSYMtype, nil, 0);
	Boolean isService = EQRef(type, RSSYMservice);
	if (isService && label != kCMOServiceIdentifier)
	{
		// a service by its four characters: the 'sid ' option naming it
		// DEVIATION (pointer size): the host's TCMOServiceIdentifier, where
		// the ROM's is twelve bytes of header and two words
		TCMOServiceIdentifier* sid = (TCMOServiceIdentifier*) NewPtrClear(sizeof(TCMOServiceIdentifier));
		if (sid == nil)
			return nil;
		sid->SetLabel(kCMOServiceIdentifier);
		sid->SetAsService();
		sid->SetOpCode(opSetRequired);
		sid->SetLength(sizeof(TCMOServiceIdentifier) - sizeof(TOption));
		sid->fServiceId = label;
		sid->fPortId = 0;
		return sid;
	}

	RefVar data(GetVariable(frame, RSSYMdata, nil, 0));
	if (ISNIL(data))
	{
		option = (TOption*) NewPtrClear(sizeof(TOption));
		error = MemError();
		if (option == nil)
			goto done;
	}
	else
	{
		option = (TOption*) ParseOutput(sink, data, form, &error);
	}
	if (error == noErr)
	{
		option->SetLength(GetPtrSize((Ptr) option) - sizeof(TOption));
		option->SetOpCode(opSetNegotiate);
		Ref opCode = GetVariable(frame, RSSYMopcode, nil, 0);
		if (ISINT(opCode))
			option->SetOpCode(RINT(opCode));
		type = GetVariable(frame, RSSYMtype, nil, 0);
		if (EQRef(type, RSSYMservice))
			option->SetAsService(label);
		else if (EQRef(type, RSSYMoption))
			option->SetAsOption(label);
		else if (EQRef(type, RSSYMconfig))
			option->SetAsConfig(label);
		else if (EQRef(type, RSSYMaddress))
			option->SetAsAddress(label);
		else
			error = kCommScriptErrNotAnOption;
	}
	if (error == noErr)
	{
		// DEVIATION (pointer size): the device's layout into the host's, for
		// an option class with pointer-sized fields (HostOptionLayouts.h)
		option = HostOptionFromDevice(option);
		if (option == nil)
			error = MemError();
	}
	if (error == noErr && label == kCMOServiceIdentifier && option->Length() >= 8)
	{
		// DEVIATION: a 'sid ' option a script writes out itself is the
		// device's two big-endian longs (the service, the port); the host's
		// TCMOServiceIdentifier has them in host words
		TCMOServiceIdentifier* sid = (TCMOServiceIdentifier*) NewPtrClear(sizeof(TCMOServiceIdentifier));
		if (sid == nil)
			error = MemError();
		else
		{
			const UByte* bytes = (const UByte*) (option + 1);
			sid->SetLabel(kCMOServiceIdentifier);
			sid->SetAsService();
			sid->SetOpCode(option->GetOpCode());
			sid->SetLength(sizeof(TCMOServiceIdentifier) - sizeof(TOption));
			sid->fServiceId = GetBigEndianWord(bytes);
			sid->fPortId = GetBigEndianWord(bytes + 4);
			DisposPtr((Ptr) option);
			option = sid;
		}
	}

done:
	if (error != noErr && option != nil)
	{
		DisposPtr((Ptr) option);
		option = nil;
	}
	return option;
}


// ROM 0x0014c3ec ParseOutput__14POptionDataOutFP10PFrameSinkRC6RefVar8FormTypePl
// The data through the sink into a Ptr with an option's header in front of
// it (an error thrown by the sink is answered).
void*
POptionDataOut::ParseOutput(PFrameSink* sink, RefArg data, FormType form, long* error)
{
	void* result = nil;
	FrameSinkParms parms;
	parms.fValue = data;
	parms.fForm = form;
	parms.fEncoding = 1;
	parms.fUseHandle = false;
	parms.fHeaderSize = sizeof(TOption);		// DEVIATION (pointer size): the ROM's twelve
	newton_try
	{
		result = sink->Translate(&parms, nil);
	}
	newton_catch_all
	{
		*error = (long) (Long) CurrentException()->data;
	}
	end_try;
	return result;
}


/* -------------------------------------------------------------------------------
	POptionDataIn
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(POptionDataIn)
PROTOCOL_CLASSINFO(POptionDataIn, "PFrameSource", "", 0, 0, nil)	// ROM 0x0038a464 ClassInfo__13POptionDataInSFv

// ROM 0x0014b9fc New__13POptionDataInFv
POptionDataIn*	POptionDataIn::New()		{ return this; }
// ROM 0x0014ba00 Delete__13POptionDataInFv
void			POptionDataIn::Delete()		{ }


// ROM 0x0014ba04 Translate__13POptionDataInFPvP12PipeCallBack
// The option array's results and data written back into the frame(s),
// which are the answer (a failure thrown as evt.ex.translator).
Ref
POptionDataIn::Translate(void* context, PipeCallBack* callback)
{
	OptionDataParms* parms = (OptionDataParms*) context;
	if (parms == nil)
		return NILREF;
	long err = ConvertFromOptionArray(parms->fFrame, parms->fOptions, (PFrameSource*) parms->fXlator);
	if (err != noErr)
		Throw(exTranslatorException, (void*) (Long) err, nil);
	return parms->fFrame;
}


// ROM 0x0014ba60 ConvertFromOptionArray__13POptionDataInFRC6RefVarP12TOptionArrayP12PFrameSource
// A frame from the first option, or each frame of an array from the option
// at the same place.
long
POptionDataIn::ConvertFromOptionArray(RefArg frame, TOptionArray* options, PFrameSource* source)
{
	long err = noErr;
	if (options == nil)
		return noErr;
	if (!IsArray(frame))
		err = ConvertFromOption(frame, options->OptionAt(0), source);
	else
	{
		TOptionIterator iter(options);
		long slot = 0;
		TOption* option = iter.FirstOption();
		while (iter.More())
		{
			if (option != nil)
			{
				RefVar element(GetArraySlotRef(frame, slot));
				err = ConvertFromOption(element, option, source);
				if (err != noErr)
					break;
			}
			slot++;
			option = iter.NextOption();
		}
	}
	return err;
}


// ROM 0x0014bb78 ConvertFromOption__13POptionDataInFRC6RefVarP7TOptionP12PFrameSource
// The option's result into the frame's `result` slot (nil for success,
// else kCommScriptOptionResultBase + the signed result byte) and its data,
// read by the frame's form, into `data` (a 'template's by the typelist of
// the data already there, which must be one; no data frame: left alone).
long
POptionDataIn::ConvertFromOption(RefArg frame, TOption* option, PFrameSource* source)
{
	long err = noErr;
	newton_try
	{
		long status = (Long) option->GetOpCodeResults();
		SetFrameSlot(frame, RSSYMresult, (status == 0) ? RefVar() : RefVar(MAKEINT(status + kCommScriptOptionResultBase)));
		FormType form = GetDataForm(RefVar(GetVariable(frame, RSSYMform, nil, 0)), kFormUserOption);
		if (form == kFormNone)
			err = kCommScriptErrBadForm;
		RefVar data;
		// DEVIATION: a 'sid ' option is read back as the device's two
		// big-endian longs (the host keeps them in host words)
		UByte sidBytes[8];
		UByte* optionData = (UByte*) (option + 1);
		Size optionLength = option->Length();
		if (option->Label() == kCMOServiceIdentifier && optionLength >= (Size) (sizeof(TCMOServiceIdentifier) - sizeof(TOption)))
		{
			PutBigEndianWord(sidBytes, (unsigned int) ((TCMOServiceIdentifier*) option)->fServiceId);
			PutBigEndianWord(sidBytes + 4, (unsigned int) ((TCMOServiceIdentifier*) option)->fPortId);
			optionData = sidBytes;
			optionLength = 8;
		}
		// DEVIATION (pointer size): an option whose class has pointer-sized
		// fields on the host is read back in the device's layout
		// (HostOptionLayouts.h)
		UByte deviceBytes[64];
		long deviceLength = HostOptionToDevice(option, deviceBytes, sizeof(deviceBytes));
		if (deviceLength >= 0)
		{
			optionData = deviceBytes;
			optionLength = deviceLength;
		}
		if (err == noErr)
		{
			if (form == kFormTemplate)
			{
				data = GetVariable(frame, RSSYMdata, nil, 0);
				if (NOTNIL(data))
				{
					RefVar typelist(GetVariable(data, RSSYMtypelist, nil, 0));
					if (ISNIL(typelist) || !IsArray(typelist))
						err = kCommScriptErrBadTypelist;
					else
					{
						data = ParseInput(source, kFormTemplate, optionLength, optionData, typelist, &err);
						SetFrameSlot(frame, RSSYMdata, data);
					}
				}
			}
			else
			{
				RefVar noTypelist;
				data = ParseInput(source, form, optionLength, optionData, noTypelist, &err);
				SetFrameSlot(frame, RSSYMdata, data);
			}
		}
	}
	newton_catch_all
	{
		err = (long) (Long) CurrentException()->data;	}
	end_try;
	return err;
}


// ROM 0x0014bdc0 ParseInput__13POptionDataInFP12PFrameSource8FormTypelPUcRC6RefVarPl
// The option's data through the source (an error thrown by it answered).
Ref
POptionDataIn::ParseInput(PFrameSource* source, FormType form, long length, UByte* data, RefArg typelist, long* error)
{
	RefVar result;
	FrameSourceParms parms;
	parms.fData = data;
	parms.fLength = length;
	parms.fForm = form;
	parms.fEncoding = 1;
	parms.fTemplate = typelist;
	newton_try
	{
		result = source->Translate(&parms, nil);
	}
	newton_catch_all
	{
		*error = (long) (Long) CurrentException()->data;
	}
	end_try;
	return result;
}


/* -------------------------------------------------------------------------------
	The flatteners
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PFlattenPtr)
PROTOCOL_CLASSINFO(PFlattenPtr, "PFrameSink", "", 0, 0, nil)	// ROM 0x0038a010 ClassInfo__11PFlattenPtrSFv

// ROM 0x000caf08 New__11PFlattenPtrFv
PFlattenPtr*	PFlattenPtr::New()			{ return this; }
// ROM 0x000caf0c Delete__11PFlattenPtrFv
void			PFlattenPtr::Delete()		{ }


// ROM 0x000caf10 Translate__11PFlattenPtrFPvP12PipeCallBack
// The value as NSOF in a new block (a Ptr, or a Handle), fHeaderSize bytes
// in front of it.  ROM QUIRK: with any header at all the stream starts at
// offset 4, whatever the header's size.  DEVIATION: the block is NewPtr'd
// where the ROM mallocs (the host's malloc is not the pointer heap, and
// the block's users ask GetPtrSize of it).
void*
PFlattenPtr::Translate(void* context, PipeCallBack* callback)
{
	FlattenPtrParms* parms = (FlattenPtrParms*) context;
	if (parms == nil)
		return nil;
	Ptr block = nil;
	Handle blockHandle = nil;
	CPtrPipe pipe;
	TObjectWriter writer(parms->fValue, pipe, false);
	long size = parms->fHeaderSize + writer.Size();
	if (!parms->fUseHandle)
	{
		block = NewPtr(size);
		if (block == nil)
			Throw(exTranslatorException, (void*) (Long) MemError(), nil);
	}
	else
	{
		blockHandle = NewHandle(size);
		if (blockHandle == nil)
			Throw(exTranslatorException, (void*) (Long) MemError(), nil);
		HLock(blockHandle);
		block = *blockHandle;
	}
	pipe.Init(block, size, false, callback);
	if (parms->fHeaderSize > 0)
		pipe.WriteSeek(4, kSeekFromBeginningPos);
	newton_try
	{
		writer.Write();
	}
	newton_catch_all
	{
		if (blockHandle == nil)
		{
			if (block != nil)
				DisposPtr(block);
		}
		else
			DisposHandle(blockHandle);
		rethrow;
	}
	end_try;
	if (blockHandle != nil)
	{
		HUnlock(blockHandle);
		return blockHandle;
	}
	return block;
}


PROTOCOL_IMPL_SOURCE_MACRO(PUnFlattenPtr)
PROTOCOL_CLASSINFO(PUnFlattenPtr, "PFrameSource", "", 0, 0, nil)	// ROM 0x0038a088 ClassInfo__13PUnFlattenPtrSFv

// ROM 0x00256910 New__13PUnFlattenPtrFv
PUnFlattenPtr*	PUnFlattenPtr::New()		{ return this; }
// ROM 0x00256914 Delete__13PUnFlattenPtrFv
void			PUnFlattenPtr::Delete()		{ }


// ROM 0x00256918 Translate__13PUnFlattenPtrFPvP12PipeCallBack
// A value read back out of an NSOF block.
Ref
PUnFlattenPtr::Translate(void* context, PipeCallBack* callback)
{
	UnflattenPtrParms* parms = (UnflattenPtrParms*) context;
	RefVar result;
	if (parms != nil)
	{
		CPtrPipe pipe;
		pipe.Init(parms->fData, parms->fLength, false, callback);
		TObjectReader reader(pipe, parms->fStore);
		result = reader.Read();
	}
	return result;
}


PROTOCOL_IMPL_SOURCE_MACRO(PFlattenRef)
PROTOCOL_CLASSINFO(PFlattenRef, "PFrameSink", "", 0, 0, nil)	// ROM 0x0038a104 ClassInfo__11PFlattenRefSFv

// ROM 0x000cb0d4 New__11PFlattenRefFv
PFlattenRef*	PFlattenRef::New()			{ return this; }
// ROM 0x000cb0d8 Delete__11PFlattenRefFv
void			PFlattenRef::Delete()		{ }


// ROM 0x000cb0dc Translate__11PFlattenRefFPvP12PipeCallBack
// The value as NSOF in a new binary (on the store, if there is one); the
// answer is the binary's Ref.
void*
PFlattenRef::Translate(void* context, PipeCallBack* callback)
{
	FlattenRefParms* parms = (FlattenRefParms*) context;
	RefVar result;
	if (parms != nil)
	{
		CRefPipe pipe;
		TObjectWriter writer(parms->fValue, pipe, false);
		long size = writer.Size();
		pipe.InitSink(size, parms->fStore, callback);
		writer.Write();
		result = pipe.fBinary;
	}
	return (void*) (Ref) result;
}


PROTOCOL_IMPL_SOURCE_MACRO(PUnFlattenRef)
PROTOCOL_CLASSINFO(PUnFlattenRef, "PFrameSource", "", 0, 0, nil)	// ROM 0x0038a17c ClassInfo__13PUnFlattenRefSFv

// ROM 0x002569c4 New__13PUnFlattenRefFv
PUnFlattenRef*	PUnFlattenRef::New()		{ return this; }
// ROM 0x002569c8 Delete__13PUnFlattenRefFv
void			PUnFlattenRef::Delete()		{ }


// ROM 0x002569cc Translate__13PUnFlattenRefFPvP12PipeCallBack
// A value read back out of an NSOF binary (functions refused when asked).
Ref
PUnFlattenRef::Translate(void* context, PipeCallBack* callback)
{
	UnflattenRefParms* parms = (UnflattenRefParms*) context;
	RefVar result;
	if (parms != nil)
	{
		CRefPipe pipe;
		pipe.InitSource(parms->fBinary, callback);
		TObjectReader reader(pipe, parms->fStore);
		if (parms->fNoFunctions)
			reader.SetAllowFunctions(false);
		result = reader.Read();
	}
	return result;
}


PROTOCOL_IMPL_SOURCE_MACRO(PStreamInRef)
PROTOCOL_CLASSINFO(PStreamInRef, "PFrameSource", "", 0, 0, nil)	// ROM 0x0038a274 ClassInfo__12PStreamInRefSFv

// ROM 0x001fc4c8 New__12PStreamInRefFv
PStreamInRef*	PStreamInRef::New()			{ return this; }
// ROM 0x001fc4cc Delete__12PStreamInRefFv
void			PStreamInRef::Delete()		{ }


// ROM 0x001fc4d0 Translate__12PStreamInRefFPvP12PipeCallBack
// A value read as NSOF from the endpoint (0x200-byte receives).
Ref
PStreamInRef::Translate(void* context, PipeCallBack* callback)
{
	StreamRefParms* parms = (StreamRefParms*) context;
	RefVar result;
	if (parms != nil)
	{
		TEndpointPipe pipe;
		pipe.Init(parms->fEndpoint, 0x200, 0, parms->fTimeout, parms->fFraming, callback);
		TObjectReader reader(pipe, parms->fValue);
		result = reader.Read();
	}
	return result;
}


PROTOCOL_IMPL_SOURCE_MACRO(PStreamOutRef)
PROTOCOL_CLASSINFO(PStreamOutRef, "PFrameSink", "", 0, 0, nil)	// ROM 0x0038a1f8 ClassInfo__13PStreamOutRefSFv

// ROM 0x001fc5d8 New__13PStreamOutRefFv
PStreamOutRef*	PStreamOutRef::New()		{ return this; }
// ROM 0x001fc5dc Delete__13PStreamOutRefFv
void			PStreamOutRef::Delete()		{ }


// ROM 0x001fc5e0 Translate__13PStreamOutRefFPvP12PipeCallBack
// A value written as NSOF to the endpoint (0x200-byte sends), the callback
// told first how much it comes to.
void*
PStreamOutRef::Translate(void* context, PipeCallBack* callback)
{
	StreamRefParms* parms = (StreamRefParms*) context;
	if (parms != nil)
	{
		TEndpointPipe pipe;
		pipe.Init(parms->fEndpoint, 0, 0x200, parms->fTimeout, parms->fFraming, callback);
		TObjectWriter writer(parms->fValue, pipe, false);
		if (callback != nil)
			callback->fWriteTotal = writer.Size();
		writer.Write();
		pipe.FlushWrite();
	}
	return nil;
}


/* -------------------------------------------------------------------------------
	InitTranslators
------------------------------------------------------------------------------- */

// ROM 0x00256220 InitTranslators__Fv
// The translators into the protocol registry.
void
InitTranslators(void)
{
	if (gProtocolRegistry == nil)
		return;
	PFlattenPtr::ClassInfo()->Register();
	PUnFlattenPtr::ClassInfo()->Register();
	PFlattenRef::ClassInfo()->Register();
	PUnFlattenRef::ClassInfo()->Register();
	PStreamInRef::ClassInfo()->Register();
	PStreamOutRef::ClassInfo()->Register();
	PScriptDataIn::ClassInfo()->Register();
	PScriptDataOut::ClassInfo()->Register();
	POptionDataIn::ClassInfo()->Register();
	POptionDataOut::ClassInfo()->Register();
}
