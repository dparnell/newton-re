/*
	File:		frames/ObjectHeap.h

	Contains:	The NewtonScript object heap - TObjectHeap, the object layouts
				it manages, the RefHandle table and the object system's
				globals.  The public interface is the DDK's objects.h
				(Frames.h); this header is for the object system's own files
				and its tests.

	Reconstructed from the MP2100 D ROM (the frames object system at
	0x002f420c-0x002fb24c, the collector at 0x002bd4f8-0x002be600, symbols
	at 0x0032d4a8-0x0032dd00); docs/frames/README.md explains the layouts.

	Host re-expression: a Ref is the ARM's word, so on a host it is
	pointer-sized (sync_ddk_headers.py makes objects.h's Ref a Long).  An
	object header is a size-and-flags word and a GC word as in the ROM, but
	both are pointer-sized so that the GC word can hold a forwarding address
	(SweepAndCompact) as well as the lock count and the marking index; the
	class/map slot and the slots after it are Refs.  So the ROM's offsets
	(class at +8, slots from +0xc, block sizes rounded to 4) become
	sizeof(ObjHeader), kObjBodySize and sizeof(Ref) here; on a 32-bit host
	they are the ROM's numbers.  Persistent forms of objects (packages,
	stores, the ROM's own object graph) keep the ARM layout and are imported
	(not yet reconstructed).
*/

#ifndef __OBJECTHEAP_H
#define __OBJECTHEAP_H

#ifndef __OBJHEADER_H
#include "ObjHeader.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

// ULong32 (host_compat.h): the ROM's 32-bit word where its width matters (symbol hashes)


/* -------------------------------------------------------------------------------
	Objects
------------------------------------------------------------------------------- */

// the object flags (the low byte of the size word); kObjSlotted, kObjFrame,
// kObjLocked, kObjReadOnly and kObjDirty are objects.h's
const int kObjFree = 4;					// a free block
const int kObjMarked = 8;				// reached during the GC's mark phase
const int kObjForward = 32;				// a forwarding object: its class slot holds the ref to use instead

// the class-slot flags of a frame map (a map is a slotted object whose class
// slot holds these instead of a class)
const Ref kMapSorted = 4;				// the tags are sorted (SearchSortedMap)
const Ref kMapShared = 8;				// shared by more than one frame: extend by a new map, never in place
const Ref kMapProto = 16;				// one of the tags is _proto

const Ref kWeakArrayClass = MAKEIMMED(kImmedSpecial, 4);		// 0x12: an array whose slots do not keep objects alive
const Ref kFaultBlockClass = MAKEIMMED(kImmedSpecial, 8);		// 0x22: a soup entry not yet read from its store
const Ref kDeclawedRef = MAKEIMMED(kImmedSpecial, 0x10);		// 0x42: a ref into a package that has gone

// a pointer ref and its object
#define MAKEPTR(p)		((Ref) (p) + kTagPointer)
#define PTRVALUE(r)		((ObjHeader*) ((r) - kTagPointer))

const ULong kObjSizeShift = 8;
const ULong kObjMaxSize = 0xffffff;					// the size field: 24 bits
const ULong kObjLockShift = 24;
const ULong kObjLockMask = ((ULong) 0xff) << kObjLockShift;
const ULong kObjGCIndexMask = ~kObjLockMask;
const ULong kObjLockForever = ((ULong) 0xff) << kObjLockShift;

const long kObjAlign = sizeof(Ref);					// blocks are rounded to this (4 in the ROM)
const long kObjBodySize = sizeof(ObjHeader) + sizeof(Ref);	// header and class slot: the size of an empty object (0xc in the ROM)

inline ULong	ObjSize(const ObjHeader* o)			{ return o->fSizeAndFlags >> kObjSizeShift; }
inline ULong	ObjFlags(const ObjHeader* o)		{ return o->fSizeAndFlags & 0xff; }
inline void		SetObjSize(ObjHeader* o, ULong size)	{ o->fSizeAndFlags = (o->fSizeAndFlags & 0xff) | (size << kObjSizeShift); }
inline void		SetObjFlags(ObjHeader* o, ULong flags)	{ o->fSizeAndFlags = (o->fSizeAndFlags & ~(ULong) 0xff) | flags; }
inline ULong	AlignedSize(ULong size)				{ return (size + kObjAlign - 1) & ~(ULong) (kObjAlign - 1); }
inline ULong	ObjAlignedSize(const ObjHeader* o)	{ return AlignedSize(ObjSize(o)); }
inline ObjHeader* NextBlock(ObjHeader* o)			{ return (ObjHeader*) ((char*) o + ObjAlignedSize(o)); }

// the body: the class slot (a frame's map) and the slots, or the binary data
inline Ref*		ObjSlots(ObjHeader* o)				{ return (Ref*) ((char*) o + sizeof(ObjHeader)); }	// slot 0 is the class
inline Ref&		ObjClass(ObjHeader* o)				{ return ObjSlots(o)[0]; }
inline Ref*		ObjArraySlots(ObjHeader* o)			{ return ObjSlots(o) + 1; }
inline char*	ObjData(ObjHeader* o)				{ return (char*) o + kObjBodySize; }
inline long		ObjArrayLength(const ObjHeader* o)	{ return ((long) ObjSize(o) - kObjBodySize) / (long) sizeof(Ref); }	// UnsafeArrayLength (0x002f87b8)
inline long		ObjBinaryLength(const ObjHeader* o)	{ return (long) ObjSize(o) - kObjBodySize; }
inline ULong	ArrayObjSize(long length)			{ return (ULong) (kObjBodySize + length * (long) sizeof(Ref)); }
inline ULong	BinaryObjSize(long length)			{ return (ULong) (kObjBodySize + length); }

// the lengths the size field allows (the ROM's 0xfffff3 and 0x3fffc)
const long kMaxBinaryLength = (long) kObjMaxSize - kObjBodySize;
const long kMaxArrayLength = ((long) kObjMaxSize - kObjBodySize) / (long) sizeof(Ref);

// A frame map: a slotted object with kMap... flags in its class slot, the
// supermap in slot 0 and the tags after it; Length(map) - 1 tags.
inline Ref&		MapFlags(ObjHeader* map)			{ return ObjClass(map); }
inline Ref&		MapSuperMap(ObjHeader* map)			{ return ObjArraySlots(map)[0]; }
inline Ref*		MapTags(ObjHeader* map)				{ return ObjArraySlots(map) + 1; }
inline long		MapTagCount(const ObjHeader* map)	{ return ObjArrayLength(map) - 1; }

// A symbol: a binary of class kSymbolClass holding its hash then its name.
struct SymbolData
{
	ULong32	fHash;				// SymbolHashFunction of the name
	char	fName[1];			// C string
};
inline SymbolData* ObjSymbol(ObjHeader* o)			{ return (SymbolData*) ObjData(o); }

// An indirect binary (flags & 3 == kObjFrame without kObjSlotted): its data
// lives elsewhere and a table of procedures at the start of the body says
// how to reach it (large binaries, C++ objects wrapped as binaries).
struct IndirectBinaryProcs
{
	long	(*fLength)(void* data);
	char*	(*fDataPtr)(void* data);
	void	(*fSetLength)(void* data, long length);
	Ref		(*fClone)(void* data, Ref theClass);
	void	(*fDelete)(void* data);
	void	(*fSetClass)(void* data, RefArg theClass);
	void	(*fMark)(void* data);
	void	(*fUpdate)(void* data);
};
inline IndirectBinaryProcs*& ObjIndirectProcs(ObjHeader* o)	{ return *(IndirectBinaryProcs**) ObjData(o); }
inline void*	ObjIndirectData(ObjHeader* o)		{ return ObjData(o) + sizeof(IndirectBinaryProcs*); }
inline Boolean	ObjIsIndirect(const ObjHeader* o)	{ return (ObjFlags(o) & (kObjSlotted | kObjFrame)) == kObjFrame; }

// A fault block (a frame of class kFaultBlockClass): the entry's handler,
// its store and id, and the object once it is in memory.
const long kFaultBlockHandlerSlot = 0;
const long kFaultBlockStoreSlot = 1;
const long kFaultBlockIdSlot = 2;
const long kFaultBlockObjectSlot = 3;


/* -------------------------------------------------------------------------------
	TObjectHeap
------------------------------------------------------------------------------- */

// a range of refs (a package's address range) to be replaced by kDeclawedRef
// at the next GC
class DeclawingRange
{
public:
					DeclawingRange(ULong start, ULong end, DeclawingRange* next);
	DeclawingRange*	Next(void) const					{ return fNext; }
	Boolean			InRange(Ref r) const;
	Boolean			InAnyRange(Ref r) const;

	ULong			fStart;			// +0x00
	ULong			fEnd;			// +0x04
	DeclawingRange*	fNext;			// +0x08
};


class TObjectHeap
{
public:
					TObjectHeap(long size, long allocateInTempMemory);
	virtual			~TObjectHeap();
	virtual void	DisposeMemory(void);

	Ref				AllocateObject(long size, ULong flags);
	Ref				AllocateBinary(RefArg theClass, long length);
	Ref				AllocateIndirectBinary(RefArg theClass, long length);
	Ref				AllocateArray(RefArg theClass, long length);
	Ref				AllocateFrame(void);
	Ref				AllocateFrameWithMap(RefArg map);
	Ref				AllocateMap(RefArg superMap, long length);
	Ref				Clone(RefArg obj);
	void			ResizeObject(RefArg obj, long size);
	void			ReplaceObject(Ref target, Ref replacement);
	void			SetLength(RefArg obj, long length);
	void			UnsafeSetArrayLength(RefArg obj, long length);
	void			UnsafeSetBinaryLength(RefArg obj, long length);

	void			GC(void);
	Boolean			InHeap(Ref r) const;
	Boolean			InHeap(const void* p) const	{ return p >= fStart && p < fEnd; }
	void			ClearRefHandles(void);
	Boolean			RegisterRangeForDeclawing(ULong start, ULong end);
	void			DeclawRefsInRegisteredRanges(void);

	ObjHeader*		BlockStatistics(ObjHeader* previous, ULong* size, Boolean* isFree);
	void			Statistics(ULong* freeSpace, ULong* largestFreeBlock);
	void			Uriah(void);
	void			UriahBinaryObjects(int printStrings);

	// block management
	ULong			CoalesceFreeBlocks(ObjHeader* block, long size);
	ObjHeader*		FindFreeBlock(long size);
	void			SplitBlock(ObjHeader* block, long size);
	void			MakeFreeBlock(ObjHeader* block, long size);
	ObjHeader*		AllocateBlock(long size, ULong flags);
	void			KillBlock(ObjHeader* block);
	ObjHeader*		ResizeBlock(ObjHeader* block, long size);

	// the collector (GC.cpp)
	void			Mark(Ref r);
	void			CleanUpWeakChain(void);
	Ref				UpdateRef(Ref r);
	void			SweepAndCompact(void);

	void*			fMemory;				// +0x04  the allocation (NewPtr)
	char*			fStart;					// +0x08  the first block
	char*			fEnd;					// +0x0c  past the last block
	ObjHeader*		fRover;					// +0x10  where the search for a free block starts
	ObjHeader*		fRefHandleTable;		// +0x14  a slotted object at the top of the heap whose slots are the RefHandles
	long			fFreeHandleIndex;		// +0x18  the head of the free handle chain (-1: none)
	Ref				fResizeRoot;			// +0x1c  a GC root holding the object being resized while its new block is found
	ULong			fRefHandleTableSize;	// +0x20  the size the table should have (ExpandObjectTable raises it, the GC grows the table)
	ObjHeader*		fWeakChain;				// +0x24  the weak arrays reached while marking, chained through their class slots
	Boolean			fInGC;					// +0x28
	DeclawingRange*	fDeclawingRanges;		// +0x2c
	Boolean			fDeclawing;				// +0x30  UpdateRef declaws instead of following forwarding
};

// the RefHandle table: a slotted object whose slots are the handles
const long kRefHandleTableEntries = 256;												// at creation
const ULong kRefHandleTableSize = sizeof(ObjHeader) + kRefHandleTableEntries * sizeof(RefHandle);	// 0x808 in the ROM
const ULong kRefHandleTableGrowth = 32 * sizeof(RefHandle);								// 0x100 bytes in the ROM
inline RefHandle* RefHandleTableEntries(ObjHeader* table)	{ return (RefHandle*) ObjSlots(table); }
inline long		RefHandleTableCount(ObjHeader* table)		{ return (long) ((ObjSize(table) - sizeof(ObjHeader)) / sizeof(RefHandle)); }


// The objects DeepClone and TotalClone have seen (and their clones): an
// array grown by 16, searched with the ROM's ListEQ.
class TPrecedentsVar
{
public:
				TPrecedentsVar();
	void		Append(RefArg obj);
	long		Find(RefArg obj);
	Ref			Get(long index);

	RefVar		fArray;			// +0x00
	long		fCount;			// +0x04
};


/* -------------------------------------------------------------------------------
	Globals
------------------------------------------------------------------------------- */

extern TObjectHeap*	gHeap;					// 0x0c10263c
extern Ref			gCurrentStackPos;		// 0x0c102654  generation (high 16 bits) and depth of the RefVar stack
extern Ref			gCacheObjPtrRef;		// 0x0c102640  ObjectPtr's one-entry cache
extern ObjHeader*	gCacheObjPtrPtr;		// 0x0c102644
extern Ref			gCacheLengthObj;		// 0x0c102648  Length's one-entry cache
extern long			gCacheLengthLen;		// 0x0c10264c
extern Ref			gVarFrame;				// 0x0c1018fc  the global variables (magic pointer 1.1)
extern Ref			gFunctionFrame;			// 0x0c102540  the global functions
extern Ref			gInheritanceFrame;		// 0x0c102278  class -> superclass (IsSubclassRef)
extern Ref			gStores;				// 0x0c102a30
extern Ref			gUnionSoups;			// 0x0c1027c4
extern Ref			gPackageStores;			// 0x0c1017d0
extern int			gVerboseGC;				// 0x0c1024f4
extern Ref			gROMBuiltinFunctions;	// the ROM's frame of built-in functions (magic pointer 1.2, the ROM's object 0x0062418d)
extern Ref			gROMSymbolTableRef;		// host: the ROM's symbol table once ROMImport has read the ROM's objects (NILREF else)

// the magic pointer tables (ResolveMagicPtr): table 0 the ROM's, 1 the two
// specials, even 2-8 the REx export tables, odd 3-9 the RAM tables of their
// imports (gRExImportTables 0x0c104c8c, gRExExportTableCounts 0x0c104cac)
const long			kMagicPointerTables = 10;
extern Ref*			gMagicPointerTables[kMagicPointerTables];
extern long			gMagicPointerTableCounts[kMagicPointerTables];

// the exception names the object system throws (user/ExceptionNames.cpp)
extern const ExceptionName exFrames;						// "evt.ex.fr"
extern const ExceptionName exFramesWithFrameData;			// "evt.ex.fr;type.ref.frame"
extern const ExceptionName exBadTypeWithFrameData;			// "evt.ex.fr.type;type.ref.frame"
extern const ExceptionName exInterpreterWithFrameData;		// "evt.ex.fr.intrp;type.ref.frame"
extern const ExceptionName exStoreError;					// "evt.ex.fr.store"
extern const ExceptionName exInterpreter;					// "evt.ex.fr.intrp"

// GC hooks (GC.cpp)
extern Handle		gGCRoots;				// 0x0c1024f8  Ref* entries
extern Handle		gDIYGCEntries;			// 0x0c1024fc  {refCon, mark, update} entries
extern Handle		gGCProcEntries;			// 0x0c102500  {refCon, proc} entries

// symbols (Symbols.cpp)
extern Ref			gSymbolTable;			// 0x0c102a60  an array, open-addressed by hash
extern long			gSymbolTableSize;		// 0x0c102a64
extern long			gSymbolTableHashShift;	// 0x0c102a68  index = hash >> shift
extern Ref*			gROMSymbolTable;		// 0x0c102a6c  the ROM's table, read-only
extern long			gROMSymbolTableSize;	// 0x0c102a70
extern long			gROMSymbolTableHashShift;	// 0x0c102a74
extern long			gNumSymbols;			// 0x0c102a78
extern long			gNumSlotsTaken;			// 0x0c102a7c  symbols plus deleted slots


/* -------------------------------------------------------------------------------
	The object system's internals (ObjectHeap.cpp, Objects.cpp, Symbols.cpp)
------------------------------------------------------------------------------- */

// refs to objects (objects.h declares Ptr ObjectPtr(Ref); OBJ is it as a header)
inline ObjHeader* OBJ(Ref r)					{ return (ObjHeader*) ObjectPtr(r); }
ObjHeader*	NoFaultObjectPtr(Ref obj);				// a fault block itself, not the entry it stands for
ObjHeader*	FaultCheckObjectPtr(Ref obj);			// nil for a fault block whose entry is not in memory
ObjHeader*	NoTouchObjectPtr(Ref obj, int* isLargeObject);
ObjHeader*	ResolveMagicPtr(Ref r);
Ref			ForwardReference(Ref r);
Boolean		IsFaultBlock(Ref r);
Ref			FollowFaultBlock(RefArg faultBlock);
extern Ref	(*gFollowFaultBlockProc)(RefArg faultBlock);	// the stores layer's reader (stores/Entries.cpp)
void		DirtyObject(Ref obj);
void		UndirtyObject(Ref obj);
Boolean		InROMSymbolSpace(Ref r);				// symbols there are unique: compare by identity
Boolean		InROMObjectArea(Ref r);					// host: an object read from the ROM image (ROMImport)

// frames and maps
long		FindOffset(Ref map, Ref tag);
long		FindOffset1(Ref map, Ref tag, Ref* foundIn);
long		SearchSortedMap(ObjHeader* map, long count, Ref tag);
void		FindOffsetCacheClear(void);
long		AddSlot(RefArg frame, RefArg tag);
long		ConvertToSortedMap(RefArg frame, long trackedIndex);
Ref			ExtendSharedMap(RefArg map, int length);
Ref			ShrinkSharedMap(RefArg map, RefArg foundIn, long index);
void		ShrinkArray(RefArg array, long index);
Ref			GetTag(RefArg map, long index, long* baseIndex);
long		ComputeMapSize(RefArg map);
Ref			SharedFrameMap(RefArg frame);
Ref			GetProtoVariable(RefArg context, RefArg name, long* exists);
void		SetFramePathFor1XFunctions(RefArg obj, RefArg thePath, RefArg value);
Ref			GlobalFunctionLookup(Ref name);
Ref			UnsafeGetFrameSlot(Ref frame, Ref tag, long* exists);

// symbols
Ref			Intern(UniChar* name);
ULong32		SymbolHashFunction(const char* name);
int			SymbolCompare(Ref sym1, Ref sym2);
Boolean		UnsafeSymbolEqual(Ref sym1, Ref sym2, ULong32 hash);
Boolean		FindSymbol(Ref* table, long size, long hashShift, const char* name, ULong32 hash, long* index);
void		InternExistingSymbol(RefArg sym);
void		GCTWA(void);
void		InitSymbols(void);
void		InitROMSymbols(void);					// host: the RS symbols as a read-only symbol space (RSSymbols.h)

// interpreter caches (the interpreter is not reconstructed yet)
void		ICacheClear(void);
void		ICacheClearFrame(Ref frame);
void		ICacheClearSymbol(Ref sym, ULong32 hash);

// exceptions
void		ThrowExFramesWithBadValue(NewtonErr err, RefArg value);
void		ThrowExInterpreterWithSymbol(NewtonErr err, RefArg sym);
void		DeleteRefStruct(RefStruct* r);

// GC hooks
Ptr			CommonGCRegister(Handle* h, long entrySize);
void		CommonGCUnregister(Handle h, long entrySize, void* refCon);
void		CommonGCClearHooks(Handle* h);
Boolean		RegisterRangeForDeclawing(ULong start, ULong end);
void		DeclawRefsInRegisteredRanges(void);

Boolean		OnStack(const void* p);

#endif	/* __OBJECTHEAP_H */
