/*
	File:		testing/Journal.cpp

	Contains:	The journal: strokes recorded and played back.  See
				Journal.h.

	Reconstructed from the MP2x00 US ROM (0x000f8e08-0x000f9fd4); each
	function cites its origin.
*/

#include "Journal.h"
#include "Stroke.h"
#include "TabletBuffer.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "ByteOrder.h"
#include <string.h>

long					gJournallingState = 0;			// ROM 0x0c100fb8 gJournallingState
JournalReplayHandler*	gJournalReplayHandler = nil;	// (ROM 0x0c100fc0)

// What recording keeps (the ROM's gJournalRecordStuff, 0x14 bytes): the
// receiver and message each stroke is sent to, the first stroke's down
// time, the strokes recorded, the format.
struct JournalRecordStuff
{
	RefStruct*	fReceiver;		// +0x00
	RefStruct*	fMessage;		// +0x04
	ULong		fFirstTime;		// +0x08
	ULong		fCount;			// +0x0c
	short		fFormat;		// +0x10
};
static JournalRecordStuff*	gJournalRecordStuff = nil;	// ROM 0x0c100fbc gJournalRecordStuff

// the tablet's sample word out of a TabPt: x and y (16.16 pixels) in
// eighths, the pressure's low nibble
static inline ULong
SampleWord(Fixed x, Fixed y, ULong z)
{
	return (ULong) (ULong32) (((ULong32) x << 5) | (ULong32) (y >> 9) | (z & 0xf));
}


/*------------------------------------------------------------------------------
	P l a y i n g   b a c k
------------------------------------------------------------------------------*/

// ROM 0x000f8e08 __ct__20JournalReplayHandlerFv
JournalReplayHandler::JournalReplayHandler()
{
	fStroke = nil;
	fStrokeIndex = 0;
	fMoreStrokes = false;
	fBusy = true;
	// (the ROM leaves the rest as the block came)
	fStrokeCount = fFormat = fStrokesToPlay = 0;
	fDX = fDY = 0;
	fField18 = fSampleRate = fField1c = 0;
	fSampleCount = fDuration = fBaseTime = fStartTime = fBurstTime = fSent = fDue = 0;
	fSlow = false;
	fOffset = fSize = 0;
}


// ROM 0x000f8e58 __dt__20JournalReplayHandlerFv
JournalReplayHandler::~JournalReplayHandler()
{
	if (fStroke != nil)
		DisposPtr((Ptr) fStroke);
}


// ROM 0x000f98e8 InitStroke__20JournalReplayHandlerFUlT1
// The stroke's timing: the first stroke of a replay starts it (five
// seconds on when the file has more than one), each stroke starts its down
// time after that; a stroke written slower than the tablet samples - more
// than 60 samples, and fewer a second than eight short of the rate - is
// played 60 samples at its own pace and then 20 a second.
long
JournalReplayHandler::InitStroke(ULong dx, ULong dy)
{
	if (fStrokeIndex == 1)
	{
		fField18 = 0;
		// (the ROM asks the global handler, which is this one)
		if (gJournalReplayHandler->fStrokeCount < 2)
			fBaseTime = Ticks();
		else
			fBaseTime = Ticks() + 300;
	}
	fDY = dy;
	fDX = dx;
	fDuration = fStroke->fUpTime - fStroke->fDownTime;
	fSampleCount = fStroke->fCount;
	fSent = 0;
	fStartTime = fBaseTime + fStroke->fDownTime;
	fSlow = false;
	if (fSampleCount > 60)
	{
		// DEVIATION: __rt_udiv traps on a divisor of nought, which a stroke
		// down and up in one tick would give; the host answers nought
		ULong perSecond = fDuration != 0 ? (ULong) (ULong32) (fSampleCount * 60) / fDuration : 0;
		fSlow = perSecond < (ULong) (fSampleRate - 8);
	}
	fDue = 0;
	return 0;
}


// ROM 0x000f99cc PlayAStroke__20JournalReplayHandlerFP13JournalStrokeUlT2Uc
// A stroke to play: copied (it is in a binary, big-endian - turned into
// the host's order here) or borrowed (made in memory), every sample moved
// by (dx, dy) pixels, its timing set up.  ==> 0, -1 while one is playing.
//
// ROM BUG, kept: the offset is added to the stroke's first fCount words,
// which are its samples only in format 1; a format 2 stroke has its TabPts
// moved in the wrong places.
long
JournalReplayHandler::PlayAStroke(JournalStroke* stroke, ULong dx, ULong dy, Boolean borrow)
{
	if (fStroke != nil)
		return -1;
	if (!borrow)
	{
		ULong size = GetBigEndianWord((const UByte*) &stroke->fSize);
		JournalStroke* copy = (JournalStroke*) NewPtr(size);
		if (copy == nil)
			return MemError();
		BlockMove(stroke, copy, size);
		// (host: the header and the samples into the host's order)
		UByte* p = (UByte*) copy;
		for (ULong i = 0; i < 4; i++)
			((ULong32*) p)[i] = GetBigEndianWord(p + i * 4);
		if (fFormat == 2)
		{
			for (ULong i = 0; i < copy->fCount && 0x10 + (i + 1) * 12 <= size; i++)
			{
				TabPt* pt = (TabPt*) (p + 0x10 + i * 12);
				UByte* q = (UByte*) pt;
				pt->x = (Fixed) GetBigEndianWord(q);
				pt->y = (Fixed) GetBigEndianWord(q + 4);
				pt->z = GetBigEndianHalf(q + 8);
				pt->p = GetBigEndianHalf(q + 10);
			}
		}
		else
		{
			for (ULong i = 0; i < copy->fCount && 0x10 + (i + 1) * 4 <= size; i++)
				copy->fSamples[i] = GetBigEndianWord((const UByte*) &copy->fSamples[i]);
		}
		fStroke = copy;
	}
	else
		fStroke = stroke;
	ULong count = fStroke->fCount;
	if (dx != 0 || dy != 0)
		for (ULong i = 0; i < count; i++)
			fStroke->fSamples[i] = (ULong32) (fStroke->fSamples[i] + dx * 0x200000 + dy * 0x80);
	InitStroke(dx, dy);
	return 0;
}


// ROM 0x000f9a98 IsJournalReplayBusy__20JournalReplayHandlerFv
Boolean
JournalReplayHandler::IsJournalReplayBusy(void)
{
	return fBusy;
}


// ROM 0x000f9aa0 GetNextTabletSample__20JournalReplayHandlerFPUl
// The next sample, when one is due: the file's next stroke started when
// none is playing (no more: the replay is no longer busy), the samples due
// worked out from the time since the stroke started, one handed out.
// After the stroke's last the answer is a pen-up (0xe) and the stroke is
// done.
//
// ROM QUIRK, kept: the pen-up replaces the stroke's last sample, so that
// point is never played.
Boolean
JournalReplayHandler::GetNextTabletSample(ULong* sample)
{
	ULong now = Ticks();
	Boolean given = false;
	if (fStroke == nil)
	{
		if (!fMoreStrokes)
		{
			fBusy = false;
			return false;
		}
		JournalStroke* next = GetNextStroke();
		if (next == nil)
			fMoreStrokes = false;
		else
			PlayAStroke(next, fDX, fDY, false);
	}
	JournalStroke* stroke = fStroke;
	if (stroke == nil)
	{
		fBusy = false;
		return false;
	}
	if (fStartTime > now)
		return false;
	ULong sent = fSent;
	if (fDue <= sent)
	{
		ULong due;
		if (fSlow && sent == 59)
			fBurstTime = now;
		if (fSlow && sent >= 60)
			due = (ULong) (ULong32) (now - fBurstTime) / 3 + 0x3d;
		else
			// DEVIATION: __rt_udiv traps on a divisor of nought (a stroke of
			// no duration); the host has every sample due at once
			due = fDuration != 0 ? (ULong) (ULong32) (fSampleCount * (now - fStartTime)) / (ULong32) fDuration : fSampleCount;
		fDue = due;
	}
	if (sent < fDue)
	{
		if (fFormat == 1)
			*sample = stroke->fSamples[sent];
		else if (fFormat == 2)
		{
			TabPt* pt = (TabPt*) ((UByte*) stroke + 0x10 + sent * 12);
			*sample = SampleWord(pt->x, pt->y, pt->z);
		}
		fSent++;
		if (fSlow && fSent == 59)
			fBurstTime = now;
		given = true;
	}
	if (fSent >= fStroke->fCount)
	{
		*sample = kTabletPenUp;
		DisposPtr((Ptr) fStroke);
		fStroke = nil;
	}
	return given;
}


// ROM 0x000f9c68 ParseStrokeFileHeader__20JournalReplayHandlerFv
// A file of strokes: its stroke count, format and sample rate (all to be
// played), the first stroke 0x18 bytes in.
void
JournalReplayHandler::ParseStrokeFileHeader(void)
{
	const UByte* file = (const UByte*) BinaryData(fStrokes);
	fMoreStrokes = true;
	fStrokeCount = (short) GetBigEndianHalf(file + 4);
	fFormat = (short) GetBigEndianHalf(file + 2);
	fStrokeIndex = 0;
	fStrokesToPlay = fStrokeCount;
	fSampleRate = (short) GetBigEndianHalf(file + 6);
	fField1c = (short) GetBigEndianHalf(file + 8);
	fOffset = 0x18;
	fSize = 0;
}


// ROM 0x000f9d08 SetStrokesToPlay__20JournalReplayHandlerFs
// Only the first count of the file's strokes played (nought, or more than
// it has: all of them).
void
JournalReplayHandler::SetStrokesToPlay(short count)
{
	if (count == 0 || count >= fStrokeCount)
		fStrokesToPlay = fStrokeCount;
	else
		fStrokesToPlay = count;
}


// ROM 0x000f9d38 GetNextStroke__20JournalReplayHandlerFv
// The file's next stroke (after the last one, by its size), or nil when
// as many as are to be played have been.
JournalStroke*
JournalReplayHandler::GetNextStroke(void)
{
	if (fStrokesToPlay <= fStrokeIndex)
		return nil;
	UByte* file = (UByte*) BinaryData(fStrokes);
	JournalStroke* stroke = (JournalStroke*) (file + fOffset + fSize);
	fOffset = fOffset + fSize;
	fSize = GetBigEndianWord((const UByte*) &stroke->fSize);
	fStrokeIndex = fStrokeIndex + 1;
	return stroke;
}


// ROM 0x000f8e98 JournalInsertTabletSamople__Fv
// Every sample that is due put into the tablet buffer: before a stroke's
// first, a pen-down - the first stroke of a replay bypassing the tablet
// first, and when that cannot be done (the pen is down) its first sample
// is dropped with the pen-down.  An empty sample (a pause in a line made
// up by JournalReplayALine) is skipped.
void
JournalInsertTabletSamople(void)
{
	JournalReplayHandler* handler = gJournalReplayHandler;
	if (handler == nil)
		return;
	ULong sample;
	while (handler->GetNextTabletSample(&sample))
	{
		if (handler->fSent == 1)
		{
			long err;
			if (handler->fStrokeIndex != 1 || (err = StartBypassTablet()) == 0)
				err = InsertTabletSample(kTabletPenDown, 0);
			if (err != 0)
				continue;
		}
		if (sample != 0)
			InsertTabletSample(sample, 0);
	}
}


// ROM 0x000f8f34 JournalStopReplay__Fv
// The replay abandoned: the tablet's bypass ended, the handler gone.
void
JournalStopReplay(void)
{
	if (gJournalReplayHandler == nil)
		return;
	StopBypassTablet();
	gJournallingState = 0;
	if (gJournalReplayHandler != nil)
		delete gJournalReplayHandler;
	gJournalReplayHandler = nil;
}


// ROM 0x000f9890 IsJournalReplayBusy__Fv
Boolean
IsJournalReplayBusy(void)
{
	return gJournalReplayHandler != nil && gJournalReplayHandler->IsJournalReplayBusy();
}


// The test agent's idle proc (TTestAgent::IdleProc 0x00228374,
// testing/TestAgent.h) plays the journal first of all: the samples due,
// and when the replay has run out the journal is idle again.  So on the
// machine - and on the host - a replay plays only while the agent runs.
//
// host: that half of the idle proc on its own, for the unit tests, which
// run the view system without the OS and so without the agent's task
// (hal/host/HostTablet.cpp's HostTabletWait calls it every tick).
//
// ROM QUIRK, kept: nothing ends the tablet's bypass when a replay runs out
// (only JournalStopReplay does, which the test agent calls when it is
// deactivated), so the real pen stays out of it after a replay.
void
JournalAgentIdle(void)
{
	if (gJournallingState != 2)
		return;
	JournalInsertTabletSamople();
	if (!IsJournalReplayBusy())
		gJournallingState = 0;
}


/*------------------------------------------------------------------------------
	R e c o r d i n g
------------------------------------------------------------------------------*/

// ROM 0x000f9da4 JournalRecordAStroke__FP7TStroke
// A finished stroke, while recording, sent to the recorder as [binary,
// size]: a JournalStroke of its points in the format asked for, its times
// from the first stroke recorded.
//
// ROM QUIRK, kept: the block is sized as though the last sample were a
// twelve-byte TabPt, so a format 1 stroke's binary ends in eight bytes of
// whatever the block held.
void
JournalRecordAStroke(TStroke* stroke)
{
	RefVar binary;
	if (gJournallingState != 1)
		return;
	long bytes;
	if (gJournalRecordStuff->fFormat == 1)
		bytes = 4;
	else if (gJournalRecordStuff->fFormat == 2)
		bytes = 12;
	else
		return;
	ULong count = stroke->fCount;
	ULong size = (ULong) (bytes * ((long) count - 1) + 0x1c);
	UByte* block = (UByte*) NewPtr(size);
	gJournalRecordStuff->fCount++;
	if (gJournalRecordStuff->fCount == 1)
		gJournalRecordStuff->fFirstTime = stroke->fDownTime;
	PutBigEndianWord(block, (ULong32) size);
	PutBigEndianWord(block + 4, (ULong32) count);
	PutBigEndianWord(block + 8, (ULong32) (stroke->fDownTime - gJournalRecordStuff->fFirstTime));
	PutBigEndianWord(block + 0xc, (ULong32) (stroke->fUpTime - gJournalRecordStuff->fFirstTime));
	UByte* p = block + 0x10;
	for (long i = 0; i < (long) count; i++, p += bytes)
	{
		TabPt pt;
		stroke->GetTabPt(i, &pt);
		if (gJournalRecordStuff->fFormat == 1)
			PutBigEndianWord(p, (ULong32) SampleWord(pt.x, pt.y, pt.z));
		else if (gJournalRecordStuff->fFormat == 2)
		{
			PutBigEndianWord(p, (ULong32) pt.x);
			PutBigEndianWord(p + 4, (ULong32) pt.y);
			PutBigEndianHalf(p + 8, pt.z);
			PutBigEndianHalf(p + 10, pt.p);
		}
	}
	RefVar args(AllocateArray(RefVar(Intern((char*) "array")), 2));
	binary = AllocateBinary(RSSYMfoo, size);
	BlockMove(block, BinaryData(binary), size);
	DisposPtr((Ptr) block);
	SetArraySlot(args, 0, binary);
	SetArraySlot(args, 1, RefVar(MAKEINT(size)));
	DoMessage(*gJournalRecordStuff->fReceiver, *gJournalRecordStuff->fMessage, args);
}


/*------------------------------------------------------------------------------
	N a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x000f8f78 FJournalStopRecord
// JournalStopRecord(): recording ended.
static Ref
FJournalStopRecord(RefArg /*rcvr*/)
{
	gJournallingState = 0;
	if (gJournalRecordStuff != nil)
	{
		delete gJournalRecordStuff->fReceiver;
		delete gJournalRecordStuff->fMessage;
		DisposPtr((Ptr) gJournalRecordStuff);
		gJournalRecordStuff = nil;
	}
	return MAKEINT(0);
}


// ROM 0x000f8ff0 FJournalStartRecord
// receiver:JournalStartRecord(message, format): every stroke finished from
// now on sent to receiver:message([binary, size]).  ==> 0, or -1.
static Ref
FJournalStartRecord(RefArg receiver, RefArg message, RefArg format)
{
	NewtonErr err = noErr;
	gJournalRecordStuff = (JournalRecordStuff*) NewPtr(sizeof(JournalRecordStuff));
	gJournalRecordStuff->fCount = 0;
	gJournalRecordStuff->fReceiver = new RefStruct;
	if (gJournalRecordStuff->fReceiver == nil)
		return MAKEINT(MemError());
	*gJournalRecordStuff->fReceiver = receiver;
	gJournalRecordStuff->fMessage = new RefStruct;
	if (gJournalRecordStuff->fMessage == nil)
		return MAKEINT(MemError());
	*gJournalRecordStuff->fMessage = message;
	gJournalRecordStuff->fFormat = (short) RINT(format);
	if (gJournalRecordStuff != nil && gJournalRecordStuff->fReceiver != nil && gJournalRecordStuff->fMessage != nil)
		gJournallingState = 1;
	else
	{
		err = -1;
		FJournalStopRecord(RefVar());
	}
	return MAKEINT(err);
}


// ROM 0x000f9130 FJournalReplayAStroke
// JournalReplayAStroke(stroke, dx, dy, format, sampleRate, other): one
// recorded stroke played (moved by dx, dy).  ==> 0, or -1 while the
// journal is busy.  NOT YET: the test agent told (AgentReportStatus 0xd).
static Ref
FJournalReplayAStroke(RefArg /*rcvr*/, RefArg stroke, RefArg dx, RefArg dy, RefArg format, RefArg sampleRate, RefArg other)
{
	if (gJournallingState != 0)
		return MAKEINT(-1);
	gJournalReplayHandler = new JournalReplayHandler;
	if (gJournalReplayHandler == nil)
		return MAKEINT(MemError());
	gJournalReplayHandler->fStrokeCount = 1;
	gJournalReplayHandler->fStrokesToPlay = 1;
	gJournalReplayHandler->fFormat = (short) RINT(format);
	gJournalReplayHandler->fSampleRate = (short) RINT(sampleRate);
	gJournalReplayHandler->fField1c = (short) RINT(other);
	ULong x = (ULong) RINT(dx);
	ULong y = (ULong) RINT(dy);
	JournalStroke* data = (JournalStroke*) BinaryData(stroke);
	gJournalReplayHandler->fStrokeIndex = 1;
	NewtonErr err = gJournalReplayHandler->PlayAStroke(data, x, y, false);
	gJournallingState = 2;
	return MAKEINT(err);
}


// ROM 0x000f92c4 FJournalReplayALine
// JournalReplayALine(x1, y1, x2, y2, hold, sampleRate): a straight line
// drawn from (x1, y1) to (x2, y2), a sample every two pixels of its longer
// side (or ten, for a dot).  With hold not nil, a line of more than three
// pixels is held still at its start for 60 samples first, drawn a sample
// every eight pixels and taking a minute's worth of samples at the rate;
// a shorter one is drawn twice with a pen-up and a pause between - a
// double tap.  ==> 0, or -1 while the journal is busy.  NOT YET: the test
// agent told (AgentReportStatus 0xd).
static Ref
FJournalReplayALine(RefArg /*rcvr*/, RefArg x1Ref, RefArg y1Ref, RefArg x2Ref, RefArg y2Ref, RefArg hold, RefArg rateRef)
{
	short rate = (short) RINT(rateRef);
	if (gJournallingState != 0)
		return MAKEINT(-1);
	if (gJournalReplayHandler == nil)
	{
		gJournalReplayHandler = new JournalReplayHandler;
		if (gJournalReplayHandler == nil)
			return MAKEINT(MemError());
	}
	else
	{
		gJournalReplayHandler->fStrokeIndex = 0;
		gJournalReplayHandler->fStroke = nil;
		gJournalReplayHandler->fMoreStrokes = false;
		gJournalReplayHandler->fBusy = true;
	}
	Boolean held = false;
	Boolean twice = false;
	Long x1 = RINT(x1Ref), y1 = RINT(y1Ref), x2 = RINT(x2Ref), y2 = RINT(y2Ref);
	long width = x2 < x1 ? x1 - x2 : x2 - x1;
	long height = y2 < y1 ? y1 - y2 : y2 - y1;
	if (NOTNIL(hold))
	{
		long along = width < 4 ? height : width;
		held = along > 3;
		if (!held)
			twice = true;
	}
	ULong steps = (ULong) (width > height ? width : height) >> 1;
	if (held)
		steps = (ULong) ((long) steps >> 2);
	if (steps == 0)
		steps = 10;
	long count = steps + 1;
	long pause = 0;
	if (twice)
	{
		pause = (rate * 9) / 60;
		count = pause + count * 2 + 2;
	}
	if (held)
		count += 60;
	ULong size = count * 4 + 0x10;
	JournalStroke* stroke = (JournalStroke*) NewPtr(size);
	if (stroke == nil)
		return MAKEINT(MemError());
	stroke->fSize = (ULong32) size;
	stroke->fCount = (ULong32) count;
	stroke->fDownTime = 5;
	// (DEVIATION: __rt_sdiv/__rt_udiv trap on a rate of nought; the host
	//  answers nought)
	long up;
	if (held)
		up = (rate != 0 ? (ULong) 3600 / (ULong) rate : 0) + 5 + (long) (steps * 60) / 20;
	else
		up = (rate != 0 ? (count * 60) / rate : 0) + 5;
	stroke->fUpTime = (ULong32) up;
	if (stroke->fUpTime == 5)
		stroke->fUpTime = 6;
	ULong32* p = stroke->fSamples;
	if (held)
		for (ULong i = 0; i < 60; i++)
			*p++ = (ULong32) (x1 * 0x200000 + y1 * 0x80 + 4);
	for (long i = 0; i < (long) steps; i++)
		*p++ = (ULong32) (((i * (x2 - x1)) / (long) steps + x1) * 0x200000 + ((i * (y2 - y1)) / (long) steps + y1) * 0x80 + 4);
	ULong32 last = (ULong32) (x2 * 0x200000 + y2 * 0x80 + 4);
	*p = last;
	if (twice)
	{
		p[1] = kTabletPenUp;
		p += 2;
		for (long i = 0; i < pause; i++)
			*p++ = 0;
		*p++ = kTabletPenDown;
		for (long i = 0; i < (long) steps; i++)
			*p++ = (ULong32) (((i * (x2 - x1)) / (long) steps + x1) * 0x200000 + ((i * (y2 - y1)) / (long) steps + y1) * 0x80 + 4);
		*p = last;
	}
	gJournalReplayHandler->fStrokeCount = 1;
	gJournalReplayHandler->fStrokesToPlay = 1;
	gJournalReplayHandler->fFormat = 1;
	gJournalReplayHandler->fSampleRate = rate;
	gJournalReplayHandler->fField1c = 0x14;
	gJournalReplayHandler->fStrokeIndex = 1;
	NewtonErr err = gJournalReplayHandler->PlayAStroke(stroke, 0, 0, true);
	gJournallingState = 2;
	return MAKEINT(err);
}


// ROM 0x000f976c FJournalReplayStrokes
// JournalReplayStrokes(strokes, dx, dy, count): a file of strokes played,
// moved by dx, dy - the first count of them when count is an integer.
// ==> 0, or -1 while the journal is busy.  NOT YET: the test agent told
// (AgentReportStatus 0xd).
static Ref
FJournalReplayStrokes(RefArg /*rcvr*/, RefArg strokes, RefArg dx, RefArg dy, RefArg count)
{
	if (gJournallingState != 0)
		return MAKEINT(-1);
	gJournalReplayHandler = new JournalReplayHandler;
	if (gJournalReplayHandler == nil)
		return MAKEINT(MemError());
	gJournallingState = 2;
	gJournalReplayHandler->fStrokes = strokes;
	gJournalReplayHandler->ParseStrokeFileHeader();
	if (ISINT(count))
		gJournalReplayHandler->SetStrokesToPlay((short) RVALUE(count));
	gJournalReplayHandler->fDX = RINT(dx);
	gJournalReplayHandler->fDY = RINT(dy);
	return MAKEINT(0);
}


// ROM 0x000f98c8 FJournalReplayBusy
// JournalReplayBusy(): whether a replay is still playing.
static Ref
FJournalReplayBusy(RefArg /*rcvr*/)
{
	return IsJournalReplayBusy() ? TRUEREF : NILREF;
}


// ROM 0x00202c70 FStartBypassTablet
// StartBypassTablet(): the tablet's own samples ignored.  ==> 0, or -1
// when the pen is down.
static Ref
FStartBypassTablet(RefArg /*rcvr*/)
{
	return MAKEINT(StartBypassTablet());
}


// ROM 0x00202c88 FStopBypassTablet
static Ref
FStopBypassTablet(RefArg /*rcvr*/)
{
	return MAKEINT(StopBypassTablet());
}


// ROM 0x00202ca0 FInsertTabletSample
// InsertTabletSample(x, y, z, time): a sample of the pen at (x, y) pixels
// with pressure z put into the tablet buffer (time 0: now).
//
// ROM QUIRK, kept: x is not masked to its fourteen bits, so a negative x
// fills the word's top with ones.
static Ref
FInsertTabletSample(RefArg /*rcvr*/, RefArg x, RefArg y, RefArg z, RefArg time)
{
	Long sx = RINT(x);
	ULong sy = (ULong) RINT(y);
	ULong sz = (ULong) RINT(z);
	ULong t = (ULong) RINT(time);
	ULong sample = (ULong) (ULong32) ((ULong32) (sx << 21) | (ULong32) ((sy & 0x3fff) << 7) | (ULong32) (sz & 0xf));
	return MAKEINT(InsertTabletSample(sample, t));
}


void
RegisterJournalNatives(void)
{
	RegisterNativeFunction("FJournalStartRecord", (void*) FJournalStartRecord, 2);
	RegisterNativeFunction("FJournalStopRecord", (void*) FJournalStopRecord, 0);
	RegisterNativeFunction("FJournalReplayAStroke", (void*) FJournalReplayAStroke, 6);
	RegisterNativeFunction("FJournalReplayALine", (void*) FJournalReplayALine, 6);
	RegisterNativeFunction("FJournalReplayStrokes", (void*) FJournalReplayStrokes, 4);
	RegisterNativeFunction("FJournalReplayBusy", (void*) FJournalReplayBusy, 0);
	RegisterNativeFunction("FStartBypassTablet", (void*) FStartBypassTablet, 0);
	RegisterNativeFunction("FStopBypassTablet", (void*) FStopBypassTablet, 0);
	RegisterNativeFunction("FInsertTabletSample", (void*) FInsertTabletSample, 4);
}
