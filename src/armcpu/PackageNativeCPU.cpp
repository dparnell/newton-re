/*
	File:		armcpu/PackageNativeCPU.cpp

	Contains:	A package's native function run on the ARM interpreter,
				its calls into the ROM answered on the host
				(PackageNativeCPU.h; the design: docs/armcpu/README.md).
*/

#include "PackageNativeCPU.h"
#include "ARMCPU.h"
#include "PublicJumpTable.h"
#include "PackageNatives.h"
#include "ROMImport.h"
#include "FramesPart.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "SortTables.h"
#include "utility/Unicode.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern const ExceptionName exMsgException;

// (the object system's functions the fixtures' native code reaches that
//  no header of the reconstruction's declares)
void	ArrayMunger(RefArg a1, long a1start, long a1count, RefArg a2, long a2start, long a2count);
long	ArrayPosition(RefArg array, RefArg item, long start, RefArg test);
void	BinaryMunger(RefArg a1, long a1start, long a1count, RefArg a2, long a2start, long a2count);
void	StrMunger(RefArg s1, long s1start, long s1count, RefArg s2, long s2start, long s2count);
double	CoerceToDouble(RefArg r);
Ref		EnsureInternal(RefArg obj);
void	ReplaceObjectRef(Ref target, Ref replacement);
void	SortArray(RefArg array, RefArg test, RefArg key);
int		StrBeginsWith(RefArg str, RefArg prefix);
void	StrCapitalizeWords(RefArg str);
void	StrCapitalize(RefArg str);
void	StrDowncase(RefArg str);
void	StrUpcase(RefArg str);
long	StrPosition(RefArg str, RefArg substr, long start);
long	StrReplace(RefArg str, RefArg substr, RefArg replacement, long count);
Ref		Substring(RefArg str, long start, long count);
void	TrimString(RefArg str);
void	PrintObject(RefArg obj, long indent);

extern const ExceptionName exInterpreter;

/*------------------------------------------------------------------------------
	T h e   3 2 - b i t   w o r l d
	(docs/armcpu/README.md, "Refs in the 32-bit world")
------------------------------------------------------------------------------*/

const uint32_t	kCodeBase		= 0x20000000;	// the package's code binary (a copy)
const uint32_t	kPackageHeaderSize	= 0x34;
const uint32_t	kArenaBase		= 0x30000000;	// RefHandles, RefVars, and the stack at the top
const uint32_t	kArenaSize		= 0x00100000;
const uint32_t	kStackSize		= 0x00010000;
const uint32_t	kHandleBase		= 0x40000000;	// pointer refs: kHandleBase + index * 8, tagged 1
const uint32_t	kHandleLimit	= 0x4ffffff8;
const uint32_t	kWindowBase		= 0x50000000;	// object data and slots mapped on demand
const uint32_t	kInterpreter	= 0x60000000;	// the interpreter, as the ARM code sees it
const uint32_t	kStackStates	= 0x61000000;	// stack state blocks, likewise
const uint32_t	kReturn			= 0x0ffffff0;	// the stop address a call returns to
const uint32_t	kCallbacks		= 0x70000000;	// host functions the ARM code was given to call (NativeEntry)
const uint32_t	kARMHandlerException	= 0x60;		// an ExceptionHandler's Exception (after the 0x58-byte jmp_buf)
const uint32_t	kARMHandlerState		= 0x08;		// and its jmp_buf

// A growable array (the standard containers reach <locale.h>, which the
// reconstruction's intl/Locale.h shadows on a case-insensitive file system).
template <class T>
class Vec
{
public:
				Vec() : fItems(nil), fCount(0), fCapacity(0) { }
				Vec(size_t n, const T& v) : fItems(nil), fCount(0), fCapacity(0) { resize(n, v); }
				~Vec() { free(fItems); }
	size_t		size() const					{ return fCount; }
	bool		empty() const					{ return fCount == 0; }
	T*			data()							{ return fItems; }
	T&			operator[](size_t i)			{ return fItems[i]; }
	const T&	operator[](size_t i) const		{ return fItems[i]; }
	T&			back()							{ return fItems[fCount - 1]; }
	void		pop_back()						{ fCount--; }
	T*			begin()							{ return fItems; }
	T*			end()							{ return fItems + fCount; }
	void		reserve(size_t n)
	{
		if (n <= fCapacity)
			return;
		fItems = (T*) realloc((void*) fItems, n * sizeof(T));
		fCapacity = n;
	}
	void		push_back(const T& v)
	{
		if (fCount == fCapacity)
			reserve(fCapacity == 0 ? 16 : fCapacity * 2);
		fItems[fCount++] = v;
	}
	void		resize(size_t n, const T& v)
	{
		reserve(n);
		for (size_t i = fCount; i < n; i++)
			fItems[i] = v;
		fCount = n;
	}
	void		swap(Vec& other)
	{
		T* items = fItems; fItems = other.fItems; other.fItems = items;
		size_t c = fCount; fCount = other.fCount; other.fCount = c;
		c = fCapacity; fCapacity = other.fCapacity; other.fCapacity = c;
	}
private:
				Vec(const Vec&);
	T*			fItems;
	size_t		fCount;
	size_t		fCapacity;
};

/*------------------------------------------------------------------------------
	T h e   A R M   w o r l d ' s   h e a p
	What the ARM code allocates - NewPtr/NewPtrClear, malloc, operator new -
	and gives back - DisposPtr, free, operator delete - lives here, one heap
	for every call and every package: a C++ object a native makes on one call
	is still there on the next, as on the Newton.  A block is a size word and
	a check word in front of the bytes (the ARM code sees neither); the free
	blocks are kept in address order and run together as they are given back.
------------------------------------------------------------------------------*/

const uint32_t	kHeapBase		= 0x80000000;	// the heap, for as far as it has grown
const uint32_t	kHeapLimit		= 0x04000000;	// (64MB at most)
const uint32_t	kHeapGrowth		= 0x00100000;
const uint32_t	kHeapBlockMark	= 0x41524d68;	// 'ARMh', the word before a block in use

class TARMHeap
{
public:
				TARMHeap() : fTop(0) { }
	bool		Contains(uint32_t a, uint32_t n) const
				{ return a >= kHeapBase && a - kHeapBase <= fBytes.size() && n <= fBytes.size() - (a - kHeapBase); }
	uint8_t*	At(uint32_t a)		{ return &fBytes[a - kHeapBase]; }
	uint32_t	Alloc(uint32_t n);				// ==> 0: no room
	void		Free(uint32_t a);
	uint32_t	BlockSize(uint32_t a);			// ==> 0: not a block of the heap

private:
	struct FreeBlock { uint32_t fAddr; uint32_t fSize; };	// fSize: the whole block, header and all
	uint32_t	Word(uint32_t a)	{ return BE32(At(a)); }
	void		SetWord(uint32_t a, uint32_t v)	{ uint8_t* p = At(a); p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }
	static uint32_t	BE32(const uint8_t* p)	{ return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
	Vec<uint8_t>	fBytes;
	Vec<FreeBlock>	fFree;
	uint32_t		fTop;					// the next byte never handed out (from kHeapBase)
};

static TARMHeap	gARMHeap;


uint32_t
TARMHeap::Alloc(uint32_t n)
{
	if (n > kHeapLimit)
		return 0;
	uint32_t whole = ((n + 7) & ~7u) + 8;
	for (size_t i = 0; i < fFree.size(); i++)
	{
		FreeBlock& f = fFree[i];
		if (f.fSize < whole)
			continue;
		uint32_t a = f.fAddr;
		if (f.fSize - whole >= 16)
		{
			f.fAddr += whole;
			f.fSize -= whole;
		}
		else
		{
			whole = f.fSize;
			for (size_t j = i + 1; j < fFree.size(); j++)
				fFree[j - 1] = fFree[j];
			fFree.pop_back();
		}
		SetWord(a, whole);
		SetWord(a + 4, kHeapBlockMark);
		return a + 8;
	}
	if (whole > kHeapLimit - fTop)
		return 0;
	if (fTop + whole > fBytes.size())
	{
		size_t size = ((size_t) fTop + whole + kHeapGrowth - 1) & ~(size_t) (kHeapGrowth - 1);
		fBytes.resize(size, 0);
	}
	uint32_t a = kHeapBase + fTop;
	fTop += whole;
	SetWord(a, whole);
	SetWord(a + 4, kHeapBlockMark);
	return a + 8;
}


uint32_t
TARMHeap::BlockSize(uint32_t a)
{
	if (a < kHeapBase + 8 || !Contains(a - 8, 8) || Word(a - 4) != kHeapBlockMark)
		return 0;
	return Word(a - 8) - 8;
}


void
TARMHeap::Free(uint32_t a)
{
	if (BlockSize(a) == 0)
	{
		if (a != 0)
			fprintf(stderr, "[armcpu] %08x given back is not a block of the heap\n", a);
		return;
	}
	FreeBlock b = { a - 8, Word(a - 8) };
	SetWord(a - 4, 0);
	size_t i = 0;
	while (i < fFree.size() && fFree[i].fAddr < b.fAddr)
		i++;
	// run together with the block after and the block before
	if (i < fFree.size() && b.fAddr + b.fSize == fFree[i].fAddr)
	{
		b.fSize += fFree[i].fSize;
		for (size_t j = i + 1; j < fFree.size(); j++)
			fFree[j - 1] = fFree[j];
		fFree.pop_back();
	}
	if (i > 0 && fFree[i - 1].fAddr + fFree[i - 1].fSize == b.fAddr)
	{
		fFree[i - 1].fSize += b.fSize;
		return;
	}
	fFree.push_back(b);
	for (size_t j = fFree.size() - 1; j > i; j--)
		fFree[j] = fFree[j - 1];
	fFree[i] = b;
}


class TNativeWorld;
typedef bool (*GlueFn)(TNativeWorld& w, TARMCPU& cpu);

static Boolean	gTrace = false;

// A window onto host data: the bytes of an object (data), or its slots
// seen as 32-bit refs, read and written through (slots).
struct Window
{
	uint32_t	fBase;
	uint32_t	fSize;
	RefStruct*	fObject;
	bool		fSlots;
	bool		fHostOrder;		// a string: its UniChars in the host's order (else the bytes as they lie, words big-endian)
};

// LockedBinaryPtr's windows: a binary's bytes seen from the ARM world for as
// long as the ARM code keeps it locked - across calls, since a locked
// binary's pointer is the ARM code's to keep until it unlocks it.  (A call's
// own windows, BinaryData's, go with the call.)
const uint32_t	kLockedBase		= 0x58000000;
const uint32_t	kLockedLimit	= 0x60000000;
struct LockedWindow { Window fWindow; long fLocks; };
static Vec<LockedWindow>	gLockedWindows;
static uint32_t			gLockedTop = kLockedBase;

static uint32_t
LockBinaryWindow(RefArg obj)
{
	for (LockedWindow& l : gLockedWindows)
		if (EQRef(*l.fWindow.fObject, obj))
		{
			l.fLocks++;
			return l.fWindow.fBase;
		}
	uint32_t size = (uint32_t) Length(obj);
	uint32_t span = ((size + 0xfff) & ~0xfffu) + 0x1000;
	if (span > kLockedLimit - gLockedTop)
		gLockedTop = kLockedBase;		// (the addresses come round again: the oldest are long unlocked)
	LockedWindow l;
	l.fWindow.fBase = gLockedTop;
	l.fWindow.fSize = size;
	l.fWindow.fObject = new RefStruct(obj);
	l.fWindow.fSlots = false;
	l.fWindow.fHostOrder = IsString(obj);
	l.fLocks = 1;
	gLockedTop += span;
	gLockedWindows.push_back(l);
	return l.fWindow.fBase;
}

// an unlock of a binary LockedBinaryPtr windowed: the window goes with its
// last lock
static void
UnlockBinaryWindow(Ref obj)
{
	for (size_t i = 0; i < gLockedWindows.size(); i++)
		if (EQRef(*gLockedWindows[i].fWindow.fObject, obj))
		{
			if (--gLockedWindows[i].fLocks > 0)
				return;
			delete gLockedWindows[i].fWindow.fObject;
			for (size_t j = i + 1; j < gLockedWindows.size(); j++)
				gLockedWindows[j - 1] = gLockedWindows[j];
			gLockedWindows.pop_back();
			return;
		}
}

class TNativeWorld : public ARMMemory
{
public:
					TNativeWorld(RefArg code);
					~TNativeWorld();

	// memory
	bool			Read32(uint32_t a, uint32_t* v) override;
	bool			Read16(uint32_t a, uint16_t* v) override;
	bool			Read8(uint32_t a, uint8_t* v) override;
	bool			Write32(uint32_t a, uint32_t v) override;
	bool			Write16(uint32_t a, uint16_t v) override;
	bool			Write8(uint32_t a, uint8_t v) override;
	bool			IsTrap(uint32_t pc) override;
	bool			Trap(TARMCPU* cpu, uint32_t pc) override;

	// refs
	uint32_t		ToARM(Ref ref);
	Ref				ToHost(uint32_t ref);
	Ref				ArgRef(uint32_t refVar);		// a RefVar const& from the ARM world
	uint32_t		NewRefHandle(uint32_t ref);		// ==> its ARM address
	void			DisposeRefHandle(uint32_t handle);
	uint32_t		NewRefVar(Ref ref);				// a RefHandle and a word pointing at it
	uint32_t		MapData(RefArg obj);			// BinaryData
	uint32_t		MapSlots(RefArg obj);			// Slots
	Ref				CodeObject(uint32_t addr);		// an object in the code binary, as a host object
	static const Ref	kNoObject = (Ref) -1;
	struct CodeObjectEntry { uint32_t fAddr; RefStruct* fObject; };
	Vec<CodeObjectEntry>	fCodeObjects;			// translated once each call

	// the arguments of a call out, and its answer
	uint32_t		Arg(TARMCPU& cpu, int i);
	void			Return(TARMCPU& cpu, uint32_t value) { cpu.r[0] = value; cpu.r[15] = cpu.r[14]; }

	// the ARM code's exception handlers (ExceptionHandler records in its
	// own frames: the CatchHeader, a jmp_buf of 0x58 bytes, the Exception)
	void			AddHandler(uint32_t handler)	{ fHandlers.push_back(handler); }
	void			ExitHandler(uint32_t handler);
	bool			Deliver(TARMCPU& cpu, const char* name, uint32_t data);	// ==> false: no ARM handler
	bool			DeliverHost(TARMCPU& cpu, Exception* e);		// a host exception, its data translated
	void			ThrowToHost(const char* name, uint32_t data);	// an ARM throw no ARM handler takes
	uint32_t		CString(const char* s);			// a copy in the arena
	uint32_t		Alloc(uint32_t size);			// a block of the arena
	void			Free(uint32_t a);				// (only the last block goes back)
	bool			ReadCString(uint32_t a, char* buffer, size_t size);
	uint32_t		StackStateToken(StackState* state);
	StackState*		StackStateOf(uint32_t token);

	// host functions handed to the ARM code as addresses to call
	uint32_t		HostCallback(void* fn, long numArgs);
	uint32_t		FunctionCallback(RefArg fn, long numArgs);	// a native in another code binary
	bool			CallFunction(TARMCPU& cpu, RefArg fn, long numArgs);
	bool			CallHost(TARMCPU& cpu, void* fn, long numArgs);

	const char*		fStoppedIn;				// the entry point that stopped the CPU
	struct Callback { void* fFn; long fNumArgs; RefStruct* fFunction; };
	Vec<Callback>	fCallbacks;
	Vec<uint32_t>	fHandlers;				// innermost last
	Vec<StackState*>	fStackStates;
	char			fThrownName[64];		// the exception being delivered, while it is
	Vec<uint8_t>	fCode;
	RefStruct*		fCodeRef;				// the code binary (fCode is its bytes, relocated)
	void			RelocateCode(RefArg code);
	bool			IsThisCode(RefArg code);
	Vec<uint8_t>	fArena;
	uint32_t		fArenaTop;				// the bump allocator's next free byte (from kArenaBase)
	Vec<uint32_t>	fFreeHandles;
	Vec<RefStruct*>	fHandles;
	Vec<uint32_t>	fHandleIndex;		// open addressing over fHandles: 0 empty
	Vec<Window>		fWindows;
	uint32_t		fWindowTop;

private:
	const uint8_t*	fROM;
	ULong			fROMSize;
	// with no image (booted on the object file): the ROM data it carries -
	// the parameter block whose version words the stubs read (romsrc/romdata/)
	const uint8_t*	ROMData(uint32_t a, uint32_t n) const
					{ return fROM == nil && a < kCodeBase ? (const uint8_t*) ROMBytesAt(a, n) : nil; }
	bool			Arena(uint32_t a, uint32_t n) const { return a >= kArenaBase && a + n <= kArenaBase + kArenaSize; }
	bool			Code(uint32_t a, uint32_t n) const { return a >= kCodeBase && a + n <= kCodeBase + fCode.size(); }
	Window*			FindWindow(uint32_t a, uint32_t n);
};

// the entry points answered, by public jump table offset / 4
struct GlueEntry { const char* fName; GlueFn fFn; };
static GlueEntry*	gGlue = nil;
static const uint32_t	kGlueSlots = kPublicJumpTableSize / 4 + 1;
static void		InitGlue(void);


TNativeWorld::TNativeWorld(RefArg code)
	: fStoppedIn(nil), fArena(kArenaSize, 0), fArenaTop(0), fHandleIndex(1024, 0), fWindowTop(kWindowBase)
{
	long length = Length(code);
	fCode.resize(length, 0);
	memcpy(fCode.data(), BinaryData(code), length);
	fCodeRef = new RefStruct(code);
	RelocateCode(code);
	fROM = (const uint8_t*) ROMImageBase(&fROMSize);
	if (fROM == nil)
		fROMSize = 0;
	fHandles.push_back(nil);			// (index 0 unused: kHandleBase + 1 is not a ref)
}


TNativeWorld::~TNativeWorld()
{
	for (CodeObjectEntry& e : fCodeObjects)
		delete e.fObject;
	delete fCodeRef;
	for (Callback& c : fCallbacks)
		delete c.fFunction;
	for (RefStruct* r : fHandles)
		delete r;
	for (Window& w : fWindows)
	{
		if (!w.fSlots)
			UnlockRef(*w.fObject);
		delete w.fObject;
	}
}


static inline uint32_t	BE32(const uint8_t* p)	{ return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3]; }
static inline void		PutBE32(uint8_t* p, uint32_t v)	{ p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v; }


// The code binary's C relocation, as the ROM applies it when it maps the
// package's pages (TSimpleCRelocator::Relocate 0x0004a148: each word the
// package's relocation chunk names moved by where the package is less the
// address it was linked at).  The Newton C++ Tools link a package at 0, so
// a relocated word is an offset in the package - here the code's own
// constant data, lying in its binary.  DEVIATION: the host maps packages
// unrelocated (packages/StorePackages.cpp's RelocatePage), and the ARM
// world maps only the code binary, at kCodeBase: the package is taken to
// be where that puts it, and the words inside the binary are relocated to
// match.  A binary that is no imported package part's (made at run time)
// has nothing to relocate.
void
TNativeWorld::RelocateCode(RefArg code)
{
	ULong32 address = 0;
	const uint8_t* object = FramesPartObjectSource(code, &address);
	if (object == nil || address < kPackageHeaderSize)
		return;
	const uint8_t* package = object - address;
	if (memcmp(package, "package", 7) != 0 || (BE32(package + 0x0c) & 0x04000000) == 0)
		return;
	const uint8_t* chunk = package + BE32(package + 0x2c);		// (after the directory)
	uint32_t chunkSize = BE32(chunk + 4);
	uint32_t pageSize = BE32(chunk + 8);
	uint32_t linkBase = BE32(chunk + 16);
	if (BE32(chunk) != 0 || chunkSize < 20 || pageSize == 0)
		return;
	uint32_t dataStart = address + 12;			// (the header, the GC's word and the class)
	uint32_t dataEnd = dataStart + (uint32_t) fCode.size();
	uint32_t delta = (kCodeBase - dataStart) - linkBase;
	const uint8_t* p = chunk + 20;
	const uint8_t* end = chunk + chunkSize;
	while (p + 4 <= end)
	{
		uint32_t page = (uint32_t) ((p[0] << 8) | p[1]);
		uint32_t count = (uint32_t) ((p[2] << 8) | p[3]);
		for (uint32_t i = 0; i < count && p + 4 + i < end; i++)
		{
			uint32_t at = page * pageSize + p[4 + i] * 4;
			if (at >= dataStart && at + 4 <= dataEnd)
			{
				uint8_t* word = &fCode[at - dataStart];
				PutBE32(word, BE32(word) + delta);
			}
		}
		p += 4 + ((count + 3) & ~3u);
	}
}


// Whether a code binary is this world's (its bytes, unrelocated, the same
// as this world's binary's).
bool
TNativeWorld::IsThisCode(RefArg code)
{
	if (EQRef(code, *fCodeRef))
		return true;
	return Length(code) == Length(*fCodeRef) && memcmp(BinaryData(code), BinaryData(*fCodeRef), (size_t) Length(code)) == 0;
}


Window*
TNativeWorld::FindWindow(uint32_t a, uint32_t n)
{
	for (Window& w : fWindows)
		if (a >= w.fBase && a + n <= w.fBase + w.fSize)
			return &w;
	if (a >= kLockedBase && a < kLockedLimit)
		for (LockedWindow& l : gLockedWindows)
			if (a >= l.fWindow.fBase && a + n <= l.fWindow.fBase + l.fWindow.fSize)
				return &l.fWindow;
	return nil;
}


bool
TNativeWorld::Read32(uint32_t a, uint32_t* v)
{
	if (a + 4 <= fROMSize)				{ *v = BE32(fROM + a); return true; }
	if (const uint8_t* p = ROMData(a, 4))	{ *v = BE32(p); return true; }
	if (Code(a, 4))						{ *v = BE32(&fCode[a - kCodeBase]); return true; }
	if (Arena(a, 4))					{ *v = BE32(&fArena[a - kArenaBase]); return true; }
	if (gARMHeap.Contains(a, 4))		{ *v = BE32(gARMHeap.At(a)); return true; }
	if (Window* w = FindWindow(a, 4))
	{
		if (w->fSlots)
		{
			// a slot, as the ARM code sees it: a 32-bit ref
			*v = ToARM(Slots(*w->fObject)[(a - w->fBase) >> 2]);
			return true;
		}
		// object data: a string's in the host's own byte order, anything
		// else's as the bytes lie (docs/armcpu/README.md)
		const uint8_t* p = (const uint8_t*) BinaryData(*w->fObject) + (a - w->fBase);
		if (w->fHostOrder)
			memcpy(v, p, 4);
		else
			*v = BE32(p);
		return true;
	}
	return false;
}


bool
TNativeWorld::Read16(uint32_t a, uint16_t* v)
{
	if (a + 2 <= fROMSize)				{ *v = (uint16_t) ((fROM[a] << 8) | fROM[a + 1]); return true; }
	if (const uint8_t* p = ROMData(a, 2))	{ *v = (uint16_t) ((p[0] << 8) | p[1]); return true; }
	if (Code(a, 2))						{ *v = (uint16_t) ((fCode[a - kCodeBase] << 8) | fCode[a - kCodeBase + 1]); return true; }
	if (Arena(a, 2))					{ *v = (uint16_t) ((fArena[a - kArenaBase] << 8) | fArena[a - kArenaBase + 1]); return true; }
	if (gARMHeap.Contains(a, 2))		{ const uint8_t* p = gARMHeap.At(a); *v = (uint16_t) ((p[0] << 8) | p[1]); return true; }
	if (Window* w = FindWindow(a, 2))
	{
		if (w->fSlots)
			return false;
		const uint8_t* p = (const uint8_t*) BinaryData(*w->fObject) + (a - w->fBase);
		if (w->fHostOrder)
			memcpy(v, p, 2);
		else
			*v = (uint16_t) ((p[0] << 8) | p[1]);
		return true;
	}
	return false;
}


bool
TNativeWorld::Read8(uint32_t a, uint8_t* v)
{
	if (a < fROMSize)					{ *v = fROM[a]; return true; }
	if (const uint8_t* p = ROMData(a, 1))	{ *v = *p; return true; }
	if (Code(a, 1))						{ *v = fCode[a - kCodeBase]; return true; }
	if (Arena(a, 1))					{ *v = fArena[a - kArenaBase]; return true; }
	if (gARMHeap.Contains(a, 1))		{ *v = *gARMHeap.At(a); return true; }
	if (Window* w = FindWindow(a, 1))
	{
		if (w->fSlots)
			return false;
		*v = ((uint8_t*) BinaryData(*w->fObject))[a - w->fBase];
		return true;
	}
	return false;
}


bool
TNativeWorld::Write32(uint32_t a, uint32_t v)
{
	if (Code(a, 4))						{ PutBE32(&fCode[a - kCodeBase], v); return true; }
	if (Arena(a, 4))					{ PutBE32(&fArena[a - kArenaBase], v); return true; }
	if (gARMHeap.Contains(a, 4))		{ PutBE32(gARMHeap.At(a), v); return true; }
	if (Window* w = FindWindow(a, 4))
	{
		if (w->fSlots)
		{
			SetArraySlotRef(*w->fObject, (a - w->fBase) >> 2, ToHost(v));
			return true;
		}
		uint8_t* p = (uint8_t*) BinaryData(*w->fObject) + (a - w->fBase);
		if (w->fHostOrder)
			memcpy(p, &v, 4);
		else
			PutBE32(p, v);
		return true;
	}
	return false;
}


bool
TNativeWorld::Write16(uint32_t a, uint16_t v)
{
	if (Code(a, 2))						{ fCode[a - kCodeBase] = (uint8_t) (v >> 8); fCode[a - kCodeBase + 1] = (uint8_t) v; return true; }
	if (Arena(a, 2))					{ fArena[a - kArenaBase] = (uint8_t) (v >> 8); fArena[a - kArenaBase + 1] = (uint8_t) v; return true; }
	if (gARMHeap.Contains(a, 2))		{ uint8_t* p = gARMHeap.At(a); p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; return true; }
	if (Window* w = FindWindow(a, 2))
	{
		if (w->fSlots)
			return false;
		uint8_t* p = (uint8_t*) BinaryData(*w->fObject) + (a - w->fBase);
		if (w->fHostOrder)
			memcpy(p, &v, 2);
		else
		{
			p[0] = (uint8_t) (v >> 8);
			p[1] = (uint8_t) v;
		}
		return true;
	}
	return false;
}


bool
TNativeWorld::Write8(uint32_t a, uint8_t v)
{
	if (Code(a, 1))						{ fCode[a - kCodeBase] = v; return true; }
	if (Arena(a, 1))					{ fArena[a - kArenaBase] = v; return true; }
	if (gARMHeap.Contains(a, 1))		{ *gARMHeap.At(a) = v; return true; }
	if (Window* w = FindWindow(a, 1))
	{
		if (w->fSlots)
			return false;
		((uint8_t*) BinaryData(*w->fObject))[a - w->fBase] = v;
		return true;
	}
	return false;
}


/*------------------------------------------------------------------------------
	R e f s
------------------------------------------------------------------------------*/

uint32_t
TNativeWorld::ToARM(Ref ref)
{
	if (!ISPTR(ref))
		return (uint32_t) ref;		// (integers, characters, nil, true, magic pointers: the same bits)
	// the same object gets the same handle while the call lasts
	size_t mask = fHandleIndex.size() - 1;
	size_t slot = ((uintptr_t) ref >> 3) & mask;
	for ( ; fHandleIndex[slot] != 0; slot = (slot + 1) & mask)
		if (*fHandles[fHandleIndex[slot]] == ref)
			return kHandleBase + fHandleIndex[slot] * 8 + 1;
	uint32_t index = (uint32_t) fHandles.size();
	fHandles.push_back(new RefStruct(ref));
	fHandleIndex[slot] = index;
	if (fHandles.size() * 2 > fHandleIndex.size())
	{
		// grown, and every handle entered again
		Vec<uint32_t> bigger(fHandleIndex.size() * 2, 0);
		size_t m = bigger.size() - 1;
		for (uint32_t i = 1; i < fHandles.size(); i++)
		{
			size_t k = ((uintptr_t) (Ref) *fHandles[i] >> 3) & m;
			while (bigger[k] != 0)
				k = (k + 1) & m;
			bigger[k] = i;
		}
		fHandleIndex.swap(bigger);
	}
	return kHandleBase + index * 8 + 1;
}


Ref
TNativeWorld::ToHost(uint32_t ref)
{
	if ((ref & 3) != 1)
		return (Ref) (int32_t) ref;		// (an integer is signed)
	uint32_t addr = ref - 1;
	if (addr >= kHandleBase && addr <= kHandleLimit)
	{
		uint32_t index = (addr - kHandleBase) / 8;
		if (index > 0 && index < fHandles.size())
			return *fHandles[index];
	}
	if (Code(addr, 12))
	{
		Ref obj = CodeObject(addr);
		if (obj != kNoObject)
			return obj;
	}
	else
		fprintf(stderr, "[armcpu] %08x is not a ref of this call\n", ref);
	ThrowMsg("armcpu: a ref the ARM world does not know");
	return NILREF;
}


// An object in the code binary - the ROM's layout: a header word (the size
// in bytes << 8 | the flags: 1 slotted, 2 a frame), the GC's word, the class
// (or a frame's map), then the data or the slots - as a host object, made
// once each call: a symbol interned; a binary copied (a string's big-endian
// UniChars in the host's order); an array with its slots translated.
// NOT YET: frames (their maps).  NTK puts only four symbols there
// (_proto, CFunction, binCFunction, string), and those in every fixture's
// code; a function's own literals come to it through its closure.
Ref
TNativeWorld::CodeObject(uint32_t addr)
{
	for (CodeObjectEntry& e : fCodeObjects)
		if (e.fAddr == addr)
			return *e.fObject;
	const uint8_t* p = &fCode[addr - kCodeBase];
	uint32_t header = BE32(p);
	uint32_t size = header >> 8;
	uint32_t flags = header & 0xff;
	uint32_t cls = BE32(p + 8);
	if (size < 12 || !Code(addr, size))
	{
		fprintf(stderr, "[armcpu] an object at +%#x of the code binary runs off its end\n", addr - kCodeBase);
		return kNoObject;
	}
	RefVar obj;
	if (cls == 0x00055552)
		obj = Intern((char*) (p + 16));
	else if ((flags & 1) == 0)
	{
		RefVar theClass(ToHost(cls));
		uint32_t length = size - 12;
		obj = AllocateBinary(theClass, length);
		char* data = (char*) BinaryData(obj);
		if (IsSubclassRef(theClass, RSSYMstring))
			for (uint32_t i = 0; i + 1 < length; i += 2)
				*(UniChar*) (data + i) = (UniChar) ((p[12 + i] << 8) | p[12 + i + 1]);
		else
			memcpy(data, p + 12, length);
	}
	else if ((flags & 2) == 0)
	{
		uint32_t count = (size - 12) / 4;
		obj = AllocateArray(RefVar(ToHost(cls)), count);
		for (uint32_t i = 0; i < count; i++)
			SetArraySlot(obj, i, RefVar(ToHost(BE32(p + 12 + i * 4))));
	}
	else
	{
		fprintf(stderr, "[armcpu] a frame at +%#x of the code binary is not translated (NOT YET)\n", addr - kCodeBase);
		return kNoObject;
	}
	CodeObjectEntry e = { addr, new RefStruct(obj) };
	fCodeObjects.push_back(e);
	return obj;
}


Ref
TNativeWorld::ArgRef(uint32_t refVar)
{
	uint32_t handle, ref;
	if (!Read32(refVar, &handle) || !Read32(handle, &ref))
	{
		fprintf(stderr, "[armcpu] %08x is not a RefVar\n", refVar);
		ThrowMsg("armcpu: a bad RefVar");
	}
	return ToHost(ref);
}


uint32_t
TNativeWorld::NewRefHandle(uint32_t ref)
{
	uint32_t h;
	if (!fFreeHandles.empty())
	{
		h = fFreeHandles.back();
		fFreeHandles.pop_back();
	}
	else
	{
		if (fArenaTop + 8 > kArenaSize - kStackSize)
			ThrowMsg("armcpu: the arena is full");
		h = kArenaBase + fArenaTop;
		fArenaTop += 8;
	}
	Write32(h, ref);
	Write32(h + 4, 0);
	return h;
}


void
TNativeWorld::DisposeRefHandle(uint32_t handle)
{
	if (Arena(handle, 8))
		fFreeHandles.push_back(handle);
}


uint32_t
TNativeWorld::NewRefVar(Ref ref)
{
	uint32_t h = NewRefHandle(ToARM(ref));
	uint32_t var = NewRefHandle(h);		// (a word pointing at the handle; the second word unused)
	return var;
}


uint32_t
TNativeWorld::MapData(RefArg obj)
{
	for (Window& w : fWindows)
		if (!w.fSlots && EQRef(*w.fObject, obj))
			return w.fBase;
	Window w;
	w.fBase = fWindowTop;
	w.fSize = (uint32_t) Length(obj);
	w.fObject = new RefStruct(obj);
	w.fSlots = false;
	w.fHostOrder = IsString(obj);
	LockRef(obj);
	fWindows.push_back(w);
	fWindowTop += (w.fSize + 0xfff) & ~0xfffu;
	fWindowTop += 0x1000;
	return w.fBase;
}


uint32_t
TNativeWorld::MapSlots(RefArg obj)
{
	for (Window& w : fWindows)
		if (w.fSlots && EQRef(*w.fObject, obj))
			return w.fBase;
	Window w;
	w.fBase = fWindowTop;
	w.fSize = (uint32_t) Length(obj) * 4;
	w.fObject = new RefStruct(obj);
	w.fSlots = true;
	w.fHostOrder = false;
	fWindows.push_back(w);
	fWindowTop += (w.fSize + 0xfff) & ~0xfffu;
	fWindowTop += 0x1000;
	return w.fBase;
}


uint32_t
TNativeWorld::Arg(TARMCPU& cpu, int i)
{
	if (i < 4)
		return cpu.r[i];
	uint32_t v = 0;
	Read32(cpu.r[13] + (i - 4) * 4, &v);
	return v;
}


/*------------------------------------------------------------------------------
	E x c e p t i o n s
	NTK's code keeps its handlers as the ROM does, in ExceptionHandler records
	on its own stack; the adapter keeps the chain.  A throw - from the ARM
	code, or out of a host function it called - goes to the innermost ARM
	handler (its Exception filled in, and a longjmp to its jmp_buf), or,
	with none, on out to the host.
------------------------------------------------------------------------------*/

void
TNativeWorld::ExitHandler(uint32_t handler)
{
	while (!fHandlers.empty())
	{
		uint32_t h = fHandlers.back();
		fHandlers.pop_back();
		if (h == handler)
			break;
	}
}


uint32_t
TNativeWorld::CString(const char* s)
{
	size_t n = strlen(s) + 1;
	uint32_t size = (uint32_t) ((n + 7) & ~7u);
	if (fArenaTop + size > kArenaSize - kStackSize)
		ThrowMsg("armcpu: the arena is full");
	uint32_t a = kArenaBase + fArenaTop;
	fArenaTop += size;
	memcpy(&fArena[a - kArenaBase], s, n);
	return a;
}


uint32_t
TNativeWorld::Alloc(uint32_t n)
{
	uint32_t size = (n + 7) & ~7u;
	if (size < n || fArenaTop + size > kArenaSize - kStackSize)
		ThrowMsg("armcpu: the arena is full");
	uint32_t a = kArenaBase + fArenaTop;
	fArenaTop += size;
	memset(&fArena[a - kArenaBase], 0, size);
	return a;
}


void
TNativeWorld::Free(uint32_t a)
{
	if (a == 0 || !Arena(a - 8, 8))
		return;
	uint32_t size = 0;
	Read32(a - 8, &size);
	if (a - 8 + ((size + 8 + 7) & ~7u) == kArenaBase + fArenaTop)
		fArenaTop = a - 8 - kArenaBase;
}


bool
TNativeWorld::ReadCString(uint32_t a, char* buffer, size_t size)
{
	for (size_t i = 0; i < size; i++)
	{
		uint8_t c;
		if (!Read8(a + (uint32_t) i, &c))
			return false;
		buffer[i] = (char) c;
		if (c == 0)
			return true;
	}
	buffer[size - 1] = 0;
	return true;
}


// the ARM setjmp's jmp_buf: r4-r11, sp and lr (the ROM's is 0x58 bytes, of
// which this uses 40; only this pair of functions reads it)
static void
ARMLongJump(TNativeWorld& w, TARMCPU& cpu, uint32_t buf, uint32_t value)
{
	for (int i = 0; i < 8; i++)
		w.Read32(buf + i * 4, &cpu.r[4 + i]);
	w.Read32(buf + 32, &cpu.r[13]);
	uint32_t lr = 0;
	w.Read32(buf + 36, &lr);
	cpu.r[0] = value != 0 ? value : 1;
	cpu.r[15] = lr;
}


bool
TNativeWorld::Deliver(TARMCPU& cpu, const char* name, uint32_t data)
{
	if (fHandlers.empty())
		return false;
	uint32_t h = fHandlers.back();
	Write32(h + kARMHandlerException, CString(name));
	Write32(h + kARMHandlerException + 4, data);
	Write32(h + kARMHandlerException + 8, 0);
	// (the handler is taken off as the ROM's Throw takes it: the catch
	//  clause's ExitHandler or NextHandler finds it gone)
	fHandlers.pop_back();
	ARMLongJump(*this, cpu, h + kARMHandlerState, 1);
	return true;
}


// A host exception into the ARM world: a ref exception's data (a RefStruct*)
// becomes a RefVar of the ARM world's, a message exception's (a C string) a
// copy in the arena, anything else (an error number) the number.
bool
TNativeWorld::DeliverHost(TARMCPU& cpu, Exception* e)
{
	if (fHandlers.empty())
		return false;
	strncpy(fThrownName, e->name, sizeof(fThrownName) - 1);
	fThrownName[sizeof(fThrownName) - 1] = 0;
	uint32_t data;
	if (Subexception(e->name, (ExceptionName) "type.ref") && e->data != nil)
		data = NewRefVar(*(RefStruct*) e->data);
	else if (Subexception(e->name, exMsgException) && e->data != nil)
		data = CString((const char*) e->data);
	else
		data = (uint32_t) (uintptr_t) e->data;
	return Deliver(cpu, fThrownName, data);
}


// The other way: an exception the ARM code throws (or passes on) that no
// handler of its takes goes on out to the host, its data translated back.
void
TNativeWorld::ThrowToHost(const char* name, uint32_t data)
{
	strncpy(fThrownName, name, sizeof(fThrownName) - 1);
	fThrownName[sizeof(fThrownName) - 1] = 0;
	if (Subexception((ExceptionName) fThrownName, (ExceptionName) "type.ref") && data != 0)
		ThrowRefException((ExceptionName) fThrownName, RefVar(ArgRef(data)));
	if (Subexception((ExceptionName) fThrownName, exMsgException) && data != 0)
	{
		static char message[256];
		ReadCString(data, message, sizeof(message));
		Throw((ExceptionName) fThrownName, message, nil);
	}
	Throw((ExceptionName) fThrownName, (void*) (intptr_t) (int32_t) data, nil);
}


uint32_t
TNativeWorld::StackStateToken(StackState* state)
{
	fStackStates.push_back(state);
	return kStackStates + (uint32_t) (fStackStates.size() - 1) * 16;
}


StackState*
TNativeWorld::StackStateOf(uint32_t token)
{
	uint32_t i = (token - kStackStates) / 16;
	return i < fStackStates.size() ? fStackStates[i] : nil;
}


/*------------------------------------------------------------------------------
	C a l l s   o u t
------------------------------------------------------------------------------*/

bool
TNativeWorld::IsTrap(uint32_t pc)
{
	return (pc >= kPublicJumpTableBase && pc < kPublicJumpTableBase + kPublicJumpTableSize)
		|| (pc >= kCallbacks && pc < kCallbacks + fCallbacks.size() * 4);
}


uint32_t
TNativeWorld::HostCallback(void* fn, long numArgs)
{
	for (uint32_t i = 0; i < fCallbacks.size(); i++)
		if (fCallbacks[i].fFn == fn)
			return kCallbacks + i * 4;
	Callback c = { fn, numArgs, nil };
	fCallbacks.push_back(c);
	return kCallbacks + (uint32_t) (fCallbacks.size() - 1) * 4;
}


// A native function of another code binary (another package's, or another
// part's): called from here as the interpreter calls one - its host
// re-expression if it has one, else a world of its own on another CPU.  The
// ARM code is handed a callback address; the function's closure, if it has
// one, comes as the last argument, as the interpreter passes it.
uint32_t
TNativeWorld::FunctionCallback(RefArg fn, long numArgs)
{
	for (uint32_t i = 0; i < fCallbacks.size(); i++)
		if (fCallbacks[i].fFunction != nil && EQRef(*fCallbacks[i].fFunction, fn) && fCallbacks[i].fNumArgs == numArgs)
			return kCallbacks + i * 4;
	Callback c = { nil, numArgs, new RefStruct(fn) };
	fCallbacks.push_back(c);
	return kCallbacks + (uint32_t) (fCallbacks.size() - 1) * 4;
}


bool
TNativeWorld::CallFunction(TARMCPU& cpu, RefArg fn, long numArgs)
{
	if (numArgs > 15)
		ThrowMsg("armcpu: too many arguments");
	uint32_t rcvrVar = Arg(cpu, 0);
	RefVar rcvr(rcvrVar != 0 ? ArgRef(rcvrVar) : NILREF);
	RefVar a[15];
	const RefVar* args[15];
	for (long i = 0; i < numArgs; i++)
	{
		a[i] = ArgRef(Arg(cpu, (int) i + 1));
		args[i] = &a[i];
	}
	RefVar code(GetArraySlotRef(fn, 1));
	ULong offset = (ULong) RINT(GetArraySlotRef(fn, 4));
	long boundArgs = 0;
	PackageNativeKey key;
	void* host = FindPackageNative(code, offset, &boundArgs, &key);
	Ref result;
	if (host != nil)
	{
		if (boundArgs != numArgs)
			Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
		return CallHost(cpu, host, numArgs);
	}
	else
		result = RunPackageNativeOnCPU(code, offset, rcvr, numArgs, args);
	Return(cpu, ToARM(result));
	return true;
}


// A host native called from the ARM code: the receiver and the arguments
// by reference, as the ROM calls any native function.
bool
TNativeWorld::CallHost(TARMCPU& cpu, void* fn, long numArgs)
{
	Callback c = { fn, numArgs, nil };
	if (c.fNumArgs > 6)
		ThrowMsg("armcpu: a host native of more than six arguments");
	// (NTK passes nought for the receiver of a function that has none - the
	//  frequently called ones, FAref and the like)
	uint32_t rcvrVar = Arg(cpu, 0);
	RefVar rcvr(rcvrVar != 0 ? ArgRef(rcvrVar) : NILREF);
	RefVar a[6];
	for (long i = 0; i < c.fNumArgs; i++)
		a[i] = ArgRef(Arg(cpu, (int) i + 1));
	Ref result = NILREF;
	switch (c.fNumArgs)
	{
	case 0:	result = ((NativeFn0) c.fFn)(rcvr); break;
	case 1:	result = ((NativeFn1) c.fFn)(rcvr, a[0]); break;
	case 2:	result = ((NativeFn2) c.fFn)(rcvr, a[0], a[1]); break;
	case 3:	result = ((NativeFn3) c.fFn)(rcvr, a[0], a[1], a[2]); break;
	case 4:	result = ((NativeFn4) c.fFn)(rcvr, a[0], a[1], a[2], a[3]); break;
	case 5:	result = ((NativeFn5) c.fFn)(rcvr, a[0], a[1], a[2], a[3], a[4]); break;
	case 6:	result = ((NativeFn6) c.fFn)(rcvr, a[0], a[1], a[2], a[3], a[4], a[5]); break;
	}
	Return(cpu, ToARM(result));
	return true;
}


bool
TNativeWorld::Trap(TARMCPU* cpu, uint32_t pc)
{
	InitGlue();
	if (pc >= kCallbacks)
	{
		if (gTrace)
			fprintf(stderr, "[armcpu] host native %08x\n", pc);
		newton_try
		{
			Callback c = fCallbacks[(pc - kCallbacks) / 4];
			if (c.fFunction != nil)
				CallFunction(*cpu, RefVar(*c.fFunction), c.fNumArgs);
			else
				CallHost(*cpu, c.fFn, c.fNumArgs);
		}
		newton_catch_all
		{
			if (!DeliverHost(*cpu, CurrentException()))
				rethrow;
		}
		end_try;
		return true;
	}
	uint32_t offset = pc - kPublicJumpTableBase;
	GlueEntry* g = &gGlue[offset / 4];
	if ((offset & 3) != 0 || g->fFn == nil)
	{
		const char* name = "?";
		unsigned long slot = 0;
		for (unsigned long i = 0; i < kPublicJumpTableCount; i++)
			if (kPublicJumpTable[i].fOffset == offset)
			{
				name = kPublicJumpTable[i].fName;
				slot = kPublicJumpTable[i].fSlot;
			}
		// a native function of the ROM's that the host has (the ROM's
		// native function table, as a function object's funcPtr resolves):
		// called as natives are, the receiver and arguments by reference
		long numArgs = 0;
		void* host = (slot != 0 && (offset & 3) == 0) ? ResolveNativeFunction((ULong) slot, &numArgs) : nil;
		if (host != nil)
		{
			if (gTrace)
				fprintf(stderr, "[armcpu] %s (native, %ld args)\n", name, numArgs);
			newton_try
			{
				CallHost(*cpu, host, numArgs);
			}
			newton_catch_all
			{
				if (!DeliverHost(*cpu, CurrentException()))
					rethrow;
			}
			end_try;
			return true;
		}
		fStoppedIn = name;
		fprintf(stderr, "[armcpu] the ROM's %s (public jump table +%#x) is not answered (NOT YET)\n", name, offset);
		return false;
	}
	if (gTrace)
		fprintf(stderr, "[armcpu] %s(%08x, %08x, %08x, %08x)", g->fName, cpu->r[0], cpu->r[1], cpu->r[2], cpu->r[3]);
	bool ok = true;
	Exception* thrown = nil;
	newton_try
	{
		ok = g->fFn(*this, *cpu);
	}
	newton_catch_all
	{
		thrown = CurrentException();
		if (gTrace)
			fprintf(stderr, " -> threw %s\n", thrown->name);
		if (!DeliverHost(*cpu, thrown))
			rethrow;
	}
	end_try;
	if (thrown != nil)
		return true;
	if (gTrace)
		fprintf(stderr, " -> %08x\n", cpu->r[0]);
	return ok;
}


// the entry points, by the ROM's names (the adapter answers these)

#define GLUE(fn)	static bool fn(TNativeWorld& w, TARMCPU& cpu)

GLUE(Glue_AllocateRefHandle)	{ w.Return(cpu, w.NewRefHandle(cpu.r[0])); return true; }
GLUE(Glue_DisposeRefHandle)		{ w.DisposeRefHandle(cpu.r[0]); w.Return(cpu, 0); return true; }
GLUE(Glue_GetArraySlotRef)		{ w.Return(cpu, w.ToARM(GetArraySlotRef(w.ToHost(cpu.r[0]), (int32_t) cpu.r[1]))); return true; }
GLUE(Glue_SetArraySlotRef)		{ SetArraySlotRef(w.ToHost(cpu.r[0]), (int32_t) cpu.r[1], w.ToHost(cpu.r[2])); w.Return(cpu, 0); return true; }
GLUE(Glue_GetFrameSlotRef)		{ w.Return(cpu, w.ToARM(GetFrameSlotRef(w.ToHost(cpu.r[0]), w.ToHost(cpu.r[1])))); return true; }
GLUE(Glue_FrameHasSlotRef)		{ w.Return(cpu, FrameHasSlot(RefVar(w.ToHost(cpu.r[0])), RefVar(w.ToHost(cpu.r[1]))) ? 1 : 0); return true; }
GLUE(Glue_Length)				{ w.Return(cpu, (uint32_t) Length(w.ToHost(cpu.r[0]))); return true; }
GLUE(Glue_SetLength)			{ SetLength(RefVar(w.ArgRef(cpu.r[0])), (int32_t) cpu.r[1]); w.Return(cpu, 0); return true; }
GLUE(Glue_Clone)				{ w.Return(cpu, w.ToARM(Clone(RefVar(w.ArgRef(cpu.r[0]))))); return true; }
GLUE(Glue_GetFramePath)			{ w.Return(cpu, w.ToARM(GetFramePath(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))))); return true; }
GLUE(Glue_EQRef)				{ w.Return(cpu, EQRef(w.ToHost(cpu.r[0]), w.ToHost(cpu.r[1])) ? 1 : 0); return true; }
GLUE(Glue_ObjectFlags)			{ w.Return(cpu, (uint32_t) ObjectFlags(w.ToHost(cpu.r[0]))); return true; }
GLUE(Glue_Slots)				{ w.Return(cpu, w.MapSlots(RefVar(w.ToHost(cpu.r[0])))); return true; }
GLUE(Glue_BinaryData)			{ w.Return(cpu, w.MapData(RefVar(w.ToHost(cpu.r[0])))); return true; }
GLUE(Glue_RINTError)			{ w.Return(cpu, (uint32_t) _RINTError(w.ToHost(cpu.r[0]))); return true; }
GLUE(Glue_RCHARError)			{ w.Return(cpu, (uint32_t) _RCHARError(w.ToHost(cpu.r[0]))); return true; }
GLUE(Glue_ClassOf)				{ w.Return(cpu, w.ToARM(ClassOf(RefVar(w.ArgRef(cpu.r[0]))))); return true; }
GLUE(Glue_IsSymbol)				{ w.Return(cpu, IsSymbol(w.ToHost(cpu.r[0])) ? 1 : 0); return true; }
GLUE(Glue_AllocateFrame)		{ w.Return(cpu, w.ToARM(AllocateFrame())); return true; }
GLUE(Glue_AllocateFrameWithMap)	{ w.Return(cpu, w.ToARM(AllocateFrameWithMap(RefVar(w.ArgRef(cpu.r[0]))))); return true; }
GLUE(Glue_AllocateArray)		{ w.Return(cpu, w.ToARM(AllocateArray(RefVar(w.ArgRef(cpu.r[0])), (int32_t) cpu.r[1]))); return true; }
GLUE(Glue_AddArraySlot)			{ AddArraySlot(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))); w.Return(cpu, 0); return true; }
GLUE(Glue_SetFrameSlot)			{ SetFrameSlot(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), RefVar(w.ArgRef(cpu.r[2]))); w.Return(cpu, 0); return true; }
GLUE(Glue_MAKEBOOLEAN)			{ w.Return(cpu, cpu.r[0] != 0 ? (uint32_t) TRUEREF : (uint32_t) NILREF); return true; }
GLUE(Glue_LockRef)				{ LockRef(w.ToHost(cpu.r[0])); w.Return(cpu, 0); return true; }
GLUE(Glue_UnlockRef)			{ Ref obj = w.ToHost(cpu.r[0]); UnlockBinaryWindow(obj); UnlockRef(obj); w.Return(cpu, 0); return true; }

// the interpreter the code runs under: one opaque address for it
GLUE(Glue_GetGInterpreter)		{ w.Return(cpu, kInterpreter); return true; }
GLUE(Glue_IsSend)				{ w.Return(cpu, gInterpreter->IsSend() ? 1 : 0); return true; }
GLUE(Glue_SetCallEnv)			{ gInterpreter->SetCallEnv(); w.Return(cpu, 0); return true; }
GLUE(Glue_GetReceiver)			{ w.Return(cpu, w.ToARM(gInterpreter->GetReceiver())); return true; }
GLUE(Glue_GetImplementor)		{ w.Return(cpu, w.ToARM(gInterpreter->GetImplementor())); return true; }
GLUE(Glue_SetSendEnv)			{ gInterpreter->SetSendEnv(RefVar(w.ArgRef(cpu.r[1])), RefVar(w.ArgRef(cpu.r[2]))); w.Return(cpu, 0); return true; }
GLUE(Glue_PushValue)			{ gInterpreter->PushValue(RefVar(w.ArgRef(cpu.r[1]))); w.Return(cpu, 0); return true; }
GLUE(Glue_PopValue)				{ w.Return(cpu, w.ToARM(gInterpreter->PopValue())); return true; }

GLUE(Glue_setjmp)
{
	uint32_t buf = cpu.r[0];
	for (int i = 0; i < 8; i++)
		w.Write32(buf + i * 4, cpu.r[4 + i]);
	w.Write32(buf + 32, cpu.r[13]);
	w.Write32(buf + 36, cpu.r[14]);
	w.Return(cpu, 0);
	return true;
}
GLUE(Glue_longjmp)				{ ARMLongJump(w, cpu, cpu.r[0], cpu.r[1]); return true; }
GLUE(Glue_AddExceptionHandler)	{ w.AddHandler(cpu.r[0]); w.Return(cpu, 0); return true; }
GLUE(Glue_ExitHandler)			{ w.ExitHandler(cpu.r[0]); w.Return(cpu, 0); return true; }
GLUE(Glue_NextHandler)
{
	// the exception the handler caught passed on to the next one out
	uint32_t h = cpu.r[0];
	uint32_t nameAddr = 0, data = 0;
	w.Read32(h + kARMHandlerException, &nameAddr);
	w.Read32(h + kARMHandlerException + 4, &data);
	char name[64];
	if (!w.ReadCString(nameAddr, name, sizeof(name)))
		strcpy(name, "evt.ex");
	w.ExitHandler(h);
	if (w.Deliver(cpu, name, data))
		return true;
	w.ThrowToHost(name, data);
	return true;
}
GLUE(Glue_Throw)
{
	char name[64];
	if (!w.ReadCString(cpu.r[0], name, sizeof(name)))
		strcpy(name, "evt.ex");
	if (w.Deliver(cpu, name, cpu.r[1]))
		return true;
	w.ThrowToHost(name, cpu.r[1]);
	return true;
}
// (the ROM's makes a RefStruct of the ref and throws it; here that is a new
//  RefVar of the ARM world's, which a host catcher gets back as the ref)
GLUE(Glue_ThrowRefException)
{
	char name[64];
	if (!w.ReadCString(cpu.r[0], name, sizeof(name)))
		strcpy(name, "evt.ex");
	uint32_t data = w.NewRefVar(w.ArgRef(cpu.r[1]));
	if (w.Deliver(cpu, name, data))
		return true;
	w.ThrowToHost(name, data);
	return true;
}
GLUE(Glue_Subexception)
{
	char name[64], super[64];
	w.ReadCString(cpu.r[0], name, sizeof(name));
	w.ReadCString(cpu.r[1], super, sizeof(super));
	w.Return(cpu, Subexception((ExceptionName) name, (ExceptionName) super) ? 1 : 0);
	return true;
}
GLUE(Glue_GetStackStateBlock)	{ w.Return(cpu, w.StackStateToken(GetStackStateBlock())); return true; }
GLUE(Glue_ResetStackStateBlock)	{ if (StackState* st = w.StackStateOf(cpu.r[0])) ResetStack(*st); w.Return(cpu, 0); return true; }
GLUE(Glue_DisposeStackStateBlock)
{
	uint32_t i = (cpu.r[0] - kStackStates) / 16;
	if (i < w.fStackStates.size() && w.fStackStates[i] != nil)
	{
		DisposeStackStateBlock(w.fStackStates[i]);
		w.fStackStates[i] = nil;
	}
	w.Return(cpu, 0);
	return true;
}
GLUE(Glue_IncrementCurrentStackPos)	{ IncrementCurrentStackPos(); w.Return(cpu, 0); return true; }
GLUE(Glue_DecrementCurrentStackPos)	{ DecrementCurrentStackPos(); w.Return(cpu, 0); return true; }
// (the ARM world's handles are the call's own, given back with it)
GLUE(Glue_ClearRefHandles)		{ w.Return(cpu, 0); return true; }

GLUE(Glue_GetGFunctionFrame)	{ w.Return(cpu, w.ToARM(gFunctionFrame)); return true; }
// the interpreter: Call/Send push a frame for a function (true: a native,
// done already), Run runs it
GLUE(Glue_Call)					{ w.Return(cpu, gInterpreter->Call(RefVar(w.ArgRef(cpu.r[1])), (int32_t) cpu.r[2]) ? 1 : 0); return true; }
GLUE(Glue_Send)
{
	RefVar receiver(w.ArgRef(cpu.r[1]));
	RefVar implementor(w.ArgRef(cpu.r[2]));
	RefVar fn(w.ArgRef(cpu.r[3]));
	long numArgs = (int32_t) w.Arg(cpu, 4);
	w.Return(cpu, gInterpreter->Send(receiver, implementor, fn, numArgs) ? 1 : 0);
	return true;
}
GLUE(Glue_Run)					{ gInterpreter->Run(); w.Return(cpu, 0); return true; }
// SetupSend(receiver, message, ifDefined, RefVar& implementor): the method,
// with the implementor written back through the ARM code's RefVar
GLUE(Glue_SetupSend)
{
	RefVar implementor(w.ArgRef(cpu.r[3]));
	Ref fn = SetupSend(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), (int32_t) cpu.r[2], implementor);
	RefVar method(fn);
	uint32_t handle = 0;
	w.Read32(cpu.r[3], &handle);
	w.Write32(handle, w.ToARM(implementor));
	w.Return(cpu, w.ToARM(method));
	return true;
}
// TranslateException(interpreter, Exception*): the exception frame of an
// Exception in the ARM world (a catch clause's CurrentException() is the
// ARM address of its handler's Exception: the name a C string in the arena,
// a ref exception's data a RefVar of the ARM world's, a message's a C
// string) - made into a host Exception for the interpreter's own.
GLUE(Glue_TranslateException)
{
	uint32_t e = cpu.r[1];
	uint32_t nameAddr = 0, data = 0;
	w.Read32(e, &nameAddr);
	w.Read32(e + 4, &data);
	char name[64];
	if (!w.ReadCString(nameAddr, name, sizeof(name)))
		strcpy(name, "evt.ex");
	static char message[256];
	RefStruct ref;
	Exception x;
	x.name = (ExceptionName) name;
	x.destructor = nil;
	if (Subexception(x.name, exMsgException))
	{
		if (!w.ReadCString(data, message, sizeof(message)))
			message[0] = 0;
		x.data = message;
	}
	else if (Subexception(x.name, (ExceptionName) "type.ref"))
	{
		ref = data != 0 ? w.ArgRef(data) : NILREF;
		x.data = &ref;
	}
	else
		x.data = (void*) (intptr_t) (int32_t) data;
	w.Return(cpu, w.ToARM(gInterpreter->TranslateException(&x)));
	return true;
}
GLUE(Glue_StrEndsWith)
{
	w.Return(cpu, StrEndsWith(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))) ? 1 : 0);
	return true;
}
// SetupResend(message, ifDefined, RefVar& implementor), as SetupSend
GLUE(Glue_SetupResend)
{
	RefVar implementor(w.ArgRef(cpu.r[2]));
	RefVar method(SetupResend(RefVar(w.ArgRef(cpu.r[0])), (int32_t) cpu.r[1], implementor));
	uint32_t handle = 0;
	w.Read32(cpu.r[2], &handle);
	w.Write32(handle, w.ToARM(implementor));
	w.Return(cpu, w.ToARM(method));
	return true;
}
#define REF(n)		RefVar(w.ArgRef(cpu.r[n]))
#define LONG(n)		((long) (int32_t) cpu.r[n])
GLUE(Glue_ArrayMunger)		{ ArrayMunger(REF(0), LONG(1), LONG(2), REF(3), (int32_t) w.Arg(cpu, 4), (int32_t) w.Arg(cpu, 5)); w.Return(cpu, 0); return true; }
GLUE(Glue_BinaryMunger)		{ BinaryMunger(REF(0), LONG(1), LONG(2), REF(3), (int32_t) w.Arg(cpu, 4), (int32_t) w.Arg(cpu, 5)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrMunger)		{ StrMunger(REF(0), LONG(1), LONG(2), REF(3), (int32_t) w.Arg(cpu, 4), (int32_t) w.Arg(cpu, 5)); w.Return(cpu, 0); return true; }
GLUE(Glue_ArrayPosition)	{ w.Return(cpu, (uint32_t) ArrayPosition(REF(0), REF(1), LONG(2), REF(3))); return true; }
GLUE(Glue_EnsureInternal)	{ w.Return(cpu, w.ToARM(EnsureInternal(REF(0)))); return true; }
GLUE(Glue_ReplaceObjectRef)	{ ReplaceObjectRef(w.ToHost(cpu.r[0]), w.ToHost(cpu.r[1])); w.Return(cpu, 0); return true; }
GLUE(Glue_SortArray)		{ SortArray(REF(0), REF(1), REF(2)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrBeginsWith)	{ w.Return(cpu, StrBeginsWith(REF(0), REF(1)) ? 1 : 0); return true; }
GLUE(Glue_StrCapitalizeWords)	{ StrCapitalizeWords(REF(0)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrCapitalize)	{ StrCapitalize(REF(0)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrDowncase)		{ StrDowncase(REF(0)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrUpcase)		{ StrUpcase(REF(0)); w.Return(cpu, 0); return true; }
GLUE(Glue_StrPosition)		{ w.Return(cpu, (uint32_t) StrPosition(REF(0), REF(1), LONG(2))); return true; }
GLUE(Glue_StrReplace)		{ w.Return(cpu, (uint32_t) StrReplace(REF(0), REF(1), REF(2), LONG(3))); return true; }
GLUE(Glue_Substring)		{ w.Return(cpu, w.ToARM(Substring(REF(0), LONG(1), LONG(2)))); return true; }
GLUE(Glue_TrimString)		{ TrimString(REF(0)); w.Return(cpu, 0); return true; }
GLUE(Glue_PrintObject)		{ PrintObject(REF(0), LONG(1)); w.Return(cpu, 0); return true; }
// CoerceToDouble: Norcroft's software floating point answers a double in
// r0 (the high word) and r1
GLUE(Glue_CoerceToDouble)
{
	double d = CoerceToDouble(REF(0));
	uint64_t bits;
	memcpy(&bits, &d, 8);
	cpu.r[1] = (uint32_t) bits;
	w.Return(cpu, (uint32_t) (bits >> 32));
	return true;
}
#undef REF
#undef LONG

// The Unicode text functions over the ARM world's memory: the text copied
// out (a UniChar is a value there: Read16/Write16), worked on, written back.
static long
ReadUniChars(TNativeWorld& w, uint32_t a, UniChar* buffer, long max)
{
	long n = 0;
	uint16_t c = 0;
	while (n < max - 1 && w.Read16(a + (uint32_t) n * 2, &c) && c != 0)
		buffer[n++] = c;
	buffer[n] = 0;
	return n;
}
GLUE(Glue_CompareStringNoCase)
{
	static UniChar a[1024], b[1024];
	ReadUniChars(w, cpu.r[0], a, 1024);
	ReadUniChars(w, cpu.r[1], b, 1024);
	w.Return(cpu, (uint32_t) CompareStringNoCase(a, b));
	return true;
}
static bool
CaseText(TNativeWorld& w, TARMCPU& cpu, bool upper)
{
	long n = (int32_t) cpu.r[1];
	UniChar* text = (UniChar*) malloc((size_t) (n > 0 ? n : 1) * sizeof(UniChar));
	uint16_t c = 0;
	for (long i = 0; i < n; i++)
	{
		w.Read16(cpu.r[0] + (uint32_t) i * 2, &c);
		text[i] = c;
	}
	if (upper)
		UppercaseText(text, n);
	else
		LowercaseText(text, n);
	for (long i = 0; i < n; i++)
		w.Write16(cpu.r[0] + (uint32_t) i * 2, text[i]);
	free(text);
	w.Return(cpu, 0);
	return true;
}
GLUE(Glue_UppercaseText)		{ return CaseText(w, cpu, true); }
GLUE(Glue_LowercaseText)		{ return CaseText(w, cpu, false); }
// ConvertToUnicode(src, dest, encoding, n): at most n characters, up to a
// nul, and the nul written after them
GLUE(Glue_ConvertToUnicode)
{
	long n = (int32_t) cpu.r[3];
	if (n < 0)
		n = 0;
	char* src = (char*) malloc((size_t) n + 1);
	UniChar* dest = (UniChar*) malloc(((size_t) n + 1) * sizeof(UniChar));
	uint8_t c = 0;
	long i = 0;
	for (; i < n && w.Read8(cpu.r[0] + (uint32_t) i, &c) && c != 0; i++)
		src[i] = (char) c;
	src[i] = 0;
	ConvertToUnicode(src, dest, (int32_t) cpu.r[2], n);
	for (long j = 0; j <= n; j++)
	{
		w.Write16(cpu.r[1] + (uint32_t) j * 2, dest[j]);
		if (dest[j] == 0)
			break;
	}
	free(src);
	free(dest);
	w.Return(cpu, 0);
	return true;
}
GLUE(Glue_ConvertFromUnicode)
{
	long n = (int32_t) cpu.r[3];
	if (n < 0)
		n = 0;
	UniChar* src = (UniChar*) malloc(((size_t) n + 1) * sizeof(UniChar));
	char* dest = (char*) malloc((size_t) n * 2 + 2);
	uint16_t c = 0;
	long i = 0;
	for (; i < n && w.Read16(cpu.r[0] + (uint32_t) i * 2, &c) && c != 0; i++)
		src[i] = c;
	src[i] = 0;
	memset(dest, 0, (size_t) n * 2 + 2);
	ConvertFromUnicode(src, dest, (int32_t) cpu.r[2], n);
	for (long j = 0; j < n * 2 + 2; j++)
	{
		w.Write8(cpu.r[1] + (uint32_t) j, (uint8_t) dest[j]);
		if (dest[j] == 0)
			break;
	}
	free(src);
	free(dest);
	w.Return(cpu, 0);
	return true;
}
// The memory the ARM code allocates: blocks of the ARM world's heap
// (gARMHeap), which outlive the call.  The ROM's malloc and free are NewPtr
// and DisposPtr (ROM 0x001e2c50, 0x001e2c54); a failed NewPtr answers nought.
static uint32_t
HeapAlloc(uint32_t size, bool clear)
{
	uint32_t a = gARMHeap.Alloc(size);
	if (a != 0 && clear)
		memset(gARMHeap.At(a), 0, size);
	return a;
}
GLUE(Glue_malloc)				{ w.Return(cpu, HeapAlloc(cpu.r[0], false)); return true; }
GLUE(Glue_free)					{ gARMHeap.Free(cpu.r[0]); w.Return(cpu, 0); return true; }
GLUE(Glue_NewPtr)				{ w.Return(cpu, HeapAlloc(cpu.r[0], false)); return true; }
GLUE(Glue_NewPtrClear)			{ w.Return(cpu, HeapAlloc(cpu.r[0], true)); return true; }
GLUE(Glue_DisposPtr)			{ gARMHeap.Free(cpu.r[0]); w.Return(cpu, 0); return true; }
GLUE(Glue_GetPtrSize)			{ w.Return(cpu, gARMHeap.BlockSize(cpu.r[0])); return true; }
// ROM 0x00318ee8 __nw__FUi: malloc of the size (one byte for nought); a
// failure calls the new handler, which the ARM world has none of
GLUE(Glue_new)					{ w.Return(cpu, HeapAlloc(cpu.r[0] == 0 ? 1 : cpu.r[0], false)); return true; }
// ROM 0x00318f28 __dl__FPv
GLUE(Glue_delete)				{ if (cpu.r[0] != 0) gARMHeap.Free(cpu.r[0]); w.Return(cpu, 0); return true; }

// RefVar and RefStruct, the C++ classes (a word holding a RefHandle's
// address), as ROM 0x00079e7c-0x00079fac: a constructor given no object
// makes one with operator new; a destructor's second argument's bit 0 says
// to delete the object too.
static uint32_t
RefHandleOf(TNativeWorld& w, uint32_t var)
{
	uint32_t handle = 0;
	if (!w.Read32(var, &handle))
		ThrowMsg("armcpu: a bad RefVar");
	return handle;
}
// ROM 0x00079ea8 __ct__6RefVarFCl
GLUE(Glue_RefVar_ctor)
{
	uint32_t self = cpu.r[0];
	if (self == 0)
		self = HeapAlloc(4, false);
	if (self != 0)
		w.Write32(self, w.NewRefHandle(cpu.r[1]));
	w.Return(cpu, self);
	return true;
}
// ROM 0x00079d74 __ct__6RefVarFv: a handle of nil
GLUE(Glue_RefVar_ctor0)
{
	uint32_t self = cpu.r[0];
	if (self == 0)
		self = HeapAlloc(4, false);
	if (self != 0)
		w.Write32(self, w.NewRefHandle(NILREF));
	w.Return(cpu, self);
	return true;
}
// ROM 0x00079f40 __ct__9RefStructFv: a RefVar of nil whose handle is on no
// stack (stackPos nought)
GLUE(Glue_RefStruct_ctor)
{
	uint32_t self = cpu.r[0];
	if (self == 0)
		self = HeapAlloc(4, false);
	if (self != 0)
	{
		uint32_t handle = w.NewRefHandle(NILREF);
		w.Write32(self, handle);
		w.Write32(handle + 4, 0);
	}
	w.Return(cpu, self);
	return true;
}
// ROM 0x00079ee4 __dt__6RefVarFv (and 0x00079f80 __dt__9RefStructFv, which
// is it with the flags nought and then the delete)
GLUE(Glue_RefVar_dtor)
{
	uint32_t self = cpu.r[0];
	w.DisposeRefHandle(RefHandleOf(w, self));
	if (cpu.r[1] & 1)
		gARMHeap.Free(self);
	w.Return(cpu, self);
	return true;
}
// ROM 0x00079f28 __as__6RefVarFCl, 0x00079e7c __as__9RefStructFCl
GLUE(Glue_RefVar_assign)		{ w.Write32(RefHandleOf(w, cpu.r[0]), cpu.r[1]); w.Return(cpu, cpu.r[0]); return true; }
// ROM 0x00079e88 __as__9RefStructFRC6RefVar, 0x00079f14 __as__6RefVarFRC6RefVar
GLUE(Glue_RefVar_assignVar)
{
	uint32_t ref = 0;
	w.Read32(RefHandleOf(w, cpu.r[1]), &ref);
	w.Write32(RefHandleOf(w, cpu.r[0]), ref);
	w.Return(cpu, cpu.r[0]);
	return true;
}
// ROM 0x00079f34 __opl__6RefVarCFv, 0x00079e9c __opl__9RefStructCFv
GLUE(Glue_RefVar_ref)
{
	uint32_t ref = 0;
	w.Read32(RefHandleOf(w, cpu.r[0]), &ref);
	w.Return(cpu, ref);
	return true;
}

// the RefArg forms of the object functions (the Newton C++ Tools' API)
#define REFARG(n)	RefVar(w.ArgRef(cpu.r[n]))
// ROM 0x0031c694 MakeInt__Fl, 0x0031c6b4 MakeBoolean__Fi
GLUE(Glue_MakeInt)				{ w.Return(cpu, cpu.r[0] << 2); return true; }
GLUE(Glue_MakeBoolean)			{ w.Return(cpu, cpu.r[0] != 0 ? (uint32_t) TRUEREF : (uint32_t) NILREF); return true; }
// ROM 0x0031c79c RefToInt__FRC6RefVar: an integer's value, else _RINTError
GLUE(Glue_RefToInt)
{
	uint32_t ref = 0;
	w.Read32(RefHandleOf(w, cpu.r[0]), &ref);
	if ((ref & 3) != 0)
		_RINTError(w.ToHost(ref));
	w.Return(cpu, (uint32_t) ((int32_t) ref >> 2));
	return true;
}
// ROM 0x0031c970 MakeSymbol__FPc: Intern
GLUE(Glue_MakeSymbol)
{
	char name[256];
	if (!w.ReadCString(cpu.r[0], name, sizeof(name)))
		ThrowMsg("armcpu: MakeSymbol of a bad string");
	w.Return(cpu, w.ToARM(Intern(name)));
	return true;
}
GLUE(Glue_LengthArg)			{ w.Return(cpu, (uint32_t) Length(REFARG(0))); return true; }
GLUE(Glue_GetFrameSlot)			{ w.Return(cpu, w.ToARM(GetFrameSlot(REFARG(0), REFARG(1)))); return true; }
GLUE(Glue_SetArraySlot)			{ SetArraySlot(REFARG(0), (int32_t) cpu.r[1], REFARG(2)); w.Return(cpu, 0); return true; }
GLUE(Glue_GetArraySlot)			{ w.Return(cpu, w.ToARM(GetArraySlot(REFARG(0), (int32_t) cpu.r[1]))); return true; }
GLUE(Glue_SetVariable)			{ w.Return(cpu, SetVariable(REFARG(0), REFARG(1), REFARG(2)) ? 1 : 0); return true; }
// (NEWTON_TRACE_ARMCPU=2 names the global function)
static void
TraceGlobalFn(TNativeWorld& w, TARMCPU& cpu)
{
	if (gTrace)
	{
		Ref name = w.ArgRef(cpu.r[0]);
		fprintf(stderr, " [%s]", IsSymbol(name) ? SymbolName(name) : "?");
	}
}
GLUE(Glue_NSCallGlobalFn0)		{ TraceGlobalFn(w, cpu); w.Return(cpu, w.ToARM(NSCallGlobalFn(REFARG(0)))); return true; }
GLUE(Glue_NSCallGlobalFn1)		{ TraceGlobalFn(w, cpu); w.Return(cpu, w.ToARM(NSCallGlobalFn(REFARG(0), REFARG(1)))); return true; }
GLUE(Glue_NSCallGlobalFn2)		{ TraceGlobalFn(w, cpu); w.Return(cpu, w.ToARM(NSCallGlobalFn(REFARG(0), REFARG(1), REFARG(2)))); return true; }
GLUE(Glue_NSCallGlobalFn3)		{ TraceGlobalFn(w, cpu); w.Return(cpu, w.ToARM(NSCallGlobalFn(REFARG(0), REFARG(1), REFARG(2), REFARG(3)))); return true; }
// ROM 0x0031c9f0 LockedBinaryPtr__FRC6RefVar: the object locked and its
// bytes' address - here a window onto them that lasts until the lock goes
GLUE(Glue_LockedBinaryPtr)
{
	RefVar obj(w.ArgRef(cpu.r[0]));
	LockRef(obj);
	w.Return(cpu, LockBinaryWindow(obj));
	return true;
}
// ROM 0x0031ca28 UnlockRefArg__FRC6RefVar
GLUE(Glue_UnlockRefArg)
{
	Ref obj = w.ArgRef(cpu.r[0]);
	UnlockBinaryWindow(obj);
	UnlockRef(obj);
	w.Return(cpu, 0);
	return true;
}
#undef REFARG

// Ustrlen and Ustrncat over the ARM world's UniChars (utility/Unicode.cpp's,
// ROM 0x00256774 Ustrncat: n characters at most, then a nul)
GLUE(Glue_Ustrlen)
{
	uint32_t n = 0;
	uint16_t c;
	while (w.Read16(cpu.r[0] + n * 2, &c) && c != 0)
		n++;
	w.Return(cpu, n);
	return true;
}
GLUE(Glue_Ustrncat)
{
	uint32_t d = cpu.r[0], src = cpu.r[1];
	int32_t n = (int32_t) cpu.r[2];
	uint16_t c = 0;
	while (w.Read16(d, &c) && c != 0)
		d += 2;
	for (;;)
	{
		if (n == 0)
		{
			w.Write16(d, 0);
			break;
		}
		n--;
		w.Read16(src, &c);
		src += 2;
		w.Write16(d, c);
		d += 2;
		if (c == 0)
			break;
	}
	w.Return(cpu, cpu.r[0]);
	return true;
}

GLUE(Glue_SetLexScope)
{
	w.Return(cpu, w.ToARM(SetLexScope(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), RefVar(w.ArgRef(cpu.r[2])), RefVar(w.ArgRef(cpu.r[3])))));
	return true;
}
GLUE(Glue_FindImplementor)		{ w.Return(cpu, w.ToARM(FindImplementor(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))))); return true; }
GLUE(Glue_FindProtoImplementor)	{ w.Return(cpu, w.ToARM(FindProtoImplementor(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))))); return true; }
GLUE(Glue_SetVariableOrGlobal)
{
	RefVar context(w.ArgRef(cpu.r[0]));
	RefVar name(w.ArgRef(cpu.r[1]));
	RefVar value(w.ArgRef(cpu.r[2]));
	w.Return(cpu, SetVariableOrGlobal(context, name, value, (int32_t) cpu.r[3]) ? 1 : 0);
	return true;
}
GLUE(Glue_ForEachLoopReset)		{ w.Return(cpu, ForEachLoopReset(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))) ? 1 : 0); return true; }
GLUE(Glue_ForEachLoopNext)		{ w.Return(cpu, ForEachLoopNext(RefVar(w.ArgRef(cpu.r[0]))) ? 1 : 0); return true; }
GLUE(Glue_ForEachLoopDone)		{ w.Return(cpu, ForEachLoopDone(RefVar(w.ArgRef(cpu.r[0]))) ? 1 : 0); return true; }
// a long* exists answered through the ARM world
static long*
ExistsOut(uint32_t addr, long* local)
{
	return addr != 0 ? local : nil;
}
GLUE(Glue_GetVariable)
{
	long exists = 0;
	Ref v = GetVariable(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), ExistsOut(cpu.r[2], &exists), (int32_t) cpu.r[3]);
	if (cpu.r[2] != 0)
		w.Write32(cpu.r[2], (uint32_t) exists);
	w.Return(cpu, w.ToARM(v));
	return true;
}
GLUE(Glue_GetProtoVariable)
{
	long exists = 0;
	Ref v = GetProtoVariable(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), ExistsOut(cpu.r[2], &exists));
	if (cpu.r[2] != 0)
		w.Write32(cpu.r[2], (uint32_t) exists);
	w.Return(cpu, w.ToARM(v));
	return true;
}
GLUE(Glue_Intern)
{
	char name[256];
	if (!w.ReadCString(cpu.r[0], name, sizeof(name)))
		ThrowMsg("armcpu: Intern of a bad string");
	w.Return(cpu, w.ToARM(Intern(name)));
	return true;
}
GLUE(Glue_MakeString)
{
	char text[1024];
	if (!w.ReadCString(cpu.r[0], text, sizeof(text)))
		ThrowMsg("armcpu: MakeString of a bad string");
	w.Return(cpu, w.ToARM(MakeString(text)));
	return true;
}
GLUE(Glue_IsString)				{ w.Return(cpu, IsString(RefVar(w.ArgRef(cpu.r[0]))) ? 1 : 0); return true; }
GLUE(Glue_IsInstance)			{ w.Return(cpu, IsInstance(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))) ? 1 : 0); return true; }
GLUE(Glue_IsSubclassRef)		{ w.Return(cpu, IsSubclassRef(w.ToHost(cpu.r[0]), w.ToHost(cpu.r[1])) ? 1 : 0); return true; }
GLUE(Glue_ISREAL)				{ w.Return(cpu, ISREAL(w.ToHost(cpu.r[0])) ? 1 : 0); return true; }
GLUE(Glue_RemoveSlot)			{ RemoveSlot(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))); w.Return(cpu, 0); return true; }
GLUE(Glue_DeepClone)			{ w.Return(cpu, w.ToARM(DeepClone(RefVar(w.ArgRef(cpu.r[0]))))); return true; }
GLUE(Glue_TotalClone)			{ w.Return(cpu, w.ToARM(TotalClone(RefVar(w.ArgRef(cpu.r[0]))))); return true; }
GLUE(Glue_SetClass)				{ SetClass(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))); w.Return(cpu, 0); return true; }
GLUE(Glue_AllocateBinary)		{ w.Return(cpu, w.ToARM(AllocateBinary(RefVar(w.ArgRef(cpu.r[0])), (int32_t) cpu.r[1]))); return true; }
GLUE(Glue_FrameHasPath)			{ w.Return(cpu, FrameHasPath(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1]))) ? 1 : 0); return true; }
GLUE(Glue_SetFramePath)			{ SetFramePath(RefVar(w.ArgRef(cpu.r[0])), RefVar(w.ArgRef(cpu.r[1])), RefVar(w.ArgRef(cpu.r[2]))); w.Return(cpu, 0); return true; }
GLUE(Glue_RCHAR)				{ w.Return(cpu, (uint32_t) RCHAR(w.ToHost(cpu.r[0]))); return true; }

// the C library, over the ARM world's memory
GLUE(Glue_memmove)
{
	uint32_t dst = cpu.r[0], src = cpu.r[1], n = cpu.r[2];
	if (dst < src)
		for (uint32_t i = 0; i < n; i++)
		{
			uint8_t b = 0;
			w.Read8(src + i, &b);
			w.Write8(dst + i, b);
		}
	else
		for (uint32_t i = n; i-- > 0; )
		{
			uint8_t b = 0;
			w.Read8(src + i, &b);
			w.Write8(dst + i, b);
		}
	w.Return(cpu, dst);
	return true;
}
GLUE(Glue_memset)
{
	for (uint32_t i = 0; i < cpu.r[2]; i++)
		w.Write8(cpu.r[0] + i, (uint8_t) cpu.r[1]);
	w.Return(cpu, cpu.r[0]);
	return true;
}
GLUE(Glue_strlen)
{
	uint32_t n = 0;
	uint8_t c;
	while (w.Read8(cpu.r[0] + n, &c) && c != 0)
		n++;
	w.Return(cpu, n);
	return true;
}
// (the rest of the ROM's C library - Norcroft's - over the ARM world's bytes;
//  a comparison answers the difference of the first bytes that differ, taken
//  unsigned, as its does)
static uint8_t
CByte(TNativeWorld& w, uint32_t a)
{
	uint8_t c = 0;
	if (!w.Read8(a, &c))
		ThrowMsg("armcpu: a C string out of the ARM world's memory");
	return c;
}
static uint32_t
EndOf(TNativeWorld& w, uint32_t a)
{
	while (CByte(w, a) != 0)
		a++;
	return a;
}
GLUE(Glue_strcat)
{
	uint32_t d = EndOf(w, cpu.r[0]), s = cpu.r[1];
	uint8_t c;
	do { c = CByte(w, s++); w.Write8(d++, c); } while (c != 0);
	w.Return(cpu, cpu.r[0]);
	return true;
}
GLUE(Glue_strncat)
{
	uint32_t d = EndOf(w, cpu.r[0]), s = cpu.r[1], n = cpu.r[2];
	for ( ; n > 0; n--)
	{
		uint8_t c = CByte(w, s++);
		if (c == 0)
			break;
		w.Write8(d++, c);
	}
	w.Write8(d, 0);
	w.Return(cpu, cpu.r[0]);
	return true;
}
GLUE(Glue_strncpy)
{
	// (n bytes written: the string, then nuls to fill)
	uint32_t d = cpu.r[0], s = cpu.r[1], n = cpu.r[2];
	bool ended = false;
	for (uint32_t i = 0; i < n; i++)
	{
		uint8_t c = ended ? 0 : CByte(w, s + i);
		if (c == 0)
			ended = true;
		w.Write8(d + i, c);
	}
	w.Return(cpu, cpu.r[0]);
	return true;
}
static int32_t
CompareBytes(TNativeWorld& w, uint32_t a, uint32_t b, uint32_t n)
{
	for (uint32_t i = 0; i < n; i++)
	{
		uint8_t x = CByte(w, a + i), y = CByte(w, b + i);
		if (x != y)
			return (int32_t) x - (int32_t) y;
		if (x == 0)
			break;
	}
	return 0;
}
GLUE(Glue_strcmp)				{ w.Return(cpu, (uint32_t) CompareBytes(w, cpu.r[0], cpu.r[1], 0xffffffff)); return true; }
GLUE(Glue_strncmp)				{ w.Return(cpu, (uint32_t) CompareBytes(w, cpu.r[0], cpu.r[1], cpu.r[2])); return true; }
GLUE(Glue_strchr)
{
	uint32_t s = cpu.r[0];
	uint8_t want = (uint8_t) cpu.r[1];
	for (;; s++)
	{
		uint8_t c = CByte(w, s);
		if (c == want)
			break;
		if (c == 0)
		{
			s = 0;
			break;
		}
	}
	w.Return(cpu, s);
	return true;
}
static bool
InSet(TNativeWorld& w, uint32_t set, uint8_t c)
{
	for (uint8_t s; (s = CByte(w, set)) != 0; set++)
		if (s == c)
			return true;
	return false;
}
GLUE(Glue_strpbrk)
{
	uint32_t s = cpu.r[0];
	for (uint8_t c; (c = CByte(w, s)) != 0; s++)
		if (InSet(w, cpu.r[1], c))
		{
			w.Return(cpu, s);
			return true;
		}
	w.Return(cpu, 0);
	return true;
}
GLUE(Glue_strstr)
{
	uint32_t s = cpu.r[0], sub = cpu.r[1];
	uint32_t n = EndOf(w, sub) - sub;
	for (;; s++)
	{
		if (CompareBytes(w, s, sub, n) == 0)
		{
			w.Return(cpu, s);
			return true;
		}
		if (CByte(w, s) == 0)
			break;
	}
	w.Return(cpu, 0);
	return true;
}
// strtok keeps its place between calls, as the C library's static does
static uint32_t	gStrtokNext = 0;
GLUE(Glue_strtok)
{
	uint32_t s = cpu.r[0] != 0 ? cpu.r[0] : gStrtokNext;
	if (s == 0)
	{
		w.Return(cpu, 0);
		return true;
	}
	while (CByte(w, s) != 0 && InSet(w, cpu.r[1], CByte(w, s)))
		s++;
	if (CByte(w, s) == 0)
	{
		gStrtokNext = 0;
		w.Return(cpu, 0);
		return true;
	}
	uint32_t token = s;
	while (CByte(w, s) != 0 && !InSet(w, cpu.r[1], CByte(w, s)))
		s++;
	if (CByte(w, s) != 0)
	{
		w.Write8(s, 0);
		gStrtokNext = s + 1;
	}
	else
		gStrtokNext = 0;
	w.Return(cpu, token);
	return true;
}
static int32_t
ReadDecimal(TNativeWorld& w, uint32_t s)
{
	// (atoi/atol: white space, a sign, the digits; the ARM's word wraps)
	while (CByte(w, s) == ' ' || (CByte(w, s) >= 9 && CByte(w, s) <= 13))
		s++;
	bool negative = false;
	if (CByte(w, s) == '-' || CByte(w, s) == '+')
		negative = CByte(w, s++) == '-';
	uint32_t v = 0;
	for (uint8_t c; (c = CByte(w, s)) >= '0' && c <= '9'; s++)
		v = v * 10 + (uint32_t) (c - '0');
	return (int32_t) (negative ? 0u - v : v);
}
GLUE(Glue_atoi)					{ w.Return(cpu, (uint32_t) ReadDecimal(w, cpu.r[0])); return true; }
// sprintf(buffer, format, ...): the arguments from r2 on, then the stack; a
// double takes two words, the high one first
GLUE(Glue_sprintf)
{
	uint32_t out = cpu.r[0], f = cpu.r[1];
	int next = 2;
	uint32_t n = 0;
	for (uint8_t c; (c = CByte(w, f)) != 0; f++)
	{
		if (c != '%')
		{
			w.Write8(out + n++, c);
			continue;
		}
		char spec[32];
		size_t k = 0;
		spec[k++] = '%';
		f++;
		while (k < sizeof(spec) - 4)
		{
			c = CByte(w, f);
			if (strchr("-+ #0123456789.", c) == nil || c == 0)
				break;
			if (c == '*')
				break;
			spec[k++] = (char) c;
			f++;
		}
		while ((c = CByte(w, f)) == 'l' || c == 'h')
			f++;
		char text[512];
		text[0] = 0;
		if (c == 0)
			break;
		spec[k++] = (char) c;
		spec[k] = 0;
		switch (c)
		{
		case 'd': case 'i':
			snprintf(text, sizeof(text), spec, (int) (int32_t) w.Arg(cpu, next++));
			break;
		case 'u': case 'x': case 'X': case 'o': case 'c':
			snprintf(text, sizeof(text), spec, (unsigned) w.Arg(cpu, next++));
			break;
		case 'p':
			snprintf(text, sizeof(text), "%x", (unsigned) w.Arg(cpu, next++));
			break;
		case 's':
		{
			char str[256];
			uint32_t a = w.Arg(cpu, next++);
			if (!w.ReadCString(a, str, sizeof(str)))
				strcpy(str, "");
			snprintf(text, sizeof(text), spec, str);
			break;
		}
		case 'f': case 'e': case 'E': case 'g': case 'G':
		{
			uint64_t bits = ((uint64_t) w.Arg(cpu, next) << 32) | w.Arg(cpu, next + 1);
			next += 2;
			double d;
			memcpy(&d, &bits, 8);
			snprintf(text, sizeof(text), spec, d);
			break;
		}
		case '%':
			strcpy(text, "%");
			break;
		default:
			snprintf(text, sizeof(text), "%s", spec);
			break;
		}
		for (char* t = text; *t != 0; t++)
			w.Write8(out + n++, (uint8_t) *t);
	}
	w.Write8(out + n, 0);
	w.Return(cpu, n);
	return true;
}

// Norcroft's division helpers: the divisor in r0, the dividend in r1; the
// quotient in r0 and the remainder in r1
GLUE(Glue_rt_sdiv)
{
	int32_t divisor = (int32_t) cpu.r[0], dividend = (int32_t) cpu.r[1];
	if (divisor == 0)
		ThrowMsg("armcpu: division by zero");
	cpu.r[1] = (uint32_t) (dividend % divisor);
	w.Return(cpu, (uint32_t) (dividend / divisor));
	return true;
}
GLUE(Glue_rt_udiv)
{
	uint32_t divisor = cpu.r[0], dividend = cpu.r[1];
	if (divisor == 0)
		ThrowMsg("armcpu: division by zero");
	cpu.r[1] = dividend % divisor;
	w.Return(cpu, dividend / divisor);
	return true;
}
GLUE(Glue_rt_sdiv10)
{
	int32_t dividend = (int32_t) cpu.r[0];
	cpu.r[1] = (uint32_t) (dividend % 10);
	w.Return(cpu, (uint32_t) (dividend / 10));
	return true;
}
GLUE(Glue_rt_udiv10)
{
	uint32_t dividend = cpu.r[0];
	cpu.r[1] = dividend % 10;
	w.Return(cpu, dividend / 10);
	return true;
}
GLUE(Glue_multiply)				{ w.Return(cpu, cpu.r[0] * cpu.r[1]); return true; }

// NativeEntry(fn, numArgs, &closure): the address to call for a function
// object - a native C function of the ROM (a host function, handed over as
// a callback address), a function in this code binary (its ARM address,
// its closure set) - or nought for a NewtonScript function, which the code
// then runs through the interpreter.
GLUE(Glue_NativeEntry)
{
	RefVar fn(w.ArgRef(cpu.r[0]));
	long numArgs = (int32_t) cpu.r[1];
	uint32_t closureVar = cpu.r[2];
	Ref cls = GetArraySlotRef(fn, kFunctionClassSlot);
	uint32_t entry = 0;
	uint32_t closure = NILREF;
	if (cls == kNativeFuncClass)
	{
		Ref ptr = GetArraySlotRef(fn, kNativeFuncPtrSlot);
		long fnArgs = RINT(GetArraySlotRef(fn, kNativeNumArgsSlot));
		if (numArgs != fnArgs)
			Throw(exInterpreter, (void*) kNSErrWrongNumberOfArgs, nil);
		void* host = (void*) ptr;
		if ((ULong) ptr < kROMCodeLimit)
		{
			long bound;
			host = ResolveNativeFunction((ULong) ptr, &bound);
			if (host == nil)
				Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
		}
		entry = w.HostCallback(host, numArgs);
	}
	else if (cls == kBinaryNativeFuncClass)
	{
		// (the offset in its code binary; a function of another binary - not
		//  mapped here - is called through a callback, in a world of its own)
		RefVar code(GetArraySlotRef(fn, 1));
		closure = w.ToARM(GetArraySlotRef(fn, 3));
		if (!w.IsThisCode(code))
			entry = w.FunctionCallback(fn, numArgs + (closure != NILREF ? 1 : 0));
		else
			entry = kCodeBase + (uint32_t) RINT(GetArraySlotRef(fn, 4));
	}
	if (closureVar != 0)
	{
		uint32_t handle = 0;
		w.Read32(closureVar, &handle);
		w.Write32(handle, closure);
	}
	w.Return(cpu, entry);
	return true;
}

static void
InitGlue(void)
{
	if (gGlue != nil)
		return;
	gGlue = new GlueEntry[kGlueSlots];
	for (uint32_t i = 0; i < kGlueSlots; i++)
	{
		gGlue[i].fName = nil;
		gGlue[i].fFn = nil;
	}
	static const struct { const char* name; GlueFn fn; } kGlue[] =
	{
		{ "AllocateRefHandle__Fl", Glue_AllocateRefHandle },
		{ "DisposeRefHandle__FP9RefHandle", Glue_DisposeRefHandle },
		{ "GetArraySlotRef__FlT1", Glue_GetArraySlotRef },
		{ "SetArraySlotRef__FlN21", Glue_SetArraySlotRef },
		{ "GetFrameSlotRef__FlT1", Glue_GetFrameSlotRef },
		{ "FrameHasSlotRef__FlT1", Glue_FrameHasSlotRef },
		{ "Length__Fl", Glue_Length },
		{ "SetLength__FRC6RefVarl", Glue_SetLength },
		{ "Clone__FRC6RefVar", Glue_Clone },
		{ "GetFramePath__FRC6RefVarT1", Glue_GetFramePath },
		{ "EQRef__FlT1", Glue_EQRef },
		{ "ObjectFlags__Fl", Glue_ObjectFlags },
		{ "Slots__Fl", Glue_Slots },
		{ "BinaryData__Fl", Glue_BinaryData },
		{ "_RINTError__Fl", Glue_RINTError },
		{ "_RCHARError__Fl", Glue_RCHARError },
		{ "ClassOf__FRC6RefVar", Glue_ClassOf },
		{ "IsSymbol__Fl", Glue_IsSymbol },
		{ "AllocateFrame__Fv", Glue_AllocateFrame },
		{ "AllocateArray__FRC6RefVarl", Glue_AllocateArray },
		{ "AddArraySlot__FRC6RefVarT1", Glue_AddArraySlot },
		{ "SetFrameSlot__FRC6RefVarN21", Glue_SetFrameSlot },
		{ "MAKEBOOLEAN__Fi", Glue_MAKEBOOLEAN },
		{ "LockRef__Fl", Glue_LockRef },
		{ "UnlockRef__Fl", Glue_UnlockRef },
		{ "GetGInterpreter__Fv", Glue_GetGInterpreter },
		{ "IsSend__12TInterpreterFv", Glue_IsSend },
		{ "SetCallEnv__12TInterpreterFv", Glue_SetCallEnv },
		{ "GetReceiver__12TInterpreterFv", Glue_GetReceiver },
		{ "GetImplementor__12TInterpreterFv", Glue_GetImplementor },
		{ "SetSendEnv__12TInterpreterFRC6RefVarT1", Glue_SetSendEnv },
		{ "PushValue__12TInterpreterFRC6RefVar", Glue_PushValue },
		{ "PopValue__12TInterpreterFv", Glue_PopValue },
		{ "setjmp", Glue_setjmp },
		{ "longjmp", Glue_longjmp },
		{ "AddExceptionHandler", Glue_AddExceptionHandler },
		{ "ExitHandler", Glue_ExitHandler },
		{ "NextHandler", Glue_NextHandler },
		{ "Throw", Glue_Throw },
		{ "SetupSend__FRC6RefVarT1lR6RefVar", Glue_SetupSend },
		{ "SetLexScope__FRC6RefVarN31", Glue_SetLexScope },
		{ "TranslateException__12TInterpreterFP9Exception", Glue_TranslateException },
		{ "StrEndsWith__FRC6RefVarT1", Glue_StrEndsWith },
		{ "SetupResend__FRC6RefVarlR6RefVar", Glue_SetupResend },
		{ "ArrayMunger__FRC6RefVarlT2T1N22", Glue_ArrayMunger },
		{ "BinaryMunger__FRC6RefVarlT2T1N22", Glue_BinaryMunger },
		{ "StrMunger__FRC6RefVarlT2T1N22", Glue_StrMunger },
		{ "ArrayPosition__FRC6RefVarT1lT1", Glue_ArrayPosition },
		{ "EnsureInternal__FRC6RefVar", Glue_EnsureInternal },
		{ "ReplaceObjectRef__FlT1", Glue_ReplaceObjectRef },
		{ "SortArray__FRC6RefVarN21", Glue_SortArray },
		{ "StrBeginsWith__FRC6RefVarT1", Glue_StrBeginsWith },
		{ "StrCapitalizeWords__FRC6RefVar", Glue_StrCapitalizeWords },
		{ "StrCapitalize__FRC6RefVar", Glue_StrCapitalize },
		{ "StrDowncase__FRC6RefVar", Glue_StrDowncase },
		{ "StrUpcase__FRC6RefVar", Glue_StrUpcase },
		{ "StrPosition__FRC6RefVarT1l", Glue_StrPosition },
		{ "StrReplace__FRC6RefVarN21l", Glue_StrReplace },
		{ "Substring__FRC6RefVarlT2", Glue_Substring },
		{ "TrimString__FRC6RefVar", Glue_TrimString },
		{ "PrintObject__FRC6RefVarUl", Glue_PrintObject },
		{ "CoerceToDouble__FRC6RefVar", Glue_CoerceToDouble },
		{ "CompareStringNoCase__FPUsT1", Glue_CompareStringNoCase },
		{ "UppercaseText__FPUsl", Glue_UppercaseText },
		{ "LowercaseText__FPUsl", Glue_LowercaseText },
		{ "ConvertToUnicode__FPCvPUslT3", Glue_ConvertToUnicode },
		{ "ConvertFromUnicode__FPCUsPvlT3", Glue_ConvertFromUnicode },
		{ "malloc", Glue_malloc },
		{ "free", Glue_free },
		{ "AllocateFrameWithMap__FRC6RefVar", Glue_AllocateFrameWithMap },
		{ "ThrowRefException__FPcRC6RefVar", Glue_ThrowRefException },
		{ "Subexception", Glue_Subexception },
		{ "GetStackStateBlock__Fv", Glue_GetStackStateBlock },
		{ "ResetStackStateBlock__FP10StackState", Glue_ResetStackStateBlock },
		{ "DisposeStackStateBlock__FP10StackState", Glue_DisposeStackStateBlock },
		{ "IncrementCurrentStackPos__Fv", Glue_IncrementCurrentStackPos },
		{ "DecrementCurrentStackPos__Fv", Glue_DecrementCurrentStackPos },
		{ "ClearRefHandles__Fv", Glue_ClearRefHandles },
		{ "NativeEntry__FRC6RefVarlPP9RefHandle", Glue_NativeEntry },
		{ "GetGFunctionFrame__Fv", Glue_GetGFunctionFrame },
		{ "Call__12TInterpreterFRC6RefVarl", Glue_Call },
		{ "Send__12TInterpreterFRC6RefVarN21l", Glue_Send },
		{ "Run__12TInterpreterFv", Glue_Run },
		{ "FindImplementor__FRC6RefVarT1", Glue_FindImplementor },
		{ "FindProtoImplementor__FRC6RefVarT1", Glue_FindProtoImplementor },
		{ "SetVariableOrGlobal__FRC6RefVarN21iT4", Glue_SetVariableOrGlobal },
		{ "ForEachLoopReset__FRC6RefVarT1", Glue_ForEachLoopReset },
		{ "ForEachLoopNext__FRC6RefVar", Glue_ForEachLoopNext },
		{ "ForEachLoopDone__FRC6RefVar", Glue_ForEachLoopDone },
		{ "GetVariable__FRC6RefVarT1Pli", Glue_GetVariable },
		{ "GetProtoVariable__FRC6RefVarT1Pl", Glue_GetProtoVariable },
		{ "Intern__FPc", Glue_Intern },
		{ "MakeString__FPCc", Glue_MakeString },
		{ "IsString__FRC6RefVar", Glue_IsString },
		{ "IsInstance__FRC6RefVarT1", Glue_IsInstance },
		{ "IsSubclassRef__FlT1", Glue_IsSubclassRef },
		{ "ISREAL__Fl", Glue_ISREAL },
		{ "RemoveSlot__FRC6RefVarT1", Glue_RemoveSlot },
		{ "DeepClone__FRC6RefVar", Glue_DeepClone },
		{ "TotalClone__FRC6RefVar", Glue_TotalClone },
		{ "SetClass__FRC6RefVarT1", Glue_SetClass },
		{ "AllocateBinary__FRC6RefVarl", Glue_AllocateBinary },
		{ "FrameHasPath__FRC6RefVarT1", Glue_FrameHasPath },
		{ "SetFramePath__FRC6RefVarN21", Glue_SetFramePath },
		{ "RCHAR__Fl", Glue_RCHAR },
		{ "memcpy", Glue_memmove },
		{ "memmove", Glue_memmove },
		{ "memset", Glue_memset },
		{ "strlen", Glue_strlen },
		{ "__rt_sdiv", Glue_rt_sdiv },
		{ "__rt_udiv", Glue_rt_udiv },
		{ "__rt_sdiv10", Glue_rt_sdiv10 },
		{ "__rt_udiv10", Glue_rt_udiv10 },
		{ "__multiply", Glue_multiply },
		// the heap (outliving the call)
		{ "NewPtr", Glue_NewPtr },
		{ "NewPtrClear", Glue_NewPtrClear },
		{ "DisposPtr", Glue_DisposPtr },
		{ "GetPtrSize", Glue_GetPtrSize },
		{ "__nw__FUi", Glue_new },
		{ "__dl__FPv", Glue_delete },
		// RefVar and RefStruct
		{ "__ct__6RefVarFCl", Glue_RefVar_ctor },
		{ "__ct__6RefVarFv", Glue_RefVar_ctor0 },
		{ "__ct__9RefStructFv", Glue_RefStruct_ctor },
		{ "__dt__6RefVarFv", Glue_RefVar_dtor },
		{ "__dt__9RefStructFv", Glue_RefVar_dtor },
		{ "__as__6RefVarFCl", Glue_RefVar_assign },
		{ "__as__9RefStructFCl", Glue_RefVar_assign },
		{ "__as__6RefVarFRC6RefVar", Glue_RefVar_assignVar },
		{ "__as__9RefStructFRC6RefVar", Glue_RefVar_assignVar },
		{ "__opl__6RefVarCFv", Glue_RefVar_ref },
		{ "__opl__9RefStructCFv", Glue_RefVar_ref },
		// the RefArg forms
		{ "MakeInt__Fl", Glue_MakeInt },
		{ "MakeBoolean__Fi", Glue_MakeBoolean },
		{ "RefToInt__FRC6RefVar", Glue_RefToInt },
		{ "MakeSymbol__FPc", Glue_MakeSymbol },
		{ "Length__FRC6RefVar", Glue_LengthArg },
		{ "GetFrameSlot__FRC6RefVarT1", Glue_GetFrameSlot },
		{ "GetArraySlot__FRC6RefVarl", Glue_GetArraySlot },
		{ "SetArraySlot__FRC6RefVarlT1", Glue_SetArraySlot },
		{ "SetVariable__FRC6RefVarN21", Glue_SetVariable },
		{ "NSCallGlobalFn__FRC6RefVar", Glue_NSCallGlobalFn0 },
		{ "NSCallGlobalFn__FRC6RefVarT1", Glue_NSCallGlobalFn1 },
		{ "NSCallGlobalFn__FRC6RefVarN21", Glue_NSCallGlobalFn2 },
		{ "NSCallGlobalFn__FRC6RefVarN31", Glue_NSCallGlobalFn3 },
		{ "LockedBinaryPtr__FRC6RefVar", Glue_LockedBinaryPtr },
		{ "UnlockRefArg__FRC6RefVar", Glue_UnlockRefArg },
		{ "Ustrlen", Glue_Ustrlen },
		{ "Ustrncat", Glue_Ustrncat },
		// the C library
		{ "strcat", Glue_strcat },
		{ "strncat", Glue_strncat },
		{ "strncpy", Glue_strncpy },
		{ "strcmp", Glue_strcmp },
		{ "strncmp", Glue_strncmp },
		{ "strchr", Glue_strchr },
		{ "strpbrk", Glue_strpbrk },
		{ "strstr", Glue_strstr },
		{ "strtok", Glue_strtok },
		{ "atoi", Glue_atoi },
		{ "atol", Glue_atoi },
		{ "sprintf", Glue_sprintf },
	};
	for (const auto& g : kGlue)
	{
		bool found = false;
		for (unsigned long i = 0; i < kPublicJumpTableCount; i++)
			if (strcmp(kPublicJumpTable[i].fName, g.name) == 0)
			{
			{
				gGlue[kPublicJumpTable[i].fOffset / 4].fName = g.name;
				gGlue[kPublicJumpTable[i].fOffset / 4].fFn = g.fn;
			}
				found = true;
			}
		if (!found)
			fprintf(stderr, "[armcpu] %s is not in the public jump table\n", g.name);
	}
}


long
PackageNativeCPUEntryCount(void)
{
	InitGlue();
	long n = 0;
	for (uint32_t i = 0; i < kGlueSlots; i++)
		if (gGlue[i].fFn != nil)
			n++;
	return n;
}


Boolean
PackageNativeCPUAnswers(const char* name)
{
	InitGlue();
	for (uint32_t i = 0; i < kGlueSlots; i++)
		if (gGlue[i].fName != nil && strcmp(gGlue[i].fName, name) == 0)
			return true;
	// (or a native function of the ROM's the host has, called generically)
	for (unsigned long i = 0; i < kPublicJumpTableCount; i++)
		if (strcmp(kPublicJumpTable[i].fName, name) == 0 && kPublicJumpTable[i].fSlot != 0)
		{
			long numArgs = 0;
			if (ResolveNativeFunction((ULong) kPublicJumpTable[i].fSlot, &numArgs) != nil)
				return true;
		}
	return false;
}


/*------------------------------------------------------------------------------
	T h e   f a l l b a c k
------------------------------------------------------------------------------*/

Ref
RunPackageNativeOnCPU(RefArg code, ULong offset, RefArg rcvr, long numArgs, const RefVar* const* args)
{
	// NEWTON_TRACE_ARMCPU=1: a line for each native call; 2: every call out
	const char* trace = getenv("NEWTON_TRACE_ARMCPU");
	gTrace = trace != nil && atoi(trace) >= 2;
	Boolean summary = trace != nil;
	long count = numArgs + 1;
	if (count > 16)
		ThrowMsg("armcpu: too many arguments");
	// (on the heap: an exception out of the call unwinds by longjmp, which
	//  runs no destructors, so the cleanup clause gives it back)
	TNativeWorld* world = new TNativeWorld(code);
	TARMCPU* cpu = new TARMCPU(world);
	Ref result = NILREF;
	newton_try
	{
		// the receiver and each argument by reference: r0, r1, r2, r3, then
		// on the stack
		uint32_t refVars[16];
		refVars[0] = world->NewRefVar(rcvr);
		for (long i = 0; i < numArgs; i++)
			refVars[i + 1] = world->NewRefVar(*args[i]);
		uint32_t sp = kArenaBase + kArenaSize - 16;
		if (count > 4)
		{
			sp -= (uint32_t) (count - 4) * 4;
			for (long i = 4; i < count; i++)
				world->Write32(sp + (uint32_t) (i - 4) * 4, refVars[i]);
		}
		for (long i = 0; i < 4 && i < count; i++)
			cpu->r[i] = refVars[i];
		cpu->r[13] = sp;
		ARMStop stop = cpu->Call(kCodeBase + offset, kReturn);
		if (summary)
			fprintf(stderr, "[armcpu] native +%#lx: %lu instructions, stop %d, r0 %08x\n", (unsigned long) offset, (unsigned long) cpu->steps, (int) stop, cpu->r[0]);
		if (stop != kARMReturned)
		{
			if (stop != kARMStoppedByHost)
				fprintf(stderr, "[armcpu] native +%#lx stopped (%d) at pc %08x, address %08x\n", (unsigned long) offset, (int) stop, cpu->faultPC, cpu->faultAddress);
			ThrowMsg("armcpu: the native function did not run to its end");
		}
		result = world->ToHost(cpu->r[0]);
	}
	cleanup
	{
		delete cpu;
		delete world;
	}
	end_try;
	delete cpu;
	delete world;
	return result;
}


void
InstallPackageNativeCPU(void)
{
	SetPackageNativeFallback(RunPackageNativeOnCPU);
}
