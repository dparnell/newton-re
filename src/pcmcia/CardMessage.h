/*
	File:		pcmcia/CardMessage.h

	Contains:	TCardMessage, the event the card server, the PSS manager,
				the card power code and the application's card handler
				exchange ('newt' 'cdsv'): what happened (fType), to which
				socket, and - for a new card - the devices its handler
				installed, four at most.

				Not in the DDK; 0xb8 bytes in the ROM, laid out from the
				accesses of its constructor, Clear and the code that fills
				and reads it (the field names are ours).  Reconstructed from
				the MP2x00 US ROM (0x0004ed10-0x0004ee04).
*/

#ifndef __CARDMESSAGE_H
#define __CARDMESSAGE_H

#ifndef __AEVENTS_H
#include "AEvents.h"
#endif

// fType
enum
{
	kCardMessageCardAvailable	= 0x32,		// the PSS manager: a card's stores are ready
	kCardMessageTaskBlocked		= 0x35,		// TCardDomains: a task faulted on a card that is gone
	kCardMessagePowerOn			= 0x50		// the power code: a socket (or the internal flash's Vpp) was switched on
};

const ULong	kCardMessageDevices = 4;

// A device a card's handler installed (TCardHandler::GetDeviceInfo)
struct SCardMessageDevice
{
	ULong		fType;			// +00 'flsh', 'sram', 'rom ', 'comm'
	TObjectId	fPhys;			// +04 the phys it is mapped through
	ULong		fField08;		// +08
	void*		fDriver;		// +0C its TFlash (a memory card's) or serial chip
	ULong		fOffset;		// +10 where it starts in common memory
	ULong		fSize;			// +14
	ULong		fField18;		// +18
	ULong		fField1C;		// +1C
};

class TCardMessage : public TAEvent
{
public:
					TCardMessage();													// ROM 0x0004ed10 __ct__12TCardMessageFv
					~TCardMessage();												// ROM 0x0004ed78 __dt__12TCardMessageFv

	void			Clear(void);													// ROM 0x0004ed84 Clear__12TCardMessageFv
	void			MessageStuff(ULong type, ULong socket, ULong data);			// ROM 0x0004edc4 MessageStuff__12TCardMessageFUlN21

	ULong			fType;				// +08
	ULong			fData;				// +0C
	ULong			fSocket;			// +10
	ULong			fField14;			// +14
	ULong			fField18;			// +18
	void*			fCardHandler;		// +1C the card's handler
	ULong			fField20;			// +20
	ULong			fField24;			// +24
	ULong			fField28;			// +28
	UChar			fField2C;			// +2C
	UChar			fField2D;			// +2D 1 after Clear
	UChar			fField2E;			// +2E
	UChar			fField2F;			// +2F
	ULong			fField30;			// +30
	ULong			fField34;			// +34
	SCardMessageDevice	fDevices[kCardMessageDevices];		// +38
};

#endif	/* __CARDMESSAGE_H */
