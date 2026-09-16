# Compression

Reverse-engineering notes for the ROM's compression subsystem: the
`TCompressor`/`TDecompressor` (buffer at a time) and
`TCallbackCompressor`/`TCallbackDecompressor` (streamed) protocols and
their implementations. Reconstructed in `src/compression/` with the host
test `src/compression/tests/test_Compression.cpp` (round trips and format
checks through the registry). Facts come from the ROM at the addresses the
code cites; the tables are extracted by `tools/newton-rom/analysis/romtable.py`
(`src/compression/LZTables.cpp` names the command).

## The protocols

No DDK header describes them; the interfaces follow the dispatch tables
(`classinfo.py --name TLZCompressor` etc.) and the glue at 0x0037fdf4:

| Interface | Methods (after ClassInfo/New/Delete) |
|---|---|
| `TCompressor` | `Init(void*)`, `Compress(ULong* outSize, void* dst, ULong dstSize, void* src, ULong srcSize)`, `EstimatedCompressedSize(void* src, ULong srcSize)`; plus the interface's own Handle forms (0x00071aa4) |
| `TDecompressor` | `Init`, `Decompress(outSize, dst, dstSize, src, srcSize)` |
| `TCallbackCompressor` | `Init`, `Reset`, `WriteChunk(data, size)`, `Flush`; the instance's `fWriteProc`/`fRefCon` at +0x10/+0x14 are written by the client after `New` (e.g. `NewPackage` 0x002fbe20) |
| `TCallbackDecompressor` | `Init`, `Reset`, `ReadChunk(into, long* size, Boolean* underflow)` |

Implementations in the ROM (`docs/protocols/classinfos.md`): `TLZCompressor`,
`TLZDecompressor`, `TLZCallbackCompressor` (signature: the store
decompressors that read its output), `TZippyCompressor`,
`TZippyDecompressor`, `TZippyCallbackCompressor`, `TArithmeticCompressor`
/`Decompressor` (callback), `TUnicodeCompressor`/`Decompressor` (callback).
`InitializeCompression` (0x00100ac8, called from `RegisterROMDomainManager`)
registers them all; the store decompressors and package stores are
registered next by `InitializeStoreDecompressors` (not yet reconstructed).

## The LZ format (`LZCompression.h`)

Used for stores and packages. A *chunk* is a 4-byte total length (itself
included) followed by *blocks*, one per 0x400 source bytes:

* stored block: byte 0 = 1, then the bytes (used when coding would not
  shrink the block);
* coded block: bytes 0-3 = `00 01 00 00`, then a bit stream, most
  significant bit first (`Pushpopper`, 0x003131b0).

The stream is a sequence of codewords, each a *copy length* code followed
either by a literal run (when the code is 0 and a run may follow) or by
an offset:

* copy length: a prefix code - `00` 0, `01` 1, `100` 2, `1010`/`1011` 3-4,
  `1100` 5, then the `CL`/`CLB`/`CLBase` bands (5 bits for 6-9, 8 for
  10-17, 10 for 18-33, 11 for 34-49, 12 for 50-81). The decoder reads 8
  bits and looks them up in `LZCopyBits`/`CopyValue`. The value is the copy
  length minus 3 after a partial literal run (1-62 bytes), minus 2
  otherwise - so that 0 stays free for "a literal run follows";
* literal run: `0` for 1 byte, else the `LL`/`LLB`/`LLBase` bands (63 at
  most), then the bytes;
* offset (distance back, 1-based): coded in the current *case*, 10 down to
  1. Case N codes offsets below 0x15 · 2^(10-N) in three bands: `0` + (10-N)
  bits, `10` + (12-N) bits, `11` + k bits where k grows with the position in
  the block (`O1`-`O10`: the width needed for the offsets possible so far).
  The band's top value is an escape: the coder moves to case N-1 for the
  rest of the block and codes the offset there. Copies are at most 64
  bytes; a copy of 3 or more is preferred to literals.

The compressor (0x00100110) builds a suffix-tree-like index per block
(`treesearch1m5`, 0x001d04cc: 0x200 `TTNode`s of 0x14 bytes, one head per
first byte, siblings moved to the front on a hit, insertions stop when the
pool is used up) and emits a codeword per match or per 63 literals.

## Zippy (`ZippyCompression.h`)

A word-oriented cache compressor (the WK kind) for data made of pointers
and small integers, 0x400 bytes a block. An 8-byte header - total length,
then 0x10000 for coded or 0x1000000 for stored - and a bit stream, most
significant bit first, one code per 32-bit source word (a trailing partial
word is dropped): `00` the word is zero; `10 iiii` cache entry i; `01 iiii
b(11)` entry i with bits 13-3 replaced; `11 w(32)` a new word. The cache
holds 16 words with a use counter (`CacheAndCompress`, 0x0028305c: the
first entry matching exactly or partly, in index order, wins; a new word
replaces the least recently used). The last byte is padded with ones, which
read as an impossible new-word code at the end (`ExpandValue`,
0x002835ec). `StuffBits`/`ExpandValue` juggle big-endian byte windows in
the ROM; the reconstruction writes the bit-string operations they amount
to.

## Arithmetic (`ArithmeticCompression.h`)

Witten-Neal-Cleary adaptive arithmetic coding of bytes with 32-bit
low/range arithmetic, as callback compressor/decompressor. Symbols 1-256
are the bytes, 257 the end. There is no division: `NarrowRegion`
(0x00037bf8) scales by a 5-bit quotient of range / (total · 16), and the
decoder's `FindSymbol` (0x000376c0) divides value - low by that quotient
bit by bit while narrowing the symbol. The model (`StartModel`, 0x00036d30)
starts flat with an increment of 2^18 per symbol, keeps the symbols
ordered by frequency (index/char tables swapped on update) and halves
itself when the total exceeds 2^27. Bits leave least-significant first
within each byte; the compressor buffers 128 bytes for its write proc, the
decompressor reads 128 at a time (`ReadByte`, 0x00037878: `NewtonErr
(*)(void* refCon, void* into, long* size, Boolean* underflow)`) and supplies
zeros for four bytes past the end before throwing the end of data. Proc
errors travel as `evt.ex.comp` exceptions with the error as data. A fixed
model (`ArithmeticModel`: the four tables and an adaptive flag) can be
given to `Init` instead of nil.

Quirks kept: `TArithmeticCompressor::Delete` frees nothing (the tables are
`Cleanup`'s); the decompressor's `Delete` frees the tables whenever the
model is adaptive, whoever owns them. Two `DEVIATION`s zero flags the ROM
leaves uninitialised in a fresh instance (`fOwnsTables`; the Unicode
decompressor's `fRunCount`/`fSourceDone`).

## Unicode text (`UnicodeCompression.h`)

`TUnicodeCompressor`/`TUnicodeDecompressor` (callback) shorten UniChar
text: a run of characters from one 256-character block goes out as the
high byte, a count (at most 255) and the low bytes; characters from other
blocks go out as their two bytes. Which blocks are run-coded is
`gUnicodeLookupTable` (0x00371008, a 32-byte bitmap: 0x00, 0x02-0x06,
0x09-0x0e, 0x10). Write-proc errors are ignored by the compressor (the
ROM's), the decompressor keeps the read proc's underflow flag to know the
end.

## Byte order

The ROM writes words big-endian; so do these coders on any host
(`toolbox/ByteOrder.h`), so that data made by a real Newton reads back
byte for byte: the LZ chunk length, the Zippy header and the words Zippy
codes.

## Not yet

`InitializeStoreDecompressors` (the store companders and package stores
over these coders).
