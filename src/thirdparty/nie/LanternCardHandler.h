/*
	File:		thirdparty/nie/LanternCardHandler.h

	Contains:	TLanternCardHandler, the Newton Internet Enabler's card
				handler for the Lantern Ethernet card - newtdev.pkg ("Newton
				Devices") part 4, a 'cdhl protocol part implementing
				TCardHandler in 3372 bytes of ARM - re-expressed as host code
				(the owner's decision for the NIE: its native code as the
				host's own) and registered as the part's stand-in
				(packages/ProtocolStandIns.h), so the card server offers it
				every card as the ROM's would.

				A card is its when its id string (the card's product name,
				as its CIS gives it) is registered with the name server
				under type "Lantern" - which the Ethernet driver package
				(enetsup.pkg) does for the cards it drives; any other card
				(a memory card) it declines with -10501.  Taking a card, it
				resets it, starts the driver's event world (TEventWorldAPI's
				TLanternEventWorld, newtdev.pkg part 5) unless one is already
				there, and tells that world of the card ('lant' 'newC'); the
				world gets the card's interrupts.

				NOT YET: the event world (part 5, 13492 bytes of ARM) is not
				re-expressed, so a Lantern card - which the host has none of -
				fails as it would with no such world (-61001).

				Each method cites its offset in the part, as the NIE's other
				re-expressions do.
*/

#ifndef __LANTERNCARDHANDLER_H
#define __LANTERNCARDHANDLER_H

#ifndef __CARDHANDLER_H
#include "CardHandler.h"
#endif

class TUPort;
class TUAsyncMessage;


PROTOCOL TLanternCardHandler : public TCardHandler
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TLanternCardHandler);

	TLanternCardHandler*	New(void);																	// NIE newtdev.pkg part 4 +0x2b0 New
	void			Delete(void);																		// NIE newtdev.pkg part 4 +0x4c8 Delete

	NewtonErr		RecognizeCard(TCardSocket* socket, TCardPCMCIA* card);								// NIE newtdev.pkg part 4 +0x694 RecognizeCard
	NewtonErr		ParseUnrecognizedCard(TCardSocket* socket, TCardPCMCIA* card);						// NIE newtdev.pkg part 4 +0x438 ParseUnrecognizedCard
	NewtonErr		InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);		// NIE newtdev.pkg part 4 +0x75c InstallServices
	NewtonErr		RemoveServices(void);																// NIE newtdev.pkg part 4 +0x95c RemoveServices
	NewtonErr		SuspendServices(void);																// NIE newtdev.pkg part 4 +0xafc SuspendServices
	NewtonErr		ResumeServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);		// NIE newtdev.pkg part 4 +0xb30 ResumeServices
	NewtonErr		EmergencyShutdown(void);															// NIE newtdev.pkg part 4 +0x94c EmergencyShutdown
	NewtonErr		FormatCIS(TCardSocket* socket, TCardPCMCIA* card);									// NIE newtdev.pkg part 4 +0x444 FormatCIS
	char*			CardIdString(TCardPCMCIA* card);													// NIE newtdev.pkg part 4 +0x38c CardIdString
	ULong			CardStatus(void);																	// NIE newtdev.pkg part 4 +0x384 CardStatus
	ULong			GetNumberOfDevice(void);															// NIE newtdev.pkg part 4 +0x3e0 GetNumberOfDevice
	void			GetDeviceInfo(ULong deviceNumber, ULong* cardType, TObjectId* cardPhys, void** cardDriverInfo, ULong* deviceOffset, ULong* deviceSize);	// NIE newtdev.pkg part 4 +0x3e8 GetDeviceInfo
	void			SetCardServerPort(TObjectId port);													// NIE newtdev.pkg part 4 +0x428 SetCardServerPort
	void			SetRemovableHandler(Boolean removable);												// NIE newtdev.pkg part 4 +0x430 SetRemovableHandler
	Boolean			GetRemovableHandler(void);															// NIE newtdev.pkg part 4 +0x420 GetRemovableHandler
	long			CardSpecific(ULong selector, void* ptr, ULong something);							// NIE newtdev.pkg part 4 +0x9a8 CardSpecific

	NewtonErr		ResetCard(TCardSocket* socket);														// NIE newtdev.pkg part 4 +0x44c (unnamed) - ResetCard
	void			SelectIO(void);																		// NIE newtdev.pkg part 4 +0x5d8 (unnamed) - SelectIO
	ULong			ReadyRetries(void);																	// NIE newtdev.pkg part 4 +0x608 (unnamed) - ReadyRetries
	static NewtonErr	CardInterrupt(void* handler, TCardSocket* socket);								// NIE newtdev.pkg part 4 +0x650 (unnamed) - CardInterrupt

	// the instance: 0x48 bytes in the part (its Sizeof, +0x2a8), the
	// TProtocol's first; host fields pointer-sized
	ULong			fReadyRetries;		// +0x10 how many tenths of a second a reset waits for the card
	UByte			fInstalled;			// +0x14 services installed
	char*			fIdString;			// +0x18 the card's id string (its product name)
	ULong			fLookedUp;			// +0x1C what the name server has under the id string
	ULong			fCardType;			// +0x20 the first word of its spec: the device's type
	UByte			fRemovable;			// +0x24
	TObjectId		fServerPort;		// +0x28 the card server's port
	TCardSocket*	fSocket;			// +0x2C
	TCardPCMCIA*	fCard;				// +0x30
	TUAsyncMessage*	fInterruptMessage;	// +0x34 sent from the card's interrupt
	TUPort*			fWorldPort;			// +0x38 the event world's port, found by the id string
	TUAsyncMessage*	fSuspendMessage;	// +0x3C
	TUAsyncMessage*	fRemoveMessage;		// +0x40
	TUAsyncMessage*	fResumeMessage;		// +0x44
};


// TLanternCardHandler registered as newtdev.pkg's card handler part's stand-in
void	RegisterNIEProtocolStandIns(void);

#endif	/* __LANTERNCARDHANDLER_H */
