/*
	File:		hal/SerialChipRegistry.cpp

	Contains:	TSerialChip's and PSerialChipRegistry's glue and
				PTheSerChipRegistry, the ROM's registry of serial chips
				(hal/HALSerialChip.h): eight slots of (chip, hardware location),
				a chip's id being 0x80 + its slot; a chip registered is also
				registered with the name server under its location (a
				four-character name) and the type "TSerialChip", which is
				where a tool claims it; the default chip for a service is a
				configuration of the config server ("DefHWLoc").

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "HALSerialChip.h"
#include "UserPorts.h"
#include "HALOptions.h"
#include "UserSemaphore.h"
#include "SerialOptions.h"

// ROM 0x0c101b04 SerChipTypeStr
char*	SerChipTypeStr = (char*) "TSerialChip";

PSerialChipRegistry*	gSerialChipRegistry = nil;		// ROM 0x0c101b00 gSerialChipRegistry


/*------------------------------------------------------------------------------
	The interfaces' glue
------------------------------------------------------------------------------*/

// ROM 0x00384b0c New__11TSerialChipSFPc
TSerialChip*
TSerialChip::New(char* implementation)
{
	TSerialChip* p = (TSerialChip*) AllocInstanceByName("TSerialChip", implementation);
	return p != nil ? (TSerialChip*) p->GlueNew() : nil;
}


// ROM 0x00384b38 Delete__11TSerialChipFv
void
TSerialChip::Delete()
{
	GlueDelete();
}


// ROM 0x00384e34 New__19PSerialChipRegistrySFPc
PSerialChipRegistry*
PSerialChipRegistry::New(char* implementation)
{
	PSerialChipRegistry* p = (PSerialChipRegistry*) AllocInstanceByName("PSerialChipRegistry", implementation);
	return p != nil ? (PSerialChipRegistry*) p->GlueNew() : nil;
}


// ROM 0x00384e6c Delete__19PSerialChipRegistryFv
void
PSerialChipRegistry::Delete()
{
	GlueDelete();
}


// ROM 0x001d5ff4 GetSerialChipRegistry__Fv
PSerialChipRegistry*
GetSerialChipRegistry(void)
{
	return gSerialChipRegistry;
}


/*------------------------------------------------------------------------------
	P T h e S e r C h i p R e g i s t r y
------------------------------------------------------------------------------*/

#define kSerChipSlots		8
#define kSerChipIDBase		0x80

PROTOCOL PTheSerChipRegistry : public PSerialChipRegistry
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PTheSerChipRegistry);

	PTheSerChipRegistry*	New();
	void			Delete();

	NewtonErr		Init();
	NewtonErr		Register(TSerialChip* theChip, ULong hwLoc);
	NewtonErr		UnRegister(TSerialChip* theChip);
	NewtonErr		SetDefaultChip(ULong serviceType, ULong* hwLocation, Boolean onlyIfUnset);
	TSerialChip*	GetChipPtr(SerialChipID);
	ULong			GetChipLocation(SerialChipID);
	SerialChipID	FindByChip(TSerialChip* theChip);
	SerialChipID	FindByOption(TCMOSerialChipSpec* opt);
	SerialChipID	FindByLocation(ULong hwLocation);
	NewtonErr		ClaimSerialChip(SerialChipID, Boolean passive, TObjectId ownerPortId);
	NewtonErr		GetDefaultChip(ULong serviceType, ULong* hwLocation);

private:
	long			GetMutex(void);
	void			RelMutex(void);
	Boolean			IDToIndex(ULong id, ULong* index);
	ULong			IndexToID(ULong index);
	void			SetChipData(ULong index, TSerialChip* chip, ULong hwLoc);
	void			ClearEntry(ULong index);
	NewtonErr		FindNextFreeIndex(ULong* index);

	struct Entry
	{
		TSerialChip*	fChip;					// +0x10 + 8i
		ULong			fHWLoc;					// +0x14 + 8i
	};
	Entry				fEntries[kSerChipSlots];
	ULong				fNextIndex;				// +0x50  where FindNextFreeIndex looks from
	TUConfigServer*		fNameServer;			// +0x54
	TULockingSemaphore*	fLock;					// +0x58
};

PROTOCOL_IMPL_SOURCE_MACRO(PTheSerChipRegistry)		// ROM 0x001d6004 Sizeof__19PTheSerChipRegistrySFv
PROTOCOL_CLASSINFO(PTheSerChipRegistry, "PSerialChipRegistry", "", 0x10000, 0, nil)	// ROM 0x00384da0 ClassInfo__19PTheSerChipRegistrySFv


// ROM 0x001d600c FindByChip__19PTheSerChipRegistryFP11TSerialChip
SerialChipID
PTheSerChipRegistry::FindByChip(TSerialChip* theChip)
{
	for (long i = 0; i < kSerChipSlots; i++)
		if (fEntries[i].fChip == theChip)
			return i + kSerChipIDBase;
	return kNilSerChipID;
}


// ROM 0x001d6038 FindByLocation__19PTheSerChipRegistryFUl
// (A PCMCIA slot of either socket, 'slot, is looked for among the chips
// and not found: the ROM's loop over them does nothing - kept.)
SerialChipID
PTheSerChipRegistry::FindByLocation(ULong hwLocation)
{
	for (long i = 0; i < kSerChipSlots; i++)
		if (fEntries[i].fHWLoc == hwLocation)
			return i + kSerChipIDBase;
	if (hwLocation == 'slot')
	{
		for (long i = 0; i < kSerChipSlots; i++)
			;
		return kNilSerChipID;
	}
	return kNilSerChipID;
}


// ROM 0x001d6084 FindByOption__19PTheSerChipRegistryFP18TCMOSerialChipSpec
// The first chip whose own spec (its answer to the option, got as current)
// has everything the option asks for: the location if one is given, the
// features and the outputs, inputs, parities and data/stop bits it
// supports, not in use if that is asked, and the card's manufacturer and
// its info if they are given.
SerialChipID
PTheSerChipRegistry::FindByOption(TCMOSerialChipSpec* opt)
{
	TCMOSerialChipSpec spec;
	spec.SetOpCode(opGetCurrent);
	for (ULong i = 0; i < kSerChipSlots; i++)
	{
		if (fEntries[i].fChip == nil || fEntries[i].fChip->ProcessOption(&spec) != noErr)
			continue;
		if (opt->fHWLoc != 0 && opt->fHWLoc != spec.fHWLoc)
			continue;
		if ((opt->fSerFeatures & spec.fSerFeatures) != opt->fSerFeatures
		 || (opt->fSerOutSupported & spec.fSerOutSupported) != opt->fSerOutSupported
		 || (opt->fSerInSupported & spec.fSerInSupported) != opt->fSerInSupported
		 || (opt->fParitySupport & spec.fParitySupport) != opt->fParitySupport
		 || (opt->fDataStopBitSupport & spec.fDataStopBitSupport) != opt->fDataStopBitSupport)
			continue;
		if (opt->fChipNotInUse && !spec.fChipNotInUse)
			continue;
		if (opt->fCIS_ManFID != 0 && opt->fCIS_ManFID != spec.fCIS_ManFID)
			continue;
		if (opt->fCIS_ManFIDInfo != 0 && opt->fCIS_ManFIDInfo != spec.fCIS_ManFIDInfo)
			continue;
		return IndexToID(i);
	}
	return kNilSerChipID;
}


// ROM 0x001d61a4 ClaimSerialChip__19PTheSerChipRegistryFUlUcT1
// The chip claimed (or, for no owner, unclaimed) with the name server,
// actively or passively.
NewtonErr
PTheSerChipRegistry::ClaimSerialChip(SerialChipID id, Boolean passive, TObjectId ownerPortId)
{
	ULong location = GetChipLocation(id);
	if (location == 0)
		return kInvalidSerChipID;
	GetMutex();
	char name[8];
	fNameServer->ULongStrToCStr(location, name);
	NewtonErr err;
	if (ownerPortId == 0)
		err = passive ? fNameServer->ResourcePassiveUnclaim(name, SerChipTypeStr)
					  : fNameServer->ResourceUnclaim(name, SerChipTypeStr);
	else
		err = passive ? fNameServer->ResourcePassiveClaim(name, SerChipTypeStr, ownerPortId, 0)
					  : fNameServer->ResourceClaim(name, SerChipTypeStr, ownerPortId, 0);
	RelMutex();
	return err;
}


// ROM 0x001d6264 GetDefaultChip__19PTheSerChipRegistryFUlPUl
NewtonErr
PTheSerChipRegistry::GetDefaultChip(ULong serviceType, ULong* hwLocation)
{
	GetMutex();
	NewtonErr err = fNameServer->GetDefaultHWLoc(serviceType, hwLocation, nil);
	RelMutex();
	return err;
}


// ROM 0x001d62a8 GetMutex__19PTheSerChipRegistryFv
long
PTheSerChipRegistry::GetMutex(void)
{
	return fLock->Acquire(kWaitOnBlock);
}


// ROM 0x001d62b4 RelMutex__19PTheSerChipRegistryFv
void
PTheSerChipRegistry::RelMutex(void)
{
	fLock->Release();
}


// ROM 0x001d62bc IDToIndex__19PTheSerChipRegistryFUlPUl
Boolean
PTheSerChipRegistry::IDToIndex(ULong id, ULong* index)
{
	*index = id - kSerChipIDBase;
	return id >= kSerChipIDBase && id <= kSerChipIDBase + kSerChipSlots - 1;
}


// ROM 0x001d62e4 IndexToID__19PTheSerChipRegistryFUl
ULong
PTheSerChipRegistry::IndexToID(ULong index)
{
	return index + kSerChipIDBase;
}


// ROM 0x001d62ec SetChipData__19PTheSerChipRegistryFUlP11TSerialChipT1
void
PTheSerChipRegistry::SetChipData(ULong index, TSerialChip* chip, ULong hwLoc)
{
	fEntries[index].fChip = chip;
	fEntries[index].fHWLoc = hwLoc;
}


// ROM 0x001d62fc New__19PTheSerChipRegistryFv
PTheSerChipRegistry*
PTheSerChipRegistry::New(void)
{
	for (ULong i = 0; i < kSerChipSlots; i++)
		ClearEntry(i);
	fNextIndex = 0;
	fNameServer = nil;
	fLock = nil;
	gSerialChipRegistry = this;
	return this;
}


// ROM 0x001d634c ClearEntry__19PTheSerChipRegistryFUl
void
PTheSerChipRegistry::ClearEntry(ULong index)
{
	fEntries[index].fHWLoc = 0;
	fEntries[index].fChip = nil;
}


// ROM 0x001d6360 FindNextFreeIndex__19PTheSerChipRegistryFPUl
// The next empty slot after the last one found, round the eight.
NewtonErr
PTheSerChipRegistry::FindNextFreeIndex(ULong* index)
{
	for (long tries = 0; ; )
	{
		ULong i = fNextIndex + 1;
		if ((long) i > kSerChipSlots - 1)
			i = 0;
		fNextIndex = i;
		if (fEntries[i].fChip == nil)
		{
			*index = i;
			return noErr;
		}
		if (++tries > kSerChipSlots - 1)
			return kRegistryFullError;
	}
}


// ROM 0x001d63a8 Init__19PTheSerChipRegistryFv
// DEVIATION: the ROM wires the registry's memory (LockHeapRange: it is
// read at interrupt time); host memory needs no wiring.
NewtonErr
PTheSerChipRegistry::Init(void)
{
	fNameServer = new TUConfigServer;
	if (fNameServer == nil)
		return -10007;			// (0xffffd8e9)
	fLock = new TULockingSemaphore;
	if (fLock == nil)
		return -10007;
	NewtonErr err = fLock->Init();
	if (err != noErr)
		return err;
	return fNameServer->RegisterName((char*) "", (char*) "PTheSerChipRegistry", (ULong) this, 0);
}


// ROM 0x001d6478 Delete__19PTheSerChipRegistryFv
void
PTheSerChipRegistry::Delete(void)
{
	fNameServer->UnRegisterName((char*) "", (char*) "PTheSerChipRegistry");
	delete fNameServer;
	fNameServer = nil;
	delete fLock;
	fLock = nil;
}


// ROM 0x001d64e8 Register__19PTheSerChipRegistryFP11TSerialChipUl
// The chip in a free slot, and named to the name server by its location.
NewtonErr
PTheSerChipRegistry::Register(TSerialChip* theChip, ULong hwLoc)
{
	GetMutex();
	ULong index;
	NewtonErr err = FindNextFreeIndex(&index);
	if (err == noErr)
	{
		SetChipData(index, theChip, hwLoc);
		err = fNameServer->RegisterULongName(hwLoc, SerChipTypeStr, IndexToID(index), 0);
		if (err != noErr)
			ClearEntry(index);
	}
	RelMutex();
	return err;
}


// ROM 0x001d6584 UnRegister__19PTheSerChipRegistryFP11TSerialChip
NewtonErr
PTheSerChipRegistry::UnRegister(TSerialChip* theChip)
{
	SerialChipID id = FindByChip(theChip);
	ULong location;
	if (id == kNilSerChipID || (location = GetChipLocation(id)) == 0)
		return kInvalidSerChipID;
	GetMutex();
	NewtonErr err = fNameServer->UnRegisterULongName(location, SerChipTypeStr);
	RelMutex();
	ULong index;
	IDToIndex(id, &index);
	ClearEntry(index);
	return err;
}


// ROM 0x001d6614 SetDefaultChip__19PTheSerChipRegistryFUlPUlUc
// The service's default location set - unless onlyIfUnset and one is set
// already, which answers -10068 (and, for a location of nought, the
// default is set to nought without looking).
NewtonErr
PTheSerChipRegistry::SetDefaultChip(ULong serviceType, ULong* hwLocation, Boolean onlyIfUnset)
{
	ULong location = *hwLocation;
	GetMutex();
	if (location != 0
	 && fNameServer->GetDefaultHWLoc(serviceType, hwLocation, nil) == noErr
	 && onlyIfUnset)
	{
		RelMutex();
		return -10068;
	}
	NewtonErr err = fNameServer->SetDefaultHWLoc(serviceType, location, 0);
	RelMutex();
	return err;
}


// ROM 0x001d66a0 GetChipLocation__19PTheSerChipRegistryFUl
ULong
PTheSerChipRegistry::GetChipLocation(SerialChipID id)
{
	ULong index;
	if (!IDToIndex(id, &index))
		return 0;
	return fEntries[index].fHWLoc;
}


// ROM 0x001d66d4 GetChipPtr__19PTheSerChipRegistryFUl
TSerialChip*
PTheSerChipRegistry::GetChipPtr(SerialChipID id)
{
	ULong index;
	if (!IDToIndex(id, &index))
		return nil;
	return fEntries[index].fChip;
}


/*------------------------------------------------------------------------------
	Making the registry
------------------------------------------------------------------------------*/

// The part of InitializeCommHardware (ROM 0x000ea0b4) that makes the
// registry: an instance made straight from the implementation's class info
// (not registered by name) and initialised; its New makes it the global.
// (The rest - the FIQ timer, the debug links, the Voyager chips - is the
// machine's; a host registers chips of its own.)
NewtonErr
InitSerialChipRegistry(void)
{
	if (gSerialChipRegistry != nil)
		return noErr;
	PSerialChipRegistry* registry = (PSerialChipRegistry*) PTheSerChipRegistry::ClassInfo()->New();
	if (registry == nil)
		return -10007;
	return registry->Init();
}
