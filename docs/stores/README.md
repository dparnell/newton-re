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
| 12 | a large binary | its id and its class (4 bytes each) |

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
refs searched with a PATRICIA trie over the ref bits (`Search`,
`GenerateLinks`: each element tests one bit, kept in the top byte of its
right link; DEVIATION: the root tests a host ref's top bit, not bit 31;
`test_ObjectStreamer`'s `TestPrecedents`).  Both register with the
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
string keys, `name` "Untitled", `signature`, `ephemerals`)
stored, the map and symbol tables made, the root data (`'WALY'`, version
4, the three ids) written - otherwise the root data is checked and the
persistent frame loaded.  The frame is a clone of `storePrototype` with
`_proto` the persistent frame's fault block, `store` the wrapper (a raw
pointer in a slot, as the ROM keeps it), `soups` an entry cache and
`version`.

### The internal store's signature

A store formatted for the first time is signed.  The **internal** store -
the machine's own flash, which `GetInternalStore` 0x00154908 names - is
signed with the machine's serial number, its second word
(`TSerialNumberROM::GetSystemSerialNumber`, `hal/System.h`), and 0 when
that cannot be read; every other store gets a random number
(`GetRandomSignature` 0x00353a44).  That is how the system tells its own
flash from a card's store.

A boot script (the ROM object 0x005692f1) checks it: it reads the serial
number, takes its second word with `ExtractLong(serial, 4)` and compares
that with `GetStores()[0]:GetSignature()`.  A signature of 0 gets "The
internal store's signature is invalid", any other mismatch "The internal
store's signature has been altered", and a serial number that cannot be
read "This unit's serial number cannot be read" - each notified over
whatever is on screen.  On the machine, the fix is the hard reset that
erases and reformats the flash, which is the formatting branch above
doing its work again.

Two things make that come out right on a host and are worth knowing about
because each of them, got wrong, produces exactly that notification: the
port has to say which store is the internal one (`SetInternalStore`,
a DEVIATION - `TPSSManager` is NOT YET, so nothing else knows), and the
serial number's second word has to be a 30-bit NewtonScript integer
written into the binary big-endian, because `ExtractLong` reads
big-endian everywhere and throws on anything that will not fit
(`system/SystemNatives.cpp`, `hal/host/System.cpp`).

`RegisterTStore` puts it in `gStores` (and the union soups:
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

## Searching the text (`src/stores/StoreObject.h`, `Cursors.h`)

A query may ask for entries whose *text* has something in it - which is
what Find does - and that is answered without reading a single entry
back into frames.  Every string an entry holds is kept together in one
object beside it, compressed (`TStoreObjectWriter` collects them as it
writes), so the search walks those objects instead.

`WithPermObjectTextDo` 0x002e0008 is the walk: it reads the entry's
header, and when there is a text object it decompresses it and hands the
whole of it to a callback.  The callback answers true to stop, which is
how a search says it has found what it was looking for.

`TObjTextDecompressor` 0x002dfbe0 is worth a look for its shape.  The
two buffers are the object itself - a thousand bytes of compressed text
at the front, two thousand of decompressed after it - so a text small
enough for both is read into the object and decompressed inside it with
no allocation at all.  Anything larger goes through a `TStoreReadPipe`
into a buffer of its own, and the caller tells the two apart by whether
what comes back is the object's own output.

DEVIATION: the MessagePad keeps the text as it lies, UniChars high byte
first, so on a little-endian host the characters are turned round before
anything reads them - the same thing `TStoreObjectReader` does for a
string it loads.

`TCursor::WordsValidTest` 0x002cea1c and `TextValidTest` 0x002ceb10 are
the two tests a query runs through it.  A *text* query looks for the
string anywhere, cases apart (`FindString` 0x00257a0c); a *words* query
wants every word of it, each at the start of a word of the text
(`FindWord` 0x00257a74), and with `entireWords` at the whole of one -
so "cross" finds "crossing" unless entire words were asked for.  The
words are taken from the end backwards and the walk stops at the first
that is not there.

Before the text is read at all, `TestObjHints` 0x002dc934 tests the
*hint chunks* an entry carries against the query's words: a chunk is two
words of bits and a word passes when every bit of its own hint is in one
of them, which refuses most entries without reading anything.

The hints are written as the entry is (`TStoreObjectWriter::Scan1`, a
string of any class but `'string.nohint`): one chunk per 32 characters of
the entry's text (`TWordHintsHandler::GetNumHintChunks` 0x002ddd74 - one
more than the text's 32s, at most 255, and a single chunk from 8160
characters up), each word of three characters or more
(`FindHintWord` 0x002df358) setting bits in the chunk its characters fall
in.  A word's quadgrams - a space and its first three characters, then
each character with the three before it, every character first made its
upper-case, diacritic-free Mac Roman byte (`CanonicalCharacter`
0x002dd688) - are hashed at their position in the word (`HashQuadgram`
0x002dd2b8: the four bytes rotated right by the position and multiplied
by 0x9E3779B9, the golden ratio's 32 bits), and the hash's top six bits
pick one of the chunk's 64 (`SetHints` 0x002dfc64; the quadgrams ending
at the third and fourth characters are hashed on twice more and the third
once more again, so a word's beginning sets more bits).  The last chunk
takes whatever text is left, all its bits set once the text runs past it
(`NextHintChunk` 0x002ddd14).  The handler that wrote them is in the
header's flags: 1, `TWordHintsHandler`, for everything the ROM writes; 0
is `TOldWordHintsHandler`, which hashed each quadgram once.  A query's
own hints are one chunk per word per handler (`GetWordsHints` 0x002dc754),
made with the cursor and deleted with it.  The chunks are two big-endian
words on every host.  ctest `host.NewtonWordHints` (`demo/hints.ns`)
copies the 753 entries of the WorldData package in the ROM extension -
written, hints and all, by Apple's tools - onto the internal store, and
the host writes the same chunks byte for byte; its words queries then
find what the package's own soups find.

An entry written with no hint chunks at all passes no words query: the
ROM never writes such an entry, and `TestObjHints` refuses one.  The host
wrote exactly that (handler 0, no chunks) until it wrote hints, so
`RegisterTStore` repairs a writable store it mounts (`RepairWordHints`,
DEVIATION: confined to that condition, which the ROM cannot produce) -
each such entry's store object rewritten as it is, its indexes left
alone - and says so once ("rewrote word hints for N entries").  The walk
reads every entry's header on each mount, about 30 ms per thousand
entries.  ctests `host.NewtonWordHintsOld` and `.Repair`
(`demo/hintsrepair.ns`) write a store as the old host did
(`NEWTON_NO_WORD_HINTS`) and find its entries after the repair.

### An older host's byte order

Until 2026-10-01 the host wrote a store's reals, the shapes' halfword
structures and its large binaries of a string class (a NetHopper
document's text, the text engine's `'text`) in its own byte order, where a
MessagePad writes them big-endian (`frames/HostOrder.h`,
docs/frames/README.md's "The binaries the host keeps in its own order").
The sparse store file says which it holds: bit 0 of the header's word at
0x28 (`hal/host/HostFlash.h`), set in every file made since, clear in an
older one (a flat file cannot say and is taken to be a MessagePad's, as
Einstein's are).  `host/HostStores.cpp` repairs an unmarked file's
internal store when it mounts it (`RepairHostByteOrder` in `Soups.cpp`,
DEVIATION): every entry looked through, each such object turned round in
memory and the entry written back with its `_modTime` kept, and the file
marked - "turned the reals and text of N entries to a MessagePad's byte
order", once.  A real or a text is turned only when it reads implausibly
(a real subnormal, NaN, infinite or beyond 10^+/-300 and plausible the other
way round; text with more `0xnn00` characters than `0x00nn`), so an object
a newer host already wrote big-endian is left alone.  A user's store holds
few: the System soup's `soundVolumeDb`/`alarmVolumeDb` (a volume read
wrong the other way is a subnormal: silence) and the text of the
documents NetHopper and Newt's Cape keep.  ctests
`host.NewtonByteOrderOld`, `.Repair` and `.Repair.again`
(`demo/byteorder.ns`, `NEWTON_OLD_BYTE_ORDER` writing as the old host
did).  `tools/stores/flashimage.py info` says which a file is, and
`to-flat` refuses an unrepaired one.

The decompressor a search reads the entries' text through is kept for the
whole walk and let go at its end (`ReleasePermObjectTextCache` 0x002e01c8,
from `Move`, `CountEntries` and `Collect`).

NOT YET: the large binaries of an entry, which the ROM also searches when
the entry's flags say one of them is a string (`LoadLargeBinary`,
`TStoreObjectReader::EachLargeObjectDo`).

`test_Soups`'s `TestTextSearch` puts three entries in a soup and asks
both kinds of query over them.

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
variants, `TXIPStoreCompander`.  (The store decompressors and the
wrapper are done since - `packages/StorePackages.h`.)

`TPixelMapCompander` (`stores/PixelMapCompander.cpp`, 0x0018a95c-
0x0018b22c) is a store bitmap's default compander - `MakeBitmap` with a
store makes a 'pixels large binary with it.  A page is LZ-compressed
after a *row-delta* filter: walking back from the end of the last whole
row, each word is XORed with the word a row above it, so rows much like
the one before come out mostly nought; reading undoes it walking forward.
The row length is the bitmap's: the first write keeps a copy of the
PixelMap at the front of the object in the compander's parameter object -
a 0x2c-byte header, its size and then the Newton's 0x1c-byte PixelMap
(DEVIATION: made from the host's PixelMap, big-endian).  A page of noughts
is an empty object.  The domain manager passes the object's base as a
page's last argument, which is where the PixelMap is read from; ROM BUG:
`FillChunkArray` (an object filled from a pipe) passes nought, so the ROM
reads its "PixelMap" from the vectors page (DEVIATION: the host copies
noughts - no row length, no filter).  Now fixed by default: the PixelMap is
taken from the first bytes written (`NEWTON_ROM_BUGS=1` for the noughts).  ROM quirks kept: `Write` filters
the caller's page in place (the host's domain manager writes each page
from a copy, DEVIATION, since it keeps the whole object mapped where the
ROM writes a page out as it lets it go); a 1-bit map's copy has its
grayTable word overwritten with the row's words; 0xc of the header's
bytes are never set.  `InitGraf` registers it (`InitQDCompression`).
`test_LargeObjects` checks a round trip and that page 0 on the store is
exactly the LZ of the filtered page.

## Large objects (`src/stores/LargeObjects.h`)

What a large binary (a VBO) and a package on a store are made of: data kept
as fixed 0x400-byte blocks behind a store compander, mapped into memory to
be read and written.  On the store a large object is a `LargeObjectRoot`
(0x20 bytes, big-endian words): the chunk array (one block object id per
0x400 bytes), the compander's name (a C string object;
TSimpleStoreCompander by default), its parameters, flags (the kind, 2, in
the low half; 0x10000 when made writable), `'paok'` once complete
(`PackageAllocationOk`), and the size.  `LODefaultCreate` makes one
(`InitializeChunkArray`: an empty block object per 0x400 bytes, all in
separate transactions) and `FillChunkArray` fills it from a pipe through
the compander.

Mapping, unmapping, resizing, flushing, committing and aborting are
requests to the ROM domain manager (`RDMParams`: store, id, address,
package id, size, offset, read-only, dirty; `TROMDomainManager1K::
UserRequest`'s selectors - 1 map, 2 unmap, 9 flush, 10 resize, 11 abort,
12 commit, 13 information, 14 address, 15 the object at an address, 16
end session).  On the MessagePad the domain manager is a kernel monitor
that maps an object into virtual memory and pages its blocks in on a
fault.  DEVIATION: the host's (`stores/host/HostLargeObjects.cpp`, reached
only through `ROMDomainUserRequest`) keeps a mapped object whole in a host
block read through its compander; a resize may move it; every writable
object is taken to be dirty, so a flush or commit writes it all back
(growing the chunk array and updating the root's size), and a commit hands
its store objects to the store's transaction; an abort throws the changes
away and unmaps it, as the ROM's ends the session.

ROM bugs (now fixed by default; `NEWTON_ROM_BUGS=1` for the ROM's
behaviour): `InitializeChunkArray`'s clean-up after a failure aborts
the same wrong entry each time; `LOWrite` leaks the compander's
name when the compander is unknown.  Found on
the way: the companders read a root's chunk-table id and the table's block
ids as native words - now big-endian, as on the MessagePad.  Duplicating (`DuplicatePackageData`: the chunk array and every block
copied, big-endian ids) and the streamed form (`LOWrite`, `LOSizeOfStream`,
`LODefaultBackup`) are done, and a large object is deleted by
`LODefaultDelete`, which is `DeallocatePackage` (`stores/PackageObjects.cpp`:
the root, the chunk table or index and every block, the name and the
parameters - a package's root and a large object's start the same way, and
one function takes both back).  NOT YET: objects made from a
compressed stream (`LODefCreateFromComp`), the backup progress callback
(`TLOCallback`), the XIP requests.  A package kept as a large object -
its root a 0x14-byte `PackageRoot`, its pages read through a
`TStoreDecompressor` - is `docs/packages/README.md`'s "Packages on a
store"; the store side of it is `stores/PackageObjects.cpp`
(`PackageAvailable`/`PackageUnavailable`, `DeletePackage`, `IdToStore`,
`IdToVAddr`, `StoreToId`), and the host's domain manager maps one by
reading every page through its decompressor and keeps the package id it
was installed as (`kRDMSetPackageId`).  `test_LargeObjects`.

## Large binaries (`src/stores/LargeBinaries.h`, `Ephemerals.h`)

A large binary (a *virtual binary object*, VBO) is an indirect binary
whose procs are `gLBProcs` and whose data is an `LBData`: the length, the
large object's id, the entry it belongs to, its class, the address it is
mapped at (0 until someone reads it) and its store (an index into the
unnamed table of store wrappers at 0x0c1010c4).  `IsLargeBinary` is just
that procs test.  The procs map the object on demand (`LBDataPtr`),
resize it (`LBSetLength`, through `ResizeLargeObject`), and copy it
(`LBClone`: a new large object on the same store).  A script makes one
with `store:NewVBO(class, length)` or `NewCompressedVBO(class, length,
companderName, companderData)`.

Until an entry holds it, a new large binary is *ephemeral*: the store
wrapper's `TEphemeralTracker` (0x1c bytes) keeps its id on a list - a
store object of big-endian ids named by the store's `'ephemerals` slot -
and a store mounted with ids still on that list deletes them (a VBO made
and never saved is not left behind).  Writing an entry takes the id off
(`CommitLargeBinary`, from the writer's tag 12); a large binary another
entry or another store owns is duplicated first.  Reading an entry back
wraps the object again (`LoadLargeBinary`), found first in `gLBCache`, a
weak array of the large binaries in memory.  An abort
(`AbortLargeBinaries`, `VBOUndoChanges`) throws the changes away; a store
unmounted leaves its large binaries unmapped with no store
(`LargeBinariesStoreRemoved`).  In NSOF a large binary is streamed whole
(`LOWrite`) and read back into a new large object.

The store wrapper drives the tracker: `LockStore` locks it, `UnlockStore`
flushes, `Abort` aborts, and - fixed on the way, they did not before -
`Dirty` returns at once when already dirty and otherwise locks the store,
`SparklingClean` returns at once when not dirty and otherwise flushes and
unlocks.  Only a store that answers `"LOBJ"` has a tracker; `THostStore`
does (the ROM's `TFlashStore` answers `"LOBJ; rom ; sram; flsh"`).
`InitLargeObjects` (the ROM's, from `InitExternal`) is called from
`InitQueries` on the host (DEVIATION: layering).

ROM bugs (now fixed by default; `NEWTON_ROM_BUGS=1` for the ROM's
behaviour): `GetVBOStoredSize` never checks it was given a large
binary, nor does `VBOUndoChanges` (`FLBRollback`) - there the host answers
nil for anything else rather than read past an ordinary binary
(DEVIATION, and the fix); NSOF's reader leaks the compander name and
parameters it read.  A consumer's ROM bug worth
knowing: `SizeOfLearningData` answers the handle word less than nought
when there is no big-learning database, so `GetLearningData` then asks
for a VBO of nearly the whole address space - only ever asked with one.
`test_LargeBinaries` (in a TAppWorld: the package store part handler
`InitQueries` registers wants a world's port): a VBO made in an entry,
written, read back after the store is unmounted and mounted again,
resized, rolled back, cloned, streamed, an orphan dropped from the
ephemerals on remount, an LZ-compressed one, the entry deleted.  ctest
`host.NewtonVBO` makes one on the booted machine (`MakeBitmap` with a
store, `GetBitmapInfo`).

## Plan: the real stores - the flash, the PSS manager and the cards

The host keeps the internal store in a `THostStore`, an in-memory store
saved to a file of its own (a DEVIATION). The ROM keeps it in the internal
flash, in the flash store's own log-structured format. Memory cards are
not there at all: `TPSSManager` and the card server are NOT YET, so
`GetCardSlotStores` and `UnmountCard` answer "no sockets".

**The goal:**

- The internal store is the ROM's own flash format (`TFlashStore`), kept
  in a host file that stands for the flash chips, and it survives
  restarts.
- A memory card can be inserted and removed: `newton --card file` at
  boot, and a host command to insert or pull one while the OS runs.
  `GetCardSlotStores` and `UnmountCard` then work for real.

### What the ROM has, by layer

The sizes are the ROM's code for each layer, from `symbols.json`; the
host's share is what must be written for the host rather than
transcribed.

| Layer | ROM | Size | What it is |
|---|---|---|---|
| **The flash store** | `TFlashStore` (a `TStore`, capabilities `LOBJ rom sram flsh`), `TFlashBlock`, `TFlashPhysBlock`, `TFlashIterator`, `TFlashStoreLookupCache`, `SObject`, `SDirEnt`, `SFlashLogEntry`, `SFlashBlockLogEntry`, `SReservedBlockLogEntry`, `SCompactState` (DDK `PSS/CompactState.h`: 'bltg'..'zarf') | ~45 KB (129 + 44 + iterator/cache functions) | The on-flash format: logical blocks mapped to physical ones by a log, a directory of objects in each block (buckets of `SDirEnt`), objects (`SObject`) with transaction state bits cleared as a transaction goes (flash can only turn 1s into 0s), compaction into a spare block, erase counts and wear levelling, the transaction record and recovery at mount, reserved blocks (the calibration data) |
| **The mutex wrapper** | `TMuxStore` over `TMuxStoreMonitor` | ~5 KB | Every `TStore` call made through a monitor, so that one store is used by one task at a time |
| **The flash** | the `TFlash` protocol (37 methods: `Read`, `Write`, `Erase`, `SuspendErase`, `Copy`, `IsVirgin`, the geometry: `GetTotalSize`, `GetEraseRegionSize`, `GetGroupSize`, ...) with `TNewInternalFlash` (internal), `TFlashSeries2` and `TFlashAMD` (card chips) | ~18 KB + ~6 KB | A flash device's bytes and blocks. `TNewInternalFlash` keeps a list of `TFlashRange`s (`T8/16/32BitFlashRange`: a bank of chips on a bus of that width), the reserved block ranges, and the logical-to-physical mapping of its banks |
| **The chip driver** | the `TFlashDriver` protocol (12 methods), `T28F016_SA_SVDriver` (the Intel 28F016SA/SV chips) | ~2 KB | Chip commands: write a word, start and poll an erase, lock a block, read status |
| **The PSS manager** | `TPSSManager` (a task: `InitPSSManager`, `MainConstructor`, `RegisterStores`, `CardAvailable`, `CardGone`, `CardIsSame`, `ReinsertCard`, `GetCardSlotStores`, `GetStorePSSInfo`, `GCStores`, the UI engine for "card removed while in use") | ~5 KB | Makes the internal store at boot, and a store for each memory card the card server announces; keeps up to four stores for each socket (0x1fc bytes a socket, 0x50 a store) |
| **The card server** | `TCardServer` (a task: card detection and recognition, power, the card handlers' registry, `DoCardEjection`, the async messages `TCardAsyncMsg` - 0x6f is unmount), `TCardEventHandler`, `TCardSystemEventHandler` | ~12 KB | Watches the sockets and runs card handlers on what is in them |
| **Sockets and cards** | `TCardSocket` (87 functions: interrupts, power, the bus, the pins), `TCardPCMCIA` (the CIS: tuples, `CisTpl_*`, checksums), `TCHMemModem` (the memory and modem card handler: `CheckNSetupMemoryDevice`, `NewFlashDriver`, ...) | ~18 + ~29 + ~30 KB | The PCMCIA hardware, a card's own description of itself, and turning a memory card into a `TFlash` for the PSS manager |

In all, some 170 KB of ROM code. About a third of it is the flash store,
which must be exact because it is the format on the flash. About a third
is sockets, power and interrupts, which the host replaces.

### The host boundary

The boundary sits where the hardware starts. Everything above it is
transcribed; below it, the host stands in.

- **The flash chips' bytes.** The host implements the chips' bytes and
  their rules, not the chips' commands:
  - reads;
  - a write that ANDs into what is there, since flash only clears bits
    (Einstein's `(existing & ~mask) | value`);
  - a block erase to 0xFF, taking no time.

  This lives in `hal/host/HostFlash.h`: a flash bank over a file.
  `TFlashRange` is transcribed down to the point where it hands a word
  or a block to the chips' driver. `T28F016_SA_SVDriver` is not: it *is*
  the chips' command set (page-buffer loads, block erases polled through
  the status registers), so a host `TFlashDriver`
  (`stores/flash/host/HostFlashDriver.cpp`) stands in its place and does
  what the commands come to on the bank. It identifies the chips as the
  ROM's driver identifies an MP2x00's - two 16-bit Intel 28F016SA parts
  (0x89/0x66A0, 2 MB each in 64 KB blocks) on a 32-bit bus - so the
  ranges, the 128 KB erase regions, the reserved region and the
  logical-to-physical layout are exactly the ROM's, and the file is the
  flash as the machine would hold it.
- **The sockets.** `hal/host/HostCardSocket.h` stands for the PCMCIA
  controller:
  - card detect and card lock as the host inserts and removes;
  - write protect;
  - the attribute memory (the CIS) and the common memory, read from and
    written to a card image file;
  - power, interrupts and wait states as things that simply succeed.

  `TCardSocket`'s interface is the DDK's (`PCMCIA/CardSocket.h`), so the
  card server above it is transcribed.
- **The card.** A linear flash card image: the common memory, a
  `TFlashSeries2` or `TFlashAMD` chip set over it (the card's CIS says
  which), and the attribute memory with the CIS.

### Einstein's files

Einstein (`pguyot/Einstein`) emulates the same machine at the hardware
level, and its two files are simple.

- **Einstein's internal flash** is one 8 MB file: two 4 MB banks one after
  the other (physical 0x02000000 and 0x10000000). Bytes are in the
  machine's order, erased is 0xFF, and a write ANDs as above. If the host
  bank has the same two banks at the same file offsets, **the file is
  Einstein's**, and the internal store can be moved between the two.
  - The reserved blocks (block 0's 'DLDS', 'OSCD', the calibration data)
    Einstein seeds itself. The host must seed them the same way, or leave
    them erased and let the ROM's code handle a virgin flash.
  - To check with a real Einstein file once the flash store mounts.
- **Einstein's linear card image** (`TLinearCard`, read from Einstein's
  `Emulator/PCMCIA/TLinearCard.cpp` for the format only) is:
  1. the common memory, "as seen by the Newton" - byte *a* of the file is
     the byte the machine reads at common-memory address *a*;
  2. the CIS in its natural order, one byte per CIS byte (the card holds
     it at the even attribute addresses; Einstein answers attribute byte
     *o* with CIS byte `(o / 2) ^ 1`, which is the byte the ROM's
     `(address ^ 3)` access asks for);
  3. an optional icon (a PNG; Einstein writes none);
  4. the card's name, a UTF-8 C string;
  5. a 52-byte footer, `ImageInfo`: ten big-endian words - the name's
     size and start, the icon's, the CIS's, the data's, the card's type
     (the high nibble of the CISTPL_DEVICE byte) and the format version
     (1) - and the twelve bytes `"TLinearCard\0"`.

  Its flash follows the Intel Series 2 command set (two 28F016SA chips on
  the 16-bit card bus). **The host's card file is this very container**
  (`hal/host/HostCard.h`'s `HostCardCreate` makes one), so a card written
  by one should open in the other.

  `tools/cards/linearcard.py` reads one (`info`: the footer, the name and
  the CIS tuple by tuple) and makes one with any CIS (`make`). Einstein's
  default card (`TLinearCard::kDefaultCISData`) is a 2 MB Intel Series 2
  card of 28F008SA chips: CISTPL_DEVICE `52 06 ff` (flash, 200 ns, 2 MB;
  the `ff` ends the tuple's device list, it is not a CISTPL_END), a
  CISTPL_DEVICE_GEO of 64 KB blocks and partitions of three, CISTPL_JEDEC_C
  `89 a2`, and a CISTPL_VERS_1. A card with that CIS - made by
  `linearcard.py make`, not copied - is recognised, formatted and written
  by the reconstruction (ctest `host.NewtonCardEinstein`,
  `src/host/demo/card-einstein.ns`). Not checked yet: an image Einstein
  itself wrote, and one of ours in Einstein (none is to hand here).

### Card packages

A card can carry a package: Apple's vendor-unique CIS tuple 0x8e
(maker 200, code 0x2000; then the package's type, whether it is in
attribute memory, its address and length, a version, two reserved bytes,
its name, and the strings "Arm610" and "NewtOS", which
`PCMCIA20Parser::CisTpl_Vendor_Unique` requires) names it, and
`TCardServer::LoadCardPackage` loads each one the card's CIS lists when
the card goes in, after checking the first seven bytes say "package"; the
card coming out removes them (`state->fPackages`).

- In **common memory** it is loaded where it lies (source format 3,
  `LoadPackage(Ptr, ...)`; DEVIATION: the ROM finds it through a window
  `gCardPackageVAddr` mapped into the socket's domain).
- In **attribute memory** - which has a card byte only at every other
  address - it is read through a `TCardPipe` (`pcmcia/CardPipe.h`, ROM
  0x4fefc-0x502f8: byte p at `address + 2p + 1` through
  `CardAttrMemReadByte`; a card that faults is a pipe exception: -10061
  permission, -10059 bus error, -10065 write protected) and loaded as a
  stream (source format 2, kRemovableStream, device kind 1, the socket's
  number; `LoadPackage(CPipe*, ...)`). A frames part from a stream is one
  NSOF object (`TFramePartHandler::Expand`), so the package must be made
  for streaming - an ordinary package, its frames part in object layout,
  fails with -48006 (not NSOF version 2), on a MessagePad as here.

A 'form part from a card never installs on the ROM: its `InstallFormPart`
reads `deviceNumber` as a variable (`docs/curiosities.md`, "No application
installs off a card's own package"); an 'auto part does. Booted from
`romsrc/` the bug is fixed by default (`RomBugFixed()`; `NEWTON_ROM_BUGS=1`
for the ROM's behaviour) and the application installs, its base view's
`cardSocket` the socket (ctest `host.NewtonCardFormPackage`,
`src/host/demo/card-form-package.ns`).
`tools/cards/streamedpkg.py` makes a streamed package (an 'auto part by
default, whose InstallScript and removeScript set the globals
`cardPackageInstalled` and `cardPackageRemoved`), and `linearcard.py make
--attr-package PKG --package-name NAME` puts it after the CIS with its
tuple (`info` prints the tuple's fields). The host card keeps 128 KB of
attribute memory (`hal/host/HostCard.h`'s `kHostCardAttrSize` is the
0x40000-byte window), with byte p of a package at attribute address 2K at
CIS-area byte (K + p) ^ 1, as the bus's lanes put it. ctest
`host.NewtonCardPackage` (`src/host/demo/card-package.ns`): loaded, its
InstallScript run, the card out and the removeScript run, the card back
and loaded again.

### ATA cards

The ROM has no store for an ATA (PC Card or CompactFlash fixed-disk)
card and no handler for one: what it has is the means to take on the
driver a card carries for itself. When the CIS says a fixed disk
(CISTPL_FUNCID 4) with an ATA interface (CISTPL_FUNCE 1, 1),
`TCardServer::LoadCardPackage` marks the socket (`kATACard`) and has
`TCardATALoader::LoadATAPackages` (`pcmcia/CardATALoader.h`, ROM
0x4a1f4-0x4ac54) read the card's partition map through a `TATA` - the
ROM's own `TATASimple` (`pcmcia/ATA.h`, 0x26408-0x271a4, registered by
`InitCardServices`) when nothing else is given:

- the card put in its first memory-mapped configuration (else the last
  I/O one with sixteen addresses), its task file found in common memory
  (or I/O space) and drive 0 identified;
- block 0: a PC master boot record sends it to the first partition of type
  0x83 (a bootable one first); there, a driver descriptor block ('ER'),
  and from the next block on Apple partition map entries ('PM');
- an entry is the Newton's when its `pmPad` says `'newt'` and the word
  after has bit 8 set; the `Apple_Newton_Driver` entry's and the
  `Apple_Newton` entry's packages (the word at +0x94 says how many blocks,
  from `pmPyPartStart` - a block number on the disk itself, a quirk when
  the map is inside a PC partition) are read whole into memory and loaded
  from there (`LoadDriverPackage`, source format 3, device kind 1); both
  entries are kept in the `TATAPartitionInfo` the card's handler is given
  (`CardSpecific(kCardSpecificATASetPartitionInfo)`);
- the `Apple_Newton` entry's boot area, if it has one for an "ARM610", is
  read, checked against `pmBootCksum` (`ChecksumOf`, the partition map's
  rotate-and-add) and *called* - the ROM jumps to the address in its first
  word with the `TATABootParamBlock` in r0. DEVIATION: the host cannot,
  and fails such a card as if the code had thrown (kError_Call_Aborted).
  The drive is then put on standby (0x96).

With no handler that recognises the card (the driver package is what
should bring one), the card server takes the packages out again
(`RemoveATAPackages`) and the card is unrecognised - which is as far as the
ROM goes. The rest - a store and a handler - comes from a driver package:
Kallisys's ATA Support 1.0 (`fixtures/packages/drivers/`, Paul Guyot,
2001) installs on the host (ctest `host.NewtonATASupport`: its installer's
native code on `src/armcpu`), and its driver package carries
`TATACardHandler`, `TATAStore` (a `TStore`), its own `TATASimple` and an
ATA card server - ARM protocol parts the host runs on `src/armcpu`: a
card is partitioned, formatted and mounted through the package's own slip
and its store written and read back after a restart (ctests
`host.NewtonATASupport.store`, `.storerestart`; `docs/armcpu/README.md`,
"Stores, store events and the private jump table").

`TATASimple` is programmed I/O a sector at a time, polling the status
register for up to three seconds (`WaitFor`); ROM QUIRK: that first wait
looks at the status before writing a command, and a drive keeps the last
command's ERR bit there, so after one command fails every command fails
the same way until the drive is reset. Its registers are where the
MessagePad's bus puts them (register r at window offset r ^ 3, the data
register's word at +2 read with an unaligned `ldr`); DEVIATION: it reads
and writes them through `hal/CardBus.h`, which on the host routes an ATA
card's register window to `hal/host/HostATA.cpp`, a model of a
CompactFlash card in memory mode over the card image's data section (READ
and WRITE SECTORS and their kin, IDENTIFY, the power, feature and buffer
commands, LBA or cylinder/head/sector; `NEWTON_TRACE_ATA` prints each
command, `NEWTON_TRACE_ATA=2` every register access but the data's). The
model also raises INTRQ as a card does - when a command is done, and when
each sector of data is ready or taken (not after a read's last) - cleared
by reading the status register or writing a command, masked by nIEN; the
card puts it on the socket's Ready/IREQ# pin, so `TCardSocket::IsIRQ`
(ROM 0x00055d90: not ready) answers it and its rise makes the socket's
IREQ interrupt pending. And the card's configuration registers are in
attribute memory at 0x200 (`atacard.py`'s CISTPL_CONFIG): the option
register (bit 7 a soft reset), the status register's Intr bit, the pin
replacement register (always ready) and the socket and copy register - the
ROM's own driver touches none of these, but ATA Support resets the card
through the option register, waits on the pin replacement register's
ready bit and on the card's interrupt. An ATA card image is the same TLinearCard container with type
0xD, its data section the disk: `tools/cards/atacard.py make FILE --size MB
[--driver PKG] [--package PKG] [--mbr]` makes one (CIS and partition map),
`info` describes one. ctests `pcmcia.ATACard` (`test_ATACard`: identify,
read and write by LBA and by CHS, past the end, reset, power mode, the map
plain and in a PC partition, `SameStrings`/`ChecksumOf`, what was written
still there after a reinsertion) and `host.NewtonATACard`
(`src/host/demo/ata-card.ns`: Newt's Cape's ISO-8859-1 encoding as the
card's driver, installed and removed again).

### Order of work

Each step comes with its host tests.

1. **The flash, from the chips up.** DONE (2026-09-30) -
   `stores/flash/Flash.h`, "The internal flash" below; ctest
   `stores.Flash`.
   `hal/host/HostFlash.h` (a bank over a
   file, with AND writes and erases), a host `TFlashDriver`, `TFlashRange`
   and its 8/16/32-bit forms, and `TNewInternalFlash`.
   - Test: `Read`/`Write`/`Erase` through `TFlash` land in the file where
     Einstein's layout puts them.
2. **The flash store's format, read-only.** DONE with step 3 (2026-09-30):
   "The flash store" below; ctest `stores.FlashStore`.
   - `TFlashStore::Init`/`Mount`, the log scans, `TFlashBlock`, lookup,
     `Read`, `GetObjectSize`, `NextObject`.
   - Test: a flash image Einstein wrote (or one the ROM formatted in a
     later step) mounts, and its objects read back.
3. **The flash store, writing.** DONE (see step 2).
   - `Format`, `NewObject`, `Write`, `SetObjectSize`, `DeleteObject`,
     `ReplaceObject`, the lookup cache, compaction, wear levelling.
   - Then transactions: `StartTransaction`, the transaction bits,
     `DoCommit`/`DoAbort`, `SeparatelyAbort`, `LowLevelRecovery` at
     mount.
   - Tests: the same scripts `test_Store` runs over `THostStore`; a
     power cut in the middle of a transaction (the file copied at each
     step), recovered at the next mount.
4. **`TMuxStore`, and the internal store for real.** DONE (2026-09-30):
   `stores/MuxStore.h`, `stores/flash/PSSManager.h`; `newton --store` is
   the flash file, and every ctest that boots on a store runs on it.
   - `TPSSManager`'s internal half: `InitPSSManager`, `MainConstructor`,
     `RegisterStores`.
   - `newton --store` becomes the flash file.
   - Tests: the ctests that boot on a store, with the internal store on
     the flash; a restart keeps the Names, the packages and the store
     packages.
5. **The cards.** Some 250 ROM functions from the socket up to the
   NewtonScript card handler, in six pieces, each with its tests:
   - **5a. The host card and socket.** DONE (2026-09-30; ctest
     `hal.HostCard`). `hal/host/HostCard.h`: a card image
     in Einstein's container, its attribute memory laid out as the bus
     presents it (CIS byte *i* at `(2i) ^ 3`) and its common memory as the
     file holds it; inserted, removed, write-protected. `TCardSocket` (the
     DDK's class; the ROM's drives the Voyager ASIC) gets a host
     implementation over it: the base addresses, the pins (card detect,
     ready, write protect), power and speeds as things that succeed, and
     the card-detect and card-lock interrupts raised when the host inserts
     or pulls a card. `HostCardCreate` makes a blank flash card of a given
     size with a CIS of our own (CISTPL_DEVICE flash 150 ns, JEDEC_C Intel
     0x89 0xA0 - a 28F016SA, Series 2 - and VERS_1 "Newton host" and the
     card's name). The DDK's `PCMCIA/CardSocket.h` is replaced by
     `hal/CardSocket.h` (the DDK's interface, plus the 31 members the ROM's
     card server calls and the port's fields). DEVIATION: the windows are
     host addresses, so `CreateSocketPhys` makes no physical object (the
     host has no MMU to map a card through). Test: the CIS reads back
     through the socket's attribute window.
   - **5b. The CIS.** DONE (2026-09-30; ctest `pcmcia.CardCIS`):
     `pcmcia/CardCISIterator.h`, `PCMCIA20Parser.h`, `CardPCMCIA.cpp`
     (every function of 0x0004b30c-0x0004e40c and 0x0004ecbc-0x0004feb4
     but the card server's own). The DDK's `TCardDevice` is 0x1c bytes
     where the ROM's is 0x20: Apple's compiler gave the three one-bit
     fields a word of their own, so `fDeviceType` is at +0x14, not +0x11 -
     which is why Ghidra's field names for it are three bytes out (its
     `fBusSize` is the device type; `verify-report.txt`'s mismatch). ROM
     quirks kept: CISTPL_ALTSTR, DEVICE_GEO_A and 0x43-0x45 are read as
     attribute-memory device lists; a CISTPL_CFTABLE_ENTRY's I/O ranges
     may overrun the eight kept; DEVICE_GEO does not check its device;
     the checksum sums the bytes where they lie while the tuples are read
     through the bus's byte lanes. `TCardCISIterator` (reading tuples, the long links,
     the multi-function CISs), `TPCMCIA20Parser` (the `CisTpl_*` tuple
     handlers, `ParsePCCardCIS`) and what they fill: `TCardPCMCIA`,
     `TCardDevice`, `TCardFunction`, `TCardConfiguration`,
     `TCardPackage`. Test: mkcard's CIS parsed into one flash device of
     the right size; `GetCardInfo`'s frame for it.
   - **5c. The memory card handler.** DONE (2026-09-30; ctest
     `pcmcia.MemoryCard`): `pcmcia/CHMemModem.h` (the memory side;
     `CheckNSetupModemDevice`, `AllocateSerialDriver` and
     `ParseUnrecognizedCard` NOT YET), `pcmcia/CardHandler.h` (the DDK's
     protocol with its methods virtual, in the ROM's dispatch order),
     `pcmcia/CardPower.cpp` (the sockets' Vcc and Vpp, counted, with the
     countdowns the idle task runs down), `pcmcia/CardMessage.h`,
     `stores/flash/CardFlash.h` (the host's `TFlashSeries2`) and the flash
     store's card branches (`Init` from an `SPSSStoreInfo`, the power
     calls, write protection through the flash or the card handler). A
     card whose CIS gives no CISTPL_DEVICE_GEO gets block size 1 from the
     ROM's `IdentifyCard` (2^(0-1) with the ARM's shift), so a real Series
     2 card must carry one - and `HostCardCreate`'s does.
     Was: `TCHMemModem`'s memory side
     (`RecognizeCard`, `InstallServices`, `CheckNSetupMemoryDevice`,
     `NewFlashDriver`, `GetDeviceInfo`, `FormatCIS`; the modem side NOT
     YET) and a card `TFlash`. `TFlashSeries2` is the chips' command set
     driven through the Voyager's bus modes (16-bit writes by swapping
     the halves of the word, `SetControl`), so, as for the internal flash,
     a host `TFlash` stands in for it under the same name and does what
     the commands come to on the card's bytes - the geometry the ROM's
     `IdentifyCard` works out for a pair of 28F016SA (64 KB blocks,
     interleave 2: 128 KB erase regions). `TFlashStore`'s card branches
     (`Init` with `kFlashStoreIsCard`, `VccOn`/`VppOn` over the card's
     power, `IsWriteProtected`). Test: a card store formatted, written,
     remounted.
   - **5d. The card server.** DONE (2026-09-30; ctest
     `pcmcia.CardServer`): `pcmcia/CardServer.h` - `TCardServer` ('cdsv'),
     the card processor `TCardProcessor` ('cdpr', which reads the CIS and
     offers the card to the handlers so the server's own loop stays free),
     their handlers, `TCardSocketState`, `TCardAsyncMsg`,
     `TNewCardAsyncMsg` (the 'card' system event a new card is announced
     with, carrying the devices its handler installed) and `TCardDomains`
     (DEVIATION: no fault monitor - a host card's memory never faults);
     `InitCardServices`, `GetSocketInfo` and a real `GetCardInfo`. A card
     going in is an interrupt, a poll of the pins 20 ms later
     (`DoPollLockSwitchAndCardDetected`), power and a wait for the card to
     say it is ready (0xcb, every 100 ms), then the processor's
     `DoCardRecognition` (0xfc); a card coming out is 0x33 to the
     application and to whoever holds the card (the PSS manager), whose
     letting go (0x34) has the processor remove the handler's services
     (0xfe). Nothing happens until the application gives the server its
     port (message 100): that is what starts card detection. The host's
     socket had to answer the Voyager's raw pins (`GetVPCPins`: a 5 V
     card's voltage sense pins) and keep the card-detect and lock
     interrupts enabled through `ResetInterrupts` as the ROM's does. NOT
     YET: the alert dialogs (no 'alrt' server). Card packages in
     attribute memory and ATA cards as far as the ROM takes them are DONE
     (2026-10-01): "Card packages" and "ATA cards" below.
   - **5e. The PSS manager and the newt side.** DONE (2026-09-30):
     `stores/flash/PSSManager.h` - the 'pssm world (`TPSSManager`:
     `CardAvailable` on the 'card system event makes a store for each
     storage device - `NewByName("TStore", nil, <device type>)`, a
     `TFlashStore` for 'flsh, in a `TMuxStore` - `CardGone`, `CardIsSame`,
     the slot states and `UIEngine`, which sends the application 'stor to
     mount a slot's stores and 'rstr to unmount them and tells the card
     server (0x34) once they are let go), `InitializeCardStore`,
     `GetCardSlotStores`, `GetStorePSSInfo`; `SPSSStoreInfo` moved to
     `stores/PSSInfo.h` so the store frames' `CardSlot`/`CardType` can
     read it (DEVIATION: they reach the manager through a hook, the flash
     library being above `stores`). The application's half:
     `pcmcia/NewtCardEvents.h` (`TNewtCardEventHandler`, `HandleCardEvents`,
     `HandleNewCard`, the NewtonScript card handler's calls;
     `CheckCardBattery`, `GetCardTypes`, `UnmountCard` real) and
     `newt/StorageCards.h` (`StorageCardInserted`/`MountStore` - format,
     password and conversion prompts through the ROM's own NewtonScript
     `HandleCardEvent`, which puts up the ROM's own dialogs -
     `StorageCardRemoved`/`UnmountStore`, `CheckCardActiveProtocols`,
     `CheckStoreVersion`, `SetStoreVersion`); `TNewtWorld` dispatches
     'card, 'rstr and 'stor. `HostMountStores` starts the card server
     (`InitCardServices`) before the PSS manager, as `TLoader::TheMain`
     does, and the PSS manager runs with or without the internal flash.
     A card pulled while its store is in use puts up the card reinsert
     alert until it is back (`docs/alert/README.md`, ctest
     `host.NewtonCardAlert`); `ReinsertCard`'s own way in - a fault on the
     card's memory - holds nobody on the host (no fault monitor).
   - **5f. The host's hand.** DONE (2026-09-30; ctest `host.NewtonCard`):
     `newton --card file` (a blank 4 MB card made when there is none),
     `HostCreateCard(path, mb)`, `HostInsertCard(socket, path)` and
     `HostRemoveCard(socket)` for scripts (`host/HostCards.h`), and
     `src/host/demo/card.ns`: a blank card put in, the ROM's "This card
     appears to be new" and "This will delete all information" dialogs
     answered Erase, a soup written, the card pulled (its store
     unmounted) and put back, and the entry still there. A card pulled
     out keeps its host memory until the program ends: the store is
     unmounted a moment after, and reads it meanwhile, where a MessagePad
     would fault.
6. **Einstein's files.** PARTLY DONE (2026-09-30): the format matched
   from Einstein's source, a card with Einstein's default CIS made
   (`tools/cards/linearcard.py`) and mounted (ctest
   `host.NewtonCardEinstein`). NOT YET: images Einstein itself wrote, and
   ours opened in Einstein - both wait on an Einstein build or image.

`THostStore` stays for the unit tests that want a store without a flash
under it.

## The internal flash

`stores/flash/` (library `flash`) is the flash the flash store will sit
on: the `TFlash` and `TFlashDriver` protocols, the ranges of chips and
`TNewInternalFlash` - all of it reconstructed but the chips' driver.

**Finding the chips.** `TNewInternalFlash::InternalInit` asks each driver
(the ROM extensions' 'fdrv entries, then the ROM's own) what answers at
the flash bank's write window, 0x34000000 - first a pair of 16-bit chips
on the two halves of the bus, then four 8-bit ones, then each half on its
own - and makes a `TFlashRange` for what it finds: a `T32BitFlashRange`
for a full bus, `T16Bit`/`T8Bit` for a half or a lane. A second bank in
I/O space at 0x10000000 is tried too, unless an 'flsa entry forbids it or
a ROM extension already lives there. Each range gets a read window from
0x30000000 up (cached) and a write window from 0x34000000 up (uncached;
twice or four times as wide for a narrow range, since each word of the
bus then carries two bytes or one), mapped a megabyte at a time - but
only by the instance the boot makes (`kMapWindows`, InitCGlobals through
`InitForReservedBlock`); the one the store uses (`Init`) relies on those
mappings. The range's erase unit is a block of every chip at once: 128 KB
on an MP2x00.

**Regions.** The first erase region is the reserved block (calibration,
patches - `TReservedBlockAccessor`, NOT YET) and is taken out of the flash
addresses. The rest are *regions*, one of which is always the spare. Each
region starts with four bytes of the flash's own: the logical region it
holds (big-endian) and 0x00FF. `Init` reads them into a map
(`GatherBlockMappingInfo`); `Erase` of a logical region marks its region
0x000F, gives the spare its logical number, wipes the old header to
nought, points the map at the spare and starts erasing the old region in
the background - which becomes the new spare. A start after an
interruption finishes what was left (`SetupVirtualMappings`); a flash
that makes no sense is wiped into regions 0..n-2 with the last the spare
(`Clobber`), and `Init` answers `kSError_NeedsFormat` so the store
formats it. So what the store sees is `GetTotalSize` bytes - on an MP2x00
4 MB less the reserved region and the spare, 3.75 MB - whose every
region begins with four bytes it must leave alone.

**The host's chips.** `hal/host/HostFlash.h` keeps the banks in a file -
flat, in Einstein's layout (bank 1 at offset 0, bank 2 after it in an 8
MB file), or a sparse image of only what is not erased ("Bigger flash",
below) - writes through to it at every word and erase, and registers
them as physical memory at 0x02000000 and 0x10000000; `AddNewSecPNJT` on the host records the
section and `VirtualAddressToPointer` (`hal/MMU.h`) finds the bytes behind
a window. The host's driver ANDs each word in and erases a block at once.
`TBankControlRegister` (the bus width) and `InternalVppOn`/`Off` (the
programming voltage) are `hal/Flash.h`, remembered or counted only.

ROM bugs kept: `TBankControlRegister::ConfigureFlashBankDataSize` answers
a positive 0x293b for lanes it cannot make a bus of; `TFlashRange::IsVirgin`
answers "no" without `DoneReadingArray`; `TNewInternalFlash::IsVirgin`
wraps a length shorter than a region's header; `CheckEraseCompletion`
always says "not complete" on an instance with no lock; and an Erase
interrupted between its first two writes is recovered into a state the
next start wipes (`test_Flash` shows it; `docs/curiosities.md`).  All but
the `CheckEraseCompletion` quirk are now fixed by default (the error signed,
`DoneReadingArray` called, the short piece not asked about, the interrupted
Erase finished); `NEWTON_ROM_BUGS=1` gives the ROM's behaviour.

## The flash store

`TFlashStore` (`stores/flash/FlashStore.h`; `FlashStore.cpp`, `FlashStoreObjects.cpp`,
`FlashBlock.cpp`, `FlashIterator.cpp`, `FlashStoreParts.cpp`) is the ROM's
store on flash, reconstructed whole for the internal flash; a card's and a
RAM store's parts (`TStoreDriver`, the compaction in place, the power and
write-protect alerts) are NOT YET, and `Init` refuses those stores.

**The format.** A store is *blocks*, each one erase region of the flash
(on the MP2x00 128 KB, 30 of them, one always kept spare). A block starts
(after four bytes it leaves alone - the internal flash's region header)
with its *root directory*, an object of id 3: `fBlockSize >> 11` buckets
of sixteen four-byte directory entries, hashed by object id, a full
bucket continued in another id-3 object linked from its last two slots.
Objects follow: an eight-byte header - the id (28 bits), a 16-bit size,
the transaction bits, the separate-transaction and execute-in-place flags -
and the data to a word. The last 0x400 bytes of every physical block are
its *log*: entries guarded by their own address XORed with "dyer" and
"foo!" and the word "newt", saying that this physical block holds logical
block so-and-so ('fblk: with its root directory, its erase count, and the
random number and time of the format that together identify the store),
that a block was erased ('eblk), or that one is reserved ('zblk). Mount
reads the latest entry (by log sequence number) of each kind for each block.

**Ids.** An id's top bits are the block it was made in
(`0x1C - CeilLog2(blocks)` of them), so lookup starts there; an object
committed elsewhere leaves a *migrated* directory entry behind. Ids with a
single bit set, or a single bit clear, are never handed out
(`IsValidPSSID`), so no single bit going wrong on the flash turns one id
into another.

**Transactions.** Flash only clears bits, so every state an object goes
through is a code whose bits include the last's (`gObjectStateToTransBits`,
read back by `gObjectTransBitsToState`: new, committed, superceded,
superceder, deleted, and the clones in between). A change to a committed
object makes a copy - the *superceder* - and marks the original
*superceded*; a new object is written in place while its bytes are still
blank. Object 0x17 is the transaction record: all ones while a
transaction is under way, written to noughts at its commit point, so a
start after the power went knows whether to abort (`DoAbort`) or finish
(`DoCommit`) it. Separate transactions (a large object's blocks) keep two
header bits of their own. Every entry point locks the store around
itself, so a change made with no lock of the caller's commits at once;
`TMuxStore` (next) is what serialises the tasks.

**Compaction.** When no block has room, the block with the best
`EraseHeuristic` - what compacting would yield, squared, plus how far its
erase count is below the average, cubed - has its live objects copied into
the spare physical block through the store's last logical block
(`DummyBlock`), and becomes the spare itself (`TFlashPhysBlock::SetSpare`,
which writes the erase entry into the *new* block's log since its own is
about to go).

The words of these structures are big-endian on the flash (DEVIATION: the
host converts at every read and write, `ReadWords`/`WriteWords`; a
`FlashWord` is 32 bits whatever the host's ULong). `test_FlashStore`
formats the internal flash, runs objects through transactions and
separate transactions, churns 1200 4 KB objects to force compactions, and
reads the store back from the file - including a transaction left under
way, undone at the next start.

## The internal store at boot

`InitPSSManager` (`stores/flash/PSSManager.cpp`) is what makes it: the
store implementations registered, a `TFlashStore` on a `TNewInternalFlash`
(`TFlash::New`, `Init` over the heap allocator) wrapped in a `TMuxStore`
(`stores/MuxStore.h`: every call under the wrapper's lock, the changes
made by its `TMuxStoreMonitor` - a monitor, so on its own stack whichever
task asked), formatted when `NeedsFormat` says so, and named "InRAMStore".
`GetInternalStore` answers `gMuxInRAMStore`; `GetStoreClassInfo` looks
through a `TMuxStore` to the store inside (the flash store's capabilities,
"LOBJ rom sram flsh", are what let large objects and store packages on it).

On the host, `HostMountStores` does it when a store file is named and the
OS is running: `HostFlashOpen`, then the windows mapped as the boot's
`InitCGlobals` maps them (`MapInternalFlashWindows`, with a throwaway
instance, `kMapWindows`), then `InitPSSManager`. A new file is a sparse
image of a 64 MB flash - the most the ROM's own code takes unchanged, and
nothing on the disk until it is written - unless `newton --flash-size` and
`--flat-flash` say otherwise (a flat file is 4 MB unless `--flash-size`
says more, since it takes all its size at once) ("Bigger flash", below); a file that is neither a sparse image
nor a flat flash of a size the host makes is refused and the store kept
in memory (a `THostStore`, as without a file; and as `newtonscript`, which
does not run the OS, always does).
NOT YET: the `TPSSManager` world itself, which makes the cards' stores; a
RAM internal store; the reserved block's accessor.

## Bigger flash

An MP2x00 has 4 or 8 MB of internal flash; the host can give it up to
128 MB, with the ROM's own flash code and the ROM's own store format on
it, kept in a *sparse* file that grows with what is written. How far the
ROM's code goes, layer by layer (the addresses are the MP2x00 US ROM's;
the instructions read with `analysis/disasm.py`):

**How the chips are found.** Not by CFI and not from a table of sizes: a
*driver* identifies them. `TNewInternalFlash::SearchForFlashDrivers`
(0x0013b908) takes up to six drivers from the ROM extensions' 'fdrv
entries and falls back on the ROM's own, `T28F016_SA_SVDriver`, whose
`Identify` (0x002044ec) puts the chips in read-identifier mode (0x90 on
the lanes asked about) and knows three parts: Intel 0x89/0x66A0 (the
28F016SA, 2 MB) and Sharp 0xB0/0x6688 (2 MB) or 0x66A8 (1 MB), all in
64 KB blocks. What it answers (`SFlashChipInformation`) is the chip's
size and block size, and everything above works from those: a range's
size is chips x chip size and its erase unit chips x block size
(`TFlashRange::TFlashRange` 0x000c26e0). So the ROM's own driver stops at
2 MB chips - 8 MB in all - but the design is open: a ROM extension with
an 'fdrv driver for bigger chips gets a bigger flash with no other
change. The host's driver (`THostFlashDriver`, already a DEVIATION since
the host's chips are a file) is that driver: it answers the 28F016SA for
a 4 or 8 MB flash, as before, and for a bigger one the same part made as
big as the flash needs (two to a bank, a quarter of the flash each,
still 64 KB blocks so the store's blocks stay the MP2x00's 128 KB).

**Banks and ranges.** Two banks: the flash bank at physical 0x02000000
(32 bits wide, or each 16-bit half a range of its own, or a byte lane) and
the I/O bank at 0x10000000 (32 bits only; ruled out by an 'flsa entry or a
ROM extension living there) - `ConfigureFlashBank` 0x0013cc44,
`ConfigureIOBank` 0x0013caf0. At most three ranges (`AddFlashRange`
0x0013c9ec: `cmp r0,#0x3; mvncs r0,#0x2940` -
kError_Flash_Unsupported_Configuration). Nothing limits a range's size.

**The windows - the ROM's cap: 64 MB.** Each range is read through a
window from 0x30000000 up and written through one from 0x34000000 up
(`InternalInit` 0x0013b484 `mov r0,#0x30000000`, 0x0013b48c `mov
r0,#0x34000000`), each range's windows following the last's. The read
windows of all the ranges together therefore have 64 MB before they run
into the first write window: with more, the second bank's read window
would be mapped over the first bank's write window. The MMU map has
nothing else there (`analysis/mmumap.py`: the I/O identity map ends at
0x30000000, the ROM domain starts at 0x60000000).
**So the largest internal flash the ROM's code handles unchanged is 64 MB:
two banks of two 16 MB chips, the read windows exactly filling
0x30000000-0x34000000** - 511 regions of 128 KB after the reserved one,
510 store blocks, 63.75 MB of store.

**Regions.** `TNewInternalFlash`'s map is a 16-bit word a region
(`Init` 0x0013b5bc allocates `regions * 2 - 2` bytes) and a region's
header names its logical region in 16 bits: 65535 regions, 8 GB of 128 KB
regions. No cap in practice.

**The store - the next cap: 128 MB.** `TFlashStore::Init` (0x000c6bf4)
sizes everything from the flash's total size and region size: its block
arrays, and the ids (28 bits, the top `CeilLog2(blocks)` of them the block
an object was made in). What does not scale is a *migrated* directory
entry - the note a block keeps that one of its objects now lives
elsewhere: 14 bits of object number and 10 of block
(`SDirEnt::SetMigratedObjectInfo` 0x000c4fa0), and
`IsValidMigratedObjectInfo` (0x000c4f7c: `subs r12,r0,#0x3fc0; cmpge
r12,#0x3f; ... cmple r1,#0x3ff`) refuses a block past 0x3ff. So 1024
blocks can be named - 128 MB of 128 KB blocks (1022 store blocks, with the
reserved region and the spare). Past that the store still works (an
object with no entry is found by `TFlashStore::Lookup` 0x000c747c
searching every block, which is also what happens today to objects
numbered past 0x3fff) but slowly. (Reading those instructions showed
the reconstruction had the test as `< 0x3FC0 && < 0x3FF`; it is `<= 0x3FFF
&& <= 0x3FF`, fixed 2026-09-30.) Nothing else in the store depends on the
size: an object is at most 64 KB (a 16-bit size) whatever the flash, each
block's log is its last 0x400 bytes, `GetStoreSizes` (0x000c84a0) works in
longs, and the sizes reach NewtonScript as 30-bit integers, good to
512 MB. The PSS manager takes the size it is given
(`InternalStoreInfo`, `gInternalFlashStoreSize`) and assumes nothing.

**What the host does.** Up to 64 MB it is the ROM's code as it is. For
128 MB the write windows must move out of the read windows' way, and the
host moves them to 0x38000000 - `InternalFlashWriteWindow()` in
`stores/flash/Flash.h`, a DEVIATION the host's driver answers with the
ROM's 0x34000000 for 64 MB or less. It changes no byte on the flash: the
store's format, block size and every structure are the ROM's. Going past
128 MB would mean either blocks the migrated entries cannot name (the
slow path above) or bigger erase regions (chips with 128 KB blocks: 256 KB
store blocks, 256 MB before the cap) - neither is done; the second is a
store unlike any MP2x00's, and the owner's call.

`newton --store file --flash-size N` (N = 4, 8, 16, 32, 64 or 128 MB)
makes a new store file of that size (64 MB when not given, since
2026-10-01; 4 MB before); a file that is there keeps its size.  A first
boot on 64 MB takes about two seconds longer than on 4 MB (510 blocks'
logs written instead of 30) and leaves an 860 KB file; a later boot
about half a second longer.
`test_HostFlash` (ctest `stores.HostFlash`) finds 64 MB with the ROM's
windows and 128 MB with the moved ones, and formats a flash store on
128 MB, writes 12 MB of objects and reads them back after reopening the
file; ctests `host.NewtonBigStore.write`/`host.NewtonBigStore`/
`host.NewtonBigStore.file` boot on a new 128 MB store
(`src/host/demo/bigstore.ns`), write 9.9 MB of soup entries (600 of
16 KB), boot again on the same file, read them all back, and check the
file is about 12 MB (`flashimage.py info --expect-stored`).

**The sparse image.** A 128 MB flash file would be 128 MB on disk from
the start, so a new file is a sparse image (`hal/host/HostFlash.h`): the
flash is cut into 1 KB chunks, and the file holds a 0x40-byte header
('NewtFlsh', version, sizes), a map of one big-endian word per chunk (0:
erased; n: kept in slot n) and the chunks that are not all 0xFF, each
appended the first time a bit in it is cleared. A word written as ones
changes nothing and costs nothing; an erase that leaves a chunk all 0xFF
clears its map word and its slot is reused for the next chunk written.
A new 4 MB image is 17 KB, a new 128 MB one 512 KB (the map); formatted
it holds one chunk per store block (1.5 MB at 128 MB), and then grows
with the data - 9.9 MB of soup entries on 128 MB made an 11.75 MB file.
Crash safety: a chunk's bytes are written (and the C library's buffer
emptied) before the map word naming them, and a freed chunk's word is
cleared before its slot is reused, so a program stopped part-way leaves
every map word naming whole bytes; at open a word naming a slot past the
end of the file reads as erased (and is put right), and of two words
naming one slot - which only an operating-system crash reordering the
writes could leave - the first is kept. A torn write inside a chunk
already there is a torn flash write, which the flash store's
transactions are made for. (No fsync: plain C has none.) The whole
flash is held in memory - 128 MB of RAM for the largest - because
`TFlashRange` reads and writes through pointers into its windows; the
file is read in at open and written through at every change.

Flat files still open (a 4 or 8 MB flat file is Einstein's flash, and
flat files of the bigger sizes are the same layout: bank 1 then bank
2), told from a sparse image by its first eight bytes; `--flat-flash`
makes a new one flat. `tools/stores/flashimage.py` describes an image
(`info`), converts between the two (`to-sparse`, `to-flat` - so an
Einstein flash can be taken to the host's sparse form and back) and makes
an empty sparse one (`make-sparse --size N`); ctest `tools.FlashImage`.

## Not yet

`secOrder`,
the XMit methods and `XmitSoupChangeNow` (the soup change broadcasts), the
store prototype's NewtonScript methods (`SetName`, `Erase`, `SetInfo`, ...
wrap the natives with broadcasts), store passwords, `TPSSManager` and the
card store mounting, the package store part handler, `TMuxStore`,
`TFlashStore`.
