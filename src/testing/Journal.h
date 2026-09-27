/*
	File:		testing/Journal.h

	Contains:	The journal: the pen's strokes recorded as they are
				written and played back into the tablet buffer, which is
				how the Newton's test tools drove the machine as a person
				would.

				Recording (JournalStartRecord(receiver, message, format))
				turns every finished stroke into a JournalStroke - a header
				of four words (its size in bytes, its sample count, and its
				down and up times in ticks from the first stroke's down
				time) and a sample per point, either the tablet's own
				sample word (format 1: x in eighths of a pixel in bits
				18-31, y in bits 4-17, the pressure in the low nibble) or a
				whole TabPt (format 2, twelve bytes) - and sends it, as a
				binary with its size, to receiver:message.

				Playing back is a JournalReplayHandler: one stroke
				(JournalReplayAStroke), a straight line made up on the spot
				(JournalReplayALine - held still first when asked, or drawn
				twice with a pause between), or a file of strokes
				(JournalReplayStrokes: a 0x18-byte header - the format at
				+2, the stroke count at +4, the sample rate at +6 - then
				the strokes end to end).  Its samples fall due at the
				rate the stroke took to write, measured from when the
				replay began (five seconds later for a file of several),
				and JournalInsertTabletSamople - the ROM's spelling - puts
				those that are due into the tablet buffer with a pen-down
				before the first (the tablet bypassed for the first stroke,
				so the real pen stays out of it) and a pen-up after the
				last.  The test agent (TTestAgent, 'tagt - TestAgent.h)
				runs that from its idle proc, so a replay plays only while
				the agent runs (ActivateTestAgent); the unit tests, which
				have no agent, call JournalAgentIdle (below) instead.

				gJournallingState says which: 0 idle, 1 recording, 2
				playing back.

				DEVIATION: a JournalStroke in a binary - what recording
				makes and a file holds - keeps its words big-endian as the
				ROM wrote them, so a journal is the same on every host;
				the handler turns a stroke into host order as it copies it
				in.  A stroke JournalReplayALine makes is in host order
				already (the handler borrows it rather than copying it).

	Reconstructed from the MP2x00 US ROM (0x000f8e08-0x000f9fd4); each
	function cites its origin.
*/

#ifndef __JOURNAL_H
#define __JOURNAL_H

#include "Newton.h"
#include "objects.h"

class TStroke;

// A stroke as the journal keeps it: the header, then fCount samples of
// four bytes (format 1) or twelve (format 2).
struct JournalStroke
{
	ULong32		fSize;			// +0x00  bytes, header included
	ULong32		fCount;			// +0x04  samples
	ULong32		fDownTime;		// +0x08  ticks from the first stroke recorded
	ULong32		fUpTime;		// +0x0c
	ULong32		fSamples[1];	// +0x10
};

// The playing back (the ROM's is 0x48 bytes).
class JournalReplayHandler
{
public:
					JournalReplayHandler();									// ROM 0x000f8e08 __ct__20JournalReplayHandlerFv
					~JournalReplayHandler();								// ROM 0x000f8e58 __dt__20JournalReplayHandlerFv

	long			InitStroke(ULong dx, ULong dy);							// ROM 0x000f98e8 InitStroke__20JournalReplayHandlerFUlT1 - the stroke's timing set up
	long			PlayAStroke(JournalStroke* stroke, ULong dx, ULong dy, Boolean borrow);	// ROM 0x000f99cc PlayAStroke__20JournalReplayHandlerFP13JournalStrokeUlT2Uc - ==> 0, -1 when one is playing
	Boolean			IsJournalReplayBusy(void);								// ROM 0x000f9a98 IsJournalReplayBusy__20JournalReplayHandlerFv
	Boolean			GetNextTabletSample(ULong* sample);						// ROM 0x000f9aa0 GetNextTabletSample__20JournalReplayHandlerFPUl - ==> whether one was due
	void			ParseStrokeFileHeader(void);							// ROM 0x000f9c68 ParseStrokeFileHeader__20JournalReplayHandlerFv
	void			SetStrokesToPlay(short count);							// ROM 0x000f9d08 SetStrokesToPlay__20JournalReplayHandlerFs
	JournalStroke*	GetNextStroke(void);									// ROM 0x000f9d38 GetNextStroke__20JournalReplayHandlerFv - in the file, still big-endian

	JournalStroke*	fStroke;			// +0x00  the stroke playing (a copy, or a borrowed one), nil between them
	RefStruct		fStrokes;			// +0x04  the file of strokes (the ROM's TObjectPtr)
	short			fStrokeCount;		// +0x08  strokes in the file
	short			fFormat;			// +0x0a  1: sample words, 2: TabPts
	short			fStrokesToPlay;		// +0x0c
	long			fDX;				// +0x10  pixels every sample is moved by
	long			fDY;				// +0x14
	short			fField18;			// +0x18  (cleared at the first stroke; nothing reads it)
	short			fSampleRate;		// +0x1a  samples a second the tablet makes
	short			fField1c;			// +0x1c  (the file's word at +8; nothing reads it)
	short			fStrokeIndex;		// +0x1e  strokes started
	ULong			fSampleCount;		// +0x20  the stroke's
	ULong			fDuration;			// +0x24  its up time less its down time
	ULong			fBaseTime;			// +0x28  when the replay began
	ULong			fStartTime;			// +0x2c  when this stroke starts
	ULong			fBurstTime;			// +0x30  when the 59th sample went (a slow stroke's pace after it)
	ULong			fSent;				// +0x34  samples given out
	ULong			fDue;				// +0x38  samples due so far
	Boolean			fSlow;				// +0x3c  written slower than the tablet samples: 60 at the stroke's pace, then 20 a second
	Boolean			fBusy;				// +0x3d
	Boolean			fMoreStrokes;		// +0x3e  the file has strokes left
	ULong			fOffset;			// +0x40  where the next stroke is in the file
	ULong			fSize;				// +0x44  ... the size of the last
};

extern long						gJournallingState;		// ROM 0x0c100fb8 gJournallingState - 0 idle, 1 recording, 2 playing back
extern JournalReplayHandler*	gJournalReplayHandler;	// (ROM 0x0c100fc0)

void	JournalInsertTabletSamople(void);			// ROM 0x000f8e98 JournalInsertTabletSamople__Fv - the samples due put into the tablet buffer
void	JournalStopReplay(void);					// ROM 0x000f8f34 JournalStopReplay__Fv - the tablet's bypass ended, the handler gone
Boolean	IsJournalReplayBusy(void);					// ROM 0x000f9890 IsJournalReplayBusy__Fv
void	JournalRecordAStroke(TStroke* stroke);		// ROM 0x000f9da4 JournalRecordAStroke__FP7TStroke - a finished stroke sent to the recorder

// host: the test agent's idle proc's journal half (TTestAgent::IdleProc
// 0x00228374, the agent itself NOT YET): the samples due played and, when
// the replay has run out, the state back to idle.  The host's inker calls
// it every tick.
void	JournalAgentIdle(void);					// host: for the unit tests, which have no agent task

void	RegisterJournalNatives(void);				// JournalStartRecord, ..., and the tablet's natives

#endif	/* __JOURNAL_H */
