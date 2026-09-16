/*
	File:		MemObjManager.h

	Contains:	The memory object database: the machine's domains, environments
				and heaps by four-character name, and what the kernel made of each
				(the domain's or environment's object id, the heap's address).

				The layout of memory is data: a domain table (one of two, by RAM
				size - see docs/os600/memobj-tables.md), the environment table
				with each environment's default heap and its client and manager
				domains, and the data-area table (where a domain's globals live).
				BuildMemObjDatabase turns them into four tables of entries in the
				memory object heap (gMemObjHeap) - domains, environments, heaps and
				persistent heaps - that MemObjManager's Find* and Register* work on.

				The public MemObjManager calls serve both modes: in user mode they
				go through GenericSWI kGeneric_GetMemObjInfo with the request in
				the caller's task globals block (MemObjRequest), which
				PrimGetMemObjInfo answers.

	Reconstructed from:	MemObjManager 0x0011ea90-0x0011fbd8, DomainInfo / EnvironmentInfo /
				PersistentDBEntry there, BuildMemObjDatabase 0x0011e84c,
				ComputeMemObjDatabaseSize 0x0011e784, PrimGetMemObjInfo 0x0011f4bc,
				InitCGlobals 0x00045c84 (the choice of domain table)
*/

#ifndef __MEMOBJMANAGER_H
#define __MEMOBJMANAGER_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __SINGLEQ_H
#include "SingleQ.h"
#endif

#include <stdint.h>


/* -------------------------------------------------------------------------------
	The tables the ROM carries (MemObjTables.cpp, generated)
------------------------------------------------------------------------------- */

struct DomainTableEntry
{
	ULong		fName;
	ULong		fBase;				// virtual, 1 MB aligned
	ULong		fSize;
	ULong		fHeapSize;
	ULong		fHandleHeapSize;
	ULong		fHeapFlags;			// kHeapFlag_*
};

struct EnvTableEntry
{
	ULong		fName;
	ULong		fDefaultHeap;		// name of the domain whose heap is the default
	ULong		fHeapDomain;		// name
	ULong		fStackDomain;		// name
	const ULong* fClientDomains;	// names, 0-terminated; nil for none
	const ULong* fManagerDomains;
};

struct DataAreaEntry
{
	ULong		fName;
	ULong		fGlobalROMBase;		// the initialised data in ROM
	ULong		fGlobalBase;		// where it goes
	ULong		fUnknown0c;
	ULong		fGlobalInitSize;
	ULong		fGlobalZeroSize;
};

enum
{
	kHeapFlag_Persistent	= 0x01,
	kHeapFlag_ReadOnly		= 0x02,
	kHeapFlag_Cacheable		= 0x04,
	kHeapFlag_ExceptOnNoMem	= 0x08,
	kHeapFlag_MakeHeapDomain= 0x10,
	kHeapFlag_HunkOMemory	= 0x20
};

extern const DomainTableEntry	g1MegDomainTable[];		// 0x0c1012ac  for 1 MB of RAM or less
extern const DomainTableEntry	g4MegDomainTable[];		// 0x0c10139c  for more
extern const EnvTableEntry		gEnvTable[];			// 0x0c10152c
extern const DataAreaEntry		DataAreaTable[];		// ROM 0x00000040
extern const DomainTableEntry*	gDomainTable;			// 0x0c1012a8  one of the two, by RAM size

const ULong kOneMegabyte = 0x100000;
void	SelectDomainTable(ULong ramSize);					// InitCGlobals's choice


/* -------------------------------------------------------------------------------
	What the database hands out
------------------------------------------------------------------------------- */

// ROM size 0x2c
class DomainInfo
{
	public:
		void		InitDomainInfo(ULong name, ULong unknown, ULong base, ULong size);
		void		InitHeapInfo(ULong heapSize, ULong handleHeapSize, ULong heapFlags);
		void		InitGlobalInfo(ULong globalBase, ULong globalROMBase, ULong initSize, ULong zeroSize);

		ULong		Name()				{ return fName; }
		ULong		Base()				{ return fBase; }
		ULong		Size()				{ return fSize; }
		Boolean		HasGlobals()		{ return GlobalSize() != 0; }
		ULong		GlobalBase()		{ return fGlobalBase; }
		ULong		GlobalSize()		{ return fGlobalInitSize + fGlobalZeroSize; }
		ULong		GlobalROMBase()		{ return fGlobalROMBase; }
		ULong		GlobalInitSize()	{ return fGlobalInitSize; }
		ULong		GlobalZeroSize()	{ return fGlobalZeroSize; }
		Boolean		HasHeap()			{ return fHeapSize != 0 || fHandleHeapSize != 0; }
		Boolean		MakeHeapDomain()	{ return (fHeapFlags & kHeapFlag_MakeHeapDomain) != 0; }
		Boolean		IsSegregated()		{ return HasHeap() && fHandleHeapSize != 0; }
		Boolean		IsPersistent()		{ return HasHeap() && (fHeapFlags & kHeapFlag_Persistent) != 0; }
		Boolean		IsReadOnly()		{ return HasHeap() && (fHeapFlags & kHeapFlag_ReadOnly) != 0; }
		Boolean		IsCacheable()		{ return HasHeap() && (fHeapFlags & kHeapFlag_Cacheable) != 0; }
		Boolean		ExceptOnNoMem()		{ return HasHeap() && (fHeapFlags & kHeapFlag_ExceptOnNoMem) != 0; }
		Boolean		IsHunkOMemory()		{ return HasHeap() && (fHeapFlags & kHeapFlag_HunkOMemory) == kHeapFlag_HunkOMemory; }
		ULong		HeapSize()			{ return fHeapSize; }
		ULong		HandleHeapSize()	{ return fHandleHeapSize; }

		ULong		fName;				// +0x00
		ULong		fUnknown04;			// +0x04  0
		ULong		fBase;				// +0x08
		ULong		fSize;				// +0x0c
		ULong		fGlobalROMBase;		// +0x10
		ULong		fGlobalBase;		// +0x14
		ULong		fGlobalInitSize;	// +0x18
		ULong		fGlobalZeroSize;	// +0x1c
		ULong		fHeapSize;			// +0x20
		ULong		fHandleHeapSize;	// +0x24
		ULong		fHeapFlags;			// +0x28
};

// ROM size 0x14
class EnvironmentInfo
{
	public:
		void		Init(ULong name, ULong unknown, ULong defaultHeap, ULong heapDomain, ULong stackDomain);
		Boolean		Domains(ULong index, ULong* outName, Boolean* outIsManager, long* outError);	// the index'th domain, clients then managers

		ULong		Name()				{ return fName; }
		ULong		DefaultHeap()		{ return fDefaultHeap; }
		ULong		DefaultHeapDomain()	{ return fDefaultHeapDomain; }
		ULong		DefaultStackDomain(){ return fDefaultStackDomain; }

		ULong		fName;				// +0x00
		ULong		fUnknown04;			// +0x04  0
		ULong		fDefaultHeap;		// +0x08  a domain name
		ULong		fDefaultHeapDomain;	// +0x0c
		ULong		fDefaultStackDomain;// +0x10
};

// ROM size 0x24: a persistent heap's record; the 0x40 flag bit marks a slot
// in use, bits 8-15 the domain's index in the domain table.  Slots are
// 'emty' when free.
class PersistentDBEntry
{
	public:
		void		Init(ULong name, Boolean reset, ULong domainIndex);

		ULong		fName;				// +0x00
		void*		fHeap;				// +0x04  the heap's address (FindHeapRef reads it)
		VAddr		fStart;				// +0x08  the heap area (NewPersistentVMHeap)
		ULong		fSize;				// +0x0c
		ULong		fUnknown10;			// +0x10
		TSingleQContainer fQueue;		// +0x14
		TObjectId	fDomainId;			// +0x1c  the domain's kernel object, -1 when unknown
		ULong		fFlags;				// +0x20  kPersistent_*, domain index << 8
};

enum
{
	kPersistent_InUse		= 0x40,
	kPersistent_Unknown80	= 0x80,
	kPersistent_IndexShift	= 8
};

const ULong kEmptyEntryName = 'emty';


/* -------------------------------------------------------------------------------
	The database
------------------------------------------------------------------------------- */

enum MemObjType
{
	kMemObjDomain = 0,				// entries: MemObjEntry {name, domain id}
	kMemObjEnvironment,				// entries: MemObjEntry {name, environment id}
	kMemObjHeap,					// entries: MemObjEntry {name, heap address}
	kMemObjPersistent				// entries: PersistentDBEntry
};

struct MemObjEntry
{
	ULong		fName;
	uintptr_t	fValue;				// an object id, or a heap address (a word in the ROM; pointer-wide here)
};

struct MemObjTable
{
	void*		fEntries;
	ULong		fCount;
	ULong		fEntrySize;
};

struct MemObjDatabase
{
	MemObjTable	fTables[4];			// indexed by MemObjType; the entries follow in the heap
};

extern MemObjDatabase*	gMemObjDatabase;			// 0x0c1034a8 (unnamed in the symbol table)
extern void*			gMemObjHeap;				// 0x0c101264  where InitCGlobals put the database

void	ComputeMemObjDatabaseSize(ULong* outSize);
void	BuildMemObjDatabase();


// The request block a user-mode call leaves in its task globals for
// PrimGetMemObjInfo: the selector and arguments, and the answer in place.
union MemObjRequest
{
	struct
	{
		ULong		fSelector;		// +0x00  kMemObjReq_*
		ULong		fArg1;			// +0x04
		ULong		fArg2;			// +0x08
		union
		{
			struct { Boolean fIsManager; Boolean fFound; } fDomainName;	// +0x0c, +0x0d
			MemObjEntry	fEntry;											// +0x0c
			PersistentDBEntry fPersistentEntry;
		};
	};
	DomainInfo		fDomainInfo;	// from +0x00
	EnvironmentInfo	fEnvironmentInfo;
};

enum
{
	kMemObjReq_DomainInfo = 0,		// fArg2 = index -> fDomainInfo
	kMemObjReq_DomainInfoByName,	// fArg1 = name -> fDomainInfo
	kMemObjReq_EnvironmentInfo,		// fArg2 = index -> fEnvironmentInfo
	kMemObjReq_EnvDomainName,		// fArg1 = environment, fArg2 = index -> fArg1 = name, fDomainName
	kMemObjReq_GetEntryByIndex,		// fArg1 = type, fArg2 = index -> fEntry
	kMemObjReq_GetEntryByName,		// fArg1 = type, fArg2 = name -> fEntry
	kMemObjReq_SetEntryByIndex,		// fArg1 = type, fArg2 = index, fEntry
	kMemObjReq_SetEntryByName		// fArg1 = type, fArg2 = name, fEntry
};

long	PrimGetMemObjInfo();			// GenericSWI kGeneric_GetMemObjInfo


class MemObjManager
{
	public:
		// the kernel-mode primitives
		static NewtonErr	PrimGetDomainInfo(ULong index, DomainInfo* outInfo);
		static NewtonErr	PrimGetDomainInfoByName(ULong name, DomainInfo* outInfo);
		static NewtonErr	PrimGetEnvironmentInfo(ULong index, EnvironmentInfo* outInfo);
		static NewtonErr	PrimGetEnvDomainName(ULong envName, ULong index, ULong* outName, Boolean* outIsManager, Boolean* outFound);
		static NewtonErr	PrimGetEntryByIndex(MemObjType type, ULong index, void* outEntry);
		static NewtonErr	PrimSetEntryByIndex(MemObjType type, ULong index, void* entry);
		static NewtonErr	PrimGetEntryByName(MemObjType type, ULong name, void* outEntry);
		static NewtonErr	PrimSetEntryByName(MemObjType type, ULong name, void* entry);
		static void*		EntryLocByIndex(MemObjType type, ULong index);
		static void*		EntryLocByName(MemObjType type, ULong name);
		static void			CopyObject(MemObjType type, void* to, void* from);

		// the calls either side makes
		static Boolean		GetDomainInfo(ULong index, DomainInfo* outInfo, long* outError);		// false at the end (error 0) or on error
		static NewtonErr	GetDomainInfoByName(ULong name, DomainInfo* outInfo);
		static Boolean		GetEnvironmentInfo(ULong index, EnvironmentInfo* outInfo, long* outError);
		static Boolean		GetEnvDomainName(ULong envName, ULong index, ULong* outName, Boolean* outIsManager, long* outError);
		static Boolean		FindEntryByIndex(MemObjType type, ULong index, void* outEntry, long* outError);
		static NewtonErr	FindEntryByName(MemObjType type, ULong name, void* outEntry);
		static NewtonErr	RegisterEntryByName(MemObjType type, ULong name, void* entry);

		static NewtonErr	FindEnvironmentId(ULong name, TObjectId* outId);
		static NewtonErr	FindDomainId(ULong name, TObjectId* outId);
		static NewtonErr	FindHeapRef(ULong name, void** outHeap);
		static void			RegisterEnvironmentId(ULong name, TObjectId id);
		static void			RegisterDomainId(ULong name, TObjectId id);
		static void			RegisterHeapRef(ULong name, void* heap);
		static Boolean		GetPersistentRef(ULong index, PersistentDBEntry** outEntry, long* outError);
		static NewtonErr	RegisterPersistentNewEntry(ULong name, PersistentDBEntry* entry);
		static NewtonErr	DeregisterPersistentEntry(ULong name);
};

#endif	/* __MEMOBJMANAGER_H */
