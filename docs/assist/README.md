# The Intelligent Assistant (`src/assist/`)

The Assistant is the "Please ..." slip: you write a sentence, and it
works out which application should carry it out. The matching is done
against **task templates** - frames that say which words start the task
(`Lexicon`), what sort of thing it acts on (`signature`), and which
action frame carries it out (`primary_act`).

The templates live in two places: the Assistant's own frame, which is
magic pointer 8 (`@8.task_list`, 98 slots in the MP2x00 US ROM), and
`vars.dynatemplates`, which an application adds its own to with
`RegTaskTemplate`. Most of the Assistant is NewtonScript; what is in C++
is the list building and the matching arithmetic, in one ROM file at
0x00084064-0x000871d0.

## What is reconstructed (`assist/Assistant.h`)

`GenFullCommands` 0x00085bf0 answers the first word of every task the
Assistant knows, which is the list the slip offers. It clones
`@8.task_list`, mashes `vars.dynatemplates` in, drops the two templates
that are not commands (`@8.default_task` and `@8.about_task`), and for
each of the rest takes `primary_act` - a symbol being looked up as a
frame with `MapSymToFrame` - and the first word of that action's
`Lexicon`, which may be a list of words or a list of lists of them.
On the US ROM it answers

    ["schedule", "find", "remember", "mail", "fax", "print", "call", "time"]

Two things it does for no reason, both kept: the clone of `task_list` is
thrown away (the mash answers `dynatemplates`, not the clone), and the
`signature` slot is fetched and its length taken without the answer
being used. And because the mash appends *in place*, a call leaves the
ROM's own templates in `vars.dynatemplates` - so the application's list
of templates grows the first time the slip is opened.

The list operations the rest of the Assistant is written in are here
too, since they are what `GenFullCommands` is made of:

| | |
|---|---|
| `Append` 0x0008449c | an item on the end of a list, making the list when it is nil |
| `member_p` 0x000871d0 | the element EQ to an item, or nil - Lisp's `member`, answering the element rather than the tail |
| `IsReadOnly` 0x00086f60 | the object's header says it cannot be written to (the ROM, a package) |
| `UniqueAppendItem` 0x00087170 | appended unless `member_p` finds it |
| `UniqueAppendString` 0x000870e4 | appended unless `FindStringInArray` finds it |
| `UniqueAppendList` 0x00086e6c (`AppendList`) | each of a list of strings so |
| `UniqueAppendListGen` 0x00086f88 (`MashLists`) | two lists as one, read-only lists cloned first |
| `MapSymToFrame` 0x00084fd4 | a symbol as a global variable, else as a slot of `@8` |

## Not yet reconstructed

The rest of that file: the template registry (`RegTaskTemplate`
0x00085060, `UnRegTaskTemplate` 0x00085394, `AddEntry` 0x00085510), the
matching itself (`GetRelevantTemplates` 0x000857b0, `ISATest`
0x00086004, `CheezyIntersect` 0x000862f0, `CheezySubsumption`
0x00086c24, `PathToRoot` 0x000864cc, `CommonAncestors` 0x00086670,
`CompositeClass` 0x00086764, `FavorAction` 0x00086974, `FavorObject`
0x00086acc) and the string tidying the sentence goes through
(`GlueStrings` 0x00084064, `CleanString` 0x00084518,
`TrimBlanksAndPunct` 0x000845c0, `GenerateSubstrings` 0x00084294,
`FillPreconditions` 0x00084810, `GetClasses` 0x00084b28).

The words themselves are looked up in the Airus lexical dictionaries
(`FAirusIteratorMake` 0x0008f4e8, `FAirusWalkDictionary` 0x0008f44c over
the `AirusAParmBlock` engine at 0x00029944 and up), which are a
subsystem of their own and not reconstructed at all.
