/*
	File:		protocols/ClassInfoRegistry.cpp

	Contains:	TClassInfoRegistryImpl (ClassInfoRegistry.h), the protocol
				registry monitor, with its entry comparator and the matching
				helpers.

	Reconstructed from the MP2100 D ROM; each function cites its origin.
	The monitor entry and the class info are what ProtocolGen generated for
	the implementation (0x0037c8e0-0x0037ca9c), re-expressed as Protocols.h
	describes.
*/

#include "ClassInfoRegistry.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>

extern const ExceptionName exAbort;
extern const ExceptionName exBusError;

typedef unsigned int	ULong32;		// the ROM's word, for the hash arithmetic


PROTOCOL_IMPL_SOURCE_MACRO(TClassInfoRegistryImpl)		// ROM 0x0005c0e4 Sizeof__22TClassInfoRegistryImplSFv
PROTOCOL_CLASSINFO(TClassInfoRegistryImpl, "TClassInfoRegistry", "", 0, 0, TClassInfoRegistryImpl::MonitorEntry)	// ROM 0x0037c91c ClassInfo__22TClassInfoRegistryImplSFv


/* -------------------------------------------------------------------------------
	Matching
------------------------------------------------------------------------------- */

// ROM 0x0005c9f0 CompareKeys__20TClassInfoComparatorCFPCvT1
// Entries order by interface hash, implementation hash, then version.
int
TClassInfoComparator::CompareKeys(const void* key1, const void* key2) const
{
	const SProtocolEntry* a = (const SProtocolEntry*) key1;
	const SProtocolEntry* b = (const SProtocolEntry*) key2;
	if (a->fInterfaceHash < b->fInterfaceHash)
		return -1;
	if (a->fInterfaceHash > b->fInterfaceHash)
		return 1;
	if (a->fImplementationHash < b->fImplementationHash)
		return -1;
	if (a->fImplementationHash > b->fImplementationHash)
		return 1;
	if (a->fVersion < b->fVersion)
		return -1;
	if (a->fVersion > b->fVersion)
		return 1;
	return 0;
}


// ROM 0x0005c10c SatisfiesHash__FPC14SProtocolEntryUcCUsT2T3CUl
// Whether an entry is still within the run of entries a query walks: the
// hashes it checks agree and the version is enough.  (An unchecked hash
// ends the test early: an interface-only query walks every version and
// implementation of that interface's hash.)
static Boolean
SatisfiesHash(const SProtocolEntry* entry, Boolean checkInterface, const UShort interfaceHash, Boolean checkImplementation, const UShort implementationHash, const ULong minVersion)
{
	if (entry == nil)
		return false;
	if (!checkInterface)
		return true;
	if (entry->fInterfaceHash != interfaceHash)
		return false;
	if (!checkImplementation)
		return true;
	if (entry->fImplementationHash != implementationHash)
		return false;
	return entry->fVersion >= minVersion;
}


// ROM 0x0005c180 Satisfies__FPC10TClassInfoPCcT2Ul
// Whether a class info answers to the names (a nil name matches any) at
// the version or later; a class info that cannot be read does not.
static Boolean
Satisfies(const TClassInfo* info, const char* intf, const char* impl, ULong version)
{
	Boolean satisfies = false;
	newton_try
	{
		if ((intf == nil || strcmp(intf, info->InterfaceName()) == 0)
		 && (impl == nil || strcmp(impl, info->ImplementationName()) == 0))
			satisfies = info->Version() >= version;
	}
	newton_catch(exBusError)
	{
		satisfies = false;
	}
	end_try;
	return satisfies;
}


// ROM 0x0005c25c SatisifiesCapabilities__FP14SProtocolEntryPCcT2  (sic)
// Whether the entry's class info has the capability, with the value when
// one is asked for.
static Boolean
SatisifiesCapabilities(SProtocolEntry* entry, const char* capability, const char* value)
{
	if (entry == nil)
		return false;
	if (capability == nil)
		return true;
	const char* has = entry->fClassInfo->GetCapability(capability);
	if (has == nil)
		return false;
	if (value == nil)
		return true;
	return strcmp(has, value) == 0;
}


/* -------------------------------------------------------------------------------
	TClassInfoRegistryImpl
------------------------------------------------------------------------------- */

// ROM 0x0005cb40 New__22TClassInfoRegistryImplFv
// (the ROM's NSortedArray::Init: 0x14-byte entries, chunks of 0xa0, room
// for 0x68, never shrinking)
TClassInfoRegistryImpl*
TClassInfoRegistryImpl::New()
{
	fComparator = new TClassInfoComparator;
	fRegistry = new NSortedArray;
	fRegistry->Init(fComparator, sizeof(SProtocolEntry), 0xa0, 0x68, false);
	fSeed = 1;
	fSatisfyCache = (const TClassInfo**) NewPtr(kSatisfyCacheSize * sizeof(const TClassInfo*));
	InvalidateSatisfyCache();
	return this;
}


// ROM 0x0005cbc8 Delete__22TClassInfoRegistryImplFv
// (the comparator and the cache are not freed: as in the ROM)
void
TClassInfoRegistryImpl::Delete()
{
	if (fRegistry != nil)
		delete fRegistry;
}


// ROM 0x0005cbdc HashString__22TClassInfoRegistryImplCFPCc
// A rotate-and-xor of the characters, scrambled to 16 bits; 0xffff for no
// string.
UShort
TClassInfoRegistryImpl::HashString(const char* s) const
{
	if (s == nil)
		return 0xffff;
	ULong32 hash = 0;
	for (; *s != 0; s++)
		hash = ((hash >> 7) | (hash << 25)) ^ (unsigned char) *s;
	return (UShort) (hash * 0x9E3779B9);
}


// ROM 0x0005c0ec InvalidateSatisfyCache__22TClassInfoRegistryImplFv
void
TClassInfoRegistryImpl::InvalidateSatisfyCache()
{
	for (ULong i = 0; i < kSatisfyCacheSize; i++)
		fSatisfyCache[i] = nil;
}


// ROM 0x0005cf00 Find__22TClassInfoRegistryImplCFPC10TClassInfo
SProtocolEntry*
TClassInfoRegistryImpl::Find(const TClassInfo* info) const
{
	long count = fRegistry->fCount;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(0);
	for (long i = 0; i < count; i++, entry++)
	{
		if (entry->fClassInfo == info)
			return entry;
	}
	return nil;
}


// ROM 0x0005c2b4 Satisfy__22TClassInfoRegistryImplCFPC10TClassInfo
// The entry a request for this class info's names and version would get:
// the sorted array puts the highest version of the names last, so the walk
// goes down from where a version of 0xffffffff would be.
SProtocolEntry*
TClassInfoRegistryImpl::Satisfy(const TClassInfo* info) const
{
	SProtocolEntry* found = nil;
	newton_try
	{
		const char* intf = info->InterfaceName();
		const char* impl = info->ImplementationName();
		ULong version = info->Version();
		SProtocolEntry key;
		key.fInterfaceHash = HashString(intf);
		key.fImplementationHash = HashString(impl);
		key.fVersion = 0xffffffff;
		long index = fRegistry->Where(&key) - 1;
		SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(index);
		while (SatisfiesHash(entry, true, key.fInterfaceHash, true, key.fImplementationHash, version))
		{
			if (Satisfies(entry->fClassInfo, intf, impl, version))
			{
				found = entry;
				break;
			}
			entry = (SProtocolEntry*) fRegistry->At(--index);
		}
	}
	newton_catch(exBusError)
	{
		found = nil;
	}
	end_try;
	return found;
}


// ROM 0x0005cc38 Register__22TClassInfoRegistryImplFPC10TClassInfoUl
// A class info already registered has its count raised; a new one gets an
// entry (its count 0 - the ROM's) in sorted position.  The names are read
// first, so that an unreadable class info fails here rather than later.
NewtonErr
TClassInfoRegistryImpl::Register(const TClassInfo* info, ULong refCon)
{
	NewtonErr result = noErr;
	if (info == nil)
		return -1;
	newton_try
	{
		strlen(info->ImplementationName());
		strlen(info->InterfaceName());
		strlen(info->Signature());
		SProtocolEntry* entry = Find(info);
		if (entry == nil)
		{
			InvalidateSatisfyCache();
			SProtocolEntry newEntry;
			newEntry.fClassInfo = info;
			newEntry.fRefCon = refCon;
			newEntry.fRegisterCount = 0;
			newEntry.fInstanceCount = 0;
			newEntry.fInterfaceHash = HashString(info->InterfaceName());
			newEntry.fImplementationHash = HashString(info->ImplementationName());
			newEntry.fVersion = info->Version();
			long where = fRegistry->Where(&newEntry);
			result = fRegistry->InsertElements(where, 1, &newEntry);
		}
		else
			entry->fRegisterCount++;
		fSeed++;
	}
	newton_catch(exAbort)
	{
		result = -1;
	}
	end_try;
	return result;
}


// ROM 0x0005cdc0 DeRegister__22TClassInfoRegistryImplFPC10TClassInfoUc
// The entry (this class info's, or the one satisfying its names) loses a
// registration and goes when none is left.
NewtonErr
TClassInfoRegistryImpl::DeRegister(const TClassInfo* info, Boolean specific)
{
	NewtonErr result = noErr;
	newton_try
	{
		SProtocolEntry* entry = specific ? Find(info) : Satisfy(info);
		if (entry == nil)
			result = -1;
		else
		{
			if (entry->fRegisterCount > 0)
				entry->fRegisterCount--;
			if (entry->fRegisterCount == 0)
			{
				InvalidateSatisfyCache();
				long index = fRegistry->Contains(entry);
				result = fRegistry->RemoveElements(index, 1);
				fSeed++;
			}
		}
	}
	newton_catch(exAbort)
	{
		result = -1;
	}
	end_try;
	return result;
}


// ROM 0x0005ced0 IsRegistered__22TClassInfoRegistryImplCFPC10TClassInfoUc
Boolean
TClassInfoRegistryImpl::IsRegistered(const TClassInfo* info, Boolean specific) const
{
	return (specific ? Find(info) : Satisfy(info)) != nil;
}


// ROM 0x0005c45c Satisfy__22TClassInfoRegistryImplCFPCcT1Ul
// The class info for the names at the version or later - the latest one
// registered of the highest version.  A nil name matches any.  Exact
// queries for any version are answered from, and enter, the cache.
const TClassInfo*
TClassInfoRegistryImpl::Satisfy(const char* intf, const char* impl, ULong version) const
{
	Boolean cacheable = intf != nil && impl != nil && version == 0;
	if (cacheable)
	{
		const TClassInfo* cached = nil;
		newton_try
		{
			for (ULong i = 0; i < kSatisfyCacheSize && cached == nil; i++)
			{
				const TClassInfo* info = fSatisfyCache[i];
				if (info != nil && strcmp(intf, info->InterfaceName()) == 0 && strcmp(impl, info->ImplementationName()) == 0)
					cached = info;
			}
		}
		newton_catch(exBusError)
		{
			cached = nil;
		}
		end_try;
		if (cached != nil)
			return cached;
	}

	SProtocolEntry key;
	key.fInterfaceHash = HashString(intf);
	key.fImplementationHash = HashString(impl);
	key.fVersion = 0xffffffff;
	Boolean checkInterface = intf != nil;
	Boolean checkImplementation = impl != nil;
	SProtocolEntry* found = nil;
	long index = fRegistry->Where(&key) - 1;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(index);
	while (SatisfiesHash(entry, checkInterface, key.fInterfaceHash, checkImplementation, key.fImplementationHash, version))
	{
		if (Satisfies(entry->fClassInfo, intf, impl, version))
		{
			found = entry;
			break;
		}
		entry = (SProtocolEntry*) fRegistry->At(--index);
	}
	if (found == nil)
		return nil;
	if (cacheable)
	{
		for (ULong i = kSatisfyCacheSize - 1; i > 0; i--)
			fSatisfyCache[i] = fSatisfyCache[i - 1];
		fSatisfyCache[0] = found->fClassInfo;
	}
	return found->fClassInfo;
}


// ROM 0x0005c6e0 Satisfy__22TClassInfoRegistryImplCFPCcN21
const TClassInfo*
TClassInfoRegistryImpl::Satisfy(const char* intf, const char* impl, const char* capability) const
{
	return Satisfy(intf, impl, capability, (const char*) nil);
}


// ROM 0x0005c704 Satisfy__22TClassInfoRegistryImplCFPCcN31
// As Satisfy(intf, impl, 0), also requiring the capability (with the value,
// if one is given); not cached.
const TClassInfo*
TClassInfoRegistryImpl::Satisfy(const char* intf, const char* impl, const char* capability, const char* capabilityValue) const
{
	SProtocolEntry key;
	key.fInterfaceHash = HashString(intf);
	key.fImplementationHash = HashString(impl);
	key.fVersion = 0xffffffff;
	Boolean checkInterface = intf != nil;
	Boolean checkImplementation = impl != nil;
	SProtocolEntry* found = nil;
	long index = fRegistry->Where(&key) - 1;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(index);
	while (SatisfiesHash(entry, checkInterface, key.fInterfaceHash, checkImplementation, key.fImplementationHash, 0))
	{
		if (Satisfies(entry->fClassInfo, intf, impl, 0)
		 && (capability == nil || SatisifiesCapabilities(entry, capability, capabilityValue)))
		{
			found = entry;
			break;
		}
		entry = (SProtocolEntry*) fRegistry->At(--index);
	}
	return found != nil ? found->fClassInfo : nil;
}


// ROM 0x0005c894 Satisfy__22TClassInfoRegistryImplCFPCcT1ClT3
// Four-character capability name and value.
const TClassInfo*
TClassInfoRegistryImpl::Satisfy(const char* intf, const char* impl, const long capability, const long capabilityValue) const
{
	char name[5];
	char value[5];
	const char* capabilityName = nil;
	const char* capabilityValueName = nil;
	if (capability != 0)
	{
		name[4] = 0;
		value[4] = 0;
		for (int i = 0; i < 4; i++)
		{
			name[i] = (char) (((ULong) capability) >> (24 - 8 * i));
			value[i] = (char) (((ULong) capabilityValue) >> (24 - 8 * i));
		}
		capabilityName = name;
		if (capabilityValue != 0)
			capabilityValueName = value;
	}
	return Satisfy(intf, impl, capabilityName, capabilityValueName);
}


// ROM 0x0005ca48 Seed__22TClassInfoRegistryImplCFv
long
TClassInfoRegistryImpl::Seed() const
{
	return fSeed;
}


// ROM 0x0005c914 First__22TClassInfoRegistryImplCFlPUl
// (the seed is not looked at)
const TClassInfo*
TClassInfoRegistryImpl::First(long /*seed*/, ULong* pRefCon) const
{
	if (fRegistry->fCount == 0)
		return nil;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(0);
	if (pRefCon != nil)
		*pRefCon = entry->fRefCon;
	return entry->fClassInfo;
}


// ROM 0x0005c964 Next__22TClassInfoRegistryImplCFlPC10TClassInfoPUl
// The entry after from's, if the registry has not changed since the seed
// was taken (a seed of 0 does not care).
const TClassInfo*
TClassInfoRegistryImpl::Next(long seed, const TClassInfo* from, ULong* pRefCon) const
{
	if (seed != 0 && seed != fSeed)
		return nil;
	long count = fRegistry->fCount;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(0);
	Boolean found = false;
	for (long i = 0; i < count; i++, entry++)
	{
		if (found)
		{
			if (pRefCon != nil)
				*pRefCon = entry->fRefCon;
			return entry->fClassInfo;
		}
		if (entry->fClassInfo == from)
			found = true;
	}
	return nil;
}


// ROM 0x0005ca50 Find__22TClassInfoRegistryImplCFPCcT1iPUl
// The skipCount'th class info (from 0) in registry order answering to the
// names.
const TClassInfo*
TClassInfoRegistryImpl::Find(const char* intf, const char* impl, int skipCount, ULong* pRefCon) const
{
	long count = fRegistry->fCount;
	SProtocolEntry* entry = (SProtocolEntry*) fRegistry->At(0);
	for (long i = 0; i < count; i++, entry++)
	{
		if (Satisfies(entry->fClassInfo, intf, impl, 0))
		{
			if (skipCount-- == 0)
			{
				if (pRefCon != nil)
					*pRefCon = entry->fRefCon;
				return entry->fClassInfo;
			}
		}
	}
	return nil;
}


// ROM 0x0005cae4 UpdateInstanceCount__22TClassInfoRegistryImplFPC10TClassInfol
void
TClassInfoRegistryImpl::UpdateInstanceCount(const TClassInfo* info, long adjustment)
{
	SProtocolEntry* entry = Find(info);
	if (entry != nil)
		entry->fInstanceCount = (UShort) (entry->fInstanceCount + adjustment);
}


// ROM 0x0005cb1c GetInstanceCount__22TClassInfoRegistryImplFPC10TClassInfo
long
TClassInfoRegistryImpl::GetInstanceCount(const TClassInfo* info)
{
	SProtocolEntry* entry = Find(info);
	return entry != nil ? entry->fInstanceCount : 0;
}


// ROM 0x0037c99c (unnamed) - the monitor entry ProtocolGen generated: the
// selector picks the method, the arguments come from the block, the result
// goes back in it.  -1 for a selector that is not one of ours.
long
TClassInfoRegistryImpl::MonitorEntry(void* instance, ULong selector, void* argBlock)
{
	TClassInfoRegistryImpl* self = (TClassInfoRegistryImpl*) ((TProtocol*) instance)->fRealThis;
	ProtocolMonitorArgs* args = (ProtocolMonitorArgs*) argBlock;
	ULong* a = args->fArg;
	switch (selector)
	{
	case kClassInfoRegistry_New:
		args->fResult = (Long) self->New();
		break;
	case kClassInfoRegistry_Delete:
		self->Delete();
		args->fResult = 0;
		break;
	case kClassInfoRegistry_Register:
		args->fResult = self->Register((const TClassInfo*) a[0], a[1]);
		break;
	case kClassInfoRegistry_DeRegister:
		args->fResult = self->DeRegister((const TClassInfo*) a[0], (Boolean) a[1]);
		break;
	case kClassInfoRegistry_IsRegistered:
		args->fResult = self->IsRegistered((const TClassInfo*) a[0], (Boolean) a[1]);
		break;
	case kClassInfoRegistry_Satisfy:
		args->fResult = (Long) self->Satisfy((const char*) a[0], (const char*) a[1], a[2]);
		break;
	case kClassInfoRegistry_Seed:
		args->fResult = self->Seed();
		break;
	case kClassInfoRegistry_First:
		args->fResult = (Long) self->First((long) a[0], (ULong*) a[1]);
		break;
	case kClassInfoRegistry_Next:
		args->fResult = (Long) self->Next((long) a[0], (const TClassInfo*) a[1], (ULong*) a[2]);
		break;
	case kClassInfoRegistry_Find:
		args->fResult = (Long) self->Find((const char*) a[0], (const char*) a[1], (int) a[2], (ULong*) a[3]);
		break;
	case kClassInfoRegistry_SatisfyCapability:
		args->fResult = (Long) self->Satisfy((const char*) a[0], (const char*) a[1], (const char*) a[2]);
		break;
	case kClassInfoRegistry_SatisfyCapabilityValue:
		args->fResult = (Long) self->Satisfy((const char*) a[0], (const char*) a[1], (const char*) a[2], (const char*) a[3]);
		break;
	case kClassInfoRegistry_SatisfyLongCapability:
		args->fResult = (Long) self->Satisfy((const char*) a[0], (const char*) a[1], (long) a[2], (long) a[3]);
		break;
	case kClassInfoRegistry_UpdateInstanceCount:
		self->UpdateInstanceCount((const TClassInfo*) a[0], (long) a[1]);
		args->fResult = 0;
		break;
	case kClassInfoRegistry_GetInstanceCount:
		args->fResult = self->GetInstanceCount((const TClassInfo*) a[0]);
		break;
	default:
		return -1;
	}
	return 0;
}
