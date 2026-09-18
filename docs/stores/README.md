# The persistent store system (PSS)

How the ROM keeps data that survives a reset, from the bottom up: the
object store protocol `TStore`, its implementations, and (to come) the
frames layer over it - `TStoreWrapper`, the soups, their indexes and the
NewtonScript store/soup/cursor functions.  Reconstructed source is under
`src/stores/`.

How these facts were established: the protocol's method list is the
dispatch table `tools/newton-rom/analysis/classinfo.py build/MP2x00US --name
TPackageStore` (or `TFlashStore`, `TMuxStore`) decodes; the semantics come
from decompiling the three implementations (`analysis/decompile.py --class
TFlashStore` etc.) and the callers in `TStoreWrapper`; the error codes are
`headers/OS600/OSErrors.h`'s `kSError_...` (`kStoreError_Base` -10600).

## TStore (`src/stores/Store.h`)

A store holds *objects*: byte arrays addressed by a `PSSId` (an unsigned
long; 0 is never an id).  One object is the *root*, whose id `GetRootId`
answers (0x27 on a flash store, the first word of a package store's
data); everything else is reached from it.  The 42 methods (dispatch
slots 4-45; slots 0-3 are the protocol's own):

| method | what |
|---|---|
| `Init(address, size, arg3, socket, flags, pssInfo)` | the store's memory or driver; flags 0x01 card, 0x08 internal, 0x10 pssInfo is the `TFlash` |
| `NeedsFormat(Boolean*)`, `Format()` | a store that has never been formatted, and formatting it (the root object made) |
| `NewObject(size, PSSId*)`, `NewObject(data, size, PSSId*)` | a new object, zero-filled or copied from data |
| `ReplaceObject(id, data, size)` | the contents replaced |
| `DeleteObject(id)`, `EraseObject(id)` | (EraseObject is a no-op in every implementation) |
| `SetObjectSize(id, size)`, `GetObjectSize(id, long*)` | grown zero-filled or cut |
| `Read(id, offset, buffer, count)`, `Write(...)` | a range of the object; a range past the end is `kSError_ObjectOverRun` (`Read` still copies what there is) |
| `GetStoreSizes(long* total, long* used)` | |
| `IsReadOnly(Boolean*)`, `IsROM()`, `StoreKind()` | "Internal", "Flash storage card", "Storage card", "Application card", "Package" |
| `LockStore()`, `UnlockStore()`, `IsLocked()`, `Abort()`, `InTransaction()` | the transaction, below |
| `NewWithinTransaction`, `StartTransactionAgainst`, `SeparatelyAbort`, `AddToCurrentTransaction`, `InSeparateTransaction` | separate transactions, below |
| `LockReadOnly()`, `UnlockReadOnly(reset)` | a counted lock that makes every change `kSError_WriteProtected` |
| `NextObject(id, PSSId*)` | iteration (0 to start, 0 at the end); a package store has none |
| `OwnsObject(id)`, `Address(id)` | whether the id is the store's; the object's bytes in memory (nil when not addressable) |
| `Idle`, `CheckIntegrity`, `SetBuddy`, `SetStore(store, arg)`, `IsSameStore(data, size)`, `VppOff`, `Sleep` | housekeeping (SetStore gives a `TMuxStore` its store) |
| `CalcXIPObjectSize`, `NewXIPObject`, `GetXIPObjectInfo` | execute-in-place objects (flash cards only; `kError_XIP_Not_Possible` elsewhere) |

### Transactions

`LockStore` counts; while the count is above zero the changes made are one
transaction, which **commits when the count falls back to zero** in
`UnlockStore` (`TFlashStore::UnlockStore` at count 1: `MarkCommitPoint`,
`DoCommit`) and is **undone by `Abort`**, which also drops the locks
(`TFlashStore::DoAbort`).  Every modifying method locks the store around
its own work (`TFlashStore::NewObject`, `Write`, ... call `LockStore` /
`SetupForModify` / `UnlockStore`), so a change made with nothing else
locked commits at once; `SetupForModify` also starts the transaction
(`StartTransaction`) and refuses a read-only or write-protected store
(`kSError_WriteProtected`), and a needed recovery leaves the store
`kSError_WPButNeedsRepair`.

An object can be put in a *separate transaction*, which the store-wide
commit and abort leave alone: `NewWithinTransaction(size, id)` makes such
an object, `StartTransactionAgainst(id)` moves an existing one into its
own (from its state at that moment), `SeparatelyAbort(id)` undoes it,
`AddToCurrentTransaction(id)` hands it back to the store's transaction
(committing it at once when nothing is locked), `InSeparateTransaction(id)`
asks.  The flash store keeps this as a "separate tranny" bit in the
object's state word (`TObjRef::SetSeparateTranny`); the frames layer uses
it for large binaries written piecemeal.

### Implementations

* **`TFlashStore`** (ROM 0x000c1e14-0x000cbea0 and `TFlashBlock`,
  `TFlashPhysBlock`, `TFlashTracker`, `TFlashIterator`,
  `TFlashStoreLookupCache`): the internal store and the flash cards.  A
  log-structured store over erase blocks: objects are written to free
  space with a state byte (`gObjectTransBitsToState` maps the state bits
  to 15 states: committed, new-in-transaction, superseded, deleted, ...),
  a change writes a new copy and marks the old superseded, commit and
  abort walk the log flipping states, `GC` compacts blocks.  NOT
  RECONSTRUCTED: it is the flash hardware's; the host has `THostStore`.
* **`TMuxStore`** (0x00124d2c-0x00125f88): a `TStore` wrapping another
  store (`SetStore`) so that several tasks can use it: each call takes a
  `TULockingSemaphore`, and the modifying calls go through a
  `TMuxStoreMonitor` (a `TStoreMonitor`, run as a monitor task) while
  reads call the store directly.  `InitPSSManager` (0x001575f8) makes the
  internal store this way - a `TMuxStore` over a `TFlashStore` -
  formats it if it needs formatting and registers it with the name server
  as "InRAMStore"/"TStore" (`gInRAMStore`, `gMuxInRAMStore`).  NOT YET
  RECONSTRUCTED.
* **`TPackageStore`** (`src/stores/PackageStore.cpp`, ROM
  0x001625ec-0x00162b2c): read-only, over a package's soup part.  The data
  is a directory - root id, object count, `count + 1` offsets from the
  start of the data - followed by the objects; every change answers
  `kSError_WriteProtected`, transactions only count the locks.
  `InitPackageSoups` (0x00162a1c) registers it and the
  `TPackageStorePartHandler` for parts of type `'soup`; `gPackageStores`
  holds the store frames made from mounted packages.
* **`THostStore`** (`src/stores/host/HostStore.cpp`): the host's internal
  store, kept in host memory - a re-expression of the contract above, not
  a reconstruction (no ROM citations): objects in a table by id, ids never
  reused, root 0x27, a per-object undo record journaled at an object's
  first change in a transaction (dropped at commit, applied by `Abort`),
  the separate-transaction flag, the read-only lock, `NextObject`,
  `Address` (the object's bytes).  `RegisterStoreImplementations` puts it
  and `TPackageStore` in the protocol registry; `test_Store` exercises both
  through `TStore::New` by name.

## The frames layer's view of a store (`src/stores/StoreWrapper.h`)

`TStoreWrapper` (ROM 0x00328790-0x003299fc; 0x98 bytes) is what the frames
code holds for a store: the `TStore`, two `TStoreHashTable`s - the *map
table* and the *symbol table* - with caches of the last eight maps and
sixteen symbols read back, the `TNodeCache` of the store's soup indexes,
the dirty flag and (NOT YET) the ephemeral tracker.  `Dirty` locks the
store and asks for the flush task (`AskForFlush`), `SparklingClean` unlocks
it; `LockStore`/`UnlockStore`/`Abort` pass to the store, Abort also
clearing the node cache and re-reading both tables' buckets.

A frame goes to the store as a **map reference** plus its slot values in
a canonical order: `FrameToMapReference` sorts the frame's tags by symbol
(`GetFrameMapTags`: hash, then name - except a function's or an argFrame's
slots, which keep their order), drops `_proto` (and, for soup entries,
`_uniqueID` and `_modTime`), packs the names (2-byte count, then NUL
terminated names) and enters them in the map table under a hash of the
names (`AddMap`: each name's hash rotated into the running value by the
count so far); it hands back `indexes[]`, the frame slot each stored slot
comes from.  `ReferenceToMap` rebuilds the frame map (`AllocateMapWithTags`
of the interned names).  A symbol goes as a **symbol reference** into the
symbol table (`SymbolToReference`: its name under its hash) and comes back
interned (`ReferenceToSymbol`).  `StartCopyMaps_Symbols`/`CopyMap`/
`CopySymbol`/`EndCopyMaps_Symbols` translate references when a store's
objects are copied to another store, remembering the last 16 maps and 32
symbols.

`TStoreHashTable` (0x00328158-0x0032859c) is the on-store hash table both
tables are: a 256-byte table object of 64 bucket ids (`Create` makes one),
each bucket an object of `[2-byte length][bytes]` entries.  `Insert(hash,
bytes)` looks through bucket `hash & 63` (through a `TCachedReadStore`)
and appends the entry when new; the *reference* it answers is
`bucket << 16 | offset`, which `Get` reads back; `TStoreHashTableIterator`
walks every entry; `Abort` re-reads the bucket ids after the store aborted.
`TCachedReadStore` (0x003299fc-0x00329ce4) reads an object once into a
1 KB buffer (or one of the object's size) and answers pointers into it.

The store's **root object** is a `StoreRootData`: `'WALY'`, version 4, the
map table id, the symbol table id, the root frame id (`ReadStoreRootData`
0x00326dac reads it; `MakeStoreObject` 0x00328fdc writes it when a store is
first used - NOT YET, it needs the object writer and the soup name index).
On-store words are 32-bit (`ULong32`, `StorePSSId`) where the host's
`ULong` is pointer-sized.

`TNodeCache` (`NodeCache.h`, 0x002c3ad8-0x002c4174; 0x10 bytes) caches the
B-tree nodes the soup indexes read: a handle of entries (node id, its
512-byte buffer, duplicate-node and dirty flags, a use stamp, the index it
belongs to, in-use), growing when every entry is in use and reusing the
least recently used otherwise; `Commit` writes an index's dirty nodes back
through it and trims the cache to eight entries; `DeleteNode` deletes a
node's object (both in `SoupIndex.cpp`).  `test_StoreWrapper` runs the
tables, the wrapper and the cache over a `THostStore`.

## Store objects (`src/stores/StoreObject.h`)

A frames object lives on a store as a *store object* that
`TStoreObjectWriter` (ROM 0x002b7a30-0x002b8b8c) writes and
`TStoreObjectReader` (0x002b8b8c-0x002b9500) reads, through
`StorePermObject`/`LoadPermObject`/`DeletePermObject`
(0x002b96cc-0x002b9dcc): a 16-byte header (`_uniqueID`, `_modTime`, the id
of the text object, the number of 8-byte hint chunks, flags, the text's
size), the hint chunks (the word hints soup queries test with
`TestObjHints`; NOT YET: none are written) and a tagged byte stream:

| tag | object | what follows |
|---|---|---|
| 0 | an immediate (integer, true, magic pointer) | the ref itself as a long |
| 1, 2 | a character | 1 or 2 bytes |
| 3 | a binary | length, the class (an object), the data |
| 4, 5 | an array (5: of class `array`) | length, [the class], the elements |
| 6 | a frame | a 3-byte map reference, the slots in the map's (sorted) order |
| 7 | a symbol | a 3-byte symbol reference |
| 8 | a string | length, the class; the text goes to the text object |
| 9 | a precedent (an object already written) | its index |
| 10 | nil | |
| 11 | a small rect (`{top, left, bottom, right}` 0..255) | 4 bytes |
| 12 | a large binary | id, size (NOT YET) |

A long is one byte for 0..254, else 0xff and four bytes; every word is
big-endian, the MessagePad's order (the host converts the header's words
and the strings' UniChars - `toolbox/ByteOrder.h`).  Every pointer object
is entered in the precedent table in the order met (`Prescan` first, to
size the stream and the text; `Scan` to write), so shared references
come back shared; an entry's `_uniqueID` and `_modTime` go in the header
and `_proto` is never written.  The strings' text is gathered in a second
object compressed by `TUnicodeCompressor` (`CompressionType` 2; the
stream itself is type 1, uncompressed).

`TStoreWritePipe` (0x002b6d4c-0x002b7394) assembles a small object whole
in a buffer (the object is made from it at `Complete`) and writes a large
one as its 512-byte buffer fills; with a compressor its output goes to
the object.  `TStoreReadPipe` (0x002b74a8-0x002b7998) reads through a
256-byte buffer, or through the decompressor.  `TBucketArray`
(0x0032a574) holds elements in buckets of 64 so that they never move;
`TPrecedentsForReading` is one of refs, `TPrecedentsForWriting` one of
refs the ROM searches with a PATRICIA trie over the ref bits (DEVIATION:
a hash table here; the stream is the same).  Both register with the
collector (their refs are marked and updated; the writing table is
rebuilt after a collection).  `test_StoreWrapper` round-trips objects of
every kind, shared references, entries, rewrites in place, a 300-element
array with text past the pipes' buffers, and the errors.

## Soup indexes (`src/stores/SoupIndex.h`)

A soup index is a B-tree of keys with their data (an entry's unique id,
usually) in 512-byte nodes on the store: `TSoupIndex` (0x002c0a10-0x002c7370,
0x44 bytes; the pure interface `TAbstractSoupIndex` above it is what the
cursors use).  The formats, from the node and key-field primitives (`kf*`,
`KeyFieldAdr`, `LeftNodeNo`, `PutKeyIntoNode`, `DeleteKeyFromNode`,
`InitNode`, `ReadANode`, `UpdateNode`):

| Thing | Layout |
| --- | --- |
| `SKey` (0x50) | byte 0 flags (multi-key: bit *n* = sub-key *n* missing; bit 7 = a shorter key sorts after a longer one), byte 1 the data size, then up to 0x4e bytes of data.  Fixed-size key types (long 4, char 2, double 8) have no header: the `SKey` is the raw value.  Strings are UniChars with the terminator; a multi-key is the sub-keys' `SKey`s in turn, each padded even. |
| key field | 2-byte header (top 2 bits flags: 1 = has duplicates; low 14 bits the size), the key (padded even), the data entries (each padded even); a field with duplicates ends with a 2-byte count of the data in the field and the 4-byte id of its first dup node.  At most 100 bytes (`kfAssembleKeyField` throws -48022 for key + data + 4 > 100). |
| `NodeHeader` | id, parent id, bytes remaining (short), number of keys (short), numKeys + 1 offsets (shorts) to the key fields, which are packed at the end of the node, each preceded by the 4-byte id of the child to its left; the last offset is to an empty field whose left child is the rightmost child.  A new node has bytesRemaining = size - 0x14.  On the store the node is compact (header, offsets, then the key data; `UpdateNode`), expanded when read (the key data moved to the end, the gap zeroed; `ReadANode`).  `RoomInNode`: field + 6 < remaining; `NodeUnderflow`: remaining > size / 2. |
| `DupNodeHeader` | id, next dup node id, bytes remaining, count, the offset of the end of the data, 0; the data from 0x10. |
| `IndexInfo` (0x1c, the object `fInfoId`) | root node id (0: none), node size (0x200), key type, data type, duplicates (0 unique keys, 1 likewise, 2 allowed - `IndexDescToIndexInfo` at 0x0031dbcc gives 0 for the `_uniqueID` index), the multi-key's sub-key types (4 bits each, lowest first; unused 0xf), its ascending bits (a bit per sub-key; clear = that sub-key descends), a descending byte for the whole index. |

Key types (`fKeyCompareFns` at 0x0c102508, `fKeySizes` at 0x0c102524): 0
string (`CompareUnicodeText` with the index's `TSortingTable` - NOT YET
RECONSTRUCTED, letters are folded), 1 long, 2 char, 3 double, 4 ASCII
(case-folded bytes), 5 raw (`memcmp`, the shorter less), 6 multi (sub-key
by sub-key; a missing sub-key is less; a key that runs out is less unless
its flag bit 7 is set; each sub-key's order reversed unless its ascending
bit is set).  `CompareKeys` reverses the result for a descending index.

The operations: `Add`/`AddInTransaction` (`_BTEnterKey`: `InsertKey` down to
the leaf, `PutKeyIntoNode` on the way up while nodes split -
`SplitANode` moves keys from the end of the node into a new one until it is
half full and pushes the boundary key up - `CreateNewRoot` when the root
splits; a key already there gets its datum added by `InsertDupData`:
`CheckForDupData`, the field converted to one with duplicates,
`StoreDupData` into the field while it stays under 100 bytes, else into
the chain of dup nodes), `Delete` (`_BTRemoveKey`: `DeleteKey` - a datum
removed by `DeleteTheKey`; an interior key with no other data is replaced
by the leftmost leaf key to its right (`GetLeafKey`,
`InsertAfterDelete`); an underflowing node is merged or balanced with a
sibling (`BalanceTwoNodes`, `MergeTwoNodes`); an emptied root gives way
to its child), `Find` (0 found, 2 not found - the key after is
answered, 3 nothing follows), `First`/`Last`, `Next` (mode 0 the key's
next datum then the next key, 1 the next key, 2 the next datum only),
`Prior`, `Search` (forwards or backwards from a key calling a stop
function per entry), `Destroy`, `TotalSize`; the cursors' iteration by
`IndexState` (`FindAndGetState`, `MoveAndGetState`, `MoveUsingState`).
Every operation is a node cache transaction: `TNodeCache::Commit` when it
succeeds, `Abort` when it throws.  Three static 100-byte key fields
(`theKeyField`, `savedKey`, `leafKey`) are the ROM's working buffers.
`test_SoupIndex` adds 2000 long keys in a scrambled order and deletes
them again, walks 300 duplicates through the field and dup nodes both
ways, tries each key type and the multi-key, and re-reads an index from
the store.

## Entries (`src/stores/Entries.h`)

A soup entry in memory is a *fault block* (0x002ba74c `MakeFaultBlock`):
a four-slot frame of class `kFaultBlockClass` (0x22) - the entry's
handler (its soup), the `TStoreWrapper*` (0: the store is gone; nil: a
*proxy* entry whose handler answers for it), the store object id and
the entry frame once it has been read.  `ObjectPtr` on a fault block
reads the entry (`FollowFaultBlock`, 0x002ba450: `LoadPermObject`, or
the handler's `EntryAccess` for a proxy; the frames layer calls it
through `gFollowFaultBlockProc`); `WriteFaultBlock` writes the frame
back, `InvalFaultBlock` cuts it off when the store goes,
`UncacheIfFaultBlock` drops the frame from memory.  The persistent
frames of stores and soups are fault blocks with a nil handler.

A soup keeps the entries it has handed out in an *entry cache*
(`MakeEntryCache`: a weak array grown by 8; `FindEntryInCache`,
`PutEntryIntoCache`, `DeleteEntryFromCache`, `InvalidateCacheEntries`);
a store keeps its soup frames the same way and `FindSoupInCache` finds
one by name, case-insensitively.  `GetEntry(soup, id)` answers the cached
block or a new one.

The operations (0x002b3ff4-0x002b5600): `EntryChangeCommon(entry, flags)`
writes the frame back (made internal unless verbatim, its `_modTime` set,
a changed `_uniqueID` reinstated, as the flags say), updates the soup's
indexes from the old keys to the new (`UpdateIndexes`), tells the cursors
(NOT YET) and drops the frame when verbatim - `EntryChange` (7),
`EntryChangeWithModTime` (5), `EntryChangeVerbatim` (4), `EntryFlush`
(15) and `EntryFlushWithModTime` (13) are its flag combinations;
`EntryRemoveFromSoup` takes the keys out of the indexes, deletes the
store object, replaces the block by the plain frame and puts the soup's
`lastUID` back when it was the last; `EntryReplace` swaps in another
frame; `EntryUndoChanges` drops the frame; `EntryCopy` adds a clone to
another soup; `EntryMove` adds the frame to another soup and removes the
old entry (the block then stands for the new entry); `EntryDirty` walks
the frame for `kObjDirty`; `EntrySize`/`EntryTextSize`/`EntryUniqueID`/
`EntryModTime` read the store object header without reading the frame.
Proxy entries forward every operation to their handler as a message
(`ForwardEntryMessage`).  `SetupEphemeralTracker` (large objects created
and not committed) does nothing for a store without the `LOBJ`
capability, which the host store is; `TEphemeralTracker` is NOT YET.

## Stores and soups as frames (`src/stores/Soups.h`)

`MakeStoreObject(store)` (0x00328fdc) makes a store's frame: a
`TStoreWrapper` over it; an empty root object is formatted - the
persistent frame (`storePersistent`: `nameIndex` a new `TSoupIndex` of
string keys, `name` "Untitled", `signature` a random number, `ephemerals`)
stored, the map and symbol tables made, the root data (`'WALY'`, version
4, the three ids) written - otherwise the root data is checked and the
persistent frame loaded.  The frame is a clone of `storePrototype` with
`_proto` the persistent frame's fault block, `store` the wrapper (a raw
pointer in a slot, as the ROM keeps it), `soups` an entry cache and
`version`.  `RegisterTStore` puts it in `gStores` (and the union soups:
NOT YET), `RemoveTStore` takes it out and `KillStoreObject` cuts the
frame and its soups off (`_proto` nil, entries invalidated); `StoreErase`
formats and re-registers.  `InitQueries` makes `gStores`, `gUnionSoups`,
`gPackageStores` (the package store part handler: NOT YET) and, on a
host without the ROM's objects, the prototype frames themselves
(`InitSoupPrototypes`, the same slots and methods as the ROM's
0x005d00e1 `storePrototype`, 0x005d013d `storePersistent`, 0x005d3ffd
`plainSoupPrototype`, 0x005d4159 `plainSoupPersistent`, 0x005d41a1
`indexDescPrototype`: `nsfunctions.py --natives` lists the ROM's).

A soup's persistent frame (`plainSoupPersistent`: `class` `'DiskSoup`,
`lastUID`, `signature`, `indexes`, `flags`, `indexesModTime`,
`infoModTime`, `info`) is a store object whose id the store's name index
maps the soup's name to (the key: the name's UniChars, no terminator).
`StoreCreateSoup` makes one with the `_uniqueID` index description
(`indexDescPrototype`) and one per index spec (`NewIndexDesc`: the spec
total-cloned and checked, its B-tree created - `IndexDescToIndexInfo`
maps `type` to the key type, a `multiSlot` index to the multi-key's
sub-key types and ascending bits, `order` to descending);
`StoreGetSoup` clones `plainSoupPrototype` over it (`_proto` the fault
block, `tStore` the wrapper, `storeObj`, `theName`, `cache` and
`cursors` entry caches, `indexObjects` a C-object binary of the soup's
`TSoupIndex` objects (`CreateSoupIndexObjects`, `GetSoupIndexObject`),
`indexNextUID` from `lastUID`) and caches it in the store's `soups`.
Adding (`PlainSoupAdd` and its Flushed/WithUniqueID forms through
`CommonSoupAddEntry` and `SafeEntryAdd`): the frame made internal, its
`_modTime` and `_uniqueID` set, stored (`StorePermObject`), its keys put
in every index (`AlterIndexes`, the datum the store object id), the
frame replaced by a fault block in the soup's cache; `PlainSoupAddIndex`
/`RemoveIndex` change the persistent frame's `indexes` and re-index
(`IndexEntries` walks the `_uniqueID` index through one re-pointed fault
block); `RemoveAllEntries` deletes every entry's object and destroys the
indexes; `RemoveFromStore` deletes the indexes' info objects, the name
index entry and the persistent frame; `SetName` moves the name index
entry; `GetSize` sums the entries' store objects and the indexes.  Keys:
`KeyToSKey` (string: UniChars without terminator, a rich string's plain
characters; int: a long; real: a double; char: a short; symbol: the
name bytes; an array of types: `MultiKeyToSKey`), `SKeyToKey` back,
`GetEntryKey` = `GetFramePath` (an array of paths gives an array).  The
C++ side sends soups messages (`SoupAdd` = `DoMessage(soup, 'Add, ...)`
and the like), so plain and union soups look alike.  `test_Soups` runs
the store frame, a soup with string and int indexes, entries through
their fault blocks, changes, removal, moves, index changes, a store
re-registered over the same bytes, and the same through NewtonScript.
`SoupNatives.cpp` has the NewtonScript functions (`GetStores`, `Query`,
`IsSoupEntry`, `EntryChange`, `EntryUniqueID`, ... `IsSameEntry`), the
entry aliases (`MakeEntryAlias`: `[nil, soup signature, _uniqueID, soup
name]` of class `'alias`; `ResolveEntryAlias` looks through the stores)
and `RegisterSoupNatives`, which binds every store, soup and entry native
to the ROM's function objects by symbol (both tables of
`ROMNatives.cpp`).

## Cursors and queries (`src/stores/Cursors.h`)

`TUnionSoupIndex` (0x002c2f78-0x002c3bf8) is the index a query walks: the
same-path `TSoupIndex` of each of the union soup's soups (one soup is the
common case), each with its `UnionIndexData` - the index, a state
(invalid / valid / exhausted), the `IndexState` and key field the last
step left, and the node cache's modification count that state was taken
at (`IsValidState` re-finds the position when the index changed since).
`Find`/`First`/`Last` position every soup and take the lowest (highest)
key as current; `Search(forward, key, data, stopFn, refCon, ...)` steps
the current soup's index (`TSoupIndex::Search`, the stop function called
for each key) and `MoveToNextSoup` picks the next soup's key when it is
exhausted; `Next` and `Prior` are one-step searches; `CurrentSoupGone`
re-positions when the current soup is removed from the union.  Every
operation is a node-cache transaction (`Commit` on success).

`TCursor` (0xc0 bytes; 0x002a8ef4-0x002ac890) is a query's position: the
soup (`fSoup`), the cursor frame (`fCursor`, a clone of
`cursorPrototype` with the `TCursor` in a C-object binary in its
`TCursor` slot), the query's parts as flags and Refs (`indexPath`, the
begin/end keys inclusive or exclusive as `SKey`s, `startKey`, `secOrder`,
`indexValidTest` of the key, `validTest` and `endTest` of the entry;
`tagSpec`, `words`/`entireWords` and `text` NOT YET RECONSTRUCTED), the
per-soup info (`CursorSoupInfo`: the soup and its tags bits), the
`TUnionSoupIndex`, and the position - `fKey`, `fEntryData` (the entry's
store object id as the index datum; the ROM keeps a 4-byte id, the host
an `SKey` so the 4-byte big-endian datum writes fit), `fEntry` (the fault
block, nil when *parked* before the first or past the last entry,
`fParkedAtEnd`) and `fEntryRemoved` (`Entry` answers `'deleted`).
`Init(cursor, soup, querySpec)` reads the spec (`BuildSoupsInfo`,
`CreateIndexes`: a soup without the index is `fMissingIndex`), and the
cursor registers in the soup's `cursors` cache so `EachSoupCursorDo`
reaches it.

`Move(count)` (0x002aa164) follows the ROM's assembly: `ExitParking`
when parked (`Find` the begin key, or `First`; backwards `FindPrior` the
end key, or `Last`), else `Next`/`Prior` (one step fewer after a
removal), then `Search` in the direction with `CursorStopFn` counting
the valid entries (`ValidTest`: `KeyBoundsValidTest`, the index valid
test of the key, the valid and end tests of the entry - the entry made
on the way is kept) until the count is reached or the bounds are left;
arrived, the entry's fault block is made (`MakeEntryFaultBlock`), else
the cursor parks at that end.  `GotoKey(key)` finds the key (or the next
one; pinned into the bounds by `PinCurrentKey`), `GotoEntry(entry)`
positions on an entry of one of the query's soups (`GetEntryKey`, then
a `Next` of mode 0 that accepts the datum) and answers whether it is
there; `Reset` goes to the start key (or the first entry), `ResetToEnd`
to the last; `CountEntries` counts from the reset position - from the
start key when there is one, as the ROM does - and puts the position
back (`GetState`/`SetState`, `CursorState`); `Clone` makes a cursor at
the same position; `EntryKey`, `IsParked` (`WhichEnd`: `'begin`/`'end`),
`Status` (`'valid`, `'missingIndex`, `'invalid`), `Soup`, `IndexPath`.
The soup's notifications (`EachSoupCursorDo`, `Soups.h`): `EntryRemoved`
(the position stands, `fEntryRemoved`), `EntryChanged` (the key
re-read; keys or tags changed), `EntryReadded`, `EntrySoupChanged`,
`SoupAdded`/`SoupRemoved`/`SetSoup` of a union soup, `IndexRemoved`
(the query's index: `Invalidate`, the cursor answers nil for good),
`IndexObjectsChanged` and `SoupTagsChanged` (`RebuildInfo`).

`TCollectCursor` (0x002a8f5c, 0x002ac6a0-0x002ace44) collects the matching entries up
front (`Collect`: the union index searched from the start, `[id, soup
index]` pairs in `fEntries`, the cursor left on the first) and walks
the list (`Move`, `DefineCurrentEntry`, `FindEntry`, `GotoEntry`;
`EntryRemoved` takes the pair out).  `CommonSoupQuery` (0x00322d98, the
soups' `Query` method) makes a cursor through `DefineCursor` (an
errored union soup queries its last soup) and resets it; `SoupCollect`
(`collect`) makes a collect cursor, a plain query when memory runs out.
The cursor natives (`CursorMove`, ... 0x002ab76c-0x002abd44) are the
`cursorPrototype` methods (`Next`, `Prev`, `Move`, `Entry`, `GoTo`,
`GotoKey`, `Reset`, `ResetToEnd`, `Clone`, `CountEntries`, `WhichEnd`,
`Soup`, `IndexPath`, `EntryKey`, `Status`); `InitCursorPrototype`
builds the frame on a host without ROM objects and `RegisterCursorNatives`
binds them.  `test_Soups` (`TestCursors`) walks a plain, a bounded, an
exclusive and a start-key query, a collated string query, the tests
through NewtonScript, the cursor following a key change and removals,
an index removed, and a collect cursor.

## Union soups (`src/stores/UnionSoups.cpp`)

A union soup is the soup of a name across the registered stores: a clone
of `unionSoupPrototype` (`class` `'UnionSoup`, `soupList` the stores'
soups of that name in `gStores` order, `theName`, `cursors` an entry
cache of the cursors over it), kept in `gUnionSoups` (an entry cache;
`FindSoupInCache` by name).  `GetUnionSoup(name)` (0x003350a0) answers
the cached one or makes it from the stores that have the soup (nil when
none has), `GetUnionSoupAlways` one with no soups yet; `AddToUnionSoup`
(0x00335404: `StoreCreateSoup`, `RegisterTStore`, a soup renamed) and
`RemoveFromUnionSoup` (0x00335538: `RemoveFromStore`, `RemoveTStore`)
keep `soupList` and tell the cursors (`SoupAdded`, `SoupRemoved`; or
`SetSoup` when the soups' sort tables disagree - `CheckSoupsSortTables`,
the union soup's `errorCode` `kNSErrSortTablesMismatch` - which the host
never has: every sort id is 0).  `StoreCheckUnion` and
`StoreConvertSoupSortTables` (store methods `CheckUnion`,
`ConvertSoupSortTables`) find and repair such soups.  The natives:
`UnionSoupAddIndex`/`RemoveIndex` (every soup's, after
`CheckStoresWriteProtect`), `NaughtyFlush` (`UnionSoupFlush`),
`UnionSoupGetSize`, and the common `GetName`, `Query`, `collect`; the
tag methods (`AddTags`, `GetTags`, `RemoveTags`, `ModifyTag`, native
`UnionSoupHasTags`) wait for the tags indexes.

The ROM's other union soup methods are NewtonScript (`nsfunctions.py
--object unionsoupprototype`, `--disasm unionsoupprototype.Add`) and are
re-expressed as source in `UnionSoups.cpp`, compiled into the host's
prototype (`InitUnionSoupPrototype`): `GetSoupList` (a clone),
`GetMember(store)` (the store's soup of the name, created from its
*soupDef* when missing), `AddToStore(entry, store)`,
`AddToDefaultStore` (`GetDefaultStore`: the store of the user
configuration's `defaultStoreSig`, else the first), `Add` and `flush`
(discontinued: they warn through `BadWickedNaughtyNoot` and use the first
store / `NaughtyFlush`), `HasTags`.  With them the NewtonScript built-ins
they rest on: `UnionSoupRegistry` (a global: `{soupDef, apps}` sorted by
the soupDef's name, `BFetch`/`BInsert`/`BDelete`), `RegUnionSoup(app,
soupDef)` and `UnRegUnionSoup(name, app)`, `GetSoupDef(name)` (the
registry's, else a soup's `soupDef` info), `CreateUSoupMember`,
`CreateSoupFromSoupDef` (the soup created with the soupDef's indexes, its
`soupDef` info set, the `initHook` called - a function, or a message to
the `ownerApp` under the root view), `SupplantSoupDef`,
`GetSoupIndexesFromSoupDef`, `GetUserConfig`, and `XmitSoupChange`
(DEVIATION: the deferred call to `XmitSoupChangeNow` is NOT YET, so
nothing is broadcast).  `test_Soups` (`TestUnionSoups`) runs a union over
two stores with a cursor following the second store's soup as it is
created and the store removed, the methods, the registry and a soupDef
creating the member soup with its hook.

## Tags (`src/stores/Tags.h`)

A soup's *tags index* is an index description of type `'tags` whose
`tags` array holds the tag symbols; an entry's tags are the symbol or
array of symbols in the slot on the index's path, and the index maps the
entry's store object id (a long key) to its `TagsBits` (0x002abd78: an
SKey whose data is a bitmap, bit n for the nth tag of the array, as many
bytes as the highest tag needs).  `EncodeTags` (0x002ac0a4) turns tags
into bits through `FSetContains`; a tag the soup does not know is added
to its array (`PlainSoupAddTags`, at most 624; `AddTag` reuses the nil
slot of a removed tag so the other tags keep their bits) when an entry
arrives with it (`AlterTagsIndex`, 0x00323580, from `AlterIndexes` and
`IndexEntries`) or changes to it (`UpdateTagsIndex`, 0x0031ce04, from
`UpdateIndexes` when `EntryChangeCommon` has the `kEntryChangeUpdateTags`
flag).  A query's `tagSpec` `{equal, all, any, none}` is encoded against
each soup's tags as `[mode, bits binary]` pairs (`EncodeQueryTags`,
0x002ac330; nil when an `equal`/`all` tag is unknown to the soup, so no
entry can match; an error with no mode) in `TCursor::BuildSoupsInfo`
(re-encoded by `SoupTagsChanged`), and `TagsValidTest` (0x002ac49c)
tests an entry's bits from the index in every mode without reading the
entry (`TagsBits::ValidTest`: `equal` the same bits, `all` every query
bit, `any` some, `none` no query bit; an entry absent from the index
passes only a lone `none` or an empty `equal`).  The plain soup's tag
methods: `AddTags`, `RemoveTags` (every entry with any of them changed:
the tags out of the slot's array, the slot removed when none are left;
the soup's slots left nil), `ModifyTag` (renamed in every entry through
a tags query and in the soup's array - the bit stays), `HasTags`,
`GetTags` (the nil slots left out); the union soup's go to every soup
(`GetTags` the set union).  `test_Soups` (`TestTags`) runs the index's
bits, every mode, a cursor following tag changes, the methods and the
index removed.

## Copying entries (`src/stores/CopyEntries.cpp`)

`CopyEntries(toSoup)` and `CopyEntriesWithCallback(toSoup, callback,
interval)` (0x003223e4; never to a union soup) copy a soup's entries
keeping their `_uniqueID`s (so they collide with a target that has
them).  An empty target with indexes on the same paths
(`CompareSoupIndexes`) takes the fast path: under the target wrapper's
`StartCopyMaps_Symbols` every store object is copied as it lies
(`CopyPermObject`, 0x002b99ec: the text object copied and its id patched
into the header, the frame maps and symbols re-hashed into the target
store's tables by `CopyObjectReferences`, 0x002b976c, which walks the
stream and overwrites the three-byte references in place; an object with
large binaries is read and re-stored instead), each old id mapped to the
new (`PSSIDMapping`, sorted for `bsearch`), then every index copied key
by key with the ids translated in one transaction each
(`CopySoupIndexes`, `CopyIndexStopFn`: the id is the datum, or the key of
the tags index) and the `_uniqueID` state carried over.  Otherwise
`SlowCopyEntries` (0x00321b28) reads, stores and indexes each entry and
moves the target's next `_uniqueID` past the largest copied (its
`lastUID` cleared).  The callback (a function of no arguments) is called
when `interval` milliseconds have passed since the last call - per entry
copied, per hundred index keys; 0 never.  `test_Soups`
(`TestCopyEntries`) runs both paths, the ids and text objects checked,
the collision and the union soup refused.

## NSOF, the streamed object format (`src/stores/ObjectStreamer.h`)

`TObjectWriter(obj, pipe, includeProto)` (0x0032b0e0) streams an object
graph to a `CPipe` (`utility/Pipes.h`) and `TObjectReader(pipe)`
(0x0032c1cc) reads one back; packages' frames parts, the connection
protocols and the clipboard use them.  The stream is the version byte 2
then one object, each object a tag and its parts: 0 immediate (an xlong
of the ref: one byte for 0..254, else 0xff and four bytes), 1 character
(a byte), 2 unicodeCharacter (two bytes), 3 binaryObject (xlong length,
the class, the data), 4 array (xlong length, the class, the elements), 5
plainArray (no class), 6 frame (xlong count, the slot symbols, the
values), 7 symbol (xlong length, the name), 8 string (xlong bytes, the
UniChars high byte first), 9 precedent (xlong index), 10 nil, 11
smallRect (four bytes: top, left, bottom, right), 12 largeBinary (NOT
YET RECONSTRUCTED: the reader throws, the writer streams the immediate
0x52 the ROM uses for an unstreamable one).  Every pointer object is a
*precedent* as it is met (a frame and a large binary before their parts,
a binary before its class), so shared and cyclic references stream once;
the writer's precedents are the store object writer's
`TPrecedentsForWriting`, the reader's `TPrecedentsForReading` (a shared
set each, a private one when it is busy).  `Size()` prescans the bytes;
`Write()` streams; a frame's `_proto` slot is left out unless
`includeProto`; `SetAllowFunctions(false)` makes the reader refuse a
function.  `test_ObjectStreamer` checks the bytes of small streams
against the format and round-trips a graph of every kind, shared and
cyclic references, `_proto`, functions and the errors.

## The store companders

`StoreCompander.h`/`StoreCompander.cpp` reconstruct the compression layer
a store keeps its large data behind (`TStoreCompander`, the interface at
ROM 0x0037dbfc).  A soup's or package's data is held as fixed 0x400-byte
*blocks*, one store object per block; a *chunk table* object holds those
block objects' ids (one `StorePSSId` per block), and the compander's small
root object (a `PackageRoot`, whose first word is the chunk-table id)
points at it.  A byte offset maps to its block by `offset >> 10`, and the
compander reads or writes that block's object.  `TSimpleStoreCompander`
keeps the blocks uncompressed; `TLZStoreCompander` compresses each with
the LZ coder (`compression/LZCompression.h`) on `Write` and expands it on
`Read`, an empty block object reading back as zeroes.  Either may own its
own compressor/decompressor or borrow the shared pair
(`GetSharedLZObjects`).  Both are made by name
(`TStoreCompander::New("TLZStoreCompander")`) once `InitializeStoreCompanders`
has registered them (a host subset of the ROM's
`InitializeStoreDecompressors`, 0x001f824c).  `test_StoreCompander` formats
a `THostStore`, gives it a chunk table of empty block objects, and
round-trips blocks of text, runs and noise through each compander,
checking an unwritten block reads as zeroes and a rewrite replaces.

NOT YET: the read-only `TStoreDecompressor`/`TSimpleStoreDecompressor`/
`TLZStoreDecompressor` path and `TStoreCompanderWrapper` that drives it -
they relocate the NewtonScript frames of an expanded package page
(`RelocateFramesInPage`, 0x000d2ca4), a separate unit; the Zippy and reloc
variants, `TXIPStoreCompander` and `TPixelMapCompander`.

## Not yet

Large binaries (`LoadLargeBinary`, `DuplicateLargeBinary`,
`CommitLargeBinary`, `LBData`, `IsLargeBinary`), the word hints
(`TWordHintsHandler`, `GetWordsHints`, `TestObjHints`; a query's `words`
and `text`), `TEphemeralTracker`, a sorting table kept on the store
(`StoreSaveSortTable`/`StoreRemoveSortTable`, so only the registered tables
- `frames/SortTables.h` - can be named by a `sortId`) and `secOrder`,
the XMit methods and `XmitSoupChangeNow` (the soup change broadcasts), the
store prototype's NewtonScript methods (`SetName`, `Erase`, `SetInfo`, ...
wrap the natives with broadcasts), store passwords, `TPSSManager` and the
card store mounting, the package store part handler, `TMuxStore`,
`TFlashStore`.
