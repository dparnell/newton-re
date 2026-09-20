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
	function cites its origin.  NOT YET RECONSTRUCTED: the rest of the
	unit - the template registry (RegTaskTemplate 0x00085060,
	UnRegTaskTemplate 0x00085394), the matching itself
	(GetRelevantTemplates 0x000857b0, ISATest 0x00086004,
	CheezyIntersect 0x000862f0, CheezySubsumption 0x00086c24) and the
	string tidying (GlueStrings 0x00084064, CleanString 0x00084518, ...).
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

void	RegisterAssistantNatives(void);

#endif	/* __ASSISTANT_H */
