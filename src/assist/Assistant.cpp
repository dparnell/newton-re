/*
	File:		assist/Assistant.cpp

	Contains:	The Intelligent Assistant's list building (Assistant.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Assistant.h"

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


void
RegisterAssistantNatives(void)
{
	RegisterNativeFunction("UniqueAppendList__FRC6RefVarN21", (void*) UniqueAppendList, 2);
	RegisterNativeFunction("UniqueAppendListGen__FRC6RefVarN21", (void*) UniqueAppendListGen, 2);
	RegisterNativeFunction("MapSymToFrame__FRC6RefVarT1", (void*) MapSymToFrame, 1);
	RegisterNativeFunction("GenFullCommands__FRC6RefVar", (void*) GenFullCommands, 0);
}
