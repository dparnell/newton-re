/*
	File:		stores/UnionSoups.cpp

	Contains:	Union soups: the soup of a name across every registered
				store.  A union soup frame is a clone of unionSoupPrototype
				(class 'UnionSoup) with soupList (the stores' soups of that
				name, in gStores order), theName and cursors (an entry
				cache: the cursors over it); GetUnionSoup makes one from the
				stores that have the soup, GetUnionSoupAlways one even when
				none has, and gUnionSoups keeps them.  AddToUnionSoup and
				RemoveFromUnionSoup follow the stores and soups as they come
				and go, telling the cursors.

				The union soup's methods: the natives here (AddIndex,
				RemoveIndex, NaughtyFlush, GetSize, GetName, Query, collect),
				and the ROM's NewtonScript methods (Add, flush, GetSoupList,
				GetMember, AddToStore, AddToDefaultStore, HasTags) re-expressed
				as NewtonScript source, compiled when the host builds the
				prototype, together with the ROM's NewtonScript built-ins the
				union soups rest on (GetSoupDef, CreateUSoupMember,
				CreateSoupFromSoupDef, RegUnionSoup, UnRegUnionSoup, ...).
				The tag methods (AddTags, GetTags, RemoveTags, ModifyTag,
				the native HasTags) go to every soup (Tags.h).

	Reconstructed from the MP2x00 US ROM, 0x0035f030-0x00335824 and the
	unionSoupPrototype's objects (nsfunctions.py --object unionsoupprototype).
*/

#include "Soups.h"
#include "Cursors.h"
#include "Tags.h"
#include "StoreObject.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "NativeFunctions.h"
#include "NSErrors.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "ObjectHeap.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include <new>

extern const ExceptionName exStoreError;

Ref	FSetUnion(RefArg rcvr, RefArg array1, RefArg array2, RefArg uniqueOnly);


/*------------------------------------------------------------------------------
	S o r t   t a b l e s
	The soups of a union soup must sort each index by the same table; the
	host has no sort tables (every sortID is 0), so these find nothing.
------------------------------------------------------------------------------*/

// ROM 0x0035f030 CheckSoupsSortTables__FRC6RefVarT1
// The path of an index of soup1's persistent frame that soup2's index of
// the same path sorts by a different table; nil when they agree.
Ref
CheckSoupsSortTables(RefArg soupPersistent1, RefArg soupPersistent2)
{
	RefVar indexes(GetFrameSlotRef(soupPersistent1, RSSYMindexes));
	RefVar path;
	RefVar desc1;
	RefVar desc2;
	for (long i = Length(indexes) - 1; i >= 0; i--)
	{
		desc1 = GetArraySlotRef(indexes, i);
		path = GetFrameSlotRef(desc1, RSSYMpath);
		desc2 = IndexPathToIndexDesc(soupPersistent2, path, nil);
		if ((Ref) desc2 == NILREF)
			continue;
		if (!EQRef(GetFrameSlotRef(desc1, RSSYMsortid), GetFrameSlotRef(desc2, RSSYMsortid)))
			return path;
	}
	return NILREF;
}


// ROM 0x00352acc StoreHasSortTables__FRC6RefVar
// Whether the store's persistent frame keeps any sorting tables.
Boolean
StoreHasSortTables(RefArg storeObject)
{
	RefVar persistent(GetFrameSlotRef(storeObject, RSSYM_proto));
	Ref tables = GetFrameSlotRef(persistent, RSSYMsorttables);
	return tables != NILREF && Length(tables) != 0;
}


// ROM 0x00334fa0 (unnamed)
// The union soup's soups checked pairwise: kNSErrSortTablesMismatch when
// two sort an index differently, else 0.  (The ROM's static function has
// no symbol; the name is ours.)
static long
CheckUnionSortTables(RefArg unionSoup)
{
	RefVar soupList(GetFrameSlotRef(unionSoup, RSSYMsouplist));
	RefVar persistent1;
	RefVar persistent2;
	for (long i = Length(soupList) - 1; i > 0; i--)
	{
		persistent1 = GetFrameSlotRef(RefVar(GetArraySlotRef(soupList, i - 1)), RSSYM_proto);
		persistent2 = GetFrameSlotRef(RefVar(GetArraySlotRef(soupList, i)), RSSYM_proto);
		if (CheckSoupsSortTables(persistent2, persistent1) != NILREF)
			return kNSErrSortTablesMismatch;
	}
	return 0;
}


// ROM 0x0035f188 CheckUnionStopFn__FP4SKeyT1Pv
// For each soup name of the checked store: the other stores' soups of that
// name whose sort tables differ put the name in the result array.
struct CheckUnionInfo
{
	TStoreWrapper*	fWrapper;		// +0x00  the checked store
	long			fNumStores;		// +0x04
	TSoupIndex*		fIndexes;		// +0x08  each store's soup name index
	long			fStoreIndex;	// +0x0c  the checked store's
	Ref*			fResult;		// +0x10  the array of names (made on the first)
};

static int
CheckUnionStopFn(SKey* key, SKey* data, void* refCon)
{
	CheckUnionInfo* info = (CheckUnionInfo*) refCon;
	RefVar persistent;
	RefVar otherPersistent;
	for (long i = info->fNumStores - 1; i >= 0; i--)
	{
		if (i == info->fStoreIndex)
			continue;
		TSoupIndex* index = &info->fIndexes[i];
		SKey otherData;
		if (index->Find(key, nil, &otherData, true) != kIndexOK)
			continue;
		if ((Ref) persistent == NILREF)
			persistent = LoadPermObject(info->fWrapper, (PSSId) (long) *data, nil);
		otherPersistent = LoadPermObject(index->fStoreWrapper, (PSSId) (long) otherData, nil);
		if (CheckSoupsSortTables(persistent, otherPersistent) != NILREF)
		{
			if (*info->fResult == NILREF)
				*info->fResult = AllocateArray(RSSYMarray, 0);
			RefVar result(*info->fResult);
			AddArraySlot(result, RefVar(SKeyToKey(*key, RSSYMstring, nil)));
			break;
		}
	}
	return 0;
}


// ROM 0x0035f9f8 StoreCheckUnion
// The store's CheckUnion method: the names of its soups that sort an
// index differently from another store's soup of the name; nil when no
// store keeps sort tables.
Ref
StoreCheckUnion(RefArg rcvr)
{
	RefVar stores(GetStores());
	long numStores = Length(stores);
	long i;
	for (i = numStores - 1; i >= 0; i--)
		if (StoreHasSortTables(RefVar(GetArraySlotRef(stores, i))))
			break;
	if (i < 0)
		return NILREF;

	RefVar result;
	Ref resultRef = NILREF;
	AddGCRoot(resultRef);
	TSoupIndex* indexes = new TSoupIndex[numStores];
	if (indexes == nil)
		OutOfMemory();
	long storeIndex = -1;
	newton_try
	{
		RefVar store;
		for (i = 0; i < numStores; i++)
		{
			store = GetArraySlotRef(stores, i);
			InitNameIndex(&indexes[i], store);
			if (EQRef(store, rcvr))
				storeIndex = i;
		}
		CheckUnionInfo info;
		info.fWrapper = GetStoreWrapper(rcvr);
		info.fNumStores = numStores;
		info.fIndexes = indexes;
		info.fStoreIndex = storeIndex;
		info.fResult = &resultRef;
		indexes[storeIndex].Search(true, nil, nil, CheckUnionStopFn, &info, nil, nil);
	}
	newton_catch_all
	{
		delete[] indexes;
		RemoveGCRoot(resultRef);
		rethrow;
	}
	end_try;
	delete[] indexes;
	result = resultRef;
	RemoveGCRoot(resultRef);
	return result;
}


// ROM 0x0035fc9c StoreConvertSoupSortTables
// The store's ConvertSoupSortTables method: its soup of the name taken
// out of the union, its indexes rebuilt to sort as the other stores' soup
// of the name does (the last other store that has it), and put back.
Ref
StoreConvertSoupSortTables(RefArg rcvr, RefArg name)
{
	RefVar soup(StoreGetSoup(rcvr, name));
	if ((Ref) soup == NILREF)
		return NILREF;
	RefVar stores(GetStores());
	RefVar store;
	RefVar otherPersistent;
	RefVar path;
	RemoveFromUnionSoup(name, soup);
	for (long i = Length(stores) - 2; i >= 0; i--)
	{
		store = GetArraySlotRef(stores, i);
		PSSId id = StoreGetSoupId(store, name);
		if (id == 0)
			continue;
		otherPersistent = LoadPermObject(GetStoreWrapper(store), id, nil);
		Boolean converted = false;
		for ( ; ; )
		{
			path = CheckSoupsSortTables(RefVar(GetFrameSlotRef(soup, RSSYM_proto)), otherPersistent);
			if ((Ref) path == NILREF)
				break;
			PlainSoupRemoveIndex(soup, path);
			PlainSoupAddIndex(soup, RefVar(IndexPathToIndexDesc(otherPersistent, path, nil)));
			converted = true;
		}
		if (converted)
			break;
	}
	AddToUnionSoup(name, soup);
	return NILREF;
}


/*------------------------------------------------------------------------------
	T h e   u n i o n   s o u p s
------------------------------------------------------------------------------*/

// ROM 0x0035ff60 GetUnionSoup__FRC6RefVar
// The union soup of the name: the cached one, else a clone of the
// prototype over the stores' soups of that name (nil when no store has
// it), cached, its errorCode set when the soups' sort tables differ.
Ref
GetUnionSoup(RefArg name)
{
	RefVar unionSoup(FindSoupInCache(RefVar(gUnionSoups), name));
	if ((Ref) unionSoup != NILREF)
		return unionSoup;

	unionSoup = Clone(RefVar(Runionsoupprototype));
	RefVar soupList(AllocateArray(RSSYMarray, 0));
	RefVar store;
	long numStores = Length(gStores);
	for (long i = 0; i < numStores; i++)
	{
		store = GetArraySlotRef(gStores, i);
		if (StoreHasSoup(store, name) != NILREF)
			AddArraySlot(soupList, RefVar(StoreGetSoup(store, name)));
	}
	if (Length(soupList) < 1)
		return NILREF;
	SetFrameSlot(unionSoup, RSSYMsouplist, soupList);
	SetFrameSlot(unionSoup, RSSYMthename, RefVar(SoupGetName(RefVar(GetArraySlotRef(soupList, 0)))));
	SetFrameSlot(unionSoup, RSSYMcursors, RefVar(MakeEntryCache()));
	if (numStores > 0)
		PutEntryIntoCache(RefVar(gUnionSoups), unionSoup);
	long err = CheckUnionSortTables(unionSoup);
	if (err != 0)
		SetFrameSlot(unionSoup, RSSYMerrorcode, RefVar(MAKEINT(err)));
	return unionSoup;
}


// ROM 0x003601b8 GetUnionSoupAlways__FRC6RefVar
// The union soup of the name, made empty (no soups, the name cloned) and
// cached when no store has the soup yet.
Ref
GetUnionSoupAlways(RefArg name)
{
	RefVar unionSoup(GetUnionSoup(name));
	if ((Ref) unionSoup == NILREF)
	{
		unionSoup = Clone(RefVar(Runionsoupprototype));
		SetFrameSlot(unionSoup, RSSYMsouplist, RefVar(AllocateArray(RSSYMarray, 0)));
		SetFrameSlot(unionSoup, RSSYMthename, RefVar(TotalClone(name)));
		SetFrameSlot(unionSoup, RSSYMcursors, RefVar(MakeEntryCache()));
		PutEntryIntoCache(RefVar(gUnionSoups), unionSoup);
	}
	return unionSoup;
}


// ROM 0x003602c4 AddToUnionSoup__FRC6RefVarT1
// The soup added to the union soup (given, or by name: nothing when there
// is none cached of that name); the cursors told (SoupAdded, or SetSoup
// when the sort tables now differ and the union soup is errored).
void
AddToUnionSoup(RefArg nameOrUnionSoup, RefArg soup)
{
	RefVar unionSoup;
	if (IsString(nameOrUnionSoup))
	{
		unionSoup = FindSoupInCache(RefVar(gUnionSoups), nameOrUnionSoup);
		if ((Ref) unionSoup == NILREF)
			return;
	}
	else
		unionSoup = nameOrUnionSoup;
	RefVar soupList(GetFrameSlotRef(unionSoup, RSSYMsouplist));
	AddArraySlot(soupList, soup);
	long err = CheckUnionSortTables(unionSoup);
	if (err != 0)
	{
		SetFrameSlot(unionSoup, RSSYMerrorcode, RefVar(MAKEINT(err)));
		EachSoupCursorDo(unionSoup, kSoupCursorSetSoup, soup);
	}
	else
		EachSoupCursorDo(unionSoup, kSoupCursorSoupAdded, soup);
}


// ROM 0x003603f8 RemoveFromUnionSoup__FRC6RefVarT1
// The soup taken out of the union soup (given, or by name); the cursors
// told (SoupRemoved; or SetSoup to the union soup itself when it was
// errored and its soups now agree, to its last soup when they still
// differ).
void
RemoveFromUnionSoup(RefArg nameOrUnionSoup, RefArg soup)
{
	RefVar unionSoup;
	if (IsString(nameOrUnionSoup))
	{
		unionSoup = FindSoupInCache(RefVar(gUnionSoups), nameOrUnionSoup);
		if ((Ref) unionSoup == NILREF)
			return;
	}
	else
		unionSoup = nameOrUnionSoup;
	RefVar soupList(GetFrameSlotRef(unionSoup, RSSYMsouplist));
	long i;
	for (i = Length(soupList) - 1; i >= 0; i--)
		if (EQRef(GetArraySlotRef(soupList, i), soup))
			break;
	if (i < 0)
		return;
	ArrayMunger(soupList, i, 1, RefVar(NILREF), 0, 0);
	long err = CheckUnionSortTables(unionSoup);
	if (err == 0 && GetFrameSlotRef(unionSoup, RSSYMerrorcode) == NILREF)
		EachSoupCursorDo(unionSoup, kSoupCursorSoupRemoved, soup);
	else
	{
		RefVar errorCode;
		RefVar newSoup;
		if (err == 0)
			newSoup = unionSoup;
		else
		{
			errorCode = MAKEINT(err);
			newSoup = GetArraySlotRef(soupList, Length(soupList) - 1);
		}
		SetFrameSlot(unionSoup, RSSYMerrorcode, errorCode);
		EachSoupCursorDo(unionSoup, kSoupCursorSetSoup, newSoup);
	}
}


// ROM 0x0036064c CheckStoresWriteProtect__FRC6RefVar
// Every soup's store of the union soup checked writable.
void
CheckStoresWriteProtect(RefArg unionSoup)
{
	RefVar soupList(GetFrameSlotRef(unionSoup, RSSYMsouplist));
	RefVar soup;
	for (long i = Length(soupList) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soupList, i);
		CheckWriteProtect(((TStoreWrapper*) GetFrameSlotRef(soup, RSSYMtstore))->Store());
	}
}


/*------------------------------------------------------------------------------
	T h e   u n i o n   s o u p   m e t h o d s
	The receiver is the union soup frame.
------------------------------------------------------------------------------*/

// ROM 0x0035f304 UnionSoupAdd
// The entry added to the first soup (the ROM's prototype does not use
// it: its Add method adds to the first store through AddToStore).
Ref
UnionSoupAdd(RefArg rcvr, RefArg entry)
{
	RefVar soup(GetArraySlotRef(RefVar(GetFrameSlotRef(rcvr, RSSYMsouplist)), 0));
	return SoupAdd(soup, entry);
}


// ROM 0x0035f364 UnionSoupAddIndex
Ref
UnionSoupAddIndex(RefArg rcvr, RefArg indexSpec)
{
	CheckStoresWriteProtect(rcvr);
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	long count = Length(soupList);
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(soupList, i);
		PlainSoupAddIndex(soup, indexSpec);
	}
	return NILREF;
}


// ROM 0x0035f400 UnionSoupRemoveIndex
Ref
UnionSoupRemoveIndex(RefArg rcvr, RefArg path)
{
	CheckStoresWriteProtect(rcvr);
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	long count = Length(soupList);
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(soupList, i);
		PlainSoupRemoveIndex(soup, path);
	}
	return NILREF;
}


// ROM 0x0035f558 UnionSoupHasTags
// Whether every soup has a tags index (none: no).
Ref
UnionSoupHasTags(RefArg rcvr)
{
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	long count = Length(soupList);
	if (count == 0)
		return NILREF;
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(soupList, i);
		if (PlainSoupHasTags(soup) == NILREF)
			return NILREF;
	}
	return TRUEREF;
}


// The union soup's soups must all have tags indexes for the tag methods.
static void
CheckUnionHasTags(RefArg rcvr)
{
	if (UnionSoupHasTags(rcvr) == NILREF)
		Throw(exStoreError, (void*) kNSErrNoTagsIndex, nil);
}


// ROM 0x0035f49c UnionSoupAddTags
Ref
UnionSoupAddTags(RefArg rcvr, RefArg tagOrTags)
{
	CheckStoresWriteProtect(rcvr);
	CheckUnionHasTags(rcvr);
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	for (long i = Length(soupList) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soupList, i);
		PlainSoupAddTags(soup, tagOrTags);
	}
	return NILREF;
}


// ROM 0x0035f624 UnionSoupGetTags
// The union of the soups' tags; nil when one has none.
Ref
UnionSoupGetTags(RefArg rcvr)
{
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	RefVar result;
	RefVar tags;
	long count = Length(soupList);
	for (long i = 0; i < count; i++)
	{
		soup = GetArraySlotRef(soupList, i);
		tags = PlainSoupGetTags(soup);
		if ((Ref) tags == NILREF)
			return NILREF;
		if ((Ref) result == NILREF)
			result = tags;
		else
			result = FSetUnion(RefVar(NILREF), result, tags, RefVar(TRUEREF));
	}
	return result;
}


// ROM 0x0035f78c UnionSoupRemoveTags
Ref
UnionSoupRemoveTags(RefArg rcvr, RefArg tags)
{
	CheckStoresWriteProtect(rcvr);
	CheckUnionHasTags(rcvr);
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	for (long i = Length(soupList) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soupList, i);
		PlainSoupRemoveTags(soup, tags);
	}
	return NILREF;
}


// ROM 0x0035f848 UnionSoupModifyTag
Ref
UnionSoupModifyTag(RefArg rcvr, RefArg oldTag, RefArg newTag)
{
	CheckStoresWriteProtect(rcvr);
	CheckUnionHasTags(rcvr);
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	for (long i = Length(soupList) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soupList, i);
		PlainSoupModifyTag(soup, oldTag, newTag);
	}
	return NILREF;
}


// ROM 0x0035f90c UnionSoupFlush
// The NaughtyFlush method: every soup flushed.
Ref
UnionSoupFlush(RefArg rcvr)
{
	return FlushSoupList(RefVar(GetFrameSlotRef(rcvr, RSSYMsouplist)));
}


// ROM 0x0035f95c UnionSoupGetSize
Ref
UnionSoupGetSize(RefArg rcvr)
{
	RefVar soupList(GetFrameSlotRef(rcvr, RSSYMsouplist));
	RefVar soup;
	long size = 0;
	for (long i = Length(soupList) - 1; i >= 0; i--)
	{
		soup = GetArraySlotRef(soupList, i);
		size += RINT(PlainSoupGetSize(soup));
	}
	return MAKEINT(size);
}


// ROM 0x002b5ce0 FGetUnionSoup
static Ref
FGetUnionSoup(RefArg /*rcvr*/, RefArg name)
{
	return GetUnionSoup(name);
}


// ROM 0x002b5ce8 FGetUnionSoupAlways
static Ref
FGetUnionSoupAlways(RefArg /*rcvr*/, RefArg name)
{
	return GetUnionSoupAlways(name);
}


/*------------------------------------------------------------------------------
	T h e   p r o t o t y p e
	Host: the unionSoupPrototype frame when the ROM's objects are not
	imported - the native methods as host function objects, the ROM's
	NewtonScript methods and the NewtonScript built-ins they call compiled
	from the source below, which re-expresses their bytecode
	(nsfunctions.py --disasm unionsoupprototype.<method>, --disasm <name>).
------------------------------------------------------------------------------*/

static const ScriptFunctionEntry gUnionSoupScripts[] = {
	// ROM 0x0062b321 (object) unionSoupPrototype.Add: discontinued, adds to the first store
	{ "Add",
	  "func(entry) begin\n"
	  "  BadWickedNaughtyNoot(\"usoup:Add(\" & self:GetName() & \")\", 'discontinued);\n"
	  "  self:AddToStore(entry, GetStores()[0])\n"
	  "end" },
	// ROM 0x0062b359 (object) unionSoupPrototype.flush: discontinued
	{ "flush",
	  "func() begin\n"
	  "  BadWickedNaughtyNoot(\"usoup:Flush(\" & self:GetName() & \")\", 'discontinued);\n"
	  "  self:NaughtyFlush()\n"
	  "end" },
	// ROM 0x00634889 (object) unionSoupPrototype.GetSoupList
	{ "GetSoupList", "func() Clone(self.soupList)" },
	// ROM 0x006341fd (object) unionSoupPrototype.GetMember: the store's soup of the name, made from its soupDef when missing
	{ "GetMember",
	  "func(store) begin\n"
	  "  local name := self:GetName();\n"
	  "  local soup := store:GetSoup(name);\n"
	  "  if not soup then soup := CreateUSoupMember(name, store);\n"
	  "  soup\n"
	  "end" },
	// ROM 0x006341a1 (object) unionSoupPrototype.AddToStore
	{ "AddToStore", "func(entry, store) self:GetMember(store):Add(entry)" },
	// ROM 0x006340ad (object) unionSoupPrototype.AddToDefaultStore
	{ "AddToDefaultStore", "func(entry) self:AddToStore(entry, GetDefaultStore())" },
	// ROM 0x006342bd (object) unionSoupPrototype.HasTags: every soup has tags, or the soupDef has a tags index
	{ "HasTags",
	  "func() begin\n"
	  "  local i := Length(self.soupList);\n"
	  "  if i <> 0 then begin\n"
	  "    repeat\n"
	  "      i := i - 1;\n"
	  "      if not self.soupList[i]:HasTags() then return nil;\n"
	  "    until i = 0;\n"
	  "    return true\n"
	  "  end;\n"
	  "  local def := GetSoupDef(self:GetName());\n"
	  "  if def then begin\n"
	  "    local indexes := def.indexes;\n"
	  "    if indexes then begin\n"
	  "      i := Length(indexes);\n"
	  "      while i > 0 do begin\n"
	  "        i := i - 1;\n"
	  "        if indexes[i].type = 'tags then return true\n"
	  "      end\n"
	  "    end\n"
	  "  end;\n"
	  "  nil\n"
	  "end" },
	{ nil, nil }
};

// The ROM's NewtonScript built-in functions the union soups rest on.
// UnionSoupRegistry (a global variable) is an array of {soupDef, apps}
// sorted by the soupDef's name; a soupDef {name, indexes, ownerApp,
// initHook, ...} says how to create the soup on a store that lacks it.
static const ScriptFunctionEntry gUnionSoupBuiltins[] = {
	// ROM 0x00569785 (object) GetSoupDef: the registry's, else the soupDef info of a store's soup of the name
	{ "GetSoupDef",
	  "func(name) begin\n"
	  "  local reg := BFetch(UnionSoupRegistry, name, '|str<|, '[pathExpr: soupDef, name]);\n"
	  "  if reg then return reg.soupDef;\n"
	  "  local store;\n"
	  "  foreach store in GetStores() do\n"
	  "    if store:HasSoup(name) then begin\n"
	  "      local def := store:GetSoup(name):GetInfo('soupDef);\n"
	  "      if def then return def\n"
	  "    end;\n"
	  "  nil\n"
	  "end" },
	// ROM 0x005699bd (object) CreateUSoupMember
	{ "CreateUSoupMember",
	  "func(name, store) begin\n"
	  "  local def := GetSoupDef(name);\n"
	  "  if def then CreateSoupFromSoupDef(def, store, '_newt)\n"
	  "  else Throw('|evt.ex.nosoupdef;type.ref|, {errorCode: 0, value: name})\n"
	  "end" },
	// ROM 0x005698c1 (object) CreateSoupFromSoupDef: the soup created, its soupDef info set, the initHook run
	{ "CreateSoupFromSoupDef",
	  "func(soupDef, store, xmit) begin\n"
	  "  local soup := store:CreateSoup(soupDef.name, soupDef.indexes);\n"
	  "  soup:SetInfo('soupDef, soupDef);\n"
	  "  local hook := soupDef.initHook;\n"
	  "  if hook then\n"
	  "    if IsFunction(hook) then call hook with (soup, soupDef)\n"
	  "    else begin\n"
	  "      local app := GetRoot().(soupDef.ownerApp);\n"
	  "      if app then Perform(app, hook, [soup, soupDef])\n"
	  "    end;\n"
	  "  if xmit then XmitSoupChange(soupDef.name, xmit, 'soupCreated, soup);\n"
	  "  soup\n"
	  "end" },
	// ROM 0x005697ed (object) SupplantSoupDef
	{ "SupplantSoupDef", "func(soup, soupDef) soup:SetInfo('soupDef, soupDef)" },
	// ROM 0x00569911 (object) GetSoupIndexesFromSoupDef
	{ "GetSoupIndexesFromSoupDef",
	  "func(name) begin\n"
	  "  local def := GetSoupDef(name);\n"
	  "  if def then def.indexes else nil\n"
	  "end" },
	// ROM 0x005695f9 (object) RegUnionSoup: the app registered for the soupDef; the union soup answered
	{ "RegUnionSoup",
	  "func(app, soupDef) begin\n"
	  "  local reg := BFetch(UnionSoupRegistry, soupDef.name, '|str<|, '[pathExpr: soupDef, name]);\n"
	  "  app := EnsureInternal(app);\n"
	  "  if reg then begin\n"
	  "    if not IsReadOnly(reg) then\n"
	  "      BInsert(reg.apps, app, '|sym<|, nil, true)\n"
	  "  end else begin\n"
	  "    reg := {soupDef: EnsureInternal(soupDef), apps: [app]};\n"
	  "    BInsert(UnionSoupRegistry, reg, '|str<|, '[pathExpr: soupDef, name], app <> '_RegisterCardSoup)\n"
	  "  end;\n"
	  "  GetUnionSoupAlways(soupDef.name)\n"
	  "end" },
	// ROM 0x005696c9 (object) UnRegUnionSoup: the app unregistered; the soupDef dropped with its last app
	{ "UnRegUnionSoup",
	  "func(name, app) begin\n"
	  "  local reg := BFetch(UnionSoupRegistry, name, '|str<|, '[pathExpr: soupDef, name]);\n"
	  "  if reg and not IsReadOnly(reg) then begin\n"
	  "    BDelete(reg.apps, app, '|sym<|, nil, nil);\n"
	  "    if Length(reg.apps) = 0 then\n"
	  "      BDelete(UnionSoupRegistry, name, '|str<|, '[pathExpr: soupDef, name], if app = '_RegisterCardSoup then 1 else nil)\n"
	  "  end;\n"
	  "  nil\n"
	  "end" },
	// ROM 0x00569a35 (object) GetDefaultStore: the store of the user's defaultStoreSig, else the first
	{ "GetDefaultStore",
	  "func() begin\n"
	  "  local sig := GetUserConfig('defaultStoreSig);\n"
	  "  local stores := GetStores();\n"
	  "  local store;\n"
	  "  foreach store in stores do\n"
	  "    if store:GetSignature() = sig then return store;\n"
	  "  stores[0]\n"
	  "end" },
	// ROM 0x00565525 (object) GetUserConfig: a slot of the userConfiguration global
	{ "GetUserConfig", "func(slot) userConfiguration.(slot)" },
	// ROM 0x00569ea1 (object) XmitSoupChange: the change broadcast to the apps registered for soup changes, deferred
	// DEVIATION: the deferred calls (AddDeferredCall) and XmitSoupChangeNow are
	// NOT YET RECONSTRUCTED; without them the change is not broadcast.
	{ "XmitSoupChange",
	  "func(name, app, change, arg)\n"
	  "  if GlobalFnExists('AddDeferredCall) and GlobalFnExists('XmitSoupChangeNow) then\n"
	  "    AddDeferredCall(functions.XmitSoupChangeNow, [name, app, change, arg])" },
	// ROM 0x0063ac05 (object) BadWickedNaughtyNoot: a discontinued/obsolete API used - written to the REP (and the debugger entered when NoEvilLiveOn)
	{ "BadWickedNaughtyNoot",
	  "func(what, kind) begin\n"
	  "  local msg := if IsInteger(what) then\n"
	  "    \"Nasty practice #\" & what & (if kind then \"(\" & kind & \")\" else nil) & $\\n\n"
	  "  else\n"
	  "    \"Use of\" && kind && \"API:-  \" & what & $\\n;\n"
	  "  if vars.NoEvilLiveOn then begin\n"
	  "    Write(msg);\n"
	  "    BreakLoop()\n"
	  "  end\n"
	  "  else if kind <> 'obsolete then Write(msg)\n"
	  "end" },
	{ nil, nil }
};

struct NativeMethod
{
	const char*	fName;
	void*		fFunction;
	long		fNumArgs;
};

static const NativeMethod gUnionSoupNatives[] = {
	{ "GetName", (void*) CommonSoupGetName, 0 },
	{ "GetSize", (void*) UnionSoupGetSize, 0 },
	{ "collect", (void*) SoupCollect, 1 },
	{ "Query", (void*) CommonSoupQuery, 1 },
	{ "AddIndex", (void*) UnionSoupAddIndex, 1 },
	{ "RemoveIndex", (void*) UnionSoupRemoveIndex, 1 },
	{ "NaughtyFlush", (void*) UnionSoupFlush, 0 },
	{ "AddTags", (void*) UnionSoupAddTags, 1 },
	{ "GetTags", (void*) UnionSoupGetTags, 0 },
	{ "RemoveTags", (void*) UnionSoupRemoveTags, 1 },
	{ "ModifyTag", (void*) UnionSoupModifyTag, 2 },
	{ nil, nil, 0 }
};


void
InitUnionSoupPrototype(void)
{
	if (Runionsoupprototype != NILREF)
		return;										// the ROM's
	AddGCRoot(Runionsoupprototype);

	RefVar methods(AllocateFrame());
	RefVar fn;
	for (const NativeMethod* m = gUnionSoupNatives; m->fName != nil; m++)
	{
		fn = MakeCFunction(m->fFunction, m->fNumArgs, nil);
		SetFrameSlot(methods, RefVar(Intern((char*) m->fName)), fn);
	}
	for (const ScriptFunctionEntry* m = gUnionSoupScripts; m->fName != nil; m++)
	{
		fn = CompileScriptFunction(m->fSource);
		SetFrameSlot(methods, RefVar(Intern((char*) m->fName)), fn);
	}
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYM_parent, methods);
	SetFrameSlot(frame, RSSYMclass, RefVar(Intern((char*) "UnionSoup")));
	SetFrameSlot(frame, RSSYMsouplist, RefVar(NILREF));
	SetFrameSlot(frame, RSSYMthename, RefVar(NILREF));
	SetFrameSlot(frame, RSSYMcursors, RefVar(NILREF));
	Runionsoupprototype = frame;

	// the built-ins (the ROM's frame has them when its objects are imported),
	// and the registry the ROM's NewtonScript system code makes at boot
	if (gROMBuiltinFunctions == NILREF)
		InstallScriptFunctions(gUnionSoupBuiltins);
	RefVar registryTag(Intern((char*) "UnionSoupRegistry"));
	if (!FrameHasSlot(RefVar(gVarFrame), registryTag))
		SetFrameSlot(RefVar(gVarFrame), registryTag, RefVar(AllocateArray(RSSYMarray, 0)));
}


void
RegisterUnionSoupNatives(void)
{
	RegisterNativeFunction("FGetUnionSoup", (void*) FGetUnionSoup, 1);
	RegisterNativeFunction("FGetUnionSoupAlways", (void*) FGetUnionSoupAlways, 1);
	RegisterNativeFunction("UnionSoupAddIndex", (void*) UnionSoupAddIndex, 1);
	RegisterNativeFunction("UnionSoupRemoveIndex", (void*) UnionSoupRemoveIndex, 1);
	RegisterNativeFunction("UnionSoupFlush", (void*) UnionSoupFlush, 0);
	RegisterNativeFunction("UnionSoupGetSize", (void*) UnionSoupGetSize, 0);
	RegisterNativeFunction("UnionSoupAddTags", (void*) UnionSoupAddTags, 1);
	RegisterNativeFunction("UnionSoupHasTags", (void*) UnionSoupHasTags, 0);
	RegisterNativeFunction("UnionSoupGetTags", (void*) UnionSoupGetTags, 0);
	RegisterNativeFunction("UnionSoupRemoveTags", (void*) UnionSoupRemoveTags, 1);
	RegisterNativeFunction("UnionSoupModifyTag", (void*) UnionSoupModifyTag, 2);
	RegisterNativeFunction("StoreCheckUnion", (void*) StoreCheckUnion, 0);
	RegisterNativeFunction("StoreConvertSoupSortTables", (void*) StoreConvertSoupSortTables, 1);
}
