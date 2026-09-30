/*
	File:		assist/Assistant.h

	Contains:	The Intelligent Assistant's C++ side: the part of the
				"Please ..." slip that is not written in NewtonScript.

				The Assistant matches what was written in the slip against
				a list of *task templates* - frames saying which words
				start a task ('Lexicon), what the task acts on
				('signature), and which action frame carries it out
				('primary_act).  The templates live in the Assistant's own
				frame (the magic pointer @8: its `task_list`) and in
				`vars.dynatemplates`, which an application adds to with
				RegTaskTemplate.

				What is here is the list building the slip needs:
				GenFullCommands, the words the Assistant knows, and the
				small list operations the rest of the Assistant is written
				in - appending without making duplicates, and looking a
				symbol up as a frame.

	Reconstructed from the MP2x00 US ROM (0x00084064-0x000871d0); each
	function cites its origin.  The template registry (RegTaskTemplate,
	UnRegTaskTemplate), the matching (GetRelevantTemplates, ISATest,
	CheezyIntersect, CheezySubsumption) and the string tidying
	(AssistStrings.h: GlueStrings, CleanString, ...) are here and beside it.
*/

#ifndef __ASSISTANT_H
#define __ASSISTANT_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"


// The Assistant's own frame, magic pointer 8, which holds `task_list`,
// `default_task` and `about_task` among much else.
const Ref	kAssistantFrame = MAKEMAGICPTR(8);


// `item` on the end of `list`; a nil list becomes an array of the one
// item, so the answer has to be used rather than the argument.
Ref		Append(RefArg rcvr, RefArg list, RefArg item);				// ROM 0x0008449c Append__FRC6RefVarN21

// The element of `list` that is EQ to `item`, or nil.
Ref		member_p(RefArg list, RefArg item);							// ROM 0x000871d0 member_p__FRC6RefVarT1

// The object cannot be written to (it is in the ROM or a package).
Boolean	IsReadOnly(RefArg obj);										// ROM 0x00086f60 IsReadOnly__FRC6RefVar

// AppendList(list, strings): the strings not already in the list
// appended to it (compared as strings, FindStringInArray).
Ref		UniqueAppendList(RefArg rcvr, RefArg list, RefArg items);	// ROM 0x00086e6c UniqueAppendList__FRC6RefVarN21

// MashLists(a, b): the two lists as one, with no duplicates.
Ref		UniqueAppendListGen(RefArg rcvr, RefArg a, RefArg b);		// ROM 0x00086f88 UniqueAppendListGen__FRC6RefVarN21

// One string appended when it is not in the list already.
Ref		UniqueAppendString(RefArg rcvr, RefArg list, RefArg str);	// ROM 0x000870e4 UniqueAppendString__FRC6RefVarN21

// One item appended when it is not in the list already (EQ, not strings).
Ref		UniqueAppendItem(RefArg rcvr, RefArg list, RefArg item);		// ROM 0x00087170 UniqueAppendItem__FRC6RefVarN21

// MapSymToFrame(sym): the frame the symbol names, looked for in the
// global variables and then in the Assistant's own frame.
Ref		MapSymToFrame(RefArg rcvr, RefArg sym);						// ROM 0x00084fd4 MapSymToFrame__FRC6RefVarT1

// GenFullCommands(): the first word of every task the Assistant knows -
// what the "Please ..." slip offers - or nil when there are none.
Ref		GenFullCommands(RefArg rcvr);								// ROM 0x00085bf0 GenFullCommands__FRC6RefVar


/*------------------------------------------------------------------------------
	T h e   c l a s s   h i e r a r c h y

	Everything the Assistant knows about is a frame with an `isa` slot
	naming the frame it is a kind of.  Two roots are kept in globals,
	which is how a verb is told from the thing it acts on.
------------------------------------------------------------------------------*/

extern Ref	gActionClass;		// ROM 0x0c100bc4 gActionClass - @8.action
extern Ref	gObjectClass;		// ROM 0x0c100bc8 gObjectClass - @8.user_obj

// The two roots read out of the Assistant's frame and made GC roots.
Ref		InitDSTaskTemplates(RefArg rcvr, RefArg arg);				// ROM 0x00084f14 InitDSTaskTemplates__FRC6RefVarT1

// Whether `thing` is a kind of `kind` (either may be a symbol).
Ref		ISATest(RefArg rcvr, RefArg thing, RefArg kind);				// ROM 0x00086004 ISATest__FRC6RefVarN21
// The thing and every frame above it, nearest first.
Ref		PathToRoot(RefArg rcvr, RefArg thing);						// ROM 0x000864cc PathToRoot__FRC6RefVarT1
// The elements two arrays have in common (see the definition: duplicates).
Ref		CheezyIntersect(RefArg rcvr, RefArg a, RefArg b);			// ROM 0x000862f0 CheezyIntersect__FRC6RefVarN21
// The frames above both of two things, nearest first; nil for none.
Ref		CommonAncestors(RefArg rcvr, RefArg a, RefArg b);			// ROM 0x00086670 CommonAncestors__FRC6RefVarN21
// The nearest class a whole list of things has in common.
Ref		CompositeClass(RefArg rcvr, RefArg things);					// ROM 0x00086764 CompositeClass__FRC6RefVarT1
// The things sorted into the kinds they are, each thing used once.
Ref		CheezySubsumption(RefArg rcvr, RefArg things, RefArg kinds);	// ROM 0x00086c24 CheezySubsumption__FRC6RefVarN21
// The first of the things that is a verb / that is not.
Ref		FavorAction(RefArg rcvr, RefArg things);						// ROM 0x00086974 FavorAction__FRC6RefVarT1
Ref		FavorObject(RefArg rcvr, RefArg things);						// ROM 0x00086acc FavorObject__FRC6RefVarT1
// One class per word of the sentence, out of what each word might mean.
Ref		GetClasses(RefArg words);									// ROM 0x00084b28 GetClasses__FRC6RefVar
// Whether the thing is the template's own action / a verb at all.
Ref		IsPrimaryAct(RefArg thing, RefArg templ);					// ROM 0x00084df8 IsPrimaryAct__FRC6RefVarT1
Ref		IsAction(RefArg thing);										// ROM 0x00084e98 IsAction__FRC6RefVar


/*------------------------------------------------------------------------------
	T h e   t a s k   t e m p l a t e s
------------------------------------------------------------------------------*/

// The frame being taken out of the lexicon, and the symbol that named
// it, while RemovePhrasalLexEntry is walking the trie.
extern Ref	gDynaDeleteSym;												// ROM 0x0c100b8c gDynaDeleteSYM
extern Ref	gDynaDeleteFrame;											// ROM 0x0c100b90 gDynaDeleteFrame

// A class's words put into the Assistant's own lexicon, and taken out
// again (NOT YET: the trie itself - see the definitions).
Ref		MakePhrasalLexEntry(RefArg rcvr, RefArg thing);				// ROM 0x0007d5b8 MakePhrasalLexEntry__FRC6RefVarT1
Ref		RemovePhrasalLexEntry(RefArg rcvr, RefArg thing);			// ROM 0x0007da24 RemovePhrasalLexEntry__FRC6RefVarT1

// RegTaskTemplate(templ): an application's task template registered in
// `vars.dynatemplates`; ==> the copy that was kept, or nil.
Ref		RegTaskTemplate(RefArg rcvr, RefArg templ);					// ROM 0x00085060 RegTaskTemplate__FRC6RefVarT1
// UnRegTaskTemplate(templ): and taken off again.
Ref		UnRegTaskTemplate(RefArg rcvr, RefArg templ);				// ROM 0x00085394 UnRegTaskTemplate__FRC6RefVarT1
// One thing the sentence named recorded in the slip's frame.
Ref		AddEntry(RefArg rcvr, RefArg theClass, RefArg path, RefArg alias, RefArg frame);	// ROM 0x00085510 AddEntry__FRC6RefVarN41
// The words of the sentence sorted into the slots the template wants.
Ref		FillPreconditions(RefArg rcvr, RefArg templ);				// ROM 0x00084810 FillPreconditions__FRC6RefVarT1
// GetRelevantTemplates(class): the first word of every task that can
// act on something of that class.
Ref		GetRelevantTemplates(RefArg rcvr, RefArg theClass);			// ROM 0x000857b0 GetRelevantTemplates__FRC6RefVarT1
// The template's `postparse` method run on it.
Ref		DriveTaskSlip(RefArg rcvr, RefArg templ);					// ROM 0x00084f90 DriveTaskSlip__FRC6RefVarT1

void	RegisterAssistantNatives(void);

#endif	/* __ASSISTANT_H */
