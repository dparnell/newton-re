/*
	File:		user/NameServerImpl.h

	Contains:	The name server task (TNameServer) and its tables: the
				kernel service behind TUNameServer (NameServer.h) that maps
				(name, type) pairs to a (thing, spec) pair of words, parks
				callers waiting for a name to appear or vanish, keeps the
				system-event registrations (TSystemEvent, SystemEvents.h) and
				answers Gestalt.  The DDK declares only the client; these
				declarations follow the ROM (layouts Ghidra-verified).

	The server runs as a TUTaskWorld named 'name' (InitNameServer) whose
	port is the well-known "name server port" (GetPortSWI(kGetNameServerPort),
	gNameServer in the kernel).
*/

#ifndef __NAMESERVERIMPL_H
#define __NAMESERVERIMPL_H

#ifndef __NAMESERVER_H
#include "NameServer.h"
#endif
#ifndef __USERTASKS_H
#include "UserTasks.h"
#endif
#ifndef __USERSHAREDMEM_H
#include "UserSharedMem.h"
#endif
#ifndef __AEVENTS_H
#include "AEvents.h"
#endif
#ifndef __ITEMCOMPARER_H
#include "ItemComparer.h"
#endif

class CList;
class CListIterator;
class CSortedList;
class TResArbitrationInfo;


// A registration: the (name, type) pair and the two words it stands for.
// The strings are the entry's own (malloc'ed by TNameServer::BuildNameAndType).
struct TObjectNameEntry		// 0x18 bytes
{
	TObjectNameEntry*		fNext;			// +0x00
	ULong					fThing;			// +0x04
	ULong					fSpec;			// +0x08
	char*					fName;			// +0x0c
	char*					fType;			// +0x10
	TResArbitrationInfo*	fResArbInfo;	// +0x14  set while the resource is claimed
};


// A caller waiting (WaitForRegister / WaitForUnregister) for a name: its
// message token to reply to, and the strings it waits for.
struct TNameServerQueuedRequest	// 0x1c bytes
{
	TNameServerQueuedRequest*	fNext;		// +0x00
	TUMsgToken					fToken;		// +0x04
	char*						fName;		// +0x14
	char*						fType;		// +0x18
};


// One hash bucket: the registrations with that hash, and the callers waiting
// for one of them to be registered / unregistered.
class TObjectNameList			// 0x14 bytes
{
public:
						TObjectNameList();

	Boolean				Add(char* name, char* type, ULong thing, ULong spec);
	Boolean				Remove(char* name, char* type);
	Boolean				Lookup(char* name, char* type, ULong* thing, ULong* spec, TObjectNameEntry** entry);

	TNameServerQueuedRequest*	fRegisterWaiters;	// +0x00
	TNameServerQueuedRequest*	fUnregisterWaiters;	// +0x04
	ULong						fUnused8;			// +0x08  (never written by the ROM)
	ULong						fUnusedC;			// +0x0c
	TObjectNameEntry*			fEntries;			// +0x10
};


// What an asynchronous message sent by the name server carries in its refcon,
// so the reply can be told apart when it comes back through the collector
// port.
struct TRPCInfo					// 0x8 bytes
{
						TRPCInfo()	{ fType = 0; fInfo = nil; }		// ROM 0x0012f3bc __ct__8TRPCInfoFv

	ULong				fType;			// kRPCInfo_*
	void*				fInfo;			// kRPCInfo_ResArb: the TResArbitrationInfo
};

enum
{
	kRPCInfo_ResArb		= 1,	// a resource-arbitration claim notification came back
	kRPCInfo_SysEvent	= 2		// a system event was delivered to one registrant
};


// A comm tool's request and reply as the name server sends them: the
// layouts of the DDK's TCommToolResArbRequest and TCommToolReply, whose
// constructors are the comm tools' (the name server lies below them).
struct TNSResArbRequest : public TAEvent	// 0x14 bytes in the ROM
{
	ULong				fOpCode;		// +0x08  kCommToolResArbRelease (1), kCommToolResArbClaimNotification (2)
	UChar*				fResNamePtr;	// +0x0c  the entry's own strings
	UChar*				fResTypePtr;	// +0x10
};

struct TNSResArbReply : public TAEvent		// 0x10 bytes in the ROM
{
	NewtonErr			fResult;		// +0x08
	ULong				fSize;			// +0x0c
};

// Who has a resource: the owner's port, and its name (a copy of the Unicode
// string the claim named, if it named one).
class TResOwnerInfo				// 0x10 bytes
{
public:
						TResOwnerInfo();
						~TResOwnerInfo();

	TUPort				fPort;			// +0x00
	void*				fName;			// +0x08
	ULong				fField0C;		// +0x0c  (cleared, otherwise unused)
};

// A claimed resource (a TObjectNameEntry's fResArbInfo): its active and
// passive owners, and - while an active owner is being asked to give it
// up - the request sent, its reply, and the claimant waiting for it.
class TResArbitrationInfo		// 0x64 bytes
{
public:
						TResArbitrationInfo();
						~TResArbitrationInfo();
	NewtonErr			Init(TUPort* collectorPort);

	ULong				fFlags;			// +0x00  kResArb_NotificationPending, kResArb_Removed
	TResOwnerInfo*		fPassiveOwner;	// +0x04
	TResOwnerInfo*		fOwner;			// +0x08
	TNSResArbRequest	fRequest;		// +0x0c
	TUAsyncMessage		fMsg;			// +0x20
	TNSResArbReply		fReply;			// +0x30
	TUMsgToken			fClaimant;		// +0x40  the claim waiting on the owner's answer
	TObjectId			fClaimantPort;	// +0x50
	TObjectId			fClaimantName;	// +0x54
	TRPCInfo			fRPCInfo;		// +0x58  kRPCInfo_ResArb, this
};

enum
{
	kResArb_NotificationPending	= 1,	// an owner has been asked to release it
	kResArb_Removed				= 2		// the name went meanwhile
};


// The list of ports registered for one system event, each with the timeout
// and send filter to deliver it with.  Sorted by port id.
struct SysEventRegistrant		// 0xc bytes
{
	TObjectId			fPortId;
	ULong				fTimeout;
	ULong				fSendFilter;
};

class SysEventItemComparer : public CItemComparer
{
public:
	virtual CompareResult	TestItem(const void* criteria) const;
};

class EventMasterListItem		// 0x14 bytes
{
public:
						EventMasterListItem();
						~EventMasterListItem();
	NewtonErr			Init();

	SystemEvent				fEvent;			// +0x00
	SysEventItemComparer	fComparer;		// +0x04
	CSortedList*			fRegistrants;	// +0x10  of SysEventRegistrant
};

// finds the EventMasterListItem for an event
class SysEventTester : public CItemTester
{
public:
						SysEventTester(SystemEvent event);
	virtual CompareResult	TestItem(const void* item) const;

	SystemEvent			fEvent;
};


class TNameServer : public TUTaskWorld		// 0x1a8 bytes
{
public:
						TNameServer();

	virtual ULong		GetSizeOf()		{ return sizeof(TNameServer); }
	virtual long		TaskConstructor();
	virtual void		TaskMain();

	enum { kNumLists = 16 };

private:
	ULong				Hash(char* name);
	NewtonErr			BuildNameAndType(TObjectId nameId, TObjectId typeId);
	void				DeleteNameAndType();

	NewtonErr			RegisterName(ULong thing, ULong spec);
	NewtonErr			UnRegisterName();
	NewtonErr			QueueForRegister(TUMsgToken* token);
	NewtonErr			QueueForUnregister(TUMsgToken* token);
	NewtonErr			Lookup(ULong* thing, ULong* spec);

	NewtonErr			RegisterForSystemEvent(SystemEvent event, TObjectId portId, ULong timeout, ULong sendFilter);
	NewtonErr			UnRegisterForSystemEvent(SystemEvent event, TObjectId portId);
	NewtonErr			SendSystemEvent(SystemEvent event, TObjectId msgId);

	void				ResourceArbitration(TUMsgToken* token, TResArbitrationRequest* request);
	NewtonErr			ResArbBuildResArbInfo(TObjectNameEntry* entry);
	void				ResArbDeleteResArbInfo(TObjectNameEntry* entry);
	NewtonErr			ResArbBuildResOwnerInfo(TResOwnerInfo*& owner, TObjectId ownerName, TObjectId ownerPortId);
	void				ResArbDeleteResOwnerInfo(TResOwnerInfo*& owner);
	NewtonErr			ResArbSendClaimNotification(TObjectNameEntry* entry);
	void				ResArbHandleReply(TResArbitrationInfo* info);

	void				Gestalt(ULong selector, TUMsgToken* token);

	TUPort				fPort;					// +0x18  the name server port
	CList*				fSysEventList;			// +0x20  of EventMasterListItem
	CListIterator*		fSysEventIter;			// +0x24  over the registrants of the event being sent (nil: none in progress)
	TUAsyncMessage		fSysEventMsg;			// +0x28  the event message being delivered (the sender's message, our reply mem)
	TRPCInfo			fRPCInfo;				// +0x38  kRPCInfo_SysEvent: refcon of fSysEventMsg
	TUPort				fSysEventPort;			// +0x40  the registrant being sent to
	TUMsgToken			fSysEventToken;			// +0x48  the sender of the event, replied to when all registrants have had it
	TUSharedMem			fSysEventReplyMem;		// +0x58
	char*				fName;					// +0x60  the request's name and type, copied out of shared memory
	char*				fType;					// +0x64
	TObjectNameList		fLists[kNumLists];		// +0x68
};

long		InitNameServer();

#endif	/* __NAMESERVERIMPL_H */
