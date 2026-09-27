# The Intelligent Assistant (`src/assist/`)

The Assistant is the "Please ..." slip: you write a sentence, and it
works out which application should carry it out. The matching is done
against **task templates** - frames that say which words start the task
(`Lexicon`), what sort of thing it acts on (`signature`), and which
action frame carries it out (`primary_act`).

The templates live in two places: the Assistant's own frame, which is
magic pointer 8 (`@8.task_list`, ten tasks in the MP2x00 US ROM), and
`vars.dynatemplates`, which an application adds its own to with
`RegTaskTemplate`. Most of the Assistant is NewtonScript; what is in C++
is the list building, the class hierarchy and the matching arithmetic,
in one ROM file at 0x00084064-0x000871d0 - all of which is reconstructed
(`assist/Assistant.h`, `assist/AssistStrings.h`) - and the lexicon, the
phrase generator, the Names-file heuristics and the parse in the Data
Stream file (0x0007ce20-0x000831d8) and the IA code at 0x000e7048, which
are too (`assist/Lexicon.h`, `Phrases.h`, `Heuristics.h`,
`ParseUtter.h`).

## The list operations (`assist/Assistant.h`)

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

The list operations the rest of the Assistant is written in:

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

## The class hierarchy (`assist/Assistant.h`)

Everything the Assistant knows about is a frame with an `isa` slot naming
the frame it is a kind of, and the chain ends at a frame with no `isa`.
A slot may hold the frame itself or the symbol that names it, which
`MapSymToFrame` resolves — global variables first, then the Assistant's
own frame. Two roots are kept in globals because the Assistant needs to
tell a verb from the thing it acts on: `gActionClass` (`@8.action`) and
`gObjectClass` (`@8.user_obj`), both set by `InitDSTaskTemplates`
(0x00084f14) when the Assistant starts.

`ISATest` (0x00086004) is the only question the hierarchy is ever asked:
*is this a kind of that?* It walks `isa` up from the first, and answers
true if it reaches the second. Before the walk there is a shortcut worth
knowing about:

```c
if (EQRef(GetFrameSlotRef(a, RSSYMisa), GetFrameSlotRef(b, RSSYMisa))
    && EQRef(GetFrameSlotRef(a, RSSYMlexicon), GetFrameSlotRef(b, RSSYMlexicon)))
    return TRUEREF;
```

Two frames with the same `isa` *and* the same `lexicon` are taken to be
the same thing. That is there so that two lexicon entries made for the
same word compare equal without being the same object — but it also
means that any two frames with *neither* slot are each a kind of the
other, `nil` being equal to `nil`. `action` and `user_obj` are both
such frames, so on the ROM's own data `ISATest(action, user_obj)` and
`ISATest(user_obj, action)` are both true.

The rest is built on it:

| | |
|---|---|
| `PathToRoot` 0x000864cc | the frame and every frame above it, nearest first |
| `CheezyIntersect` 0x000862f0 | the elements two arrays share — two nested loops with no break, so an element twice in the longer array comes back twice |
| `CommonAncestors` 0x00086670 | the two paths to the root intersected |
| `CompositeClass` 0x00086764 | the nearest ancestor of a whole list, folded two at a time |
| `CheezySubsumption` 0x00086c24 | the things sorted into the kinds they are, each thing used once |
| `FavorAction` 0x00086974 / `FavorObject` 0x00086acc | the first of a list that is a verb / that is not |
| `GetClasses` 0x00084b28 | one class per word of the sentence |

`GetClasses` is where the reading of a sentence actually narrows down.
Every word came out of the lexicon as a *list* of the things it might
be; this picks one. Their common class, if they have one; otherwise —
and this is the interesting part — it depends on what has been seen
already. Before any verb, an ambiguous word is read as a verb
(`FavorAction`). After one, it is read as a thing (`FavorObject`),
unless that verb's frame has a `meta_level` slot, in which case another
verb is expected. So "find" then "Smith" reads Smith as a person, and a
verb that takes a verb after it says so with `meta_level`.

## The task templates (`assist/Assistant.h`)

A task template says how one sentence is carried out: `primary_act` is
the action that starts it, `signature` the sorts of thing it needs,
`preconditions` where each of those goes in the frame the slip is built
from, `taskslip` which slip to put up, and `postparse` the method that
carries it out (`DriveTaskSlip` 0x00084f90 runs it). The ROM's own ten
are in `@8.task_list`; an application registers its own.

`RegTaskTemplate` (0x00085060) is that registration. The template has to
have all eight of the slots above and a signature that is not nil; it is
then **deep-copied** with `TotalClone`, its signature's words are put
into the Assistant's lexicon, and the copy goes on the end of
`vars.dynatemplates`. The deep copy has a consequence an application has
to know about: a signature that names its class by *frame* comes back as
a copy of that frame, and `ISATest` will never match it against the
original again. Classes in a signature have to be named by **symbol**,
which `TotalClone` leaves alone because symbols are shared.

`UnRegTaskTemplate` (0x00085394) is the reverse, over `SetRemove`.

`GetRelevantTemplates` (0x000857b0) is what the slip asks when the user
has already picked the thing and only has to say what to do with it: the
first word of every task whose signature something of that class answers
to. On the MP2x00 US ROM, asked about a person, it answers

    ["schedule", "find", "mail", "fax", "call"]

out of the eight `GenFullCommands` lists — "print" and "time" want
something else, and the default task is never offered. It has the same
two quirks `GenFullCommands` has: the clone of `task_list` is thrown
away, because mashing it into `dynatemplates` answers `dynatemplates`
and appends in place, so a call leaves the ROM's own templates in the
application's list.

`FillPreconditions` (0x00084810) is the other half: once the sentence
has been read, its words are sorted into the slots the template wants
them in. `input` holds the class each word turned out to be and `raw`
the word itself, so a word whose class answers to the *n*th signature
entry goes into the slot the *n*th precondition names. A word is used
once, and the count of words still to place is what stops the walk
early. The arrays are grown sixteen at a time rather than counted
first.

`AddEntry` (0x00085510) records one named thing in the slip's frame: a
person on its `person` list and anything else on its `places` list, each
as a clone of `@8.iaref` carrying the entry (`alias`), its class
(`class`) and where it came from (`path`). The matching `personAdded` /
`placeAdded` flag is set so a second call for the same kind is ignored —
one person and one place is all a task slip takes.

## The string tidying (`assist/AssistStrings.h`)

Before any of that, the sentence is cleaned up. `CleanString`
(0x00084518) turns returns, line feeds and tabs into spaces in place;
`TrimBlanksAndPunct` (0x000845c0) takes white space and punctuation off
both ends; `MakeLowerCase` (0x000847c8) lowercases through the locale's
case table; `SplitString` and `GlueStrings` (0x00084064) take a sentence
apart into words and put it back together.

`GenerateSubstrings` (0x00084294) is the one that matters: every run of
consecutive words as a string of its own, the longest first. "call John
Smith" gives

    ["call John Smith", "call John", "John Smith", "call", "John", "Smith"]

which is (n² + n)/2 strings for n words — and that is how a phrase of
several words ("phone number", "what time is it in") is found in the
lexicon at all: every run is looked up, longest first, and the longest
one that is a phrase wins.

Two ROM bugs live here. `StringRightTrim` (0x0008418c, reconstructed in
`frames/StringNatives.cpp` with its neighbours) starts one past the
terminating nul, so its first step lands on the nul, which is not a
space, and it stops there every time: it trims nothing. And
`TrimBlanksAndPunct`'s list of marks ends with 0xc7 and 0xc8, which are
the *Mac Roman* codes for « and » — the string is Unicode by then, so
what is actually trimmed is Ç and È, and the guillemets are left on.
Both are kept.

## The lexicon (`assist/Lexicon.h`)

The words live in two Airus dictionaries. `gTrie` is the ROM's own (ROM
dictionary 2); a word's attribute indexes the array of lexicon entries at
magic pointer 248, each entry the list of things the word can mean
("call" is the call action, among others). `gDynaTrie` is built at run
time (`TrieInit` is `NewDictionary(0xf, 4)`): `MakePhrasalLexEntry` puts
every word of a class's or task template's `Lexicon` in it through
`TrieAdd`, the attribute indexing `gDynaDictionaryFrame`, whose entries
are `[count, frames]` - how many registrations the word has and the
frames it stands for. `RemovePhrasalLexEntry`/`DynaTrieDelete` count a
registration down and take the frame off the list; the last one takes the
word out of the trie and `DynaCompress` renumbers the attributes above it
(walking the trie in order with Airus's `FirstCompletion`/
`NextCompletion`, reconstructed for this).

`MatchString` is how a run of words is looked up: both tries (the
run-time meanings joined to the ROM's), each meaning copied with the run
as its `value` (`TagPhraseFrame`); else the locale's lexical dictionaries
- a date, a time, a phone number or a number, each a copy of the
Assistant's `lexical` frame with that class as its `isa`, in an array of
class `lex`; else the Names file (`DSResolveString`). One ROM bug here is
kept: a date word ("may", "june") seen for the first time is marked in
the parse's `exception` array by writing slot 2 - the value of the nil it
found there - rather than the word's own slot.

## The phrase generator (`assist/Phrases.h`)

Every run of consecutive words, longest first and left to right, from a
16 x 16 grid of bytes (4 untried, 1 a hit, 3 overlapped by a hit):
`IPhraseGenerator` marks every run of the first fifteen words untried,
`NextPhrase` hands the next one out and `PhraseHitExt` - called when the
lexicon knew it - strikes off every untried run overlapping it, so the
words of "Daniel Parnell" are not looked up again once the pair is a
person. `UnmatchedWords` is what is left: the words neither known nor
covered, the task's `noiseWords`. Two quirks are kept: each row after a
hit is scanned from the start before the hit's for as many runs as the
whole row has (reading past its end, into the next row of the grid), and
`PeekValidPhrase` does the same from the current start.

## The Names file and the helpers (`assist/Heuristics.h`)

A run no lexicon knows is tried as a person: `StringToFrameMapper` queries
the Names soup with the Assistant's `dsQuery` (at most fifteen cards; not
for a run of more than four words or with a one-letter word), and
`DSTagString` looks over each card slot by slot - the name, each of the
names of the people a card lists, the company, a custom field, the group,
the title - for one that contains every word of the run
(`DSPartialStrMatch`, case aside). Each hit is recorded in the parse
(`AddEntry`, through the card's alias) under what it matched. ROM bug
kept: a matching group falls through into the title's case, so it is
recorded as a title too.

The task scripts' helpers are here as well: `GuessAddressee` (the person
a letter's opening line is to - "Dear Mr Smith," looks up "Smith"),
`DSFindPossibleName`/`Location`/`Phone`, `DSConstructSubjectLine` (a
meeting's subject from its meal or scheduling word), the phone-number
conversions over `vars.PhoneTypes.phoneText`, and the four histories -
who, what, when and where, three each, newest first (`RecordHistory`).
ROM bug kept: `InitDSHeuristics` makes `gWhoObj` a GC root twice and
`gWhatObj` never.

## The parse (`assist/ParseUtter.h`)

`ParseUtter(sentence)` opens the Assist slip with the sentence, checks it
(`IAInputErrors`: not blank, under sixteen words) and has the progress box
(`startIAProgress`) run `IaAtWork`, which feeds every run of words (its
trailing punctuation taken off) to `MatchString` and answers the runs'
meanings, the runs, their classes (`GetClasses`) and the people and
places found. The task is the template that makes the first action among
the classes its primary act; failing that the one whose signature the
classes fit best, scored `((covered * 2 - classes) * 1000) / needed` (ROM
quirk kept: the score the template is given is the last one worked out,
not necessarily the winner's). A copy gets the parse, the sentence and
the entries, `FillPreconditions` sorts the words into its slots and
`DriveTaskSlip` opens the task's slip. `InitDarkStar`, run by
`TNotebook::InitToolbox` after the init scripts as the ROM does, sets all
of it up.

`src/host/demo/assist.ns` asks "call Daniel" after the Setup walk and the
Call slip opens addressed to the owner card. `test_Assistant` tests the
lexicon (the ROM trie, run-time registration and removal), the phrase
generator and the string helpers.

## Not yet reconstructed

`DoPopup`'s scrolling `canonicalPopup` (the Assist slip's pickers use a
plain one); the Airus dictionary cursor's `NextSet9`.
