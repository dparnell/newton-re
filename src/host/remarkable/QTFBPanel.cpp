/*
	File:		host/remarkable/QTFBPanel.cpp

	Contains:	The Paper Pro's panel as AppLoad lends it to a program it
				started: a qtfb framebuffer (QTFB.h).  newton asks for one
				exactly the size of its scaled display - AppLoad shows it in
				the middle of the screen (or scaled, as its window settings
				say) and hands back the pen in the framebuffer's own pixels,
				so no coordinates need translating here.
*/

#include "Panel.h"
#include "QTFB.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <linux/input.h>
#include <sys/ioctl.h>

#define nil 0

/*------------------------------------------------------------------------------
	A key as Qt names it (Qt::Key - what AppLoad forwards from the type
	folio and its own keyboard) as a Windows virtual key code.  Qt's
	letters and digits are their capitals' ASCII, as the virtual codes are.
------------------------------------------------------------------------------*/

static long
VirtualKeyForQtKey(long key)
{
	if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
		return key;
	if (key >= 'a' && key <= 'z')
		return key - 'a' + 'A';
	switch (key)
	{
	case 0x20:			return 0x20;		// space
	case 0x2c:			return 0xbc;		// comma
	case 0x2e:			return 0xbe;		// period
	case 0x2f:			return 0xbf;		// slash
	case 0x01000000:	return 0x1b;		// Escape
	case 0x01000001:	return 0x09;		// Tab
	case 0x01000002:	return 0x09;		// Backtab
	case 0x01000003:	return 0x08;		// Backspace
	case 0x01000004:
	case 0x01000005:	return 0x0d;		// Return, Enter
	case 0x01000007:	return 0x2e;		// Delete
	case 0x01000010:	return 0x24;		// Home
	case 0x01000011:	return 0x23;		// End
	case 0x01000012:	return 0x25;		// Left
	case 0x01000013:	return 0x26;		// Up
	case 0x01000014:	return 0x27;		// Right
	case 0x01000015:	return 0x28;		// Down
	case 0x01000016:	return 0x21;		// PageUp
	case 0x01000017:	return 0x22;		// PageDown
	case 0x01000020:	return 0x10;		// Shift
	case 0x01000021:	return 0x11;		// Control
	case 0x01000022:	return 0x12;		// Meta (the folio's command key): the Newton's command key, as Alt is
	case 0x01000023:	return 0x12;		// Alt
	case 0x01000024:	return 0x14;		// CapsLock
	case 0x0100100a:							// PowerOff
	case 0x010000b7:	return 0x7b;		// PowerDown: F12, the Newton's power switch (host/HostKeyboard.cpp)
	}
	return -1;
}


class QTFBPanel : public RemarkablePanel
{
public:
						QTFBPanel() : fSocket(-1), fShm(nil), fShmSize(0), fWidth(0), fHeight(0), fPanelWidth(0), fPanelHeight(0), fLeft(0), fTop(0),
										 fMode(-1), fPenDown(false), fTouchId(-1), fPenFd(-1), fPenMaxX(1), fPenMaxY(1), fPenX(0), fPenY(0),
										 fPenTouch(false), fPenWasDown(false), fPenMoved(false), fFlipX(false), fFlipY(false), fSwapXY(false) {}
	virtual				~QTFBPanel() { Close(); }
	virtual const char*	Name(void) { return "qtfb"; }
	virtual bool		NativeSize(long* width, long* height);
	virtual bool		Open(long width, long height);
	virtual void		Close(void);
	virtual uint16_t*	Pixels(void) { return (uint16_t*) fShm + fTop * fPanelWidth + fLeft; }
	virtual long		RowWords(void) { return fPanelWidth; }
	virtual void		Origin(long* left, long* top) { *left = fLeft; *top = fTop; }
	virtual void		Update(long left, long top, long right, long bottom, RemarkableRefresh how);
	virtual bool		Poll(RemarkableEvent* event, long timeoutMs);

private:
	bool				Connect(void);
	void				Send(const QTFBClientMessage& message);
	void				OpenPenDevice(void);
	bool				ReadPen(RemarkableEvent* event);

	int					fSocket;
	unsigned char*		fShm;
	size_t				fShmSize;
	long				fWidth, fHeight;		// the image
	long				fPanelWidth, fPanelHeight;	// the framebuffer: the whole panel
	long				fLeft, fTop;			// the image's place on it
	// the Marker read from its own input device instead of from AppLoad's
	// qtfb messages (NEWTON_RM_PEN=evdev; docs/host-remarkable.md)
	int					fPenFd;
	long				fPenMaxX, fPenMaxY;
	long				fPenX, fPenY;
	bool				fPenTouch, fPenWasDown, fPenMoved;
	bool				fFlipX, fFlipY, fSwapXY;
	int					fMode;				// the refresh mode last set (-1: none yet)
	bool				fPenDown;
	int					fTouchId;			// the touch acting as the pen (NEWTON_RM_TOUCH=pen), -1 if none
};


static const char*
SocketPath(void)
{
	const char* path = getenv("NEWTON_QTFB_SOCKET");
	return path != nil ? path : QTFB_SOCKET_PATH;
}


bool
QTFBPanel::Connect(void)
{
	if (fSocket >= 0)
		return true;
	int s = socket(AF_UNIX, SOCK_SEQPACKET, 0);
	if (s < 0)
		return false;
	struct sockaddr_un address;
	memset(&address, 0, sizeof(address));
	address.sun_family = AF_UNIX;
	strncpy(address.sun_path, SocketPath(), sizeof(address.sun_path) - 1);
	if (connect(s, (struct sockaddr*) &address, sizeof(address)) != 0)
	{
		close(s);
		return false;
	}
	fSocket = s;
	return true;
}


/*------------------------------------------------------------------------------
	The panel's size: NEWTON_RM_PANEL=WxH if it is given, else the device
	tree's model - the Paper Pro Move's is smaller - else the Paper Pro's.
	(The model strings are the community's names for the boards, and want
	checking on a device: docs/host-remarkable.md.)
------------------------------------------------------------------------------*/

bool
QTFBPanel::NativeSize(long* width, long* height)
{
	if (getenv("QTFB_KEY") == nil && getenv("NEWTON_QTFB_SOCKET") == nil)
		return false;						// not started by AppLoad (nor by its stand-in)
	if (!Connect())
		return false;
	*width = 1620;
	*height = 2160;
	fPanelWidth = 1620;
	fPanelHeight = 2160;
	const char* panel = getenv("NEWTON_RM_PANEL");
	long w, h;
	if (panel != nil && sscanf(panel, "%ldx%ld", &w, &h) == 2 && w > 0 && h > 0)
	{
		*width = fPanelWidth = w;
		*height = fPanelHeight = h;
		return true;
	}
	FILE* f = fopen("/proc/device-tree/model", "r");
	if (f != nil)
	{
		char model[128];
		size_t n = fread(model, 1, sizeof(model) - 1, f);
		model[n] = 0;
		fclose(f);
		if (strstr(model, "Chiappa") != nil || strstr(model, "Move") != nil)
		{
			*width = fPanelWidth = 954;
			*height = fPanelHeight = 1696;
		}
		else if (strstr(model, "reMarkable 2") != nil)
		{
			*width = fPanelWidth = 1404;
			*height = fPanelHeight = 1872;
		}
	}
	return true;
}


bool
QTFBPanel::Open(long width, long height)
{
	if (!Connect())
		return false;
	const char* keyText = getenv("QTFB_KEY");
	int key = keyText != nil ? (int) strtol(keyText, nil, 10) : 245209899;	// (AppLoad's own default framebuffer)
	QTFBClientMessage message;
	memset(&message, 0, sizeof(message));
	message.type = kQTFBCustomInitialize;
	message.customInit.key = key;
	message.customInit.format = kQTFBFormatRMPP_RGB565;	// (a custom size: only the pixel's size matters)
	// the whole panel, the image centred on it by newton: then AppLoad has
	// nothing to scale or move, and a point on the glass is a point of the
	// framebuffer whichever way the pen reaches newton
	if (fPanelWidth < width || fPanelHeight < height)
	{
		fPanelWidth = width;
		fPanelHeight = height;
	}
	message.customInit.width = (uint16_t) fPanelWidth;
	message.customInit.height = (uint16_t) fPanelHeight;
	Send(message);
	QTFBServerMessage answer;
	memset(&answer, 0, sizeof(answer));
	if (recv(fSocket, &answer, sizeof(answer), 0) < 1 || answer.type != kQTFBInitialize)
	{
		fprintf(stderr, "[host] qtfb: the framebuffer was refused\n");
		return false;
	}
	char name[32];
	snprintf(name, sizeof(name), QTFB_SHM_NAME_FORMAT, answer.init.shmKey);
	int fd = shm_open(name, O_RDWR, 0);
	if (fd < 0)
	{
		fprintf(stderr, "[host] qtfb: no shared memory %s (%d)\n", name, errno);
		return false;
	}
	fShmSize = answer.init.shmSize;
	void* memory = mmap(nil, fShmSize, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	close(fd);
	if (memory == MAP_FAILED || fShmSize < (size_t) (fPanelWidth * fPanelHeight * 2))
	{
		fprintf(stderr, "[host] qtfb: the shared memory could not be mapped\n");
		return false;
	}
	fShm = (unsigned char*) memory;
	fWidth = width;
	fHeight = height;
	fLeft = (fPanelWidth - width) / 2;
	fTop = (fPanelHeight - height) / 2;
	for (size_t i = 0; i < (size_t) (fPanelWidth * fPanelHeight); i++)
		((uint16_t*) fShm)[i] = 0xffff;				// white round the image
	fprintf(stderr, "[host] qtfb: framebuffer %ld x %ld (key %d, %s)\n", fPanelWidth, fPanelHeight, key, name);
	OpenPenDevice();
	return true;
}


void
QTFBPanel::Close(void)
{
	if (fSocket >= 0)
	{
		QTFBClientMessage message;
		memset(&message, 0, sizeof(message));
		message.type = kQTFBTerminate;
		Send(message);
		close(fSocket);
		fSocket = -1;
	}
	if (fPenFd >= 0)
	{
		close(fPenFd);
		fPenFd = -1;
	}
	if (fShm != nil)
	{
		munmap(fShm, fShmSize);
		fShm = nil;
	}
}


void
QTFBPanel::Send(const QTFBClientMessage& message)
{
	if (fSocket >= 0)
		send(fSocket, &message, sizeof(message), MSG_NOSIGNAL);
}


void
QTFBPanel::Update(long left, long top, long right, long bottom, RemarkableRefresh how)
{
	QTFBClientMessage message;
	memset(&message, 0, sizeof(message));
	bool whole = left <= 0 && top <= 0 && right >= fWidth && bottom >= fHeight;
	left += fLeft;
	right += fLeft;
	top += fTop;
	bottom += fTop;
	if (how == kRefreshContent && whole)
	{
		// everything shown again, the panel cleared of its ghosts (a flash)
		message.type = kQTFBRequestFullRefresh;
		Send(message);
		return;
	}
	int mode = how == kRefreshInk ? kQTFBRefreshFast : how == kRefreshUI ? kQTFBRefreshUI : kQTFBRefreshContent;
	if (mode != fMode)
	{
		message.type = kQTFBSetRefreshMode;
		message.refreshMode = mode;
		Send(message);
		fMode = mode;
	}
	memset(&message, 0, sizeof(message));
	message.type = kQTFBUpdate;
	message.update.type = kQTFBUpdatePartial;
	message.update.x = (int32_t) left;
	message.update.y = (int32_t) top;
	message.update.w = (int32_t) (right - left);
	message.update.h = (int32_t) (bottom - top);
	Send(message);
}


bool
QTFBPanel::Poll(RemarkableEvent* event, long timeoutMs)
{
	event->kind = RemarkableEvent::kNone;
	event->key = -1;
	if (fSocket < 0)
		return false;
	if (fPenFd >= 0 && ReadPen(event))
		return true;
	struct pollfd p[2];
	p[0].fd = fSocket;
	p[0].events = POLLIN;
	p[0].revents = 0;
	p[1].fd = fPenFd;
	p[1].events = POLLIN;
	p[1].revents = 0;
	if (poll(p, fPenFd >= 0 ? 2 : 1, (int) timeoutMs) <= 0)
		return false;
	if (fPenFd >= 0 && (p[1].revents & POLLIN) != 0 && ReadPen(event))
		return true;
	if ((p[0].revents & (POLLIN | POLLHUP | POLLERR)) == 0)
		return false;
	QTFBServerMessage message;
	memset(&message, 0, sizeof(message));
	ssize_t got = recv(fSocket, &message, sizeof(message), MSG_DONTWAIT);
	if (got == 0 || (got < 0 && errno != EAGAIN && errno != EINTR))
	{
		// AppLoad closed the application (the drag down from the top)
		close(fSocket);
		fSocket = -1;
		event->kind = RemarkableEvent::kClosed;
		return true;
	}
	if (got < 1 || message.type != kQTFBUserInput)
		return got > 0;					// (a device state: nothing to do with it yet - docs/host-remarkable.md)
	event->x = message.userInput.x;
	event->y = message.userInput.y;
	switch (message.userInput.inputType)
	{
	case kQTFBPenPress:		event->kind = fPenFd >= 0 ? RemarkableEvent::kNone : RemarkableEvent::kPenDown; break;	// (the device's own, when it is read)
	case kQTFBPenUpdate:	event->kind = fPenFd >= 0 ? RemarkableEvent::kNone : RemarkableEvent::kPenMove; break;
	case kQTFBPenRelease:	event->kind = fPenFd >= 0 ? RemarkableEvent::kNone : RemarkableEvent::kPenUp; break;
	case kQTFBTouchPress:	event->kind = RemarkableEvent::kTouchDown; break;
	case kQTFBTouchUpdate:	event->kind = RemarkableEvent::kTouchMove; break;
	case kQTFBTouchRelease:	event->kind = RemarkableEvent::kTouchUp; break;
	case kQTFBButtonPress:
	case kQTFBVirtualKeyPress:
	case kQTFBButtonRelease:
	case kQTFBVirtualKeyRelease:
		event->key = VirtualKeyForQtKey(message.userInput.x);
		event->kind = (message.userInput.inputType == kQTFBButtonPress || message.userInput.inputType == kQTFBVirtualKeyPress)
					? RemarkableEvent::kKeyDown : RemarkableEvent::kKeyUp;
		break;
	}
	return true;
}


/*------------------------------------------------------------------------------
	The Marker straight from its input device, read alongside xochitl (not
	grabbed: AppLoad's full-screen window keeps xochitl from acting on it).
	The device's axes run the panel's way - ABS_X 0..11180 across, ABS_Y
	0..15340 down on the Paper Pro, as AppLoad's qtfb-shim maps them - and
	NEWTON_RM_PEN_AXES=[x][y][s] flips x, flips y or swaps them should a
	tablet (or a turned screen) say otherwise.  A pen-down is BTN_TOUCH; the
	point is sent at each SYN_REPORT.
------------------------------------------------------------------------------*/

void
QTFBPanel::OpenPenDevice(void)
{
	const char* how = getenv("NEWTON_RM_PEN");
	if (how == nil || strcmp(how, "evdev") != 0)
		return;							// (AppLoad's qtfb hands on the Marker: its events are the default)
	for (int i = 0; i < 16 && fPenFd < 0; i++)
	{
		char path[32];
		snprintf(path, sizeof(path), "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK);
		if (fd < 0)
			continue;
		const size_t bits = 8 * sizeof(long);
		unsigned long keys[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))];
		memset(keys, 0, sizeof(keys));
		ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
		struct input_absinfo ax, ay;
		if (((keys[BTN_TOOL_PEN / bits] >> (BTN_TOOL_PEN % bits)) & 1) == 0
		 || ioctl(fd, EVIOCGABS(ABS_X), &ax) != 0 || ioctl(fd, EVIOCGABS(ABS_Y), &ay) != 0 || ax.maximum <= 0 || ay.maximum <= 0)
		{
			close(fd);
			continue;
		}
		fPenFd = fd;
		fPenMaxX = ax.maximum;
		fPenMaxY = ay.maximum;
		const char* axes = getenv("NEWTON_RM_PEN_AXES");
		if (axes != nil)
		{
			fFlipX = strchr(axes, 'x') != nil;
			fFlipY = strchr(axes, 'y') != nil;
			fSwapXY = strchr(axes, 's') != nil;
		}
		char name[128] = "";
		ioctl(fd, EVIOCGNAME(sizeof(name)), name);
		fprintf(stderr, "[host] qtfb: the pen from %s '%s' (%ld x %ld)\n", path, name, fPenMaxX, fPenMaxY);
	}
}


bool
QTFBPanel::ReadPen(RemarkableEvent* event)
{
	struct input_event e;
	while (read(fPenFd, &e, sizeof(e)) == (ssize_t) sizeof(e))
	{
		if (e.type == EV_ABS && e.code == ABS_X)
			{ fPenX = e.value; fPenMoved = true; }
		else if (e.type == EV_ABS && e.code == ABS_Y)
			{ fPenY = e.value; fPenMoved = true; }
		else if (e.type == EV_KEY && e.code == BTN_TOUCH)
			fPenTouch = e.value != 0;
		else if (e.type == EV_SYN && e.code == SYN_REPORT)
		{
			bool down = fPenTouch;
			if (!down && !fPenWasDown)
				{ fPenMoved = false; continue; }			// hovering
			if (down && fPenWasDown && !fPenMoved)
				continue;
			double u = (double) fPenX / (double) fPenMaxX, v = (double) fPenY / (double) fPenMaxY;
			if (fSwapXY) { double t = u; u = v; v = t; }
			if (fFlipX) u = 1.0 - u;
			if (fFlipY) v = 1.0 - v;
			event->x = (long) (u * (fPanelWidth - 1) + 0.5);
			event->y = (long) (v * (fPanelHeight - 1) + 0.5);
			event->key = -1;
			event->kind = down && !fPenWasDown ? RemarkableEvent::kPenDown
						: down ? RemarkableEvent::kPenMove : RemarkableEvent::kPenUp;
			fPenWasDown = down;
			fPenMoved = false;
			return true;
		}
	}
	return false;
}


RemarkablePanel*
NewQTFBPanel(void)
{
	return new QTFBPanel;
}
