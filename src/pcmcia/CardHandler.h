/*
	File:		pcmcia/CardHandler.h

	Contains:	TCardHandler, the protocol a kind of PC card is recognised
				and served through (the DDK's PCMCIA/CardHandler.h, Copyright
				1992-1996 by Apple Computer, Inc., derived from v6 internal
				(3/19/97)): the card server offers each card to the handlers
				it has (RecognizeCard), and the one that takes it installs
				its services - a TFlash for a memory card's devices, a serial
				chip for a modem's - which the card server then hands on
				(GetDeviceInfo).

				The DDK declares the protocol's methods as ProtocolGen took
				them; here they are virtual (src/protocols/Protocols.h), in
				the ROM's dispatch order (classinfo.py --name TCHMemModem).
				tools/newton-rom/sync_ddk_headers.py leaves the DDK's header
				out of src/ddk in favour of this one.
*/

#ifndef __CARDHANDLER_H
#define __CARDHANDLER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#ifndef __PROTOCOLS_H
#include "Protocols.h"
#endif

#ifndef __CARDDEFINES_H
#include "CardDefines.h"
#endif

class TCardSocket;
class TCardPCMCIA;


PROTOCOL TCardHandler : public TProtocol
{
public:
	static TCardHandler*	New(char* implementation);		// ROM 0x0038635c New__12TCardHandlerSFPc
	void					Delete(void);					// ROM 0x00386388 Delete__12TCardHandlerFv

	VIRTUAL NewtonErr	RecognizeCard(TCardSocket* socket, TCardPCMCIA* card) ENDVIRTUAL;						// ROM 0x003863a4 RecognizeCard__12TCardHandlerFP11TCardSocketP11TCardPCMCIA
	VIRTUAL NewtonErr	ParseUnrecognizedCard(TCardSocket* socket, TCardPCMCIA* card) ENDVIRTUAL;				// ROM 0x003863b0 ParseUnrecognizedCard__12TCardHandlerFP11TCardSocketP11TCardPCMCIA
	VIRTUAL NewtonErr	InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber = 0) ENDVIRTUAL;	// ROM 0x003863bc InstallServices__12TCardHandlerFP11TCardSocketP11TCardPCMCIAUl
	VIRTUAL NewtonErr	RemoveServices(void) ENDVIRTUAL;														// ROM 0x003863c8 RemoveServices__12TCardHandlerFv
	VIRTUAL NewtonErr	SuspendServices(void) ENDVIRTUAL;														// ROM 0x003863d4 SuspendServices__12TCardHandlerFv
	VIRTUAL NewtonErr	ResumeServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber = 0) ENDVIRTUAL;	// ROM 0x003863e0 ResumeServices__12TCardHandlerFP11TCardSocketP11TCardPCMCIAUl
	VIRTUAL NewtonErr	EmergencyShutdown(void) ENDVIRTUAL;													// ROM 0x003863ec EmergencyShutdown__12TCardHandlerFv
	VIRTUAL NewtonErr	FormatCIS(TCardSocket* socket, TCardPCMCIA* card) ENDVIRTUAL;							// ROM 0x003863f8 FormatCIS__12TCardHandlerFP11TCardSocketP11TCardPCMCIA
	VIRTUAL char*		CardIdString(TCardPCMCIA* card) ENDVIRTUAL;											// ROM 0x00386404 CardIdString__12TCardHandlerFP11TCardPCMCIA
	VIRTUAL ULong		CardStatus(void) ENDVIRTUAL;															// ROM 0x00386410 CardStatus__12TCardHandlerFv
	VIRTUAL ULong		GetNumberOfDevice(void) ENDVIRTUAL;													// ROM 0x0038641c GetNumberOfDevice__12TCardHandlerFv
	VIRTUAL void		GetDeviceInfo(ULong deviceNumber, ULong* cardType, TObjectId* cardPhys, void** cardDriverInfo, ULong* deviceOffset, ULong* deviceSize) ENDVIRTUAL;	// ROM 0x00386428 GetDeviceInfo__12TCardHandlerFUlPUlT2PPvN22
	VIRTUAL void		SetCardServerPort(TObjectId port) ENDVIRTUAL;											// ROM 0x00386434 SetCardServerPort__12TCardHandlerFUl
	VIRTUAL void		SetRemovableHandler(Boolean removable) ENDVIRTUAL;									// ROM 0x00386440 SetRemovableHandler__12TCardHandlerFUc
	VIRTUAL Boolean		GetRemovableHandler(void) ENDVIRTUAL;													// ROM 0x0038644c GetRemovableHandler__12TCardHandlerFv
	VIRTUAL long		CardSpecific(ULong selector, void* ptr = 0, ULong something = 0) ENDVIRTUAL;			// ROM 0x00386458 CardSpecific__12TCardHandlerFUlPvT1
};


//	CardSpecific should return kError_Call_Not_Implemented if the selector is not
//  supported, to help with compatibility issues...

enum kCardSpecificSelectors										// From 0x00 to 0xff reserved for Apple
{
	kCardSpecificProcessOption				=	0,				// first param = TOption*, second param = TSerialChip* or TXXXDriver*, returns option processing results
	kCardSpecificPowerOn					=	1,				// first param = nil, second param = TSerialChip* or TXXXDriver*
	kCardSpecificPowerOff					=	2,				// first param = nil, second param = TSerialChip* or TXXXDriver*

	kCardSpecificATASetPartitionInfo		=	3,				// Set partition info
	kCardSpecificATAGetBootParamBlock		=	4,				// Get parameter block
	kCardSpecificATAGetStoreProtocolInfo	=	5,				// Get store protocol info

	kCardSpecificGetDeviceHWLocationId		=	9,				// first param = ULong* to "device hw location id 'slt?'", second param = device number		1384004

	kCardSpecificRegIREQintHandler			=	10,				// first param = InterruptHandler, second param = TSerialChip* or TXXXDriver*
	kCardSpecificDeRegIREQintHandler		=	11,				// first param = nil, second param = TSerialChip* or TXXXDriver*
	kCardSpecificSetIREQIntEnable			=	12				// first param = 1 for enable, 0 for disable, second param = TSerialChip* or TXXXDriver*
	// 128..255 reserved by lantern
};

#endif	/* __CARDHANDLER_H */
