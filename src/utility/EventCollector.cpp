/*
	File:		utility/EventCollector.cpp

	Contains:	TEventCollector and THistoryCollector - EventCollector.h.

	Reconstructed from the MP2x00 US ROM (0x002dc18c-0x002dc714); each
	function cites its origin.
*/

#include "EventCollector.h"
#include "hal/Atomic.h"
#include "hal/Timer.h"
#include "VirtualMemory.h"

#include <stdlib.h>
#include <string.h>

TEventCollectorDebuggerInfo*	gEventTraceBufArray[32];		// 0x0c105370

// the buffer's words are the machine's, four bytes
typedef unsigned int	TraceWord;


// DEVIATION: the host has no counter register to point at; its time is
// the low word of the tick count GetClock keeps from it
static TraceWord
TimerCounter(const ULong* source)
{
	Int64 now;
	GetClock(&now);
	return (TraceWord) now.lo;
}


// ROM 0x002dc18c InitEvents__Fv
void
InitEvents(void)
{
	THistoryCollector::ClassInfo()->Register();
}


/*------------------------------------------------------------------------------
	TEventCollector
------------------------------------------------------------------------------*/

TEventCollector*
TEventCollector::New(const char* implementation)
{
	TEventCollector* collector = (TEventCollector*) AllocInstanceByName("TEventCollector", implementation);
	return collector != nil ? (TEventCollector*) collector->GlueNew() : nil;
}


void
TEventCollector::Delete()
{
	GlueDelete();
}


// ROM 0x002dc360 Register__15TEventCollectorFv
// The collector's information in the first free slot for a debugger.
void
TEventCollector::Register()
{
	EnterFIQAtomic();
	for (int i = 0; i < 32; i++)
	{
		if (gEventTraceBufArray[i] == nil)
		{
			gEventTraceBufArray[i] = &fData;
			break;
		}
	}
	ExitFIQAtomic();
}


// ROM 0x002dc3a8 Deregister__15TEventCollectorFv
void
TEventCollector::Deregister()
{
	EnterFIQAtomic();
	for (int i = 0; i < 32; i++)
	{
		if (gEventTraceBufArray[i] == &fData)
		{
			gEventTraceBufArray[i] = nil;
			break;
		}
	}
	ExitFIQAtomic();
}


// ROM 0x002dc3f4 AddTime__15TEventCollectorFv
void
TEventCollector::AddTime()
{
	TraceWord* pos = (TraceWord*) fData.fCurrentPos;
	*pos++ = TimerCounter(nil);
	fData.fCurrentPos = (char*) pos;
}


/*------------------------------------------------------------------------------
	THistoryCollector
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(THistoryCollector)
PROTOCOL_CLASSINFO(THistoryCollector, "TEventCollector", "", 0, 0, nil)

// ROM 0x002dc418 New__17THistoryCollectorFv
THistoryCollector*
THistoryCollector::New()
{
	fBufferLimit = nil;
	fData.fDoCollect = 0;
	fData.fEventBuffer = nil;
	fData.fDataFormat = nil;
	fData.fName = nil;
	fTimeSource = nil;
	return this;
}


// ROM 0x002dc438 Delete__17THistoryCollectorFv
// Deregistered, the buffer unlocked and freed, the strings freed.
void
THistoryCollector::Delete()
{
	Deregister();
	if (fData.fEventBuffer != nil)
	{
		if (fResidence == eLockedBuffer || fResidence == eWiredBuffer)
		{
			UnlockHeapRange((VAddr) fData.fEventBuffer, (VAddr) (fData.fEventBuffer + fData.fBufferSize));
			UnlockHeapRange((VAddr) this, (VAddr) this + sizeof(THistoryCollector));
		}
		free(fData.fEventBuffer);
		fData.fEventBuffer = nil;
	}
	if (fData.fDataFormat != nil)
		free(fData.fDataFormat);
	fData.fDataFormat = nil;
	if (fData.fName != nil)
		free(fData.fName);
	fData.fName = nil;
}


// ROM 0x002dc4b4 Init__17THistoryCollectorFUiPcT2iT4
// A buffer of number entries - the time's word and the entry, rounded to
// a word (a one-byte entry shares the time's word) - locked or wired if
// asked, cleared; the format and the name copied; collecting, and
// registered.  (If a copy fails the buffer has no limit, so nothing is
// ever collected.)
void
THistoryCollector::Init(size_t entrySizeInBytes, char* printableFormat, char* collectionName, int number, int residence)
{
	fTimeSource = nil;						// (the ROM's 0x0f181800)
	fData.fActualDescCount = 0;
	for (int i = 0; i < kEventTraceDescMax; i++)
	{
		fData.fDescInfo[i].desc = nil;
		fData.fDescInfo[i].descCount = 0;
	}
	fEntrySizeAsked = entrySizeInBytes;
	if (entrySizeInBytes == 1)
		fData.fEntrySize = 0;
	else if ((entrySizeInBytes & 3) != 0)
		fData.fEntrySize = entrySizeInBytes + 4 - (entrySizeInBytes & 3);
	else
		fData.fEntrySize = entrySizeInBytes;
	fData.fNumberOfEntries = number;
	fData.fBufferSize = (fData.fEntrySize + 4) * number;
	fResidence = residence;
	fData.fEventBuffer = (char*) malloc(fData.fBufferSize);
	if (fData.fEventBuffer == nil)
		return;
	char* limit = fData.fEventBuffer + fData.fBufferSize;
	if (fResidence == eLockedBuffer || fResidence == eWiredBuffer)
	{
		LockHeapRange((VAddr) fData.fEventBuffer, (VAddr) limit, fResidence == eWiredBuffer);
		LockHeapRange((VAddr) this, (VAddr) this + sizeof(THistoryCollector), fResidence == eWiredBuffer);
	}
	fData.fDataFormat = (char*) malloc(strlen(printableFormat) + 1);
	if (fData.fDataFormat == nil)
		return;
	strcpy(fData.fDataFormat, printableFormat);
	fData.fName = (char*) malloc(strlen(collectionName) + 1);
	if (fData.fName == nil)
		return;
	strcpy(fData.fName, collectionName);
	fData.fCurrentPos = fData.fEventBuffer;
	if (fData.fCurrentPos != nil)
		for (ULong i = 0; i < fData.fBufferSize; i++)
			fData.fCurrentPos[i] = 0;
	fBufferLimit = limit;
	CollectionControl(eDoCollect);
	Register();
}


// ROM 0x002dc638 AddDescriptions__17THistoryCollectorFP19EventTraceCauseDesci
// ROM BUG: answers false even when the descriptions were added.
Boolean
THistoryCollector::AddDescriptions(EventTraceCauseDesc* list, int count)
{
	if (fData.fActualDescCount < kEventTraceDescMax)
	{
		fData.fDescInfo[fData.fActualDescCount].desc = list;
		fData.fDescInfo[fData.fActualDescCount].descCount = count;
		fData.fActualDescCount++;
	}
	return false;
}


// ROM 0x002dc31c CollectionControl__17THistoryCollectorFi
void
THistoryCollector::CollectionControl(int control)
{
	ULong flags = control;
	if (control == eResetCollect)
		fData.fCurrentPos = fData.fEventBuffer;
	if (control == eResetCollect || control == eDoCollect)
	{
		if (fBufferLimit == nil)
			flags = 0;
		else
		{
			flags = 6;
			if (fEntrySizeAsked >= 4)
				flags = 0x2e;
			if (fEntrySizeAsked == 4)
				flags += 0x10;
		}
	}
	fData.fDoCollect = flags;
}


// the position after an entry, back to the start at the limit
static inline char*
Wrap(char* pos, THistoryCollector* collector)
{
	return (pos == collector->fBufferLimit) ? collector->fData.fEventBuffer : pos;
}


// ROM 0x002dc670 Add__17THistoryCollectorFUc
// A byte: in the time's word for a one-byte collector, else a word of its
// own after it.
void
THistoryCollector::Add(unsigned char byteEventValue)
{
	if ((fData.fDoCollect & 4) == 0)
	{
		if (fData.fDoCollect & 1)
			AddReset(byteEventValue);
		return;
	}
	TraceWord* pos = (TraceWord*) fData.fCurrentPos;
	TraceWord time = TimerCounter(fTimeSource);
	if (fEntrySizeAsked == 1)
		*pos++ = (time & ~0xff) | byteEventValue;
	else
	{
		*pos++ = time | 1;
		*pos = byteEventValue;
		pos = (TraceWord*) ((char*) pos + fData.fEntrySize);
	}
	fData.fCurrentPos = Wrap((char*) pos, this);
}


// ROM 0x002dc6dc AddReset__17THistoryCollectorFUc
void
THistoryCollector::AddReset(unsigned char byteEventValue)
{
	CollectionControl(eResetCollect);
	if (fData.fDoCollect & 4)
		Add(byteEventValue);
}


// ROM 0x002dc1a4 Add__17THistoryCollectorFUl
void
THistoryCollector::Add(unsigned long longEventValue)
{
	if ((fData.fDoCollect & 0x10) == 0)
	{
		if (fData.fDoCollect & 1)
			AddReset(longEventValue);
		return;
	}
	TraceWord* pos = (TraceWord*) fData.fCurrentPos;
	*pos++ = TimerCounter(fTimeSource) | 1;
	*pos++ = (TraceWord) longEventValue;
	fData.fCurrentPos = Wrap((char*) pos, this);
}


// ROM 0x002dc1e8 AddReset__17THistoryCollectorFUl
void
THistoryCollector::AddReset(unsigned long longEventValue)
{
	CollectionControl(eResetCollect);
	if (fData.fDoCollect & 0x10)
		Add(longEventValue);
}


// ROM 0x002dc220 Add__17THistoryCollectorFPCv
// An entry's bytes after the time's word.
void
THistoryCollector::Add(const void* event)
{
	if ((fData.fDoCollect & 8) == 0)
	{
		if (fData.fDoCollect & 1)
			AddReset(event);
		return;
	}
	char* pos = fData.fCurrentPos;
	fData.fCurrentPos = Wrap(pos + fData.fEntrySize + 4, this);
	*(TraceWord*) pos = TimerCounter(fTimeSource) | 1;
	memcpy(pos + 4, event, fEntrySizeAsked);
}


// ROM 0x002dc274 AddReset__17THistoryCollectorFPCv
void
THistoryCollector::AddReset(const void* event)
{
	CollectionControl(eResetCollect);
	if (fData.fDoCollect & 8)
		Add(event);
}


// ROM 0x002dc2ac AddAddress__17THistoryCollectorFv
// The time and the caller's address (AsmTraceAddAddrEvent).
void
THistoryCollector::AddAddress(void)
{
	if ((fData.fDoCollect & 0x20) == 0)
	{
		if (fData.fDoCollect & 1)
			AddAddressReset();
		return;
	}
	char* pos = fData.fCurrentPos;
	fData.fCurrentPos = Wrap(pos + fData.fEntrySize + 4, this);
	// DEVIATION: AsmTraceAddAddrEvent is assembly that stores the time and
	// the link register; the host stores the low word of its own return
	// address
	((TraceWord*) pos)[0] = TimerCounter(fTimeSource);
	((TraceWord*) pos)[1] = (TraceWord) (Long) __builtin_return_address(0);
}


// ROM 0x002dc2ec AddAddressReset__17THistoryCollectorFv
void
THistoryCollector::AddAddressReset(void)
{
	CollectionControl(eResetCollect);
	if (fData.fDoCollect & 0x20)
		AddAddress();
}
