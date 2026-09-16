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

## Not yet

Zippy (`TZippyCompressor`, 0x00282da8), arithmetic (`TArithmeticCompressor`,
0x00036d04) and Unicode (`TUnicodeCompressor`, 0x00254d28) coding, and
`InitializeStoreDecompressors`.
