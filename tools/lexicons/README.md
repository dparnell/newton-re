# tools/lexicons: the recognisers' lexicons as text

The ROM carries 40 lexicons that the handwriting recognisers, the
Assistant and the date, time and number readers look words up in: word
lists (the general words, first names, cities, countries, states,
honorifics, the days and months, prefixes and suffixes, the Assistant's
words...) and lexical grammars (dates, times, numbers, money, phone and
ID numbers, postcodes, quotes and punctuation). Each is an Airus
dictionary behind a big-endian size word (`docs/recognition/README.md`,
"The Airus engine" and "The ROM's word data"). `romsrc/lexicons/` keeps
them as text, and `newtonlex.py` makes the ROM's bytes out of the text -
exactly the ROM's bytes for the committed files.

## The word lists (`.words`)

The enumerated dictionaries (`gEnum8*`, `gSymb8*`; the type byte's low
three bits 3, 5 or 7) are tries of characters. Their text is the words,
one to a line:

    type 0x17
    about newton	2
    anniversary	1

`type` is the dictionary's second byte - its kind in the low three bits
and the size of the attribute each word carries in the high four. When
that size is not nought each word has its attribute after a tab (a
number). Lines beginning `#` are comments.

The trie is a function of the set of words: each row's nodes in the order
of their character codes, each node's sibling offset in as few nibbles as
hold it (one, three or seven). So the lines may be in any order, and a
word is added or taken out by adding or taking out its line. The
unpacker writes them in the trie's order, which is the character codes'.

## The lexical graphs (`.lex`)

The ROM lexicon kind (`gLex8*`; kind 1 or 2) is a graph whose nodes each
stand for a *set* of characters that all lead to the same place - "a
digit, then a digit, then a hyphen" - with the sets and the nodes below
them shared. Its text is the sets and the nodes in the order they lie in
the data:

    type 0x01
    set s0 "0123456789" "t0"
    set s1 "-" "t1"
    node n0 s0 last -> n9
    ...
    node n10 s0 word last

A set is its characters and the tag the recogniser is told when one of
them matched (`AL_GetAttribute2`, the string after the set). A node names
its set, says `word` if a word may end there and `last` if it is the last
of its row, and `-> n` names its first child; the first row is the
root's. A node's siblings are the nodes after it up to the one marked
`last`. `flags=` keeps any flag bit beyond those three and `attr=` the
node's attribute when the dictionary gives one. The builder lays the
nodes out from offset 2 and then the sets, so names may be changed, nodes
and sets added, children pointed elsewhere; offsets are sixteen bits, so
a graph stays under 64 KB.

## Characters

Characters are the Newton's 8-bit encoding, Mac Roman, written as UTF-8.
`\xNN` stands for a control character, `\\` for a backslash, and a space
at either end of a word is written `\x20` so that an editor cannot lose
it; inside a `.lex` set's quotes `\"` is a quote. A sixteen-bit
dictionary (none is in the ROM) is written in Unicode, `\uNNNN` for what
does not print.

## Use

    python tools/lexicons/newtonlex.py unpack romsrc/lexicons/X.bin X.words   # (or .lex: by the dictionary's type)
    python tools/lexicons/newtonlex.py pack X.words X.bin                    # the size word, then the dictionary
    python tools/lexicons/newtonlex.py check X.bin...                        # unpacked and packed back: the same?

`romsrc.py extract` writes each lexicon this way (falling back on `.bin`
for one that would not come back the same; none does), and `romsrc.py
build` packs them. A lexicon an edit has made bigger than the room it had
in the ROM (the distance to the next block of ROM data) is only built
with `--relayout`, which puts it at an address of its own
(`MOVED_DATA_BASE`) and records the move in the object file's moved
table; the host's `InitROMDictionaryData` finds it there
(`ROMMovedAddress`, `frames/ROMImport.h`).

Tests: `test_newtonlex.py [--rom build/MP2x00US]` (ctests
`tools.NewtonLexicons`, `tools.NewtonLexiconsROM`: every committed
lexicon packs to the ROM's bytes and unpacks to its committed text, and
edits read back as they should); `lexicon_edit_test.py` (ctest
`host.ROMSourceLexiconEdit`: "zorbleflax" added to
`gEnum80sh_words.words`, the tree built, and `LookupWord` on the booted
machine knows it).
