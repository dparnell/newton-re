# Sound

Reverse-engineering notes for the ROM's sound codecs.  Reconstructed in
`src/sound/` with the host test `src/sound/tests/test_IMACodec.cpp`.

## IMA/DVI ADPCM (`IMACodec.h`)

The Newton records sound as IMA ADPCM: 16-bit linear PCM coded at 4 bits
per sample by an adaptive differential scheme.  The predictor and a step
size are carried from sample to sample; each sample's code is the
quantised difference from the predictor, and the code also adjusts the
step-size index for the next sample.  Both the coder and the decoder run
the same reconstruction, so a stream decodes to exactly the samples the
coder reconstructed.

Two tables drive it (the standard IMA tables, at ROM 0x0034f5fc and
0x0034f5dc; unnamed in the debug symbols, so written into `IMACodec.cpp`
with their address as the citation rather than through `romtable.py`):

* `kIMAStepTable[89]` - the step sizes, 7 growing to 32767 (~1.1x each);
* `kIMAIndexTable[16]` - how a code moves the step index (`-1` for a small
  code, up to `+8` for the largest; the sign bit does not matter).

The compressed form is a run of 0x40-sample **blocks** (`CompressIMA`
rounds the input down to whole blocks).  A block is a 2-byte big-endian
header - the block's initial predictor rounded to its top 9 bits, or'd
with the 7-bit step index - then 0x20 bytes of 0x40 nibbles (the first
sample of a pair in the low nibble, the second in the high).  Interleaved
stereo is coded a block at a time, `srcStride`/`numChannels` selecting the
channel and stepping over the other.

* `CompressIMA(pcm, dst, numSamples, state, srcStride, channel)` codes
  (ROM 0x000e98d0);
* `ExpandIMA(src, dst, state, numBlocks, numChannels, outFormat)` decodes
  (ROM 0x000e9ad8), `outFormat` choosing 8- or 16-bit output and the
  channel stride;
* `CheckState(src, state)` (ROM 0x000e9a7c) re-syncs a decoder to a
  block's header - if the header's step index or its predictor (within
  0x80) disagree with the running state, it resets to the header.
  `ExpandIMA` calls it on the first block; the running predictor then
  carries across the rest, whose headers it skips.

The reconstruction works on the sample values (host-native `short`s); the
ROM, big-endian, keeps those samples big-endian in memory, but the values
are the same, and the compressed stream's bytes are kept exactly as the
ROM lays them, so the two interoperate.

## Not yet

The mu-law codec (`SampleConvertLin16ToMuLaw`/`SampleConvertMuLawToLin16`,
`TMuLawCodec`; the mu-law->linear conversion dithers the low two bits with
`Random()`), `Resample`, and the sound-server streaming layer
(`TSoundCodec`, `TIMACodec`, `TSoundServer`/`TSoundChannel`, `CodecBlock`,
`Produce`/`Consume`).
