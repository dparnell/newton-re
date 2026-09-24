# The classifier net, and what is known about `BPNetEvaluate`

At the bottom of reading a word is a back-propagation net. Every
character the handwriting engine offers comes out of it:
`WordRecogNetEvaluate` fills its inputs from a patternizer, runs it, and
maps its 134 outputs through the common info's tables into scores for
the 256 character codes; `CharBoxNetEvaluate` does the same for a letter
written in a box.

`recognition/BPNet.h` has the net and its life, and
`analysis/bpnet.py` generates `BPNetTables.cpp` - the template and the
eight trained tables it points at. **`BPNetEvaluate` itself is NOT
YET**; this page is what has been read out of its assembly, so that
whoever writes it does not start again.

Read the assembly with

```
analysis/disasm.py --project build/ghidra --name MP2x00US --ghidra <ghidra> \
                   --start 0x0001a260 --end 0x0001a618 --force
```

`--force` is needed: nothing calls the body directly, so the
auto-analysis leaves 0x0001a294 onwards as raw bytes.

## The shape of the net

Fixed point, and tiny - which is what let it run on a 162 MHz
StrongARM with no floating point at all.

* A unit's activation is **one byte**, 0 to 255, with 128 standing for
  nought.
* A weight is **one byte biased by 128**, so a unit accumulates
  `sum += activation * (weight - 128)`. The assembly does that in two
  instructions, `mla r11,r9,r10,r11` and `sub r11,r11,r9,lsl #7`,
  because `a*w - a*128` is `a*(w-128)`.
* The sigmoid is a **lookup table**. The magnitude of the sum is taken
  (`mvnlt r11,r11` for a negative one, which is `~x` rather than `-x`),
  clamped at 0x5900, shifted down six bits and used as an index into
  `QSigLu`; a negative sum answers 255 minus the result, done as an
  exclusive-or with 0xff. So the whole non-linearity is one `ldrb`.
  `QSigLu` is 360 bytes, starts at 128 and rises to 255, and
  0x5900 >> 6 = 356 is inside it.

There are **1002 units**: 384 inputs, 484 hidden and 134 outputs, laid
end to end in one byte array (`fUnits`), with the outputs last
(`fOutputs = fUnits + 868`). They are worked out in order, so a unit
may read any unit before it, and the write pointer walks forward from
`fUnits + 384` while the read pointer is always *behind* it.

## The connection program

The connections are a program rather than a matrix. `newtConnects` is
2392 words; each word is

| bits | what |
|---|---|
| 31..18 | a signed count, negative, of connections in this group |
| 17 | step on to the next of the four weight bytes in hand |
| 16 | take four more weight words (sixteen bytes) |
| 15..0 | how far *back* from the write pointer the activations start |

A word whose count is **nought** ends the unit being worked out - the
sigmoid is applied and the byte stored - and its low halfword is the
next unit's **bias**, sign-extended and multiplied by 256
(`mov r11,lr,lsl #0x10` then `mov r11,r11,asr #0x8`).

Three things about that check out against the data, which is why the
reading above can be trusted:

* there are exactly **619** words whose count is nought - 618 units and
  one terminator;
* exactly **one** of them has bit 17 set, and it is the very last word
  in the table (index 2391), which is what makes the routine return;
* the counts come to 83,448 connections, and rounding each group up to
  a whole number of weight bytes brings that to about the 91,124 bytes
  of `bpWeight`.

`test_Rosetta` asserts the first two.

## How the assembly runs it

The inner loop is unrolled sixteen ways: four blocks, one per weight
byte held in r5, r6, r7 and r8, each of four quads, one per byte of the
activation word in r2. A block is 0x4c bytes and they sit at
0x0001a294, 0x0001a2e0, 0x0001a32c and 0x0001a378; after the fourth,
0x0001a3c0 loads sixteen fresh weight bytes with
`ldmia r1!,{r5,r6,r7,r8}` and 0x0001a3c4 branches back to the first.

Between groups the code has to resume at the right weight byte *and*
the right activation byte, which is what the computed jump at
0x0001a610 is for:

```
and r9,lr,#0x3            ; which byte of the activation word to start at
sub r12,r12,r9            ; ... paid for out of the count
bic lr,lr,#0x3
ldr r2,[lr],#0x4
add pc,r10,r9, lsl #0x4   ; r10 = the block for the current weight byte
```

`r10` is carried from group to group as "which of the four weight bytes
we are on": bit 17 of the word advances it by 0x4c and bit 16 winds it
back by 0xe4 (three blocks) and reloads. `adr r10,0x1a378` sets it to
the *fourth* block at the start, so the first word - which has bit 16
set - winds back to the first.

There is a second set of entry points at 0x0001a3cc, 0x0001a444,
0x0001a4bc and 0x0001a534, reached with `blge`. The `bl` is not a call:
it is how the routine gets the block's own base into `lr`, which
`mov r10,lr` then records. Each begins `addne pc,pc,r12,lsl #0x4`,
skipping `r12` quads when the count has been overshot.

## What does not add up

```
0001a26c  ldr r1,[r0,#0x38]       ; r1 = net->fWeights
0001a27c  add r1,r1,#0x3500000
```

`fWeights` is `bpWeight`, at 0x003948f0, and every use of r1 afterwards
is `ldmia r1!,{r5,r6,r7,r8}` - it only ever moves forward. So the net
reads its first weights from 0x038948f0, which is outside the eight
megabytes of ROM and outside anything in `layout.json`. Nothing else
writes `fWeights`: `BPNetCreateNumOut` copies the template verbatim and
`BPNetLoad` only makes room for the units.

Everything *else* about the weights says they start at `bpWeight`: the
table is exactly the size the connection counts call for, and the net
has no other weights. So either there is a mapping of the ROM at
0x03500000 that has not been found, or the field is not read the way it
looks.

Until that is settled the reconstruction would have to take the weights
from `fWeights` and note the difference, which is why `BPNetEvaluate`
is left NOT YET rather than written on a guess.

## What is left below reading

The net is the bottom. Above it, and all NOT YET:

| | what | size |
|---|---|---|
| the patternizers | `NetPattern*`, seven kinds, what turns strokes into the net's 384 inputs | 7.4 KB |
| the segments | `Segment*`, cutting a word into characters | 20 KB |
| the boxed recogniser | `CharBox*` | 2.2 KB |
| the feature extraction | `low_type`, `EXTR`, `SPEC_TYPE` | 556 KB, 2384 symbols |
| level 3's top | `WordRecogAddStroke`, `WordRecogAnalyzeWord` | 10 KB |
| level 2's top | `RosettaSetArea`, `RosettaClassify` and its three passes | 3 KB |
