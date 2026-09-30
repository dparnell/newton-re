/*
	File:		pcmcia/CardCISIterator.cpp

	Contains:	TCardCISIterator and the attribute memory's byte accessors
				(CardCISIterator.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardCISIterator.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x0004ecbc CardAttrMemReadDelay__Fv
// DEVIATION: the ROM spins a moment after each attribute memory read, for
// the slow card; the host's card needs no time.
void
CardAttrMemReadDelay(void)
{ }


// ROM 0x0004ece0 CardAttrMemWriteDelay__Fv
// DEVIATION: the ROM waits on the null port after a write to attribute
// memory (the card's configuration registers settle); the host's need not.
void
CardAttrMemWriteDelay(void)
{ }


// ROM 0x0004ecec CardAttrMemReadByte__FPv
UChar
CardAttrMemReadByte(void* addr)
{
	UChar data = *(UChar*) addr;
	CardAttrMemReadDelay();
	return data;
}


// ROM 0x0004ed08 CardAttrMemWriteByte__FPvUc
void
CardAttrMemWriteByte(void* addr, UChar data)
{
	*(UChar*) addr = data;
	CardAttrMemWriteDelay();
}


// The byte lane of a card address: the Voyager puts the card's bytes on the
// 32-bit bus the other way round within each word, so byte a is at a ^ 3.
static inline UChar*
Lane(UChar* p)
{
	return (UChar*) ((uintptr_t) p ^ 3);
}


// A byte of a CIS mirrored in common memory, read as the ROM reads it -
// the word at p & ~3 loaded and shifted right by (p & 3) bytes - which on
// the big-endian machine is the byte at p ^ 3 too; the host's memory is the
// machine's bytes, so it takes that byte directly.
static inline UChar
WordLane(UChar* p)
{
	return *Lane(p);
}


// ROM 0x0004b30c __ct__16TCardCISIteratorFv
TCardCISIterator::TCardCISIterator()
{ }


// ROM 0x0004b31c __dt__16TCardCISIteratorFv
TCardCISIterator::~TCardCISIterator()
{ }


// ROM 0x0004b598 Version__16TCardCISIteratorFv
ULong
TCardCISIterator::Version(void)
{
	return 0x20200;
}


// ROM 0x0004b87c GetStatus__16TCardCISIteratorFv
ULong
TCardCISIterator::GetStatus(void)
{
	return fStatus;
}


// ROM 0x0004b328 GetTupleData__16TCardCISIteratorFPUcUl
// The current tuple (code, link and data), no more than size bytes of it.
NewtonErr
TCardCISIterator::GetTupleData(UChar* buffer, ULong size)
{
	if ((ULong) fTupleLink + 2 <= size)
		size = (ULong) fTupleLink + 2;
	return ReadCIS(fTupleAddress, buffer, size, (fStatus & kCISStatusInAttrMemory) != 0);
}


// ROM 0x0004b36c GetPackage__16TCardCISIteratorFP12TCardPackageUc
// The next of Apple's vendor-unique tuples (0x8E, manufacturer 200, code
// 0x2000) that describes a package on the card: its type, attribute,
// address, length, version and name, CPU and OS strings.
NewtonErr
TCardCISIterator::GetPackage(TCardPackage* package, UChar fromStart)
{
	NewtonErr err;
	if (package == nil)
		err = kError_Bad_Parameters;
	else
	{
		UChar buffer[0x50];
		fSearchCode = 0x8E;
		err = GetTuple(fromStart);
		if (err == noErr && (err = GetTupleData(buffer, 0x50)) == noErr)
		{
			err = kError_Card_Bad_CIS;
			if (SwapLittleEndianShort(buffer + 2) == 200 && SwapLittleEndianShort(buffer + 4) == 0x2000)
			{
				err = noErr;
				package->fType = buffer[6];
				package->fAttribute = buffer[7];
				package->fAddress = SwapLittleEndianLong(buffer + 8);
				package->fLength = SwapLittleEndianLong(buffer + 12);
				package->fVersion = SwapLittleEndianLong(buffer + 16);
				package->fReserved0 = buffer[20];
				package->fReserved1 = buffer[21];
				char* name = (char*) buffer + 22;
				package->SetName(name);
				char* cpu = name + strlen(name) + 1;
				package->SetCPUType(cpu);
				package->SetOSType(cpu + strlen(cpu) + 1);
			}
		}
	}
	return err;
}


// ROM 0x0004b4ac VerifyLinkTargetTuple__16TCardCISIteratorFPUcT1Uc
// Whether a chain starts at `at` with CISTPL_LINKTARGET ("CIS"); if it
// does, the iterator is put there.
NewtonErr
TCardCISIterator::VerifyLinkTargetTuple(UChar* at, UChar* buffer, UChar inAttrMemory)
{
	if (at == nil)
		return kError_Card_No_CIS;
	NewtonErr err = ReadCIS(at, buffer, 5, inAttrMemory);
	if (err != noErr)
		return err;
	if (buffer[0] == 0x13 && buffer[2] == 'C' && buffer[3] == 'I' && buffer[4] == 'S')
	{
		fNextTuple = at;
		fLongLink = nil;
		fTupleAddress = at;
		if (inAttrMemory == 0)
			fStatus &= ~kCISStatusInAttrMemory;
		else
			fStatus |= kCISStatusInAttrMemory;
		fTupleCode = buffer[0];
		fTupleLink = buffer[1];
		return noErr;
	}
	return kError_Card_No_CIS;
}


// ROM 0x0004b560 SwapLittleEndianShort__16TCardCISIteratorFPUc
ULong
TCardCISIterator::SwapLittleEndianShort(UChar* p)
{
	return (ULong) p[0] + (ULong) p[1] * 0x100;
}


// ROM 0x0004b578 SwapLittleEndianLong__16TCardCISIteratorFPUc
ULong
TCardCISIterator::SwapLittleEndianLong(UChar* p)
{
	return (ULong) p[0] + (ULong) p[1] * 0x100 + (ULong) p[2] * 0x10000 + (ULong) p[3] * 0x1000000;
}


// ROM 0x0004b5a4 ResetFields__16TCardCISIteratorFv
// One CIS, starting at the beginning of attribute memory.
void
TCardCISIterator::ResetFields(void)
{
	fField03 = 0;
	fField04 = 0;
	fField08 = 0;
	fField13 = 0;
	fNumOfCISs = 1;
	fCurrentCIS = 0;
	fStopAtEvery = 0;
	fStatus = 0;
	fCISInAttrMemory = 1;
	fCISAddress[0] = (UChar*) fSocket->AttributeMemBaseAddr();
	for (ULong i = 1; i < kMaxCISs; i++)
		fCISAddress[i] = nil;
	ResetCIS();
}


// ROM 0x0004b610 ResetCIS__16TCardCISIteratorFv
// At the start of the current CIS.  The primary chain goes on, when it
// has no link of its own, at the start of common memory.
void
TCardCISIterator::ResetCIS(void)
{
	UChar* start = fCISAddress[fCurrentCIS];
	fNextTuple = start;
	fTupleAddress = start;
	if (fCurrentCIS == 0)
		fLongLink = (UChar*) fSocket->CommonMemBaseAddr();
	else
		fLongLink = nil;
	fTupleCode = 0xFF;
	fTupleLink = 0;
	ULong status = fStatus;
	fStatus = (status & 0xFFFFFF0F) | kCISStatusAtStart;
	if ((fCISInAttrMemory & (1 << fCurrentCIS)) != 0)
		fStatus = (status & 0xFFFFFF0F) | kCISStatusAtStart | kCISStatusInAttrMemory;
	if (fNumOfCISs > 1)
		fStatus |= fCurrentCIS == 0 ? kCISStatusMultiCIS : kCISStatusFunctionCIS;
}


// ROM 0x0004b6b0 Init__16TCardCISIteratorFP11TCardSocket
// Over the card in a socket: whether it has a CIS at all.  A card with no
// attribute memory - its common memory and attribute memory both starting
// 03 03 01 01 - has the CIS mirrored in common memory.  The first tuple
// after any nulls must be the end (0xFF) or a CISTPL_DEVICE of a sensible
// length.  ==> kError_Card_No_CIS for a blank one, kError_Card_Bad_CIS.
NewtonErr
TCardCISIterator::Init(TCardSocket* socket)
{
	fSocket = socket;
	ResetFields();
	fSearchCode = 0xFF;
	fStatus &= ~kCISStatusMirrored;
	NewtonErr err = noErr;
	newton_try
	{
		UChar* attr = (UChar*) fSocket->AttributeMemBaseAddr();
		UChar* common = (UChar*) fSocket->CommonMemBaseAddr();
		if (common[0] == 3 && common[1] == 3 && common[2] == 1 && common[3] == 1 && attr[1] == 3 && attr[3] == 1)
		{
			fStatus |= kCISStatusMirrored;
			fSocket->SetControl(fSocket->GetControl() & ~kCardByteAccess);
		}
		ULong i = 0;
		UChar code;
		do
		{
			code = CardAttrMemReadByte(Lane(attr + i));
			i += 2;
		} while (i <= 0xF && code == 0);
		fTupleCode = code;
		if (code == 0)
			err = kError_Card_No_CIS;
		else if ((code == 0x01 && CardAttrMemReadByte(Lane(attr + i)) < 0x21) || code == 0xFF)
			;
		else
			err = kError_Card_Bad_CIS;
		if (err != noErr)
			fStatus |= 1;
	}
	newton_catch(exPermissionViolation)
	{
		err = kError_Access_Permission;
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	if (err != noErr)
		fStatus |= kCISStatusError;
	return err;
}


// ROM 0x0004b884 SelectCIS__16TCardCISIteratorFUl
NewtonErr
TCardCISIterator::SelectCIS(ULong cisNumber)
{
	if (cisNumber < fNumOfCISs)
	{
		fCurrentCIS = (UChar) cisNumber;
		ResetCIS();
		return noErr;
	}
	return kError_Bad_Parameters;
}


// ROM 0x0004b8b4 ReadCIS__16TCardCISIteratorFPUcT1UlUc
// count bytes of the CIS from `from`: one byte in two in attribute memory
// (with the read delay), each byte in common memory, or - for a card whose
// CIS is mirrored there - each byte out of its word.
NewtonErr
TCardCISIterator::ReadCIS(UChar* from, UChar* buffer, ULong count, UChar inAttrMemory)
{
	NewtonErr err = noErr;
	newton_try
	{
		if ((fStatus & kCISStatusMirrored) == 0 || inAttrMemory != 0)
		{
			for (ULong i = 0; i < count; i++)
			{
				if (inAttrMemory == 0)
				{
					buffer[i] = *Lane(from);
					from += 1;
				}
				else
				{
					buffer[i] = CardAttrMemReadByte(Lane(from));
					from += 2;
				}
			}
		}
		else
		{
			for (ULong i = 0; i < count; i++)
			{
				buffer[i] = WordLane(from);
				from += 1;
			}
		}
	}
	newton_catch(exPermissionViolation)
	{
		err = kError_Access_Permission;
	}
	newton_catch_all
	{
		err = kError_Bus_Access;
	}
	end_try;
	if (err != noErr)
		fStatus |= kCISStatusError;
	return err;
}


// ROM 0x0004ba0c GetTuple__16TCardCISIteratorFUc
// The next tuple of the kind fSearchCode names (any, 0xFF), from the start
// of the current CIS if fromStart: nulls, long links (their targets noted
// for the chain's end), CISTPL_LONGLINK_MFC (the functions' CISs noted),
// CISTPL_NO_LINK and CISTPL_END (the chain followed on to its long link)
// are taken on the way and not answered, unless fStopAtEvery.  ==>
// kError_Card_No_CIS at the end of the last chain.
NewtonErr
TCardCISIterator::GetTuple(UChar fromStart)
{
	UChar buffer[0x30];
	NewtonErr err;
	if (fromStart != 0)
	{
		if ((fStatus & kCISStatusAtStart) == 0)
			ResetCIS();
		ULong status = fStatus;
		fStatus = status & ~kCISStatusAtStart;
		if (fCurrentCIS != 0)
		{
			Boolean inAttr = (status & kCISStatusInAttrMemory) != 0;
			err = VerifyLinkTargetTuple(fTupleAddress, buffer, inAttr);
			if (err != noErr)
				return err;
			fNextTuple = fTupleAddress + (((ULong) fTupleLink + 2) << inAttr);
		}
	}
	err = noErr;
	if (fNextTuple != nil || ((fNextTuple = fTupleAddress), fTupleAddress != nil))
	{
		fTupleCode = 0xFF;
		do
		{
			ULong headerSize = 2;
			Boolean taken = false;
			fTupleAddress = fNextTuple;
			err = ReadCIS(fNextTuple, buffer, 2, (fStatus & kCISStatusInAttrMemory) != 0);
			if (err != noErr)
				return err;
			fTupleLink = buffer[1];
			fTupleCode = buffer[0];
			if (buffer[0] == 0x12 || buffer[0] == 0x11)
			{
				// CISTPL_LONGLINK_C, CISTPL_LONGLINK_A: where this chain goes on
				// PatchPoint
				taken = true;
				err = GetTupleData(buffer, 6);
				if (err == noErr)
				{
					ULong address = SwapLittleEndianLong(buffer + 2);
					if (fTupleCode == 0x11)
					{
						fLongLink = (UChar*) fSocket->AttributeMemBaseAddr() + address * 2;
						fStatus |= kCISStatusLinkToAttr;
					}
					else
					{
						fLongLink = (UChar*) fSocket->CommonMemBaseAddr() + address;
						fStatus &= ~kCISStatusLinkToAttr;
					}
				}
			}
			else if (buffer[0] < 0x13)
			{
				err = noErr;
				if (buffer[0] == 0x00)
				{
					// CISTPL_NULL: one byte, no link
					taken = true;
					fTupleLink = 0;
					headerSize = 1;
				}
				else if (buffer[0] == 0x06)
				{
					// CISTPL_LONGLINK_MFC: the other functions' CISs
					// PatchPoint
					taken = true;
					err = GetTupleData(buffer, 0x2F);
					if (err == noErr)
					{
						fStatus = (fStatus & ~kCISStatusFunctionCIS) | kCISStatusMultiCIS;
						fNumOfCISs = buffer[2] + 1;
						UChar* entry = buffer + 3;
						for (ULong i = 1; i < fNumOfCISs; i++)
						{
							UChar space = entry[0];
							ULong address = SwapLittleEndianLong(entry + 1);
							if (space == 0)
							{
								fCISAddress[i] = (UChar*) fSocket->AttributeMemBaseAddr() + address * 2;
								fCISInAttrMemory |= (UChar) (1 << i);
							}
							else
								fCISAddress[i] = (UChar*) fSocket->CommonMemBaseAddr() + address;
							entry += 5;
						}
					}
				}
			}
			else
			{
				taken = (buffer[0] == 0x14);
				if (taken)
					fLongLink = nil;			// CISTPL_NO_LINK
				else if (buffer[0] == 0xFF)
				{
					// CISTPL_END: on to the chain's long link, if it has one
					// PatchPoint
					taken = true;
					fTupleLink = 0xFF;
					err = VerifyLinkTargetTuple(fLongLink, buffer, (fStatus & kCISStatusLinkToAttr) != 0);
				}
			}
			// PatchPoint
			if (err == noErr)
			{
				ULong length = (ULong) fTupleLink + headerSize;
				fNextTuple = fTupleAddress + (length << ((fStatus & kCISStatusInAttrMemory) != 0));
				if ((fStopAtEvery & 1) != 0)
					return noErr;
				if (!taken && (fSearchCode == 0xFF || fTupleCode == fSearchCode))
					return noErr;
			}
		} while (err == noErr);
	}
	return err;
}
