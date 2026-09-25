/*
	File:		user/NameServer.cpp

	Contains:	The name server task (NameServerImpl.h): a TUTaskWorld serving
				the TNameServerRequests TUNameServer, TSystemEvent and
				TUGestalt send to the name server port.

				Names live in 16 hash buckets (TObjectNameList) of
				TObjectNameEntry; each bucket also queues the callers waiting
				for one of its names to be registered or unregistered, who
				are replied to when it happens.  System events are delivered
				one registrant at a time with an asynchronous send collected
				back on the server's own port; the sender is replied to when
				every registrant has had the event.  Gestalt answers from the
				kernel's globals.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
	Resource arbitration (the comm tools' claim/unclaim protocol,
	0x0012fa3c-0x001300f4) is NOT YET RECONSTRUCTED.
*/

#include "NameServerImpl.h"
#include "SystemEvents.h"
#include "NewtonGestalt.h"
#include "GestaltSources.h"
#include "NewtQD.h"
#include "UserGlobals.h"
#include "KernelGlobals.h"
#include "VirtualMemory.h"
#include "List.h"
#include "ListIterator.h"
#include "SortedList.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <string.h>

// a content pointer of all ones (kPortSend_BufferAlreadySet) means "leave the
// message's buffer as it is" (UserPorts.cpp)
static void* const kNoContent = (void*) (uintptr_t) (ULong) kPortSend_BufferAlreadySet;

// the message type TSendSystemEvent sends with; while an event is being
// delivered the server takes no other
const ULong kSysEventMsgType = 1;


/* -------------------------------------------------------------------------------
	TObjectNameList
------------------------------------------------------------------------------- */

// ROM 0x0012f03c InitNameServer__Fv +0x58 (__vec_new of the 16 lists with the
// constructor at 0x00381794, unnamed in the symbol table)
TObjectNameList::TObjectNameList()
{
	fEntries = nil;
	fRegisterWaiters = nil;
	fUnregisterWaiters = nil;
}


// ROM 0x0012f100 Add__15TObjectNameListFPcT1UlT3
// A new entry (taking the strings) goes at the head; every caller waiting
// for that name and type to be registered gets the (thing, spec) and is
// dropped from the queue.  False when the entry cannot be made.
Boolean
TObjectNameList::Add(char* name, char* type, ULong thing, ULong spec)
{
	TObjectNameEntry* entry = new TObjectNameEntry;
	if (entry == nil)
		return false;
	entry->fNext = nil;
	entry->fName = nil;
	entry->fType = nil;
	entry->fResArbInfo = nil;
	entry->fType = type;
	entry->fName = name;
	entry->fThing = thing;
	entry->fSpec = spec;
	entry->fNext = fEntries;
	fEntries = entry;

	TNameServerQueuedRequest* prev = nil;
	TNameServerQueuedRequest* waiter = fRegisterWaiters;
	while (waiter != nil)
	{
		if (strcmp(waiter->fName, name) == 0 && strcmp(waiter->fType, type) == 0)
		{
			TNameServerReply reply;
			reply.fThing = entry->fThing;
			reply.fSpec = entry->fSpec;
			waiter->fToken.ReplyRPC(&reply, sizeof(reply), noErr);
			if (prev == nil)
				fRegisterWaiters = waiter->fNext;
			else
				prev->fNext = waiter->fNext;
			TNameServerQueuedRequest* next = waiter->fNext;
			DisposPtr(waiter->fName);
			DisposPtr(waiter->fType);
			delete waiter;
			waiter = next;
		}
		else
		{
			prev = waiter;
			waiter = waiter->fNext;
		}
	}
	return true;
}


// ROM 0x0012f56c Remove__15TObjectNameListFPcT1
// Drops the first entry of that name (NOTE: the type is not compared - a
// ROM quirk); a resource claim on it is ended (or, with a claim
// notification outstanding, marked to end when that returns).  Every
// caller waiting for the name and type to be unregistered is replied to
// and dropped.  False if no entry has the name.
Boolean
TObjectNameList::Remove(char* name, char* type)
{
	TObjectNameEntry* prev = nil;
	TObjectNameEntry* entry = fEntries;
	for (;;)
	{
		if (entry == nil)
			return false;
		if (strcmp(entry->fName, name) == 0)
			break;
		prev = entry;
		entry = entry->fNext;
	}
	if (prev == nil)
		fEntries = entry->fNext;
	else
		prev->fNext = entry->fNext;

	if (entry->fResArbInfo != nil)
	{
		// NOT YET RECONSTRUCTED: TResArbitrationInfo (0x00130c7c-): with its
		// kResArb_NotificationPending bit (1) clear the record is destroyed,
		// otherwise its kResArb_Removed bit (2) is set for ResArbHandleReply
	}

	TNameServerQueuedRequest* prevWaiter = nil;
	TNameServerQueuedRequest* waiter = fUnregisterWaiters;
	while (waiter != nil)
	{
		if (strcmp(waiter->fName, name) == 0 && strcmp(waiter->fType, type) == 0)
		{
			TNameServerReply reply;
			waiter->fToken.ReplyRPC(&reply, sizeof(reply), noErr);
			if (prevWaiter == nil)
				fUnregisterWaiters = waiter->fNext;
			else
				prevWaiter->fNext = waiter->fNext;
			TNameServerQueuedRequest* next = waiter->fNext;
			DisposPtr(waiter->fName);
			DisposPtr(waiter->fType);
			delete waiter;
			waiter = next;
		}
		else
		{
			prevWaiter = waiter;
			waiter = waiter->fNext;
		}
	}
	DisposPtr(entry->fName);
	DisposPtr(entry->fType);
	delete entry;
	return true;
}


// ROM 0x0012fef8 Lookup__15TObjectNameListFPcT1PUlT3PP16TObjectNameEntry
Boolean
TObjectNameList::Lookup(char* name, char* type, ULong* thing, ULong* spec, TObjectNameEntry** entry)
{
	TObjectNameEntry* i = fEntries;
	for (;;)
	{
		if (i == nil)
			return false;
		if (strcmp(i->fName, name) == 0 && strcmp(i->fType, type) == 0)
			break;
		i = i->fNext;
	}
	*thing = i->fThing;
	*spec = i->fSpec;
	if (entry != nil)
		*entry = i;
	return true;
}


/* -------------------------------------------------------------------------------
	System event registrations
------------------------------------------------------------------------------- */

// ROM 0x00130f6c TestItem__20SysEventItemComparerCFPCv
// Registrants are ordered by port id.
CompareResult
SysEventItemComparer::TestItem(const void* criteria) const
{
	TObjectId item = ((const SysEventRegistrant*) fItem)->fPortId;
	TObjectId other = ((const SysEventRegistrant*) criteria)->fPortId;
	if (item < other)
		return kItemLessThanCriteria;
	if (item == other)
		return kItemEqualCriteria;
	return kItemGreaterThanCriteria;
}


// ROM 0x00130f90 __ct__14SysEventTesterFUl
SysEventTester::SysEventTester(SystemEvent event)
{
	fEvent = event;
}


// ROM 0x00130fc8 TestItem__14SysEventTesterCFPCv
CompareResult
SysEventTester::TestItem(const void* item) const
{
	return ((const EventMasterListItem*) item)->fEvent == fEvent ? kItemEqualCriteria : kItemLessThanCriteria;
}


// ROM 0x00131030 RegisterForSystemEvent__11TNameServerFUlN31 +0x28 (the constructor is inlined there)
EventMasterListItem::EventMasterListItem()
{
	fRegistrants = nil;
}


// ROM 0x00130f00 __dt__19EventMasterListItemFv
EventMasterListItem::~EventMasterListItem()
{
	if (fRegistrants != nil)
		delete fRegistrants;
}


// ROM 0x00130f38 Init__19EventMasterListItemFv
NewtonErr
EventMasterListItem::Init()
{
	fRegistrants = new CSortedList(&fComparer);
	return fRegistrants != nil ? noErr : kError_No_Memory;
}


/* -------------------------------------------------------------------------------
	TNameServer
------------------------------------------------------------------------------- */

// ROM 0x0012f03c InitNameServer__Fv +0x1c (the constructor is inlined there)
TNameServer::TNameServer()
{
	fSysEventList = nil;
	fSysEventIter = nil;
	fName = nil;
	fType = nil;
}


// ROM 0x001300f4 TaskConstructor__11TNameServerFv
// In the new task: the port (which becomes the well-known name server port),
// the reply memory for system events, the event list.
long
TNameServer::TaskConstructor()
{
	fSysEventIter = nil;
	fSysEventList = nil;
	fRPCInfo.fType = kRPCInfo_SysEvent;
	long err = fPort.Init();
	if (err != noErr)
		return err;
	err = fSysEventReplyMem.Init();
	if (err != noErr)
		return err;
	gNameServer = &fPort;
	fSysEventList = new CList;
	return fSysEventList != nil ? noErr : kError_No_Memory;
}


// ROM 0x0012f3f0 Hash__11TNameServerFPc
// The bucket: the byte sum of the name, mod 16.
ULong
TNameServer::Hash(char* name)
{
	unsigned char sum = 0;
	for (int i = 0; name[i] != 0; i++)
		sum += name[i];
	return sum & (kNumLists - 1);
}


// ROM 0x0012f424 BuildNameAndType__11TNameServerFUlT1
// Copies the request's name and type strings out of the caller's shared
// memory objects into fName and fType.
NewtonErr
TNameServer::BuildNameAndType(TObjectId nameId, TObjectId typeId)
{
	TUSharedMem nameMem, typeMem;
	fName = nil;
	fType = nil;
	nameMem.CopyObject(nameId);
	typeMem.CopyObject(typeId);
	ULong size;
	NewtonErr err = nameMem.GetSize(&size, nil);
	if (err == noErr)
	{
		fName = NewPtr(size);		// the ROM's malloc is NewPtr
		if (fName == nil)
			return kError_No_Memory;
		err = nameMem.CopyFromShared(&size, fName, size, 0, nil);
	}
	if (err == noErr && (err = typeMem.GetSize(&size, nil)) == noErr)
	{
		fType = NewPtr(size);
		err = kError_No_Memory;
		if (fType != nil)
			err = typeMem.CopyFromShared(&size, fType, size, 0, nil);
	}
	return err;
}


// ROM 0x0012f548 DeleteNameAndType__11TNameServerFv
// (both strings are freed; the ROM inlines DisposPtr for the second)
void
TNameServer::DeleteNameAndType()
{
	DisposPtr(fName);
	if (fType != nil)
		DisposPtr(fType);
}


// ROM 0x0012f6b8 RegisterName__11TNameServerFUlT1
// The entry takes the strings, so they are not to be freed by DeleteNameAndType.
NewtonErr
TNameServer::RegisterName(ULong thing, ULong spec)
{
	ULong oldThing, oldSpec;
	if (fLists[Hash(fName)].Lookup(fName, fType, &oldThing, &oldSpec, nil))
		return kError_Already_Registered;
	if (!fLists[Hash(fName)].Add(fName, fType, thing, spec))
		return kError_Already_Registered;		// (sic: the ROM answers this for "no memory" too)
	fName = nil;
	fType = nil;
	return noErr;
}


// ROM 0x0012f768 UnRegisterName__11TNameServerFv
NewtonErr
TNameServer::UnRegisterName()
{
	ULong thing, spec;
	if (!fLists[Hash(fName)].Lookup(fName, fType, &thing, &spec, nil))
		return kError_Not_Registered;
	if (!fLists[Hash(fName)].Remove(fName, fType))
		return kError_Not_Registered;
	return noErr;
}


// ROM 0x0012f7f8 QueueForRegister__11TNameServerFP10TUMsgToken
// A name already registered is answered at once; otherwise the caller (its
// token) waits in the bucket, which takes the strings.
NewtonErr
TNameServer::QueueForRegister(TUMsgToken* token)
{
	ULong thing, spec;
	if (Lookup(&thing, &spec) == noErr)
	{
		TNameServerReply reply;
		reply.fThing = thing;
		reply.fSpec = spec;
		token->ReplyRPC(&reply, sizeof(reply), noErr);
		return noErr;
	}
	TNameServerQueuedRequest* waiter = new TNameServerQueuedRequest;
	if (waiter == nil)
		return kError_No_Memory;
	waiter->fNext = nil;
	waiter->fName = fName;
	waiter->fType = fType;
	waiter->fToken = *token;
	TObjectNameList& list = fLists[Hash(fName)];
	waiter->fNext = list.fRegisterWaiters;
	list.fRegisterWaiters = waiter;
	fName = nil;
	fType = nil;
	return noErr;
}


// ROM 0x0012f8fc QueueForUnregister__11TNameServerFP10TUMsgToken
// The counterpart: a name not registered is an error at once.
NewtonErr
TNameServer::QueueForUnregister(TUMsgToken* token)
{
	ULong thing, spec;
	if (Lookup(&thing, &spec) != noErr)
	{
		token->ReplyRPC(nil, 0, kError_Not_Registered);
		return noErr;
	}
	TNameServerQueuedRequest* waiter = new TNameServerQueuedRequest;
	if (waiter == nil)
		return kError_No_Memory;
	waiter->fNext = nil;
	waiter->fName = fName;
	waiter->fType = fType;
	waiter->fToken = *token;
	TObjectNameList& list = fLists[Hash(fName)];
	waiter->fNext = list.fUnregisterWaiters;
	list.fUnregisterWaiters = waiter;
	fName = nil;
	fType = nil;
	return noErr;
}


// ROM 0x0012f9e4 Lookup__11TNameServerFPUlT1
NewtonErr
TNameServer::Lookup(ULong* thing, ULong* spec)
{
	if (!fLists[Hash(fName)].Lookup(fName, fType, thing, spec, nil))
		return kError_Not_Registered;
	return noErr;
}


// ROM 0x00131030 RegisterForSystemEvent__11TNameServerFUlN31
// The event's master item (made on first use) gets the port; a port already
// there is kError_Already_Registered, and an item left with no registrants
// is dropped again.
NewtonErr
TNameServer::RegisterForSystemEvent(SystemEvent event, TObjectId portId, ULong timeout, ULong sendFilter)
{
	SysEventTester tester(event);
	ArrayIndex index;
	EventMasterListItem* item = (EventMasterListItem*) fSysEventList->Search(&tester, index);
	NewtonErr err;
	if (item == nil)
	{
		item = new EventMasterListItem;
		if (item == nil)
			return kError_No_Memory;
		if ((err = item->Init()) != noErr)
		{
			delete item;
			return err;
		}
		item->fEvent = event;
		if ((err = fSysEventList->InsertAt(fSysEventList->Count(), item)) != noErr)
		{
			delete item;
			return err;
		}
	}
	SysEventRegistrant* registrant = new SysEventRegistrant;
	if (registrant == nil)
		err = kError_No_Memory;
	else
	{
		registrant->fTimeout = timeout;
		registrant->fSendFilter = sendFilter;
		registrant->fPortId = portId;
		if (item->fRegistrants->InsertUnique(registrant))
			return noErr;
		delete registrant;
		err = kError_Already_Registered;
	}
	if (item->fRegistrants->Count() != 0)
		return err;
	fSysEventList->Remove(item);
	delete item;
	return err;
}


// ROM 0x00131154 UnRegisterForSystemEvent__11TNameServerFUlT1
NewtonErr
TNameServer::UnRegisterForSystemEvent(SystemEvent event, TObjectId portId)
{
	SysEventTester tester(event);
	ArrayIndex index;
	EventMasterListItem* item = (EventMasterListItem*) fSysEventList->Search(&tester, index);
	NewtonErr err = kError_Not_Registered;
	if (item != nil)
	{
		SysEventRegistrant key;
		key.fPortId = portId;
		item->fComparer.SetTestItem(&key);
		SysEventRegistrant* registrant = (SysEventRegistrant*) item->fRegistrants->Search(&item->fComparer, index);
		if (registrant != nil)
		{
			err = item->fRegistrants->Remove(registrant);
			delete registrant;
			if (item->fRegistrants->Count() == 0)
			{
				fSysEventList->Remove(item);
				delete item;
			}
		}
	}
	return err;
}


// ROM 0x0013120c SendSystemEvent__11TNameServerFUlT1
// Starts delivering: the sender's message (its shared memory holds the
// event) goes to the first registrant that accepts an asynchronous send,
// collected back on our port with fRPCInfo as its refcon; TaskMain carries
// on from there.  kError_Not_Registered when nobody is registered or nobody
// could be sent to.
NewtonErr
TNameServer::SendSystemEvent(SystemEvent event, TObjectId msgId)
{
	SysEventTester tester(event);
	ArrayIndex index;
	EventMasterListItem* item = (EventMasterListItem*) fSysEventList->Search(&tester, index);
	if (item == nil || item->fRegistrants->Count() == 0)
		return kError_Not_Registered;
	fSysEventIter = new CListIterator(item->fRegistrants);
	if (fSysEventIter == nil)
		return kError_No_Memory;
	{
		TUAsyncMessage msg(msgId, fSysEventReplyMem);
		fSysEventMsg = msg;
	}
	fSysEventMsg.SetCollectorPort(fPort);
	fSysEventMsg.SetUserRefCon((ULong) &fRPCInfo);
	for (SysEventRegistrant* r = (SysEventRegistrant*) fSysEventIter->FirstItem(); r != nil; r = (SysEventRegistrant*) fSysEventIter->NextItem())
	{
		fSysEventPort.CopyObject(r->fPortId);
		if (fSysEventPort.SendRPC(&fSysEventMsg, kNoContent, 0, nil, 0, r->fTimeout, nil, r->fSendFilter) == noErr)
			return noErr;
	}
	delete fSysEventIter;
	fSysEventIter = nil;
	return kError_Not_Registered;
}


// ROM 0x0012fa3c ResourceArbitration__11TNameServerFR10TUMsgTokenP22TResArbitrationRequest
void
TNameServer::ResourceArbitration(TUMsgToken* token, TResArbitrationRequest* /*request*/)
{
	// NOT YET RECONSTRUCTED: the comm tools' resource claim protocol
	// (kResArbitrationClaim/Unclaim/PassiveClaim/PassiveUnclaim over
	// TResArbitrationInfo/TResOwnerInfo and TCommToolResArbRequest,
	// 0x00131498-0x00131b50)
	TNameServerReply reply;
	reply.fResult = kError_Call_Not_Implemented;
	token->ReplyRPC(&reply, sizeof(reply), noErr);
}


// ROM 0x00130014 ResArbHandleReply__11TNameServerFP19TResArbitrationInfo
void
TNameServer::ResArbHandleReply(TResArbitrationInfo* /*info*/)
{
	// NOT YET RECONSTRUCTED: see ResourceArbitration
}


// host: see GestaltSources.h
long	(*gGestaltGrafInfo)(long selector, void* info) = nil;
void	(*gGestaltTabletResolution)(long* x, long* y) = nil;


// ROM 0x00131b54 Gestalt__11TNameServerFUlP10TUMsgToken
// The machine's answers to the gestalt selectors (NewtonGestalt.h).
void
TNameServer::Gestalt(ULong selector, TUMsgToken* token)
{
	union
	{
		TGestaltVersion				version;
		TGestaltRebootInfo			rebootInfo;
		TGestaltNewtonScriptVersion	nsVersion;
		TGestaltPatchInfo			patchInfo;
		TGestaltPCMCIAInfo			pcmciaInfo;
		struct { TGestaltSystemInfo info; ULong fManufDate; } systemInfo;	// the ROM answers 0x3c bytes: the date of manufacture after the class's fields
	} info;
	ULong size;
	long result = noErr;
	switch (selector)
	{
	case kGestalt_Version:
		info.version.fVersion = 1;
		size = sizeof(info.version);
		break;

	case kGestalt_SystemInfo:
	{
		// the machine (an MP2x00, ROM 2.2 stage 0x8000), the screen's size
		// out of its pixel map's bounds, its resolution, its depth, and the
		// tablet's resolution.  NOT YET RECONSTRUCTED: the RAM size
		// (InternalRAMInfo), the patch version (GetPatchInfo), gMainCPUType,
		// gMainCPUClockSpeed and gManufDate, which answer nought.
		memset(&info.systemInfo, 0, sizeof(info.systemInfo));
		info.systemInfo.info.fManufacturer = kGestalt_Manufacturer_Apple;
		info.systemInfo.info.fMachineType = 0x10003000;
		info.systemInfo.info.fROMVersion = 0x20002;
		info.systemInfo.info.fROMStage = 0x8000;
		if (gGestaltGrafInfo != nil)
		{
			PixelMap screen;
			gGestaltGrafInfo(0, &screen);
			info.systemInfo.info.fScreenHeight = (ULong) (long) screen.bounds.bottom;
			info.systemInfo.info.fScreenWidth = (ULong) (long) screen.bounds.right;
			Point resolution;
			gGestaltGrafInfo(1, &resolution);
			info.systemInfo.info.fScreenResolution = resolution;
			info.systemInfo.info.fScreenDepth = screen.pixMapFlags & 0xff;
		}
		if (gGestaltTabletResolution != nil)
		{
			long x, y;
			gGestaltTabletResolution(&x, &y);
			info.systemInfo.info.fTabletResX = x;
			info.systemInfo.info.fTabletResY = y;
		}
		size = sizeof(info.systemInfo);
		break;
	}

	case kGestalt_RebootInfo:
		info.rebootInfo.fRebootReason = gGlobalsThatLiveAcrossReboot.fRebootReason;
		info.rebootInfo.fRebootCount = 0;
		size = sizeof(info.rebootInfo);
		break;

	case kGestalt_NewtonScriptVersion:
		info.nsVersion.fVersion = 1;
		size = sizeof(info.nsVersion);
		break;

	case kGestalt_PatchInfo:
		info.patchInfo.fTotalPatchPageCount = gGlobalsThatLiveAcrossReboot.fTotalPatchPageCount;
		for (int i = 0; i < kMaxPatchCount; i++)
		{
			const SPatchInfo& patch = gGlobalsThatLiveAcrossReboot.fPatchArray[i];
			info.patchInfo.fPatch[i].fPatchCheckSum = patch.fPatchCheckSum;
			info.patchInfo.fPatch[i].fPatchVersion = patch.fPatchVersion;
			info.patchInfo.fPatch[i].fPatchPageCount = patch.fPatchPageCount;
			info.patchInfo.fPatch[i].fPatchFirstPageIndex = patch.fPatchFirstPageIndex;
		}
		size = sizeof(info.patchInfo);
		break;

	case kGestalt_PCMCIAInfo:
		memset(&info.pcmciaInfo, 0, sizeof(info.pcmciaInfo));
		info.pcmciaInfo.fServicesAvailibility = kPCMCIACISParserAvail | kPCMCIACISIteratorAvailable | kPCMCIAExtendedTCardPCMCIA | kPCMCIAExtendedTCardSocket;
		size = sizeof(info.pcmciaInfo);
		break;

	case kGestalt_RexInfo:
		// NOT YET RECONSTRUCTED: the headers of the up to four ROM extensions
		// (GetRExPtr, 0x001209f8)
		result = kError_Call_Not_Implemented;
		size = 0;
		break;

	default:			// (kGestalt_SoundInfo included)
		result = kError_Bad_Parameters;
		size = 0;
		break;
	}
	token->ReplyRPC(&info, size, result);
}


// ROM 0x00130168 TaskMain__11TNameServerFv
// The service loop.  A request is a TNameServerRequest of some kind (the
// largest, TResArbitrationRequest, is received into); a message that is a
// collected sender is one of our own asynchronous sends completing.
void
TNameServer::TaskMain()
{
	TNameServerReply reply;
	TUMsgToken token;
	TResArbitrationRequest request;
	ULong size, msgType;
	for (;;)
	{
		ULong msgFilter = fSysEventIter == nil ? (ULong) kMsgType_MatchAll : ~kSysEventMsgType;
		long err = fPort.Receive(&size, &request, sizeof(request), &token, &msgType, kNoTimeout, msgFilter, false, false);
		if (msgType & kMsgType_CollectedSender)
		{
			TRPCInfo* info = nil;
			token.GetUserRefCon((ULong*) &info);
			if (info->fType == kRPCInfo_ResArb)
				ResArbHandleReply((TResArbitrationInfo*) info->fInfo);
			else if (info->fType == kRPCInfo_SysEvent)
			{
				// the event reached one registrant: on to the next that accepts
				// it; when none is left the sender hears back
				SysEventRegistrant* r = (SysEventRegistrant*) fSysEventIter->NextItem();
				while (r != nil)
				{
					fSysEventPort.CopyObject(r->fPortId);
					if (fSysEventPort.SendRPC(&fSysEventMsg, kNoContent, 0, nil, 0, r->fTimeout, nil, r->fSendFilter) == noErr)
						break;
					r = (SysEventRegistrant*) fSysEventIter->NextItem();
				}
				if (r != nil)
					continue;
				delete fSysEventIter;
				fSysEventIter = nil;
				fSysEventToken.ReplyRPC(kNoContent, 0, noErr);
			}
			continue;
		}
		if (err != noErr)
			continue;

		Boolean doReply = true;
		long result;
		TNameRequest& nameRequest = request;
		switch (request.fCommand)
		{
		case kRegisterForSystemEvent:		// a TSysEventRequest: event, port, timeout, filter
			result = RegisterForSystemEvent(nameRequest.fThing, nameRequest.fSpec, nameRequest.fParam1, nameRequest.fParam2);
			break;
		case kUnRegisterForSystemEvent:
			result = UnRegisterForSystemEvent(nameRequest.fThing, nameRequest.fSpec);
			break;
		case kSendSystemEvent:
			fSysEventToken = token;
			result = SendSystemEvent(nameRequest.fThing, nameRequest.fSpec);
			doReply = result != noErr;
			break;
		case kGestalt:						// a TGestaltRequest: the selector
			Gestalt(nameRequest.fThing, &token);
			continue;
		default:
			result = BuildNameAndType(nameRequest.fObjectName, nameRequest.fObjectType);
			if (result == noErr)
			{
				switch (request.fCommand)
				{
				case kRegisterName:
					result = RegisterName(nameRequest.fThing, nameRequest.fSpec);
					break;
				case kUnregisterName:
					result = UnRegisterName();
					break;
				case kWaitForRegister:
					result = QueueForRegister(&token);
					doReply = false;
					break;
				case kWaitForUnregister:
					result = QueueForUnregister(&token);
					doReply = false;
					break;
				case kLookup:
					result = Lookup(&reply.fThing, &reply.fSpec);
					break;
				case kResourceArbitration:
					doReply = false;
					ResourceArbitration(&token, &request);
					break;
				default:					// 0, k_DEBUGGING_DumpObjectName
					result = kError_Bad_Parameters;
					break;
				}
			}
			DeleteNameAndType();
			break;
		}
		if (doReply)
			token.ReplyRPC(&reply, sizeof(reply), result);
	}
}


// ROM 0x0012f03c InitNameServer__Fv
// Spawns the name server task, 'name', on a copy of a TNameServer.
long
InitNameServer()
{
	TNameServer server;
	return server.StartTask(true, false, kNoTimeout, 6000, 10, 'name');
}
