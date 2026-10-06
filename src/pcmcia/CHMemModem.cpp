/*
	File:		pcmcia/CHMemModem.cpp

	Contains:	TCHMemModem and TCHDeviceInfo (CHMemModem.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CHMemModem.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "CardCISIterator.h"
#include "CardPower.h"
#include "stores/flash/Flash.h"
#include "UserPhys.h"
#include "ListIterator.h"
#include "NewtonExceptions.h"
#include "UserTasks.h"
#include "OSErrors.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>

extern const unsigned char	sCISRamDevice[5];		// CISTables.cpp
extern const unsigned char	sCISNoLink[2];
extern const unsigned char	sCISEnd[1];
extern const unsigned char	sCISTarget[5];
extern const unsigned char	sCISLevel2[19];


/*------------------------------------------------------------------------------
	T C a r d H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x0038635c New__12TCardHandlerSFPc
TCardHandler*
TCardHandler::New(char* implementation)
{
	TCardHandler* p = (TCardHandler*) AllocInstanceByName("TCardHandler", implementation);
	return p != nil ? (TCardHandler*) p->GlueNew() : nil;
}


// ROM 0x00386388 Delete__12TCardHandlerFv
void
TCardHandler::Delete(void)
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	T C H D e v i c e I n f o
------------------------------------------------------------------------------*/

// ROM 0x00047bd4 __ct__13TCHDeviceInfoFv
TCHDeviceInfo::TCHDeviceInfo()
{
	fPhys = nil;
	fType = 0;
	fDriver = nil;
	fOffset = 0;
	fSize = 0;
	fCISNumber = 0;
	fDeviceNumber = 0;
	fKind = 0;
	fConfiguration = 0;
	fRegisterBase = 0;
	fIOBase = 0;
	fHWLocation = 0;
	fFunction = nil;
	fManufacturer = 0;
	fManufacturerInfo = 0;
	fSerialInfo[0] = 0;
	fSerialInfo[1] = 0;
	fSerialInfo[2] = 0;
	fActiveBits = 0;
}


// ROM 0x00047c54 __dt__13TCHDeviceInfoFv
// Its driver deleted: a memory device's TFlash, a modem's serial chip.
// NOT YET: a modem's serial chip (TSerialChip::Delete).
TCHDeviceInfo::~TCHDeviceInfo()
{
	if (fDriver != nil && fKind == 1)
		((TFlash*) fDriver)->Delete();
}


/*------------------------------------------------------------------------------
	T C H M e m M o d e m
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TCHMemModem)						// ROM 0x00048690 Sizeof__11TCHMemModemSFv
PROTOCOL_CLASSINFO(TCHMemModem, "TCardHandler", "", 0, 0, nil)	// ROM 0x00386770 ClassInfo__11TCHMemModemSFv


// ROM 0x00048e04 New__11TCHMemModemFv
TCHMemModem*
TCHMemModem::New(void)
{
	fDevices = new CList;
	fPhysList = new CList;
	fRemovable = false;
	if (fDevices == nil || fPhysList == nil)
	{
		Delete();
		return nil;
	}
	Clear();
	return this;
}


// ROM 0x00048e60 Delete__11TCHMemModemFv
void
TCHMemModem::Delete(void)
{
	Clear();
	if (fDevices != nil)
		delete fDevices;
	if (fPhysList != nil)
		delete fPhysList;
}


// ROM 0x00048e9c Clear__11TCHMemModemFv
// Every device and every phys given back.
void
TCHMemModem::Clear(void)
{
	TCHDeviceInfo* info;
	while ((info = (TCHDeviceInfo*) fDevices->At(fDevices->Count() - 1)) != nil)
	{
		delete info;
		fDevices->RemoveElementsAt(fDevices->Count() - 1, 1);
	}
	fSocket = nil;
	fModemReadyTries = 0;
	fShutdown = false;
	fHasMemory = false;
	fHasModem = false;
	for (ULong i = 0; i < 0x20; i++)
		fIdString[i] = 0;
	TUPhys* phys;
	while ((phys = (TUPhys*) fPhysList->At(fPhysList->Count() - 1)) != nil)
	{
		fPhysList->RemoveElementsAt(fPhysList->Count() - 1, 1);
		delete phys;
	}
}


// ROM 0x000492f8 RecognizeCard__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
// Whether the card has a memory device, or is a modem.
NewtonErr
TCHMemModem::RecognizeCard(TCardSocket* socket, TCardPCMCIA* card)
{
	NewtonErr err = CheckNSetupMemoryDevice(socket, card, nil, 0);
	if (err != noErr && (err = CheckNSetupModemDevice(socket, card, nil, 0)) != noErr)
		return err;
	SetBusAccess(socket, card);
	return noErr;
}


// ROM 0x00048f90 ParseUnrecognizedCard__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
// NOT YET: a card with no CIS is probed by writing patterns through its
// memory and seeing where they wrap, and taken for SRAM of that size (or
// asked of the flash drivers when nothing sticks) - a probe that runs over
// 64 MB of the socket's window, where the host has only the card's bytes.
// Every card the host makes has a CIS.
NewtonErr
TCHMemModem::ParseUnrecognizedCard(TCardSocket* socket, TCardPCMCIA* card)
{
	if (card == nil || socket == nil)
		return kError_Unrecognized_Card;
	if (socket->IsWriteProtected())
		return kError_Unformatted_WriteProtected_Card;
	return kError_Unrecognized_Card;
}


// ROM 0x00049364 InstallServices__11TCHMemModemFP11TCardSocketP11TCardPCMCIAUl
// The card's memory devices and modem installed; a card with neither (or a
// device that would not install) has what was installed taken away again.
NewtonErr
TCHMemModem::InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber)
{
	Clear();
	fSocket = socket;
	NewtonErr memoryErr = CheckNSetupMemoryDevice(socket, card, fDevices, configNumber);
	NewtonErr err = memoryErr;
	if (memoryErr == noErr || memoryErr == kError_Unrecognized_Card)
	{
		err = CheckNSetupModemDevice(socket, card, fDevices, configNumber);
		if (err == noErr || (err == kError_Unrecognized_Card && memoryErr != kError_Unrecognized_Card))
		{
			SetBusAccess(socket, card);
			return noErr;
		}
		if (err == kError_Unrecognized_Card)
		{
			RemoveServices();
			return err;
		}
	}
	if (err == noErr)
		return noErr;
	RemoveServices();
	return err;
}


// ROM 0x00049430 RemoveServices__11TCHMemModemFv
NewtonErr
TCHMemModem::RemoveServices(void)
{
	Clear();
	return noErr;
}


// ROM 0x0004849c SuspendServices__11TCHMemModemFv
NewtonErr
TCHMemModem::SuspendServices(void)
{
	NewtonErr err = noErr;
	CListIterator iter(fDevices);
	for (TCHDeviceInfo* info = (TCHDeviceInfo*) iter.FirstItem(); info != nil; info = (TCHDeviceInfo*) iter.NextItem())
		if (info->fDriver != nil && info->fKind == 1)
			err = ((TFlash*) info->fDriver)->SuspendService();
	return err;
}


// ROM 0x00048510 ResumeServices__11TCHMemModemFP11TCardSocketP11TCardPCMCIAUl
// NOT YET: a modem's serial chip remade after an emergency shutdown.
NewtonErr
TCHMemModem::ResumeServices(TCardSocket* socket, TCardPCMCIA* card, ULong configNumber)
{
	NewtonErr err = noErr;
	SetBusAccess(socket, card);
	CListIterator iter(fDevices);
	for (TCHDeviceInfo* info = (TCHDeviceInfo*) iter.FirstItem(); info != nil; info = (TCHDeviceInfo*) iter.NextItem())
	{
		if (info->fDriver != nil && info->fKind == 1)
			err = ((TFlash*) info->fDriver)->ResumeService(socket, card, configNumber);
	}
	return err;
}


// ROM 0x000485d0 EmergencyShutdown__11TCHMemModemFv
// The card has gone (from the interrupt handler): every device told.
// NOT YET: a modem's serial chip (TSerialChip::CardRemoved).
NewtonErr
TCHMemModem::EmergencyShutdown(void)
{
	fShutdown = true;
	for (long i = fDevices->Count() - 1; i >= 0; i--)
	{
		TCHDeviceInfo* info = (TCHDeviceInfo*) fDevices->At(i);
		if (info->fDriver != nil && info->fKind == 1)
			((TFlash*) info->fDriver)->SuspendService();
	}
	return noErr;
}


// ROM 0x0004863c WriteTuple__11TCHMemModemFPUcT1UlUc
// A tuple written into the card's memory (one byte in two in attribute
// memory), through the bus's byte lanes.  ==> where the next goes.
UChar*
TCHMemModem::WriteTuple(UChar* to, const UChar* tuple, ULong size, UChar inAttrMemory)
{
	ULong shift = inAttrMemory != 0 ? 1 : 0;
	for (ULong i = 0; i < size; i++)
		CardAttrMemWriteByte((void*) ((uintptr_t) (to + (i << shift)) ^ 3), tuple[i]);
	return to + (size << shift);
}


// ROM 0x00048698 FormatCIS__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
// An SRAM card with no good CIS given one: a CISTPL_DEVICE of its size
// (and CISTPL_NO_LINK in attribute memory, or - on a card with none -
// CISTPL_LINKTARGET and CISTPL_VERS_2 at the start of common memory) and
// the end.
// DEVIATION: the ROM writes the size code into its sCISRamDevice table
// itself (a global it recomputes each time); a copy is written here.
// ROM BUG (fixed): a card with a bad CIS and no devices at all is not
// checked for (device 0 is nil).  The fix leaves such a card alone, as one
// that is not SRAM.
NewtonErr
TCHMemModem::FormatCIS(TCardSocket* socket, TCardPCMCIA* card)
{
	NewtonErr err = noErr;
	TCardDevice* device = card->GetCardDevice(0);
	if (card->fBadCIS == 0)
		return noErr;
	if (device == nil && RomBugFixed())
		return noErr;
	if (device->fDeviceType != 6)
		return noErr;
	UChar ramDevice[5];
	memcpy(ramDevice, sCISRamDevice, sizeof(ramDevice));
	ULong size = device->fSize;
	UChar code;
	if (size <= 0x4000)
		code = (UChar) (((size >> 9) - 1) * 8);
	else if (size <= 0x10000)
		code = (UChar) (1 + ((size >> 11) - 1) * 8);
	else if (size <= 0x40000)
		code = (UChar) (2 + ((size >> 13) - 1) * 8);
	else if (size <= 0x100000)
		code = (UChar) (3 + ((size >> 15) - 1) * 8);
	else if (size <= 0x400000)
		code = (UChar) (4 + ((size >> 17) - 1) * 8);
	else if (size <= 0x1000000)
		code = (UChar) (5 + ((size >> 19) - 1) * 8);
	else
		code = (UChar) (6 + ((size >> 21) - 1) * 8);
	ramDevice[3] = code;
	newton_try
	{
		Boolean noAttrMem = card->fNoAttrMem != 0;
		UChar* p;
		if (noAttrMem)
		{
			p = (UChar*) socket->CommonMemBaseAddr();
			p = WriteTuple(p, sCISTarget, 5, 0);
			p = WriteTuple(p, ramDevice, 5, 0);
			p = WriteTuple(p, sCISLevel2, 0x13, 0);
		}
		else
		{
			p = (UChar*) socket->AttributeMemBaseAddr();
			p = WriteTuple(p, ramDevice, 5, 1);
			p = WriteTuple(p, sCISNoLink, 2, 1);
		}
		WriteTuple(p, sCISEnd, 1, !noAttrMem);
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	return err;
}


// ROM 0x000488d4 CardStatus__11TCHMemModemFv
// The battery (bits 0-1, from the BVD pins), write protection (bit 2) and
// busy (bit 3) - a memory card's from its pins, a modem's from its pin
// replacement register.
ULong
TCHMemModem::CardStatus(void)
{
	ULong status = 3;
	int socket;
	if (!fSocket->IsIOInteface())
	{
		socket = (int) fSocket->SocketNumber();
		VccOn(socket, false);
		ULong pins = fSocket->GetPCPins();
		status = (pins & 0x18) >> 3;
		if ((pins & 0x20) != 0)
			status |= 4;
		if ((pins & 4) == 0)
			status |= 8;
	}
	else
	{
		UChar* reg = nil;
		UChar active = 0;
		for (ULong i = 0; i < (ULong) fDevices->Count() && active == 0; i++)
		{
			TCHDeviceInfo* info = (TCHDeviceInfo*) fDevices->At(i);
			if (info->fKind != 1)
			{
				active = info->fActiveBits;
				if (active != 0)
					reg = (UChar*) ((info->fRegisterBase + 4) ^ 3);
			}
		}
		if (active == 0)
			return 3;
		socket = (int) fSocket->SocketNumber();
		VccOn(socket, false);
		UChar pins = *reg;
		if ((active & 1) != 0)
			status = (pins & 0xC) >> 2;
		if ((active & 2) != 0 && (pins & 1) != 0)
			status |= 4;
		if ((active & 4) != 0 && (pins & 2) == 0)
			status |= 8;
	}
	VccOff(socket);
	return status;
}


// ROM 0x000489dc CardIdString__11TCHMemModemFP11TCardPCMCIA
// The manufacturer and product, and for a memory card its speed and size:
// "AcmeWidget150 ns 4096K bytes", in 31 characters.
char*
TCHMemModem::CardIdString(TCardPCMCIA* card)
{
	char* id = fIdString;
	id[0] = 0;
	if (card != nil)
	{
		const char* manufacturer = card->GetCardManufacturer();
		const char* product = card->GetCardProduct();
		strncpy(id, manufacturer, 0x1F);
		long room = 0x1F - (long) strlen(manufacturer);
		if (room > 0)
		{
			strncat(id, product, room);
			long left = room - (long) strlen(product);
			TCHDeviceInfo* info = (TCHDeviceInfo*) fDevices->At(fDevices->Count() - 1);
			if (info != nil && left > 0 && info->fKind == 1)
			{
				char speedAndSize[0x100];
				TCardDevice* device = card->GetCardDevice(info->fDeviceNumber);
				sprintf(speedAndSize, "%d ns %dK bytes", (int) device->fnsecSpeed, (int) (device->fSize >> 10));
				strncat(id, speedAndSize, left);
			}
		}
		id[0x1F] = 0;
	}
	return id;
}


// ROM 0x00048adc GetNumberOfDevice__11TCHMemModemFv
ULong
TCHMemModem::GetNumberOfDevice(void)
{
	return fDevices->Count();
}


// ROM 0x00048ae8 GetDeviceInfo__11TCHMemModemFUlPUlT2PPvN22
void
TCHMemModem::GetDeviceInfo(ULong deviceNumber, ULong* cardType, TObjectId* cardPhys, void** cardDriverInfo, ULong* deviceOffset, ULong* deviceSize)
{
	if ((ULong) fDevices->Count() <= deviceNumber)
		return;
	TCHDeviceInfo* info = (TCHDeviceInfo*) fDevices->At(deviceNumber);
	if (info != nil)
	{
		*cardType = info->fType;
		*cardPhys = info->fPhys != nil ? (TObjectId) *info->fPhys : 0;
		*cardDriverInfo = info->fDriver;
		*deviceOffset = info->fOffset;
		*deviceSize = info->fSize;
	}
}


// ROM 0x00048b5c SetCardServerPort__11TCHMemModemFUl
void
TCHMemModem::SetCardServerPort(TObjectId)
{ }


// ROM 0x00048b60 GetRemovableHandler__11TCHMemModemFv
Boolean
TCHMemModem::GetRemovableHandler(void)
{
	return fRemovable;
}


// ROM 0x00048b68 SetRemovableHandler__11TCHMemModemFUc
void
TCHMemModem::SetRemovableHandler(Boolean removable)
{
	fRemovable = removable;
}


// ROM 0x00048b70 CardSpecific__11TCHMemModemFUlPvT1
// A modem's requests (9 is anybody's: a device's hardware location).
// NOT YET: selector 0 (a serial option) and 1 (the modem powered on and
// configured through its registers) need the modem side.
long
TCHMemModem::CardSpecific(ULong selector, void* ptr, ULong something)
{
	if (selector == kCardSpecificGetDeviceHWLocationId)
	{
		*(ULong*) ptr = 0;
		TCHDeviceInfo* info = (TCHDeviceInfo*) fDevices->At(something);
		if (info->fDriver != nil)
			*(ULong*) ptr = info->fHWLocation;
		return noErr;
	}
	TCHDeviceInfo* info;
	CListIterator iter(fDevices);
	for (info = (TCHDeviceInfo*) iter.FirstItem(); info != nil; info = (TCHDeviceInfo*) iter.NextItem())
	{
		if (something == 0)
		{
			if (info->fKind == 2)
				break;
		}
		else if ((ULong) (uintptr_t) info->fDriver == something)
			break;
	}
	long err = kError_Call_Not_Implemented;
	if (info == nil)
		return err;
	switch (selector)
	{
	case kCardSpecificRegIREQintHandler:
		err = fSocket->RegisterSocketInterrupt(kSocketCardIREQInt, (IntProcPtr) ptr, (void*) something);
		break;
	case kCardSpecificPowerOff:
		VccOff((int) fSocket->SocketNumber(), 0);
		err = noErr;
		break;
	case kCardSpecificDeRegIREQintHandler:
		fSocket->DeregisterSocketInterrupt(kSocketCardIREQInt);
		err = noErr;
		break;
	case kCardSpecificSetIREQIntEnable:
		if (ptr == nil)
			fSocket->DisableSocketInterrupt(kSocketCardIREQInt);
		else
			fSocket->EnableSocketInterrupt(kSocketCardIREQInt);
		err = noErr;
		break;
	}
	return err;
}


// ROM 0x00047fcc NewFlashDriver__11TCHMemModemFP11TCardSocketP11TCardPCMCIAPP6TFlashPcUlT5
// A flash driver of the named kind made for a device: initialised (flash
// given) or only asked whether it knows the card (flash nil).
NewtonErr
TCHMemModem::NewFlashDriver(TCardSocket* socket, TCardPCMCIA* card, TFlash** flash, char* name, ULong configNumber, ULong deviceNumber)
{
	TFlash* driver = TFlash::New(name);
	if (driver == nil)
		return kError_No_Memory;
	NewtonErr err;
	if (flash == nil)
		err = driver->FlashSpecific(0, socket, (ULong) (uintptr_t) card);
	else
		err = driver->Initialize(socket, card, configNumber, deviceNumber);
	if (err == noErr && flash != nil)
		*flash = driver;
	else
		driver->Delete();
	return err;
}


// ROM 0x00047ca0 CheckNSetupMemoryDevice__11TCHMemModemFP11TCardSocketP11TCardPCMCIAP5CListUl
// The card's common-memory devices - ROM, flash (a flash driver made for
// it: a TFlashSeries2, or failing that a TFlashAMD) and SRAM - in every
// CIS, each a TCHDeviceInfo from its first 4K page past the card's first
// data byte; the common memory made a phys for them.  With no list, only
// whether there is one.  ==> kError_Unrecognized_Card when there is none.
// ROM QUIRK kept: a device info that cannot be made ends it with noErr.
NewtonErr
TCHMemModem::CheckNSetupMemoryDevice(TCardSocket* socket, TCardPCMCIA* card, CList* devices, ULong configNumber)
{
	NewtonErr err = kError_Unrecognized_Card;
	ULong cisNumber = 0;
	if (card->GetNumOfCISs() == 0)
		return kError_Unrecognized_Card;
	for (;;)
	{
		TUPhys* phys = nil;
		ULong total = devices != nil ? card->fTotalDeviceSize : 0;
		if (devices != nil && total != 0)
		{
			phys = new TUPhys;
			if (phys == nil)
				return err;
			NewtonErr physErr = socket->CreateSocketPhys(phys, socket->CommonMemBaseAddr() - socket->AttributeMemBaseAddr(),
														 (card->fTotalDeviceSize + 0xFFFFF) & 0xFFF00000, false);
			if (physErr != noErr)
				return physErr;
			fPhysList->InsertAt(fPhysList->Count(), phys);
			err = noErr;
		}
		for (ULong deviceNumber = 0; deviceNumber < card->fNumOfDevice; deviceNumber++)
		{
			TCardDevice* device = card->GetCardDevice(deviceNumber);
			if (device == nil || device->fAttributeMemoryDescr != 0)
				continue;
			ULong type;
			UChar deviceType = device->fDeviceType;
			if (deviceType == 1 || deviceType == 2)
				type = 'rom ';
			else if (deviceType == 5)
				type = 'flsh';
			else if (deviceType == 6)
				type = 'sram';
			else
				continue;
			if (devices == nil)
				return noErr;
			TFlash* flash = nil;
			if (type == 'flsh')
			{
				err = NewFlashDriver(socket, card, &flash, (char*) "TFlashSeries2", configNumber, deviceNumber);
				if (err != noErr && (err = NewFlashDriver(socket, card, &flash, (char*) "TFlashAMD", configNumber, deviceNumber)) != noErr)
					return err;
			}
			err = noErr;
			ULong start = device->fStartOffset;
			ULong first = card->fFirstDataByteAddress;
			ULong offset;
			if (start < first && first < device->fSize + start)
				offset = first;
			else
			{
				offset = start;
				if (card->fNoAttrMem != 0 && card->fBadCIS != 0)
					offset = 0x80;
			}
			offset = (offset + 0xFFF) & 0xFFFFF000;
			TCHDeviceInfo* info = new TCHDeviceInfo;
			if (info == nil)
			{
				flash->Delete();
				return noErr;
			}
			info->fCISNumber = (UChar) cisNumber;
			info->fDeviceNumber = (UChar) deviceNumber;
			info->fKind = 1;
			info->fType = type;
			info->fPhys = phys;
			info->fDriver = flash;
			info->fOffset = offset;
			info->fSize = device->fSize - (offset - device->fStartOffset);
			devices->InsertAt(devices->Count(), info);
			fHasMemory = true;
		}
		if (devices == nil && err == noErr)
			return err;
		cisNumber++;
		if (card->GetNumOfCISs() <= cisNumber)
			return err;
		if (cisNumber != 0 && (card = card->GetCardCIS(cisNumber)) == nil)
			return err;
	}
}


// ROM 0x00048064 CheckNSetupModemDevice__11TCHMemModemFP11TCardSocketP11TCardPCMCIAP5CListUl
// NOT YET: a modem - a function id of 2 (or a VERS_1 fourth string of
// "PCMC...") with a configuration at a serial port's address - made a
// TSerialChip16450 over its registers.  Without the serial chip there is
// no modem, so every card is answered as the ROM answers a card that is
// not one.
NewtonErr
TCHMemModem::CheckNSetupModemDevice(TCardSocket*, TCardPCMCIA*, CList*, ULong)
{
	return kError_Unrecognized_Card;
}


// ROM 0x00048448 SetBusAccess__11TCHMemModemFP11TCardSocketP11TCardPCMCIA
// An 8-bit-only card read a byte at a time.
// DEVIATION: a modem also has a bit of the Voyager's control register set
// (its 0x2400 register, 0x100), which the host's socket does not have.
void
TCHMemModem::SetBusAccess(TCardSocket* socket, TCardPCMCIA* card)
{
	ULong control = socket->GetControl();
	socket->SetControl((control & ~kCardByteAccess) | (card->f8BitOnlyCard != 0 ? kCardByteAccess : 0));
}
