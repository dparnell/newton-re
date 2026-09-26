/*
	File:		assist/Assistant.cpp

	Contains:	The Intelligent Assistant's list building (Assistant.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Assistant.h"
#include "Interpreter.h"
#include "AssistStrings.h"

#include "Frames.h"
#include "ObjectHeap.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"

Ref		FFindStringInArray(RefArg rcvr, RefArg array, RefArg str);		// StringNatives.cpp


// ROM 0x0008449c Append__FRC6RefVarN21
// `item` on the end of `list`.  A nil list becomes a new array of the one
// item, which is why every caller takes the answer rather than trusting
// the argument to have been added to.
Ref
Append(RefArg /*rcvr*/, RefArg list, RefArg item)
{
	if (ISNIL(list))
	{
		RefVar made(AllocateArray(RefVar(RSSYMarray), 1));
		SetArraySlot(made, 0, item);
		return made;
	}
	AddArraySlot(list, item);
	return list;
}


// ROM 0x000871d0 member_p__FRC6RefVarT1
// The element of `list` that is EQ to `item`, or nil: Lisp's `member`
// answering the element rather than the tail of the list.
Ref
member_p(RefArg list, RefArg item)
{
	if (ISNIL(list) || ISNIL(item))
		return NILREF;
	long count = Length(list);
	for (long i = 0; i < count; i++)
	{
		if (EQRef(item, GetArraySlotRef(list, i)))
			return GetArraySlot(list, i);
	}
	return NILREF;
}


// ROM 0x00086f60 IsReadOnly__FRC6RefVar
// The object's header says it cannot be written to: it is in the ROM, or
// in a package part mapped read-only.  What the list operations do with
// that is clone it before adding to it.
Boolean
IsReadOnly(RefArg obj)
{
	return (ObjectFlags(obj) & kObjReadOnly) != 0;
}


// ROM 0x00086e6c UniqueAppendList__FRC6RefVarN21
// AppendList(list, items): each item appended unless the list has it
// already.  The comparison is FindStringInArray's, so this is for lists
// of strings - two strings that read the same are the same item.
Ref
UniqueAppendList(RefArg /*rcvr*/, RefArg list, RefArg items)
{
	RefVar item;
	if (NOTNIL(items))
	{
		long count = Length(items);
		for (long i = 0; i < count; i++)
		{
			item = GetArraySlotRef(items, i);
			if (ISNIL(RefVar(FFindStringInArray(RefVar(NILREF), list, item))))
				Append(RefVar(NILREF), list, item);
		}
	}
	return list;
}


// ROM 0x00086f88 UniqueAppendListGen__FRC6RefVarN21
// MashLists(a, b): the items of `b` that are not in `a` appended to it,
// and `a` answered.  Either list being nil makes the answer the other
// one; a list that cannot be written to is cloned first.
//
// The appending is in place, so a caller that passes a list someone else
// owns has lengthened that list - which is what GenFullCommands does to
// vars.dynatemplates below.
Ref
UniqueAppendListGen(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISNIL(b))
		return a;
	if (ISNIL(a))
		return b;
	RefVar list(IsReadOnly(a) ? Clone(a) : (Ref) a);
	RefVar items(IsReadOnly(b) ? Clone(b) : (Ref) b);
	RefVar item;
	long count = Length(items);
	for (long i = 0; i < count; i++)
	{
		item = GetArraySlotRef(items, i);
		if (ISNIL(RefVar(member_p(list, item))))
			Append(RefVar(NILREF), list, item);
	}
	return list;
}


// ROM 0x000870e4 UniqueAppendString__FRC6RefVarN21
Ref
UniqueAppendString(RefArg /*rcvr*/, RefArg list, RefArg str)
{
	if (ISNIL(RefVar(FFindStringInArray(RefVar(NILREF), list, str))))
		Append(RefVar(NILREF), list, str);
	return list;
}


// ROM 0x00087170 UniqueAppendItem__FRC6RefVarN21
Ref
UniqueAppendItem(RefArg /*rcvr*/, RefArg list, RefArg item)
{
	if (ISNIL(RefVar(member_p(list, item))))
		Append(RefVar(NILREF), list, item);
	return list;
}


// ROM 0x00084fd4 MapSymToFrame__FRC6RefVarT1
// The frame a symbol names: a global variable of that name, else a slot
// of the Assistant's own frame.  It is how a template names the action
// that carries its task out without holding the action itself.
Ref
MapSymToFrame(RefArg /*rcvr*/, RefArg sym)
{
	RefVar frame(GetFrameSlotRef(gVarFrame, sym));
	if (ISNIL(frame))
		frame = GetFrameSlotRef(kAssistantFrame, sym);
	return frame;
}


// ROM 0x00085bf0 GenFullCommands__FRC6RefVar
// The first word of every task the Assistant knows - the list the
// "Please ..." slip offers - or nil when there is none.
//
// The templates are the Assistant's own `task_list` with
// `vars.dynatemplates` (what RegTaskTemplate adds to) mashed in, less the
// two that are not commands: the default task and the one that says what
// the Assistant is.  A template's word is its action's `Lexicon`, which
// may be a list of words, or a list of lists of them - the first of the
// first is the one that is offered.
//
// Two things the ROM does that it need not: the `task_list` is cloned and
// then thrown away (the mash answers `dynatemplates`, not the clone), and
// with it goes any hope that `vars.dynatemplates` is left alone - the
// mash appends to it in place, so every call leaves the ROM's own
// templates in the application's list.  And the `signature` slot is
// fetched and its length taken for nothing at all.  Kept as they are.
Ref
GenFullCommands(RefArg /*rcvr*/)
{
	RefVar templates(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMtask_list))));
	if (IsArray(RefVar(GetFrameSlotRef(gVarFrame, RSSYMdynatemplates))))
		templates = UniqueAppendListGen(RefVar(NILREF),
										RefVar(GetFrameSlotRef(gVarFrame, RSSYMdynatemplates)),
										templates);

	long count = Length(templates);
	RefVar commands(AllocateArray(RefVar(RSSYMarray), 0));
	RefVar entry;
	RefVar act;
	for (long i = 0; i < count; i++)
	{
		entry = GetArraySlotRef(templates, i);
		if (EQRef(GetFrameSlotRef(kAssistantFrame, RSSYMdefault_task), entry))
			continue;
		if (EQRef(GetFrameSlotRef(kAssistantFrame, RSSYMabout_task), entry))
			continue;
		RefVar signature(GetFrameSlotRef(entry, RSSYMsignature));
		Length(signature);				// (asked for, and the answer dropped)
		act = GetFrameSlotRef(entry, RSSYMprimary_act);
		if (IsSymbol(act))
			act = MapSymToFrame(RefVar(NILREF), act);
		if (FrameHasSlotRef(act, RSSYMlexicon))
		{
			act = GetFrameSlotRef(act, RSSYMlexicon);
			if (IsArray(act))
				act = GetArraySlotRef(act, 0);
			if (IsArray(act))
				act = GetArraySlotRef(act, 0);
			if (NOTNIL(act))
				UniqueAppendString(RefVar(NILREF), commands, act);
		}
	}
	return Length(commands) < 1 ? NILREF : (Ref) commands;
}


/*------------------------------------------------------------------------------
	T h e   c l a s s   h i e r a r c h y

	Everything the Assistant knows about is a frame with an `isa` slot
	naming the frame it is a kind of: `john` isa `person` isa `user_obj`,
	`call` isa `action`.  A slot may hold the frame itself or the symbol
	that names it (MapSymToFrame finds that), and the chain ends at a
	frame with no `isa`.  Two roots matter enough to be kept in globals:
	`action` and `user_obj`, which is how the Assistant tells a verb from
	the thing it acts on.
------------------------------------------------------------------------------*/

Ref		gDynaDeleteFrame = NILREF;		// ROM 0x0c100b90 gDynaDeleteFrame
Ref		gDynaDeleteSym = NILREF;		// ROM 0x0c100b8c gDynaDeleteSYM
Ref		gActionClass = NILREF;			// ROM 0x0c100bc4 gActionClass - @8.action
Ref		gObjectClass = NILREF;			// ROM 0x0c100bc8 gObjectClass - @8.user_obj


// ROM 0x00084f14 InitDSTaskTemplates__FRC6RefVarT1
// The two root classes taken out of the Assistant's frame and made GC
// roots.  The ROM calls this from InitDarkStar, which is what starts the
// Assistant.
Ref
InitDSTaskTemplates(RefArg /*rcvr*/, RefArg /*arg*/)
{
	gActionClass = GetFrameSlotRef(kAssistantFrame, RSSYMaction);
	AddGCRoot(gActionClass);
	gObjectClass = GetFrameSlotRef(kAssistantFrame, RSSYMuser_obj);
	AddGCRoot(gObjectClass);
	return TRUEREF;
}


// ROM 0x00086004 ISATest__FRC6RefVarN21
// Whether `thing` is a kind of `kind`: the same frame, or one reached by
// following `isa` up from it.  Either may be given as a symbol.
//
// There is one shortcut before the walk: two frames with the same `isa`
// *and* the same `lexicon` are taken to be the same thing, which is how
// two entries the lexicon made for the same word compare equal without
// being the same object.
Ref
ISATest(RefArg /*rcvr*/, RefArg thing, RefArg kind)
{
	RefVar a(thing);
	RefVar b(kind);
	if (IsSymbol(a))
		a = MapSymToFrame(RefVar(), a);
	if (IsSymbol(b))
		b = MapSymToFrame(RefVar(), b);
	if (NOTNIL(a) && NOTNIL(b))
	{
		if (EQ(a, b))
			return TRUEREF;
		if (EQRef(GetFrameSlotRef(a, RSSYMisa), GetFrameSlotRef(b, RSSYMisa))
			&& EQRef(GetFrameSlotRef(a, RSSYMlexicon), GetFrameSlotRef(b, RSSYMlexicon)))
			return TRUEREF;
		RefVar up(GetFrameSlotRef(a, RSSYMisa));
		while (NOTNIL(up))
		{
			if (IsSymbol(up))
				up = MapSymToFrame(RefVar(), up);
			if (EQ(up, b))
				return TRUEREF;
			up = GetFrameSlotRef(up, RSSYMisa);
		}
	}
	return NILREF;
}


// ROM 0x000864cc PathToRoot__FRC6RefVarT1
// The frame and every frame above it, nearest first, as an array; nil
// when the argument names nothing.
Ref
PathToRoot(RefArg /*rcvr*/, RefArg thing)
{
	RefVar frame(thing);
	if (IsSymbol(frame))
		frame = MapSymToFrame(RefVar(), frame);
	if (ISNIL(frame))
		return NILREF;
	RefVar path(AllocateArray(RSSYMarray, 1));
	SetArraySlot(path, 0, frame);
	RefVar up(GetFrameSlotRef(frame, RSSYMisa));
	while (NOTNIL(up))
	{
		if (IsSymbol(up))
			up = MapSymToFrame(RefVar(), up);
		AddArraySlot(path, up);
		up = GetFrameSlotRef(up, RSSYMisa);
	}
	return path;
}


// ROM 0x000862f0 CheezyIntersect__FRC6RefVarN21
// The elements the two arrays have in common, in the order of whichever
// is the shorter.  Cheezy because it is two nested loops with no break:
// an element that appears twice in the longer array is answered twice.
Ref
CheezyIntersect(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISNIL(a) || ISNIL(b))
		return NILREF;
	ULong lengthA = Length(a);
	ULong lengthB = Length(b);
	if (lengthA == 0 || lengthB == 0)
		return NILREF;
	RefVar result(AllocateArray(RSSYMarray, 0));
	RefVar inner;			// the longer, walked for each of the shorter
	RefVar outer;
	ULong outerCount;
	ULong innerCount;
	if (lengthB < lengthA)
	{
		inner = a;
		outer = b;
		outerCount = lengthB;
		innerCount = lengthA;
	}
	else
	{
		inner = b;
		outer = a;
		outerCount = lengthA;
		innerCount = lengthB;
	}
	for (ULong i = 0; i < outerCount; i++)
	{
		RefVar item(GetArraySlotRef(outer, i));
		for (ULong j = 0; j < innerCount; j++)
			if (EQRef(item, GetArraySlotRef(inner, j)))
				AddArraySlot(result, item);
	}
	return result;
}


// ROM 0x00086670 CommonAncestors__FRC6RefVarN21
// The frames that are above both of two things, nearest first: the two
// paths to the root intersected.  An empty intersection answers nil.
Ref
CommonAncestors(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	RefVar pathB(PathToRoot(RefVar(), b));
	RefVar pathA(PathToRoot(RefVar(), a));
	RefVar common(CheezyIntersect(RefVar(), pathA, pathB));
	if (ISNIL(common) || Length(common) != 0)
		return common;
	return NILREF;
}


// ROM 0x00086764 CompositeClass__FRC6RefVarT1
// The one class a whole list of things has in common - the nearest
// ancestor of all of them.  A list of one answers that one; the rest are
// folded together two at a time, the running answer being the nearest
// common ancestor so far.  ==> nil as soon as two of them share nothing.
Ref
CompositeClass(RefArg /*rcvr*/, RefArg things)
{
	ULong count = Length(things);
	if (count == 1)
		return GetArraySlotRef(things, 0);
	RefVar common(CommonAncestors(RefVar(), RefVar(GetArraySlotRef(things, 0)),
								  RefVar(GetArraySlotRef(things, 1))));
	if (count < 3)
	{
		if (ISNIL(common))
			return NILREF;
	}
	else
	{
		for (ULong i = 2; i < count; i++)
		{
			common = CommonAncestors(RefVar(), RefVar(GetArraySlotRef(common, 0)),
									 RefVar(GetArraySlotRef(things, i)));
			if (ISNIL(common))
				return NILREF;
		}
	}
	return GetArraySlotRef(common, 0);
}


// ROM 0x00086c24 CheezySubsumption__FRC6RefVarN21
// The things sorted into the kinds they are: one array per kind that
// matched anything, in the kinds' order.  A thing goes to the first kind
// it answers to and is then taken out of the running, so nothing is
// counted twice - which is what makes this a subsumption rather than a
// plain test.
Ref
CheezySubsumption(RefArg /*rcvr*/, RefArg things, RefArg kinds)
{
	if (ISNIL(things) || ISNIL(kinds))
		return NILREF;
	RefVar left(Clone(things));
	ULong thingCount = Length(left);
	ULong kindCount = Length(kinds);
	if (thingCount == 0 || kindCount == 0)
		return NILREF;
	RefVar result(AllocateArray(RSSYMarray, 0));
	for (ULong k = 0; k < kindCount; k++)
	{
		RefVar kind(GetArraySlotRef(kinds, k));
		RefVar taken(AllocateArray(RSSYMarray, 0));
		for (ULong i = 0; i < thingCount; i++)
		{
			RefVar thing(GetArraySlotRef(left, i));
			if (NOTNIL(ISATest(RefVar(), thing, kind)))
			{
				AddArraySlot(taken, thing);
				SetArraySlot(left, i, RefVar());
			}
		}
		if (Length(taken) > 0)
			AddArraySlot(result, taken);
	}
	return result;
}


// ROM 0x00086974 FavorAction__FRC6RefVarT1
// The first of the things that is an action - the first whose nearest
// common ancestor with `action` is `action` itself.
Ref
FavorAction(RefArg /*rcvr*/, RefArg things)
{
	ULong count = Length(things);
	for (ULong i = 0; i < count; i++)
	{
		RefVar common(CommonAncestors(RefVar(), RefVar(gActionClass),
									  RefVar(GetArraySlotRef(things, i))));
		if (NOTNIL(common) && EQRef(gActionClass, GetArraySlotRef(common, 0)))
			return GetArraySlotRef(things, i);
	}
	return NILREF;
}


// ROM 0x00086acc FavorObject__FRC6RefVarT1
// And the first that is a thing rather than a verb.
Ref
FavorObject(RefArg /*rcvr*/, RefArg things)
{
	ULong count = Length(things);
	for (ULong i = 0; i < count; i++)
	{
		RefVar common(CommonAncestors(RefVar(), RefVar(gObjectClass),
									  RefVar(GetArraySlotRef(things, i))));
		if (NOTNIL(common) && EQRef(gObjectClass, GetArraySlotRef(common, 0)))
			return GetArraySlotRef(things, i);
	}
	return NILREF;
}


// ROM 0x00084b28 GetClasses__FRC6RefVar
// One class per word of the sentence.  Each word came out of the lexicon
// as a list of the things it might be, and this picks one: their common
// class if they have one, else - remembering whether an action has been
// seen yet - an action, else whatever the last action's `meta_level`
// says to prefer.  A word after an action that takes a meta level is
// read as another action; otherwise as a thing.
Ref
GetClasses(RefArg words)
{
	RefVar classes(AllocateArray(RSSYMarray, 0));
	RefVar lastAction;
	ULong count = Length(words);
	for (ULong i = 0; i < count; i++)
	{
		RefVar meanings(GetArraySlotRef(words, i));
		RefVar chosen(CompositeClass(RefVar(), meanings));
		if (NOTNIL(chosen))
		{
			if (NOTNIL(ISATest(RefVar(), chosen, RefVar(gActionClass))))
				lastAction = chosen;
		}
		if (ISNIL(chosen))
		{
			if (ISNIL(lastAction))
			{
				chosen = FavorAction(RefVar(), meanings);
				lastAction = chosen;
			}
			else
			{
				RefVar act(lastAction);
				if (IsSymbol(act))
					act = MapSymToFrame(RefVar(), act);
				if (ISNIL(GetFrameSlotRef(act, RSSYMmeta_level)))
					chosen = FavorObject(RefVar(), meanings);
				else
					chosen = FavorAction(RefVar(), meanings);
			}
		}
		AddArraySlot(classes, chosen);
	}
	return classes;
}


// ROM 0x00084df8 IsPrimaryAct__FRC6RefVarT1
// Whether the thing is the action the template's `primary_act` names.
Ref
IsPrimaryAct(RefArg thing, RefArg templ)
{
	RefVar act(GetFrameSlotRef(templ, RSSYMprimary_act));
	if (ISNIL(act))
		return NILREF;
	return ISATest(RefVar(), thing, act);
}


// ROM 0x00084e98 IsAction__FRC6RefVar
// Whether the thing is a verb at all.
Ref
IsAction(RefArg thing)
{
	return ISATest(RefVar(), thing, RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMaction)));
}


/*------------------------------------------------------------------------------
	T h e   t a s k   t e m p l a t e s

	A task template says how one sentence is to be carried out: which
	action starts it (`primary_act`), what sorts of thing it needs
	(`signature`), where each of those goes in the frame the slip hands
	over (`preconditions`), and which slip to put up (`taskslip`).  The
	ROM's own are in the Assistant's frame; an application registers its
	own with RegTaskTemplate, which keeps them in `vars.dynatemplates`.
------------------------------------------------------------------------------*/

// ROM 0x0007d5b8 MakePhrasalLexEntry__FRC6RefVarT1
// The words of a class put into the Assistant's own trie, so that
// writing one of them finds the class again.
//
// NOT YET RECONSTRUCTED: the trie (TrieAdd 0x0007ce20 over gDynaTrie),
// which is part of the Airus lexical engine.  Everything up to it - the
// symbol resolved to a frame, the frame having to have both an `isa` and
// a `lexicon` - is here, so a template that could not be indexed is
// still refused in the same way.
Ref
MakePhrasalLexEntry(RefArg /*rcvr*/, RefArg thing)
{
	RefVar frame(thing);
	if (!IsFrame(frame))
		frame = MapSymToFrame(RefVar(), frame);
	if (!IsFrame(frame))
		return NILREF;
	if (NOTNIL(GetFrameSlotRef(frame, RSSYMisa)))
	{
		RefVar lexicon(GetFrameSlotRef(frame, RSSYMlexicon));
		if (NOTNIL(lexicon))
		{
			ULong count = Length(lexicon);
			for (ULong i = 0; i < count; i++)
			{
				RefVar word(GetArraySlotRef(lexicon, i));
				unsigned char* ascii = (unsigned char*) NewASCIIString(word);
				if (ascii != nil)
				{
					unsigned char buffer[256];
					Bstrcpy(buffer, ascii);
					// NOT YET: TrieAdd(buffer, gDynaTrie, frame)
					DisposPtr((Ptr) ascii);
				}
			}
		}
	}
	return TRUEREF;
}


// ROM 0x0007da24 RemovePhrasalLexEntry__FRC6RefVarT1
// And the other way: the words taken out of the trie again.  The frame
// being deleted (and the symbol that named it) go into two globals while
// it happens, because the trie's walk needs to know which entry to drop
// when a word leads to several.
//
// NOT YET RECONSTRUCTED: DynaTrieDelete 0x0007e900, for the same reason.
Ref
RemovePhrasalLexEntry(RefArg /*rcvr*/, RefArg thing)
{
	RefVar frame(thing);
	if (!IsFrame(frame))
	{
		gDynaDeleteSym = frame;
		frame = MapSymToFrame(RefVar(), frame);
		gDynaDeleteFrame = frame;
	}
	else
	{
		gDynaDeleteSym = NILREF;
		gDynaDeleteFrame = frame;
	}
	Ref result = NILREF;
	if (IsFrame(frame) && NOTNIL(GetFrameSlotRef(frame, RSSYMisa)))
	{
		RefVar lexicon(GetFrameSlotRef(frame, RSSYMlexicon));
		if (NOTNIL(lexicon))
		{
			ULong count = Length(lexicon);
			for (ULong i = 0; i < count; i++)
			{
				RefVar word(GetArraySlotRef(lexicon, i));
				if (IsArray(word))
				{
					ULong parts = Length(word);
					for (ULong j = 0; j < parts; j++)
					{
						// NOT YET: DynaTrieDelete(word[j])
					}
				}
				else
				{
					// NOT YET: DynaTrieDelete(word)
				}
			}
			result = TRUEREF;
		}
	}
	gDynaDeleteSym = NILREF;
	gDynaDeleteFrame = NILREF;
	return result;
}


// ROM 0x00085060 RegTaskTemplate__FRC6RefVarT1
// An application's task template registered.  It has to have all eight
// of the slots a template is made of, and a signature that is not nil;
// it is then deep-copied (so that the application may keep writing to
// its own), its signature's words are put into the lexicon, and the copy
// goes on the end of `vars.dynatemplates`.  ==> the copy, or nil when
// the template was not one.
Ref
RegTaskTemplate(RefArg /*rcvr*/, RefArg templ)
{
	if (!FrameHasSlot(templ, RSSYMvalue) || !FrameHasSlot(templ, RSSYMisa)
		|| !FrameHasSlot(templ, RSSYMprimary_act) || !FrameHasSlot(templ, RSSYMsignature)
		|| !FrameHasSlot(templ, RSSYMpreconditions) || !FrameHasSlot(templ, RSSYMtaskslip)
		|| !FrameHasSlot(templ, RSSYMpostparse) || !FrameHasSlot(templ, RSSYMscore)
		|| ISNIL(GetFrameSlotRef(templ, RSSYMsignature)))
		return NILREF;
	RefVar copy(TotalClone(templ));
	RefVar signature(GetFrameSlotRef(copy, RSSYMsignature));
	long count = Length(signature);
	for (long i = 0; i < count; i++)
		MakePhrasalLexEntry(RefVar(), RefVar(GetArraySlotRef(signature, i)));
	RefVar list(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates));
	if (!IsArray(list))
		list = AllocateArray(RSSYMarray, 0);
	list = Append(RefVar(), list, copy);
	SetFrameSlot(RefVar(gVarFrame), RSSYMdynatemplates, list);
	return copy;
}


// ROM 0x00085394 UnRegTaskTemplate__FRC6RefVarT1
// The template taken off `vars.dynatemplates` again and its signature's
// words taken out of the lexicon.  ==> the list, or nil when there was
// not one.
Ref
UnRegTaskTemplate(RefArg /*rcvr*/, RefArg templ)
{
	RefVar signature(GetFrameSlotRef(templ, RSSYMsignature));
	if (NOTNIL(signature))
	{
		long count = Length(signature);
		for (long i = 0; i < count; i++)
			RemovePhrasalLexEntry(RefVar(), RefVar(GetArraySlotRef(signature, i)));
	}
	RefVar list(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates));
	if (!IsArray(list))
		return NILREF;
	return FSetRemove(RefVar(), list, templ);
}


// ROM 0x00085510 AddEntry__FRC6RefVarN41
// One thing the sentence named recorded in the frame the slip is built
// from: people go on its `person` list and everything else on its
// `places` list, each as a clone of the Assistant's `iaref` prototype
// carrying the entry (`alias`), what it is (`class`) and where it came
// from (`path`).  The matching `...Added` slot is set so that a second
// call for the same kind is ignored - one person and one place is all
// a task slip takes.
Ref
AddEntry(RefArg /*rcvr*/, RefArg theClass, RefArg path, RefArg alias, RefArg frame)
{
	RefVar listSlot;
	RefVar flagSlot;
	if (ISNIL(ISATest(RefVar(), theClass, RefVar(RSSYMperson))))
	{
		if (NOTNIL(GetFrameSlotRef(frame, RSSYMplaceadded)))
			return NILREF;
		listSlot = RSSYMplaces;
		flagSlot = RSSYMplaceadded;
	}
	else
	{
		if (NOTNIL(GetFrameSlotRef(frame, RSSYMpersonadded)))
			return NILREF;
		listSlot = RSSYMperson;
		flagSlot = RSSYMpersonadded;
	}
	if (ISNIL(GetFrameSlotRef(frame, listSlot)))
		SetFrameSlot(frame, listSlot, RefVar(AllocateArray(RSSYMarray, 0)));
	RefVar entry(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMiaref))));
	SetFrameSlot(entry, RSSYMalias, alias);
	SetFrameSlot(entry, RSSYMclass, theClass);
	if (NOTNIL(path))
		SetFrameSlot(entry, RSSYMpath, path);
	AddArraySlot(RefVar(GetFrameSlotRef(frame, listSlot)), entry);
	SetFrameSlot(frame, flagSlot, RefVar(MAKEINT(1)));
	return NILREF;
}


// ROM 0x00084810 FillPreconditions__FRC6RefVarT1
// The words of the sentence sorted into the slots the template wants
// them in.  The template's `signature` says what each slot takes and its
// `preconditions` names the slot; `input` holds the class of each word
// and `raw` the word itself, so a word whose class answers to a
// signature entry goes into that entry's slot.
//
// The answer arrays are grown sixteen at a time, and a word is used only
// once - the count of words still to place is what stops the walk.
Ref
FillPreconditions(RefArg /*rcvr*/, RefArg templ)
{
	RefVar signature(GetFrameSlotRef(templ, RSSYMsignature));
	long signatureCount = Length(signature);
	RefVar preconditions(GetFrameSlotRef(templ, RSSYMpreconditions));
	RefVar input(GetFrameSlotRef(templ, RSSYMinput));
	long inputCount = Length(input);
	RefVar raw(GetFrameSlotRef(templ, RSSYMraw));
	long chunk = inputCount > 15 ? 16 : inputCount;
	long remaining = inputCount;
	RefVar filled;
	for (long s = 0; s < signatureCount && remaining > 0; s++)
	{
		filled = AllocateArray(RSSYMarray, chunk);
		RefVar wanted(GetArraySlotRef(signature, s));
		long found = 0;
		for (long i = 0; i < inputCount; i++)
		{
			if (NOTNIL(ISATest(RefVar(), RefVar(GetArraySlotRef(input, i)), wanted)))
			{
				SetArraySlot(filled, found, RefVar(GetArraySlotRef(raw, i)));
				found++;
				remaining--;
			}
			if (found == chunk && i + 1 < inputCount && found > 0)
				SetLength(filled, Length(filled) + chunk);
		}
		if (found != 0)
		{
			SetLength(filled, found);
			SetFrameSlot(templ, RefVar(GetArraySlotRef(preconditions, s)), filled);
		}
		if (remaining == 0)
			break;
	}
	return TRUEREF;
}


// ROM 0x000857b0 GetRelevantTemplates__FRC6RefVarT1
// The first word of every task that can act on something of that class -
// what the Assistant offers when the user has already picked the thing
// and only has to say what to do with it.
//
// The ROM's two quirks from GenFullCommands are here too: the clone of
// `task_list` is thrown away, because mashing it into `dynatemplates`
// answers `dynatemplates` and appends in place; and the default task is
// the one template left out.
Ref
GetRelevantTemplates(RefArg /*rcvr*/, RefArg theClass)
{
	RefVar templates(Clone(RefVar(GetFrameSlotRef(kAssistantFrame, RSSYMtask_list))));
	if (IsArray(RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates))))
		templates = UniqueAppendListGen(RefVar(),
										RefVar(GetFrameSlotRef(RefVar(gVarFrame), RSSYMdynatemplates)),
										templates);
	ULong count = Length(templates);
	RefVar words(AllocateArray(RSSYMarray, 0));
	for (ULong t = 0; t < count; t++)
	{
		RefVar templ(GetArraySlotRef(templates, t));
		if (EQRef(GetFrameSlotRef(kAssistantFrame, RSSYMdefault_task), templ))
			continue;
		RefVar signature(GetFrameSlotRef(templ, RSSYMsignature));
		ULong signatureCount = Length(signature);
		for (ULong s = 0; s < signatureCount; s++)
		{
			if (ISNIL(ISATest(RefVar(), theClass, RefVar(GetArraySlotRef(signature, s)))))
				continue;
			RefVar act(GetFrameSlotRef(templ, RSSYMprimary_act));
			if (IsSymbol(act))
				act = MapSymToFrame(RefVar(), act);
			if (!FrameHasSlot(act, RSSYMlexicon))
				continue;
			RefVar word(GetFrameSlotRef(act, RSSYMlexicon));
			if (IsArray(word))
				word = GetArraySlotRef(word, 0);
			if (IsArray(word))
				word = GetArraySlotRef(word, 0);
			UniqueAppendString(RefVar(), words, word);
			break;						// (the ROM ends the walk by setting the index)
		}
	}
	return Length(words) < 1 ? NILREF : (Ref) words;
}


// ROM 0x00084f90 DriveTaskSlip__FRC6RefVarT1
// The template's own `postparse` method run on it, which is what hands
// the finished task over to the application that registered it.
Ref
DriveTaskSlip(RefArg /*rcvr*/, RefArg templ)
{
	DoMessage(templ, RSSYMpostparse, RefVar());
	return TRUEREF;
}


void
RegisterAssistantNatives(void)
{
	RegisterNativeFunction("UniqueAppendList__FRC6RefVarN21", (void*) UniqueAppendList, 2);
	RegisterNativeFunction("UniqueAppendListGen__FRC6RefVarN21", (void*) UniqueAppendListGen, 2);
	RegisterNativeFunction("MapSymToFrame__FRC6RefVarT1", (void*) MapSymToFrame, 1);
	RegisterNativeFunction("GenFullCommands__FRC6RefVar", (void*) GenFullCommands, 0);
	RegisterNativeFunction("Append__FRC6RefVarN21", (void*) Append, 2);
	RegisterNativeFunction("CleanString__FRC6RefVarT1", (void*) CleanString, 1);
	RegisterNativeFunction("GenerateSubstrings__FRC6RefVarT1", (void*) GenerateSubstrings, 1);
	RegisterNativeFunction("CommonAncestors__FRC6RefVarN21", (void*) CommonAncestors, 2);
	RegisterNativeFunction("CompositeClass__FRC6RefVarT1", (void*) CompositeClass, 1);
	RegisterNativeFunction("FavorAction__FRC6RefVarT1", (void*) FavorAction, 1);
	RegisterNativeFunction("FavorObject__FRC6RefVarT1", (void*) FavorObject, 1);
	RegisterNativeFunction("RegTaskTemplate__FRC6RefVarT1", (void*) RegTaskTemplate, 1);
	RegisterNativeFunction("UnRegTaskTemplate__FRC6RefVarT1", (void*) UnRegTaskTemplate, 1);
	RegisterNativeFunction("GetRelevantTemplates__FRC6RefVarT1", (void*) GetRelevantTemplates, 1);
	RegisterNativeFunction("ISATest__FRC6RefVarN21", (void*) ISATest, 2);
	RegisterNativeFunction("PathToRoot__FRC6RefVarT1", (void*) PathToRoot, 1);
	RegisterNativeFunction("CheezyIntersect__FRC6RefVarN21", (void*) CheezyIntersect, 2);
	RegisterNativeFunction("CheezySubsumption__FRC6RefVarN21", (void*) CheezySubsumption, 2);
	RegisterNativeFunction("MakePhrasalLexEntry__FRC6RefVarT1", (void*) MakePhrasalLexEntry, 1);
	RegisterNativeFunction("GlueStrings__FRC6RefVarT1", (void*) GlueStrings, 1);
	RegisterNativeFunction("MakeLowerCase__FRC6RefVarT1", (void*) MakeLowerCase, 1);
}
