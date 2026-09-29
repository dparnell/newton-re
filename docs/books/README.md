# Books

The Newton's book reader: the "Help" book in the Extras drawer, and any
book a package brings (Newton Book Maker and BookMaker's `'book` parts).

## Status (2026-09-29)

| Piece | Where | State |
|---|---|---|
| The `'book` part handler (`TBookPartHandler`) | `src/books/BookPartHandler.cpp` | done |
| The library (`TLibrarian`, `gLibrarian`, `InitLibrarian`, `BookAvailable`/`BookRemoved`) | `src/books/Librarian.cpp` | done |
| The book functions over the Library soup (page, bookmarks, ink marks, `WhereIsBook`, ...) | `src/books/Librarian.cpp` | done (18 of 37 natives) |
| The help book's outline (`TOutline`, `THelpOutline`: view classes 102-107) | - | NOT YET |
| The page views (`PageTurnTo`, `MakeBlockView`, `AddToContentArea`, ...) | - | NOT YET |
| Searching a book (`TLibrarian::Find`, `FindPage*`, `FindContent*`, `CuFind`) | - | NOT YET |
| Ink marks (`AddInkMarks`) | - | NOT YET |

The booting OS installs the ROM's help book: it is in the library, and the
Extras drawer has its icon (`src/books/tests/books-library.ns`, ctest
`books.Library`).  It cannot be opened yet: its reader's outline view
(`THelpOutline`) is not reconstructed, and `RefreshTopics` is the first
native the reader asks for that is missing.

## The pieces and their sizes

Sized with `analysis/callgraph.py build/MP2x00US` from the natives and the
part handler (without `--through-done`): 108 functions not yet done, 39,116
bytes, of which C stdio reached from `QuickLookDone` is none of the book
reader's business.  By area:

| Area | ROM range | Bytes | Functions |
|---|---|---|---|
| `TLibrarian` and `TBookPartHandler` | 0x001082f8-0x0010d180 | 20,104 | 44 |
| of which the search (`Find`, `TextSearch`, `ExtractWords`, `Encode`, `CheckHints`, `CompareValues`, `Find*`, `CuFind`, `AddInkMarks`) | | ~9,500 | 17 |
| `TOutline`, `THelpOutline` and their three natives | 0x0014c49c-0x0014eefc | 10,848 | 40 |
| The page views (`ContentView`, `PageTurnTo`, `PageTurnToSpread`, `MakeBlockView`, `HiliteBlock`, `ZoomView`, `PageContents`, `PageScroll`, `PageThumbnail`, `PageTurnAway`, `AddToContentArea`) | 0x00163a04-0x00166efc, 0x00162860 | 13,672 | 16 |
| `ZoomRect` (the zoom animation `ZoomView` draws) | 0x003404e0 | | 1 |

Everything else the reader does is NewtonScript in the ROM - Copperfield
(the root's `copperfield`: 90 slots, the book reader with its browsers,
bookmarks, markup and routing) and Tiny Tim (the root's `TinyTim`, the
help-book reader: an outline and a content area) - and runs as it is.

## The book format

A `'book` part is a frames part whose top-level frame is
`{book: <book>}`, with `help: true` for a help book (which Tiny Tim reads,
not Copperfield).  The book frame (the ROM's help book, "GECKO!HDI?",
version 2):

| Slot | What |
|---|---|
| `isbn` | the book's identity: a string, interned as the library's key (`StrRefToSymbol`) |
| `title`, `shortTitle` | the title, and the Extras drawer's label (at most 63 characters) |
| `version` | 1 or 2; a version-1 book's page is 318 pixels tall whatever `pageSize` says (`PageSize`) |
| `author`, `publicationDate` | |
| `help` | a help book |
| `contents` | the content items, `{data, layout, name, viewFont, ...}` - the help book has 168 |
| `styles`, `templates` | what the items and pages are laid out with |
| `browsers` | the tables of contents, `[{name: "Contents", list: [...]}]` |
| `rendering` | one per screen shape the book was laid out for: `{pageSize, pages}`; the help book has one, 206x214 with 72 pages |
| `icon` | the Extras drawer icon (cloned) |
| `assist` | task templates registered with the Assistant while the book is in |
| `bookInstallScript`, `bookRemoveScript` | run with the book when it goes in and comes out |

A page is `{template: {nColumns, column: [...]}, blocks: [...]}`: the blocks
place content items on the page's columns.

## The library

`gLibrarian` (0xc bytes: the vtable, the library frame in a RefHandle, and a
count) keeps the library: a frame from each installed book's ISBN symbol to
its part frame.  What a reader has done to a book is kept apart from the
book, in the **Library** soup on the internal store
(`copperfield:CreateGetSoup()`, indexed on `isbn` and `bookPresent`), one
entry per ISBN whether the book is in or not: `curPage`, `prevPage`,
`curRendering`, `marks` and `inkMarks` (one array per rendering),
`bookPresent`, `packageId`, `data` (the book's own, `AuthorData`) and
`flags`.  A help book has no entry.

`BookAvailable(partFrame, packageId, source)` (0x0010c624) makes or revives
the entry, enters the book in the library, gives it an icon in the Extras
drawer - `SetupROMHelpBook` for a book in memory on no device (the ROM's),
`AddIcon` of a `proto1_2Exformentry` for one with no source or on a
removable version-1 store; a package on a store has its icon from the extras
soup - makes Copperfield one of `vars.findApps` with the first book that is
not a help book, and registers the book's task templates.  It answers `nil`
(not a book: no book or no ISBN, error -10401 from the part handler), `true`
(that ISBN is in already, -10402) or `{isbn, assist}`, which the part
handler keeps (with the `bookRemoveScript`, and `type: 2` for a package on a
store) and hands to `BookRemoved` (0x00108358) when the part goes.

### ROM bugs kept

- `BookAvailable` with no source (the NewtonScript `BookAvailable`) reads
  the source from address 0: the reset vector's first two bytes, 0xea and
  0x00, say "not in memory", so the book gets its icon by `AddIcon`.
- `BookRemoved` counts down for every book with a Library entry, whether or
  not `BookAvailable` counted it.
- `TBookPartHandler::Install` reports an error out of `Copy` as -10401.
- `TLibrarian::CurrentPage` resets a page beyond the book's end to 1 in the
  entry but answers the page beyond the end.
- `CurrentKiosk` at page 0 asks a page that is not there for its blocks.
- `PrepBookX` (`PrepBook`) is nothing but a debugging line printed on the
  REP.

## The order of the work

1. The part handler and the library - done.
2. `TOutline`/`THelpOutline`: the help book's topic list (the reader opens
   on it: `OpenHelpBook` is `Open` and then `Outline:RefreshTopics(0)`),
   drawn from the browser's list, topics expanded and collapsed, a tap
   turning the content area to the topic's page (`ClickCommand`).
3. The page views: `PageTurnTo` over `MakeBlockView` (a view per block of
   the page, made from the item's content and the page template's column),
   `AddToContentArea`, `TurnToPage`/`TurnToContent`; the milestone is the
   help book opened from the Extras drawer and a page drawn.
4. The search (`Find`, `FindPage*`, `FindContent*`, `CuFind`) and the ink
   marks.

## Registration

`TNewtWorld::MainConstructor` makes the `'book` handler with the other part
handlers (after `'form`, as the ROM does) and calls `RegisterBookNatives`;
`TNotebook::Constructor` calls `InitLibrarian` once the root view is up.
