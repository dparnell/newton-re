/*
	File:		host/win32/HostWindow.cpp

	Contains:	The host's window on Windows.
*/

#ifdef _WIN32
#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "HostWindow.h"
#include <string.h>
#ifdef _WIN32

// the pen and the keyboard: C-linkage shims over hal/host/HostTablet.h
// and host/HostKeyboard.h (HostKeyboard.cpp), the Newton headers kept out
extern "C" {
void	HostWindowPenDown(long x, long y);
void	HostWindowPenMove(long x, long y);
void	HostWindowPenUp(void);
void	HostWindowKey(long virtualKey, int down);
void	HostWindowClosed(void);
}

#define nil 0
static long					gWidth = 0;
static long					gHeight = 0;
static const unsigned char*	gPixels = nil;
static long					gScale = 1;
static char					gTitle[128];
static HWND					gWindow = nil;
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
		KillTimer(hwnd, kRefreshTimer);
		PostQuitMessage(0);
		return 0;
	}
	return DefWindowProcA(hwnd, msg, wParam, lParam);
}


static void
WindowThread(void)
{
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
	gWindow = CreateWindowA("NewtonHostWindow", gTitle, style, CW_USEDEFAULT, CW_USEDEFAULT, r.right - r.left, r.bottom - r.top, nil, nil, wc.hInstance, nil);
	if (gWindow == nil)
		return;
	ShowWindow(gWindow, SW_SHOW);
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

bool
HostWindowStart(long /*width*/, long /*height*/, const unsigned char* /*pixels*/, const char* /*title*/, long /*scale*/)
{
	return false;
}


void
HostWindowStop(void)
{ }

#endif
