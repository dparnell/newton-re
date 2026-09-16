# The persistent store system (PSS)

How the ROM keeps data that survives a reset, from the bottom up: the
object store protocol `TStore`, its implementations, and (to come) the
frames layer over it - `TStoreWrapper`, the soups, their indexes and the
NewtonScript store/soup/cursor functions.  Reconstructed source is under
`src/stores/`.

How these facts were established: the protocol's method list is the
dispatch table `tools/newton-rom/analysis/classinfo.py build/MP2100D --name
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

## Not yet

Large binaries (`LoadLargeBinary`, `DuplicateLargeBinary`,
`CommitLargeBinary`, `LBData`, `IsLargeBinary`), the word hints
(`TWordHintsHandler`, `GetWordsHints`, `TestObjHints`), `TEphemeralTracker`,
`TUnionSoupIndex` and the union soups (`AddToUnionSoup`, `GetUnionSoup`),
`TSortingTable`/`TSortTables` (the sort ids are all 0), tags indexes
(`AlterTagsIndex`, `EncodeTags`, the tag methods), `TCursor`/
`TCollectCursor` and the queries (`CommonSoupQuery`, `DefineCursor`;
`EachSoupCursorDo` does nothing), entry aliases, `CopyEntries`, the XMit
methods, store passwords, the NewtonScript store/soup/entry/cursor
functions (`GetStores`, `EntryChange`, ...), `TPSSManager` and the card
store mounting, the package store part handler, `TMuxStore`, `TFlashStore`.
