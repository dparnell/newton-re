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
#include "PanelTurn.h"
#include "Folio.h"
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
void	HostWindowPenDownFine(long x8, long y8);		// eighths of a display pixel
void	HostWindowPenMoveFine(long x8, long y8);
void	HostWindowKey(long virtualKey, int down);
void	HostWindowClosed(void);
void	HostWindowThreadStarted(void);
void	HostWindowDeviceRotation(long rotation);		// host/HostOrientation.h
void	HostWindowDisplayShape(long* width, long* height, long* orientation);
}

#define nil 0

static long					gWidth = 0;			// the display's size, as it is turned now
static long					gHeight = 0;
static PanelTurn			gTurn = { 0, 0, 0, 1 };	// how it goes onto the panel's image (PanelTurn.h)
static const unsigned char*	gPixels = nil;
static long					gScale = 1;
static std::thread*			gThread = nil;
static std::atomic<bool>	gPenDown(false);
static std::atomic<bool>	gStopping(false);
static std::atomic<bool>	gStarted(false);
static std::atomic<bool>	gFailed(false);
static RemarkablePanel*		gPanel = nil;
static std::atomic<int>		gSnapshotsAsked(0);	// SIGUSR2: the display written out as a PGM

// the settings the Host preferences panel changes while newton runs
// (HostWindowSetOption): a finger as the pen (NEWTON_RM_TOUCH=off at
// start: not), the ink's waveform, the Marker read directly, and a
// flashing redraw of everything asked for
static std::atomic<bool>	gTouchIsPen(getenv("NEWTON_RM_TOUCH") == nil || strcmp(getenv("NEWTON_RM_TOUCH"), "off") != 0);
// the ink's waveform: 0 fast (the default), 1 the pen's (qtfb ufast: quicker,
// more ghosting), 2 gray (the UI waveform for everything: slowest, cleanest);
// -1 until the window has started (NEWTON_RM_WAVEFORM, NEWTON_RM_INK_MODE)
static std::atomic<int>		gWaveform(-1);
static std::atomic<int>		gDirectPenAsked(-1);	// -1 nothing asked, else 0/1 for the window's thread to give the panel
static long					gStartScale = 0;		// the scale asked for at start (the Host panel's Screen size, kept beside the store): the display the panel's size over it
static std::atomic<bool>	gClearAsked(false);


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

// AppLoad ends an application with SIGTERM (and rmkit's own handler for it
// calls exit() inside the signal, which aborted newton): the run is ended
// as a closed window ends it, so the stores are flushed
static std::atomic<bool>	gTerminateAsked(false);

static void
TerminateSignal(int)
{
	gTerminateAsked.store(true);
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

// What went to the panel, counted for NEWTON_RM_TRACE's minute-by-minute
// summary: how many updates of each kind, how many covered (nearly) the
// whole display, how often the waveform asked for changed, how many
// flashing full refreshes, and how many updates the pen overlay sent.
struct PanelCounts
{
	long	fUpdates[3] = { 0, 0, 0 };	// by RemarkableRefresh
	long	fWholeScreen = 0;
	long	fModeChanges = 0;
	long	fFullRefreshes = 0;
	long	fOverlay = 0;
	long	fStrokes = 0;
	int		fLastMode = -1;

	void	Count(int how, long area, long whole)
	{
		fUpdates[how]++;
		if (area * 10 >= whole * 9)
			fWholeScreen++;
		if (how == 2 && area * 10 >= whole * 9)
			fFullRefreshes++;
		if (how != fLastMode)
		{
			if (fLastMode >= 0)
				fModeChanges++;
			fLastMode = how;
		}
	}
	void	Report(double seconds)
	{
		fprintf(stderr, "[rm] %.0f s: updates ink %ld ui %ld content %ld, whole-screen %ld, waveform changes %ld, full refreshes %ld, pen overlay %ld, strokes %ld\n",
				seconds, fUpdates[0], fUpdates[1], fUpdates[2], fWholeScreen, fModeChanges, fFullRefreshes, fOverlay, fStrokes);
	}
};

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
// image, each pixel scale by scale, turned as gTurn says
static void
PaintRect(const unsigned char* pixels, long left, long top, long right, long bottom)
{
	uint16_t* image = gPanel->Pixels();
	long rowWords = gPanel->RowWords();
	if (gTurn.quarters != 0)
	{
		for (long y = top; y < bottom; y++)
		{
			const unsigned char* row = pixels + y * gWidth;
			for (long x = left; x < right; x++)
			{
				uint16_t pixel = PanelGray(row[x]);
				long X, Y;
				gTurn.Pixel(x, y, &X, &Y);
				for (long j = 0; j < gScale; j++)
					for (long i = 0; i < gScale; i++)
						image[(Y + j) * rowWords + X + i] = pixel;
			}
		}
		return;
	}
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


// The pen overlay's line: from (x0, y0) to (x1, y1) in the image's pixels,
// black, scale pixels wide (one display pixel - the Newton's own ink),
// straight into the panel's image.  ==> its rectangle, false if off it.
static bool
DrawPanelLine(long x0, long y0, long x1, long y1, long width, long imageWidth, long imageHeight,
			  long* left, long* top, long* right, long* bottom)
{
	uint16_t* image = gPanel->Pixels();
	long rowWords = gPanel->RowWords();
	long dx = labs(x1 - x0), dy = labs(y1 - y0);
	long sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
	long err = dx - dy;
	long l = imageWidth, t = imageHeight, r = 0, b = 0;
	long x = x0, y = y0;
	for (;;)
	{
		for (long j = 0; j < width; j++)
			for (long i = 0; i < width; i++)
			{
				long px = x - width / 2 + i, py = y - width / 2 + j;
				if (px < 0 || py < 0 || px >= imageWidth || py >= imageHeight)
					continue;
				image[py * rowWords + px] = 0;
				if (px < l) l = px;
				if (py < t) t = py;
				if (px + 1 > r) r = px + 1;
				if (py + 1 > b) b = py + 1;
			}
		if (x == x1 && y == y1)
			break;
		long e2 = 2 * err;
		if (e2 > -dy) { err -= dy; x += sx; }
		if (e2 < dx) { err += dx; y += sy; }
	}
	if (r <= l || b <= t)
		return false;
	*left = l; *top = t; *right = r; *bottom = b;
	return true;
}


// The turn for the display's shape now and the way the device is held
// (gRotation: AppLoad's).  ==> whether it changed since the last time
static std::atomic<long>	gRotation(0);
static long					gTurnRotation = -1, gTurnOrientation = -1;

static bool
UpdateTurn(long imageWidth, long imageHeight, bool flip)
{
	long width = 0, height = 0, orientation = 0;
	HostWindowDisplayShape(&width, &height, &orientation);
	if (width <= 0 || height <= 0)
		{ width = gWidth; height = gHeight; }		// (no display yet: as it was given)
	long rotation = gRotation.load();
	if (width == gWidth && height == gHeight && rotation == gTurnRotation && orientation == gTurnOrientation)
		return false;
	PanelTurn turn = { PanelTurnQuarters(rotation, width > height, imageWidth > imageHeight, orientation, flip), width, height, gScale };
	if (turn.ImageWidth() != imageWidth || turn.ImageHeight() != imageHeight)
		turn.quarters = (turn.quarters + 1) & 3;	// (a display that started landscape: whichever way fits)
	if (turn.ImageWidth() != imageWidth || turn.ImageHeight() != imageHeight)
		return false;								// (the buffer being remade: not yet)
	gWidth = width;
	gHeight = height;
	gTurn = turn;
	gTurnRotation = rotation;
	gTurnOrientation = orientation;
	fprintf(stderr, "[host] reMarkable: the display %ld x %ld (orientation %ld), the device held %ld: turned %ld quarters\n",
			width, height, orientation, rotation, turn.quarters);
	return true;
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

	// The waveforms (docs/host-remarkable.md, "Lag and full-screen
	// redraws"): NEWTON_RM_WAVEFORM=fast (the default) sends everything in
	// the fast black-and-white waveform and never changes it - AppLoad sets
	// the waveform on its whole window, so each change can redraw all of
	// it; =ui everything in the gray one; =switch the first version's way,
	// fast with the pen down and gray otherwise, the fast areas sent again
	// in gray once the pen has rested (NEWTON_RM_SETTLE ms).  A flashing
	// full refresh only every NEWTON_RM_FULL screens' worth of change (0, the
	// default: never - AppLoad's five-finger tap does one by hand).
	// (the Host preferences panel's "Ink waveform" changes it while newton
	// runs: gWaveform - fast, pen or gray; switch stays an environment
	// setting)
	const char* waveform = getenv("NEWTON_RM_WAVEFORM");
	enum { kWaveFast, kWaveUI, kWaveSwitch } policy = kWaveFast;
	if (waveform != nil && strcmp(waveform, "ui") == 0)
		policy = kWaveUI;
	else if (waveform != nil && strcmp(waveform, "switch") == 0)
		policy = kWaveSwitch;
	int waveformShown = policy == kWaveUI ? 2 : gPanel->PenInk() ? 1 : 0;
	gWaveform.store(waveformShown);
	const long settleMs = EnvLong("NEWTON_RM_SETTLE", 600);
	const long fullScreens = EnvLong("NEWTON_RM_FULL", 0);
	// the pen overlay (NEWTON_RM_PEN_OVERLAY, on by default): the pen's
	// line drawn on the panel by the window itself, from the pen events, the
	// moment they come - the Newton's own live ink follows 50 ms behind (the
	// ROM's inker draws on a 50 ms idler) and replaces it; what the Newton
	// did not ink (a drag on a button) is put back from its display once the
	// pen has been up NEWTON_RM_OVERLAY_HOLD ms
	const bool overlay = EnvLong("NEWTON_RM_PEN_OVERLAY", 1) != 0;
	// NEWTON_RM_PENLOG=<file>: every pen event the Newton is given, as
	// "milliseconds what x y" in display pixels (what 0 down, 1 move, 2 up),
	// for host/HostPenReplay.h to play back on another host
	FILE* penLog = getenv("NEWTON_RM_PENLOG") != nil ? fopen(getenv("NEWTON_RM_PENLOG"), "w") : nil;
	const bool wholePixels = EnvLong("NEWTON_RM_PEN_WHOLE", 0) != 0;
	const long overlayHoldMs = EnvLong("NEWTON_RM_OVERLAY_HOLD", 400);
	// touch (NEWTON_RM_TOUCH: on by default, =off to ignore it): one finger
	// is the pen, a tap or a drag, as on a MessagePad's resistive glass.
	// The hand rests on the glass while the Marker writes, so a touch counts
	// only with the Marker away - not down, and not within
	// NEWTON_RM_TOUCH_HOLDOFF ms (800) of its last event - and a second
	// finger ends the first one's stroke and is ignored until every finger
	// is off the glass (a palm, or two fingers that were not meant as a tap)
	// (whether a touch counts at all is gTouchIsPen, which the Host
	// preferences panel changes while newton runs)
	const long touchHoldoffMs = EnvLong("NEWTON_RM_TOUCH_HOLDOFF", 800);
	// NEWTON_RM_TURN_FLIP=1: the sideways turns the other way round
	// (PanelTurn.h), should the picture come out upside down in landscape
	const bool turnFlip = EnvLong("NEWTON_RM_TURN_FLIP", 0) != 0;
	const long frameMs = EnvLong("NEWTON_RM_FRAME", 33);		// the pace with the pen up
	const long inkFrameMs = EnvLong("NEWTON_RM_INK_FRAME", 8);	// and down: live ink is drawn as fast as it can be sent
	StrokeTrace trace;
	trace.fOn = getenv("NEWTON_RM_TRACE") != nil;
	PanelCounts counts;
	auto started = std::chrono::steady_clock::now();
	auto lastReport = started;
	auto send = [&](long left, long top, long right, long bottom, RemarkableRefresh how)
	{
		gPanel->Update(left, top, right, bottom, how);
		counts.Count(how, (right - left) * (bottom - top), imageWidth * imageHeight);
	};
	// a rectangle of the display, sent where it is on the panel
	auto sendDisplay = [&](long left, long top, long right, long bottom, RemarkableRefresh how)
	{
		long L, T, R, B;
		gTurn.Rect(left, top, right, bottom, &L, &T, &R, &B);
		send(L, T, R, B, how);
	};
	// the overlay's state: the last point drawn (panel pixels), whether the
	// stroke has moved far enough to draw, what it has drawn, when to undo it
	long penX = 0, penY = 0, penDownX = 0, penDownY = 0;
	bool penDrawing = false;
	long ovL = 0, ovT = 0, ovR = 0, ovB = 0;	// display pixels
	auto ovRestoreAt = started;
	bool ovPending = false;
	static const char* kHow[] = { "ink", "ui", "content" };

	// the grays last sent: all of them, first, in the clean waveform
	gTurn = { 0, gWidth, gHeight, gScale };
	UpdateTurn(imageWidth, imageHeight, turnFlip);
	const size_t displayBytes = (size_t) (gWidth * gHeight);	// (the same turned either way)
	unsigned char* shown = (unsigned char*) malloc(displayBytes);
	memset(shown, 0, displayBytes);
	if (gPixels != nil)
		memcpy(shown, gPixels, displayBytes);
	PaintRect(shown, 0, 0, gWidth, gHeight);
	send(0, 0, imageWidth, imageHeight, kRefreshContent);
	long fingers = 0;						// how many touches are on the glass
	bool fingersRejected = false;			// a second finger came: none is the pen until all are off
	auto penLast = started - std::chrono::hours(1);	// the Marker's last event (the touch's hold-off)
	long inkL = 0, inkT = 0, inkR = 0, inkB = 0;		// what went out in the fast waveform since the last settle
	double changedScreens = 0;							// how much has changed since the last full refresh
	auto lastChange = std::chrono::steady_clock::now();
	long touchId = -1;
	bool closed = false;

	// (after the panel is open: rmkit installs its handlers before main)
	signal(SIGTERM, TerminateSignal);
	signal(SIGINT, TerminateSignal);
	while (!gStopping.load() && !closed)
	{
		if (gTerminateAsked.load())
		{
			fprintf(stderr, "[host] reMarkable: asked to stop\n");
			closed = true;
			break;
		}
		const unsigned char* pixels = gPixels;
		long l, t, r, b;
		int wave = gWaveform.load();
		if (wave != waveformShown)
		{
			// the ink's waveform chosen on the Host panel
			waveformShown = wave;
			policy = wave == 2 ? kWaveUI : kWaveFast;
			gPanel->SetPenInk(wave == 1);
			fprintf(stderr, "[host] reMarkable: the ink in the %s waveform\n", wave == 2 ? "gray" : wave == 1 ? "pen" : "fast");
		}
		int direct = gDirectPenAsked.exchange(-1);
		if (direct >= 0)
			gPanel->SetDirectPen(direct != 0);
		if (gClearAsked.exchange(false))
		{
			send(0, 0, imageWidth, imageHeight, kRefreshContent);		// (the ghosts cleared, as AppLoad's five-finger tap does)
			changedScreens = 0;
		}
		if (UpdateTurn(imageWidth, imageHeight, turnFlip))
		{
			// the display turned, or the device: all of it again, in the
			// clean waveform
			if (gPenDown.load())
			{
				HostWindowPenUp();
				gPenDown.store(false);
			}
			touchId = -1;
			ovPending = false;
			ovR = ovL;
			inkR = inkL;
			if (pixels != nil)
				memcpy(shown, pixels, displayBytes);
			PaintRect(shown, 0, 0, gWidth, gHeight);
			send(0, 0, imageWidth, imageHeight, kRefreshContent);
			changedScreens = 0;
		}
		if (ovPending && !gPenDown.load() && std::chrono::steady_clock::now() >= ovRestoreAt && pixels != nil)
		{
			// the overlay taken back: what the Newton's display holds there now
			if (ovL < 0) ovL = 0;
			if (ovT < 0) ovT = 0;
			if (ovR > gWidth) ovR = gWidth;
			if (ovB > gHeight) ovB = gHeight;
			if (ovR > ovL && ovB > ovT)
			{
				for (long y = ovT; y < ovB; y++)
					memcpy(shown + y * gWidth + ovL, pixels + y * gWidth + ovL, (size_t) (ovR - ovL));
				PaintRect(pixels, ovL, ovT, ovR, ovB);
				sendDisplay(ovL, ovT, ovR, ovB, policy == kWaveUI ? kRefreshUI : kRefreshInk);
			}
			ovPending = false;
			ovR = ovL;
		}
		if (trace.fOn && std::chrono::steady_clock::now() - lastReport > std::chrono::seconds(60))
		{
			counts.Report(Ms(std::chrono::steady_clock::now() - started) / 1000.0);
			lastReport = std::chrono::steady_clock::now();
		}
		while (gSnapshotsAsked.load() > 0 && pixels != nil)
		{
			gSnapshotsAsked.fetch_sub(1);
			WriteSnapshot(pixels);
		}
		if (pixels != nil && ChangedRect(pixels, shown, &l, &t, &r, &b))
		{
			PaintRect(pixels, l, t, r, b);
			RemarkableRefresh how = policy == kWaveFast ? kRefreshInk : policy == kWaveUI ? kRefreshUI
								  : gPenDown.load() ? kRefreshInk : kRefreshUI;
			sendDisplay(l, t, r, b, how);
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
				send(0, 0, imageWidth, imageHeight, kRefreshContent);
				changedScreens = 0;
				inkR = inkL;
			}
			else if (inkR > inkL && policy == kWaveSwitch)
			{
				sendDisplay(inkL, inkT, inkR, inkB, kRefreshUI);
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
			// the pen to an eighth of a display pixel: at 2x the panel has
			// twice the display's pixels and the Marker several readings to
			// each, which a whole display pixel would throw away (the
			// MessagePad's resistive tablet read about eight to the pixel);
			// NEWTON_RM_PEN_WHOLE=1 gives the Newton whole pixels only.  The
			// point is the image's, turned back to the display's (PanelTurn.h)
			long x8, y8;
			gTurn.Point8(event.x - originX, event.y - originY, &x8, &y8);
			long x = x8 >= 0 ? x8 / 8 : -1, y = y8 >= 0 ? y8 / 8 : -1;
			if (wholePixels) { x8 = x * 8; y8 = y * 8; }
			if (x8 < 0) x8 = 0;
			if (y8 < 0) y8 = 0;
			if (x8 > gWidth * 8 - 1) x8 = gWidth * 8 - 1;
			if (y8 > gHeight * 8 - 1) y8 = gHeight * 8 - 1;
			if (x < 0) x = 0;
			if (y < 0) y = 0;
			if (x >= gWidth) x = gWidth - 1;
			if (y >= gHeight) y = gHeight - 1;
			if (event.kind == RemarkableEvent::kPenDown || event.kind == RemarkableEvent::kPenMove
			 || event.kind == RemarkableEvent::kPenUp)
			{
				penLast = std::chrono::steady_clock::now();
				if (touchId >= 0)
				{
					// the Marker came while a finger was the pen: the Marker it is
					touchId = -1;
					fingersRejected = true;
					if (gPenDown.load())
					{
						HostWindowPenUp();
						gPenDown.store(false);
					}
				}
			}
			else if (event.kind == RemarkableEvent::kTouchDown)
			{
				fingers++;
				bool markerNear = std::chrono::steady_clock::now() - penLast < std::chrono::milliseconds(touchHoldoffMs);
				if (touchId >= 0 && event.id != touchId)
				{
					// a second finger: the first one's stroke ended where it was
					touchId = -1;
					fingersRejected = true;
					event.kind = RemarkableEvent::kPenUp;
				}
				else if (gTouchIsPen.load() && touchId < 0 && fingers == 1 && !fingersRejected && !gPenDown.load() && !markerNear)
				{
					touchId = event.id;
					event.kind = RemarkableEvent::kPenDown;
				}
			}
			else if (event.kind == RemarkableEvent::kTouchMove)
			{
				if (touchId >= 0 && event.id == touchId)
					event.kind = RemarkableEvent::kPenMove;
			}
			else if (event.kind == RemarkableEvent::kTouchUp)
			{
				if (fingers > 0)
					fingers--;
				if (fingers == 0)
					fingersRejected = false;
				if (touchId >= 0 && event.id == touchId)
				{
					touchId = -1;
					event.kind = RemarkableEvent::kPenUp;
				}
			}
			switch (event.kind)
			{
			case RemarkableEvent::kPenDown:
				if (!gPenDown.load())
				{
					gPenDown.store(true);
					trace.PenEvent(true);
					HostWindowPenDownFine(x8, y8);
					if (penLog != nil)
						fprintf(penLog, "%.0f 0 %.3f %.3f\n", Ms(std::chrono::steady_clock::now() - started), x8 / 8.0, y8 / 8.0);
					penX = penDownX = event.x - originX;
					penY = penDownY = event.y - originY;
					penDrawing = false;
					counts.fStrokes++;
				}
				break;
			case RemarkableEvent::kPenMove:
				if (gPenDown.load())
				{
					trace.PenEvent(false);
					HostWindowPenMoveFine(x8, y8);
					if (penLog != nil)
						fprintf(penLog, "%.0f 1 %.3f %.3f\n", Ms(std::chrono::steady_clock::now() - started), x8 / 8.0, y8 / 8.0);
					if (overlay)
					{
						long px = event.x - originX, py = event.y - originY;
						// not for a tap: only once the pen has moved two display pixels
						if (!penDrawing && (labs(px - penDownX) >= 2 * gScale || labs(py - penDownY) >= 2 * gScale))
							penDrawing = true;
						if (penDrawing)
						{
							long rl, rt, rr, rb;
							if (DrawPanelLine(penX, penY, px, py, gScale, imageWidth, imageHeight, &rl, &rt, &rr, &rb))
							{
								send(rl, rt, rr, rb, policy == kWaveUI ? kRefreshUI : kRefreshInk);
								counts.fOverlay++;
								long dl, dt, dr, db;
								gTurn.DisplayRect(rl, rt, rr, rb, &dl, &dt, &dr, &db);
								Union(&ovL, &ovT, &ovR, &ovB, dl, dt, dr, db);
							}
							penX = px;
							penY = py;
						}
					}
				}
				break;
			case RemarkableEvent::kPenUp:
				if (gPenDown.load())
				{
					HostWindowPenUp();
					if (penLog != nil)
					{
						fprintf(penLog, "%.0f 2 %.3f %.3f\n", Ms(std::chrono::steady_clock::now() - started), x8 / 8.0, y8 / 8.0);
						fflush(penLog);
					}
					gPenDown.store(false);
					trace.PenUp();
					if (penDrawing && ovR > ovL)
					{
						ovPending = true;
						ovRestoreAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(overlayHoldMs);
					}
					penDrawing = false;
				}
				break;
			case RemarkableEvent::kKeyDown:
			case RemarkableEvent::kKeyUp:
				if (event.key >= 0)
					HostWindowKey(event.key, event.kind == RemarkableEvent::kKeyDown);
				break;
			case RemarkableEvent::kRotated:
				// the tablet turned (or the folio folded back): the Newton's
				// screen turned to match, and the picture with it
				gRotation.store(event.x);
				HostWindowDeviceRotation(event.x);
				break;
			case RemarkableEvent::kClosed:
				closed = true;
				break;
			default:
				break;
			}
		}
	}
	if (trace.fOn)
		counts.Report(Ms(std::chrono::steady_clock::now() - started) / 1000.0);
	if (penLog != nil)
		fclose(penLog);
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
	if (width > height && panelWidth < panelHeight)
	{
		// a landscape display (HostWindowPreferredDisplay): AppLoad shows it
		// on the panel turned, so it has the panel's sides the other way
		long t = panelWidth;
		panelWidth = panelHeight;
		panelHeight = t;
	}
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


// The display's shape, chosen once as newton starts, since the
// framebuffer's size is fixed when it is asked for: NEWTON_RM_ORIENTATION=
// portrait (the default: --display as it is), landscape, auto (landscape
// while the type folio is attached), or appload (a newer AppLoad with
// supportsRotation in the manifest turns it as the tablet turns).  AppLoad
// v0.4.2 (the owner's, on software 3.25) keeps its full-screen window
// portrait when the interface turns to landscape with the folio and scales
// it to the screen's height, so a landscape picture only comes out smaller
// there - and the scaling slowed AppLoad enough to hold up newton's updates
// and bunch the pen's events (jagged ink).  docs/host-remarkable.md, "Rotation"
void
HostWindowPreferredDisplay(long* width, long* height)
{
	if (gStartScale >= 1 && gStartScale <= 4)
	{
		// the Host panel's Screen size: the panel over the scale, portrait,
		// whatever --display said
		long panelWidth, panelHeight;
		RemarkablePanelSize(&panelWidth, &panelHeight);
		*width = (panelWidth / gStartScale) & ~1L;
		*height = (panelHeight / gStartScale) & ~1L;
	}
	const char* how = getenv("NEWTON_RM_ORIENTATION");
	if (how != nil && strcmp(how, "appload") == 0)
		return;
	bool landscape;
	if (how != nil && strcmp(how, "auto") == 0)
		landscape = RemarkableFolioAttached();
	else
		landscape = how != nil && strcmp(how, "landscape") == 0;
	if (landscape != (*width > *height))
	{
		long t = *width;
		*width = *height;
		*height = t;
	}
	fprintf(stderr, "[host] reMarkable: the display %ld x %ld (%s%s)\n", *width, *height,
			landscape ? "landscape" : "portrait", how != nil && strcmp(how, "auto") == 0 ? (landscape ? ": the folio is attached" : ": no folio") : "");
}


bool
HostWindowOption(const char* name, long* value)
{
	// (the panel's size is the device's, known before the window opens -
	// the Host settings read the screen size kept for this start then)
	long panelWidth, panelHeight;
	RemarkablePanelSize(&panelWidth, &panelHeight);
	if (strcmp(name, "panelWidth") == 0)
		{ *value = panelWidth; return true; }
	if (strcmp(name, "panelHeight") == 0)
		{ *value = panelHeight; return true; }
	// the rest only with a panel open (headless: none of them applies), and
	// only what that panel can do
	if (gPanel == nil)
		return false;
	if (strcmp(name, "touch") == 0)
		{ *value = gTouchIsPen.load() ? 1 : 0; return true; }
	if (strcmp(name, "waveform") == 0 && gPanel->HasPenInk())
		{ *value = gWaveform.load() < 0 ? 0 : gWaveform.load(); return true; }
	if (strcmp(name, "directPen") == 0 && gPanel->HasDirectPen())
	{
		int asked = gDirectPenAsked.load();
		*value = asked >= 0 ? asked : (gPanel != nil && gPanel->DirectPen() ? 1 : 0);
		return true;
	}
	if (strcmp(name, "clearGhosts") == 0)
		{ *value = 0; return true; }
	if (strcmp(name, "scale") == 0)
		{ *value = gScale; return true; }
	return false;
}


bool
HostWindowSetOption(const char* name, long value)
{
	if (strcmp(name, "touch") == 0)
		gTouchIsPen.store(value != 0);
	else if (strcmp(name, "waveform") == 0)
		{ if (value < 0 || value > 2) return false; gWaveform.store((int) value); }
	else if (strcmp(name, "directPen") == 0)
		gDirectPenAsked.store(value != 0 ? 1 : 0);
	else if (strcmp(name, "clearGhosts") == 0)
		{ if (value != 0) gClearAsked.store(true); }
	else if (strcmp(name, "startScale") == 0)
		gStartScale = value;					// (before the window starts: HostWindowPreferredDisplay)
	else
		return false;
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
