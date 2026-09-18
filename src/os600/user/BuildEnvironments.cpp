/*
	File:		user/BuildEnvironments.cpp

	Contains:	InitDomainsAndEnvironments, the user-side boot step that turns
				the memory object database into kernel objects: a domain for every
				entry of the domain table (with its heap and globals for the
				domains that have them) and an environment for every entry of the
				environment table, each registered back in the database by name.

				The heap side of BuildDomainsAndHeaps - heap domains, heap areas,
				VM and segregated heaps, the persistent heaps' page lists - is the
				paged memory system, not reconstructed yet; those domains are made
				as plain domains for now and the steps are marked NOT YET.

	Reconstructed from:	InitDomainsAndEnvironments 0x000ea784, BuildDomainsAndHeaps
				0x000ea7c8, BuildEnvironments 0x000eb200
*/

#include "UserBoot.h"
#include "UserDomain.h"
#include "UserTasks.h"
#include "UserEnvironment.h"
#include "UserGlobals.h"
#include "MemObjManager.h"
#include "Boot.h"
#include "Reboot.h"
#include "VirtualMemory.h"
#include "OSErrors.h"


// ROM 0x000e91f0 BuildDomainsAndHeaps__FUl
// For every domain but the kernel's (already made): a plain domain if it
// has neither globals nor a heap, else a heap domain with its globals area
// (copied from ROM and zeroed) and its heap (VM, segregated, persistent or
// "hunk o' memory"), the environment `envId` (the kernel's) holding the
// domain while it is set up.  Then the persistent records whose slot is free
// but not 'emty' are restored.  A malformed table cold-boots the machine
// with kError_Sorry_System_Failure.
NewtonErr
BuildDomainsAndHeaps(TObjectId envId)
{
	TUEnvironment env(envId);
	long err = noErr;
	DomainInfo info;
	for (ULong i = 0; MemObjManager::GetDomainInfo(i, &info, &err); i++)
	{
		if (info.Name() == 'krnl')
			continue;
		if ((info.Base() & 0xfffff) != 0 || (info.Size() & 0xfffff) != 0)
		{
			err = kError_Bad_Parameters;
			break;
		}
		TObjectId domainId;
		if (!info.HasGlobals() && !info.HasHeap() && !info.MakeHeapDomain())
		{
			TUDomain domain;
			if ((err = domain.Init(0, info.Base(), info.Size())) != noErr)
				break;
			domain.DenyOwnership();
			domainId = domain;
		}
		else
		{
			// NOT YET RECONSTRUCTED: NewHeapDomain(base >> 20, size >> 20) and,
			// with the domain in the kernel's environment (read-only ones as
			// manager): the globals area (NewHeapArea at GlobalBase, locked,
			// the initialised data copied from GlobalROMBase and the rest
			// zeroed), then the heap - NewVMHeap / NewSegregatedVMHeap
			// registered with RegisterHeapRef, or for a persistent domain its
			// pages restored from the persistent record (TPageManager::Make,
			// AddPageMappingToDomain, ResurrectVMHeap) or a new hunk-o-memory
			// area (NewHeapArea at base + 0x8000, SetHeapLimits) - then the
			// domain removed from the environment.  A plain domain stands in.
			TUDomain domain;
			if ((err = domain.Init(0, info.Base(), info.Size())) != noErr)
				break;
			domain.DenyOwnership();
			domainId = domain;
			(void) env;
		}
		MemObjManager::RegisterDomainId(info.Name(), domainId);
	}
	// NOT YET RECONSTRUCTED: the persistent records (GetPersistentRef) not in
	// use and not 'emty' - heaps that survived a reboot - are given a domain
	// and their pages back, and every record's unmapped pages go to
	// gPageTracker
	if (err != noErr)
		Reboot(kError_Sorry_System_Failure, kRebootMagicNumber, false);
	return err;
}


// ROM 0x000e9c28 BuildEnvironments__Fv
// An environment per table entry: the kernel's is the one already made;
// the others get the default heap the table names (the kernel heap for
// 'krnl', else the heap registered under that name) and each of their
// domains, as client or manager, flagged as the heap and/or stack domain
// where the table says so.  (TEnvironment::Add takes manager, stack, heap.)
NewtonErr
BuildEnvironments()
{
	long err = noErr;
	EnvironmentInfo info;
	for (ULong i = 0; MemObjManager::GetEnvironmentInfo(i, &info, &err); i++)
	{
		ULong name = info.Name();
		void* heap = nil;
		TUEnvironment env;
		if (name == 'krnl')
		{
			TObjectId id;
			if ((err = MemObjManager::FindEnvironmentId('krnl', &id)) != noErr)
				return err;
			env.CopyObject(id);
		}
		else
		{
			if (info.DefaultHeap() == 'krnl')
				heap = gKernelHeap;
			else if (info.DefaultHeap() != 0 && (err = MemObjManager::FindHeapRef(info.DefaultHeap(), &heap)) != noErr)
				return err;
			if ((err = env.Init(heap)) != noErr)
				return err;
			env.DenyOwnership();
		}
		ULong domainName;
		Boolean isManager;
		long domainErr = noErr;
		for (ULong j = 0; info.Domains(j, &domainName, &isManager, &domainErr); j++)
		{
			TObjectId domainId;
			if ((err = MemObjManager::FindDomainId(domainName, &domainId)) != noErr)
				return err;
			if ((err = env.Add(domainId, isManager, info.DefaultStackDomain() == domainName, info.DefaultHeapDomain() == domainName)) != noErr)
				return err;
		}
		if (domainErr != noErr)
			return err;
		MemObjManager::RegisterEnvironmentId(name, env);
	}
	return err;
}


// ROM 0x000e91ac InitDomainsAndEnvironments__Fv
void
InitDomainsAndEnvironments()
{
	TObjectId kernelEnvId;
	if (MemObjManager::FindEnvironmentId('krnl', &kernelEnvId) == noErr && BuildDomainsAndHeaps(kernelEnvId) == noErr)
		BuildEnvironments();
}
