/*
	File:		host/win32/HostWindow.cpp

	Contains:	The host's window on Windows.
*/

#ifdef _WIN32
#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#include <limits.h>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif
#include "HostWindow.h"
#include <string.h>
#include <stdio.h>
#ifdef _WIN32

// the pen and the keyboard: C-linkage shims over hal/host/HostTablet.h
// and host/HostKeyboard.h (HostKeyboard.cpp), the Newton headers kept out
extern "C" {
void	HostWindowPenDown(long x, long y);
void	HostWindowPenMove(long x, long y);
void	HostWindowPenUp(void);
void	HostWindowKey(long virtualKey, int down);
void	HostWindowClosed(void);
void	HostWindowThreadStarted(void);
void	HostWindowFileDropped(const char* path);	// host/HostPackages.cpp: a package to install
}

#define nil 0
static long					gWidth = 0;
static long					gHeight = 0;
static const unsigned char*	gPixels = nil;
static long					gScale = 1;
static char					gTitle[128];
static HWND					gWindow = nil;
static std::atomic<long>	gPositionX(LONG_MIN), gPositionY(LONG_MIN);	// where it is (or is to open)
static std::thread*			gThread = nil;
static std::atomic<bool>	gPenDown(false);
static std::atomic<bool>	gStopping(false);
const UINT_PTR				kRefreshTimer = 1;

// the display's grays as an 8-bit DIB, drawn scaled
static void
Paint(HWND hwnd)
{
	PAINTSTRUCT ps;
	HDC dc = BeginPaint(hwnd, &ps);
	long width = gWidth;
	long height = gHeight;
	struct { BITMAPINFOHEADER header; RGBQUAD palette[256]; } info;
	memset(&info, 0, sizeof(info));
	info.header.biSize = sizeof(BITMAPINFOHEADER);
	info.header.biWidth = (LONG) width;
	info.header.biHeight = -(LONG) height;			// top-down
	info.header.biPlanes = 1;
	info.header.biBitCount = 8;
	info.header.biCompression = BI_RGB;
	for (long i = 0; i < 256; i++)
	{
		BYTE level = (BYTE) (255 - i);				// the display's 0 is white, 255 black
		info.palette[i].rgbRed = info.palette[i].rgbGreen = info.palette[i].rgbBlue = level;
	}
	const unsigned char* pixels = gPixels;
	if (pixels != nil)
	{
		// a DIB row is padded to four bytes: copy when the width is not
		long rowBytes = (width + 3) & ~3;
		if (rowBytes == width)
			StretchDIBits(dc, 0, 0, (int) (width * gScale), (int) (height * gScale), 0, 0, (int) width, (int) height, pixels, (BITMAPINFO*) &info, DIB_RGB_COLORS, SRCCOPY);
		else
		{
			unsigned char* padded = new unsigned char[rowBytes * height];
			for (long y = 0; y < height; y++)
				memcpy(padded + y * rowBytes, pixels + y * width, width);
			StretchDIBits(dc, 0, 0, (int) (width * gScale), (int) (height * gScale), 0, 0, (int) width, (int) height, padded, (BITMAPINFO*) &info, DIB_RGB_COLORS, SRCCOPY);
			delete[] padded;
		}
	}
	EndPaint(hwnd, &ps);
}


static LRESULT CALLBACK
WindowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_PAINT:
		Paint(hwnd);
		return 0;
	case WM_TIMER:
		InvalidateRect(hwnd, nil, FALSE);
		return 0;
	case WM_ERASEBKGND:
		return 1;
	case WM_DROPFILES:
	{
		HDROP drop = (HDROP) wParam;
		UINT count = DragQueryFileA(drop, 0xFFFFFFFF, nil, 0);
		for (UINT i = 0; i < count; i++)
		{
			char path[MAX_PATH];
			if (DragQueryFileA(drop, i, path, sizeof(path)) > 0)
				HostWindowFileDropped(path);
		}
		DragFinish(drop);
		return 0;
	}
	case WM_LBUTTONDOWN:
		SetCapture(hwnd);
		gPenDown.store(true);
		HostWindowPenDown((short) LOWORD(lParam) / gScale, (short) HIWORD(lParam) / gScale);
		return 0;
	case WM_MOUSEMOVE:
		if (gPenDown.load())
			HostWindowPenMove((short) LOWORD(lParam) / gScale, (short) HIWORD(lParam) / gScale);
		return 0;
	case WM_LBUTTONUP:
		if (gPenDown.load())
		{
			HostWindowPenUp();
			gPenDown.store(false);
		}
		ReleaseCapture();
		return 0;
	case WM_KEYDOWN:
	case WM_SYSKEYDOWN:
		if ((lParam & (1 << 30)) == 0)		// not a repeat: the Newton repeats its own keys
			HostWindowKey((long) wParam, 1);
		return 0;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		HostWindowKey((long) wParam, 0);
		return 0;
	case WM_CLOSE:
		DestroyWindow(hwnd);
		return 0;
	case WM_DESTROY:
	{
		RECT where;
		if (GetWindowRect(hwnd, &where))
		{
			gPositionX.store(where.left);
			gPositionY.store(where.top);
		}
	}
		KillTimer(hwnd, kRefreshTimer);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(hwnd, msg, wParam, lParam);
}


static void
WindowThread(void)
{
	// this thread is none of the machine's: it must never make a Newton
	// system call (host/HostKeyboard.cpp, kernel/host/TaskRuntime.h)
	HostWindowThreadStarted();
	WNDCLASSA wc;
	memset(&wc, 0, sizeof(wc));
	wc.lpfnWndProc = WindowProc;
	wc.hInstance = GetModuleHandleA(nil);
	wc.hCursor = LoadCursor(nil, IDC_ARROW);
	wc.lpszClassName = "NewtonHostWindow";
	RegisterClassA(&wc);
	RECT r = { 0, 0, (LONG) (gWidth * gScale), (LONG) (gHeight * gScale) };
	DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
	AdjustWindowRect(&r, style, FALSE);
	long x = gPositionX.load(), y = gPositionY.load();
	gWindow = CreateWindowA("NewtonHostWindow", gTitle, style, x != LONG_MIN ? (int) x : CW_USEDEFAULT, x != LONG_MIN ? (int) y : CW_USEDEFAULT,
							r.right - r.left, r.bottom - r.top, nil, nil, wc.hInstance, nil);
	if (gWindow == nil)
		return;
	DragAcceptFiles(gWindow, TRUE);		// a package dropped onto the window is installed
	ShowWindow(gWindow, SW_SHOW);
	if (x != LONG_MIN)
	{
		// (a restarted newton's: where the old one was)
		RECT where;
		GetWindowRect(gWindow, &where);
		fprintf(stderr, "[host] window opened at %ld,%ld (asked for %ld,%ld)\n", (long) where.left, (long) where.top, x, y);
	}
	SetTimer(gWindow, kRefreshTimer, 33, nil);
	MSG msg;
	while (GetMessageA(&msg, nil, 0, 0) > 0)
	{
		TranslateMessage(&msg);
		DispatchMessageA(&msg);
	}
	gWindow = nil;
	if (!gStopping.load())
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
	gThread = new std::thread(WindowThread);
	return true;
}


bool
HostWindowPosition(long* x, long* y)
{
	HWND window = gWindow;
	RECT where;
	if (window != nil && GetWindowRect(window, &where))
	{
		gPositionX.store(where.left);
		gPositionY.store(where.top);
	}
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
HostWindowPostPen(long x, long y, int what)
{
	HWND window = gWindow;
	if (window == nil)
	{
		if (what == 0)
			HostWindowPenDown(x, y);
		else if (what == 1)
			HostWindowPenMove(x, y);
		else
			HostWindowPenUp();
		return;
	}
	LPARAM at = MAKELPARAM((WORD) (x * gScale), (WORD) (y * gScale));
	UINT msg = what == 0 ? WM_LBUTTONDOWN : what == 1 ? WM_MOUSEMOVE : WM_LBUTTONUP;
	PostMessageA(window, msg, what == 2 ? 0 : MK_LBUTTON, at);
}


void
HostWindowStop(void)
{
	gStopping.store(true);
	if (gWindow != nil)
		PostMessageA(gWindow, WM_CLOSE, 0, 0);
	if (gThread != nil)
	{
		gThread->join();
		delete gThread;
		gThread = nil;
	}
}

#else

extern "C" {
void	HostWindowPenDown(long x, long y);
void	HostWindowPenMove(long x, long y);
void	HostWindowPenUp(void);
}

bool
HostWindowStart(long /*width*/, long /*height*/, const unsigned char* /*pixels*/, const char* /*title*/, long /*scale*/)
{
	return false;
}


void
HostWindowPostPen(long x, long y, int what)
{
	if (what == 0)
		HostWindowPenDown(x, y);
	else if (what == 1)
		HostWindowPenMove(x, y);
	else
		HostWindowPenUp();
}


void
HostWindowStop(void)
{ }


bool
HostWindowPosition(long* /*x*/, long* /*y*/)
{
	return false;
}


void
HostWindowSetPosition(long /*x*/, long /*y*/)
{ }

#endif
