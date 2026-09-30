/*
	File:		stores/flash/PSSManager.cpp

	Contains:	The PSS manager's internal half: the internal store made at
				boot - a TFlashStore on the internal flash, wrapped in a
				TMuxStore - and registered as "InRAMStore".

				The TPSSManager world ('pssm): a store made for each storage
				device on a card the card server announces ('card system
				event), mounted by the application ('stor) and unmounted
				when the card goes ('rstr).

				The reinsert alert when a card in use goes is
				SetCardReinsertReason (pcmcia/CardAlerts.h).  NOT YET: a
				RAM internal store (no flash: InternalStoreInfo's RAM
				sizes, the persistent 'rams entry), and the reserved
				block's accessor.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "PSSManager.h"
#include "FlashStore.h"
#include "MuxStore.h"
#include "MemoryAllocator.h"
#include "Soups.h"
#include "NameServer.h"
#include "CardServer.h"
#include "CardAlerts.h"
#include "CardServerGlobals.h"
#include "CardSocket.h"
#include "SystemEvents.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x00155bbc Clear__13SPSSStoreInfoFv
void
SPSSStoreInfo::Clear(void)
{
	memset(this, 0, sizeof(SPSSStoreInfo));
}


// ROM 0x0011e250 InternalStoreInfo
// 0: the internal flash's store size (nought without flash); 3: 0x1100000.
// NOT YET: 1 and 2, a RAM internal store's size and its persistent part,
// worked out from the RAM there is when there is no flash.
ULong
InternalStoreInfo(int which)
{
	if (which == 3)
		return 0x1100000;
	ULong size = gInternalFlashStoreSize;
	if (which != 0)
		size = 0;
	return size;
}


// (ROM 0x000453b4 InitCGlobals, the part about the flash)
// At boot, before there is a heap, the internal flash is found once out of
// a borrowed page with its windows mapped (kMapWindows), the reserved
// block read, and the instance thrown away; the instance the store uses
// later relies on those windows.  NOT YET: the reserved block's accessor
// (TReservedBlockAccessor: the calibration and the patches).
// DEVIATION: out of the heap rather than the early-boot page.
NewtonErr
MapInternalFlashWindows(void)
{
	alignas(TNewInternalFlash) char memory[sizeof(TNewInternalFlash)];
	TNewInternalFlash::ClassInfo()->MakeAt(memory);
	TNewInternalFlash* flash = (TNewInternalFlash*) memory;
	NewtonErr err = flash->InitForReservedBlock(THeapAllocator::GetGlobalAllocator(), TNewInternalFlash::kMapWindows);
	flash->CleanUp();
	return err;
}


// ROM 0x001553cc InitPSSManager__FUlT1
// The internal store: its implementations registered, a TFlashStore made
// on the internal flash (when there is one) and wrapped in a TMuxStore,
// formatted if it needs it, and named.
NewtonErr
InitPSSManager(ULong environment, ULong)
{
	TUNameServer nameServer;
	TMuxStore::ClassInfo()->Register();
	TMuxStoreMonitor::ClassInfo()->Register();
	TFlashStore::ClassInfo()->Register();
	TNewInternalFlash::ClassInfo()->Register();
	TStore* mux = TStore::New("TMuxStore");
	TStore* store = TStore::New("TFlashStore");
	if (mux != nil && store != nil)
	{
		mux->SetStore(store, environment);
		ULong size = InternalStoreInfo(0);
		if (size == 0)
			return kError_Call_Not_Implemented;		// NOT YET: a RAM internal store
		TFlash* flash = TFlash::New("TNewInternalFlash");
		((TNewInternalFlash*) flash)->Init(THeapAllocator::GetGlobalAllocator());
		store->Init(nil, size, environment, 0, kFlashStoreUsesTFlash, flash);
		gInRAMStore = store;
		gMuxInRAMStore = mux;
		Boolean needsFormat;
		mux->NeedsFormat(&needsFormat);
		if (needsFormat)
			mux->Format();
		PSSId root;
		mux->GetRootId(&root);
		long rootSize;
		mux->GetObjectSize(root, &rootSize);
		nameServer.RegisterName("InRAMStore", "TStore", (ULong) mux, 0);
		return StartPSSManager();
	}
	if (mux != nil)
		mux->Delete();
	if (store != nil)
		store->Delete();
	return -108;
}


/* -------------------------------------------------------------------------------
	The PSS manager's world ('pssm)
------------------------------------------------------------------------------- */

TPSSManager*	gPSSManager = nil;						// ROM 0x0c1016bc gPSSManager
TUPort*			gPSSPort = nil;							// ROM 0x0c1016d4 gPSSPort
Boolean			gFormatCardsWhenInserted = false;		// ROM 0x0c1016c0 gFormatCardsWhenInserted
TStore*			gCardStore = nil;						// ROM 0x0c1016cc gCardStore
static Boolean	gCardReinsertionTracker = false;		// ROM 0x0c106820 gCardReinsertionTracker
static ULong	gCardReinsertionTracker2 = 0;			// ROM 0x0c106824 (unnamed)


// ROM 0x00155bc4 Clear__12SPSSSlotInfoFv
void
SPSSSlotInfo::Clear(void)
{
	fState = kEmpty;
	memset((void*) &fMessage, 0, sizeof(fMessage));
	for (int i = 0; i < 4; i++)
		fStores[i].Clear();
}


// ROM 0x00154988 UIEngine__11TPSSManagerFUc
// The slots moved on: an answer from the application (replied) moves the
// ones it was about along first; then, unless the application is busy
// with one already, one slot's stores are unmounted or else one slot's
// mounted; and the slots whose stores are done with are let go of.
void
TPSSManager::UIEngine(UChar replied)
{
	Boolean busy = MessageInUse();
	if (replied)
		busy = DoReplyTransitions();
	if (!busy)
		busy = DeregisterStores();
	if (!busy)
		RegisterStores();
	GCStores();
}


// ROM 0x001549d0 MessageInUse__11TPSSManagerFv
// Whether a store event is out with the application.
Boolean
TPSSManager::MessageInUse(void)
{
	for (int i = 0; i < fSlotCount; i++)
	{
		ULong state = fSlots[i].fState;
		if (state == SPSSSlotInfo::kMounting || state == SPSSSlotInfo::kGoneMounting
		||  state == SPSSSlotInfo::kUnmounting || state == SPSSSlotInfo::kBackUnmounting)
			return true;
	}
	return false;
}


// ROM 0x00154a18 DoReplyTransitions__11TPSSManagerFv
// The application has answered: mounted, gone (to be unmounted),
// unmounted, or (the same card back) ready to be mounted again.
Boolean
TPSSManager::DoReplyTransitions(void)
{
	for (int i = 0; i < fSlotCount; i++)
	{
		ULong state = fSlots[i].fState;
		if (state == SPSSSlotInfo::kMounting)
			fSlots[i].fState = SPSSSlotInfo::kMounted;
		else if (state == SPSSSlotInfo::kGoneMounting)
			fSlots[i].fState = SPSSSlotInfo::kGone;
		else if (state == SPSSSlotInfo::kUnmounting)
			fSlots[i].fState = SPSSSlotInfo::kUnmounted;
		else if (state == SPSSSlotInfo::kBackUnmounting)
			fSlots[i].fState = SPSSSlotInfo::kReady;
	}
	return false;
}


// ROM 0x00154a8c StuffSendAndTransition__11TPSSManagerFiN21
// The slot's stores sent to the application in a store event of the type,
// and the slot moved to the state.
void
TPSSManager::StuffSendAndTransition(int slot, int type, int state)
{
	fStoreEvent.fAEventClass = 'newt';
	fStoreEvent.fType = type;
	fStoreEvent.fAEventID = 'idle';
	for (int i = 0; i < 4; i++)
	{
		fStoreEvent.fStores[i].fDevice = fSlots[slot].fMessage.fDevices[i];
		fStoreEvent.fStores[i].fStore = fSlots[slot].fStores[i].fStore;
		fStoreEvent.fStores[i].fInfo = &fSlots[slot].fStores[i];
	}
	fStoreEvent.fStore = fSlots[slot].fStores[0].fStore;
	fStoreEvent.fInfo = &fSlots[slot].fStores[0];
	fSlots[slot].fState = state;
	fNewtPort.SendRPC(&fStoreAsync, &fStoreEvent, sizeof(TNewStoreEvent), &fStoreEvent, sizeof(TNewStoreEvent));
}


// ROM 0x00154b78 RegisterStores__11TPSSManagerFv
// A slot whose stores are ready has them mounted ('stor) - unless one is
// being mounted already.
Boolean
TPSSManager::RegisterStores(void)
{
	for (int i = 0; i < fSlotCount; i++)
		if (fSlots[i].fState == SPSSSlotInfo::kMounting || fSlots[i].fState == SPSSSlotInfo::kGoneMounting)
			return false;
	for (int i = 0; i < fSlotCount; i++)
		if (fSlots[i].fState == SPSSSlotInfo::kReady)
		{
			StuffSendAndTransition(i, 'stor', SPSSSlotInfo::kMounting);
			return true;
		}
	return false;
}


// ROM 0x00154c08 DeregisterStores__11TPSSManagerFv
// A slot whose card has gone has its stores unmounted ('rstr).
Boolean
TPSSManager::DeregisterStores(void)
{
	for (int i = 0; i < fSlotCount; i++)
		if (fSlots[i].fState == SPSSSlotInfo::kGone)
		{
			StuffSendAndTransition(i, 'rstr', SPSSSlotInfo::kUnmounting);
			return true;
		}
	return false;
}


// ROM 0x00154c64 GCStores__11TPSSManagerFv
// The slots whose stores are unmounted let go of: the card server told
// (0x34) - or, when the application asked to put the card away, the
// request answered - and the stores deleted.
void
TPSSManager::GCStores(void)
{
	for (int i = 0; i < fSlotCount; i++)
	{
		if (fSlots[i].fState != SPSSSlotInfo::kUnmounted)
			continue;
		if (fUnmountTokens[i] == nil)
			SendServer(kCardServerClientDone, i, 0, 0, nil);
		else
		{
			fUnmountMessages[i]->MessageStuff(kCardServerUnmount, i, 0);
			fUnmountTokens[i]->ReplyRPC(fUnmountMessages[i], sizeof(TCardMessage), noErr);
			delete fUnmountTokens[i];
			delete (TCardAsyncMsg*) fUnmountMessages[i];
			fUnmountTokens[i] = nil;
			fUnmountMessages[i] = nil;
		}
		for (int j = 0; j < 4; j++)
			if (fSlots[i].fStores[j].fStore != nil)
				fSlots[i].fStores[j].fStore->Delete();
		fSlots[i].Clear();
	}
}


// ROM 0x00154d74 CardAvailable__11TPSSManagerFP12TCardMessage
// (the new card system event) A store made for each storage device the
// card's handler installed - the TStore implementation that serves the
// device's type (a TFlashStore for 'flsh), in a TMuxStore - and the slot
// made ready to have them mounted; the card server told the slot is taken
// (0x32, with the window the stores see the card through).  A card with no
// storage is told to the application as a 'card event of its types.
void
TPSSManager::CardAvailable(TCardMessage* message)
{
	int stores = 0;
	ULong socket = message->fSocket;
	ULong physSize = 0;
	// DEVIATION: the ROM makes the stores in the 'user environment
	// (MemObjManager::FindEnvironmentId); the host has one environment
	ULong environment = 0;
	SPSSSlotInfo* slot = &fSlots[socket];
	slot->fMessage = *message;
	for (int i = 0; i < 4; i++)
	{
		SCardMessageDevice* device = &message->fDevices[i];
		fCardEvents[socket].fDeviceTypes[i] = device->fType;
		SPSSStoreInfo* info = &slot->fStores[i];
		info->Clear();
		if (device->fType == 0)
			continue;
		char capability[5];
		capability[0] = (char) (device->fType >> 24);
		capability[1] = (char) (device->fType >> 16);
		capability[2] = (char) (device->fType >> 8);
		capability[3] = (char) device->fType;
		capability[4] = 0;
		TStore* store = (TStore*) NewByName("TStore", nil, capability);
		TStore* mux = TStore::New("TMuxStore");
		if (store == nil)
		{
			if (mux != nil)
				mux->Delete();
			continue;
		}
		if (mux == nil)
		{
			store->Delete();
			continue;
		}
		stores++;
		gCardStore = store;
		mux->SetStore(store, environment);
		info->fPhysId = device->fPhys;
		info->fPhys[0] = device->fPhys;
		info->fType = device->fType;
		info->fSocket = socket;
		info->fStore = mux;
		// DEVIATION: the ROM maps the device at gCardPSSVAddr[socket] + its
		// offset in the socket's client domain; a host card's common memory
		// is host memory already, and the store reads it there
		info->fBase = (char*) gCardSockets[socket]->CommonMemBaseAddr() + device->fOffset;
		info->fSize = device->fSize;
		info->fOffset = device->fOffset;
		info->fCardHandler = (TCardHandler*) message->fCardHandler;
		info->fFlash = (TFlash*) device->fDriver;
		info->fBadCIS = message->fField2C;
		info->fEnvironment = environment;
		info->fRawStore = store;
		info->fInitialized = false;
		// (the ROM maps the device's phys into the client domain here: the
		// host's phys is never made, CardSocket.h)
	}
	if (stores == 0)
		fNewtPort.Send(&fCardAsync[socket], &fCardEvents[socket], sizeof(TNewCardEvent), 0, nil);
	else
	{
		slot->fState = SPSSSlotInfo::kReady;
		UIEngine(false);
		fServerMessage.MessageStuff(kCardMessageCardAvailable, socket, fMyPort->fId);
		// DEVIATION: the window (gCardPSSVAddr[socket]) and its size, which
		// the card server makes inaccessible when the card is pulled; the
		// host has none
		fServerMessage.fField24 = 0;
		fServerMessage.fField28 = physSize;
		fServerPort.Send(&fServerAsync, &fServerMessage, sizeof(TCardMessage), 0, nil);
	}
}


// ROM 0x001550d0 CardGone__11TPSSManagerFP12TCardMessage
// The card has gone (or is to be put away): its stores to be unmounted -
// unless one of them is mounted and in use (not initialised yet, or
// locked), when the card server is told a task is held on it (0x35) and
// the stores are left as they are.
ULong
TPSSManager::CardGone(TCardMessage* message)
{
	ULong result = 2;
	ULong socket = message->fSocket;
	SPSSSlotInfo* slot = &fSlots[socket];
	if (slot->fState == SPSSSlotInfo::kMounted)
	{
		gCardReinsertionTracker = false;
		gCardReinsertionTracker2 = 0;
		for (int i = 0; i < 4; i++)
		{
			TStore* store = slot->fStores[i].fStore;
			if (store == nil)
				continue;
			// (the ROM forgets the device's mapping in the client domain
			// here, which the host never made)
			if (!slot->fStores[i].fInitialized || store->IsLocked())
			{
				result = kCardServerTaskBlocked;
				break;
			}
		}
		if (result != 2)
			return result;
	}
	switch (slot->fState)
	{
	case SPSSSlotInfo::kReady:			slot->fState = SPSSSlotInfo::kUnmounted; break;
	case SPSSSlotInfo::kMounting:		slot->fState = SPSSSlotInfo::kGoneMounting; break;
	case SPSSSlotInfo::kMounted:		slot->fState = SPSSSlotInfo::kGone; break;
	case SPSSSlotInfo::kBackUnmounting:	slot->fState = SPSSSlotInfo::kUnmounting; break;
	}
	UIEngine(false);
	return result;
}


// ROM 0x00155244 CardIsSame__11TPSSManagerFP12TCardMessage
// A card back in the socket: if each store says it is the same store
// (from the card's memory, fData, at the store's offset), the stores carry
// on where they were (0x37 to the card server); if not, 0x38.
ULong
TPSSManager::CardIsSame(TCardMessage* message)
{
	Boolean same = true;
	ULong socket = message->fSocket;
	SPSSSlotInfo* slot = &fSlots[socket];
	for (int i = 0; i < 4; i++)
	{
		SPSSStoreInfo* info = &slot->fStores[i];
		if (info->fStore != nil)
		{
			if (!info->fInitialized)
				same = true;
			else
				same = info->fStore->IsSameStore((void*) (info->fOffset + message->fData), info->fSize);
		}
		if (!same)
			return kCardServerCardBackDone + 1;
	}
	for (int i = 0; i < 4; i++)
	{
		if (slot->fStores[i].fStore == nil)
			continue;
		// (the ROM maps the device into the client domain again here)
		switch (slot->fState)
		{
		case SPSSSlotInfo::kGoneMounting:	slot->fState = SPSSSlotInfo::kMounting; break;
		case SPSSSlotInfo::kGone:			slot->fState = SPSSSlotInfo::kMounted; break;
		case SPSSSlotInfo::kUnmounting:		slot->fState = SPSSSlotInfo::kBackUnmounting; break;
		case SPSSSlotInfo::kUnmounted:		slot->fState = SPSSSlotInfo::kReady; break;
		}
	}
	gCardReinsertionTracker = true;
	return kCardServerCardBackDone;
}


// ROM 0x001556f8 GetCardSlotStores__11TPSSManagerCFiPP6TStore
int
TPSSManager::GetCardSlotStores(int slot, TStore** stores) const
{
	if (slot < 0 || slot >= fSlotCount)
		return 0;
	int count = 0;
	for (int i = 0; i < 4; i++)
		if (fSlots[slot].fStores[i].fStore != nil)
			stores[count++] = fSlots[slot].fStores[i].fStore;
	return count;
}


// ROM 0x00155758 GetStorePSSInfo__11TPSSManagerCFPC6TStoreUc
// What the PSS manager keeps about a card's store (mounted: only while it
// is mounted or being mounted).
SPSSStoreInfo*
TPSSManager::GetStorePSSInfo(const TStore* store, UChar mounted) const
{
	for (int slot = 0; slot < fSlotCount; slot++)
	{
		ULong state = fSlots[slot].fState;
		if (!mounted || state == SPSSSlotInfo::kMounted || state == SPSSSlotInfo::kBackUnmounting || state == SPSSSlotInfo::kMounting)
			for (int i = 0; i < 4; i++)
				if (fSlots[slot].fStores[i].fStore == store)
					return (SPSSStoreInfo*) &fSlots[slot].fStores[i];
	}
	return nil;
}


// ROM 0x001557d0 ReinsertCard__11TPSSManagerFiPCUsUc
// The reinsert alert's reason set, and the card's memory touched - which,
// with the card out, faults into the card domains' monitor, which holds
// the task and has the card server put the alert up until the card is
// back - then the reason taken away again.
// DEVIATION: a host card's memory never faults (TCardDomains has no
// monitor), so touching it holds nobody and puts nothing up; the alert
// comes up through CardGone's 0x35 instead, for a store that is in use.
void
TPSSManager::ReinsertCard(int slot, const UniChar* reason, UChar ask)
{
	if (slot < 0 || slot >= fSlotCount)
		return;
	SetCardReinsertReason(reason, ask);
	if (fSlots[slot].fStores[0].fBase != nil)		// (the ROM reads address 0 - its own - when there is none)
	{
		volatile char touched = *fSlots[slot].fStores[0].fBase;
		(void) touched;
	}
	SetCardReinsertReason(nil, 0);
}


// ROM 0x00155ba0 ReinsertCard__FiPCUsUc
void
ReinsertCard(int slot, const UniChar* reason, UChar ask)
{
	gPSSManager->ReinsertCard(slot, reason, ask);
}


// ROM 0x0015582c InitializeCardStore__FP13SPSSStoreInfoPUc
// A card's store initialised on the device the first time it is mounted
// (and formatted when the machine has been told to format every card put
// in); *formatted says whether it has a store on it - not when the card's
// CIS was bad (just written afresh), nor when it needs formatting.  A
// store that has one is asked for an object of nothing, which gets it
// going.
NewtonErr
InitializeCardStore(SPSSStoreInfo* info, UChar* formatted)
{
	NewtonErr err = noErr;
	if (info->fInitialized)
	{
		*formatted = true;
		return noErr;
	}
	info->fRawStore->Init(info->fBase, info->fSize, info->fEnvironment, info->fSocket, kFlashStoreIsCard, info);
	info->fInitialized = true;
	if (gFormatCardsWhenInserted)
	{
		gFormatCardsWhenInserted = false;
		err = info->fStore->Format();
	}
	*formatted = info->fBadCIS == 0;
	info->fBadCIS = 0;
	if (*formatted)
	{
		Boolean needsFormat;
		info->fStore->NeedsFormat(&needsFormat);
		*formatted = !needsFormat;
		if (*formatted)
			info->fStore->NewObject(0, nil);
	}
	return err;
}


// ROM 0x0015591c AEHandlerProc__16TPSSEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TPSSEventHandler::AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	TPSSManager* manager = (TPSSManager*) GetGlobals();
	manager->DoCommand(token, size, (TCardMessage*) event, false);
	manager->AESetReply(sizeof(TCardMessage));
}


// ROM 0x00155964 AECompletionProc__16TPSSEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TPSSEventHandler::AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event)
{
	((TPSSManager*) GetGlobals())->DoReply(token, size, (TCardMessage*) event, true);
}


// ROM 0x0015599c NewCard__19TPSSSysEventHandlerFP7TAEvent
void
TPSSSysEventHandler::NewCard(TAEvent* event)
{
	((TPSSManager*) GetGlobals())->CardAvailable((TCardMessage*) event);
}


// The answers of the store functions below the flash library (Soups.h's
// GetStorePSSInfo and GetCardSlotStores, ROM 0x001559bc and 0x00155a20,
// which ask gPSSManager as these do).  DEVIATION: IsValidStore (ROM
// 0x001559d4: the internal store, or one the manager has mounted) stays
// Soups.cpp's, which asks the store frames - the same stores.
static const SPSSStoreInfo*
PSSStoreInfo(const TStore* store)
{
	return gPSSManager->GetStorePSSInfo(store, false);
}


static long
PSSCardSlotStores(int slot, TStore** stores)
{
	return gPSSManager->GetCardSlotStores(slot, stores);
}


// (constructed in InitPSSManager, ROM 0x001553cc)
TPSSManager::TPSSManager()
	:	fMyPort(nil), fSlotCount(0)
{
	for (int i = 0; i < 4; i++)
	{
		fUnmountTokens[i] = nil;
		fUnmountMessages[i] = nil;
	}
}


// ROM 0x0038aac8 (unnamed) GetSizeOf - the vtable's +0x04
ULong
TPSSManager::GetSizeOf()
{
	return sizeof(TPSSManager);		// (the ROM: 0xb18)
}


// ROM 0x00155a38 MainConstructor__11TPSSManagerFv
// Its port the one the stores are told about, the handlers for the card
// server and for new cards, the store events' message collected by its
// own handler, a slot for each socket.
long
TPSSManager::MainConstructor()
{
	TAppWorld::MainConstructor();
	fServerAsync.Init(true);
	fServerMessage.Clear();
	gPSSPort = GetMyPort();
	fMyPort = gPSSPort;
	fEventHandler.Init('cdsv', 'newt');
	fSysEventHandler.Init(kSysEvent_NewICCard, 0);
	fStoreAsync.Init(true);
	fStoreAsync.SetCollectorPort(*((TAppWorld*) GetGlobals())->GetMyPort());
	fStoreAsync.SetUserRefCon((ULong) &fEventHandler);
	fSlotCount = gNumberOfHWSockets;
	for (int i = 0; i < fSlotCount; i++)
	{
		fSlots[i].Clear();
		fUnmountTokens[i] = nil;
		fUnmountMessages[i] = nil;
		fCardAsync[i].Init(true);
		fCardAsync[i].SetCollectorPort(0);
		fCardEvents[i].fAEventClass = 'newt';
		fCardEvents[i].fAEventID = 'idle';
		fCardEvents[i].fType = 'card';
		fCardEvents[i].fSocket = i;
	}
	// DEVIATION: the ROM finds the client domain 'ccl0 and answers its
	// error when it is not there; the host has no domains
	fClientDomain.CopyObject(0);
	gPSSManager = this;
	gPSSStoreInfoProc = PSSStoreInfo;
	gPSSCardSlotStoresProc = PSSCardSlotStores;
	return noErr;
}


// ROM 0x00155c0c MainDestructor__11TPSSManagerFv
void
TPSSManager::MainDestructor()
{
	TAppWorld::MainDestructor();
}


// ROM 0x00155c10 TheMain__11TPSSManagerFv
// The application's and the card server's ports waited for, then the
// event loop.
void
TPSSManager::TheMain()
{
	TUNameServer nameServer;
	ULong newtPort, serverPort, spec;
	if (nameServer.WaitForRegister((char*) "newt", (char*) "TUPort", &newtPort, &spec) == noErr
	&&  nameServer.WaitForRegister((char*) "cdsv", (char*) "TUPort", &serverPort, &spec) == noErr)
	{
		fNewtPort.CopyObject((TObjectId) newtPort);
		fServerPort.CopyObject((TObjectId) serverPort);
		TAppWorld::TheMain();
	}
}


// ROM 0x00155cc4 SendServer__11TPSSManagerFUlN31P5TTime
void
TPSSManager::SendServer(ULong type, ULong socket, ULong data, ULong timeout, TTime* when)
{
	fServerMessage.MessageStuff(type, socket, data);
	fServerPort.Send(&fServerAsync, &fServerMessage, sizeof(TCardMessage), timeout, when);
}


// ROM 0x00155d2c ReplyServer__11TPSSManagerFP12TCardMessageUlN22
void
TPSSManager::ReplyServer(TCardMessage* message, ULong type, ULong socket, ULong data)
{
	message->Clear();
	message->fType = type;
	message->fData = data;
	message->fSocket = socket;
}


// ROM 0x00155d48 DoCommand__11TPSSManagerFP10TUMsgTokenPUlP12TCardMessageUc
// The card server's messages: the card gone (0x33) or to be put away
// (0x6f: the answer waits until the stores are unmounted), and a card back
// (0x36).
void
TPSSManager::DoCommand(TUMsgToken* /*token*/, ULong* /*size*/, TCardMessage* message, UChar /*completion*/)
{
	ULong type = 2;
	ULong data = 0;
	ULong kind = message->fType;
	if (kind == kCardServerCardBack)
		type = CardIsSame(message);
	else if (kind == kCardServerCardRemoved || kind == kCardServerUnmount)
	{
		type = CardGone(message);
		if (message->fType == kCardServerUnmount)
		{
			if (type == 2 || type == 0)
			{
				ULong socket = message->fSocket;
				if (fUnmountTokens[socket] == nil && fUnmountMessages[socket] == nil)
				{
					TUMsgToken* token = new TUMsgToken;
					TCardAsyncMsg* copy = new TCardAsyncMsg;
					if (copy == nil || token == nil)
					{
						delete token;
						delete copy;
					}
					else
					{
						*token = *AEGetMsgToken();
						*(TCardMessage*) copy = *(TCardMessage*) AEGetAEvent();
						// (the ROM copies the two object ids after the message
						// into the copy's asynchronous message too - words the
						// sender did not send: DEVIATION, not those)
						AEDeferReply();
						fUnmountTokens[socket] = token;
						fUnmountMessages[socket] = copy;
					}
				}
				else
					data = (ULong) kError_Message_Already_Posted;
			}
			else
				data = (ULong) kError_Call_Aborted;
			type = kCardServerUnmount;
		}
	}
	ReplyServer(message, type, message->fSocket, data);
}


// ROM 0x00155ef8 DoReply__11TPSSManagerFP10TUMsgTokenPUlP12TCardMessageUc
// The application's answer to a store event: the slots moved on.
void
TPSSManager::DoReply(TUMsgToken* /*token*/, ULong* /*size*/, TCardMessage* message, UChar /*completion*/)
{
	ULong type = message->fType;
	if (type == 2)
		return;
	if (type != 'rstr' && type != 'stor')
		return;
	UIEngine(true);
}


// The 'pssm world started as InitPSSManager starts it (ROM 0x001553cc):
// a heap object that lives as long as the machine.  (host: split out of
// InitPSSManager, so that a host without the internal flash - a THostStore
// internal store - still has the PSS manager for its cards)
NewtonErr
StartPSSManager(void)
{
	if (gPSSManager != nil)
		return noErr;
	// (the store implementations InitPSSManager registers before it makes
	// the internal store: without the flash it has not run)
	TMuxStore::ClassInfo()->Register();
	TMuxStoreMonitor::ClassInfo()->Register();
	TFlashStore::ClassInfo()->Register();
	TPSSManager* manager = new TPSSManager;
	if (manager == nil)
		return kError_No_Memory;
	NewtonErr err = manager->Init('pssm', true, 6000);
	gCardReinsertionTracker = false;
	gCardReinsertionTracker2 = 0;
	return err;
}
