/*
	File:		recognition/Inker.cpp

	Contains:	The inker, the 'inkr world (recognition/Inker.h).

				Reconstructed from the MP2x00 US ROM (0x002173ec-0x00219100);
				each function cites its origin.
*/

#include "Inker.h"
#include "TabletBuffer.h"
#include "StrokeQueue.h"
#include "SystemEvents.h"
#include "UserTasks.h"
#include "NewtonTime.h"
#include "hal/Timer.h"
#include "Objects.h"
#include "Frames.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "Ports.h"
#include "Draw.h"
#include "Shapes.h"
#include "Fonts.h"
#include "Text.h"
#include "Pictures.h"
#include "Screen.h"
#include "Unicode.h"
#include "OSErrors.h"
#include "UserPersistent.h"

#include <stdlib.h>
#include <stdio.h>

Boolean		gWireRecog = true;						// ROM 0x0c104d2c gWireRecog (1 in the ROM's initialised data)
InkerCalibrateFlags	gCalibrate;						// ROM 0x0c104d20 gCalibrate
TInker*		gInker = nil;
void		(*gInkerCalibrationTargetHook)(short h, short v) = nil;
Boolean		(*gInkerHostIdleHook)(void) = nil;

// the calibration screen's words (ROM 0x0037a70c-0x0037a84c)
static const char	myCalText11[] = "Your Newton device needs to be calibrated";	// ROM 0x0037a70c myCalText11
static const char	myCalText12[] = "to the way you naturally hold a pen.";			// ROM 0x0037a738 myCalText12
static const char	myCalText13[] = "Hold the Newton pen on the center";			// ROM 0x0037a760 myCalText13
static const char	myCalText14[] = "of the X in the corner above until it";		// ROM 0x0037a784 myCalText14
static const char	myCalText15[] = "darkens and then lift the pen.";				// ROM 0x0037a7ac myCalText15
static const char	myCalText21[] = "Now repeat on the center of the X";			// ROM 0x0037a7cc myCalText21
static const char	myCalText22[] = "in the corner below.";							// ROM 0x0037a7f0 myCalText22
static const char	myCalText31[] = "To confirm the pen is correctly";				// ROM 0x0037a808 myCalText31
static const char	myCalText32[] = "aligned, hold the pen on the center";			// ROM 0x0037a828 myCalText32
static const char	myCalText33[] = "of this last X.";								// ROM 0x0037a84c myCalText33


/*------------------------------------------------------------------------------
	T I n k e r E v e n t H a n d l e r
------------------------------------------------------------------------------*/

// ROM 0x002173ec AEHandlerProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
// A command to the inker.  Those that answer put their answer in the
// event and reply with it.
void
TInkerEventHandler::AEHandlerProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* anEvent)
{
	TInkerEvent* event = (TInkerEvent*) anEvent;
	TInker* inker = gInker;
	ULong command = event->fCommand;
	switch (command)
	{
	case kInkerIdle:
		TabletIdle();
		{
			Boolean more = gInkerHostIdleHook != nil && gInkerHostIdleHook();	// (host: a test's queued pen)
			if (!TabletBufferEmpty())
			{
				InkThem();
				if (!more && inker->fInkMode == 3 && TabletBufferEmpty() && GetTabletState() == kTabletStateIdle)
					return;
			}
		}
		StartIdle();			// (the pen is down: read it again in 50 ms)
		return;

	case kInkerConvert:
		// (the same loop as InkThem's, which the ROM writes out here a
		// third time)
		inker->LCDEntry();
		return;

	case kInkerCalibrate:
		event->fArg = (ULong) inker->Calibrate(event->fArg);
		// (rand seeded from the free-running timer, ROM 0x0f181800;
		// host: the system clock)
		{
			Int64 now;
			GetClock(&now);
			srand((unsigned) now.lo);
		}
		SetReply(kInkerReplySize, event);
		return;

	case 7:
		event->fInkedBounds = inker->fInkedBounds;
		{
			UChar mode = inker->GetCurrentPenMode();
			inker->SetCurrentPenMode((UChar) (command - 7));
			event->fCommand = mode + 7;
		}
		SetReply(kInkerBoundsReplySize, event);
		return;

	case 8: case 9: case 10: case 11:
	{
		UChar mode = inker->GetCurrentPenMode();
		inker->SetCurrentPenMode((UChar) (command - 7));
		event->fCommand = mode + 7;
		SetReply(kInkerReplySize, event);
		return;
	}

	case 12: case 13: case 14: case 15: case 16:
	{
		UChar mode = inker->GetNextPenMode();
		inker->SetNextPenMode((UChar) (command - 12));
		event->fCommand = mode + 12;
		SetReply(kInkerReplySize, event);
		return;
	}

	case kInkerGetPenMode:
		event->fCommand = inker->GetCurrentPenMode() + 7;
		SetReply(kInkerReplySize, event);
		return;

	case kInkerGetNextPenMode:
		event->fCommand = inker->GetNextPenMode() + 12;
		SetReply(kInkerReplySize, event);
		return;

	case kInkerGetCalibration:
		GetTabletCalibration(&event->fCalibration);
		SetReply(kInkerCalibrationReplySize, event);
		// the calibration also given the kernel, marked 'G00D, to keep
		// across a warm restart (SetTabletCalibrationData, inline in the ROM)
		SetTabletCalibrationData(event->fCalibration.fXScale, event->fCalibration.fXOffset,
								 event->fCalibration.fYScale, event->fCalibration.fYOffset);
		return;

	case kInkerSetCalibration:
		SetTabletCalibration(event->fCalibration);
		return;

	case kInkerCalibrationNeeded:
		event->fArg = TInker::TestForCalibrationNeeded();
		SetReply(kInkerReplySize, event);
		return;

	case 0x33: case 0x34: case 0x35: case 0x36: case 0x37:
		fBusyBox.DoCommand((long) command);
		return;

	default:
		return;
	}
}


// ROM 0x00217658 AECompletionProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
void
TInkerEventHandler::AECompletionProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{ }


// ROM 0x00218d4c IdleProc__18TInkerEventHandlerFP10TUMsgTokenPUlP7TAEvent
// The pen read again every 50 ms while it is down, until it is up and
// everything it wrote has been read (or the tablet has been bypassed or
// shut down).
void
TInkerEventHandler::IdleProc(TUMsgToken* /*token*/, ULong* /*size*/, TAEvent* /*event*/)
{
	TabletIdle();
	Boolean more = gInkerHostIdleHook != nil && gInkerHostIdleHook();	// (host: a test's queued pen)
	InkThem();
	if (gCalibrate.fArmistice == 0)
	{
		if (!more && gInker->fInkMode == 3 && TabletBufferEmpty())
		{
			long state = GetTabletState();
			if (state == kTabletStateIdle || state == kTabletStateBypassed || state == kTabletStateShutDown)
				return;
		}
	}
	else
		InsertArmisticeSamples();
	StartIdle();
}


// ROM 0x00218dd8 InkThem__18TInkerEventHandlerFv
// What has come from the tablet taken a record at a time and inked, the
// strokes read, and the application woken if a stroke changed.  (The ROM
// writes TInker::LCDEntry's loop out again here.)
void
TInkerEventHandler::InkThem(void)
{
	gInker->LCDEntry();
}


/*------------------------------------------------------------------------------
	T B u s y B o x
------------------------------------------------------------------------------*/

// ROM 0x00218bd4 __ct__8TBusyBoxFv
// A timer on the inker world's queue, and a 32-pixel square map of the
// screen's depth whose bits QDShowBusyBox chooses.
TBusyBox::TBusyBox()
	: TTimerElement(gInker != nil ? gInker->GetTimerQueue() : nil, 0)
{
	fState = 0;
	long depth = 1;
	GetGrafInfo(kGrafInfoDepth, &depth);
	fMap.baseAddr = nil;
	fMap.rowBytes = (short) (depth << 2);
	SetRect(&fMap.bounds, 0, 0, 32, 32);
	fMap.pixMapFlags = (ULong) depth + kPixMapPtr;
	fMap.deviceRes.v = kDefaultDPI;
	fMap.deviceRes.h = kDefaultDPI;
	fMap.grayTable = nil;
}


// ROM 0x00218c70 Timeout__8TBusyBoxFv
void
TBusyBox::Timeout(void)
{
	ShowBusyBox();
}


// ROM 0x00218c74 DoCommand__8TBusyBoxFl
long
TBusyBox::DoCommand(long command)
{
	switch (command - 0x33)
	{
	case 0:
		if (fState != 1)
			QDShowBusyBox(&fMap);
		fState = 1;
		return 1;
	case 1:
		if (fState == 1)
			QDHideBusyBox(&fMap);
		fState = 0;
		return 0;
	case 2:
	case 4:
		HideBusyBox();
		Cancel();				// (the ROM's own test inline: primed and queued)
		return 1;
	case 3:
		HideBusyBox();
		Prime(0x383e70);		// (a second, near enough: 0x383e70 of the 3.6864 MHz clock)
		fState = -1;
		return -1;
	default:
		return command - 0x33;
	}
}


// ROM 0x00218cf4 HideBusyBox__8TBusyBoxFv
void
TBusyBox::HideBusyBox(void)
{
	if (fState == 1)
		QDHideBusyBox(&fMap);
	fState = 0;
}


// ROM 0x00218d20 ShowBusyBox__8TBusyBoxFv
void
TBusyBox::ShowBusyBox(void)
{
	if (fState != 1)
		QDShowBusyBox(&fMap);
	fState = 1;
}


/*------------------------------------------------------------------------------
	T I n k e r
------------------------------------------------------------------------------*/

// ROM 0x00218df0 __ct__6TInkerFv
TInker::TInker()
{
	fHandler = nil;
	fNewtPort = nil;
	fField84 = 0;
	fLastX = fLastY = fCurrentX = fCurrentY = -1;
	fCurrentPenMode = fNextPenMode = 0;
	fPressure = 0;
	fInkMode = 0;
	SetEmptyRect(&fInkedBounds);
	fNewtEventType = 0;
	fNewtEventPad = 0;
}


// ROM 0x0038aadc (unnamed) - the vtable's +0x04
// The size of the copy the inker's task runs on (the ROM's 0x118).
// Without it the task copied only a TAppWorld's worth and the inker's own
// fields lay past the end of its stack block - writing fNewtEventType put
// 'inkr' into whatever came next, now and then a fork's TAppWorldState,
// whose port then pointed nowhere (the one-off crash in TUPort::Receive).
ULong
TInker::GetSizeOf()
{
	return sizeof(TInker);
}


// ROM 0x00218e48 __dt__6TInkerFv
TInker::~TInker()
{ }


// ROM 0x00218f00 MainConstructor__6TInkerFv
// In the inker's task: the message the application is woken with, the
// tablet started (IInker), and the handler for the 'newt/'inkr commands
// with its 50 ms idler, stopped until the pen goes down.
// DEVIATION: the ROM gives the stroke world a heap of its own here
// (NewSegregatedVMHeap, 64K, as gStrokerHeap) and makes it
// (RealStrokeInit); the host makes the stroke world where its tests do
// without an inker (StrokeInit, StrokeQueue.h) and out of the one heap.
long
TInker::MainConstructor()
{
	long err = TAppWorld::MainConstructor();
	if (err != noErr)
		return err;
	gInker = this;
	fNewtMessage.Init(true);
	IInker();
	fHandler = new TInkerEventHandler;
	if (fHandler == nil)
		return kError_No_Memory;
	fHandler->Init(kInkerID, kNewtEventClass);
	fHandler->InitIdler(50, kMilliseconds, 0, true);
	if (gCalibrate.fArmistice == 0)
		fHandler->StopIdle();
	return noErr;
}


// ROM 0x00218ff8 IInker__6TInkerFv
// The tablet started over the screen, the inker's port the one its driver
// wakes; the live ink's tile, the pen modes, the event the application
// is woken with.
void
TInker::IInker(void)
{
	fLiveInker.Init();
	PixelMap screen;
	GetGrafInfo(kGrafInfoScreenPixelMap, &screen);
	TabInitialize(screen.bounds, GetMyPort());
	fNextPenMode = 2;
	fCurrentPenMode = 2;
	fNewtEvent.fAEventClass = kNewtEventClass;
	fNewtEvent.fAEventID = 'idle';
	fNewtEventType = kInkerID;
	fInkMode = 3;
	gCalibrate.fArmistice = 0;
}


// ROM 0x00217814 SetNewtPort__6TInkerFP6TUPort
void
TInker::SetNewtPort(TUPort* port)
{
	fNewtPort = port;
}


// ROM 0x002177f4 GetCurrentPenMode__6TInkerFv
UChar
TInker::GetCurrentPenMode(void)
{
	return fCurrentPenMode;
}


// ROM 0x002177fc GetNextPenMode__6TInkerFv
UChar
TInker::GetNextPenMode(void)
{
	return fNextPenMode;
}


// ROM 0x00217804 SetNextPenMode__6TInkerFUc
void
TInker::SetNextPenMode(UChar mode)
{
	fNextPenMode = mode;
}


// ROM 0x0021780c SetCurrentPenMode__6TInkerFUc
void
TInker::SetCurrentPenMode(UChar mode)
{
	fCurrentPenMode = mode;
}


// (InkThem's: {'newt, 'idle, 'inkr} sent asynchronously to the Newt
// port, so the application's event loop idles the strokes)
void
TInker::SendNewtIdle(void)
{
	if (fNewtPort != nil)
		fNewtPort->Send(&fNewtMessage, &fNewtEvent, sizeof(TAEvent) + 2 * sizeof(ULong), kNoTimeout, nil, 0, true);
}


// ROM 0x00217a64 Convert__6TInkerFv
// The next record in the buffer taken: a pen-down (the stroke takes the
// next pen size), a sample (it becomes the current point), a pen-up (the
// record rewritten with the pen size and the bounds this stroke's live ink
// covered, for the stroke world), or anything else (marked as nothing).
// ==> whether there was one; nothing while the calibration converts.
Boolean
TInker::Convert(void)
{
	if (gCalibrate.fConverting != 0 || InkerBufferEmpty())
		return false;
	ULong word = GetInkerData();
	ULong type = word & 0xf;
	if (type == kTabletPenDown)
	{
		fInkMode = 0;
		fCurrentPenMode = fNextPenMode;
		IncInkerIndex(1);
	}
	else if (type == kTabletPenUp)
	{
		fInkMode = 3;
		SetInkerData(((ULong) fCurrentPenMode << 8) | kTabletPenUp);
		// (each word the top or bottom in its high half and the left or right
		// sign-extended over the whole word, as the ROM's arithmetic shift
		// leaves it: a negative left or right overwrites the other half)
		SetInkerData((ULong32) ((Long32) fInkedBounds.left | ((ULong32) (UShort) fInkedBounds.top << 16)), 2);
		SetInkerData((ULong32) ((Long32) fInkedBounds.right | ((ULong32) (UShort) fInkedBounds.bottom << 16)), 3);
		IncInkerIndex(3);
	}
	else if (type < 8)
	{
		fCurrentY = (Fixed) ((word & 0x3fff0) << 9);
		fCurrentX = (Fixed) ((word & 0xfffc0000) >> 5);
		fPressure = (UChar) type;
		fInkMode = 2;
	}
	else
	{
		SetInkerData(kTabletNoSample);
		fInkMode = 1;
	}
	IncInkerIndex(1);
	return true;
}


// ROM 0x0021765c DrawInk__6TInkerF5PointT1P4Rects
// The segment from the last point to the current one inked, and as many
// of the samples that follow as still fit the live inker's tile (up to
// 80 points, each one read being taken from the buffer and becoming the
// current point) - all of them onto the display in one blit.  inked comes
// back as the extent the points covered.
void
TInker::DrawInk(Point from, Point to, Rect* inked, short /*pressure*/)
{
	Point pen;
	pen.h = fCurrentPenMode;
	pen.v = fCurrentPenMode;
	fLiveInker.ResetAccumulator();
	Point points[81];
	points[0] = from;
	points[1] = to;
	ULong count = 2;
	fLiveInker.AddPoint(from, pen);
	Boolean fits = fLiveInker.AddPoint(to, pen);
	while (fits && !InkerBufferEmpty() && count < 0x50)
	{
		ULong word = GetInkerData();
		if ((word & 0xf) > 7)
			break;
		ULong32 w = (ULong32) word;
		Fixed x = (Fixed) (((w >> 18) << 18) >> 5);
		Fixed y = (Fixed) (((ULong32) (w << 14) >> 14 & ~(ULong32) 0xf) << 9);
		Point pt;
		pt.h = (short) ((Long32) (x + 0x8000) >> 16);
		pt.v = (short) ((Long32) (y + 0x8000) >> 16);
		fits = fLiveInker.AddPoint(pt, pen);
		if (fits)
		{
			IncInkerIndex(1);
			if (points[count - 1].h != pt.h || points[count - 1].v != pt.v)
			{
				points[count++] = pt;
				fCurrentY = y;
				fCurrentX = x;
			}
		}
	}
	*inked = fLiveInker.fExtent;
	fLiveInker.StartLiveInk();
	for (ULong i = 1; i < count; i++)
		fLiveInker.InkLine(points[i - 1], points[i], pen);
	fLiveInker.StopLiveInk();
}


// ROM 0x0021781c LCDEntry__6TInkerFv
// Every record the buffer has taken in turn: a pen-down forgets the last
// point and the ink's bounds (their left and right only - the ROM's); a
// sample is inked from the last point when the pen has a size; a pen-up
// forgets the last point.  After each the strokes are read, the
// application woken whenever one changed.  (Without gWireRecog - which is
// always set - the stroke world's reader would be moved up to the inker's
// after each record, throwing what the inker had just taken away.)
void
TInker::LCDEntry(void)
{
	while (Convert())
	{
		if (fInkMode == 0)
		{
			fLastX = -1;
			fField84 = 0;
			fInkedBounds.right = 0;
			fInkedBounds.left = 0;
		}
		else if (fInkMode == 2)
		{
			if (fCurrentPenMode != 0)
			{
				Point to, from;
				to.h = (short) ((Long32) (fCurrentX + 0x8000) >> 16);
				to.v = (short) ((Long32) (fCurrentY + 0x8000) >> 16);
				from = to;
				if (fLastX != -1)
				{
					from.h = (short) ((Long32) (fLastX + 0x8000) >> 16);
					from.v = (short) ((Long32) (fLastY + 0x8000) >> 16);
				}
				if (from.h != to.h || from.v != to.v || fLastX == -1)
				{
					Rect inked;
					DrawInk(from, to, &inked, fPressure);
					JoinRect(&fInkedBounds, &inked, &fInkedBounds);
				}
			}
			fLastX = fCurrentX;
			fLastY = fCurrentY;
		}
		else if (fInkMode == 3)
		{
			fLastX = -1;
			fField84 = 0;
		}
		if (!gWireRecog)
			FlushInkerBuffer();
		if (fInkMode != 0 && RealStrokeTime() != 0)
			SendNewtIdle();
	}
	if (fInkMode == 0)
		return;
	if (RealStrokeTime() != 0)
		SendNewtIdle();
}


// ROM 0x00218bcc PresCalibrate__6TInkerFv
// (empty in the ROM)
void
TInker::PresCalibrate(void)
{ }


// ROM 0x00218bd0 TestForCalibrationNeeded__6TInkerFv
Boolean
TInker::TestForCalibrationNeeded(void)
{
	return TabletNeedsRecalibration();
}


// ROM 0x00217b90 InsertionSort__6TInkerFPUlUlT2
// value put into its place among the first count values, which are in
// order.
void
TInker::InsertionSort(ULong* values, ULong count, ULong value)
{
	for ( ; (long) count > 0; count--)
	{
		ULong before = values[count - 1];
		values[count] = before;
		if (before <= value)
			break;
	}
	values[count] = value;
}


// ROM 0x00217fb0 InsertArmisticeSamples__Fv
// Samples fed from the armistice buffer (the ROM's at 0x03380004 - a test
// harness's).  NOT YET RECONSTRUCTED: nothing sets gCalibrate.fArmistice,
// so it is never asked.
void
InsertArmisticeSamples(void)
{ }


/*------------------------------------------------------------------------------
	T h e   c a l i b r a t i o n   s c r e e n
------------------------------------------------------------------------------*/

// ROM 0x00217bc4 GetRawPoint__6TInkerFPUlT1sT3Ul
// A target drawn at (h, v) and the panel read, raw, while the pen is held
// on it: twenty readings in a row that agree to within 24 steps each way
// darken the target, and the pen lifting then answers the middle of them
// (a weighted median: half the tenth and a quarter each of its two
// neighbours).  The pen lifting sooner starts the count again.  While the
// pen is away the stylus beside the target blinks, once a second; after
// limit (3.6864 MHz ticks; 0 for none) of that it gives up
// (kInkerCalibrationTimedOut), and the machine going to sleep - the power
// manager's 'ppen, a system event RPC - abandons it
// (kInkerCalibrationCancelled).
long
TInker::GetRawPoint(ULong* x, ULong* y, short h, short v, ULong limit)
{
	ULong count = 0;
	Boolean penUp = true;
	TUMsgToken token;
	ULong waited = 0;
	RefVar stylus(h < 50 ? Rstylusdownbitmap : Rstylusupbitmap);
	Rect stylusBounds;
	FromObject(RefVar(GetFrameSlot(stylus, RSSYMbounds)), stylusBounds);
	long dh, dv;
	if (h < 50)
	{
		dh = h + 14;
		dv = v + 14;
	}
	else
	{
		// (the height taken from h and the width from v: the ROM's)
		dh = h - 14 - (stylusBounds.bottom - stylusBounds.top);
		dv = v - 14 - (stylusBounds.right - stylusBounds.left);
	}
	OffsetRect(&stylusBounds, dh, dv);
	PenNormal();
	PenSize(3, 3);
	MoveTo(h - 1, v - 1);
	Line(-7, -7);
	Line(14, 14);
	Move(0, -14);
	Line(-14, 14);
	PenSize(1, 1);
	PenMode(10);
	MoveTo(h - 2, v);
	Line(4, 0);
	MoveTo(h, v - 2);
	Line(0, 4);
	PenMode(8);
	Rect target;
	SetRect(&target, h - 9, v - 9, h + 10, v + 10);
	if (gInkerCalibrationTargetHook != nil)
		gInkerCalibrationTargetHook(h, v);		// (host: a test's pen)

	ULong xs[20], ys[20];
	for ( ; ; )
	{
		if (count == 0)
		{
			for (ULong i = 0; i < 20; i++)
			{
				ys[i] = 0;
				xs[i] = 0;
			}
		}
		long fx, fy;
		if (PollTablet(&fx, &fy, nil, &penUp) == noErr)
		{
			if (penUp && count < 20)
				count = 0;
			else if (count < 20)
			{
				InsertionSort(xs, count, (ULong) (fx >> 13));
				InsertionSort(ys, count, (ULong) (fy >> 13));
				count++;
				if (count == 20)
				{
					if ((ULong32) (xs[19] - xs[0]) <= 24 && (ULong32) (ys[19] - ys[0]) <= 24)
						InvertRect(&target);
					else
						count = 0;
				}
			}
		}
		else if (GetTabletState() == kTabletStateIdle)
		{
			ULong msgType = 0;
			long err = GetMyPort()->Receive(nil, nil, 0, &token, &msgType, 0x384000, 3, false, false);
			if (err == kError_Message_Timed_Out)
			{
				waited += 0x384000;
				if (limit != 0 && waited > limit)
					return kInkerCalibrationTimedOut;
				DrawPicture(stylus, stylusBounds, 0, 2);
			}
			else if (msgType & 2)
			{
				token.ReplyRPC(nil, 0, 0);
				return kInkerCalibrationCancelled;
			}
			else
				waited = 0;
		}
		Sleep(0x47fe);
		if (count >= 20 && penUp)
		{
			*x = (xs[10] >> 1) + (xs[9] >> 2) + (xs[11] >> 2);
			*y = (ys[10] >> 1) + (ys[9] >> 2) + (ys[11] >> 2);
			return noErr;
		}
	}
}


// one line of the calibration screen's words
static void
DrawCalText(const char* text, StyleRecord* style, Fixed x, Fixed y)
{
	UniChar buffer[100];
	ConvertToUnicode(text, buffer, 1, 0x7fffffff);
	StyleRecord* styles[1] = { style };
	FPoint where;
	where.x = x;
	where.y = y;
	DrawTextOnce(buffer, Ustrlen(buffer), styles, nil, where, nil, nil);
}


// ROM 0x002180a0 Calibrate__6TInkerFUl
// The calibration screen.  The driver is told to hand over its raw
// readings, and the tablet buffer to keep only the last of them; then the
// pen is held on a target near the top left corner and one near the
// bottom right, which gives a scale and an offset each way - the raw axes
// matched with the screen's by how the panel is turned against the screen
// (the driver says how it reads, GetGrafInfo how the screen is) - and on
// a third, a quarter of the way down and three quarters across, which is
// put through them: all three again until that lands within ten pixels of
// its target.  The calibration is then the driver's.  limit is how long
// to wait for the pen at each target (0: for ever).  ==> 0, or the error.
// DEVIATION: the ROM draws in the inker task's own port, the default one
// over the screen; the host keeps one current port for every task, so the
// screen's is made current here and the one before put back.
long
TInker::Calibrate(ULong limit)
{
	if (gCalibrate.fArmistice != 0)
		return kInkerCalibrationTimedOut;
	TSystemEvent powerOff(kSysEvent_PowerOffPending);
	StyleRecord style;
	MakeSimpleStyle(&style, RefVar(Respyfont), 0xa0000, 1);
	powerOff.RegisterForSystemEvent(GetMyPort()->fId, 2, 0);
	ULong panelOrientation;
	SetDoingCalibration(true, &panelOrientation);
	Boolean wasPolling = TBCGetTabletPolling();
	SetTabletPolling(true);
	GrafPort* savedPort;
	GetPort(&savedPort);
	SetPort(&gGrafPort);
	GrafPort* port;
	GetPort(&port);
	FontInfo fontInfo;
	GetStyleFontInfo(&style, &fontInfo);
	Fixed lineHeight = (fontInfo.ascent + fontInfo.descent + fontInfo.leading) << 16;
	ReleaseScreenLock();

	long err;
	Calibration calibration;
	Long32 ex, ey;
	do
	{
		Rect newton;
		FromObject(RefVar(GetFrameSlot(RefVar(Rnewtonnewtbitmap), RSSYMbounds)), newton);
		Point a, b, c, words;
		a.h = port->portRect.left + 10;
		a.v = port->portRect.top + 10;
		b.h = port->portRect.right - 10;
		b.v = port->portRect.bottom - 10;
		words.v = port->portRect.top + ((port->portRect.bottom - port->portRect.top) * 45) / 100;
		words.h = 8;
		OffsetRect(&newton, words.h - 1, words.v - (newton.bottom - newton.top));

		// the first target
		EraseRect(&port->portRect);
		DrawPicture(RefVar(Rnewtonnewtbitmap), newton, 0, 1);
		Fixed x = (Fixed) words.h << 16;
		Fixed y = lineHeight + ((Fixed) words.v << 16);
		DrawCalText(myCalText11, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText12, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText13, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText14, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText15, &style, x, y);
		ULong rawX1, rawY1, rawX2, rawY2, rawX3, rawY3;
		if ((err = GetRawPoint(&rawX1, &rawY1, a.h, a.v, limit)) != noErr)
			break;
		FlushTabletBuffer();

		// the second
		EraseRect(&port->portRect);
		DrawPicture(RefVar(Rnewtonnewtbitmap), newton, 0, 1);
		x = (Fixed) words.h << 16;
		y = lineHeight + ((Fixed) words.v << 16);
		DrawCalText(myCalText21, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText22, &style, x, y);
		if ((err = GetRawPoint(&rawX2, &rawY2, b.h, b.v, limit)) != noErr)
			break;
		FlushTabletBuffer();

		// the scale and offset each way
		Long32 x1 = (Long32) rawX1, y1 = (Long32) rawY1, x2 = (Long32) rawX2, y2 = (Long32) rawY2;
		if (x2 == x1)
			x2++;
		if (y2 == y1)
			y2++;
		long screenOrientation = 0;
		GetGrafInfo(kGrafInfoOrientation, &screenOrientation);
		Long32 turn = (Long32) screenOrientation - (Long32) panelOrientation;
		if (turn < 0)
			turn += 4;
		if (turn == 2 || turn == 3)
		{
			Long32 t = x1; x1 = x2; x2 = t;
		}
		if (turn == 1 || turn == 2)
		{
			Long32 t = y1; y1 = y2; y2 = t;
		}
		if (turn == 1 || turn == 3)
		{
			// the panel's x across the screen's height
			calibration.fXScale = (Long32) ((ULong32) (b.v - a.v) << 16) / (x2 - x1);
			calibration.fYScale = (Long32) ((ULong32) (b.h - a.h) << 16) / (y2 - y1);
			calibration.fXOffset = (Long32) (((ULong32) a.v << 16) - (ULong32) calibration.fXScale * (ULong32) x1);
			calibration.fYOffset = (Long32) (((ULong32) a.h << 16) - (ULong32) calibration.fYScale * (ULong32) y1);
		}
		else
		{
			calibration.fXScale = (Long32) ((ULong32) (b.h - a.h) << 16) / (x2 - x1);
			calibration.fYScale = (Long32) ((ULong32) (b.v - a.v) << 16) / (y2 - y1);
			calibration.fXOffset = (Long32) (((ULong32) a.h << 16) - (ULong32) calibration.fXScale * (ULong32) x1);
			calibration.fYOffset = (Long32) (((ULong32) a.v << 16) - (ULong32) calibration.fYScale * (ULong32) y1);
		}
		calibration.fField10 = 1;
		calibration.fField11 = 1;
		calibration.fPad[0] = calibration.fPad[1] = 0;

		// the third target, to check them by
		EraseRect(&port->portRect);
		DrawPicture(RefVar(Rnewtonnewtbitmap), newton, 0, 1);
		x = (Fixed) words.h << 16;
		y = lineHeight + ((Fixed) words.v << 16);
		DrawCalText(myCalText31, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText32, &style, x, y);		y += lineHeight;
		DrawCalText(myCalText33, &style, x, y);
		a.h = port->portRect.left + 10;
		a.v = port->portRect.top + 10;
		c.v = (short) (a.v + (b.v - a.v) / 4);
		c.h = (short) (a.h + ((b.h - a.h) * 3) / 4);
		if ((err = GetRawPoint(&rawX3, &rawY3, c.h, c.v, limit)) != noErr)
			break;
		FlushTabletBuffer();
		Long32 x3 = (short) ((Long32) ((ULong32) calibration.fXScale * (ULong32) rawX3 + (ULong32) calibration.fXOffset + 0x8000) >> 16);
		Long32 y3 = (short) ((Long32) ((ULong32) rawY3 * (ULong32) calibration.fYScale + (ULong32) calibration.fYOffset + 0x8000) >> 16);
		// where it should have landed, with the panel turned
		if (turn == 1)
		{
			c.h = (short) (a.v + (b.v - a.v) / 4);
			c.v = (short) (a.h + (b.h - a.h) / 4);
		}
		else if (turn == 2)
		{
			c.v = (short) (a.v + ((b.v - a.v) * 3) / 4);
			c.h = (short) (a.h + (b.h - a.h) / 4);
		}
		else if (turn == 3)
		{
			c.h = (short) (a.v + ((b.v - a.v) * 3) / 4);
			c.v = (short) (a.h + ((b.h - a.h) * 3) / 4);
		}
		ex = x3 - c.h;
		if (ex < 0)
			ex = -ex;
		ey = y3 - c.v;
		if (ey < 0)
			ey = -ey;
	} while (ex > 10 || ey > 10);

	EraseRect(&port->portRect);
	powerOff.UnRegisterForSystemEvent(GetMyPort()->fId);
	if (err == noErr)
		SetTabletCalibration(calibration);
	gCalibrate.fConverting = 0;
	SetDoingCalibration(false, &panelOrientation);
	SetTabletPolling(wasPolling);
	if (style.fPattern != nil)
		DisposePattern(style.fPattern);
	SetPort(savedPort);
	return err;
}


/*------------------------------------------------------------------------------
	S t a r t i n g   t h e   i n k e r
------------------------------------------------------------------------------*/

// ROM 0x00218ea0 StartInker__FP6TUPort
// The 'inkr world started (its name registered, so InkerPort finds it),
// the application's port the one it wakes.
// (The ROM makes the world on the stack and the new task takes a copy;
// the host's worlds are heap objects for the life of the machine, as
// InitAlertManager's is.)
void
StartInker(TUPort* newtPort)
{
	TInker* inker = new TInker;
	if (inker == nil)
		return;
	inker->SetNewtPort(newtPort);
	inker->Init(kInkerID, true, 6000);
}
