/*
	File:		host/x11/HostWindow.cpp

	Contains:	The host's window on X11 (HostWindow.h), which is what a
				Linux or BSD host shows the display in - under Wayland
				through XWayland.  The Windows one is host/win32/HostWindow.cpp
				and answers the same two calls.

	The window runs on a thread of its own, as the Windows one does, and
	that thread owns the X connection: nothing else touches it.  The loop
	waits on the connection with a timeout rather than blocking in
	XNextEvent, so the same wait refreshes the display thirty times a
	second and notices HostWindowStop without another thread having to
	send it anything.

	The display's grays are shown through an XImage of the visual's own
	depth, built at the scaled size (a pixel of the display written scale
	by scale times), so no X extension is needed to magnify it.  Only a
	TrueColor visual of two or four bytes to the pixel is served; on
	anything else HostWindowStart answers false and the world runs
	headless, as it does with no display at all.

	NOT YET: a package dropped onto the window (the Windows one takes
	WM_DROPFILES and calls HostWindowFileDropped).  That is XDND, a
	protocol of its own; until it is here, `newton --package` installs one.
*/

#include "HostWindow.h"

#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#include <chrono>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <limits.h>

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
static std::atomic<long>	gPositionX(LONG_MIN), gPositionY(LONG_MIN);	// where it is (or is to open)

static long					gWidth = 0;			// the display's size
static long					gHeight = 0;
static const unsigned char*	gPixels = nil;
static long					gScale = 1;
static char					gTitle[128];
static std::thread*			gThread = nil;
static std::atomic<bool>	gPenDown(false);
static std::atomic<bool>	gStopping(false);
static std::atomic<bool>	gStarted(false);		// the window is up (or has failed to come up)
static std::atomic<bool>	gFailed(false);


/*------------------------------------------------------------------------------
	A key of a PC keyboard as its Windows virtual key code, which is what
	host/HostKeyboard.cpp's HostKeyCodeForVirtualKey names the Newton's (ADB)
	key codes by.  The window on either host hands over the same code, so
	there is one map from a key to the Newton's and not two.
------------------------------------------------------------------------------*/

static long
VirtualKeyForKeySym(KeySym sym)
{
	if (sym >= XK_a && sym <= XK_z)
		return 'A' + (sym - XK_a);
	if (sym >= XK_A && sym <= XK_Z)
		return 'A' + (sym - XK_A);
	if (sym >= XK_0 && sym <= XK_9)
		return '0' + (sym - XK_0);
	if (sym >= XK_KP_0 && sym <= XK_KP_9)
		return '0' + (sym - XK_KP_0);
	switch (sym)
	{
	case XK_Return: case XK_KP_Enter:	return 0x0d;
	case XK_Tab: case XK_ISO_Left_Tab:	return 0x09;
	case XK_space:						return 0x20;
	case XK_BackSpace:					return 0x08;
	case XK_Escape:						return 0x1b;
	case XK_Control_L: case XK_Control_R:	return 0x11;
	case XK_Shift_L: case XK_Shift_R:	return 0x10;
	case XK_Caps_Lock:					return 0x14;
	case XK_Alt_L: case XK_Alt_R:
	case XK_Meta_L: case XK_Meta_R:		return 0x12;
	case XK_Left:						return 0x25;
	case XK_Up:							return 0x26;
	case XK_Right:						return 0x27;
	case XK_Down:						return 0x28;
	case XK_Delete: case XK_KP_Delete:	return 0x2e;
	case XK_Home: case XK_KP_Home:		return 0x24;
	case XK_End: case XK_KP_End:		return 0x23;
	case XK_Prior: case XK_KP_Prior:	return 0x21;		// page up
	case XK_Next: case XK_KP_Next:		return 0x22;		// page down
	case XK_comma:						return 0xbc;
	case XK_period:						return 0xbe;
	case XK_slash:						return 0xbf;
	}
	return -1;
}


/*------------------------------------------------------------------------------
	The gray levels of a TrueColor visual: each of the three masks says
	where its component sits and how many bits it has, so a level of 0..255
	is shifted down to that many bits and up into place.  The three come to
	one gray.
------------------------------------------------------------------------------*/

struct GrayRamp
{
	unsigned long	fPixel[256];

	void	Build(unsigned long red, unsigned long green, unsigned long blue)
	{
		for (int i = 0; i < 256; i++)
		{
			unsigned char level = (unsigned char) (255 - i);		// the display's 0 is white, 255 black
			fPixel[i] = Component(level, red) | Component(level, green) | Component(level, blue);
		}
	}

	static unsigned long	Component(unsigned char level, unsigned long mask)
	{
		if (mask == 0)
			return 0;
		int shift = 0;
		while (((mask >> shift) & 1) == 0)
			shift++;
		int bits = 0;
		for (unsigned long m = mask >> shift; (m & 1) != 0; m >>= 1)
			bits++;
		return ((unsigned long) (level >> (8 - bits)) << shift) & mask;
	}
};


/*------------------------------------------------------------------------------
	The window's thread: the connection, the window, then the loop.
------------------------------------------------------------------------------*/

static void
WindowThread(void)
{
	// this thread is none of the machine's: it must never make a Newton
	// system call (host/HostKeyboard.cpp, kernel/host/TaskRuntime.h)
	HostWindowThreadStarted();

	Display* display = XOpenDisplay(nil);
	if (display == nil)
	{
		gFailed.store(true);
		gStarted.store(true);
		return;
	}
	int screen = DefaultScreen(display);
	Visual* visual = DefaultVisual(display, screen);
	int depth = DefaultDepth(display, screen);
	long scaledWidth = gWidth * gScale;
	long scaledHeight = gHeight * gScale;

	// the image the display is drawn into, at the scaled size
	XImage* image = XCreateImage(display, visual, (unsigned) depth, ZPixmap, 0, nil,
								(unsigned) scaledWidth, (unsigned) scaledHeight, 32, 0);
	int bytesPerPixel = image != nil ? image->bits_per_pixel / 8 : 0;
	if (image == nil || visual->c_class != TrueColor || (bytesPerPixel != 4 && bytesPerPixel != 2))
	{
		// nothing here can show the grays: the world runs headless
		if (image != nil)
			XDestroyImage(image);
		XCloseDisplay(display);
		gFailed.store(true);
		gStarted.store(true);
		return;
	}
	image->data = (char*) calloc((size_t) (scaledWidth * scaledHeight), (size_t) bytesPerPixel);
	// the samples are written in this machine's own byte order; Xlib puts
	// them in the server's if they differ
	unsigned short one = 1;
	image->byte_order = *(unsigned char*) &one != 0 ? LSBFirst : MSBFirst;

	GrayRamp ramp;
	ramp.Build(visual->red_mask, visual->green_mask, visual->blue_mask);

	long startX = gPositionX.load(), startY = gPositionY.load();
	Window window = XCreateSimpleWindow(display, RootWindow(display, screen),
										startX != LONG_MIN ? (int) startX : 0, startX != LONG_MIN ? (int) startY : 0,
										(unsigned) scaledWidth, (unsigned) scaledHeight, 0,
										BlackPixel(display, screen), WhitePixel(display, screen));
	XStoreName(display, window, gTitle);
	XSelectInput(display, window, ExposureMask | KeyPressMask | KeyReleaseMask
								| ButtonPressMask | ButtonReleaseMask | PointerMotionMask
								| StructureNotifyMask);
	// the close button comes as a ClientMessage rather than ending the
	// connection, so that the run is ended the same way as on Windows
	Atom deleteWindow = XInternAtom(display, "WM_DELETE_WINDOW", False);
	XSetWMProtocols(display, window, &deleteWindow, 1);
	// the display does not resize, so neither does the window
	XSizeHints* hints = XAllocSizeHints();
	if (hints != nil)
	{
		hints->flags = PMinSize | PMaxSize;
		if (startX != LONG_MIN)
		{
			// (a restarted newton's window where the old one was)
			hints->flags |= USPosition;
			hints->x = (int) startX;
			hints->y = (int) startY;
		}
		hints->min_width = hints->max_width = (int) scaledWidth;
		hints->min_height = hints->max_height = (int) scaledHeight;
		XSetWMNormalHints(display, window, hints);
		XFree(hints);
	}
	// without this, holding a key down sends a release before each repeat,
	// which would look like the key being let go (the Newton repeats its
	// own keys: only the first press is sent on either host)
	Bool detectable = False;
	XkbSetDetectableAutoRepeat(display, True, &detectable);

	GC gc = XCreateGC(display, window, 0, nil);
	XMapWindow(display, window);
	XFlush(display);
	if (startX != LONG_MIN)
		fprintf(stderr, "[host] window asked to open at %ld,%ld\n", startX, startY);	// (a restarted newton's; the window manager has the last word)
	gStarted.store(true);

	char down[256];				// which keycodes are held, so a repeat is not sent again
	memset(down, 0, sizeof(down));
	int fd = ConnectionNumber(display);
	bool closed = false;
	while (!gStopping.load() && !closed)
	{
		// the display's grays into the image, each pixel scale by scale
		const unsigned char* pixels = gPixels;
		if (pixels != nil)
		{
			for (long y = 0; y < gHeight; y++)
			{
				const unsigned char* row = pixels + y * gWidth;
				char* out = image->data + (y * gScale) * image->bytes_per_line;
				for (long x = 0; x < gWidth; x++)
				{
					unsigned long pixel = ramp.fPixel[row[x]];
					for (long i = 0; i < gScale; i++)
					{
						char* at = out + (x * gScale + i) * bytesPerPixel;
						if (bytesPerPixel == 4)
							*(unsigned int*) at = (unsigned int) pixel;
						else
							*(unsigned short*) at = (unsigned short) pixel;
					}
				}
				// the other rows of this display pixel are the same row again
				for (long i = 1; i < gScale; i++)
					memcpy(out + i * image->bytes_per_line, out, (size_t) image->bytes_per_line);
			}
			XPutImage(display, window, gc, image, 0, 0, 0, 0, (unsigned) scaledWidth, (unsigned) scaledHeight);
			XFlush(display);
		}

		// the connection waited on with a timeout: the wait is the refresh
		// (thirty times a second) and it comes back by itself, so the stop
		// is noticed without anything being sent from another thread
		while (XPending(display) == 0)
		{
			fd_set readable;
			FD_ZERO(&readable);
			FD_SET(fd, &readable);
			struct timeval timeout;
			timeout.tv_sec = 0;
			timeout.tv_usec = 33 * 1000;
			if (select(fd + 1, &readable, nil, nil, &timeout) <= 0)
				break;
		}
		while (XPending(display) > 0)
		{
			XEvent event;
			XNextEvent(display, &event);
			switch (event.type)
			{
			case ButtonPress:
				if (event.xbutton.button == Button1)
				{
					gPenDown.store(true);
					HostWindowPenDown(event.xbutton.x / gScale, event.xbutton.y / gScale);
				}
				break;
			case MotionNotify:
				if (gPenDown.load())
					HostWindowPenMove(event.xmotion.x / gScale, event.xmotion.y / gScale);
				break;
			case ButtonRelease:
				if (event.xbutton.button == Button1 && gPenDown.load())
				{
					HostWindowPenUp();
					gPenDown.store(false);
				}
				break;
			case KeyPress:
			{
				unsigned code = event.xkey.keycode & 0xff;
				if (down[code] == 0)			// not a repeat
				{
					down[code] = 1;
					long vk = VirtualKeyForKeySym(XLookupKeysym(&event.xkey, 0));
					if (vk >= 0)
						HostWindowKey(vk, 1);
				}
				break;
			}
			case KeyRelease:
			{
				unsigned code = event.xkey.keycode & 0xff;
				down[code] = 0;
				long vk = VirtualKeyForKeySym(XLookupKeysym(&event.xkey, 0));
				if (vk >= 0)
					HostWindowKey(vk, 0);
				break;
			}
			case ClientMessage:
				if ((Atom) event.xclient.data.l[0] == deleteWindow)
					closed = true;
				break;
			case DestroyNotify:
				closed = true;
				break;
			}
		}
	}

	// where it was, for a window opened after it (HostWindowPosition)
	Window child;
	int rootX = 0, rootY = 0;
	if (XTranslateCoordinates(display, window, RootWindow(display, screen), 0, 0, &rootX, &rootY, &child))
	{
		gPositionX.store(rootX);
		gPositionY.store(rootY);
	}
	XFreeGC(display, gc);
	XDestroyWindow(display, window);
	free(image->data);
	image->data = nil;
	XDestroyImage(image);
	XCloseDisplay(display);
	if (closed && !gStopping.load())
		HostWindowClosed();
}


bool
HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale)
{
	gWidth = width;
	gHeight = height;
	gPixels = pixels;
	gScale = scale < 1 ? 1 : scale;
	strncpy(gTitle, title, sizeof(gTitle) - 1);
	gTitle[sizeof(gTitle) - 1] = 0;
	gStarted.store(false);
	gFailed.store(false);
	gThread = new std::thread(WindowThread);
	// wait to be told whether there is a window at all, so that a host with
	// no display (or one this cannot show the grays on) runs headless rather
	// than going on as though it had one
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


// (the shims called as the event loop would call them: an event sent to
// one's own window is not one the server is bound to deliver in order)
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


bool
HostWindowPosition(long* x, long* y)
{
	if (gPositionX.load() == LONG_MIN)
		return false;
	*x = gPositionX.load();
	*y = gPositionY.load();
	return true;
}


void
HostWindowSetPosition(long x, long y)
{
	gPositionX.store(x);
	gPositionY.store(y);
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
}
