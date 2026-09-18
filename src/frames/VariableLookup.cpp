/*
	File:		frames/VariableLookup.cpp

	Contains:	Variable lookup for the interpreter (Interpreter.h): the
				TICache lookup caches, XGetVariable/GetVariable (a variable
				through the locals, the receiver's _proto and _parent chains),
				FindImplementor/FindProtoImplementor (the frame implementing
				a message) and SetVariable/SetVariableOrGlobal.

	Reconstructed from the MP2x00 US ROM (0x002ff53c-0x003015fc); each
	function cites its origin.  A frame in the ROM or in a package (an
	address below 0x03800000 or in 0x60000000-0x67ffffff) gets its lookups
	cached in gROProtoCache, which survives more; here that is the ROM
	object area (InROMObjectArea).
*/

#include "Interpreter.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"

TICache*	gGetVarCache = nil;
TICache*	gProtoCache = nil;
TICache*	gROProtoCache = nil;
TICache*	gFindImpCache = nil;


/* -------------------------------------------------------------------------------
	TICache
	Entries of a context (frame) and symbol: the frame the symbol's slot was
	found in and its index, or that it was not found (fFoundIn 0).  Indexed
	by the context's address and the symbol's hash.
------------------------------------------------------------------------------- */

// ROM 0x002ff53c __ct__7TICacheFl
TICache::TICache(long bits)
{
	fSize = 1 << bits;
	fShift = 32 - bits;
	fMask = fSize - 1;
	fEntries = new Entry[fSize];
	if (fEntries == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	Clear();
	DIYGCRegister(this, DIYMarkTICache, DIYUpdateTICache);
}


// ROM 0x002ff5e0 __dt__7TICacheFv
TICache::~TICache()
{
	DIYGCUnregister(this);
	delete[] fEntries;
}


inline TICache::Entry*
CacheEntry(TICache* cache, Ref context, ULong32 hash)
{
	return &cache->fEntries[(cache->fMask & (context >> 4)) ^ (hash >> cache->fShift)];
}


// ROM 0x00301584 Clear__7TICacheFv
void
TICache::Clear(void)
{
	for (long i = 0; i < fSize; i++)
		fEntries[i].fContext = 0;
}


// ROM 0x002ff650 ClearSymbol__7TICacheFlT1
void
TICache::ClearSymbol(Ref sym, ULong32 hash)
{
	for (long i = 0; i < fSize; i++)
	{
		Entry* e = &fEntries[i];
		if (e->fContext != 0 && e->fHash == hash && UnsafeSymbolEqual(e->fSymbol, sym, hash))
			e->fContext = 0;
	}
}


// ROM 0x002ff71c ClearFrame__7TICacheFl
// Drop the entries found in this frame.
void
TICache::ClearFrame(Ref frame)
{
	for (long i = 0; i < fSize; i++)
	{
		Entry* e = &fEntries[i];
		if (e->fContext != 0 && EQRef(e->fFoundIn, frame))
			e->fContext = 0;
	}
}


// ROM 0x00301390 Lookup__7TICacheFlT1PlN33
Boolean
TICache::Lookup(Ref context, Ref sym, Ref* foundIn, Ref* value, long* exists, long* index)
{
	ULong32 hash = ObjSymbol(PTRVALUE(sym))->fHash;
	Entry* e = CacheEntry(this, context, hash);
	if (e->fHash != hash || e->fContext == 0 || !EQRef(e->fContext, context) || !UnsafeSymbolEqual(e->fSymbol, sym, hash))
		return false;
	if (e->fFoundIn == 0)
	{
		*exists = 0;
		*value = NILREF;
	}
	else
	{
		*exists = 1;
		*foundIn = e->fFoundIn;
		*value = ObjArraySlots(OBJ(e->fFoundIn))[e->fIndex];
		*index = e->fIndex;
	}
	return true;
}


// ROM 0x0030146c LookupValue__7TICacheFlT1PlT3
Boolean
TICache::LookupValue(Ref context, Ref sym, Ref* value, long* exists)
{
	ULong32 hash = ObjSymbol(PTRVALUE(sym))->fHash;
	Entry* e = CacheEntry(this, context, hash);
	if (e->fHash != hash || e->fContext == 0 || !EQRef(e->fContext, context) || !UnsafeSymbolEqual(e->fSymbol, sym, hash))
		return false;
	if (e->fFoundIn == 0)
	{
		*value = NILREF;
		*exists = 0;
	}
	else
	{
		*exists = 1;
		*value = ObjArraySlots(OBJ(e->fFoundIn))[e->fIndex];
	}
	return true;
}


// ROM 0x00301528 Insert__7TICacheFlN31
// A fault block is never a context here (its slots may go away).
void
TICache::Insert(Ref context, Ref sym, Ref foundIn, long index)
{
	if (IsFaultBlock(context))
		return;
	ULong32 hash = ObjSymbol(PTRVALUE(sym))->fHash;
	Entry* e = CacheEntry(this, context, hash);
	e->fContext = context;
	e->fSymbol = sym;
	e->fHash = hash;
	e->fFoundIn = foundIn;
	e->fIndex = index;
}


// ROM 0x00301300 Mark__7TICacheFv
// Nothing: a cache keeps nothing alive.
void
TICache::Mark(void)
{ }


// ROM 0x00301308 Update__7TICacheFv
// An entry whose context, symbol or frame went (NILREF now) is dropped.
void
TICache::Update(void)
{
	for (long i = 0; i < fSize; i++)
	{
		Entry* e = &fEntries[i];
		if (e->fContext == 0)
			continue;
		e->fContext = DIYGCUpdate(e->fContext);
		if (e->fContext == NILREF)
			e->fContext = 0;
		e->fSymbol = DIYGCUpdate(e->fSymbol);
		if (e->fSymbol == NILREF)
			e->fContext = 0;
		e->fFoundIn = DIYGCUpdate(e->fFoundIn);
		if (e->fFoundIn == NILREF)
			e->fContext = 0;
	}
}


// ROM 0x00300610 DIYMarkTICache__7TICacheSFPv
void
TICache::DIYMarkTICache(void* cache)
{
	((TICache*) cache)->Mark();
}


// ROM 0x00301304 DIYUpdateTICache__7TICacheSFPv
void
TICache::DIYUpdateTICache(void* cache)
{
	((TICache*) cache)->Update();
}


// ROM 0x002ff618 ICacheClear__Fv
void
ICacheClear(void)
{
	gGetVarCache->Clear();
	gFindImpCache->Clear();
	gProtoCache->Clear();
	gROProtoCache->Clear();
}


// ROM 0x002ff6cc ICacheClearSymbol__FlT1
void
ICacheClearSymbol(Ref sym, ULong32 hash)
{
	gGetVarCache->ClearSymbol(sym, hash);
	gFindImpCache->ClearSymbol(sym, hash);
	gProtoCache->ClearSymbol(sym, hash);
}


// ROM 0x002ff794 ICacheClearFrame__Fl
void
ICacheClearFrame(Ref frame)
{
	gGetVarCache->ClearFrame(frame);
	gFindImpCache->ClearFrame(frame);
	gProtoCache->ClearFrame(frame);
}


// ROM 0x002ff7d4 InitICache__Fv
void
InitICache(void)
{
	gGetVarCache = new TICache(5);
	gProtoCache = new TICache(6);
	gROProtoCache = new TICache(6);
	gFindImpCache = new TICache(6);
}


/* -------------------------------------------------------------------------------
	Lookup
------------------------------------------------------------------------------- */

// a frame whose lookups are cached in the read-only proto cache
static inline Boolean
IsROFrame(Ref frame)
{
	return InROMObjectArea(frame);
}


// ROM 0x002ff82c XGetVariable__FRC6RefVarT1Pli
// The variable name seen from context: with lookupLocals, the locals
// frames (the _nextArgFrame chain) first; then, from the context's
// _parent (the receiver), each frame's _proto chain, and on to that
// frame's _parent.  *exists tells whether it was found; the result is
// cached.
Ref
XGetVariable(RefArg context, RefArg name, long* exists, int lookupLocals)
{
	if ((Ref) context == NILREF)
	{
		*exists = 0;
		return NILREF;
	}
	Ref value;
	if (gGetVarCache->LookupValue(context, name, &value, exists))
		return value;
	RefVar current(context);
	RefVar map;
	RefVar chainStart(context);
	long index;
	if (lookupLocals)
	{
		do {
			map = ObjClass(OBJ(current));
			index = FindOffset(map, name);
			if (index != -1)
			{
				*exists = 1;
				gGetVarCache->Insert(context, name, current, index);
				return ObjArraySlots(OBJ(current))[index];
			}
			long found;
			current = UnsafeGetFrameSlot(current, RSSYM_nextargframe, &found);
		} while ((Ref) current != NILREF && Length(current) > 0);
		long found;
		current = UnsafeGetFrameSlot(chainStart, RSSYM_parent, &found);
		chainStart = current;
	}
	Ref foundIn;
	if ((Ref) current != NILREF && gProtoCache->Lookup(current, name, &foundIn, &value, exists, &index))
	{
		if (*exists)
		{
			gGetVarCache->Insert(context, name, foundIn, index);
			return value;
		}
		// this chain has it not: on to its parent
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
		{
			*exists = 0;
			gGetVarCache->Insert(context, name, 0, 0);
			return NILREF;
		}
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	Boolean firstChain = true;
	while ((Ref) current != NILREF)
	{
		Ref roFrame = 0;								// the ROM frame whose lookup gROProtoCache answers
		do {
			ObjHeader* o = OBJ(current);
			if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
				ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
			if (roFrame == 0 && IsROFrame(current))
			{
				roFrame = current;
				if (gROProtoCache->Lookup(roFrame, name, &foundIn, &value, exists, &index))
				{
					if (*exists)
					{
						gGetVarCache->Insert(context, name, foundIn, index);
						if (firstChain)
							gProtoCache->Insert(chainStart, name, foundIn, index);
						return value;
					}
					map = ObjClass(o);
					goto nextProto;
				}
			}
			map = ObjClass(o);
			index = FindOffset(map, name);
			if (index != -1)
			{
				*exists = 1;
				gGetVarCache->Insert(context, name, current, index);
				if (firstChain)
					gProtoCache->Insert(chainStart, name, current, index);
				if (roFrame != 0)
					gROProtoCache->Insert(roFrame, name, current, index);
				return ObjArraySlots(OBJ(current))[index];
			}
		nextProto:
			index = FindOffset(map, RSSYM_proto);
			if (index == -1)
				break;
			current = ObjArraySlots(OBJ(current))[index];
		} while ((Ref) current != NILREF);
		if (firstChain)
		{
			gProtoCache->Insert(chainStart, name, 0, 0);
			firstChain = false;
		}
		if (roFrame != 0)
			gROProtoCache->Insert(roFrame, name, 0, 0);
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	*exists = 0;
	gGetVarCache->Insert(context, name, 0, 0);
	return NILREF;
}


// ROM 0x00300930 GetVariable__FRC6RefVarT1Pli
// XGetVariable without the caches (the tracing version).
Ref
GetVariable(RefArg context, RefArg name, long* exists, int lookupLocals)
{
	if ((Ref) context == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrNilContext, name);
	if (exists != nil)
		*exists = 1;
	RefVar current(context);
	RefVar proto;
	while ((Ref) current != NILREF)
	{
		if (FrameHasSlotRef(current, name))
		{
			if (gInterpreter->fTraceLevel > 1)
				gInterpreter->TraceGet(context, current, name);
			return GetFrameSlotRef(current, name);
		}
		proto = GetFrameSlotRef(current, lookupLocals ? RSSYM_nextargframe : RSSYM_proto);
		while ((Ref) proto != NILREF)
		{
			if (FrameHasSlotRef(proto, name))
			{
				if (gInterpreter->fTraceLevel > 1)
					gInterpreter->TraceGet(context, proto, name);
				return GetFrameSlotRef(proto, name);
			}
			proto = GetFrameSlotRef(proto, lookupLocals ? RSSYM_nextargframe : RSSYM_proto);
		}
		lookupLocals = 0;
		current = GetFrameSlotRef(current, RSSYM_parent);
	}
	if (gInterpreter->fTraceLevel > 1)
		gInterpreter->TraceGet(context, context, name);
	if (exists != nil)
		*exists = 0;
	return NILREF;
}


// ROM 0x002ffe78 XFindImplementor__FRC6RefVarT1P6RefVarT3
// The frame implementing message name for receiver - up its _proto chain,
// then its _parent's - and the method; cached.
Boolean
XFindImplementor(RefArg receiver, RefArg name, RefVar* implementor, RefVar* value)
{
	if ((Ref) receiver == NILREF)
		return false;
	long exists, index;
	if (gFindImpCache->Lookup(receiver, name, &implementor->h->ref, &value->h->ref, &exists, &index))
		return exists != 0;
	RefVar current(receiver);
	RefVar map;
	RefVar chainStart(receiver);
	if (gProtoCache->Lookup(current, name, &implementor->h->ref, &value->h->ref, &exists, &index))
	{
		if (exists)
		{
			gFindImpCache->Insert(receiver, name, *implementor, index);
			return true;
		}
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
		{
			gFindImpCache->Insert(receiver, name, 0, 0);
			return false;
		}
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	Boolean firstChain = true;
	while ((Ref) current != NILREF)
	{
		Ref roFrame = 0;
		do {
			ObjHeader* o = OBJ(current);
			if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
				ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
			if (roFrame == 0 && IsROFrame(current))
			{
				roFrame = current;
				if (gROProtoCache->Lookup(roFrame, name, &implementor->h->ref, &value->h->ref, &exists, &index))
				{
					if (exists)
					{
						if (firstChain)
							gProtoCache->Insert(chainStart, name, *implementor, index);
						gFindImpCache->Insert(receiver, name, *implementor, index);
						return true;
					}
					map = ObjClass(o);
					goto nextProto;
				}
			}
			map = ObjClass(o);
			index = FindOffset(map, name);
			if (index != -1)
			{
				*implementor = current;
				*value = ObjArraySlots(OBJ(current))[index];
				if (roFrame != 0)
					gROProtoCache->Insert(roFrame, name, current, index);
				if (firstChain)
					gProtoCache->Insert(chainStart, name, current, index);
				gFindImpCache->Insert(receiver, name, current, index);
				return true;
			}
		nextProto:
			index = FindOffset(map, RSSYM_proto);
			if (index == -1)
				break;
			current = ObjArraySlots(OBJ(current))[index];
		} while ((Ref) current != NILREF);
		if (firstChain)
		{
			gProtoCache->Insert(chainStart, name, 0, 0);
			firstChain = false;
		}
		if (roFrame != 0)
			gROProtoCache->Insert(roFrame, name, 0, 0);
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	gFindImpCache->Insert(receiver, name, 0, 0);
	return false;
}


// ROM 0x003003c8 XFindProtoImplementor__FRC6RefVarT1P6RefVarT3
// The frame implementing name among the _protos of receiver (not receiver
// itself), and the method.
Boolean
XFindProtoImplementor(RefArg receiver, RefArg name, RefVar* implementor, RefVar* value)
{
	if ((Ref) receiver == NILREF)
		return false;
	RefVar current(receiver);
	ObjHeader* o = OBJ(current);
	if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
		ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
	RefVar map(ObjClass(o));
	Ref roFrame = 0;
	long index;
	for (;;)
	{
		index = FindOffset(map, RSSYM_proto);
		if (index == -1)
		{
			if (roFrame != 0)
				gROProtoCache->Insert(roFrame, name, 0, 0);
			return false;
		}
		current = ObjArraySlots(OBJ(current))[index];
		o = OBJ(current);
		if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
			ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
		if (roFrame == 0 && IsROFrame(current))
		{
			roFrame = current;
			long exists;
			if (gROProtoCache->Lookup(roFrame, name, &implementor->h->ref, &value->h->ref, &exists, &index))
				return exists != 0;
		}
		map = ObjClass(o);
		index = FindOffset(map, name);
		if (index != -1)
			break;
	}
	*implementor = current;
	*value = ObjArraySlots(OBJ(current))[index];
	if (roFrame != 0)
		gROProtoCache->Insert(roFrame, name, current, index);
	return true;
}


// ROM 0x00300614 FindProtoImplementor__FRC6RefVarT1
// The frame in receiver's _proto chain (receiver included) with a slot
// name, NILREF if none; cached.
Ref
FindProtoImplementor(RefArg receiver, RefArg name)
{
	if ((Ref) receiver == NILREF)
		return NILREF;
	Ref foundIn, value;
	long exists, index;
	if (gProtoCache->Lookup(receiver, name, &foundIn, &value, &exists, &index))
		return exists ? foundIn : NILREF;
	RefVar current(receiver);
	RefVar map;
	Ref roFrame = 0;
	while ((Ref) current != NILREF)
	{
		ObjHeader* o = OBJ(current);
		if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
			ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
		if (roFrame == 0 && IsROFrame(current))
		{
			roFrame = current;
			if (gROProtoCache->Lookup(roFrame, name, &foundIn, &value, &exists, &index))
				return exists ? foundIn : NILREF;
		}
		map = ObjClass(o);
		index = FindOffset(map, name);
		if (index != -1)
		{
			if (roFrame != 0)
				gROProtoCache->Insert(roFrame, name, current, index);
			gProtoCache->Insert(receiver, name, current, index);
			return current;
		}
		index = FindOffset(map, RSSYM_proto);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(current))[index];
	}
	if (roFrame != 0)
		gROProtoCache->Insert(roFrame, name, 0, 0);
	gProtoCache->Insert(receiver, name, 0, 0);
	return NILREF;
}


// ROM 0x003008ac FindImplementor__FRC6RefVarT1
Ref
FindImplementor(RefArg receiver, RefArg name)
{
	RefVar implementor, value;
	if (!XFindImplementor(receiver, name, &implementor, &value))
		return NILREF;
	return implementor;
}


/* -------------------------------------------------------------------------------
	Assignment
------------------------------------------------------------------------------- */

// ROM 0x00300e48 SetVariableOrGlobal__FRC6RefVarN21l
// Set the variable name seen from context (as XGetVariable finds it: with
// kSetVarLookupLocals through the locals first, then the receiver's
// chains).  A slot found in a _proto is set in the frame at the foot of
// that _proto chain (the proto is not written).  Not found: an existing
// global when kSetVarSetGlobal; else a slot of the context when
// kSetVarSetInContext, or a new global when kSetVarMakeGlobal, else
// nothing (answers false).
Boolean
SetVariableOrGlobal(RefArg context, RefArg name, RefArg value, long flags)
{
	RefVar current(context);
	RefVar map;
	RefVar chainStart(context);
	long index;
	if (flags & kSetVarLookupLocals)
	{
		do {
			map = ObjClass(OBJ(current));
			index = FindOffset(map, name);
			if (index != -1)
			{
				if (gInterpreter->fTraceLevel > 1)
					gInterpreter->TraceSet(context, current, name, value);
				ObjArraySlots(OBJ(current))[index] = value;
				return true;
			}
			long found;
			current = UnsafeGetFrameSlot(current, RSSYM_nextargframe, &found);
		} while ((Ref) current != NILREF);
		long found;
		current = UnsafeGetFrameSlot(chainStart, RSSYM_parent, &found);
		chainStart = current;
	}
	while ((Ref) current != NILREF)
	{
		do {
			ObjHeader* o = OBJ(current);
			if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
				ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
			map = ObjClass(o);
			index = FindOffset(map, name);
			if (index != -1)
			{
				if (gInterpreter->fTraceLevel > 1)
					gInterpreter->TraceSet(context, current, name, value);
				if ((Ref) chainStart != (Ref) current)
				{
					SetFrameSlot(chainStart, name, value);				// the slot is a proto's: the frame gets its own
					return true;
				}
				ObjArraySlots(OBJ(current))[index] = value;
				return true;
			}
			index = FindOffset(map, RSSYM_proto);
			if (index == -1)
				break;
			current = ObjArraySlots(OBJ(current))[index];
		} while ((Ref) current != NILREF);
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	if ((flags & kSetVarSetGlobal) && FrameHasSlotRef(gVarFrame, name))
	{
		RefVar vars(gVarFrame);
		if (gInterpreter->fTraceLevel > 1)
			gInterpreter->TraceSet(vars, vars, name, value);
		SetFrameSlot(vars, name, value);
		return true;
	}
	if (flags & kSetVarSetInContext)
	{
		if (gInterpreter->fTraceLevel > 1)
			gInterpreter->TraceSet(context, context, name, value);
		SetFrameSlot(context, name, value);
		return true;
	}
	if (flags & kSetVarMakeGlobal)
	{
		RefVar vars(gVarFrame);
		if (gInterpreter->fTraceLevel > 1)
			gInterpreter->TraceSet(vars, vars, name, value);
		SetFrameSlot(vars, name, value);
		return true;
	}
	return false;
}


// ROM 0x003012f8 SetVariable__FRC6RefVarN21
// Set the variable through the receiver's chains (no locals, no globals);
// a slot of the context when not found.
Boolean
SetVariable(RefArg context, RefArg name, RefArg value)
{
	RefVar current(context);
	RefVar map;
	RefVar chainStart(context);
	long index;
	while ((Ref) current != NILREF)
	{
		do {
			ObjHeader* o = OBJ(current);
			if ((ObjFlags(o) & (kObjSlotted | kObjFrame)) != (kObjSlotted | kObjFrame))
				ThrowBadTypeWithFrameData(kNSErrNotAFrame, current);
			map = ObjClass(o);
			index = FindOffset(map, name);
			if (index != -1)
			{
				if (gInterpreter->fTraceLevel > 1)
					gInterpreter->TraceSet(context, current, name, value);
				if ((Ref) chainStart != (Ref) current)
				{
					SetFrameSlot(chainStart, name, value);
					return true;
				}
				ObjArraySlots(OBJ(current))[index] = value;
				return true;
			}
			index = FindOffset(map, RSSYM_proto);
			if (index == -1)
				break;
			current = ObjArraySlots(OBJ(current))[index];
		} while ((Ref) current != NILREF);
		map = ObjClass(OBJ(chainStart));
		index = FindOffset(map, RSSYM_parent);
		if (index == -1)
			break;
		current = ObjArraySlots(OBJ(chainStart))[index];
		chainStart = current;
	}
	if (gInterpreter->fTraceLevel > 1)
		gInterpreter->TraceSet(context, context, name, value);
	SetFrameSlot(context, name, value);
	return true;
}
