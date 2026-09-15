// Host unit test for domains, environments and the memory-architecture
// manager (src/os600/kernel/Domain.*, Environment.*, MemArchManager.*).
// Domains are set up by hand (TKDomain::Init needs the MMU).

#include "Domain.h"
#include "Environment.h"
#include "MemArchManager.h"
#include "ObjectTable.h"
#include "Task.h"
#include "KernelGlobals.h"
#include "OSErrors.h"

#include <stdio.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static TObjectTable table;
static TMemArchManager manager;

static TKDomain* MakeDomain(VAddr base, ULong size, TObjectId owner)
{
	TKDomain* d = new TKDomain;
	d->fBase = base;
	d->fSize = size;
	table.Add(d, kDomainType, owner);
	return d;
}

int main()
{
	table.Init();
	gObjectTable = &table;
	gTheMemArchManager = &manager;

	// --- domain access words --------------------------------------------------
	ULong dcr = DefaultDCR();
	EXPECT(dcr == 5);												// domains 0 and 1, client
	EXPECT(AddClientToDCR(0, 3) == 0x40 && AddManagerToDCR(0, 3) == 0xC0);
	EXPECT(RemoveFromDCR(0xC5, 3) == 5 && RemoveFromDCR(0xC5, kNoDomainNumber) == 0xC5);
	EXPECT(AddClientToDCR(1, kNoDomainNumber) == 1);
	EXPECT(NextAvailDomainInDCR(dcr) == 2 && dcr == 0x35);
	EXPECT(GetSpecificDomainFromDCR(dcr, 2) == kNoDomainNumber);
	EXPECT(GetSpecificDomainFromDCR(dcr, 5) == 5 && (dcr & 0xC00) == 0xC00);
	ULong full = 0x3FFFFFFF;										// 0-14 taken, 15 free
	EXPECT(NextAvailDomainInDCR(full) == kNoDomainNumber);			// 15 is never handed out

	// --- the manager hands out numbers and keeps ranges apart -------------------
	EXPECT(manager.fDomainsInUse == 5 && manager.fDomains == nil && manager.fEnvironments == nil);
	TTask& owner = *new TTask;
	table.Add(&owner, kTaskType, 1);
	TKDomain* d1 = MakeDomain(0x00100000, 0x00100000, owner.fId);
	TKDomain* d2 = MakeDomain(0x00400000, 0x00200000, owner.fId);
	EXPECT(d1->fNumber == kNoDomainNumber && d1->fFaultMonitorId == 0);
	EXPECT(manager.AddDomain(d1) == noErr && d1->fNumber == 2);
	EXPECT(manager.AddDomainWithDomainNumber(d2, 7) == noErr && d2->fNumber == 7 && manager.fDomains == d2 && d2->fNext == d1);
	TKDomain d3;
	EXPECT(manager.AddDomainWithDomainNumber(&d3, 7) == kError_Bad_Parameters && d3.fNumber == kNoDomainNumber);
	EXPECT(!manager.DomainRangeIsFree(0x00000000, 0x001FFFFF));	// overlaps d1
	EXPECT(manager.DomainRangeIsFree(0x00200000, 0x003FFFFF));		// between them
	EXPECT(!manager.DomainRangeIsFree(0x00500000, 0x00500FFF));	// inside d2
	EXPECT(manager.DomainRangeIsFree(0x00600000, 0x006FFFFF));		// just after d2
	ULong before = manager.fDomainsInUse;
	manager.RemoveDomain(&d3);										// not in the list: nothing happens
	EXPECT(manager.fDomains == d2 && manager.fDomainsInUse == before);
	manager.RemoveDomain(d1);
	EXPECT(d1->fNumber == kNoDomainNumber && d1->fNext == nil && manager.fDomains == d2 && d2->fNext == nil);
	EXPECT(manager.fDomainsInUse == before);						// the number is not recycled
	EXPECT(manager.AddDomain(d1) == noErr && d1->fNumber == 3 && manager.fDomains == d1);

	// --- fault monitors ------------------------------------------------------------
	TObjectId monitorId = 0x1230 | kMonitorType;
	EXPECT(d2->SetFaultMonitor(monitorId) == noErr);
	EXPECT(gFaultMonitorTable[7].fMonitorId == monitorId && gFaultMonitorTable[7].fDomainId == d2->fId);
	RegisterFaultMonitor(7, d1->fId, 0);							// 0 keeps the monitor
	EXPECT(gFaultMonitorTable[7].fMonitorId == monitorId && gFaultMonitorTable[7].fDomainId == d1->fId);
	DeregisterFaultMonitorByDomainNumber(7);
	EXPECT(gFaultMonitorTable[7].fMonitorId == 0 && gFaultMonitorTable[7].fDomainId == 0);

	// --- environments ----------------------------------------------------------------
	TEnvironment* env = new TEnvironment;
	TObjectId envId = table.Add(env, kEnvironmentType, owner.fId);
	EXPECT(env->Init((void*) 0x11) == noErr);
	EXPECT(env->fDomainAccess == 5 && env->fRefCount == 0 && !env->fRemoved && manager.fEnvironments == env);
	Boolean has, isManager;
	EXPECT(EnvironmentHasDomain(envId, d1->fId, &has, &isManager) == noErr && !has && !isManager);
	EXPECT(AddDomainToEnvironment(envId, d1->fId, kEnvDomain_IsHeap) == noErr);
	EXPECT(env->fDomainAccess == (5 | (1 << 6)) && env->fHeapDomainId == d1->fId && env->fStackDomainId == 0);
	EXPECT(EnvironmentHasDomain(envId, d1->fId, &has, &isManager) == noErr && has && !isManager);
	EXPECT(AddDomainToEnvironment(envId, d2->fId, kEnvDomain_IsManager | kEnvDomain_IsStack) == noErr);
	EXPECT(env->fDomainAccess == (5 | (1 << 6) | (3 << 14)) && env->fStackDomainId == d2->fId);
	EXPECT(EnvironmentHasDomain(envId, d2->fId, &has, &isManager) == noErr && has && isManager);
	EXPECT(RemoveDomainFromEnvironment(envId, d1->fId) == noErr && env->fDomainAccess == (5 | (3 << 14)));
	EXPECT(env->fHeapDomainId == d1->fId);							// the heap domain id is not cleared
	EXPECT(AddDomainToEnvironment(envId, 0x9990 | kDomainType, 0) == kError_Bad_Parameters);
	EXPECT(AddDomainToEnvironment(d1->fId, d1->fId, 0) == kError_Bad_Parameters);
	EXPECT(RemoveDomainFromEnvironment(0x9990 | kEnvironmentType, d1->fId) == kError_Bad_Parameters);
	EXPECT(EnvironmentHasDomain(0x9990 | kEnvironmentType, d1->fId, &has, &isManager) == kError_Bad_ObjectId);
	EXPECT(EnvironmentHasDomain(envId, 0x9990 | kDomainType, &has, &isManager) == kError_Bad_ObjectId);

	// --- a task switches environments -------------------------------------------------
	TEnvironment* env2 = new TEnvironment;
	TObjectId env2Id = table.Add(env2, kEnvironmentType, owner.fId);
	env2->Init(nil);
	EXPECT(manager.fEnvironments == env2 && env2->fNext == env);
	owner.fEnvironment = env;
	env->IncrRefCount();
	gCurrentTask = &owner;
	TObjectId id = 0;
	EXPECT(GetEnvironment(&id) == noErr && id == envId);
	EXPECT(SetEnvironment(0x9990 | kEnvironmentType, &id) == kError_Bad_ObjectId);
	EXPECT(SetEnvironment(env2Id, &id) == noErr && id == envId);
	EXPECT(owner.fEnvironment == env2 && env2->fRefCount == 1 && env->fRefCount == 0);

	// --- removal and reference counting -----------------------------------------------
	env2->IncrRefCount();
	EXPECT(!env2->DecrRefCount() && env2->fRefCount == 1);			// still in the manager
	manager.RemoveEnvironment(env2);
	EXPECT(env2->fRemoved && manager.fEnvironments == env && env2->fNext == nil);
	manager.RemoveEnvironment(env2);								// already gone: harmless
	EXPECT(manager.fEnvironments == env);
	EXPECT(env2->DecrRefCount() && env2->fRefCount == 0 && env2->fOwnerId == 0);
	delete env;														// ~TEnvironment removes it
	EXPECT(manager.fEnvironments == nil);

	if (failures == 0)
		printf("test_Environment: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
