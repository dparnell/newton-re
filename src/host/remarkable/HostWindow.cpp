/*
	File:		host/remarkable/HostWindow.cpp

	Contains:	The host's "window" on a reMarkable tablet (HostWindow.h):
				the whole e-ink panel, or as much of it as the Newton's
				display fills at a whole-number scale, with the pen as the
				pen, the type folio and AppLoad's keyboard as the keys and
				the power button as the Newton's power switch.  The panel
				is reached through host/remarkable/Panel.h - AppLoad's qtfb
				on the Paper Pro, rmkit on a reMarkable 1 or 2 (and on the
				Paper Pro through AppLoad's qtfb-shim) - docs/host-remarkable.md.

	E-ink is not a monitor: every update costs a refresh of the glass, a
	flashing one to be rid of ghosts.  So where the other windows copy the
	whole display thirty times a second, this one keeps the grays it last
	sent, sends only the rectangle that changed since, and picks the
	waveform for it:

	  - while the pen is down, the fastest (kRefreshInk), so live ink
	    follows the pen - black and white is all ink needs;
	  - otherwise the ordinary gray one (kRefreshUI), no flash;
	  - and once the pen has been up and the screen still for a while
	    (NEWTON_RM_SETTLE ms, 600), whatever was drawn in the fast waveform
	    is sent again in the gray one, so a stroke's grays come back; after
	    enough of the screen has changed (NEWTON_RM_FULL screens' worth,
	    4; 0 never) a full, flashing refresh clears the ghosts.

	The scale: --scale n if it is more than 1, else the largest that fits
	the panel (the Newton's 320 x 480 at four times on the Paper Pro's
	1620 x 2160; --display 540x720 fills it at three).  Touch is ignored
	(the palm rests on the glass) unless NEWTON_RM_TOUCH=pen makes a
	finger the pen.
*/

#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#include <chrono>
#include "HostWindow.h"
#include "Panel.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <signal.h>
#include <algorithm>
#include <vector>

// the pen and the keyboard: C-linkage shims over hal/host/HostTablet.h
// and host/HostKeyboard.h (HostKeyboard.cpp), the Newton headers kept out
extern "C" {
void	HostWindowPenDown(long x, long y);
void	HostWindowPenMove(long x, long y);
void	HostWindowPenUp(void);
void	HostWindowKey(long virtualKey, int down);
void	HostWindowClosed(void);
void	HostWindowThreadStarted(void);
}

#define nil 0

static long					gWidth = 0;			// the display's size
static long					gHeight = 0;
static const unsigned char*	gPixels = nil;
static long					gScale = 1;
static std::thread*			gThread = nil;
static std::atomic<bool>	gPenDown(false);
static std::atomic<bool>	gStopping(false);
static std::atomic<bool>	gStarted(false);
static std::atomic<bool>	gFailed(false);
static RemarkablePanel*		gPanel = nil;
static std::atomic<int>		gSnapshotsAsked(0);	// SIGUSR2: the display written out as a PGM


/*------------------------------------------------------------------------------
	What the window can say about itself, for working on a tablet one cannot
	watch from the desk (docs/host-remarkable.md):

	  kill -USR2 <newton>      the display as it is, written as
	                           $NEWTON_RM_SNAPDIR/panel-N.pgm (default: the
	                           working directory) - what newton handed the panel
	  NEWTON_RM_TRACE=1        each update (waveform, rectangle) and, for each
	                           stroke, how many pen events came, how fast, and
	                           how long from a pen event to the update that
	                           showed its ink went out (newton's share of the
	                           ink's latency; the panel's own comes after)
------------------------------------------------------------------------------*/

static void
SnapshotSignal(int)
{
	gSnapshotsAsked.fetch_add(1);
}

static void
WriteSnapshot(const unsigned char* pixels)
{
	static int sNumber = 0;
	const char* dir = getenv("NEWTON_RM_SNAPDIR");
	char path[512];
	snprintf(path, sizeof(path), "%s/panel-%d.pgm", dir != nil ? dir : ".", ++sNumber);
	FILE* f = fopen(path, "wb");
	if (f == nil)
		return;
	fprintf(f, "P5\n%ld %ld\n255\n", gWidth, gHeight);
	for (long i = 0; i < gWidth * gHeight; i++)
		fputc(255 - pixels[i], f);			// (the display's 0 is white; a PGM's 255 is)
	fclose(f);
	fprintf(stderr, "[host] reMarkable: wrote %s\n", path);
}

typedef std::chrono::steady_clock Clock;

static double
Ms(Clock::duration d)
{
	return std::chrono::duration<double, std::milli>(d).count();
}

struct StrokeTrace
{
	bool				fOn = false;
	Clock::time_point	fDown, fPending;
	bool				fHasPending = false;
	long				fEvents = 0, fUpdates = 0;
	std::vector<double>	fLatency;

	void	PenEvent(bool down)
	{
		if (!fOn) return;
		Clock::time_point now = Clock::now();
		if (down) { fDown = now; fEvents = 0; fUpdates = 0; fLatency.clear(); }
		fEvents++;
		if (!fHasPending) { fPending = now; fHasPending = true; }
	}
	void	Updated(const char* how, long l, long t, long r, long b)
	{
		if (!fOn) return;
		Clock::time_point now = Clock::now();
		if (fHasPending)
		{
			double ms = Ms(now - fPending);
			fLatency.push_back(ms);
			fUpdates++;
			fHasPending = false;
			fprintf(stderr, "[rm] update %s %ld,%ld %ldx%ld, %.1f ms after the pen\n", how, l, t, r - l, b - t, ms);
		}
		else
			fprintf(stderr, "[rm] update %s %ld,%ld %ldx%ld\n", how, l, t, r - l, b - t);
	}
	void	PenUp(void)
	{
		if (!fOn) return;
		double seconds = Ms(Clock::now() - fDown) / 1000.0;
		std::vector<double> v = fLatency;
		std::sort(v.begin(), v.end());
		fprintf(stderr, "[rm] stroke: %.2f s, %ld pen events (%.0f a second), %ld ink updates; pen to update %s",
				seconds, fEvents, seconds > 0 ? fEvents / seconds : 0.0, fUpdates, v.empty() ? "-\n" : "");
		if (!v.empty())
			fprintf(stderr, "min %.1f median %.1f max %.1f ms\n", v.front(), v[v.size() / 2], v.back());
		fHasPending = false;
	}
};


static long
EnvLong(const char* name, long otherwise)
{
	const char* value = getenv(name);
	return value != nil && *value != 0 ? strtol(value, nil, 0) : otherwise;
}


// the display's gray (0 white .. 255 black) as an RGB565 gray
static inline uint16_t
PanelGray(unsigned char level)
{
	unsigned l = 255u - level;
	return (uint16_t) (((l >> 3) << 11) | ((l >> 2) << 5) | (l >> 3));
}


// the display's rectangle [left, right) x [top, bottom) into the panel's
// image, each pixel scale by scale
static void
PaintRect(const unsigned char* pixels, long left, long top, long right, long bottom)
{
	uint16_t* image = gPanel->Pixels();
	long rowWords = gPanel->RowWords();
	for (long y = top; y < bottom; y++)
	{
		const unsigned char* row = pixels + y * gWidth;
		uint16_t* out = image + (y * gScale) * rowWords;
		for (long x = left; x < right; x++)
		{
			uint16_t pixel = PanelGray(row[x]);
			uint16_t* at = out + x * gScale;
			for (long i = 0; i < gScale; i++)
				at[i] = pixel;
		}
		for (long i = 1; i < gScale; i++)
			memcpy(out + i * rowWords + left * gScale, out + left * gScale, (size_t) ((right - left) * gScale) * sizeof(uint16_t));
	}
}


// What changed since the grays last sent (shown): its rectangle, the
// shown copy brought up to date.  ==> false if nothing did
static bool
ChangedRect(const unsigned char* pixels, unsigned char* shown, long* left, long* top, long* right, long* bottom)
{
	long t = -1, b = -1, l = gWidth, r = 0;
	for (long y = 0; y < gHeight; y++)
	{
		const unsigned char* row = pixels + y * gWidth;
		unsigned char* was = shown + y * gWidth;
		if (memcmp(row, was, (size_t) gWidth) == 0)
			continue;
		if (t < 0)
			t = y;
		b = y + 1;
		long x0 = 0, x1 = gWidth;
		while (row[x0] == was[x0])
			x0++;
		while (row[x1 - 1] == was[x1 - 1])
			x1--;
		if (x0 < l)
			l = x0;
		if (x1 > r)
			r = x1;
		memcpy(was, row, (size_t) gWidth);
	}
	if (t < 0)
		return false;
	*left = l;
	*top = t;
	*right = r;
	*bottom = b;
	return true;
}


static void
Union(long* l, long* t, long* r, long* b, long left, long top, long right, long bottom)
{
	if (*r <= *l)
	{
		*l = left; *t = top; *r = right; *b = bottom;
		return;
	}
	if (left < *l) *l = left;
	if (top < *t) *t = top;
	if (right > *r) *r = right;
	if (bottom > *b) *b = bottom;
}


static void
WindowThread(void)
{
	// this thread is none of the machine's: it must never make a Newton
	// system call (host/HostKeyboard.cpp, kernel/host/TaskRuntime.h)
	HostWindowThreadStarted();

	long imageWidth = gWidth * gScale, imageHeight = gHeight * gScale;
	if (!gPanel->Open(imageWidth, imageHeight))
	{
		delete gPanel;
		gPanel = nil;
		gFailed.store(true);
		gStarted.store(true);
		return;
	}
	long originX, originY;
	gPanel->Origin(&originX, &originY);
	fprintf(stderr, "[host] reMarkable: the display at %ld x on %s, %ld x %ld at %ld,%ld\n",
			gScale, gPanel->Name(), imageWidth, imageHeight, originX, originY);
	gStarted.store(true);

	const long settleMs = EnvLong("NEWTON_RM_SETTLE", 600);
	const long fullScreens = EnvLong("NEWTON_RM_FULL", 4);
	const bool touchIsPen = getenv("NEWTON_RM_TOUCH") != nil && strcmp(getenv("NEWTON_RM_TOUCH"), "pen") == 0;
	const long frameMs = EnvLong("NEWTON_RM_FRAME", 33);		// the pace with the pen up
	const long inkFrameMs = EnvLong("NEWTON_RM_INK_FRAME", 8);	// and down: live ink is drawn as fast as it can be sent
	StrokeTrace trace;
	trace.fOn = getenv("NEWTON_RM_TRACE") != nil;
	static const char* kHow[] = { "ink", "ui", "content" };

	// the grays last sent: all of them, first, in the clean waveform
	unsigned char* shown = (unsigned char*) malloc((size_t) (gWidth * gHeight));
	memset(shown, 0, (size_t) (gWidth * gHeight));
	if (gPixels != nil)
		memcpy(shown, gPixels, (size_t) (gWidth * gHeight));
	PaintRect(shown, 0, 0, gWidth, gHeight);
	gPanel->Update(0, 0, imageWidth, imageHeight, kRefreshContent);
	long inkL = 0, inkT = 0, inkR = 0, inkB = 0;		// what went out in the fast waveform since the last settle
	double changedScreens = 0;							// how much has changed since the last full refresh
	auto lastChange = std::chrono::steady_clock::now();
	long touchId = -1;
	bool closed = false;

	while (!gStopping.load() && !closed)
	{
		const unsigned char* pixels = gPixels;
		long l, t, r, b;
		while (gSnapshotsAsked.load() > 0 && pixels != nil)
		{
			gSnapshotsAsked.fetch_sub(1);
			WriteSnapshot(pixels);
		}
		if (pixels != nil && ChangedRect(pixels, shown, &l, &t, &r, &b))
		{
			PaintRect(pixels, l, t, r, b);
			RemarkableRefresh how = gPenDown.load() ? kRefreshInk : kRefreshUI;
			gPanel->Update(l * gScale, t * gScale, r * gScale, b * gScale, how);
			trace.Updated(kHow[how], l, t, r, b);
			if (how == kRefreshInk)
				Union(&inkL, &inkT, &inkR, &inkB, l, t, r, b);
			changedScreens += (double) ((r - l) * (b - t)) / (double) (gWidth * gHeight);
			lastChange = std::chrono::steady_clock::now();
		}
		else if (!gPenDown.load()
			  && std::chrono::steady_clock::now() - lastChange > std::chrono::milliseconds(settleMs))
		{
			// the screen is still: its grays back where the pen went fast,
			// and now and then the ghosts cleared
			if (fullScreens > 0 && changedScreens >= (double) fullScreens)
			{
				gPanel->Update(0, 0, imageWidth, imageHeight, kRefreshContent);
				changedScreens = 0;
				inkR = inkL;
			}
			else if (inkR > inkL)
			{
				gPanel->Update(inkL * gScale, inkT * gScale, inkR * gScale, inkB * gScale, kRefreshUI);
				inkR = inkL;
			}
		}

		// the pen, the keys: one event, then whatever else is waiting
		// (the wait is the refresh's pace, thirty times a second)
		RemarkableEvent event;
		long wait = gPenDown.load() ? inkFrameMs : frameMs;
		while (gPanel->Poll(&event, wait))
		{
			wait = 0;
			long x = (event.x - originX) / gScale, y = (event.y - originY) / gScale;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= gWidth) x = gWidth - 1;
			if (y >= gHeight) y = gHeight - 1;
			if (touchIsPen)
			{
				// a finger as the pen: the first touch down, until it lifts
				if (event.kind == RemarkableEvent::kTouchDown && touchId < 0 && !gPenDown.load())
					{ touchId = 1; event.kind = RemarkableEvent::kPenDown; }
				else if (event.kind == RemarkableEvent::kTouchMove && touchId >= 0)
					event.kind = RemarkableEvent::kPenMove;
				else if (event.kind == RemarkableEvent::kTouchUp && touchId >= 0)
					{ touchId = -1; event.kind = RemarkableEvent::kPenUp; }
			}
			switch (event.kind)
			{
			case RemarkableEvent::kPenDown:
				if (!gPenDown.load())
				{
					gPenDown.store(true);
					trace.PenEvent(true);
					HostWindowPenDown(x, y);
				}
				break;
			case RemarkableEvent::kPenMove:
				if (gPenDown.load())
				{
					trace.PenEvent(false);
					HostWindowPenMove(x, y);
				}
				break;
			case RemarkableEvent::kPenUp:
				if (gPenDown.load())
				{
					HostWindowPenUp();
					gPenDown.store(false);
					trace.PenUp();
				}
				break;
			case RemarkableEvent::kKeyDown:
			case RemarkableEvent::kKeyUp:
				if (event.key >= 0)
					HostWindowKey(event.key, event.kind == RemarkableEvent::kKeyDown);
				break;
			case RemarkableEvent::kClosed:
				closed = true;
				break;
			default:
				break;
			}
		}
	}
	free(shown);
	if (closed)
		HostWindowClosed();				// AppLoad closed it: the run ends, as a window's close button ends it
}


bool
HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale)
{
	(void) title;
	// which panel: NEWTON_RM_PANEL_KIND=qtfb or rmkit, else qtfb when AppLoad
	// started newton (QTFB_KEY), else rmkit if this newton has it
	const char* kind = getenv("NEWTON_RM_PANEL_KIND");
	RemarkablePanel* candidates[2] = { nil, nil };
	if (kind != nil && strcmp(kind, "rmkit") == 0)
		candidates[0] = NewRMKitPanel();
	else if (kind != nil && strcmp(kind, "qtfb") == 0)
		candidates[0] = NewQTFBPanel();
	else
	{
		candidates[0] = NewQTFBPanel();
		candidates[1] = NewRMKitPanel();
	}
	long panelWidth = 0, panelHeight = 0;
	gPanel = nil;
	for (int i = 0; i < 2; i++)
	{
		RemarkablePanel* p = candidates[i];
		if (p == nil)
			continue;
		if (gPanel == nil && p->NativeSize(&panelWidth, &panelHeight))
			gPanel = p;
		else
			delete p;
	}
	if (gPanel == nil)
		return false;					// no panel here: the world runs headless
	gWidth = width;
	gHeight = height;
	gPixels = pixels;
	gScale = scale;
	if (gScale <= 1)
	{
		gScale = 1;
		while (width * (gScale + 1) <= panelWidth && height * (gScale + 1) <= panelHeight)
			gScale++;
	}
	if (width * gScale > panelWidth || height * gScale > panelHeight)
		fprintf(stderr, "[host] reMarkable: %ld x %ld at %ld x is more than the panel's %ld x %ld\n",
				width, height, gScale, panelWidth, panelHeight);
	signal(SIGUSR2, SnapshotSignal);
	gStopping.store(false);
	gStarted.store(false);
	gFailed.store(false);
	gThread = new std::thread(WindowThread);
	while (!gStarted.load())
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	if (gFailed.load())
	{
		gThread->join();
		delete gThread;
		gThread = nil;
		return false;
	}
	return true;
}


// (the shims called as the event loop would call them)
void
HostWindowPostPen(long x, long y, int what)
{
	if (what == 0)
	{
		gPenDown.store(true);
		HostWindowPenDown(x, y);
	}
	else if (what == 1)
		HostWindowPenMove(x, y);
	else
	{
		gPenDown.store(false);
		HostWindowPenUp();
	}
}


// a panel has no position to keep across a restart
bool
HostWindowPosition(long* x, long* y)
{
	(void) x;
	(void) y;
	return false;
}


void
HostWindowSetPosition(long x, long y)
{
	(void) x;
	(void) y;
}


void
HostWindowStop(void)
{
	gStopping.store(true);
	if (gThread != nil)
	{
		gThread->join();
		delete gThread;
		gThread = nil;
	}
	if (gPanel != nil)
	{
		gPanel->Close();
		delete gPanel;
		gPanel = nil;
	}
}
