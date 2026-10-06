/*
	File:		newt/StorageCards.cpp

	Contains:	The application's half of a storage card: the store events
				the PSS manager sends the newt world - 'stor, a card's
				stores to be mounted (StorageCardInserted, MountStore: the
				store initialised, formatted when it has to be or the user
				says so, its password checked, a 1.x store converted, then
				registered and the NewtonScript card handler told
				'StoreMounted), and 'rstr, a card gone (StorageCardRemoved,
				UnmountStore) - and the newt world's 'cdsv handler's event
				procs (TNewtCardEventHandler::HandleCardEvent: the card
				server's news told to the NewtonScript card handler).

	Reconstructed from the MP2x00 US ROM (0x0030c224-0x0030edb4); each
	function cites its origin.
*/

#include "StorageCards.h"
#include "NewtCardEvents.h"
#include "PSSManager.h"
#include "FlashStore.h"
#include "Soups.h"
#include "PackageManager.h"
#include "LargeObjects.h"
#include "RootView.h"
#include "Interpreter.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "ObjectHeap.h"
#include "OSErrors.h"

void	ExceptionNotify(Exception* exception);		// (Notebook.cpp)


// ROM 0x0030c3d0 HandleCardEvent__21TNewtCardEventHandlerFP12TCardMessage
// The card server's news for the application, told to the NewtonScript
// card handler: an error (3; a dead battery, -8001, as such), a card gone
// (0x33), a battery low or dead (0x69), and a card put away (0x6f, the
// answer to UnmountCard: its callback called with [error] and the request
// done with).  An exception is reported rather than passed on.  ==> 2:
// the message is answered as the card server's own messages are.
long
TNewtCardEventHandler::HandleCardEvent(TCardMessage* message)
{
	newton_try
	{
		ULong type = message->fType;
		if (type == kCardServerCheckBattery)
		{
			if ((long) message->fData == ERRBASE_NEWT - 1)
				CardEventPrompt(RSSYMsramcardreplacebattery, message->fSocket);
			else
				CardEventPrompt(RSSYMmisccarderror, message->fSocket, (long) message->fData);
		}
		else if (type == kCardServerCardRemoved)
			CardEventPrompt(RSSYMcardremoved, message->fSocket);
		else if (type == kCardServerBatteryReply)
		{
			if ((message->fData & 1) == 0)
				CardEventPrompt(RSSYMsramcardreplacebattery, message->fSocket);
			else if ((message->fData & 2) == 0)
				CardEventPrompt(RSSYMsramcardlowbattery, message->fSocket);
		}
		else if (type == kCardServerUnmount)
		{
			RefStruct* callback = (RefStruct*) message->fField20;
			unwind_protect
			{
				RefVar args(MakeArray(1));
				SetArraySlot(args, 0, RefVar(MAKEINT((long) message->fData)));
				DoBlock(*callback, args);
			}
			on_unwind
			{
				delete callback;
				delete (TCardAsyncMsg*) message;
			}
			end_unwind;
			// ROM BUG (fixed): the ROM answers the message it has just
			// deleted (ReplyServer writes into the freed block); an answer
			// to a completion goes nowhere, so the host leaves the freed
			// block alone (DEVIATION) - which is also the fix, so both
			// paths are one (no RomBugFixed() test)
			ExitHandler(&_info);
			return 2;
		}
		ReplyServer(message, 2, message->fSocket, 0);
	}
	newton_catch_all
	{
		ExceptionNotify(CurrentException());
		if (gREPout != nil)
			gREPout->ExceptionNotify(CurrentException());
	}
	end_try;
	return 2;
}


// ROM 0x0030c224 AEHandlerProc__21TNewtCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TNewtCardEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	if (HandleCardEvent((TCardMessage*) event) != 0)
		((TAppWorld*) GetGlobals())->AESetReply(sizeof(TCardMessage));
}


// ROM 0x0030c250 AECompletionProc__21TNewtCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TNewtCardEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* event)
{
	HandleCardEvent((TCardMessage*) event);
}


// ROM 0x0030d538 HandleCardStoreEvent__FRC6RefVarT1
Ref
HandleCardStoreEvent(RefArg event, RefArg storeObject)
{
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlot(args, 0, storeObject);
	return NSCallGlobalFn(RSSYMhandlecardevent, event, args);
}


// ROM 0x0030e3ac CheckStoreVersion__FP6TStoreiPlPUc
// A store older than version 4 (a 1.x card): converted when the user says
// so (*convert), left alone and mounted read-only otherwise - which a store
// implementation older than 2.0 cannot be, so that is an error
// (kNSErrNoLargeObjectsOnStore).  *version comes back 0 when the card was
// pulled while asking.
NewtonErr
CheckStoreVersion(TStore* store, int socket, long* version, UChar* convert)
{
	*convert = false;
	NewtonErr err = GetStoreVersion(store, version);
	if (err != noErr)
		return err;
	if (*version > 3)
		return noErr;
	Boolean readOnly;
	if ((err = store->IsReadOnly(&readOnly)) != noErr)
		return err;
	if (!readOnly)
	{
		RefVar answer(CardEventPrompt(RSSYMconvert1_2Excard_3F, socket));
		if (EQRef(answer, RSSYMcardyanked))
		{
			*version = 0;
			return noErr;
		}
		if (NOTNIL(answer))
		{
			*convert = true;
			return noErr;
		}
	}
	const TClassInfo* info = GetStoreClassInfo(store);
	if (info->Version() < 0x20000)
		return readOnly ? noErr : kNSErrNoLargeObjectsOnStore;
	return store->LockReadOnly();
}


// ROM 0x0030e4cc StorageCardInserted__FP14TNewStoreEvent
// ('stor) A card's stores mounted, the slot marked busy meanwhile.  A card
// the PSS manager has seen before (its store initialised) whose store is
// not mounted is a card put back after being pulled: mounted again when
// the user says so.
void
StorageCardInserted(TNewStoreEvent* event)
{
	SPSSStoreInfo* info = event->fInfo;
	if (info->fInitialized)
	{
		if (NOTNIL(ToObject(info->fStore)))
			return;
		RefVar answer(CardEventPrompt(RSSYMcardreinserted, info->fSocket));
		if (ISNIL(answer) || EQRef(answer, RSSYMcardyanked))
			return;
	}
	unwind_protect
	{
		NSCallGlobalFn(RSSYMmarkslotbusy, RSSYMstoremounted, RefVar(MAKEINT(info->fSocket)), RefVar(NILREF));
		for (int i = 0; i < 4; i++)
			if (event->fStores[i].fStore != nil)
				MountStore(event->fStores[i].fStore, event->fStores[i].fInfo);
	}
	on_unwind
	{
		NSCallGlobalFn(RSSYMmarkslotnotbusy, RSSYMstoremounted, RefVar(MAKEINT(info->fSocket)));
	}
	end_unwind;
}


// ROM 0x0030e658 MountStore__FP6TStoreP13SPSSStoreInfo
// A card's store initialised and, with the user's say-so, formatted (a
// card with no store on it, or any writable card when the user will have
// it done anyway), its password asked for, a 1.x store converted, then
// registered - named "Storage Card" when it has just been formatted - and
// the NewtonScript card handler told 'StoreMounted.  A store that will not
// register may be formatted and tried again.
NewtonErr
MountStore(TStore* store, SPSSStoreInfo* info)
{
	UChar formatted;
	InitializeCardStore(info, &formatted);
	Boolean didFormat = false;
	Boolean readOnly;
	NewtonErr err = store->IsReadOnly(&readOnly);
	Boolean badVpp = info->fType == 'flsh' && info->fFlash != nil && (info->fFlash->GetAttributes() & 0x80) != 0;
	ULong socket = info->fSocket;
	if (err == noErr)
	{
		Boolean format = false;
		if (!formatted)
		{
			if (badVpp)
			{
				CardEventPrompt(RSSYMformatbadvppcard, socket);
				return noErr;
			}
			if (readOnly)
			{
				CardEventPrompt(RSSYMformatlockedcard, socket);
				return noErr;
			}
			RefVar answer(CardEventPrompt(RSSYMformat_3F, socket));
			if (EQRef(answer, RSSYMcardyanked) || ISNIL(answer))
				return noErr;
			didFormat = true;
			format = true;
		}
		else if (!readOnly)
		{
			RefVar answer(CardEventPrompt(RSSYMformatwithextremeprejudice_3F, socket));
			if (EQRef(answer, RSSYMcardyanked))
				return noErr;
			didFormat = NOTNIL(answer);
			format = didFormat;
		}
		if (format)
			err = store->Format();
	}
	if (err != noErr)
	{
		CardEventPrompt(RSSYMmisccarderror, socket, err);
		return err;
	}

	NewtonErr result = noErr;
	if (!CheckStorePassword(store, RefVar(NILREF)))
	{
		RefVar password(CardEventPrompt(RSSYMcheckpassword, socket));
		if (ISNIL(password))
			return kSError_StoreNotFound;
		if (!CheckStorePassword(store, password))
		{
			CardEventPrompt(RSSYMbadpassword, socket);
			return kSError_StoreNotFound;
		}
	}
	if (badVpp)
	{
		RefVar answer(CardEventPrompt(RSSYMflashcardbadvpp, socket));
		if (EQRef(answer, RSSYMcardyanked))
			return noErr;
	}
	UChar convert;
	long version = 0;
	if (didFormat)
		convert = false;
	else
	{
		if ((err = CheckStoreVersion(store, socket, &version, &convert)) != noErr)
		{
			CardEventPrompt(RSSYMmisccarderror, socket, err);
			return err;
		}
		if (version == 0)
			return noErr;
	}

	Boolean again = true;
	RefVar storeObject;
	while (again)
	{
		newton_try
		{
			storeObject = RegisterTStore(store);
			if (didFormat)
				StoreSetName(storeObject, RefVar(Rfreshcardname));
			if (convert)
			{
				NSCallGlobalFn(RSSYMconvert1_2Exstore, storeObject, RefVar(MAKEINT(version)));
				SetStoreVersion(storeObject, 4);
			}
			HandleCardStoreEvent(RSSYMstoremounted, storeObject);
			again = false;
		}
		newton_catch_all
		{
			if (NOTNIL(storeObject))
			{
				RemoveTStore(store);
				storeObject = NILREF;
			}
			long error = GetExceptionErr(CurrentException());
			if (readOnly)
			{
				CardEventPrompt(RSSYMlockedcardmounterror, socket, error);
				again = false;
			}
			else
			{
				RefVar answer(CardEventPrompt(RSSYMformataftermounterror_3F, socket, error));
				if (ISNIL(answer) || EQRef(answer, RSSYMcardyanked))
					again = false;
				else
				{
					didFormat = true;
					result = store->Format();
					if (result != noErr)
					{
						CardEventPrompt(RSSYMmisccarderror, socket, result);
						again = false;
					}
				}
			}
		}
		end_try;
	}
	return result;
}


// ROM 0x0030eb5c CheckCardActiveProtocols__FP14TNewStoreEvent
// Whether a package on one of the card's stores is in use and cannot be
// deactivated - in which case the card is asked for back (ReinsertCard,
// with the package's name).
Boolean
CheckCardActiveProtocols(TNewStoreEvent* event)
{
	Boolean active = false;
	for (int i = 0; i < 4; i++)
	{
		TStore* store = event->fStores[i].fStore;
		if (store == nil)
			continue;
		TPMIterator iter;
		iter.Init();
		while (iter.More())
		{
			ULong packageId = iter.PackageId();
			TStore* packageStore;
			PSSId rootId;
			if (IdToStore(packageId, &packageStore, &rootId) == noErr && packageStore == store)
			{
				UChar safe;
				if (SafeToDeactivatePackage(packageId, &safe) == noErr && safe == 0)
				{
					ReinsertCard(event->fInfo->fSocket, iter.PackageName(), true);
					active = true;
					break;
				}
			}
			iter.NextPackage();
		}
		iter.Done();
	}
	return active;
}


// ROM 0x0030ec70 StorageCardRemoved__FP14TNewStoreEvent
// ('rstr) A card gone: its stores unmounted - unless the NewtonScript side
// says the slot is busy, or a package on it cannot let go, when the card
// is asked for back.
void
StorageCardRemoved(TNewStoreEvent* event)
{
	GC();
	ULong socket = event->fInfo->fSocket;
	RefVar busy(NSCallGlobalFn(RSSYMisslotbusy, RefVar(MAKEINT(socket))));
	if (ISNIL(busy))
	{
		if (!CheckCardActiveProtocols(event))
		{
			gRootView->SetPopup(nil, true);
			for (int i = 0; i < 4; i++)
				if (event->fStores[i].fStore != nil)
					UnmountStore(event->fStores[i].fStore);
		}
	}
	else if (!EQRef(busy, RSSYMbecause))
		ReinsertCard(socket, (const UniChar*) BinaryData(busy), true);
	else
		ReinsertCard(socket, nil, true);
}


// ROM 0x0030edb4 UnmountStore__FP6TStore
// The NewtonScript card handler told 'StoreUnMounted, and the store taken
// out of the store list (a store that was not in it is no matter).
NewtonErr
UnmountStore(TStore* store)
{
	RefVar storeObject(ToObject(store));
	if (NOTNIL(storeObject))
	{
		HandleCardStoreEvent(RSSYMstoreunmounted, storeObject);
		newton_try
		{
			RemoveTStore(store);
		}
		newton_catch("evt.ex.fr.store")
		{
			if ((long) (intptr_t) CurrentException()->data != kNSErrStoreNotRegistered)
				rethrow;
		}
		end_try;
	}
	return noErr;
}
