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

## Not yet

`TStoreWrapper` (the frames layer's view of a store: its symbol table and
map table, `TStoreHashTable`, the object reader/writer and pipes that
turn frames into store objects, ephemerals), `TSoupIndex` and
`TUnionSoupIndex` (the B-tree indexes with `TNodeCache`), the entry cache
and fault blocks, `TCursor`/`TCollectCursor`, the NewtonScript
store/soup/entry/cursor functions, `TPSSManager` and the card store
mounting, `TMuxStore`, `TFlashStore`.
