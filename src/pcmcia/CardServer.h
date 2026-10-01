/*
	File:		pcmcia/CardServer.h

	Contains:	The card server: the 'cdsv' world that watches the PCMCIA
				sockets, and the 'cdpr' world it hands the slow work to.

				A card going in or coming out is a socket interrupt
				(TCardServer::CardIntHandler), which sends the server a
				message (0xca) to look at the socket's pins a little later
				(DoPollLockSwitchAndCardDetected).  A card that is in and
				locked is powered up for its CIS and, once the card says it
				is ready (0xcb), the card processor ('cdpr', 0xfc) reads the
				CIS (gPCMCIA20Parser), offers the card to the socket's card
				handlers until one recognises it, and has that one install
				its services (ActivateCardHandler); the server then tells the
				system about the new card - a 'card' system event carrying
				the devices the handler installed (SendNewCardMessage, a
				TNewCardAsyncMsg), which is how the PSS manager hears of a
				memory card.  A card taken out is told to the application
				(0x33 to the client port, the newt world, which gave it with
				message 100) and to whoever took the card on (the socket's
				client port: the PSS manager), and once they have let it go
				(0x34) the card processor removes the handler's services
				(0xfe, DeactivateCardHandler).

				TCardDomains is the fault monitor for the sockets' memory: a
				task touching a card that has gone is held until the card is
				back, or given a bus error.

				Not in the DDK.  Layouts from the ROM (TCardServer 0x29e8
				bytes, TCardSocketState 0x368, TCardAsyncMsg 0xcc,
				TNewCardAsyncMsg 0xf0); the field names are ours where the
				ROM has none.  Reconstructed from the MP2x00 US ROM
				(0x0004b0d0-0x0004b2dc, 0x0004e40c-0x0004ec60,
				0x0005105c-0x00054d10).

				A task held on a card that has gone (0x35) puts up the card
				reinsert alert (CardAlerts.h, through the alert manager),
				which comes down when the card is back.

				NOT YET: the packages a card carries in its attribute memory
				(TCardPipe) and ATA cards (TCardATALoader); the card handlers'
				own packages ('cdhl', whose code is ARM: CardPartHandler.h).
*/

#ifndef __CARDSERVER_H
#define __CARDSERVER_H

#ifndef __CARDATALOADER_H
#include "CardATALoader.h"
#endif

#ifndef __CARDMESSAGE_H
#include "CardMessage.h"
#endif
#ifndef __APPWORLD_H
#include "AppWorld.h"
#endif
#ifndef __USERPORTS_H
#include "UserPorts.h"
#endif
#ifndef __USERPHYS_H
#include "UserPhys.h"
#endif
#ifndef __SYSTEMEVENTS_H
#include "SystemEvents.h"
#endif
#ifndef __NAMESERVER_H
#include "NameServer.h"
#endif
#ifndef __LIST_H
#include "List.h"
#endif
#ifndef __CARDSERVERGLOBALS_H
#include "CardServerGlobals.h"
#endif

class TCardSocket;
class TCardPCMCIA;
class TCardHandler;
class TClassInfo;
class TCardServer;
class TPCMCIA20Parser;
class TCardAlertEvent;


// The messages the card server takes (TCardMessage::fType) and sends
enum
{
	kCardServerCheckBattery		= 0x03,		// to the client: a socket's card status (fData)
	kCardServerCardRemoved		= 0x33,		// to the client and the socket's client: the card has gone
	kCardServerClientDone		= 0x34,		// the socket's client has let the card go
	kCardServerTaskBlocked		= 0x35,		// TCardDomains: a task is held on a card that has gone
	kCardServerCardBack			= 0x36,		// to the socket's client: the same card is back (fData its common memory)
	kCardServerCardBackDone		= 0x37,		// the socket's client has taken it back
	kCardServerFormatCIS		= 0x3c,		// write a new CIS on the card (one whose CIS is bad)
	kCardServerResetIdle		= 0x50,		// the card power code: a supply went on
	kCardServerClientPort		= 100,		// the application's port (fData)
	kCardServerBatteryCheck		= 0x68,		// check the card's battery (0xfb to the processor)
	kCardServerBatteryReply		= 0x69,		// the processor's answer, to the client
	kCardServerDeviceTypes		= 0x6e,		// the device types of the socket's card (in the reply's devices)
	kCardServerUnmount			= 0x6f,		// put the card away (FUnmountCard)
	kCardServerEject			= 0xc9,
	kCardServerPollSocket		= 0xca,		// look at the socket's pins
	kCardServerCardReady		= 0xcb,		// wait for the card to be ready for its CIS
	kCardServerCardGone			= 0xce,
	kCardProcessorFormatCIS		= 0xfa,		// to the card processor
	kCardProcessorBattery		= 0xfb,
	kCardProcessorRecognize		= 0xfc,
	kCardProcessorResume		= 0xfd,
	kCardProcessorRemove		= 0xfe,
	kCardProcessorUnmount		= 0xff
};


// A TCardMessage that can be sent asynchronously (0xcc bytes).
class TCardAsyncMsg : public TCardMessage
{
public:
					TCardAsyncMsg();												// ROM 0x0004b0d0 __ct__13TCardAsyncMsgFv
	long			Init(void);														// ROM 0x0004b114 Init__13TCardAsyncMsgFv
	void			Free(void);														// ROM 0x0004b120 Free__13TCardAsyncMsgFv
	void			SendRPC(TUPort* port, TUPort* collector, ULong timeout, TTime* when);	// ROM 0x0004b130 SendRPC__13TCardAsyncMsgFP6TUPortT1UlP5TTime
	void			Send(TUPort* port, ULong timeout, TTime* when);					// ROM 0x0004b1b4 Send__13TCardAsyncMsgFP6TUPortUlP5TTime

	enum { kInUse = 0x80000000, kPermanent = 0x40000000 };
	ULong			fFlags;				// +B8 in use; one of the server's own (not made on demand)
	TUAsyncMessage	fAsync;				// +BC
};


// The 'card' system event a new card is announced with (0xf0 bytes).
class TNewCardAsyncMsg : public TCardMessage
{
public:
					TNewCardAsyncMsg();												// ROM 0x0004b224 __ct__16TNewCardAsyncMsgFv
	NewtonErr		Init(void);														// ROM 0x0004b284 Init__16TNewCardAsyncMsgFv
	NewtonErr		SendSystemEvent(void);											// ROM 0x0004b2dc SendSystemEvent__16TNewCardAsyncMsgFv

	TUAsyncMessage	fAsync;				// +B8
	TSendSystemEvent fEvent;			// +C8
};


// What the card server knows of a socket (0x368 bytes).
class TCardSocketState
{
public:
					TCardSocketState();												// ROM 0x0005105c __ct__16TCardSocketStateFv
					~TCardSocketState();											// ROM 0x00051ca0 __dt__16TCardSocketStateFv
	long			Init(void);														// ROM 0x000510e4 Init__16TCardSocketStateFv
	void			Clear(void);													// ROM 0x00051600 Clear__16TCardSocketStateFv

	enum
	{
		kATACard		= 0x01000000,		// its services came with an ATA loader
		kPagerHandler	= 0x02000000		// the handler is TCardHandlerPager version 0 (no emergency shutdown)
	};
	// fState
	enum { kSocketEmpty = 0, kSocketCardIn = 2, kSocketSuspended = 3 };
	// fCardState
	enum { kCardNone = 0, kCardActive = 1, kCardRemoved = 2, kCardTaskBlocked = 3 };

	ULong			fFlags;				// +000
	TCardPCMCIA*	fCard;				// +004 the card its handler has
	ULong			fConfigNumber;		// +008
	TCardHandler*	fHandler;			// +00C
	char			fIdString[0x20];	// +010 the handler's CardIdString
	TUPort			fClientPort;		// +030 who took the card on (the PSS manager)
	TObjectId		fPackages[8];		// +038 the packages the card carried
	TCardAsyncMsg	fMessage;			// +058
	TCardAsyncMsg	fMessage2;			// +124
	TUAsyncMessage	fAsync;				// +1E0
	ULong			fReadyTries;		// +1F0
	TUPhys			fPhys;				// +1F4 the socket's common memory (DEVIATION: never made, CardSocket.h)
	ULong			fDeviceTypes[4];	// +1FC the types of the devices its handler installed
	ULong			fClientDomain;		// +20C
	CList			fHandlers;			// +210 the card handlers' class infos
	ArrayIndex		fHandlerIndex;		// +228 the next one FirstCardHandler/NextCardHandler makes
	TUMsgToken		fDeferred;			// +22C an unmount waiting on the socket's client
	TCardAsyncMsg	fDeferredMsg;		// +23C what it asked (a copy), and the answer
	ULong			fState;				// +308 the socket: empty, card in, suspended
	ULong			fCardState;			// +30C the card: none, active, removed, a task blocked on it
	ULong			fField310;			// +310
	TATAPartitionInfo	fPartitionInfo;	// +314 an ATA card's (CardATALoader.h)
	TATABootParamBlock	fBootParams;	// +334
	TCardATALoader	fATALoader;			// +348
	TCardPCMCIA*	fParsedCard;		// +35C the CIS as it was last read
	ULong			fPSSBase;			// +360 the socket's client's window (the PSS manager's)
	ULong			fPSSSize;			// +364
};


// The card server's handler for 'cdsv' events (0x18 bytes)
class TCardEventHandler : public TAEventHandler
{
public:
					TCardEventHandler();											// ROM 0x0004e8d8 __ct__17TCardEventHandlerFv
	NewtonErr		Init(TCardServer* server);										// ROM 0x0004e918 Init__17TCardEventHandlerFP11TCardServer
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x0004ead0 AEHandlerProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0004eb08 AECompletionProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	IdleProc(TUMsgToken* token, ULong* size, TAEvent* event);			// ROM 0x0004eb38 IdleProc__17TCardEventHandlerFP10TUMsgTokenPUlP7TAEvent

	TCardServer*	fServer;			// +14
};


// The card server's handler for the system's power events (0x38 bytes)
class TCardSystemEventHandler : public TSystemEventHandler
{
public:
					TCardSystemEventHandler();										// ROM 0x0004ec60 __ct__23TCardSystemEventHandlerFv
	NewtonErr		Init(TCardServer* server);										// ROM 0x0004e970 Init__23TCardSystemEventHandlerFP11TCardServer
	virtual void	PowerOn(TAEvent* event);										// ROM 0x0004e9dc PowerOn__23TCardSystemEventHandlerFP7TAEvent
	virtual void	PowerOff(TAEvent* event);										// ROM 0x0004ea14 PowerOff__23TCardSystemEventHandlerFP7TAEvent
	virtual void	NewCard(TAEvent* event);										// ROM 0x0004e9d0 NewCard__23TCardSystemEventHandlerFP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0004eaa8 AECompletionProc__23TCardSystemEventHandlerFP10TUMsgTokenPUlP7TAEvent
	void			ReplyPowerOff(void);											// ROM 0x0004ea68 ReplyPowerOff__23TCardSystemEventHandlerFv

	Boolean			fCardRegistered;	// +14 registered for 'card' too
	TCardServer*	fServer;			// +18
	TUMsgToken		fPowerOffToken;		// +1C the power-off event, answered when the sockets are off
	TAESystemEvent	fPowerOffEvent;		// +2C
};


// The card processor's handler for 'cdsv' events (0x14 bytes)
class TCardProcessorEventHandler : public TAEventHandler
{
public:
					TCardProcessorEventHandler();									// ROM 0x0004eb84 __ct__26TCardProcessorEventHandlerFv
	NewtonErr		Init(void);														// ROM 0x0004ebc4 Init__26TCardProcessorEventHandlerFv
	virtual void	AEHandlerProc(TUMsgToken* token, ULong* size, TAEvent* event);		// ROM 0x0004ebd8 AEHandlerProc__26TCardProcessorEventHandlerFP10TUMsgTokenPUlP7TAEvent
	virtual void	AECompletionProc(TUMsgToken* token, ULong* size, TAEvent* event);	// ROM 0x0004ec20 AECompletionProc__26TCardProcessorEventHandlerFP10TUMsgTokenPUlP7TAEvent
};


// The 'cdpr' world: what takes the card server too long to do in its own
// (0x84 bytes).
class TCardProcessor : public TAppWorld
{
public:
					TCardProcessor();												// ROM 0x00054ca4 __ct__14TCardProcessorFv
	virtual			~TCardProcessor();												// ROM 0x00051158 __dt__14TCardProcessorFv
	virtual ULong	GetSizeOf();
	virtual long	ForkInit(TForkWorld* parent);									// ROM 0x000512d8 ForkInit__14TCardProcessorFP10TForkWorld
	virtual long	MainConstructor();												// ROM 0x000511b0 MainConstructor__14TCardProcessorFv
	virtual void	MainDestructor();												// ROM 0x00051298 MainDestructor__14TCardProcessorFv
	virtual void	TheMain();														// ROM 0x000512cc TheMain__14TCardProcessorFv
	virtual TForkWorld*	MakeFork();													// ROM 0x000512d0 MakeFork__14TCardProcessorFv

	long			DoCommand(TUMsgToken* token, ULong* size, TCardMessage* message, UChar completion);	// ROM 0x0005394c DoCommand__14TCardProcessorFP10TUMsgTokenPUlP12TCardMessageUc

	TUPort			fPort;				// +70 its own
	TUPort			fServerPort;		// +78 the card server's
	TCardProcessorEventHandler*	fHandler;	// +80
};


// The 'cdsv' world (0x29e8 bytes)
class TCardServer : public TAppWorld
{
public:
					TCardServer();													// ROM 0x00052938 __ct__11TCardServerFv
	virtual			~TCardServer();													// ROM 0x0005455c __dt__11TCardServerFv
	virtual ULong	GetSizeOf();
	virtual long	MainConstructor();												// ROM 0x00054648 MainConstructor__11TCardServerFv
	virtual void	MainDestructor();												// ROM 0x00054bc4 MainDestructor__11TCardServerFv

	long			DoCommand(TUMsgToken* token, ULong* size, TCardMessage* message, UChar completion);	// ROM 0x00052e10 DoCommand__11TCardServerFP10TUMsgTokenPUlP12TCardMessageUc
	void			DoSysEventPowerOff(TAEvent* event);								// ROM 0x000537ac DoSysEventPowerOff__11TCardServerFP7TAEvent
	NewtonErr		DoSysEventPowerOn(TAEvent* event);								// ROM 0x0005387c DoSysEventPowerOn__11TCardServerFP7TAEvent

	long			SendMessage(TUPort* port, ULong type, ULong socket, ULong data, ULong timeout, TTime* when);	// ROM 0x00051320 SendMessage__11TCardServerFP6TUPortUlN32P5TTime
	long			SendMessage(TUPort* port, TCardAsyncMsg* message, ULong timeout, TTime* when);				// ROM 0x00051390 SendMessage__11TCardServerFP6TUPortP13TCardAsyncMsgUlP5TTime
	long			SendNewCardMessage(TUPort* port, ULong socket, TCardHandler* handler, ULong* phys);			// ROM 0x00051418 SendNewCardMessage__11TCardServerFP6TUPortUlP12TCardHandlerPUl
	long			SendSelfMessage(ULong type, ULong socket, ULong data, ULong timeout, TTime* when);			// ROM 0x000515cc SendSelfMessage__11TCardServerFUlN31P5TTime
	void			ReplyMessage(TCardMessage* message, ULong type, ULong socket, ULong data);					// ROM 0x00051688 ReplyMessage__11TCardServerFP12TCardMessageUlN22
	TCardAsyncMsg*	GetFreeMessage(void);											// ROM 0x000516a4 GetFreeMessage__11TCardServerFv
	TCardAsyncMsg*	NewMessage(void);												// ROM 0x000516f8 NewMessage__11TCardServerFv

	NewtonErr		AddCardHandler(ULong socket, const TClassInfo* info);			// ROM 0x00051784 AddCardHandler__11TCardServerFUlPC10TClassInfo
	NewtonErr		RemoveCardHandler(ULong socket, const TClassInfo* info);		// ROM 0x00051858 RemoveCardHandler__11TCardServerFUlPC10TClassInfo
	TCardHandler*	FirstCardHandler(TCardSocketState* state);						// ROM 0x00051884 FirstCardHandler__11TCardServerFP16TCardSocketState
	TCardHandler*	NextCardHandler(TCardSocketState* state);						// ROM 0x00051890 NextCardHandler__11TCardServerFP16TCardSocketState
	NewtonErr		ActivateCardHandler(TCardHandler* handler, TCardSocket* socket, TCardSocketState* state);		// ROM 0x000518f8 ActivateCardHandler__11TCardServerFP12TCardHandlerP11TCardSocketP16TCardSocketState
	NewtonErr		DeactivateCardHandler(TCardHandler* handler, TCardSocket* socket, TCardSocketState* state);	// ROM 0x00051ad8 DeactivateCardHandler__11TCardServerFP12TCardHandlerP11TCardSocketP16TCardSocketState

	NewtonErr		SelectCardCISPower(TCardSocket* socket);						// ROM 0x00051bec SelectCardCISPower__11TCardServerFP11TCardSocket
	ULong			SelectCardConfiguration(TCardPCMCIA* card, TCardSocket* socket);	// ROM 0x00051d84 SelectCardConfiguration__11TCardServerFP11TCardPCMCIAP11TCardSocket
	NewtonErr		SelectCardPower(TCardSocket* socket, TCardPCMCIA* card);		// ROM 0x00051d8c SelectCardPower__11TCardServerFP11TCardSocketP11TCardPCMCIA
	ULong			CheckCardStatus(ULong socket);									// ROM 0x00051dbc CheckCardStatus__11TCardServerFUl
	void			InitializePCMCIABus(TCardSocket* socket);						// ROM 0x00051e60 InitializePCMCIABus__11TCardServerFP11TCardSocket
	void			InitializePCMCIABus(void);										// ROM 0x00051e88 InitializePCMCIABus__11TCardServerFv
	long			InitializeCardDetection(TCardSocket* socket, TCardSocketState* state);	// ROM 0x00051ed8 InitializeCardDetection__11TCardServerFP11TCardSocketP16TCardSocketState
	void			InitializeCardDetection(void);									// ROM 0x00051f84 InitializeCardDetection__11TCardServerFv
	void			SetPCMCIAWaitStates(TCardSocket* socket, TCardPCMCIA* card);	// ROM 0x00051fe0 SetPCMCIAWaitStates__11TCardServerFP11TCardSocketP11TCardPCMCIA
	NewtonErr		LoadCardPackage(TCardPCMCIA* card, TCardSocket* socket, TCardSocketState* state);	// ROM 0x000520b4 LoadCardPackage__11TCardServerFP11TCardPCMCIAP11TCardSocketP16TCardSocketState
	void			CardIntHandler(TCardSocket* socket);							// ROM 0x000523cc CardIntHandler__11TCardServerFP11TCardSocket
	NewtonErr		SuspendSocketAccess(TCardSocket* socket, TCardSocketState* state, ULong faultState);	// ROM 0x0005248c SuspendSocketAccess__11TCardServerFP11TCardSocketP16TCardSocketStateUl
	NewtonErr		ResumeSocketAccess(TCardSocket* socket, TCardSocketState* state);	// ROM 0x00052590 ResumeSocketAccess__11TCardServerFP11TCardSocketP16TCardSocketState
	long			DoCardEjection(ULong socketNumber, TCardSocket* socket, TCardSocketState* state);	// ROM 0x00052664 DoCardEjection__11TCardServerFUlP11TCardSocketP16TCardSocketState
	long			DoPollLockSwitchAndCardDetected(ULong socketNumber, TCardSocket* socket, TCardSocketState* state);	// ROM 0x0005266c DoPollLockSwitchAndCardDetected__11TCardServerFUlP11TCardSocketP16TCardSocketState
	UChar			CardReinsertAlertProc(ULong button, ULong socket);				// ROM 0x000525f4 CardReinsertAlertProc__11TCardServerFUlT1
	UChar			CardPositionAlertProc(ULong button, ULong socket);				// ROM 0x00052648 CardPositionAlertProc__11TCardServerFUlT1
	long			DoCardRecognition(ULong socketNumber, TCardSocket* socket, TCardSocketState* state);	// ROM 0x00052a70 DoCardRecognition__11TCardServerFUlP11TCardSocketP16TCardSocketState

	enum { kMessages = 32 };

	TUPort			fPort;				// +0070 its own
	TUPort			fClientPort;		// +0078 the application's (message 100)
	TUPort			fProcessorPort;		// +0080 the card processor's
	TCardEventHandler*	fEventHandler;	// +0088
	TCardAsyncMsg	fMessages[kMessages];	// +008C
	CList			fExtraMessages;		// +1A0C made when the 32 are all in use
	ULong			fField1A24;			// +1A24 holds a new card back (0xcb) and a resume (0xfd); nothing reconstructed sets it
	TCardSystemEventHandler*	fSystemHandler;	// +1A28
	TNewCardAsyncMsg fNewCardMessages[kMaxCardSockets][4];	// +1A2C a socket's 'card' events
	// +292C TCardPartHandler (DEVIATION: the newt world makes it, CardPartHandler.h)
	ULong			fField2968;			// +2968 cleared before a card's package is loaded
	TUPort			fAlertPort;			// +29A8 the alert manager's ('alrt)
	TUAsyncMessage	fReinsertAsync;		// +29B0
	TCardAlertEvent*	fReinsertEvent;	// +29C0 the card reinsert alert
	TUAsyncMessage	fPositionAsync;		// +29C4
	TCardAlertEvent*	fPositionEvent;	// +29D4 the card position alert
	ULong			fAlertFlags;		// +29D8 the reinsert alert is up (bit 31); the card is not positioned right (bit 30)
	UChar			fField29DC;			// +29DC
};


// TCardDomains: the fault monitor for the sockets' memory.  All static.
enum TCardFaultStates { kCardFaultNone = 0, kCardFaultHold = 1, kCardFaultRelease = 2 };

class TCardDomains
{
public:
					TCardDomains();													// ROM 0x0004e40c __ct__12TCardDomainsFv
					~TCardDomains();												// ROM 0x0004e41c __dt__12TCardDomainsFv
	long			Init(void);														// ROM 0x0004e674 Init__12TCardDomainsFv

	static long		CardFaultMonProc(long selector, void* userRefCon);				// ROM 0x0004e498 CardFaultMonProc__12TCardDomainsFlPv
	static void		NotifyTaskBlocked(ULong socket);								// ROM 0x0004e428 NotifyTaskBlocked__12TCardDomainsSFUl
	static TObjectId	SocketDomain(ULong socket);									// ROM 0x0004e830 SocketDomain__12TCardDomainsSFUl
	static TObjectId	ClientDomain(ULong socket);									// ROM 0x0004e840 ClientDomain__12TCardDomainsSFUl
	static TObjectId	CardFaultMonitor(void);										// ROM 0x0004e850 CardFaultMonitor__12TCardDomainsSFv
	static void		SetCardServerPort(TObjectId port);								// ROM 0x0004e860 SetCardServerPort__12TCardDomainsSFUl
	static NewtonErr	SetCardFaultState(ULong socket, TCardFaultStates state);	// ROM 0x0004e870 SetCardFaultState__12TCardDomainsSFUl16TCardFaultStates
	static TObjectId	ReleaseBlockedTask(void);									// ROM 0x0004e8c0 ReleaseBlockedTask__12TCardDomainsSFv
};


extern TCardServer*			gCardServer;						// ROM 0x0c100a7c gCardServer
extern TObjectId			gCardServerId;						// ROM 0x0c100a80 gCardServerId
extern TObjectId			gCardProcessorId;					// ROM 0x0c100a84 gCardProcessorId
extern TPCMCIA20Parser		gPCMCIA20Parser;					// ROM 0x0c100a88 gPCMCIA20Parser
extern TCardSocketState*	gSocketStates[kMaxCardSockets];		// ROM 0x0c105fe4 gSocketStates

NewtonErr	InitCardServices(void);									// ROM 0x00054d10 InitCardServices__Fv
NewtonErr	GetSocketInfo(ULong socket, TCardSocket** cardSocket, TCardPCMCIA** card);	// ROM 0x000544ec GetSocketInfo__FUlPP11TCardSocketPP11TCardPCMCIA

#endif	/* __CARDSERVER_H */
