# The testing system (`src/testing/`)

The Newton carried its own test tools in ROM: a **test agent**
(`TTestAgent`, an application world named `'tagt` that the loader starts
at boot) that a test server on a desktop drove over a connection,
reporters that sent it messages, test cases run as tasks, and the
**journal**, which records the pen's strokes and plays them back into the
tablet buffer so that a test can write on the machine as a person would.
38 NewtonScript natives reach it (`natives.py --unbound`, the `testing`
area).

Reconstructed so far: the journal and the tablet's bypass.  The test
agent, its reporters and test cases, the debugging hooks (`StdioOn`,
`DebugRunUntilIdle`, ...), Uriah and the IR sniffing are NOT YET.

## The journal (`testing/Journal.h`, ROM 0x000f8e08-0x000f9fd4)

`gJournallingState` says what it is doing: 0 idle, 1 recording, 2 playing
back.

### Recording

`receiver:JournalStartRecord(message, format)` - the receiver is the
function's `self` - keeps the receiver and message (the ROM's
`gJournalRecordStuff`, 0x14 bytes).  From then on
`StrokeCentral::IdleStrokes`, as each stroke is finished, calls
`JournalRecordAStroke`, which sends `receiver:message(binary, size)`:
`binary` a `'foo` binary holding a **JournalStroke** -

| offset | |
|---|---|
| +0x00 | the size in bytes |
| +0x04 | the sample count |
| +0x08 | the down time, in ticks from the first stroke recorded |
| +0x0c | the up time, likewise |
| +0x10 | a sample per point: format 1 the tablet's own sample word (x in eighths of a pixel in bits 18-31, y in bits 4-17, the pressure in the low nibble); format 2 the whole `TabPt` (x and y 16.16, the pressure and flag halfwords) |

The block is sized as though the last sample were twelve bytes, so a
format 1 stroke ends in eight bytes of whatever the block held (kept).
`JournalStopRecord()` ends it.

### Playing back

A `JournalReplayHandler` (0x48 bytes) plays strokes:

- `JournalReplayAStroke(stroke, dx, dy, format, sampleRate, other)` - one
  recorded stroke, moved by (dx, dy) pixels;
- `JournalReplayALine(x1, y1, x2, y2, hold, sampleRate)` - a straight line
  made up on the spot, a sample every two pixels of its longer side (ten
  for a dot).  With `hold` not nil, a line of more than three pixels is
  held still at its start for 60 samples and drawn a sample every eight
  pixels (a press and drag, as a selection is made), and a shorter one is
  drawn twice with a pen-up and a pause of `rate * 9 / 60` empty samples
  between (a double tap);
- `JournalReplayStrokes(strokes, dx, dy, count)` - a file of strokes: a
  0x18-byte header (the format at +2, the stroke count at +4, the sample
  rate at +6, a word at +8 nothing reads) and the strokes end to end,
  each found by the one before's size; `count` plays only the first so
  many.

`JournalReplayBusy()` says whether a replay is still going.

**Timing.**  A replay starts when it is asked for - five seconds later for
a file of several strokes - and each stroke at its down time after that.
A stroke's samples fall due in proportion to the time since it started
(`count * elapsed / duration`), except that a stroke written slower than
the tablet samples (more than 60 samples, fewer a second than eight short
of the sample rate) plays its first 60 at its own pace and the rest at 20
a second.

**Into the tablet buffer.**  `JournalInsertTabletSamople` (the ROM's
spelling) takes every sample that is due and inserts it with
`InsertTabletSample`, a pen-down (0xd) before a stroke's first.  Before
the first stroke of a replay it **bypasses the tablet** -
`StartBypassTablet`, the tablet driver's state 8, in which the real pen's
samples are ignored; refused while the pen is down, and then that first
sample and its pen-down are dropped.  After a stroke's last sample comes
a pen-up (0xe) - *in place of* that last sample, so a replayed stroke
never plays its final point (kept: the `n` of a replayed "ton" loses the
end of its last leg and reads as "tor").  An empty sample (the pause in a
double tap) is skipped.

Nothing ends the bypass when a replay runs out: only `JournalStopReplay`
does, which the test agent calls when it is deactivated or a test exits
(kept - after a replay, the real pen stays out of it).

On the machine the test agent's idle proc (`TTestAgent::IdleProc`
0x00228374) runs `JournalInsertTabletSamople` and puts the journal back
to idle when the replay is no longer busy - every 50 milliseconds under a
test manager, every three seconds otherwise.  DEVIATION: with the test
agent NOT YET, the host's inker task (and the tests' wait hook) runs that
half of the idle proc, `JournalAgentIdle`, every tick.

DEVIATION: the words of a JournalStroke in a binary (what recording makes
and a file holds) are kept big-endian, as the ROM wrote them, so a
journal is the same on every host; the handler turns a stroke into host
order as it copies it in.  A line JournalReplayALine makes is in host
order already, and the handler borrows it rather than copying it (as
the ROM does).

ROM bug kept: `PlayAStroke` moves a stroke by adding the offset to its
first `count` words - its samples in format 1, but in format 2 the wrong
places in the TabPts.

### The tablet's natives

`StartBypassTablet()`/`StopBypassTablet()` (0 or -1), and
`InsertTabletSample(x, y, z, time)`, which puts one sample of the pen at
(x, y) pixels into the buffer (x is not masked to its fourteen bits, so a
negative one fills the word's top - kept).  The tablet driver itself
(`TResistiveTablet`) is NOT YET: the host's tablet (`hal/host/
HostTablet.h`) keeps the driver's state - idle, pen down, bypassed - and
answers the bypass through `gTabletDriverBypass`; while it is bypassed
the window's pen is ignored.  `HobbleTablet` (a message to the inker) is
NOT YET.

### Tests

`test_Views`'s `TestJournal` records a stroke, checks the JournalStroke
it comes as, plays it back moved and checks it reaches a view as a click
there with the tablet bypassed meanwhile, and does the same with a line
from `JournalReplayALine`.  `src/host/demo/journal.ns` (ctest
`host.NewtonJournal`) writes "ton" on the Notepad while recording and
plays the four strokes back: the page reads "ton tor" afterwards, and
nothing else.  (It once also left an ink word behind: the replayed word,
written below any text, goes in at the caret without a child of the page
choosing it, and the host's `TEditView::HandleWord` answered nil there,
so the command's result was 0, the handler did not claim the word unit,
the arbiter marked it claimed *and invalid* as an at-once winner, and
the clean-up expired its strokes as ink.  The ROM answers a register it
never set - in `HandleWordUnit`'s case the word's text pointer - which
is not nil; the host now does the same.  Nothing to do with the
journal: a word written well away from the text with remote writing on
did the same.)
