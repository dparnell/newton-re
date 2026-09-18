/*
	File:		protocols/Protocols.cpp

	Contains:	TProtocol, TClassInfo, the by-name instance makers and the
				protocol registry's startup (Protocols.h), and the client
				side of TClassInfoRegistry - the monitor-call glue ProtocolGen
				generated for it.

	Reconstructed from the MP2x00 US ROM (0x0005b740-0x0005cce0 and the
	registry glue at 0x00385f04-0x00386040); each function cites its origin.
	Where the ROM decodes its class-info table (a self-relative offset, a
	branch instruction) this calls the pointer that stands for it - see
	Protocols.h for the correspondence.
*/

#include "Protocols.h"
#include "ClassInfoRegistry.h"
#include "UserMonitor.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"

#include <string.h>

// what a failed monitor call throws (the ROM has the string in the glue, 0x0037c7dc)
static const ExceptionName exMonitorCall = (ExceptionName) "evt.ex.moncall";

TClassInfoRegistry*	gProtocolRegistry = nil;		// 0x0c100b58


/* -------------------------------------------------------------------------------
	TProtocol
------------------------------------------------------------------------------- */

// ROM 0x0005b740 ClassInfo__9TProtocolCFv
// (the ROM: dispatch slot 1 of the real this)
const TClassInfo*
TProtocol::ClassInfo() const
{
	return fRealThis->fBTable;
}


// ROM 0x0005c708 Become__9TProtocolFPC9TProtocol
// Forwarding to another instance: unused in the ROM, and only the class
// info follows here (calls dispatch through this object's own vtable).
void
TProtocol::Become(const TProtocol* instance)
{
	fRealThis = instance;
}


// ROM 0x0005c5bc Become__9TProtocolFUl
void
TProtocol::Become(TObjectId monitorId)
{
	fMonitorId = monitorId;
}


// ROM 0x0005ca4c SetType__9TProtocolFPC10TClassInfo
// (the ROM points the instance at the class info's dispatch table)
void
TProtocol::SetType(const TClassInfo* info)
{
	fBTable = info;
}


// ROM 0x0005ccc4 GetMonitorId__9TProtocolCFv
TObjectId
TProtocol::GetMonitorId() const
{
	return fMonitorId;
}


// ROM 0x0005cbe4 StartMonitor__9TProtocolFUlN21Uc
// The instance becomes a monitor: a monitor task running the class info's
// entry proc on it.
long
TProtocol::StartMonitor(unsigned long stackSize, TObjectId environment, ULong name, Boolean rebootProtected)
{
	TUMonitor monitor(0);
	Boolean faultMonitor = false;
	long err = monitor.Init((MonitorProcPtr) ClassInfo()->EntryProc(), stackSize, this, environment, faultMonitor, name, rebootProtected);
	if (err == noErr)
	{
		monitor.SetDestroyKernelObject(false);
		fMonitorId = monitor.fId;
	}
	return err;
}


// ROM 0x0005cc80 DestroyMonitor__9TProtocolFv
// The monitor goes, and the instance with it (the ROM's free is DisposPtr).
long
TProtocol::DestroyMonitor()
{
	TUMonitor* monitor = new TUMonitor(fMonitorId);
	if (monitor == nil)
		return MemError();
	monitor->SetDestroyKernelObject(true);
	delete monitor;
	DisposPtr((Ptr) fRuntime);
	return noErr;
}


// The New(char*)/Delete() glue of a protocol interface (ROM: e.g.
// TSerialChip::New 0x0037b3ac, TSerialChip::Delete 0x0037b3d8): dispatch
// slot 2 or 3 on the real this, Delete followed by FreeInstance.
TProtocol*
TProtocol::GlueNew()
{
	return ClassInfo()->fDefaultNew((TProtocol*) fRealThis);
}


void
TProtocol::GlueDelete()
{
	ClassInfo()->fDefaultDelete((TProtocol*) fRealThis);
	FreeInstance(this);
}


// ROM 0x0037c7a4 (unnamed) - the monitor-call glue of TClassInfoRegistry
// (every MONITOR interface has one): the call goes to the monitor with the
// selector; a monitor error is thrown, otherwise the result comes back
// through the argument block.
Long
TProtocol::MonitorCall(ULong selector, ProtocolMonitorArgs* args)
{
	long err = MonitorDispatchSWI(fMonitorId, selector, args);
	if (err != noErr)
		Throw(exMonitorCall, nil, nil);
	return args->fResult;
}


/* -------------------------------------------------------------------------------
	TClassInfo
------------------------------------------------------------------------------- */

// ROM 0x0005cccc ImplementationName__10TClassInfoCFv
const char*
TClassInfo::ImplementationName() const
{
	return PrivateClassInfoImplementationName(this);
}


// ROM 0x0005ccd0 InterfaceName__10TClassInfoCFv
const char*
TClassInfo::InterfaceName() const
{
	return PrivateClassInfoInterfaceName(this);
}


// ROM 0x0005ccd4 Signature__10TClassInfoCFv
const char*
TClassInfo::Signature() const
{
	return fSignature;
}


// ROM 0x0005c5c4 Size__10TClassInfoCFv
size_t
TClassInfo::Size() const
{
	return PrivateClassInfoSize(this);
}


// ROM 0x0005c5f0 MakeAt__10TClassInfoCFPCv
void
TClassInfo::MakeAt(const void* at) const
{
	PrivateClassInfoMakeAt(this, at);
}


// ROM 0x0005c5f4 EntryProc__10TClassInfoCFv
const void*
TClassInfo::EntryProc() const
{
	return (const void*) fEntryProc;
}


// ROM 0x0005c5c8 AllocProc__10TClassInfoCFv
const void*
TClassInfo::AllocProc() const
{
	return (const void*) fAlloc;
}


// ROM 0x0005c5dc FreeProc__10TClassInfoCFv
const void*
TClassInfo::FreeProc() const
{
	return (const void*) fFree;
}


// ROM 0x0005c600 Version__10TClassInfoCFv
unsigned long
TClassInfo::Version() const
{
	return fVersion;
}


// (declared by the DDK, not compiled into the ROM)
unsigned long
TClassInfo::Flags() const
{
	return fFlags;
}


// ROM 0x0005c608 Register__10TClassInfoCFv
long
TClassInfo::Register() const
{
	return gProtocolRegistry->Register(this, 0);
}


// ROM 0x0005c620 DeRegister__10TClassInfoCFv
long
TClassInfo::DeRegister() const
{
	return gProtocolRegistry->DeRegister(this, true);
}


// ROM 0x0005c638 New__10TClassInfoCFv
// An instance: from the alloc proc, or the memory manager (the ROM's
// malloc); made at, then its New() run, and counted.
TProtocol*
TClassInfo::New() const
{
	void* memory;
	if (fAlloc != nil)
		memory = fAlloc();
	else
		memory = NewPtr(Size());
	if (memory == nil)
		return nil;
	TProtocol* instance = PrivateClassInfoMakeAt(this, memory);
	fDefaultNew(instance);
	if (gProtocolRegistry != nil)
		gProtocolRegistry->UpdateInstanceCount(this, 1);
	return instance;
}


// ROM 0x0005c6b0 Destroy__10TClassInfoCFP9TProtocol
// (the instance's Delete() is the caller's business)
void
TClassInfo::Destroy(TProtocol* instance) const
{
	if (fFree != nil)
		fFree(instance->fRuntime);
	else
		DisposPtr((Ptr) instance->fRuntime);
	if (gProtocolRegistry != nil)
		gProtocolRegistry->UpdateInstanceCount(this, -1);
}


// ROM 0x0005c710 Selector__10TClassInfoCFv
// (sic: the ROM answers the address of the free branch, +0x20, not the
// selector branch at +0x34)
CodeProcPtr
TClassInfo::Selector() const
{
	return (CodeProcPtr) fFree;
}


// ROM 0x0005c718 GetCapability__10TClassInfoCFPCc
// The signature is a list of name, value string pairs ended by an empty
// name; the value of the named capability (the first, for a nil name).
const char*
TClassInfo::GetCapability(const char* name) const
{
	for (const char* s = Signature(); s != nil && *s != 0; )
	{
		if (name == nil || strcmp(s, name) == 0)
			return s + strlen(s) + 1;
		const char* value = s + strlen(s) + 1;
		s = value + strlen(value) + 1;
	}
	return nil;
}


// ROM 0x0005c79c GetCapability__10TClassInfoCFl
// A four-character capability name.
const char*
TClassInfo::GetCapability(long name) const
{
	char key[5];
	key[4] = 0;
	ULong word = (ULong) name;
	key[0] = (char) (word >> 24);
	key[1] = (char) (word >> 16);
	key[2] = (char) (word >> 8);
	key[3] = (char) word;
	for (const char* s = Signature(); s != nil && *s != 0; )
	{
		if (strcmp(s, key) == 0)
			return s + strlen(s) + 1;
		const char* value = s + strlen(s) + 1;
		s = value + strlen(value) + 1;
	}
	return nil;
}


// ROM 0x0005c830 HasInstances__10TClassInfoCFPl
Boolean
TClassInfo::HasInstances(long* count) const
{
	*count = 0;
	if (gProtocolRegistry != nil)
	{
		*count = gProtocolRegistry->GetInstanceCount(this);
		if (*count != 0)
			return true;
	}
	return false;
}


// ROM 0x0005cba8 PrivateClassInfoSize__FPC10TClassInfo
size_t
PrivateClassInfoSize(const TClassInfo* info)
{
	return info->fSizeof();
}


// ROM 0x0005cbac PrivateClassInfoMakeAt__FPC10TClassInfoPCv
// The TProtocol fields of a new instance: no runtime, its own real this,
// the dispatch table, no monitor.  (Here the vtable pointer as well, and
// the runtime field remembers the memory.)
TProtocol*
PrivateClassInfoMakeAt(const TClassInfo* info, const void* proto)
{
	TProtocol* p = info->fMakeAt((void*) proto);
	p->fRuntime = (void*) proto;
	p->fRealThis = p;
	p->fBTable = info;
	p->fMonitorId = 0;
	return p;
}


// ROM 0x0005cbcc PrivateClassInfoInterfaceName__FPC10TClassInfo
const char*
PrivateClassInfoInterfaceName(const TClassInfo* info)
{
	return info->fInterface;
}


// ROM 0x0005cbd8 PrivateClassInfoImplementationName__FPC10TClassInfo
const char*
PrivateClassInfoImplementationName(const TClassInfo* info)
{
	return info->fName;
}


/* -------------------------------------------------------------------------------
	Instances by name
------------------------------------------------------------------------------- */

// ROM 0x0005c884 NewByName__FPCcT1
TProtocol*
NewByName(const char* abstract, const char* implementation)
{
	const TClassInfo* info = gProtocolRegistry->Satisfy(abstract, implementation, (ULong) 0);
	return info != nil ? info->New() : nil;
}


// ROM 0x0005c8c0 NewByName__FPCcT1Ul
TProtocol*
NewByName(const char* abstract, const char* implementation, ULong version)
{
	const TClassInfo* info = gProtocolRegistry->Satisfy(abstract, implementation, version);
	return info != nil ? info->New() : nil;
}


// ROM 0x0005c8fc NewByName__FPCcN21
TProtocol*
NewByName(const char* abstract, const char* implementation, const char* capability)
{
	const TClassInfo* info = gProtocolRegistry->Satisfy(abstract, implementation, capability);
	return info != nil ? info->New() : nil;
}


// ROM 0x0005ca30 ClassInfoByName__FPCcT1Ul
const TClassInfo*
ClassInfoByName(const char* abstract, const char* implementation, ULong version)
{
	return gProtocolRegistry->Satisfy(abstract, implementation, version);
}


// ROM 0x0005c938 AllocInstanceByName__FPCcT1
// An instance made but not New()ed - the interface's New(char*) glue does
// that.
TProtocol*
AllocInstanceByName(const char* abstract, const char* implementation)
{
	TProtocol* instance = nil;
	const TClassInfo* info = gProtocolRegistry->Satisfy(abstract, implementation, (ULong) 0);
	if (info != nil)
	{
		void* memory;
		if (info->AllocProc() != nil)
			memory = info->fAlloc();
		else
			memory = NewPtr(info->Size());
		if (memory != nil)
		{
			instance = PrivateClassInfoMakeAt(info, memory);
			if (gProtocolRegistry != nil)
				gProtocolRegistry->UpdateInstanceCount(info, 1);
		}
	}
	return instance;
}


// ROM 0x0005c9c4 FreeInstance__FP9TProtocol
void
FreeInstance(TProtocol* instance)
{
	if (instance == nil)
		return;
	const TClassInfo* info = instance->ClassInfo();
	if (info == nil)
		return;
	if (info->FreeProc() != nil)
		info->fFree(instance->fRuntime);
	else
		DisposPtr((Ptr) instance->fRuntime);
	if (gProtocolRegistry != nil)
		gProtocolRegistry->UpdateInstanceCount(info, -1);
}


// ROM 0x0005ca5c ClassInfoFromHunkByName__FPvPCcT2
// The hunk's classes are asked for one by one; a nil name matches any.
const TClassInfo*
ClassInfoFromHunkByName(void* hunk, const char* abstract, const char* implementation)
{
	if (hunk == nil)
		return nil;
	CodeProcPtr code = (CodeProcPtr) hunk;
	int count = (int) (intptr_t) code(kCodeClassCount);
	for (int i = 0; i < count; i++)
	{
		const TClassInfo* info = (const TClassInfo*) code(kCodeClassAt, i);
		if (info == nil)
			continue;
		if (abstract != nil && strcmp(info->InterfaceName(), abstract) != 0)
			continue;
		if (implementation != nil && strcmp(info->ImplementationName(), implementation) != 0)
			continue;
		return info;
	}
	return nil;
}


// ROM 0x0005cb0c NewFromHunkByName__FPvPCcT2
// (made and MakeAt'd, not New()ed and not counted)
TProtocol*
NewFromHunkByName(void* hunk, const char* abstract, const char* implementation)
{
	const TClassInfo* info = ClassInfoFromHunkByName(hunk, abstract, implementation);
	if (info == nil)
		return nil;
	void* memory = NewPtr(info->Size());
	return memory != nil ? PrivateClassInfoMakeAt(info, memory) : nil;
}


/* -------------------------------------------------------------------------------
	The registry
------------------------------------------------------------------------------- */

// ROM 0x0005cb4c StartupProtocolRegistry__Fv
// The registry is an instance of its own implementation, started as a
// monitor (1 KB stack, the caller's environment) and registered with itself.
void
StartupProtocolRegistry(void)
{
	gProtocolRegistry = (TClassInfoRegistry*) TClassInfoRegistryImpl::ClassInfo()->New();
	gProtocolRegistry->StartMonitor(0x400, 0, 'mntr', false);
	TClassInfoRegistryImpl::ClassInfo()->Register();
}


// ROM 0x0005cb98 GetProtocolRegistry__Fv
TClassInfoRegistry*
GetProtocolRegistry(void)
{
	return gProtocolRegistry;
}


/* -------------------------------------------------------------------------------
	TClassInfoRegistry - the interface's glue: each method is a monitor call
	with its selector (ROM 0x0037c844-0x0037c8e0, one three-instruction stub
	each).
------------------------------------------------------------------------------- */

// ROM 0x0037c7ec (unnamed) - the interface's name
static const char*
TClassInfoRegistryName(void)
{
	return "TClassInfoRegistry";
}


// ROM 0x00385f68 New__18TClassInfoRegistrySFPCc
// (an instance, not yet New()ed: for a monitor that is a call to make once
// it runs)
TClassInfoRegistry*
TClassInfoRegistry::New(const char* implementation)
{
	return (TClassInfoRegistry*) AllocInstanceByName(TClassInfoRegistryName(), implementation);
}


// ROM 0x00385f8c Delete__18TClassInfoRegistryFv
void
TClassInfoRegistry::Delete()
{
	ProtocolMonitorArgs args = { 0, { 0, 0, 0, 0 } };
	MonitorCall(kClassInfoRegistry_Delete, &args);
	DestroyMonitor();
}


// ROM 0x00385fa4 Register__18TClassInfoRegistryFPC10TClassInfoUl
NewtonErr
TClassInfoRegistry::Register(const TClassInfo* info, ULong refCon)
{
	ProtocolMonitorArgs args = { 0, { (ULong) info, refCon, 0, 0 } };
	return (NewtonErr) MonitorCall(kClassInfoRegistry_Register, &args);
}


// ROM 0x00385fb0 DeRegister__18TClassInfoRegistryFPC10TClassInfoUc
NewtonErr
TClassInfoRegistry::DeRegister(const TClassInfo* info, Boolean specific)
{
	ProtocolMonitorArgs args = { 0, { (ULong) info, specific, 0, 0 } };
	return (NewtonErr) MonitorCall(kClassInfoRegistry_DeRegister, &args);
}


// ROM 0x00385fbc IsRegistered__18TClassInfoRegistryCFPC10TClassInfoUc
Boolean
TClassInfoRegistry::IsRegistered(const TClassInfo* info, Boolean specific) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) info, specific, 0, 0 } };
	return (Boolean) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_IsRegistered, &args);
}


// ROM 0x00385fc8 Satisfy__18TClassInfoRegistryCFPCcT1Ul
const TClassInfo*
TClassInfoRegistry::Satisfy(const char* intf, const char* impl, ULong version) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) intf, (ULong) impl, version, 0 } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_Satisfy, &args);
}


// ROM 0x00385fd4 Seed__18TClassInfoRegistryCFv
long
TClassInfoRegistry::Seed() const
{
	ProtocolMonitorArgs args = { 0, { 0, 0, 0, 0 } };
	return ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_Seed, &args);
}


// ROM 0x00385fe0 First__18TClassInfoRegistryCFlPUl
const TClassInfo*
TClassInfoRegistry::First(long seed, ULong* pRefCon) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) seed, (ULong) pRefCon, 0, 0 } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_First, &args);
}


// ROM 0x00385fec Next__18TClassInfoRegistryCFlPC10TClassInfoPUl
const TClassInfo*
TClassInfoRegistry::Next(long seed, const TClassInfo* from, ULong* pRefCon) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) seed, (ULong) from, (ULong) pRefCon, 0 } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_Next, &args);
}


// ROM 0x00385ff8 Find__18TClassInfoRegistryCFPCcT1iPUl
const TClassInfo*
TClassInfoRegistry::Find(const char* intf, const char* impl, int skipCount, ULong* pRefCon) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) intf, (ULong) impl, (ULong) skipCount, (ULong) pRefCon } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_Find, &args);
}


// ROM 0x00386004 Satisfy__18TClassInfoRegistryCFPCcN21
const TClassInfo*
TClassInfoRegistry::Satisfy(const char* intf, const char* impl, const char* capability) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) intf, (ULong) impl, (ULong) capability, 0 } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_SatisfyCapability, &args);
}


// ROM 0x00386010 Satisfy__18TClassInfoRegistryCFPCcN31
const TClassInfo*
TClassInfoRegistry::Satisfy(const char* intf, const char* impl, const char* capability, const char* capabilityValue) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) intf, (ULong) impl, (ULong) capability, (ULong) capabilityValue } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_SatisfyCapabilityValue, &args);
}


// ROM 0x0038601c Satisfy__18TClassInfoRegistryCFPCcT1ClT3
const TClassInfo*
TClassInfoRegistry::Satisfy(const char* intf, const char* impl, const long capability, const long capabilityValue) const
{
	ProtocolMonitorArgs args = { 0, { (ULong) intf, (ULong) impl, (ULong) capability, (ULong) capabilityValue } };
	return (const TClassInfo*) ((TProtocol*) this)->MonitorCall(kClassInfoRegistry_SatisfyLongCapability, &args);
}


// ROM 0x00386028 UpdateInstanceCount__18TClassInfoRegistryFPC10TClassInfol
void
TClassInfoRegistry::UpdateInstanceCount(const TClassInfo* info, long adjustment)
{
	ProtocolMonitorArgs args = { 0, { (ULong) info, (ULong) adjustment, 0, 0 } };
	MonitorCall(kClassInfoRegistry_UpdateInstanceCount, &args);
}


// ROM 0x00386034 GetInstanceCount__18TClassInfoRegistryFPC10TClassInfo
long
TClassInfoRegistry::GetInstanceCount(const TClassInfo* info)
{
	ProtocolMonitorArgs args = { 0, { (ULong) info, 0, 0, 0 } };
	return MonitorCall(kClassInfoRegistry_GetInstanceCount, &args);
}
