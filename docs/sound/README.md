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

## Mu-law (`MuLaw.h`)

Eight-bit sampled sound is companded: a sign bit, a three-bit exponent and a
four-bit mantissa, stored complemented so that silence is `0xFF` and the
byte's ordering follows the sample's.  This is CCITT G.711's mu-law, but not
the usual parameters - the ROM works on the 16-bit sample shifted down by
two (a 14-bit magnitude) and biases it by 33 rather than 132, so its loudest
code stands for about `0x5D7C` rather than full scale, and the steps run 2,
4, 8 ... 256 over the eight exponents.

* `SampleConvertLin16ToMuLaw` (ROM 0x001e9880) and
  `SampleConvertMuLawToLin16` (ROM 0x001e9b90) do one sample;
* `BlockConvertLin16ToMuLaw` (ROM 0x001e98e0) and
  `BlockConvertMuLawToLin16` (ROM 0x001e9c14) do a run, taking a count for
  each side and answering the smaller of the two in both.

Decoding dithers the two bits the coding cannot carry with bits 8 and 9 of
QuickDraw's `Random` (`qd/Ports.h`), which is why the sound library depends
on the graphics one.

**Two bugs, kept.**  The magnitude is never clamped to what eight exponents
can hold (G.711's implementations clip at the top of the last segment), so
the loudest samples overflow the exponent search:

* 32636..32763 and -32760..-32633 leave no bit set in the low eight of the
  shifted magnitude.  The search runs off the end, the exponent stays -1 and
  the mantissa is shifted by it - ARM takes a shift count modulo 256, so a
  count of 255 shifts everything out and the mantissa is zero.  The code
  comes out `0x0F` whichever the sign, and `0x0F` means about -16764: a loud
  positive sample decodes as a loud negative one.
* 32764 and above, -32761 and below, the search finds bit 0 instead, the
  mantissa keeps only its bottom four bits, which are zero, and the sample
  codes as silence (`0xFF` or `0x7F`).

`test_MuLaw` checks the coding by what it promises - every code round-trips
through decode and encode, the curve is monotonic with doubling steps, a
ramp comes back within the step of the code it lands in - and states both
bugs as expectations.

## Not yet

`TMuLawCodec`, `Resample`/`ResampleFiltered`, and the sound-server streaming
layer (`TSoundCodec`, `TIMACodec`, `TSoundServer`/`TSoundChannel`,
`CodecBlock`, `Produce`/`Consume`).
