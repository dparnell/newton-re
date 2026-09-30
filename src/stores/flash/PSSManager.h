/*
	File:		stores/flash/PSSManager.h

	Contains:	The PSS manager (PSSManager.cpp): the internal store made
				on the internal flash at boot, and the 'pssm world that makes
				a store for each storage device on a card the card server
				announces, has the application mount it ('stor') and unmount
				it when the card goes ('rstr'), and lets the card server know
				when it is done with the card.

				Not in the DDK; layouts from the ROM (TPSSManager 0xb18
				bytes, a slot 0x1fc, a store 0x50).  Reconstructed from the
				MP2x00 US ROM (0x00154908-0x00155ef8).
*/

#ifndef __PSSMANAGER_H
#define __PSSMANAGER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __CARDMESSAGE_H
#include "CardMessage.h"
#endif
#ifndef __PSSINFO_H
#include "PSSInfo.h"
#endif

class TStore;
class TFlash;
class TCardHandler;


// One slot's (a socket's) state and stores (0x1fc bytes)
struct SPSSSlotInfo
{
	void			Clear(void);			// ROM 0x00155bc4 Clear__12SPSSSlotInfoFv

	// fState: what the slot's stores are doing
	enum
	{
		kEmpty = 0,
		kReady = 1,				// made, to be mounted
		kMounting = 2,			// 'stor sent to the application
		kGoneMounting = 3,		// the card went while they were being mounted
		kMounted = 4,
		kGone = 5,				// the card went: to be unmounted
		kUnmounting = 6,		// 'rstr sent
		kBackUnmounting = 7,	// the same card came back while they were being unmounted
		kUnmounted = 8			// to be let go of
	};

	ULong			fState;				// +000
	TCardMessage	fMessage;			// +004 the card server's new card message
	SPSSStoreInfo	fStores[4];			// +0BC
};


// The application's store events: 'stor (mount) and 'rstr (unmount) (0xb4
// bytes), a socket's stores, each with the device it is on
struct TNewStoreEvent : public TAEvent
{
	struct Store
	{
		TStore*				fStore;			// +00
		SPSSStoreInfo*		fInfo;			// +04
		SCardMessageDevice	fDevice;		// +08
	};

	ULong			fType;				// +08 'stor or 'rstr
	TStore*			fStore;				// +0C the first store (a 1.x client's)
	SPSSStoreInfo*	fInfo;				// +10
	Store			fStores[4];			// +14
};


// A card with no storage on it, told to the application ('card): the types
// of the devices its handler installed (0x20 bytes; sent as 0x80)
struct TNewCardEvent : public TAEvent
{
	ULong			fType;				// +08 'card
	ULong			fSocket;			// +0C
	ULong			fDeviceTypes[4];	// +10
};


// 'cdsv events: the card server's
class TPSSEventHandler : public TAEventHandler
{
public:
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x0015591c AEHandlerProc__16TPSSEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x00155964 AECompletionProc__16TPSSEventHandlerFP10TUMsgTokenPUlP7TAEvent
};


// 'card system events: a new card
class TPSSSysEventHandler : public TSystemEventHandler
{
public:
	virtual void	NewCard(TAEvent* event);										// ROM 0x0015599c NewCard__19TPSSSysEventHandlerFP7TAEvent
};


class TPSSManager : public TAppWorld
{
public:
					TPSSManager();
	virtual ULong	GetSizeOf();
	virtual long	MainConstructor();												// ROM 0x00155a38 MainConstructor__11TPSSManagerFv
	virtual void	MainDestructor();												// ROM 0x00155c0c MainDestructor__11TPSSManagerFv
	virtual void	TheMain();														// ROM 0x00155c10 TheMain__11TPSSManagerFv

	void			DoCommand(TUMsgToken* token, ULong* size, TCardMessage* message, UChar completion);	// ROM 0x00155d48 DoCommand__11TPSSManagerFP10TUMsgTokenPUlP12TCardMessageUc
	void			DoReply(TUMsgToken* token, ULong* size, TCardMessage* message, UChar completion);	// ROM 0x00155ef8 DoReply__11TPSSManagerFP10TUMsgTokenPUlP12TCardMessageUc
	void			CardAvailable(TCardMessage* message);							// ROM 0x00154d74 CardAvailable__11TPSSManagerFP12TCardMessage
	ULong			CardGone(TCardMessage* message);								// ROM 0x001550d0 CardGone__11TPSSManagerFP12TCardMessage
	ULong			CardIsSame(TCardMessage* message);								// ROM 0x00155244 CardIsSame__11TPSSManagerFP12TCardMessage
	void			UIEngine(UChar replied);										// ROM 0x00154988 UIEngine__11TPSSManagerFUc
	Boolean			MessageInUse(void);												// ROM 0x001549d0 MessageInUse__11TPSSManagerFv
	Boolean			DoReplyTransitions(void);										// ROM 0x00154a18 DoReplyTransitions__11TPSSManagerFv
	void			StuffSendAndTransition(int slot, int type, int state);			// ROM 0x00154a8c StuffSendAndTransition__11TPSSManagerFiN21
	Boolean			RegisterStores(void);											// ROM 0x00154b78 RegisterStores__11TPSSManagerFv
	Boolean			DeregisterStores(void);											// ROM 0x00154c08 DeregisterStores__11TPSSManagerFv
	void			GCStores(void);													// ROM 0x00154c64 GCStores__11TPSSManagerFv
	int				GetCardSlotStores(int slot, TStore** stores) const;			// ROM 0x001556f8 GetCardSlotStores__11TPSSManagerCFiPP6TStore
	SPSSStoreInfo*	GetStorePSSInfo(const TStore* store, UChar mounted) const;		// ROM 0x00155758 GetStorePSSInfo__11TPSSManagerCFPC6TStoreUc
	void			ReinsertCard(int slot, const UniChar* reason, UChar ask);		// ROM 0x001557d0 ReinsertCard__11TPSSManagerFiPCUsUc
	void			SendServer(ULong type, ULong socket, ULong data, ULong timeout, TTime* when);	// ROM 0x00155cc4 SendServer__11TPSSManagerFUlN31P5TTime
	void			ReplyServer(TCardMessage* message, ULong type, ULong socket, ULong data);		// ROM 0x00155d2c ReplyServer__11TPSSManagerFP12TCardMessageUlN22

	TUAsyncMessage		fServerAsync;		// +070 what the server is sent through
	TCardMessage		fServerMessage;		// +080
	TUPort*				fMyPort;			// +138
	TUPort				fNewtPort;			// +13C the application's
	TUPort				fServerPort;		// +144 the card server's
	TPSSEventHandler	fEventHandler;		// +14C
	TPSSSysEventHandler	fSysEventHandler;	// +160
	TUObject			fClientDomain;		// +178 ('ccl0')
	TUAsyncMessage		fStoreAsync;		// +180 the store events go through
	TNewStoreEvent		fStoreEvent;		// +190
	TUAsyncMessage		fCardAsync[4];		// +244 a socket's 'card events
	TNewCardEvent		fCardEvents[4];		// +284
	int					fSlotCount;			// +304
	SPSSSlotInfo		fSlots[4];			// +308
	TUMsgToken*			fUnmountTokens[4];	// +AF8 an unmount waiting on the application
	TCardMessage*		fUnmountMessages[4];	// +B08
};

extern TPSSManager*	gPSSManager;					// ROM 0x0c1016bc gPSSManager
extern TUPort*		gPSSPort;						// ROM 0x0c1016d4 gPSSPort
extern Boolean		gFormatCardsWhenInserted;		// ROM 0x0c1016c0 gFormatCardsWhenInserted
extern TStore*		gCardStore;						// ROM 0x0c1016cc gCardStore

void			ReinsertCard(int slot, const UniChar* reason, UChar ask);		// ROM 0x00155ba0 ReinsertCard__FiPCUsUc
NewtonErr		InitializeCardStore(SPSSStoreInfo* info, UChar* formatted);	// ROM 0x0015582c InitializeCardStore__FP13SPSSStoreInfoPUc
NewtonErr		StartPSSManager(void);											// (host: the world InitPSSManager starts)

ULong		InternalStoreInfo(int which);						// ROM 0x0011e250 InternalStoreInfo
NewtonErr	MapInternalFlashWindows(void);						// (part of InitCGlobals, ROM 0x000453b4)
NewtonErr	InitPSSManager(ULong environment, ULong);			// ROM 0x001553cc InitPSSManager__FUlT1

#endif	/* __PSSMANAGER_H */
