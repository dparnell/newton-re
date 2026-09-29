/*
	File:		comms/CommTrace.cpp

	Contains:	The communications' trace frame and translate natives:
				cfinstantiate, cfrecord and Dispose (CFInstantiate,
				CFRecord, CFDispose), which keep a THistoryCollector
				(utility/EventCollector.h) for a frame - its name, and its
				traceElements' trace strings as the descriptions of the
				causes - and record (cause, byte) events in it; and
				translate(data, translator, options), which flattens a
				frame into a binary or unflattens one through the
				PFlattenRef/PUnFlattenRef translators (comms/Translators.h).

	Reconstructed from the MP2x00 US ROM (0x0013a010-0x0013acc8); each
	function cites its origin.
*/

#include "Translators.h"
#include "EventCollector.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"

#include <stdlib.h>
#include <string.h>

extern const ExceptionName exCommException;
extern const ExceptionName exTranslatorException;

// DEVIATION (pointer size): the ROM's table is a count word and 8-byte
// (cause, string) pairs; the host's pairs are EventTraceCauseDesc, which
// hold a pointer
struct TraceTable
{
	long					fCount;
	EventTraceCauseDesc		fEntries[1];
};


// ROM 0x0013a010 CFDispose
// The collector deleted and the trace strings freed; the frame's two
// private slots cleared.
Ref
CFDispose(RefArg rcvr)
{
	TEventCollector* collector = nil;
	TraceTable* table = nil;
	if (NOTNIL(rcvr))
	{
		RefVar value(GetVariable(rcvr, RSSYMprivateeventcollector, nil, 0));
		if (NOTNIL(value))
			collector = (TEventCollector*) RefToAddress(value);
		value = GetVariable(rcvr, RSSYMprivatetraceevents, nil, 0);
		if (NOTNIL(value))
			table = (TraceTable*) RefToAddress(value);
	}
	SetFrameSlot(rcvr, RSSYMprivateeventcollector, RefVar(NILREF));
	SetFrameSlot(rcvr, RSSYMprivatetraceevents, RefVar(NILREF));
	if (collector != nil)
		collector->Delete();
	if (table != nil)
	{
		for (long i = 0; i < table->fCount; i++)
			if (table->fEntries[i].description != nil)
				free(table->fEntries[i].description);
		free(table);
	}
	return NILREF;
}


// ROM 0x0013a8cc CFInstantiate
// A history collector of 128 (cause, byte) entries named by the frame's
// name, with a description for each of its traceElements that has a trace
// string; kept in the frame's privateEventCollector, and the strings in
// privateTraceEvents.
Ref
CFInstantiate(RefArg rcvr)
{
	char* collectionName = nil;
	NewtonErr err = noErr;		// (never set: neither the Dispose nor the Throw below happens)
	if (NOTNIL(rcvr))
	{
		RefVar name(GetVariable(rcvr, RSSYMname, nil, 0));
		long length = Ustrlen(GetCString(name));
		collectionName = (char*) malloc(length + 1);
		if (collectionName != nil)
			ConvertFromUnicode(GetCString(name), collectionName, 1, length + 1);
		RefVar elements(GetVariable(rcvr, RSSYMtraceelements, nil, 0));
		ArrayIndex count = Length(elements);
		// ROM BUG: only the elements with a trace string are entered, but
		// the collector is given the whole count, and CFDispose frees
		// every entry's string.  DEVIATION: the host's table is cleared,
		// so an entry not filled in has none (the ROM's is heap rubbish).
		TraceTable* table = (TraceTable*) calloc(1, sizeof(long) + count * sizeof(EventTraceCauseDesc));
		// ROM BUG: the count is written before the table is checked
		table->fCount = count;
		long entry = 0;
		if (table != nil)
		{
			for (ArrayIndex slot = 0; slot < count; slot++)
			{
				RefVar element(GetArraySlotRef(elements, slot));
				RefVar trace(GetVariable(element, RSSYMtrace, nil, 0));
				if (NOTNIL(trace))
				{
					long traceLength = Ustrlen(GetCString(trace)) + 1;
					char* description = (char*) malloc(traceLength);
					if (description != nil)
					{
						ConvertFromUnicode(GetCString(trace), description, 1, traceLength);
						table->fEntries[entry].cause = slot;
						table->fEntries[entry].description = description;
						entry++;
					}
				}
			}
		}
		TEventCollector* collector;
		if (collectionName == nil || table == nil
		||  (collector = TEventCollector::New(kTHistoryCollector)) == nil)
		{
			if (err != noErr)
				CFDispose(rcvr);
		}
		else
		{
			collector->Init(4, (char*) "\t%bd %bx", collectionName, 0x80, 0);
			collector->AddDescriptions(table->fEntries, count);
			SetFrameSlot(rcvr, RSSYMprivateeventcollector, RefVar(AddressToRef(collector)));
			SetFrameSlot(rcvr, RSSYMprivatetraceevents, RefVar(AddressToRef(table)));
			err = noErr;
		}
	}
	free(collectionName);
	if (err != noErr)
		Throw(exCommException, (void*) (Long) err, nil);
	return NILREF;
}


// ROM 0x0013ab94 CFRecord
// An event: the cause (an integer) in the top byte and the data's byte
// (an integer's low byte, a character's, 0xff for anything else but nil)
// in the next.
Ref
CFRecord(RefArg rcvr, RefArg cause, RefArg data)
{
	TEventCollector* collector = nil;
	if (NOTNIL(rcvr))
	{
		RefVar value(GetVariable(rcvr, RSSYMprivateeventcollector, nil, 0));
		if (NOTNIL(value))
			collector = (TEventCollector*) RefToAddress(value);
	}
	if (ISINT(cause))
	{
		ULong byte = 0;
		if (ISINT(data))
			byte = RINT(data) & 0xff;
		else if (ISCHAR(data))
		{
			UniChar c[1] = { RCHAR(data) };
			UByte bytes[4];
			ConvertFromUnicode(c, bytes, 1, 1);
			byte = bytes[0];
		}
		else if (NOTNIL(data))
			byte = 0xff;
		// ROM BUG: a frame with no collector is recorded through nil (a
		// data abort on the MessagePad).  DEVIATION: the host does nothing.
		if (collector != nil)
			collector->Add((unsigned long) ((RINT(cause) << 24) + (byte << 16)));
	}
	return NILREF;
}


// ROM 0x0013a240 FTranslate
// translate(data, translator, store, callback): 'flattener a frame into a binary,
// 'unflattener or 'unflattenNoCode a binary back (the second refusing
// functions); the store the binary goes on or comes from (nil: the heap);
// the callback is not used.  A translator's exception is
// thrown as evt.ex.comm with its error; so is a wrong translator or data
// (-54001).  ROM QUIRK: after a translation that went well the memory
// manager's last error is thrown if there is one.
Ref
FTranslate(RefArg rcvr, RefArg data, RefArg translator, RefArg options, RefArg callback)
{
	NewtonErr err = noErr;
	Boolean flatten = false;
	Boolean noCode = false;
	RefVar result(NILREF);
	if (EQRef(translator, RSSYMflattener) && IsFrame(data))
		flatten = true;
	else if (EQRef(translator, RSSYMunflattener) && IsBinary(data))
		;
	else if (EQRef(translator, RSSYMunflattennocode) && IsBinary(data))
		noCode = true;
	else
		err = kCommScriptErrBadForm;
	if (err == noErr)
	{
		if (flatten)
		{
			PFrameSink* sink = (PFrameSink*) NewByName("PFrameSink", "PFlattenRef");
			if (sink != nil)
			{
				FlattenRefParms parms;
				parms.fValue = data;
				parms.fStore = options;
				newton_try
				{
					unwind_protect
					{
						result = (Ref) (Long) sink->Translate(&parms, nil);
					}
					on_unwind
					{ }
					sink->Delete();
					end_unwind;
				}
				newton_catch(exTranslatorException)
				{
					err = (NewtonErr) (Long) CurrentException()->data;
				}
				end_try;
			}
		}
		else
		{
			PFrameSource* source = (PFrameSource*) NewByName("PFrameSource", "PUnFlattenRef");
			if (source != nil)
			{
				UnflattenRefParms parms;
				parms.fBinary = data;
				parms.fStore = options;
				parms.fNoFunctions = noCode;
				newton_try
				{
					unwind_protect
					{
						result = source->Translate(&parms, nil);
					}
					on_unwind
					{ }
					source->Delete();
					end_unwind;
				}
				newton_catch(exTranslatorException)
				{
					err = (NewtonErr) (Long) CurrentException()->data;
				}
				end_try;
			}
		}
		if (err == noErr)
			err = MemError();
	}
	if (err != noErr)
		Throw(exCommException, (void*) (Long) err, nil);
	return result;
}


void
RegisterCommTraceNatives(void)
{
	RegisterNativeFunction("CFDispose", (void*) CFDispose, 0);
	RegisterNativeFunction("CFInstantiate", (void*) CFInstantiate, 0);
	RegisterNativeFunction("CFRecord", (void*) CFRecord, 2);
	RegisterNativeFunction("FTranslate", (void*) FTranslate, 4);
}
