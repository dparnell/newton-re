/*
	File:		pcmcia/CHMemModem.h

	Contains:	TCHMemModem, the ROM's card handler for memory and modem
				cards: a card whose CIS describes a common-memory device -
				flash (a TFlashSeries2 or TFlashAMD over it), SRAM or ROM -
				or a PC card modem.  Each device it installs is a
				TCHDeviceInfo, which GetDeviceInfo hands the card server.

				Not in the DDK; layouts from the ROM (0x44 and 0x30 bytes).
				Reconstructed from the MP2x00 US ROM (0x00047bd4-0x000494b0).
				NOT YET: the modem side - CheckNSetupModemDevice and
				AllocateSerialDriver wait on the PC card serial chip
				(TSerialChip16450) - and ParseUnrecognizedCard, which probes
				a card with no CIS by writing to it (the host's cards all
				have one).
*/

#ifndef __CHMEMMODEM_H
#define __CHMEMMODEM_H

#ifndef __CARDHANDLER_H
#include "CardHandler.h"
#endif

#ifndef __LIST_H
#include "List.h"
#endif

class TUPhys;
class TCardFunction;
class TFlash;


// A device a TCHMemModem installed (0x30 bytes)
class TCHDeviceInfo
{
public:
					TCHDeviceInfo();				// ROM 0x00047bd4 __ct__13TCHDeviceInfoFv
					~TCHDeviceInfo();				// ROM 0x00047c54 __dt__13TCHDeviceInfoFv

	TUPhys*			fPhys;				// +00 the card's common memory
	ULong			fType;				// +04 'flsh', 'sram', 'rom ', 'comm'
	void*			fDriver;			// +08 its TFlash, or its serial chip
	ULong			fOffset;			// +0C where it starts in common memory
	ULong			fSize;				// +10
	UChar			fCISNumber;			// +14
	UChar			fDeviceNumber;		// +15
	UChar			fKind;				// +16 1 memory, 2 modem
	UChar			fConfiguration;		// +17 a modem's configuration entry
	ULong			fRegisterBase;		// +18 a modem's configuration registers
	ULong			fIOBase;			// +1C
	ULong			fHWLocation;		// +20 'slt1' + the socket
	TCardFunction*	fFunction;			// +24
	UShort			fManufacturer;		// +28
	UShort			fManufacturerInfo;	// +2A
	UChar			fSerialInfo[3];		// +2C a modem's CISTPL_FUNCE serial port bytes
	UChar			fActiveBits;		// +2F
};


PROTOCOL TCHMemModem : public TCardHandler
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TCHMemModem);

	TCHMemModem*	New(void);																	// ROM 0x00048e04 New__11TCHMemModemFv
	void			Delete(void);																// ROM 0x00048e60 Delete__11TCHMemModemFv

	NewtonErr		RecognizeCard(TCardSocket* socket, TCardPCMCIA* card);						// ROM 0x000492f8 RecognizeCard__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
	NewtonErr		ParseUnrecognizedCard(TCardSocket* socket, TCardPCMCIA* card);				// ROM 0x00048f90 ParseUnrecognizedCard__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
	NewtonErr		InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);	// ROM 0x00049364 InstallServices__11TCHMemModemFP11TCardSocketP11TCardPCMCIAUl
	NewtonErr		RemoveServices(void);														// ROM 0x00049430 RemoveServices__11TCHMemModemFv
	NewtonErr		SuspendServices(void);														// ROM 0x0004849c SuspendServices__11TCHMemModemFv
	NewtonErr		ResumeServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber);	// ROM 0x00048510 ResumeServices__11TCHMemModemFP11TCardSocketP11TCardPCMCIAUl
	NewtonErr		EmergencyShutdown(void);													// ROM 0x000485d0 EmergencyShutdown__11TCHMemModemFv
	NewtonErr		FormatCIS(TCardSocket* socket, TCardPCMCIA* card);							// ROM 0x00048698 FormatCIS__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
	char*			CardIdString(TCardPCMCIA* card);											// ROM 0x000489dc CardIdString__11TCHMemModemFP11TCardPCMCIA
	ULong			CardStatus(void);															// ROM 0x000488d4 CardStatus__11TCHMemModemFv
	ULong			GetNumberOfDevice(void);													// ROM 0x00048adc GetNumberOfDevice__11TCHMemModemFv
	void			GetDeviceInfo(ULong deviceNumber, ULong* cardType, TObjectId* cardPhys, void** cardDriverInfo, ULong* deviceOffset, ULong* deviceSize);	// ROM 0x00048ae8 GetDeviceInfo__11TCHMemModemFUlPUlT2PPvN22
	void			SetCardServerPort(TObjectId port);											// ROM 0x00048b5c SetCardServerPort__11TCHMemModemFUl
	void			SetRemovableHandler(Boolean removable);										// ROM 0x00048b68 SetRemovableHandler__11TCHMemModemFUc
	Boolean			GetRemovableHandler(void);													// ROM 0x00048b60 GetRemovableHandler__11TCHMemModemFv
	long			CardSpecific(ULong selector, void* ptr, ULong something);					// ROM 0x00048b70 CardSpecific__11TCHMemModemFUlPvT1

	void			Clear(void);																// ROM 0x00048e9c Clear__11TCHMemModemFv
	NewtonErr		CheckNSetupMemoryDevice(TCardSocket* socket, TCardPCMCIA* card, CList* devices, ULong configNumber);	// ROM 0x00047ca0 CheckNSetupMemoryDevice__11TCHMemModemFP11TCardSocketP11TCardPCMCIAP5CListUl
	NewtonErr		NewFlashDriver(TCardSocket* socket, TCardPCMCIA* card, TFlash** flash, char* name, ULong configNumber, ULong deviceNumber);	// ROM 0x00047fcc NewFlashDriver__11TCHMemModemFP11TCardSocketP11TCardPCMCIAPP6TFlashPcUlT5
	NewtonErr		CheckNSetupModemDevice(TCardSocket* socket, TCardPCMCIA* card, CList* devices, ULong configNumber);	// ROM 0x00048064 CheckNSetupModemDevice__11TCHMemModemFP11TCardSocketP11TCardPCMCIAP5CListUl
	void			SetBusAccess(TCardSocket* socket, TCardPCMCIA* card);						// ROM 0x00048448 SetBusAccess__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
	UChar*			WriteTuple(UChar* to, const UChar* tuple, ULong size, UChar inAttrMemory);	// ROM 0x0004863c WriteTuple__11TCHMemModemFPUcT1UlUc

	TCardSocket*	fSocket;			// +10
	char			fIdString[0x20];	// +14
	Boolean			fRemovable;			// +34
	Boolean			fShutdown;			// +35 EmergencyShutdown has run: a modem's chip is remade on resume
	Boolean			fHasMemory;			// +36
	Boolean			fHasModem;			// +37
	CList*			fPhysList;			// +38 the TUPhys the memory devices are mapped through
	CList*			fDevices;			// +3C the TCHDeviceInfo
	ULong			fModemReadyTries;	// +40
};

#endif	/* __CHMEMMODEM_H */
