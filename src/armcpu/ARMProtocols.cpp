/*
	File:		armcpu/ARMProtocols.cpp

	Contains:	Packages' protocol parts on the ARM interpreter
				(ARMProtocols.h): the loader, the host class infos and
				proxies' plumbing, the mirrors, and the glue the protocol
				machinery and the name server need from the ARM side.

	Written by:	the reconstruction (DEVIATION: the ROM runs a part's code
				where it lies; the host runs it on src/armcpu).  The glue
				cites the ROM functions it answers for.
*/

#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "ProtocolStandIns.h"
#include "NameServer.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "ByteOrder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool	gTraceProtocols = false;

// A growable array (the standard containers reach <locale.h>)
template <class T>
class PVec
{
public:
				PVec() : fItems(nil), fCount(0), fCapacity(0) { }
	size_t		size() const					{ return fCount; }
	T&			operator[](size_t i)			{ return fItems[i]; }
	void		push_back(const T& v)
	{
		if (fCount == fCapacity)
		{
			fCapacity = fCapacity == 0 ? 16 : fCapacity * 2;
			fItems = (T*) realloc((void*) fItems, fCapacity * sizeof(T));
		}
		fItems[fCount++] = v;
	}
	void		erase(size_t i)
	{
		for (size_t j = i + 1; j < fCount; j++)
			fItems[j - 1] = fItems[j];
		fCount--;
	}
private:
	T*			fItems;
	size_t		fCount;
	size_t		fCapacity;
};


/*------------------------------------------------------------------------------
	P r o x y   k i n d s
------------------------------------------------------------------------------*/

struct ProxyKind { const char* fInterface; ARMProxyMakeAt fMakeAt; ARMProxySize fSize; };
static PVec<ProxyKind>	gKinds;

void
RegisterARMProxyKind(const char* interface, ARMProxyMakeAt makeAt, ARMProxySize size)
{
	ProxyKind k = { interface, makeAt, size };
	gKinds.push_back(k);
}

static const ProxyKind*
KindFor(const char* interface)
{
	for (size_t i = 0; i < gKinds.size(); i++)
		if (strcmp(gKinds[i].fInterface, interface) == 0)
			return &gKinds[i];
	return nil;
}


/*------------------------------------------------------------------------------
	T h e   c l a s s e s
	A part's code copied into the ARM world, its table read, and a host
	class info made for it.
------------------------------------------------------------------------------*/

struct ARMClass
{
	TClassInfo		fInfo;				// the host's, registered (first: a class info is its record)
	uint32_t		fBase;				// the code's region
	uint32_t		fSize;
	uint32_t		fBTable;			// the dispatch table
	uint32_t		fSizeofFn;			// the branches' targets (0: none)
	uint32_t		fAllocFn;
	uint32_t		fFreeFn;
	uint32_t		fInstanceSize;		// what its sizeof answered
	const ProxyKind*	fKind;			// nil: the ARM code may make instances, the host may not
	uint8_t*		fBytes;
	char			fImplementation[64];
	char			fInterface[64];
	char			fSignature[256];	// the capability list as it lies (name, value, ... "")
};
static PVec<ARMClass*>	gClasses;

static ARMClass*
ClassOfInfo(const TClassInfo* info)
{
	for (size_t i = 0; i < gClasses.size(); i++)
		if (&gClasses[i]->fInfo == info)
			return gClasses[i];
	return nil;
}

static ARMClass*
ClassOfBTable(uint32_t btable)
{
	for (size_t i = 0; i < gClasses.size(); i++)
		if (gClasses[i]->fBTable == btable)
			return gClasses[i];
	return nil;
}

static inline uint32_t
Word(const uint8_t* p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

// the target of an ARM branch at at (an offset in the part), 0 for none
static uint32_t
BranchTarget(const uint8_t* bytes, uint32_t at)
{
	uint32_t w = Word(bytes + at);
	if ((w >> 24) != 0xEA)
		return 0;
	int32_t off = (int32_t) (w << 8) >> 8;
	return at + 8 + (uint32_t) (off * 4);
}

// a self-relative offset's target (the field at at)
static uint32_t
Delta(const uint8_t* bytes, uint32_t at)
{
	uint32_t w = Word(bytes + at);
	return w == 0 ? 0 : at + w;
}

// the package's C relocation for the part's words (TSimpleCRelocator::
// Relocate, ROM 0x0004a148: each word the relocation chunk names moved by
// where the package is less where it was linked): the part is taken to lie
// where its copy does
static void
Relocate(uint8_t* bytes, uint32_t size, uint32_t base, const uint8_t* package, uint32_t partOffset)
{
	if (package == nil || (Word(package + 0x0c) & 0x04000000) == 0)
		return;
	const uint8_t* chunk = package + Word(package + 0x2c);
	uint32_t chunkSize = Word(chunk + 4);
	uint32_t pageSize = Word(chunk + 8);
	uint32_t linkBase = Word(chunk + 16);
	if (Word(chunk) != 0 || chunkSize < 20 || pageSize == 0)
		return;
	uint32_t delta = (base - partOffset) - linkBase;
	const uint8_t* p = chunk + 20;
	const uint8_t* end = chunk + chunkSize;
	uint32_t moved = 0;
	while (p + 4 <= end)
	{
		uint32_t page = (uint32_t) ((p[0] << 8) | p[1]);
		uint32_t count = (uint32_t) ((p[2] << 8) | p[3]);
		for (uint32_t i = 0; i < count && p + 4 + i < end; i++)
		{
			uint32_t at = page * pageSize + p[4 + i] * 4;
			if (at >= partOffset && at + 4 <= partOffset + size)
			{
				uint8_t* w = bytes + (at - partOffset);
				uint32_t v = Word(w) + delta;
				w[0] = (uint8_t) (v >> 24); w[1] = (uint8_t) (v >> 16); w[2] = (uint8_t) (v >> 8); w[3] = (uint8_t) v;
				moved++;
			}
		}
		p += 4 + ((count + 3) & ~3u);
	}
	if (gTraceProtocols)
		fprintf(stderr, "[armprotocols] %u words relocated\n", moved);
}

static size_t
NoSize(void)
{
	return 0;
}

static TProtocol*	ProxyNew(TProtocol* p);
static void			ProxyDelete(TProtocol* p);
// an instance of a part whose interface has no host proxy, asked for by
// host code: there is nothing the host could call it through
static TProtocol*
NoMakeAt(void*)
{
	ThrowMsg("armprotocols: host code asked for an instance of a part with no host proxy for its interface (NOT YET)");
	return nil;
}


// ARMProtocolPartLoader (packages/ProtocolStandIns.h)
static const TClassInfo*
LoadPart(const void* part, ULong size, const void* package, ULong partOffset)
{
	return LoadARMProtocolPart(part, size, package, partOffset);
}


const TClassInfo*
LoadARMProtocolPart(const void* part, unsigned long size, const void* package, unsigned long partOffset)
{
	gTraceProtocols = getenv("NEWTON_TRACE_ARMPROTOCOLS") != nil;
	if (size < 0x3c)
		return nil;
	ARMClass* c = new ARMClass;
	memset(c, 0, sizeof(ARMClass));
	c->fBytes = (uint8_t*) malloc(size);
	memcpy(c->fBytes, part, size);
	c->fSize = (uint32_t) size;
	c->fBase = ARMMapRegion(c->fBytes, c->fSize, kARMRegionMemory);
	Relocate(c->fBytes, c->fSize, c->fBase, (const uint8_t*) package, (uint32_t) partOffset);
	const uint8_t* b = c->fBytes;
	uint32_t name = Delta(b, 0x04), interface = Delta(b, 0x08), signature = Delta(b, 0x0c), btable = Delta(b, 0x10);
	if (name == 0 || interface == 0 || btable == 0 || name >= size || interface >= size || btable >= size)
	{
		ARMUnmapRegion(c->fBase);
		free(c->fBytes);
		delete c;
		return nil;
	}
	strncpy(c->fImplementation, (const char*) b + name, sizeof(c->fImplementation) - 1);
	strncpy(c->fInterface, (const char*) b + interface, sizeof(c->fInterface) - 1);
	if (signature != 0 && signature < size)
	{
		// the capability list, as it lies: name, value, ..., ""
		uint32_t at = signature, n = 0;
		while (at < size && n + 2 < sizeof(c->fSignature))
		{
			size_t nameLength = strnlen((const char*) b + at, size - at);
			if (nameLength == 0)
				break;
			size_t valueLength = strnlen((const char*) b + at + nameLength + 1, size - at - nameLength - 1);
			size_t whole = nameLength + 1 + valueLength + 1;
			if (n + whole + 1 > sizeof(c->fSignature))
				break;
			memcpy(c->fSignature + n, b + at, whole);
			n += (uint32_t) whole;
			at += (uint32_t) whole;
		}
	}
	c->fBTable = c->fBase + btable;
	uint32_t t;
	c->fSizeofFn = (t = BranchTarget(b, 0x18)) != 0 ? c->fBase + t : 0;
	c->fAllocFn = (t = BranchTarget(b, 0x1c)) != 0 ? c->fBase + t : 0;
	c->fFreeFn = (t = BranchTarget(b, 0x20)) != 0 ? c->fBase + t : 0;
	uint32_t version = Word(b + 0x2c), flags = Word(b + 0x30);
	c->fKind = KindFor(c->fInterface);
	if ((flags & kci_IsMonitor) != 0)
		fprintf(stderr, "[armprotocols] %s (%s) is a monitor: NOT YET\n", c->fImplementation, c->fInterface);
	c->fInstanceSize = c->fSizeofFn != 0 ? ARMCall(c->fSizeofFn, nil, 0) : 0x10;

	TClassInfo& info = c->fInfo;
	info.fReserved1 = 0;
	info.fName = c->fImplementation;
	info.fInterface = c->fInterface;
	info.fSignature = c->fSignature;
	info.fMakeAt = c->fKind != nil ? c->fKind->fMakeAt : NoMakeAt;
	info.fEntryProc = nil;
	info.fSizeof = c->fKind != nil ? c->fKind->fSize : NoSize;
	info.fAlloc = nil;
	info.fFree = nil;
	info.fDefaultNew = ProxyNew;
	info.fDefaultDelete = ProxyDelete;
	info.fVersion = version;
	info.fFlags = flags;
	info.fSelector = nil;
	info.fReserved2 = 0;
	gClasses.push_back(c);
	RegisterProtocolStandIn(&info);
	fprintf(stderr, "[armprotocols] %s (implements %s) on the ARM interpreter at %08x, instances %u bytes%s\n",
			c->fImplementation, c->fInterface, c->fBase, c->fInstanceSize,
			c->fKind != nil ? "" : " (no host proxy: the ARM code's own use only)");
	return &info;
}


/*------------------------------------------------------------------------------
	I n s t a n c e s
------------------------------------------------------------------------------*/

// an ARM instance of the class, made as TClassInfo::MakeAt makes one
// (PrivateClassInfoMakeAt, ROM 0x0005cbac)
static uint32_t
MakeARMInstance(ARMClass* c)
{
	uint32_t memory = c->fAllocFn != 0 ? ARMCall(c->fAllocFn, nil, 0) : ARMAlloc(c->fInstanceSize, false);
	if (memory == 0)
		return 0;
	ARMWrite32(memory, 0);
	ARMWrite32(memory + 4, memory);
	ARMWrite32(memory + 8, c->fBTable);
	ARMWrite32(memory + 12, 0);
	return memory;
}

static void
FreeARMInstance(ARMClass* c, uint32_t instance)
{
	if (c != nil && c->fFreeFn != 0)
	{
		uint32_t a = instance;
		ARMCall(c->fFreeFn, &a, 1);
	}
	else
		ARMFree(instance);
}

struct ProxyEntry { const TProtocol* fProxy; uint32_t fInstance; };
static PVec<ProxyEntry>	gProxies;

uint32_t
ARMInstanceOf(const TProtocol* proxy)
{
	for (size_t i = 0; i < gProxies.size(); i++)
		if (gProxies[i].fProxy == proxy)
			return gProxies[i].fInstance;
	return 0;
}

uint32_t
ARMCallInstanceSlot(uint32_t instance, int slot, const uint32_t* args, int count)
{
	uint32_t realThis = 0, btable = 0;
	if (!ARMRead32(instance + 4, &realThis) || !ARMRead32(realThis + 8, &btable))
		ThrowMsg("armprotocols: not an ARM instance");
	uint32_t all[16];
	all[0] = realThis;
	for (int i = 0; i < count && i < 15; i++)
		all[i + 1] = args[i];
	return ARMCall(btable + (uint32_t) slot * 4, all, count + 1);
}

uint32_t
ARMCallSlot(const TProtocol* proxy, int slot, const uint32_t* args, int count)
{
	uint32_t instance = ARMInstanceOf(proxy);
	if (instance == 0)
		ThrowMsg("armprotocols: a proxy with no ARM instance");
	if (gTraceProtocols)
		fprintf(stderr, "[armprotocols] %s slot %d\n", proxy->fBTable != nil ? ((const TClassInfo*) proxy->fBTable)->fName : "?", slot);
	return ARMCallInstanceSlot(instance, slot, args, count);
}

// a proxy's New(): its ARM instance made and the ARM New() called
static TProtocol*
ProxyNew(TProtocol* p)
{
	ARMClass* c = ClassOfInfo((const TClassInfo*) p->fBTable);
	if (c == nil)
		return nil;
	uint32_t instance = MakeARMInstance(c);
	if (instance == 0)
		return nil;
	ProxyEntry e = { p, instance };
	gProxies.push_back(e);
	uint32_t made = ARMCallInstanceSlot(instance, 2);
	if (made == 0)
	{
		for (size_t i = 0; i < gProxies.size(); i++)
			if (gProxies[i].fProxy == p)
				gProxies.erase(i);
		FreeARMInstance(c, instance);
		return nil;
	}
	return p;
}

// a proxy's Delete(): the ARM Delete() called and its instance given back
static void
ProxyDelete(TProtocol* p)
{
	for (size_t i = 0; i < gProxies.size(); i++)
		if (gProxies[i].fProxy == p)
		{
			uint32_t instance = gProxies[i].fInstance;
			gProxies.erase(i);
			ARMCallInstanceSlot(instance, 3);
			FreeARMInstance(ClassOfInfo((const TClassInfo*) p->fBTable), instance);
			return;
		}
}


/*------------------------------------------------------------------------------
	M i r r o r s
------------------------------------------------------------------------------*/

struct Mirror { const void* fHost; uint32_t fKind; uint32_t fARM; };
static PVec<Mirror>	gMirrors;

uint32_t
ARMMirrorFor(const void* host, uint32_t size, uint32_t kind)
{
	if (host == nil)
		return 0;
	for (size_t i = 0; i < gMirrors.size(); i++)
		if (gMirrors[i].fHost == host && gMirrors[i].fKind == kind)
			return gMirrors[i].fARM;
	Mirror m = { host, kind, ARMAlloc(size < 8 ? 8 : size, true) };
	gMirrors.push_back(m);
	return m.fARM;
}

void*
ARMHostOf(uint32_t mirror, uint32_t kind)
{
	for (size_t i = 0; i < gMirrors.size(); i++)
		if (gMirrors[i].fARM == mirror && gMirrors[i].fKind == kind)
			return (void*) gMirrors[i].fHost;
	return nil;
}

void
ARMForgetMirror(const void* host)
{
	for (size_t i = 0; i < gMirrors.size(); )
		if (gMirrors[i].fHost == host)
		{
			ARMFree(gMirrors[i].fARM);
			gMirrors.erase(i);
		}
		else
			i++;
}

uint32_t
ARMCString(const char* s)
{
	uint32_t n = (uint32_t) strlen(s) + 1;
	uint32_t a = ARMAlloc(n, false);
	if (a != 0)
		memcpy(ARMHostAddress(a, n), s, n);
	return a;
}


/*------------------------------------------------------------------------------
	T h e   g l u e
------------------------------------------------------------------------------*/

static void
CString(ARMTrapContext& c, int arg, char* buffer, uint32_t size)
{
	uint32_t a = c.Arg(arg);
	if (a == 0 || !c.ReadCString(a, buffer, size))
		buffer[0] = 0;
}

// ROM 0x0005c938 AllocInstanceByName__FPCcT1
// An instance of an ARM part's class for the ARM code: the ARM instance
// itself.  NOT YET: a host implementation handed to ARM code.
static bool
Glue_AllocInstanceByName(void*, ARMTrapContext& c)
{
	char interface[64], implementation[64];
	CString(c, 0, interface, sizeof(interface));
	CString(c, 1, implementation, sizeof(implementation));
	const TClassInfo* info = gProtocolRegistry->Satisfy(interface, implementation[0] != 0 ? implementation : nil, (ULong) 0);
	ARMClass* k = info != nil ? ClassOfInfo(info) : nil;
	uint32_t instance = 0;
	if (k != nil)
	{
		instance = MakeARMInstance(k);
		if (instance != 0)
			gProtocolRegistry->UpdateInstanceCount(info, 1);
	}
	else if (info != nil)
		fprintf(stderr, "[armprotocols] the ARM code asked for %s (%s), a host implementation: NOT YET\n", interface, info->fName);
	if (gTraceProtocols)
		fprintf(stderr, "[armprotocols] AllocInstanceByName(%s, %s) -> %08x\n", interface, implementation, instance);
	c.Return(instance);
	return true;
}

// ROM 0x0005c884 NewByName__FPCcT1
static bool
Glue_NewByName(void* refCon, ARMTrapContext& c)
{
	Glue_AllocInstanceByName(refCon, c);
	uint32_t instance = c.Register(0);
	if (instance != 0)
	{
		uint32_t made = ARMCallInstanceSlot(instance, 2);
		if (made == 0)
		{
			uint32_t btable = 0;
			ARMRead32(instance + 8, &btable);
			FreeARMInstance(ClassOfBTable(btable), instance);
		}
		instance = made;
	}
	c.Return(instance);
	return true;
}

// ROM 0x0005c9c4 FreeInstance__FP9TProtocol
static bool
Glue_FreeInstance(void*, ARMTrapContext& c)
{
	uint32_t instance = c.Arg(0);
	if (instance != 0)
	{
		uint32_t btable = 0;
		ARMRead32(instance + 8, &btable);
		ARMClass* k = ClassOfBTable(btable);
		FreeARMInstance(k, instance);
		if (k != nil)
			gProtocolRegistry->UpdateInstanceCount(&k->fInfo, -1);
	}
	c.Return(0);
	return true;
}

// The name server: an ARM TUNameServer is a block on the ARM side the host
// keeps nothing in; each call is a host TUNameServer's.  A thing registered
// is an ARM word (an ARM address, a port id) kept as it is.
// ROM 0x001304c0 __ct__12TUNameServerFv
static bool
Glue_TUNameServer_ctor(void*, ARMTrapContext& c)
{
	uint32_t self = c.Arg(0);
	if (self == 0)
		self = ARMAlloc(0x10, true);
	else
		for (uint32_t i = 0; i < 0x10; i += 4)
			c.Write32(self + i, 0);
	c.Return(self);
	return true;
}
// ROM 0x00130544 __dt__12TUNameServerFv
static bool
Glue_TUNameServer_dtor(void*, ARMTrapContext& c)
{
	if ((c.Arg(1) & 1) != 0)
		ARMFree(c.Arg(0));
	c.Return(0);
	return true;
}
// ROM 0x001309dc Lookup__12TUNameServerFPcT1PUlT3
static bool
Glue_TUNameServer_Lookup(void*, ARMTrapContext& c)
{
	char name[64], type[64];
	CString(c, 1, name, sizeof(name));
	CString(c, 2, type, sizeof(type));
	uint32_t thingAt = c.Arg(3), specAt = c.Arg(4);
	ULong thing = 0, spec = 0;
	TUNameServer nameServer;
	NewtonErr err = nameServer.Lookup(name, type, &thing, &spec);
	if (thingAt != 0)
		c.Write32(thingAt, (uint32_t) thing);
	if (specAt != 0)
		c.Write32(specAt, (uint32_t) spec);
	if (gTraceProtocols)
		fprintf(stderr, "[armprotocols] Lookup(%s, %s) -> %ld %08x\n", name, type, (long) err, (uint32_t) thing);
	c.Return((uint32_t) err);
	return true;
}
// ROM 0x001305b8 RegisterName__12TUNameServerFPcT1UlT3
static bool
Glue_TUNameServer_RegisterName(void*, ARMTrapContext& c)
{
	char name[64], type[64];
	CString(c, 1, name, sizeof(name));
	CString(c, 2, type, sizeof(type));
	TUNameServer nameServer;
	NewtonErr err = nameServer.RegisterName(name, type, c.Arg(3), c.Arg(4));
	if (gTraceProtocols)
		fprintf(stderr, "[armprotocols] RegisterName(%s, %s, %08x) -> %ld\n", name, type, c.Arg(3), (long) err);
	c.Return((uint32_t) err);
	return true;
}
// ROM 0x001306b8 UnRegisterName__12TUNameServerFPcT1
static bool
Glue_TUNameServer_UnRegisterName(void*, ARMTrapContext& c)
{
	char name[64], type[64];
	CString(c, 1, name, sizeof(name));
	CString(c, 2, type, sizeof(type));
	TUNameServer nameServer;
	c.Return((uint32_t) nameServer.UnRegisterName(name, type));
	return true;
}


void	InstallARMKernelGlue(void);		// ARMKernelGlue.cpp
void	InstallARMLists(void);			// ARMLists.cpp

void
InstallARMProtocols(void)
{
	SetARMProtocolPartLoader(LoadPart);
	InstallARMKernelGlue();
	InstallARMLists();
	ARMRegisterGlue("AllocInstanceByName__FPCcT1", Glue_AllocInstanceByName);
	ARMRegisterGlue("NewByName__FPCcT1", Glue_NewByName);
	ARMRegisterGlue("FreeInstance__FP9TProtocol", Glue_FreeInstance);
	ARMRegisterGlue("__ct__12TUNameServerFv", Glue_TUNameServer_ctor);
	ARMRegisterGlue("__dt__12TUNameServerFv", Glue_TUNameServer_dtor);
	ARMRegisterGlue("Lookup__12TUNameServerFPcT1PUlT3", Glue_TUNameServer_Lookup);
	ARMRegisterGlue("RegisterName__12TUNameServerFPcT1UlT3", Glue_TUNameServer_RegisterName);
	ARMRegisterGlue("UnRegisterName__12TUNameServerFPcT1", Glue_TUNameServer_UnRegisterName);
}
