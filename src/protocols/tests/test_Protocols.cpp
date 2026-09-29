// Protocol test: the OS is booted (OsBoot) so that the protocol registry
// exists as InitialKSRVTask starts it - an instance of
// TClassInfoRegistryImpl running as a monitor, registered with itself.  In
// the 'ksrv' task a test protocol (TShape, with three implementations) is
// registered and exercised through the registry: lookup by names, version
// and capability, NewByName and calls through the interface, instance
// counting, enumeration with the seed, deregistration; then the registry is
// asked about itself.

#include "Protocols.h"
#include "ClassInfoRegistry.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


/* -------------------------------------------------------------------------------
	A protocol, as a DDK header declares one, and its implementations, as
	ProtocolGen would have them.
------------------------------------------------------------------------------- */

PROTOCOL TShape : public TProtocol
{
public:
	static TShape*	New(const char* implementation);
	void			Delete();

	VIRTUAL long	Area(long size) ENDVIRTUAL;
	VIRTUAL const char*	Name() ENDVIRTUAL;
};

// the glue every interface has (ROM: e.g. TSerialChip::New/Delete)
TShape*
TShape::New(const char* implementation)
{
	TShape* shape = (TShape*) AllocInstanceByName("TShape", implementation);
	return shape != nil ? (TShape*) shape->GlueNew() : nil;
}

void
TShape::Delete()
{
	GlueDelete();
}


static int gSquaresAlive = 0;

PROTOCOL TSquare : public TShape
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSquare);

	TSquare*		New();
	void			Delete();

	long			Area(long size);
	const char*		Name();

	long			fMadeWith;
};

PROTOCOL_IMPL_SOURCE_MACRO(TSquare)
PROTOCOL_CLASSINFO(TSquare, "TShape", "", 1, 0, nil)

TSquare*	TSquare::New()				{ fMadeWith = 4; gSquaresAlive++; return this; }
void		TSquare::Delete()			{ gSquaresAlive--; }
long		TSquare::Area(long size)	{ return size * size; }
const char*	TSquare::Name()				{ return "square"; }


// a later version of the same implementation name
PROTOCOL TSquare2 : public TShape
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TSquare2);

	TSquare2*		New();
	void			Delete();

	long			Area(long size);
	const char*		Name();
};

PROTOCOL_IMPL_SOURCE_MACRO(TSquare2)
// its class info calls itself TSquare, version 2
static TProtocol* TSquare2_MakeAt(void* at) { return new (at) TSquare2; }
static TProtocol* TSquare2_New(TProtocol* p) { return ((TSquare2*) p)->New(); }
static void TSquare2_Delete(TProtocol* p) { ((TSquare2*) p)->Delete(); }
const TClassInfo* TSquare2::ClassInfo()
{
	static const TClassInfo info = { 0, "TSquare", "TShape", "", TSquare2_MakeAt, nil, TSquare2::Sizeof, nil, nil, TSquare2_New, TSquare2_Delete, 2, 0, nil, 0 };
	return &info;
}

TSquare2*	TSquare2::New()				{ return this; }
void		TSquare2::Delete()			{ }
long		TSquare2::Area(long size)	{ return size * size * 2; }
const char*	TSquare2::Name()			{ return "square v2"; }


PROTOCOL TCircle : public TShape
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TCircle);

	TCircle*		New();
	void			Delete();

	long			Area(long size);
	const char*		Name();
};

PROTOCOL_IMPL_SOURCE_MACRO(TCircle)
PROTOCOL_CLASSINFO(TCircle, "TShape", "kind\0round\0edges\0none\0", 1, 0, nil)

TCircle*	TCircle::New()				{ return this; }
void		TCircle::Delete()			{ }
long		TCircle::Area(long size)	{ return 3 * size * size; }
const char*	TCircle::Name()				{ return "circle"; }


/* -------------------------------------------------------------------------------
	The scenario, in the kernel services task
------------------------------------------------------------------------------- */

static void
RegistryScenario()
{
	TClassInfoRegistry* registry = GetProtocolRegistry();
	EXPECT(registry != nil && registry == gProtocolRegistry);
	EXPECT(registry->GetMonitorId() != 0);

	// the registry registered itself
	const TClassInfo* own = TClassInfoRegistryImpl::ClassInfo();
	EXPECT(registry->IsRegistered(own, true));
	EXPECT(strcmp(own->ImplementationName(), "TClassInfoRegistryImpl") == 0 && strcmp(own->InterfaceName(), "TClassInfoRegistry") == 0);
	EXPECT(own->Size() == sizeof(TClassInfoRegistryImpl));
	EXPECT(ClassInfoByName("TClassInfoRegistry", "TClassInfoRegistryImpl") == own);
	EXPECT(registry->ClassInfo() == own);
	long count = -1;
	EXPECT(!own->HasInstances(&count) && count == 0);	// made before the registry existed to count it (as in the ROM)

	// nothing of ours yet
	EXPECT(ClassInfoByName("TShape", "TSquare") == nil);
	EXPECT(NewByName("TShape", "TSquare") == nil);
	long seed = registry->Seed();

	// register the shapes
	const TClassInfo* square = TSquare::ClassInfo();
	const TClassInfo* square2 = TSquare2::ClassInfo();
	const TClassInfo* circle = TCircle::ClassInfo();
	EXPECT(square->Register() == noErr);
	EXPECT(registry->Register(circle, 'circ') == noErr);
	EXPECT(registry->Seed() != seed);
	EXPECT(registry->IsRegistered(square, true) && registry->IsRegistered(circle, true) && !registry->IsRegistered(square2, true));
	EXPECT(!registry->IsRegistered(square2, false));	// nothing satisfies its names at its version (2) yet

	// lookup by names, and version
	EXPECT(ClassInfoByName("TShape", "TSquare") == square);
	EXPECT(ClassInfoByName("TShape", "TCircle") == circle);
	EXPECT(ClassInfoByName("TShape", "TCircle", 1) == circle);
	EXPECT(ClassInfoByName("TShape", "TCircle", 2) == nil);
	EXPECT(ClassInfoByName("TShape", nil) != nil);			// any implementation
	EXPECT(ClassInfoByName("TNothing", "TSquare") == nil);
	EXPECT(ClassInfoByName("TShape", "TSquare") == square);	// from the cache now

	// version 2 of TSquare: the newest satisfies a plain request
	EXPECT(square2->Register() == noErr);
	EXPECT(ClassInfoByName("TShape", "TSquare") == square2);
	EXPECT(ClassInfoByName("TShape", "TSquare", 2) == square2);
	EXPECT(ClassInfoByName("TShape", "TSquare", 1) == square2);
	EXPECT(ClassInfoByName("TShape", "TSquare", 3) == nil);
	EXPECT(registry->IsRegistered(square2, true));
	EXPECT(square2->DeRegister() == noErr);
	EXPECT(!registry->IsRegistered(square2, true));
	EXPECT(ClassInfoByName("TShape", "TSquare") == square);

	// capabilities
	EXPECT(circle->GetCapability("kind") != nil && strcmp(circle->GetCapability("kind"), "round") == 0);
	EXPECT(strcmp(circle->GetCapability("edges"), "none") == 0);
	EXPECT(circle->GetCapability("colour") == nil);
	EXPECT(strcmp(circle->GetCapability((const char*) nil), "round") == 0);
	EXPECT(strcmp(circle->GetCapability((long) 'kind'), "round") == 0);
	EXPECT(square->GetCapability("kind") == nil);
	EXPECT(registry->Satisfy("TShape", nil, "kind") == circle);
	EXPECT(registry->Satisfy("TShape", nil, "kind", "round") == circle);
	EXPECT(registry->Satisfy("TShape", nil, "kind", "square") == nil);
	EXPECT(registry->Satisfy("TShape", nil, (long) 'kind') == circle);
	EXPECT(registry->Satisfy("TShape", "TSquare", "kind") == nil);
	EXPECT(registry->Satisfy("TShape", "TSquare", (long) 0) == square);

	// instances through the interface
	TShape* a = TShape::New("TSquare");
	TShape* b = (TShape*) NewByName("TShape", "TCircle");
	TShape* c = (TShape*) NewByName("TShape", "TSquare", (ULong) 1);
	EXPECT(a != nil && b != nil && c != nil);
	EXPECT(a->ClassInfo() == square && b->ClassInfo() == circle && c->ClassInfo() == square);
	EXPECT(a->Area(3) == 9 && b->Area(3) == 27 && c->Area(2) == 4);
	EXPECT(strcmp(a->Name(), "square") == 0 && strcmp(b->Name(), "circle") == 0);
	EXPECT(((TSquare*) a)->fMadeWith == 4 && gSquaresAlive == 2);
	EXPECT(a->fRealThis == a && a->GetMonitorId() == 0);
	EXPECT(square->HasInstances(&count) && count == 2);
	EXPECT(circle->HasInstances(&count) && count == 1);
	EXPECT(registry->GetInstanceCount(square) == 2);
	a->Delete();
	EXPECT(gSquaresAlive == 1 && registry->GetInstanceCount(square) == 1);
	square->Destroy(c);						// without its Delete()
	EXPECT(gSquaresAlive == 1 && registry->GetInstanceCount(square) == 0);
	EXPECT(!square->HasInstances(&count) && count == 0);
	b->Delete();
	EXPECT(registry->GetInstanceCount(circle) == 0);
	EXPECT((SAFELY_CAST(registry, TClassInfoRegistryImpl)) != nil);
	TShape* d = TShape::New("TCircle");
	EXPECT((SAFELY_CAST(d, TSquare)) == nil && (SAFELY_CAST(d, TCircle)) != nil);
	d->Delete();

	// enumeration: First/Next in registry order, valid while the seed holds
	seed = registry->Seed();
	int seen = 0, seenShapes = 0;
	ULong refCon = 0;
	for (const TClassInfo* info = registry->First(seed, &refCon); info != nil; info = registry->Next(seed, info, &refCon))
	{
		// (the boot's loader registers the event collector, InitEvents)
		if (strcmp(info->InterfaceName(), "TEventCollector") != 0)
			seen++;
		if (strcmp(info->InterfaceName(), "TShape") == 0)
			seenShapes++;
		if (info == circle)
			EXPECT(refCon == 'circ');
	}
	EXPECT(seen == 3 && seenShapes == 2);
	EXPECT(registry->Find("TShape", nil, 0) != nil && registry->Find("TShape", nil, 1) != nil && registry->Find("TShape", nil, 2) == nil);
	EXPECT(registry->Find(nil, "TCircle", 0, &refCon) == circle && refCon == 'circ');
	const TClassInfo* first = registry->First(seed);
	EXPECT(square2->Register() == noErr);		// the registry changed: the seed is stale
	EXPECT(registry->Next(seed, first) == nil);
	EXPECT(registry->Next(0, first) != nil);		// a seed of 0 does not care
	EXPECT(square2->DeRegister() == noErr);

	// a second registration of a registered class info raises its count -
	// from the ROM's 0, so one deregistration undoes both
	EXPECT(square->Register() == noErr);
	EXPECT(registry->IsRegistered(square, true));
	EXPECT(square->DeRegister() == noErr);
	EXPECT(!registry->IsRegistered(square, true));
	EXPECT(square->DeRegister() != noErr);
	EXPECT(registry->DeRegister(circle, false) == noErr);
	EXPECT(ClassInfoByName("TShape", nil) == nil);
	EXPECT(registry->Register(nil) != noErr);

	HostStopTasks();
}


int main()
{
	gHostKernelServicesTask = RegistryScenario;
	OsBoot();
	if (failures == 0)
		printf("test_Protocols: all passed\n");
	return failures != 0;
}
