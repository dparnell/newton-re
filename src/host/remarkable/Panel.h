/*
	File:		host/remarkable/Panel.h

	Contains:	What host/remarkable/HostWindow.cpp shows the Newton's
				display on: a reMarkable tablet's e-ink panel, reached one
				of two ways (docs/host-remarkable.md):

				  qtfb   the Paper Pro's (and Paper Pro Move's) - AppLoad's
				         framebuffer protocol over a Unix socket and shared
				         memory (QTFBPanel.cpp, the client written here from
				         the protocol);
				  rmkit  rmkit's framebuffer and input (RMKitPanel.cpp, over
				         rmkit.h, which tools/remarkable/fetch_rmkit.py makes):
				         the reMarkable 1 and 2 directly, the Paper Pro
				         through AppLoad's qtfb-shim, which makes rmkit's
				         /dev/fb0 and /dev/input/event* a qtfb client.

				The panel is 16-bit RGB565 either way (the Paper Pro is
				colour, but the Newton's grays are all it is asked to show).
				Like the window, nothing here sees a Newton header.
*/

#ifndef __REMARKABLE_PANEL_H
#define __REMARKABLE_PANEL_H

#include <stdint.h>

// how an area is to be refreshed: the e-ink's waveforms, fastest first
enum RemarkableRefresh
{
	kRefreshInk,			// the pen is down: as fast as it goes, black and white (DU/A2)
	kRefreshUI,				// an ordinary change: grays, no flash (GL16)
	kRefreshContent			// a clean redraw of everything shown: flashes (GC16)
};

struct RemarkableEvent
{
	enum Kind { kNone, kPenDown, kPenMove, kPenUp, kTouchDown, kTouchMove, kTouchUp, kKeyDown, kKeyUp, kRotated, kClosed };
	Kind	kind;
	long	x, y;					// in the panel's pixels (the pen, the touch); kRotated: x is how the device is held (host/HostOrientation.h's kHostRotation*)
	long	key;					// a key, as a Windows virtual key code (host/HostWindow.h); -1 if none
	long	id;						// a touch: which finger
};

class RemarkablePanel
{
public:
	virtual					~RemarkablePanel() {}
	virtual const char*		Name(void) = 0;
	// the size of the area the panel can show, before it is opened (the
	// window picks its scale from it): ==> false if there is no such panel here
	virtual bool			NativeSize(long* width, long* height) = 0;
	// opened for an image of that size (at most the native size), shown in the middle
	virtual bool			Open(long width, long height) = 0;
	virtual void			Close(void) = 0;
	// the image: RGB565 words, rowWords to a row, the opened width by height,
	// already offset to where it is shown
	virtual uint16_t*		Pixels(void) = 0;
	virtual long			RowWords(void) = 0;
	// the image's (left, top) on the panel - pen points come in panel pixels
	virtual void			Origin(long* left, long* top) = 0;
	// [left, right) x [top, bottom) of the image to the glass
	virtual void			Update(long left, long top, long right, long bottom, RemarkableRefresh how) = 0;
	// one event, waiting at most that long for it: ==> whether there was one
	virtual bool			Poll(RemarkableEvent* event, long timeoutMs) = 0;
	// the ink sent in the pen's own waveform (qtfb's ufast) rather than the
	// fast one, from the next update on; a panel without one ignores it
	virtual void			SetPenInk(bool pen) { (void) pen; }
	virtual bool			PenInk(void) { return false; }
};

RemarkablePanel*	NewQTFBPanel(void);
RemarkablePanel*	NewRMKitPanel(void);		// nil when newton was built without rmkit

#endif	/* __REMARKABLE_PANEL_H */
