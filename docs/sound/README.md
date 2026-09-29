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

## The sample converters (`SampleConvert.h`)

Two 8-bit codings that 16-bit linear sound is turned into and back from, a
sample or a block at a time.  The sound DMA channel picks a pair by the
formats at the two ends of a buffer, and the filtered resampler calls them
for every sample it reads and writes.

### "Standard" 8-bit

Offset binary: the sample's top eight bits with 0x80 added, so silence is
0x80.  `SampleConvertLin16ToStd8` (ROM 0x001e980c) adds 255 to a negative
sample before the shift, so the division rounds toward zero;
`SampleConvertStd8ToLin16` (ROM 0x001ea21c) dithers the eight bits the
coding dropped with five bits of QuickDraw's `Random`.  The block forms are
ROM 0x001e9828 and 0x001ea260.

The dither is OR'd in whatever the sign, so it always moves the value up,
while the coder rounds toward zero: a negative code decoded and coded again
can come back one step higher.  That is the ROM's arithmetic, kept.

### Mu-law

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

## Sample-rate conversion (`Resample.h`)

### The plain converter

`Resample` (ROM 0x001e9978) is what the sound DMA channel puts a buffer
through when the sound's rate is not the hardware's
(`TDMAChannel::SetupNode` fills a `SampleSpec` and calls it).  It is
nearest-neighbour: an accumulator is carried from sample to sample, the
input rate paying for the output rate, so input samples are dropped when the
output rate is the lower and repeated when it is the higher.  Both rates are
kept doubled, so that the half-step the decimating branch starts from is
exact.  Nothing is interpolated and nothing is filtered.

`SampleSpec` (the field names are ours - it is not in the DDK, and its only
caller is the unreconstructed sound channel) is a destination pointer, rate
and sample size, the same three for the source, and a sample converter:
`fConvert` is one of `MuLaw.h`'s converters, or nil to copy the sample's
bytes.  The counts are in and out: each comes back as how far its side got,
so a caller can carry on where the smaller of the two stopped.

### The filtered converter

`ResampleFiltered` (ROM 0x001e9eec, and ROM 0x001e9de8 for the form that
takes everything from the state) is the good one.  `kResampleFilter`
(`ResampleTables.cpp`, generated from ROM 0x0036dbe8 by `romtable.py`) is a
windowed sinc of 261 points in 16.16 Fixed, its peak 1.0 in the middle and
thirteen points to a zero crossing, so it spans ten input samples either
side.  An output walks the table from 0 to its end in steps of thirteen,
reading it at a fractional position and interpolating between neighbouring
points, and multiplies each step by the input sample that far back.

Going down, the step is scaled by the rate ratio, which stretches the filter
and drops its cut-off to the output's Nyquist limit; the tap count is then
20 / the ratio rather than 20, and the sum is scaled back by the ratio.
Because thirteen points is exactly one input sample, at the same rate in and
out every tap but the middle one lands on a zero of the sinc, so the
conversion is an exact identity delayed by ten samples - which is what
`test_ResampleFiltered` checks it against.

The state (`ResampleState`, field names ours) carries the rate ratio, the
tap count, the phase and a 160-entry history: the samples before the
buffer's start come from the tail the last call left behind, so a stream
converted in pieces comes out the same as one converted whole.
`InitResampleState` (ROM 0x001e9cd8) works the ratio, the tap count, the
sample strides and the converters out of the rates, sample sizes and formats
already in the state; `GetSample` and `PutSample` (ROM 0x001e9e30,
0x001e9e8c) are the one-sample reads and writes that go through them.

## The codec protocol (`SoundCodec.h`)

`TSoundCodec` is the protocol a sound channel turns coded sound into samples
through, and samples back into coded sound.  A codec holds one buffer at a
time: `Reset` hands it a `CodecBlock` - where the buffer is, how big, how
its samples are coded and at what rate - and `Produce` is then asked, over
and over, for the next stretch as 16-bit linear.  Each call fills as much of
the caller's buffer as it can, answers how many bytes and samples that was,
and fills the block in with what the samples have become (16-bit linear, at
the recorded rate).  `Consume` goes the other way, coding the caller's
samples into the codec's own buffer.  `BufferCompleted` says when the buffer
is used up; `Init`, `Start` and `Stop` are the hooks a codec with state of
its own needs.

`CodecBlock`'s field names are ours, read off `ConvertCodecBlock` (ROM
0x001e8004 and 0x001e8040), which copies a `SoundBlock` into one and back;
`fError` is zeroed when the block is made, and converting one back into a
`SoundBlock` is refused while it is negative.

The `SafeCodec*` calls (ROM 0x000d3558, 0x001e8080-0x001e8290) are the same
calls with an exception handler round them, which is how the sound channel
makes them: a Throw out of a codec becomes `ERRBASE_SOUND` rather than
unwinding into the channel.

Two implementations are reconstructed.  `TMuLawCodec` (ROM
0x001249c8-0x00124ce0) keeps nothing but where it has got to in the buffer,
so `Init`, `Start` and `Stop` have nothing to do, and it carries its own
copies of the two conversions - the same arithmetic as `SampleConvert.h`'s,
including the overflow bug, but without the dither.  `TIMACodec` (ROM
0x000e9898-0x000e9efc) is IMA/DVI ADPCM over `IMACodec.h`: a coded block of
`kIMABlockBytes` unpacks to `kIMABlockSize` samples, so the two sides'
counts are far apart, and the block's sample size decides whether the linear
side is 8- or 16-bit (`Consume` only ever takes 16-bit).  Its `Reset` starts
the predictor from silence, and the state carries across calls so a buffer
can be produced a piece at a time.

`InitializeSound` (ROM 0x001eae0c) registers them; `test_SoundCodec` drives
both directly and through instances made by name from the registry, which is
why it boots the OS.

## The volume (`SoundSettings.h`, `SoundChannel.h`)

The Newton has five volume settings, 0 to 4, and the sound server works in
decibels - 16.16 fixed point, with `0x80000000` standing for silence.
`VolumeToDecibels` 0x001e85ac is the table between them, read out of the
ROM's own words: an eighth of full amplitude for 1 (-18.0618 dB), a half
for 2 (-6.0206 dB), a square root of a half for 3 (-3.0103 dB), full for 4
and silence for 0; anything below 0 is silence and anything above 4 is
full.  `DecibelsToVolume` 0x001e8f54 is the way back, comparing against
settings 2's and 3's levels built up out of immediate constants, so a level
exactly at a setting's own answers that setting.

The NewtonScript functions are thin wrappers over those and the global
sound channel, scaling by 65536 to hand decibels to and from a script as
reals: `VolumeToDecibels` 0x001e8614 and `DecibelsToVolume` 0x001e9434,
`GetVolume` 0x001e95f8 and `SetVolume` 0x001e9618 (a setting, nil meaning
silence), `GetSystemVolume` 0x001e9668 and `SetSystemVolume` 0x001e96a0
(decibels; it answers the decibels the channel settled on).

`TUSoundChannel` (`SoundChannel.h`, 0x3c bytes over `TAEventHandler`) is
the client side of the sound server: a task opens a channel on the server's
port (`gSndPort`) and then talks to it with immediate 'newt/'usnd events,
`SendImmediate` 0x002591c4 carrying `{command, channel, value}` and the
answer coming back in the reply's last word.  The channel keeps the volume
(+0x2c, `0x7fffffff` until it is set), the input gain (+0x30, 0x80) and the
output device (+0x38) itself as well as telling the server, so it can
answer them without a round trip: `SetVolume` 0x00258e34 stores the
decibels whatever happens, and `GetVolume` 0x00258edc only asks the server
when `kGestalt_Ext_VolumeInfo` said it would answer.  `GlobalSoundChannel`
0x001e94cc is the one the script functions go through, made on demand.

Only that much is reconstructed, which is what the ROM's NewtonScript boot
needs: its last act is to read the user configuration's `soundVolumeDb` and
call `SetSystemVolume`.  **DEVIATION:** the ROM makes a
`TFrameSoundChannel` and opens it for output, throwing `evt.ex.fr` if that
fails - and with no sound server it does fail, `TUSoundChannel::Open`
answering `ERRBASE_SOUND` when `gSndPort` is 0.  The host makes a plain,
unopened channel instead: it keeps the volume it is told and answers it,
and plays nothing.  `SendImmediate` answers `kError_Bad_ObjectId` rather
than sending to port 0, because it is reached from the NewtonScript boot
before there is an OS to ask, and every caller falls back on the channel's
own value.  `test_SoundVolume` pins the table, the channel's state and the
script functions.

`ConvertToSoundFrame` 0x000d3188 (the native at 0x001e870c) is what the
sound functions put their argument through: a string is speech, so it
becomes a codec frame for `TMacintalkCodec` with the text as its samples;
a binary is coded sound, and its class names the codec that can read it;
anything else - a sound frame already - is answered as it stands.  Either
frame gets the ROM's own numbers: a buffer size of 5000, four buffers,
compression type 6, data type 16 and a sampling rate of 21600.  The ROM
strips the ink out of the string first (`FStripInk`, NOT YET; a string of
this reconstruction carries none).  The boot's init scripts call it 312
times, once for each sound they register.

The play functions are `PlaySoundIrregardless` 0x001e8660 (whatever is
playing stopped, this scheduled in its place and started without waiting),
`PlaySoundSync` 0x001e88e4 (the same, but the start waits for the end and
it answers true), `PlaySound` 0x001e88b4 (the same as Irregardless when
`SoundPlayEnabled` 0x001e96f8 says the preferences allow it - the two
click sounds the pen makes go by `penSoundEffects`, everything else by
`actionSoundEffects`) and `PlaySoundEffect` 0x001e8714 (the sound made
into a frame, cloned with a `volume` slot when one is given, and played
when the preference for its kind - 'pen, 'alarm or 'action - allows it; a
kind that is none of those plays regardless).

**DEVIATION:** all four go through `TUSoundChannel::Schedule`, `Start` and
`Stop`, and each of those answers `kSoundErrNotOpen` in the ROM when the
channel was never opened - which, with no sound server, the host's always
is.  The host's channel plays nothing, so scheduling, starting and
stopping it are no work and succeed; otherwise every sound the ROM's
scripts play would throw, and the boot plays 375 of them.  NOT YET
RECONSTRUCTED behind them: `TFrameSoundChannel::Convert` (the frame turned
into a `SoundBlock`, a codec opened for it) and the scheduled nodes.

## The sound server (`SoundServer.h`, `SoundDriver.h`)

The server is the `'sndm` app world `InitializeSound` starts (ROM
0x001e89f4).  Sized with `analysis/callgraph.py build/MP2x00US` from the
sound natives (`FSoundOpen` and its neighbours, 0x001e67e8-0x001e70b4) and
`InitializeSound`: about 105 functions and 13KB of code below them, in
four layers - the NewtonScript natives and `TFrameSoundChannel`, the
client `TUSoundChannel`, the server and its channels, and the driver.
The order chosen puts what is heard first: the server and output first,
then the client and the frame channel (a ROM sound such as the click
played by `PlaySoundSync`), then the natives - and only then input, the
compressor channels and the codec channel's task, which the Sound Recorder
needs.

**The seam** is `PSoundDriver` (`SoundDriver.h`), the ROM's own protocol
between the server and the hardware, in its dispatch order: the server
hands it two DMA buffers of 0xea0 bytes, says which to play next and how
much of it, and the driver's interrupt at the end of each runs the
server's callback through `OutputIntHandlerDispatcher`.  The ROM registers
`PCirrusSoundDriver`; a host registers its own through
`gHostRegisterSoundDriver`, and the server asks for `PMainSoundDriver`
first, as the ROM does.  The host's is `hal/host/HostSoundDriver.h`:
output only, 16-bit at 21600 a second (the MP2x00's top rate, so sounds
are resampled as they were on the machine), each buffer's end a host
interrupt source (`docs/host-runtime.md`) due when it would have finished
playing, which on the controllable clock makes playback deterministic.  A
backend is handed each buffer as it starts; the null one keeps the
samples for tests (`HostSoundCaptured`).  With no driver registered
`InitializeSound` starts no server and `gSndPort` stays 0, as before.

**The server** (`TSoundServer`, 0x104 bytes) answers the client's
commands (`TSoundServerHandler::AEHandlerProc`: open, close, start, start
and wait, pause, stop, schedule, cancel, volume) and the interrupts' own
(command 4: a buffer played).  `FillDMABuffer` is the whole of playback:
the first active output channel produces into the buffer, silence after
it, every other active channel is produced into the mixing buffer and
added in (`MixLin16`, clamped), and the loudest channel's volume goes to
the hardware; the buffer's size is only what was filled, so the last one
of a sound is short.  A `TDMAChannel` produces a node's samples copied
(the hardware's own format and rate), converted (`SampleConvert.h`) or
resampled - by the plain converter or, while `gMaxFilterNodes` allows (six
on a StrongARM), the filtered one.

Note what a schedule's answer means: a node is freed - and its request and
a waiting start answered - when its last samples have gone *into a DMA
buffer*, not when they have been heard; up to two buffers are still to
play (`test_SoundServer` waits for the output to stop).

## The client: channels and sound frames (`SoundChannel.h`, `FrameSoundChannel.h`)

A `TUSoundChannel` is a pair of the server's channels - an output channel
and a decompressor feeding it (`Open` asks for both) - and an event
handler: a block is scheduled as a `SoundNode`, the request, its reply and
an asynchronous message whose collector is the task's own app world, and
when the server answers (the block has gone to the hardware) the answer
comes back through the world's event loop to `AECompletionProc`, which
hands it to the block's `TUSoundCallback`.  `Start(0)` is the server's
command 10, answered only when everything scheduled has been played -
which is what makes `PlaySoundSync` synchronous; `Start(1)` is command 9
and marks the channel running, and only a running channel can be stopped.

`TFrameSoundChannel::Convert` is where a sound frame becomes a block: the
samples binary locked, `compressionType` (0, 1 or 6), `dataType` (8/1 or
16/2 bits), the count, `samplingRate` (an integer, a real, or a binary
holding a 16.16 number; nothing at all means 22026.43), `start`, `count`,
`loops` and `volume` (a setting, or decibels as a real); anything else
throws `evt.ex.fr` -30009.  The block's refCon holds the frame, and
`TFrameSoundCallback` lets it go, unlocks the samples and sends the frame
its `callback` method with the state and the error.  `GlobalSoundChannel`
is the one `PlaySound`, `PlaySoundSync`, `PlaySoundEffect` and the clicker
use, opened on `userConfiguration.outputDevice`; the twelve
`protoSoundChannel` natives (`FSoundOpen` .. `FGetChannelInputGain`) keep
a script's own channel in its `_channel` slot.  `NEWTON_TRACE_SOUND=1`
says why a frame could not be played.

`test_PlaySound` boots the Newt world with the null driver and plays the
ROM's click (magic pointer 51: 267 samples, 8-bit, 22026.43 a second):
260 samples reach the driver, correlated 0.965 with the click's own
(the filtered resampler at work); `src/host/demo/sound.ns` (ctest
`host.NewtonSound`) plays it with `PlaySoundSync` and through a
`protoSoundChannel` (@431) whose callback is answered 0, 0.  The windowed
`newton` plays through the loudspeaker (`host/win32/HostAudio.cpp`,
waveOut, built without the Newton include paths); headless it keeps the
samples and says how many there were.

The host-only departure: with no sound server (no driver registered)
the channel plays nothing and answers noErr, as the host always did.

## Coded sound: the codec channel (`TCodecChannel`)

A frame whose `sndFrameType` is `'codec` names a `TSoundCodec` by
`codecName`; its block goes to the channel's decompressor, and starting
it starts the decompressor's own task (`'codc`, `MainEventLoop`).
`DecompressLoop` runs the codec a buffer at a time - the frame's
`bufferSize` bytes, with up to `bufferCount` (held to 2..8) of them out at
once - and schedules each buffer on the output channel *through
`gSndPort`, as any client would* (`ScheduleDMA`, then `SendStart` once,
command 9); each buffer's reply comes back to the codec channel's own
port (`WaitForNextBuffer`), and a node is answered only when its last
buffer is back (`FreeNode` parks it on `fCodecNodes`, `ReleaseNode`
answers it).  A `Stop` sets the abort flag, stops the output channel -
which hands back the buffers and so wakes the task - and waits for the
task to let go of the channel's lock.

Worth knowing: the codec is done with a node when it produces *less than
a whole buffer*, so a frame whose `bufferSize` is not a whole number of
the codec's units (IMA: 128 bytes of output per 34-byte block) ends after
its first buffer - the ROM's behaviour, kept.  A codec that does not
exist (the ROM has no `TMacintalkCodec`, the speech a string becomes; the
host lacks `TGSMCodec`) leaves the block without one - `OpenCodec`'s error
is not looked at - and the coded bytes are played as samples, as the
ROM's would be; `NEWTON_TRACE_SOUND=1` says so.

`test_PlaySound` plays an IMA frame (a second of a rising tone, coded by
`CompressIMA`) and the driver is handed exactly what `ExpandIMA` makes of
the same bytes.

## The tone synthesiser (`TDTMFCodec`, `DTMFCodec.cpp`)

The touch tones are a codec that decodes nothing: its "coded" data is a
*score* - a version (1), an algorithm, a repeat count and up to twelve
tones, each a 16.16 frequency, an envelope of six stretches in
milliseconds (silent, attack, decay, sustain, release, tail) with a peak
and a sustain level - and `Produce` synthesises it by FM: algorithm 0
plays each tone on its own, 1 in pairs (a modulator on a carrier), 2 in
threes (two modulators on one carrier), 3 in chains of three and 4 in
chains of four.  The sine is `quarterSineWaveTable` (721 2.30 words to
the quarter turn, an eighth of a degree apart - `DTMFTables.cpp`,
generated by `romtable.py`), read through `SinTable` or, on a StrongARM,
`SinTableInterp`, which interpolates between neighbours with
`FractMultiply`; the ROM reads it through the uncached alias 0x03870744.
The state is kept as the ROM's words at their offsets because the
chained algorithms index past the twelve-tone arrays (and the slopes past
theirs on the stack) when the tones do not fill the last chain, and the
reconstruction reads what the ROM would.  `test_PlaySound` plays the "1"
key (697 and 1209 Hz): the two frequencies carry the power, and the tone
ends when its 100 ms envelope does.

## Recording: the input side

The mirror of playback: `OpenInputChannel` makes a DMA channel for input
and `OpenCompressorChannel` a codec channel fed by it; starting either
starts the input (`StartInput`: both buffers given to the driver);
`SoundInputIH` (command 5) says a buffer is full and `EmptyDMABuffer` hands
it to every running input channel (`TDMAChannel::Consume` into the node's
samples), throwing one away when the interrupts fell behind.  A frame
recorded through a codec goes the compressor's way: `CompressLoop` has
the input channel fill the codec channel's buffers and runs each through
the codec into the node (`TCodecChannel::EmptyDMABuffer`).  The host
driver's microphone is its backend's `record` - the null backend's is a
test signal (`HostSoundSetSource`), each buffer full at the time the
hardware would have filled it.  A script records with a
`protoSoundChannel` whose `direction` is `'record`.  `test_PlaySound`
records 9600 samples and plays them back unchanged, and records through
`TIMACodec` into exactly the bytes `CompressIMA` makes.

`TSoundPowerHandler` stops everything and powers the hardware down on the
machine's power-off event.

## The byte order of samples (`SampleWords.h`)

A sound's 16-bit samples are persistent - a recording is kept in a soup
entry, beamed and backed up, and a package's sound is in its bytes - so,
by the project's rule for persistent formats, they are big-endian in
memory on every host, as the ROM keeps them: in a frame's samples binary,
in a codec's buffers and in the server's DMA buffers.  Everything that
works on a sample as a number goes through `GetSampleWord`/`PutSampleWord`
(the converters, both resamplers - `GetSample`/`PutSample` - `MixLin16`,
the IMA and mu-law codecs, the DTMF synthesiser), which on the ARM are
its plain halfword loads and stores.  The one place they meet the host's
byte order is the host sound driver, which swaps a buffer as it hands it
to the loudspeaker or takes it from the microphone.  `test_PlaySound`
keeps a recording in a soup entry, finds the entry's samples big-endian,
and plays it back unchanged; the unit tests put their signals into
memory order first (`tests/SampleOrder.h`).

## The host's microphone and the Sound Recorder

`host/win32/HostAudio.cpp` has the microphone beside the loudspeaker: the
waveIn device captures into a ring of buffers all the time it is open, and
`HostMicrophoneRecord` - the driver's `record` backend in the windowed
`newton` - hands over the oldest samples that have arrived (silence for
what has not, so it never waits).  Headless, `newton --microphone-tone HZ`
gives the null backend's microphone a sine instead, and newton says at the
end how much of what was played was that tone.

`src/host/demo/recorder.ns` (ctest `host.NewtonRecorder`) drives the
Sound Recorder through its own buttons - Rec, two seconds, Stop, Play -
and checks its status line at each step and that the playback is the
tone (98% of it; GSM is lossy, and the pass rule asks for 80%).  The
Sound Recorder records through `TGSMCodec`.

## GSM 06.10 (`GSM.h`, `GSM.cpp`, `GSMCodec.cpp`)

The Sound Recorder's codec is GSM full rate: 160 samples (20 ms at 8000
a second) become a 33-byte frame.  The ROM carries the whole of the Toast
library (Jutta Degener and Carsten Bormann's libgsm 1.0) compiled in, at
0x002a85f8-0x00347000, and `GSM.cpp` transcribes it function by function
from the ROM: the saturating word arithmetic (`gsm_add` .. `gsm_norm`,
whose bit table is in the initialised data), preprocessing (offset
compensation and pre-emphasis), LPC analysis (the autocorrelation,
Schur's recursion to eight reflection coefficients, their log-area ratios
quantised to 6,6,5,5,4,4,3,3 bits), the short-term analysis and synthesis
lattice filters over coefficients interpolated between frames, the
long-term predictor (a lag of 40..120 and a gain of four), regular pulse
excitation (a weighting filter, one of four grids of thirteen pulses,
APCM-quantised to three bits under a six-bit block maximum), the
post-processing (de-emphasis, and samples truncated to 13 bits), and the
frame packing with its 0xD magic nibble.  The library's static helpers
have no names in the ROM; they are cited by address with the library's
names for them.  The tables (`gsm_A` .. `gsm_FAC`, the bit table) come
from the ROM's copy of the initialised data (`GSMTables.cpp`,
`romtable.py`).  One difference from the library as published is kept:
an all-silent subframe is scaled by 6 rather than 0 in the lag search,
which changes nothing.  Left shifts are written as the ARM does them
(`SHL`), a negative value included.

`TGSMCodec` codes and decodes whole frames only, both counts answered;
its state is made by `Init` (and a second `Init` loses the first - kept).
`test_GSM`: every frame carries the magic, decoded samples are 13-bit, a
voiced signal (a 150 Hz voice's harmonics) comes back with correlation
0.999 once the predictors settle, silence stays silent, the coder is
deterministic and a frame without the magic is refused.  No published
test vector was at hand to check it bit for bit.

## Not yet

`NewWiredPtr` (memory), for which the DMA
buffers fall back on `NewPtr`.  `TMacintalkCodec` (speech) is not in the
ROM either, so a string played as a sound plays its bytes, as on the
machine.
