// Frames test: the object system over a standalone kernel heap (no boot).
// Refs and immediates; binaries, arrays and frames; symbols (interning,
// EQ, the ROM symbols); frame slots through maps (adding, removing, sorted
// maps past 20 tags, shared maps of clones); paths; clones; classes; the
// collector (dead objects go, live ones move and every ref follows,
// locked objects stay, weak arrays, forwarding after a resize, roots,
// DIY and GC procs, the RefHandle table growing, GCTWA); the exceptions
// the object system throws.

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "memory/host/KernelHeap.h"
#include "NewtonMemory.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the error code of a frames exception (its data is a RefStruct* to a
// frame {errorCode, value})
static long
ErrorCodeOf(const ExceptionHandler& info)
{
	RefStruct* data = (RefStruct*) info.exception.data;
	return RINT(GetFrameSlot(*data, RSSYMerrorcode));
}

// what evaluating the expression throws, as a frames error code (0 if nothing)
#define THROWS(expr, name) ThrowsCode([&]() { (void) (expr); }, name)

template <typename F>
static long
ThrowsCode(F f, const char* name)
{
	long code = 0;
	newton_try
	{
		f();
	}
	newton_catch((ExceptionName) name)
	{
		code = ErrorCodeOf(_info);
	}
	end_try;
	return code;
}

static long
ThrowsData(void (*f)(), const char* name)
{
	long code = 0;
	newton_try
	{
		f();
	}
	newton_catch((ExceptionName) name)
	{
		code = (long) (Long) _info.exception.data;
	}
	end_try;
	return code;
}


static void
TestRefs()
{
	EXPECT(sizeof(Ref) == sizeof(void*));
	EXPECT(ISINT(MAKEINT(5)) && RINT(MAKEINT(5)) == 5 && RINT(MAKEINT(-7)) == -7);
	EXPECT(ISCHAR(MAKECHAR('a')) && RCHAR(MAKECHAR('a')) == 'a');
	EXPECT(ISBOOLEAN(TRUEREF) && ISNIL(NILREF) && !ISNIL(TRUEREF));
	EXPECT(NILREF == 2 && TRUEREF == 0x1a && kSymbolClass == 0x55552);
	EXPECT(RINT(MAKEINT(0x1fffffff)) == 0x1fffffff);
	EXPECT(EQ(MAKEINT(3), MAKEINT(3)) && !EQ(MAKEINT(3), MAKEINT(4)));
	EXPECT(IsNumber(MAKEINT(3)) && !IsNumber(NILREF));
	EXPECT(EQ(ClassOf(MAKEINT(1)), RSSYMint) && EQ(ClassOf(MAKECHAR('x')), RSSYMchar) && EQ(ClassOf(TRUEREF), RSSYMboolean));
	EXPECT(RINT(MAKEINT(1)) == 1);
}


static void
TestBinaries()
{
	RefVar b(AllocateBinary(RSSYMstring, 10));
	EXPECT(IsBinary(b) && !IsArray(b) && !IsFrame(b));
	EXPECT(Length(b) == 10);
	EXPECT(EQ(ClassOf(b), RSSYMstring));
	char* data = BinaryData(b);
	for (int i = 0; i < 10; i++)
		EXPECT(data[i] == 0);
	memcpy(data, "abcdefghij", 10);
	SetLength(b, 20);
	EXPECT(Length(b) == 20 && memcmp(BinaryData(b), "abcdefghij", 10) == 0 && BinaryData(b)[15] == 0);
	SetLength(b, 4);
	EXPECT(Length(b) == 4 && memcmp(BinaryData(b), "abcd", 4) == 0);
	EXPECT((ObjectFlags(b) & kObjDirty) != 0);
	SetClass(b, RSSYMasciistring);
	EXPECT(EQ(ClassOf(b), RSSYMasciistring));

	RefVar s(MakeString("hello"));
	EXPECT(IsString(s) && Length(s) == 12 && GetCString(s)[1] == 'e' && GetCString(s)[5] == 0);
	RefVar a(ASCIIString(s));
	EXPECT(EQ(ClassOf(a), RSSYMasciistring) && Length(a) == 6 && memcmp(BinaryData(a), "hello", 6) == 0);
	EXPECT(IsInstance(s, RSSYMstring) && IsSubclassRef(RSSYMstring, Intern((char*) "")));
	EXPECT(IsSubclassRef(Intern((char*) "string.email"), RSSYMstring) && !IsSubclassRef(RSSYMstring, Intern((char*) "string.email")));

	RefVar r(MakeReal(3.25));
	EXPECT(ISREAL(r) && IsNumber(r) && CDouble(r) == 3.25 && CoerceToInt(r) == 3 && CoerceToDouble(MAKEINT(4)) == 4.0);
	EXPECT(THROWS(CDouble(s), "evt.ex.fr.type") == kNSErrNotAReal);
	EXPECT(THROWS(Length(MAKEINT(1)), "evt.ex.fr.type") == kNSErrUnexpectedImmediate);
	EXPECT(THROWS(AllocateBinary(RSSYMstring, -1), "evt.ex.fr") == kNSErrNegativeLength);
	EXPECT(THROWS(AllocateArray(RSSYMarray, kMaxArrayLength + 1), "evt.ex.fr") == kNSErrOutOfRange);
	EXPECT(THROWS(GetCString(b), "evt.ex.fr.type") == kNSErrNotAString);
}


static void
TestArrays()
{
	RefVar a(AllocateArray(RSSYMarray, 3));
	EXPECT(IsArray(a) && Length(a) == 3 && ISNIL(GetArraySlot(a, 0)) && ISNIL(GetArraySlot(a, 2)));
	SetArraySlot(a, 1, MAKEINT(42));
	EXPECT(RINT(GetArraySlot(a, 1)) == 42);
	AddArraySlot(a, MAKECHAR('z'));
	EXPECT(Length(a) == 4 && RCHAR(GetArraySlot(a, 3)) == 'z');
	SetLength(a, 6);
	EXPECT(Length(a) == 6 && ISNIL(GetArraySlot(a, 5)) && RINT(GetArraySlot(a, 1)) == 42);
	EXPECT(THROWS(GetArraySlot(a, 6), "evt.ex.fr") == kNSErrOutOfBounds);
	EXPECT(THROWS(GetArraySlot(a, -1), "evt.ex.fr") == kNSErrOutOfBounds);
	EXPECT(THROWS(SetArraySlot(a, 99, NILREF), "evt.ex.fr") == kNSErrOutOfBounds);
	RefVar b(AllocateBinary(RSSYMstring, 2));
	EXPECT(THROWS(GetArraySlot(b, 0), "evt.ex.fr.type") == kNSErrNotAnArray);
	EXPECT(THROWS(AddArraySlot(b, NILREF), "evt.ex.fr.type") == kNSErrNotAnArray);

	// MapSlots and the iterator
	struct Sum { static Ref Add(RefArg tag, RefArg value, ULong total) { if (ISINT(value)) *(long*) total += RINT(value); return NILREF; } };
	long total = 0;
	MapSlots(a, Sum::Add, (ULong) &total);
	EXPECT(total == 42);
	TObjectIterator iter(a);
	int n = 0;
	for ( ; !iter.Done(); iter.Next(), n++)
		EXPECT(RINT(iter.Tag()) == n);
	EXPECT(n == 6);

	// a path into an array
	EXPECT(RINT(GetFramePath(a, MAKEINT(1))) == 42);
	EXPECT(ISNIL(GetFramePath(a, MAKEINT(100))));
	SetFramePath(a, MAKEINT(0), MAKEINT(7));
	EXPECT(RINT(GetArraySlot(a, 0)) == 7 && FrameHasPath(a, MAKEINT(0)) && !FrameHasPath(a, MAKEINT(6)));
}


static void
TestSymbols()
{
	Ref foo = Intern((char*) "foo");
	EXPECT(IsSymbol(foo) && strcmp(SymbolName(foo), "foo") == 0);
	EXPECT(Intern((char*) "foo") == foo && Intern((char*) "FOO") == foo);
	EXPECT(EQ(foo, Intern((char*) "Foo")));
	EXPECT(SymbolHash(foo) == SymbolHashFunction("foo"));
	EXPECT(foo == RSSYMfoo && !gHeap->InHeap(foo));					// a ROM symbol
	Ref fresh = Intern((char*) "quiteNewSymbol");
	EXPECT(gHeap->InHeap(fresh) && !InROMSymbolSpace(fresh) && Intern((char*) "QUITENEWSYMBOL") == fresh);
	EXPECT(!gHeap->InHeap(RSSYMstring) && InROMSymbolSpace(RSSYMstring) && Intern((char*) "string") == RSSYMstring);
	EXPECT(EQ(ClassOf(foo), RSSYMsymbol));
	EXPECT(symcmp((char*) "abc", (char*) "ABD") < 0 && symcmp((char*) "b", (char*) "A") > 0 && symcmp((char*) "x", (char*) "x") == 0);
	EXPECT(SymbolCompareLexRef(Intern((char*) "apple"), Intern((char*) "Banana")) < 0);
	EXPECT(THROWS(SymbolCompareLexRef(MAKEINT(1), foo), "evt.ex.fr") == kNSErrNotASymbol);
	EXPECT(EQ(ClassOf(Intern((char*) "sym")), RSSYMsymbol));
	// symbols of the same name made elsewhere are EQ but not identical
	RefVar copy(AllocateBinary(RefVar(kSymbolClass), 8));
	SymbolData* d = (SymbolData*) BinaryData(copy);
	d->fHash = SymbolHashFunction("foo");
	strcpy(d->fName, "foo");
	EXPECT((Ref) copy != foo && EQ(copy, foo) && !EQ(copy, Intern((char*) "bar")));
	EXPECT(FrameHasSlot(AllocateFrame(), foo) == 0);

	// many symbols: the table grows and stays consistent
	char name[32];
	for (int i = 0; i < 500; i++)
	{
		sprintf(name, "sym%d", i);
		Intern(name);
	}
	for (int i = 0; i < 500; i++)
	{
		sprintf(name, "SYM%d", i);
		Ref s = Intern(name);
		sprintf(name, "sym%d", i);
		EXPECT(strcmp(SymbolName(s), name) == 0);
	}
	EXPECT(Intern((char*) "foo") == foo);
	EXPECT(gNumSymbols >= 501 && gNumSymbols < gSymbolTableSize);
}


static void
TestFrames()
{
	RefVar f(AllocateFrame());
	EXPECT(IsFrame(f) && Length(f) == 0 && EQ(ClassOf(f), RSSYMframe));
	Ref x = Intern((char*) "x"), y = Intern((char*) "y"), z = Intern((char*) "z");
	SetFrameSlot(f, x, MAKEINT(1));
	SetFrameSlot(f, y, MAKEINT(2));
	EXPECT(Length(f) == 2 && FrameHasSlot(f, x) && FrameHasSlot(f, y) && !FrameHasSlot(f, z));
	EXPECT(RINT(GetFrameSlot(f, x)) == 1 && RINT(GetFrameSlot(f, y)) == 2 && ISNIL(GetFrameSlot(f, z)));
	SetFrameSlot(f, x, MAKEINT(10));
	EXPECT(RINT(GetFrameSlot(f, x)) == 10 && Length(f) == 2);
	EXPECT(FrameSlotPosition(f, y) == 1 && FrameSlotPosition(f, z) == -1);
	RefVar tags(CollectFrameTags(f));
	EXPECT(Length(tags) == 2 && GetArraySlot(tags, 0) == x && GetArraySlot(tags, 1) == y);

	// a class slot, and the ROM symbol as a tag
	SetFrameSlot(f, RSSYMclass, RSSYMstring);
	EXPECT(EQ(ClassOf(f), RSSYMstring) && Length(f) == 3);
	SetClass(f, Intern((char*) "myFrame"));
	EXPECT(EQ(ClassOf(f), Intern((char*) "MYFRAME")));

	// iteration in slot order with tags
	TObjectIterator iter(f);
	EXPECT(!iter.Done() && iter.Tag() == x && RINT(iter.Value()) == 10);
	iter.Next();
	EXPECT(iter.Tag() == y);
	iter.Next();
	EXPECT(EQ(iter.Tag(), RSSYMclass));
	iter.Next();
	EXPECT(iter.Done());

	// removing slots: from the middle, the first, the last, a missing one
	RemoveSlot(f, y);
	EXPECT(Length(f) == 2 && !FrameHasSlot(f, y) && RINT(GetFrameSlot(f, x)) == 10 && FrameHasSlot(f, RSSYMclass));
	RemoveSlot(f, z);
	EXPECT(Length(f) == 2);
	RemoveSlot(f, x);
	EXPECT(Length(f) == 1 && !FrameHasSlot(f, x) && FrameHasSlot(f, RSSYMclass));
	RemoveSlot(f, RSSYMclass);
	EXPECT(Length(f) == 0 && EQ(ClassOf(f), RSSYMframe));
	SetFrameSlot(f, x, MAKEINT(1));
	EXPECT(Length(f) == 1 && RINT(GetFrameSlot(f, x)) == 1);

	// past 20 slots the map is sorted, and lookups still work
	RefVar big(AllocateFrame());
	char name[16];
	for (int i = 0; i < 40; i++)
	{
		sprintf(name, "slot%02d", (i * 7) % 40);
		SetFrameSlot(big, Intern(name), MAKEINT(i));
	}
	EXPECT(Length(big) == 40);
	for (int i = 0; i < 40; i++)
	{
		sprintf(name, "slot%02d", (i * 7) % 40);
		EXPECT(RINT(GetFrameSlot(big, Intern(name))) == i);
	}
	EXPECT((MapFlags(OBJ(ObjClass(OBJ(big)))) & kMapSorted) != 0);
	RemoveSlot(big, Intern((char*) "slot07"));
	EXPECT(Length(big) == 39 && !FrameHasSlot(big, Intern((char*) "slot07")) && RINT(GetFrameSlot(big, Intern((char*) "slot14"))) == 2);
	SetFrameSlot(big, Intern((char*) "slot07"), MAKEINT(-1));
	EXPECT(RINT(GetFrameSlot(big, Intern((char*) "slot07"))) == -1 && Length(big) == 40);

	// clones share the map: adding to one leaves the other's tags alone
	RefVar g(Clone(f));
	EXPECT(ObjClass(OBJ(g)) == ObjClass(OBJ(f)) && (MapFlags(OBJ(ObjClass(OBJ(f)))) & kMapShared) != 0);
	SetFrameSlot(g, y, MAKEINT(5));
	EXPECT(Length(g) == 2 && Length(f) == 1 && !FrameHasSlot(f, y) && RINT(GetFrameSlot(g, y)) == 5 && RINT(GetFrameSlot(g, x)) == 1);
	EXPECT(ObjClass(OBJ(g)) != ObjClass(OBJ(f)));
	RemoveSlot(g, x);
	EXPECT(Length(g) == 1 && RINT(GetFrameSlot(g, y)) == 5 && !FrameHasSlot(g, x) && RINT(GetFrameSlot(f, x)) == 1);
	SetFrameSlot(g, z, MAKEINT(9));
	RemoveSlot(g, y);
	EXPECT(Length(g) == 1 && RINT(GetFrameSlot(g, z)) == 9);

	// a frame built on a map with tags
	RefVar tagArray(AllocateArray(RSSYMarray, 2));
	SetArraySlot(tagArray, 0, x);
	SetArraySlot(tagArray, 1, RSSYM_proto);
	RefVar map(AllocateMapWithTags(RefVar(NILREF), tagArray));
	EXPECT((MapFlags(OBJ(map)) & (kMapShared | kMapProto)) == (kMapShared | kMapProto));
	RefVar h(AllocateFrameWithMap(map));
	EXPECT(Length(h) == 2 && FrameHasSlot(h, x) && FrameHasSlot(h, RSSYM_proto));

	// _proto inheritance through paths and ClassOf
	RefVar proto(AllocateFrame());
	SetFrameSlot(proto, z, MAKEINT(99));
	SetFrameSlot(proto, RSSYMclass, Intern((char*) "protoClass"));
	SetFrameSlot(h, RSSYM_proto, proto);
	EXPECT(RINT(GetFramePath(h, z)) == 99 && ISNIL(GetFrameSlot(h, z)) && FrameHasPath(h, z) && !FrameHasSlot(h, z));
	EXPECT(EQ(ClassOf(h), Intern((char*) "protoClass")));
	RefVar path(AllocateArray(RSSYMpathexpr, 2));
	SetArraySlot(path, 0, RSSYM_proto);
	SetArraySlot(path, 1, z);
	EXPECT(RINT(GetFramePath(h, path)) == 99 && FrameHasPath(h, path));
	SetArraySlot(path, 1, Intern((char*) "deep"));
	RefVar deepPath(AllocateArray(RSSYMpathexpr, 3));
	SetArraySlot(deepPath, 0, Intern((char*) "a"));
	SetArraySlot(deepPath, 1, Intern((char*) "b"));
	SetArraySlot(deepPath, 2, Intern((char*) "c"));
	SetFramePath(h, deepPath, MAKEINT(123));				// makes the frames on the way
	EXPECT(RINT(GetFramePath(h, deepPath)) == 123 && IsFrame(GetFrameSlot(h, Intern((char*) "a"))));
	EXPECT(FrameHasPath(h, deepPath) && !FrameHasPath(h, path));
	EXPECT(THROWS(GetFramePath(h, MAKEINT(0)), "evt.ex.fr") == kNSErrPathFailed);
	EXPECT(THROWS(GetFramePath(h, MakeString("no")), "evt.ex.fr.type") == kNSErrNotAPathExpr);
	EXPECT(THROWS(GetFramePath(NILREF, x), "evt.ex.fr") == kNSErrPathFailed);
	EXPECT(THROWS(GetFrameSlot(tagArray, x), "evt.ex.fr.type") == kNSErrNotAFrame);
	EXPECT(THROWS(SetFrameSlot(h, MAKEINT(1), NILREF), "evt.ex.fr.type") == kNSErrNotASymbol);
	EXPECT(THROWS(SetLength(h, 3), "evt.ex.fr.type") == kNSErrUnexpectedFrame);
	EXPECT(IsPathExpr(path) && IsPathExpr(x) && IsPathExpr(MAKEINT(2)) && !IsPathExpr(tagArray));

	// read-only objects
	ObjHeader* ro = OBJ(f);
	ro->fSizeAndFlags |= kObjReadOnly;
	EXPECT(THROWS(SetFrameSlot(f, x, MAKEINT(2)), "evt.ex.fr") == kNSErrObjectReadOnly);
	EXPECT(THROWS(RemoveSlot(f, x), "evt.ex.fr") == kNSErrObjectReadOnly);
	ro->fSizeAndFlags &= ~(ULong) kObjReadOnly;
}


static void
TestClones()
{
	RefVar inner(AllocateArray(RSSYMarray, 2));
	SetArraySlot(inner, 0, MAKEINT(1));
	RefVar f(AllocateFrame());
	Ref a = Intern((char*) "a"), b = Intern((char*) "b");
	SetFrameSlot(f, a, inner);
	SetFrameSlot(f, b, MakeString("s"));
	SetArraySlot(inner, 1, f);							// a cycle

	RefVar shallow(Clone(f));
	EXPECT((Ref) shallow != (Ref) f && GetFrameSlot(shallow, a) == (Ref) inner);
	RefVar deep(DeepClone(f));
	EXPECT((Ref) deep != (Ref) f && GetFrameSlot(deep, a) != (Ref) inner);
	EXPECT(GetArraySlot(GetFrameSlot(deep, a), 1) == (Ref) deep);	// the cycle is preserved
	EXPECT(GetFrameSlot(deep, b) != GetFrameSlot(f, b) && IsString(GetFrameSlot(deep, b)));
	EXPECT(ObjClass(OBJ(deep)) == ObjClass(OBJ(f)));		// the map is shared, not cloned
	RefVar total(TotalClone(f));
	EXPECT(ObjClass(OBJ(total)) != ObjClass(OBJ(f)) && RINT(GetArraySlot(GetFrameSlot(total, a), 0)) == 1);
	EXPECT(GetArraySlot(GetFrameSlot(total, a), 1) == (Ref) total);
	EXPECT(EnsureInternal(f) == (Ref) f && EnsureInternal(MAKEINT(3)) == MAKEINT(3) && Clone(MAKEINT(4)) == MAKEINT(4));
	EXPECT(Clone(a) == a && DeepClone(NILREF) == NILREF);
}


// a DIY marker/updater keeping a ref of its own alive
struct DIYHolder
{
	Ref	fRef;
	static void Mark(void* refCon)		{ DIYGCMark(((DIYHolder*) refCon)->fRef); }
	static void Update(void* refCon)	{ ((DIYHolder*) refCon)->fRef = DIYGCUpdate(((DIYHolder*) refCon)->fRef); }
};
static int gGCProcCalls = 0;
static void CountGC(void* refCon)	{ (*(int*) refCon)++; }


static void
TestGC()
{
	ULong freeBefore, largest;
	Statistics(&freeBefore, &largest);
	RefVar keep(AllocateArray(RSSYMarray, 10));
	RefVar sym(Intern((char*) "keeper"));
	SetArraySlot(keep, 0, sym);
	Ref rawArray = NILREF;
	{
		// garbage: nothing refers to these after the handles go
		for (int i = 0; i < 200; i++)
		{
			RefVar junk(AllocateBinary(RSSYMstring, 100));
			if (i == 100)
				rawArray = AllocateArray(RSSYMarray, 5);
		}
	}
	RefVar tail(AllocateFrame());
	SetFrameSlot(tail, Intern((char*) "k"), keep);
	SetArraySlot(keep, 1, tail);

	// a locked object stays put, a DIY holder's ref follows its object, a
	// GC proc is called, a weak array drops the dead
	RefVar locked(AllocateBinary(RSSYMstring, 40));
	LockRef(locked);
	ObjHeader* lockedBefore = OBJ(locked);
	DIYHolder holder;
	holder.fRef = AllocateArray(RSSYMarray, 1);
	SetArraySlot(RefVar(holder.fRef), 0, MAKEINT(77));
	DIYGCRegister(&holder, DIYHolder::Mark, DIYHolder::Update);
	GCRegister(&gGCProcCalls, CountGC);
	RefVar weak(AllocateArray(RefVar(kWeakArrayClass), 2));
	SetArraySlot(weak, 0, keep);
	SetArraySlot(weak, 1, rawArray);
	Ref symbolOnly = Intern((char*) "unreferenced");				// only the symbol table has it: GCTWA drops it
	EXPECT(FrameHasSlot(tail, Intern((char*) "k")));
	Ref tailBefore = tail;

	GC();

	EXPECT(gGCProcCalls == 1);
	EXPECT(OBJ(locked) == lockedBefore);
	EXPECT(Length(keep) == 10 && GetArraySlot(keep, 0) == (Ref) sym && GetArraySlot(keep, 1) == (Ref) tail);
	EXPECT((Ref) tail != tailBefore);									// it moved down over the garbage
	EXPECT(GetFrameSlot(tail, Intern((char*) "k")) == (Ref) keep);
	EXPECT(RINT(GetArraySlot(RefVar(holder.fRef), 0)) == 77);
	EXPECT(GetArraySlot(weak, 0) == (Ref) keep && ISNIL(GetArraySlot(weak, 1)));
	EXPECT(EQ(ClassOf(weak), RSSYM_weakarray));
	ULong freeAfter;
	Statistics(&freeAfter, &largest);
	EXPECT(freeAfter > freeBefore - 2000);
	EXPECT(Intern((char*) "keeper") == (Ref) sym);
	EXPECT(Intern((char*) "unreferenced") != symbolOnly);		// a new symbol object now
	UnlockRef(locked);
	DIYGCUnregister(&holder);
	GCUnregister(&gGCProcCalls);
	GC();
	EXPECT(gGCProcCalls == 1);

	// a resize that moves leaves a forwarding object; refs follow
	RefVar arr(AllocateArray(RSSYMarray, 2));
	RefVar after(AllocateArray(RSSYMarray, 2));			// blocks arr in place
	Ref before = arr;
	SetLength(arr, 200);
	EXPECT((Ref) arr == before);							// the handle still names the old address
	EXPECT((ObjFlags(PTRVALUE(before)) & kObjForward) != 0 && Length(arr) == 200);
	SetArraySlot(arr, 150, MAKEINT(3));
	EXPECT(RINT(GetArraySlot(arr, 150)) == 3 && EQ(arr, before));
	GC();
	EXPECT((Ref) arr != before && Length(arr) == 200 && RINT(GetArraySlot(arr, 150)) == 3);

	// ReplaceObject
	RefVar old(AllocateArray(RSSYMarray, 1));
	RefVar replacement(AllocateArray(RSSYMarray, 3));
	ReplaceObject(old, replacement);
	EXPECT(Length(old) == 3 && EQ(old, replacement));
	EXPECT(THROWS(ReplaceObject(RefVar(MAKEINT(1)), replacement), "evt.ex.fr.type") == kNSErrNotAPointer);

	// the RefHandle table grows when RefVars outnumber it
	{
		const int kMany = 700;
		RefVar* many = new RefVar[kMany];
		for (int i = 0; i < kMany; i++)
			many[i] = MAKEINT(i);
		EXPECT(RefHandleTableCount(gHeap->fRefHandleTable) > 256);
		for (int i = 0; i < kMany; i++)
			EXPECT(RINT(many[i]) == i);
		delete[] many;
	}
	EXPECT(Length(keep) == 10 && GetArraySlot(keep, 1) == (Ref) tail);

	// a full heap: out of memory, then the space is back
	{
		long code = 0;
		RefVar hogs(AllocateArray(RSSYMarray, 0));
		newton_try
		{
			for (;;)
				AddArraySlot(hogs, RefVar(AllocateBinary(RSSYMstring, 0x8000)));
		}
		newton_catch(exOutOfMemory)
		{
			code = (long) (Long) _info.exception.data;
		}
		end_try;
		EXPECT(code == kNSErrOutOfObjectMemory);
		EXPECT(Length(hogs) > 4 && Length(keep) == 10);
		hogs = NILREF;
	}
	GC();
	RefVar big(AllocateBinary(RSSYMstring, 0x10000));
	EXPECT(Length(big) == 0x10000);
	EXPECT(Length(keep) == 10);

	// a locked object cannot be resized out of place
	LockRef(locked);
	EXPECT(THROWS(SetLength(locked, 0x20000), "evt.ex.fr") == kNSErrCouldntResizeLockedObject);
	UnlockRef(locked);
	SetLength(locked, 0x20000);
	EXPECT(Length(locked) == 0x20000);
}


static void
TestExceptions()
{
	EXPECT(THROWS(ObjectPtr(MAKEINT(1)), "evt.ex.fr") == kNSErrObjectPointerOfNonPtr);
	EXPECT(ThrowsData([]() { ObjectPtr(kDeclawedRef); }, "evt.ex.fr") == kNSErrBadPackageRef);
	EXPECT(THROWS(ObjectPtr(MAKEMAGICPTR(0x5000)), "evt.ex.fr") == kNSErrBadMagicPointer);
	EXPECT(EQ(ClassOf(MAKEIMMED(kImmedReserved, 1)), RSSYMweird_immediate));
	EXPECT(THROWS(RINT(NILREF), "evt.ex.fr.type") == kNSErrNotAnInteger);
	EXPECT(THROWS(RCHAR(NILREF), "evt.ex.fr.type") == kNSErrNotACharacter);
	EXPECT(THROWS(Slots(MakeString("x")), "evt.ex.fr.type") == kNSErrNotAnArray);
	EXPECT(ThrowsData([]() { ThrowRefException((ExceptionName) "evt.ex.other", RefVar(NILREF)); }, "evt.ex.fr") != 0);
	// the thrown frame carries the value
	newton_try
	{
		RefVar f(AllocateFrame());
		GetFrameSlot(f, MAKEINT(7));
	}
	newton_catch(exBadTypeWithFrameData)
	{
		RefStruct* data = (RefStruct*) _info.exception.data;
		EXPECT(RINT(GetFrameSlot(*data, RSSYMvalue)) == 7);
	}
	end_try;
	// RefVars made inside a failed try are freed by ClearRefHandles
	long freeIndex = gHeap->fFreeHandleIndex;
	IncrementCurrentStackPos();
	newton_try
	{
		RefVar a(MAKEINT(1)), b(MAKEINT(2));
		Throw((ExceptionName) "evt.ex.test", nil, nil);
	}
	newton_catch_all
	{
		ClearRefHandles();
	}
	end_try;
	DecrementCurrentStackPos();
	EXPECT(gHeap->fFreeHandleIndex == freeIndex || true);		// (the chain is reordered, but no handle is lost:)
	long n = 0;
	for (long i = gHeap->fFreeHandleIndex; i != -1; i = RVALUE(RefHandleTableEntries(gHeap->fRefHandleTable)[i].ref))
		n++;
	long inUse = 0;
	RefHandle* handles = RefHandleTableEntries(gHeap->fRefHandleTable);
	for (long i = 0; i < RefHandleTableCount(gHeap->fRefHandleTable); i++)
		if (handles[i].stackPos != MAKEINT(-1))
			inUse++;
	EXPECT(n + inUse == RefHandleTableCount(gHeap->fRefHandleTable));
}


// Without the ROM's objects the interpreter still starts (InitObjects
// starts it): the bound natives get function objects in gFunctionFrame
// and the frequently called functions are found among them.
static void
TestHostNatives()
{
	EXPECT(gInterpreter != nil && gROMBuiltinFunctions == NILREF);
	EXPECT(Length(gFreqFuncs) == kNumFreqFuncs && IsFunction(GetArraySlot(gFreqFuncs, kFFLength)));
	RefVar arr(AllocateArray(RSSYMarray, 3));
	EXPECT(RINT(NSCallGlobalFn(RefVar(Intern((char*) "Length")), arr)) == 3);
	EXPECT(RINT(NSCallGlobalFn(RefVar(Intern((char*) "Max")), RefVar(MAKEINT(2)), RefVar(MAKEINT(5)))) == 5);
	EXPECT(gInterpreter->ValuePosition() == -1);
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x80000;
	InitObjects();
	EXPECT(gHeap != nil && gRSSymbolCount == 1765 && IsSymbol(RSSYMarray) && IsFrame(gVarFrame));
	TestRefs();
	TestBinaries();
	TestArrays();
	TestSymbols();
	TestFrames();
	TestClones();
	TestGC();
	TestExceptions();
	TestHostNatives();
	if (failures == 0)
		printf("test_Frames: all passed\n");
	else
		printf("test_Frames: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
