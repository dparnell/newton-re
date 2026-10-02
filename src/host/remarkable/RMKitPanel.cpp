/*
	File:		host/remarkable/RMKitPanel.cpp

	Contains:	The panel through rmkit (github.com/rmkit-dev/rmkit, MIT):
				its framebuffer - /dev/fb0 and the mxcfb update ioctls of
				the reMarkable 1, rm2fb's on the reMarkable 2, AppLoad's
				qtfb-shim's on the Paper Pro - and its input devices, the
				pen (the Wacom digitiser), the touch screen and the buttons
				(the type folio's keys come through the shim as buttons too).

				Built only when the configure is given rmkit's single
				header (-DNEWTON_RMKIT_DIR=<dir with rmkit.h and stb/>, made
				by tools/remarkable/fetch_rmkit.py); otherwise NewRMKitPanel
				answers nil.  Note that rmkit opens the framebuffer and the
				input devices in a static constructor, and installs its own
				handlers for SIGINT/SIGTERM/SIGABRT/SIGSEGV, before main -
				newton's crash handler replaces the last of those.
*/

#include "Panel.h"

#if NEWTON_HAVE_RMKIT

#define RMKIT_IMPLEMENTATION
#include "rmkit.h"

#define nil 0

/*------------------------------------------------------------------------------
	A key as Linux numbers it (linux/input-event-codes.h) as a Windows
	virtual key code.
------------------------------------------------------------------------------*/

static long
VirtualKeyForLinuxKey(int code)
{
	static const char kTopRow[] = "QWERTYUIOP";			// KEY_Q (16) ..
	static const char kHomeRow[] = "ASDFGHJKL";			// KEY_A (30) ..
	static const char kBottomRow[] = "ZXCVBNM";			// KEY_Z (44) ..
	if (code >= KEY_1 && code <= KEY_9)
		return '1' + (code - KEY_1);
	if (code == KEY_0)
		return '0';
	if (code >= KEY_Q && code < KEY_Q + 10)
		return kTopRow[code - KEY_Q];
	if (code >= KEY_A && code < KEY_A + 9)
		return kHomeRow[code - KEY_A];
	if (code >= KEY_Z && code < KEY_Z + 7)
		return kBottomRow[code - KEY_Z];
	switch (code)
	{
	case KEY_ESC:			return 0x1b;
	case KEY_BACKSPACE:		return 0x08;
	case KEY_TAB:			return 0x09;
	case KEY_ENTER:			return 0x0d;
	case KEY_SPACE:			return 0x20;
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL:		return 0x11;
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:	return 0x10;
	case KEY_LEFTALT: case KEY_RIGHTALT:
	case KEY_LEFTMETA: case KEY_RIGHTMETA:		return 0x12;
	case KEY_CAPSLOCK:		return 0x14;
	case KEY_COMMA:			return 0xbc;
	case KEY_DOT:			return 0xbe;
	case KEY_SLASH:			return 0xbf;
	case KEY_LEFT:			return 0x25;		// (the reMarkable 1's left button too)
	case KEY_UP:			return 0x26;
	case KEY_RIGHT:			return 0x27;		// (and its right button)
	case KEY_DOWN:			return 0x28;
	case KEY_HOME:			return 0x24;		// (and its middle button)
	case KEY_END:			return 0x23;
	case KEY_PAGEUP:		return 0x21;
	case KEY_PAGEDOWN:		return 0x22;
	case KEY_DELETE:		return 0x2e;
	case KEY_POWER:			return 0x7b;		// F12: the Newton's power switch (host/HostKeyboard.cpp)
	}
	return -1;
}


class RMKitPanel : public RemarkablePanel
{
public:
						RMKitPanel() : fWidth(0), fHeight(0), fLeft(0), fTop(0), fPenDown(false), fTouchDown(false), fHead(0), fTail(0) {}
	virtual const char*	Name(void) { return "rmkit"; }
	virtual bool		NativeSize(long* width, long* height);
	virtual bool		Open(long width, long height);
	virtual void		Close(void) {}
	virtual uint16_t*	Pixels(void) { return (uint16_t*) fFB->fbmem + fTop * fFB->width + fLeft; }
	virtual long		RowWords(void) { return fFB->width; }
	virtual void		Origin(long* left, long* top) { *left = fLeft; *top = fTop; }
	virtual void		Update(long left, long top, long right, long bottom, RemarkableRefresh how);
	virtual bool		Poll(RemarkableEvent* event, long timeoutMs);

private:
	void				Queue(RemarkableEvent::Kind kind, long x, long y, long key);

	shared_ptr<framebuffer::FB>	fFB;
	long				fWidth, fHeight, fLeft, fTop;
	bool				fPenDown, fTouchDown;
	RemarkableEvent		fQueue[64];
	int					fHead, fTail;
};


bool
RMKitPanel::NativeSize(long* width, long* height)
{
	fFB = framebuffer::get();
	if (fFB == nullptr || fFB->fbmem == nullptr)
		return false;
	*width = fFB->display_width;
	*height = fFB->height;
	return true;
}


bool
RMKitPanel::Open(long width, long height)
{
	if (fFB == nullptr)
		fFB = framebuffer::get();
	if (width > fFB->display_width || height > fFB->height)
		return false;
	fWidth = width;
	fHeight = height;
	fLeft = (fFB->display_width - width) / 2;
	fTop = (fFB->height - height) / 2;
	fFB->clear_screen();
	fFB->waveform_mode = WAVEFORM_MODE_GC16;
	fFB->update_mode = UPDATE_MODE_FULL;
	fFB->redraw_screen(true);
	return true;
}


void
RMKitPanel::Update(long left, long top, long right, long bottom, RemarkableRefresh how)
{
	fFB->update_dirty(fFB->dirty_area, (int) (fLeft + left), (int) (fTop + top));
	fFB->update_dirty(fFB->dirty_area, (int) (fLeft + right - 1), (int) (fTop + bottom - 1));
	switch (how)
	{
	case kRefreshInk:		fFB->waveform_mode = WAVEFORM_MODE_DU;   fFB->update_mode = UPDATE_MODE_PARTIAL; break;
	case kRefreshUI:		fFB->waveform_mode = WAVEFORM_MODE_GC4;  fFB->update_mode = UPDATE_MODE_PARTIAL; break;	// (GL16 on the rM1; the shim's "UI")
	case kRefreshContent:	fFB->waveform_mode = WAVEFORM_MODE_GC16; fFB->update_mode = UPDATE_MODE_FULL; break;
	}
	fFB->redraw_screen(how == kRefreshContent);
}


void
RMKitPanel::Queue(RemarkableEvent::Kind kind, long x, long y, long key)
{
	int next = (fTail + 1) % 64;
	if (next == fHead)
		return;
	fQueue[fTail].kind = kind;
	fQueue[fTail].x = x;
	fQueue[fTail].y = y;
	fQueue[fTail].key = key;
	fTail = next;
}


bool
RMKitPanel::Poll(RemarkableEvent* event, long timeoutMs)
{
	if (fHead == fTail)
	{
		// rmkit's wait: 0 would be for ever, so at least a millisecond
		input::Input& in = ui::MainLoop::in;
		in.listen_all(timeoutMs > 0 ? timeoutMs : 1);
		for (auto& ev : in.wacom.events)
		{
			bool down = ev.btn_touch != 0 && ev.eraser == 0;
			if (down && !fPenDown)
				Queue(RemarkableEvent::kPenDown, ev.x, ev.y, -1);
			else if (down)
				Queue(RemarkableEvent::kPenMove, ev.x, ev.y, -1);
			else if (fPenDown)
				Queue(RemarkableEvent::kPenUp, ev.x, ev.y, -1);
			fPenDown = down;
		}
		for (auto& ev : in.touch.events)
		{
			bool down = ev.left != 0;
			if (down && !fTouchDown)
				Queue(RemarkableEvent::kTouchDown, ev.x, ev.y, -1);
			else if (down)
				Queue(RemarkableEvent::kTouchMove, ev.x, ev.y, -1);
			else if (fTouchDown)
				Queue(RemarkableEvent::kTouchUp, ev.x, ev.y, -1);
			fTouchDown = down;
		}
		for (auto& ev : in.button.events)
			if (ev.key >= 0 && ev.is_pressed != 2)				// (2: an auto-repeat - the Newton repeats its own keys)
				Queue(ev.is_pressed ? RemarkableEvent::kKeyDown : RemarkableEvent::kKeyUp, 0, 0, VirtualKeyForLinuxKey(ev.key));
	}
	if (fHead == fTail)
		return false;
	*event = fQueue[fHead];
	fHead = (fHead + 1) % 64;
	return true;
}


RemarkablePanel*
NewRMKitPanel(void)
{
	return new RMKitPanel;
}

#else

RemarkablePanel*
NewRMKitPanel(void)
{
	return 0;					// built without rmkit
}

#endif
