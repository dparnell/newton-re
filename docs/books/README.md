# Books

The Newton's book reader: the "Help" book in the Extras drawer, and any
book a package brings (Newton Book Maker and BookMaker's `'book` parts).

## Status (2026-09-29)

| Piece | Where | State |
|---|---|---|
| The `'book` part handler (`TBookPartHandler`) | `src/books/BookPartHandler.cpp` | done |
| The library (`TLibrarian`, `gLibrarian`, `InitLibrarian`, `BookAvailable`/`BookRemoved`) | `src/books/Librarian.cpp` | done |
| The book functions over the Library soup (page, bookmarks, ink marks, `WhereIsBook`, ...) | `src/books/Librarian.cpp` | done |
| The outline (`TOutline`, `THelpOutline`: view classes 102-107, `RefreshTopics`, `TopicByName`, `ScrollToCurrent`) | `src/books/Outline.cpp` | done |
| The page views (`PageTurnTo`, `PageTurnToSpread`, `PageTurnAway`, `MakeBlockView`, `AddToContentArea`, `TurnToPage`, `HiliteBlock`) | `src/books/Pages.cpp` | done |
| `FindContentByValue`, `FindPageByContent`, `CompareValues` | `src/books/Librarian.cpp` | done |
| The search (`TLibrarian::Find` over the hints, `TextSearch`, `ExtractWords`, `CuFind`, `FindPageByValue`/`BySubject`, `FindContentBySlot`, `TurnToContent`) and `AddInkMarks` | `src/books/Search.cpp` | done |
| `PageContents`, `PageScroll`, `ZoomView` (over `qd/ZoomRect.cpp`'s `ZoomRect`/`FixStep`) | `src/books/Pages.cpp` | done |
| `PageThumbnail` (a `TRemoteView`, class 88 - `views/RemoteView.h` - of the page's blocks) | `src/books/Pages.cpp` | done; the thumbnail shows only its top-left corner, as the ROM's does (below) |
| Ink marks (`AddInkMarks`) | - | NOT YET |

The booting OS installs the ROM's help book: it is in the library, and the
Extras drawer has its icon (`src/books/tests/books-library.ns`, ctest
`books.Library`).  It opens and reads: Tiny Tim's `OpenHelpBook` (what the
Extras drawer's Help icon runs) shows the outline of the book's topics, a
tap opens "Learn the Basics", and a tap on "Erase text, writing, or
drawings" turns the content area to its page - the text and the zigzag
picture (`src/books/tests/books-help.ns`, ctest `books.HelpBook`, which
writes `tmp/books-help-{outline,topics,page}.pgm`).  Copperfield reads a
book that is not a help book - a copy of the help book under an ISBN of its
own, put in by `BookAvailable` as a package's part is: `OpenBook`,
`TurnToPage`, `ScrollPage`, the page kept in the Library soup, bookmarks
(`src/books/tests/books-copperfield.ns`, ctest `books.Copperfield`).  No
package with a `'book` part is in `fixtures/` yet, so the part handler has
only installed the ROM's own.  Copperfield draws the book's title over the
top of a help book's page, whose pages are laid out for Tiny Tim, which
shows no title; the ROM's layout is presumably the same (not checked on a
MessagePad).

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

### Finding things (`Search.cpp`)

A book may carry `hints`: one binary (class `'data`) per content item, a bit
set of the three-letter runs of its text, each run coded in fifteen bits over
the five-bit alphabet `FiveBitASCII_Adobe` (which folds cases and the
accented letters together; generated by `romtable.py` into `BookTables.cpp`).
`TLibrarian::Find` - the Find slip's `CuFind` - codes the word the same way
and passes over an item whose hints lack a run of it, then searches the
item's text a character at a time (`TextSearch`: a to z folded, and only at
the start of a word), each place found an entry whose title is the words
round it between ellipses (`ExtractWords`).  A book's `bookSearchScript` and
`mungeContentScript`, and a form's `formSearchScript`, take part.

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
- `TLibrarian::Find` searches no book without `hints`, keeps one book's
  search scripts for the books after it, and answers the last book entry it
  made whether or not the last book had anything.
- `TextSearch` does not try the character that breaks a partial match as
  the start of a new one ("aab" does not contain "ab").
- `FindPageByValue` with book `true` (the first only) stops at the first
  block on each page, not at the first page.
- A page thumbnail shows only the part of the page that lands in its
  top-left 17x23 pixels.  The text is drawn at the right scale (each line
  27 to 51 pixels wide in the 60-pixel bitmap), but it is clipped:
  `TRemoteView::RealDraw` maps the port's clip back to the page's
  coordinates for the scaler to map forward again, and not the visible
  region, which the scaler (`mapVis`) maps forward all the same - the
  thumbnail view's visible region (its clipper's: its own bounds, 0..60)
  comes out 0..17.  Traced on the host with the drawing's rectangles
  printed at each stage, and every step checked against the ROM's code
  (`ScaledText`, `DrTextChunk`, `StretchBits`, `SetupScalingRegions`,
  `StartScaling`, `ViewIntoBitmap`, `TView::Constructor`, `TView::Draw`).
- `THelpOutline::DerivedFrom` asks `TView`, so a help outline does not say it
  is a `TOutline`.
- `TOutline::FindTopic` compares the pen, made relative to the view's left,
  with half the view's right edge on the screen.
- `TOutline::ScrollToSelection` keeps the position from going below nought
  scrolling up but not scrolling down.
- `TOutline::AddTopic` marks the word before the topic array when the first
  topic is deeper than level 1 (the host leaves it alone), and a topic with
  no shallower topic before it keeps a stack-rubbish parent (nought here).

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
