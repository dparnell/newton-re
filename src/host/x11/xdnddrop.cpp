/*
	File:		host/x11/xdnddrop.cpp

	Contains:	xdnddrop, a drag source for testing a window's XDND drop
				target (host/x11/HostWindow.cpp): it drops a file onto the
				top-level window of a given name, as a file manager would.

	Usage:
		xdnddrop [--name NAME] [--timeout SECONDS] FILE [-- PROGRAM ARGS...]

	With a program it runs the program first and answers the program's exit
	status, which is how ctest host.NewtonWindowDrop uses it:

		xdnddrop fixtures/packages/fonts/monaco.pkg -- newton --limit 120 ...

	It waits for a window named NAME ("Newton" by default) that says it takes
	drops (XdndAware), then plays the source's half of XDND version 5:
	XdndEnter offering text/uri-list, XdndPosition over the window's middle
	(waiting for XdndStatus), XdndDrop, the file's file:// URI handed over
	when the target asks for the selection, and XdndFinished awaited.  It
	prints what it did ("xdnddrop: dropped ... accepted") and exits non-zero
	(after stopping the program it started) when there is no X display, no
	such window within the timeout, or the drop is refused - 77, which ctest
	takes for a skip, when there is no display at all.
*/

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static Display*	gDisplay = nullptr;
static pid_t	gChild = 0;

static Atom		aAware, aEnter, aPosition, aStatus, aDrop, aFinished, aActionCopy, aSelection, aUriList, aNetName, aUtf8;


static double
Now(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}


static int
Fail(const char* message)
{
	fprintf(stderr, "xdnddrop: %s\n", message);
	if (gChild > 0)
	{
		kill(gChild, SIGTERM);
		waitpid(gChild, nullptr, 0);
	}
	return 2;
}


// Whether the window is named so (WM_NAME or _NET_WM_NAME)
static bool
NamedSo(Window w, const char* name)
{
	bool yes = false;
	char* wmName = nullptr;
	if (XFetchName(gDisplay, w, &wmName) && wmName != nullptr)
	{
		yes = strcmp(wmName, name) == 0;
		XFree(wmName);
	}
	if (!yes)
	{
		Atom type;
		int format;
		unsigned long count = 0, left = 0;
		unsigned char* data = nullptr;
		if (XGetWindowProperty(gDisplay, w, aNetName, 0, 256, False, aUtf8, &type, &format, &count, &left, &data) == Success && data != nullptr)
		{
			yes = strlen(name) == count && memcmp(data, name, count) == 0;
			XFree(data);
		}
	}
	return yes;
}


// A window of that name that takes drops, anywhere under w
static Window
FindTarget(Window w, const char* name, int depth)
{
	if (NamedSo(w, name))
	{
		Atom type;
		int format;
		unsigned long count = 0, left = 0;
		unsigned char* data = nullptr;
		bool aware = XGetWindowProperty(gDisplay, w, aAware, 0, 1, False, XA_ATOM, &type, &format, &count, &left, &data) == Success && count > 0;
		if (data != nullptr)
			XFree(data);
		if (aware)
			return w;
	}
	if (depth > 4)
		return 0;
	Window root, parent, *children = nullptr;
	unsigned int n = 0;
	Window found = 0;
	if (XQueryTree(gDisplay, w, &root, &parent, &children, &n))
	{
		for (unsigned int i = 0; i < n && found == 0; i++)
			found = FindTarget(children[i], name, depth + 1);
		if (children != nullptr)
			XFree(children);
	}
	return found;
}


static void
Send(Window to, Atom type, long l0, long l1, long l2, long l3, long l4)
{
	XEvent e;
	memset(&e, 0, sizeof(e));
	e.xclient.type = ClientMessage;
	e.xclient.display = gDisplay;
	e.xclient.window = to;
	e.xclient.message_type = type;
	e.xclient.format = 32;
	e.xclient.data.l[0] = l0;
	e.xclient.data.l[1] = l1;
	e.xclient.data.l[2] = l2;
	e.xclient.data.l[3] = l3;
	e.xclient.data.l[4] = l4;
	XSendEvent(gDisplay, to, False, NoEventMask, &e);
	XFlush(gDisplay);
}


// The next event, waiting at most until the deadline: ==> whether there is one
static bool
NextEvent(XEvent* e, double deadline)
{
	while (XPending(gDisplay) == 0)
	{
		double left = deadline - Now();
		if (left <= 0)
			return false;
		int fd = ConnectionNumber(gDisplay);
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(fd, &readable);
		struct timeval tv;
		tv.tv_sec = (long) left;
		tv.tv_usec = (long) ((left - (long) left) * 1e6);
		select(fd + 1, &readable, nullptr, nullptr, &tv);
	}
	XNextEvent(gDisplay, e);
	return true;
}


int
main(int argc, char** argv)
{
	const char* name = "Newton";
	double timeout = 60;
	const char* file = nullptr;
	char** program = nullptr;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--name") == 0 && i + 1 < argc)
			name = argv[++i];
		else if (strcmp(argv[i], "--timeout") == 0 && i + 1 < argc)
			timeout = atof(argv[++i]);
		else if (strcmp(argv[i], "--") == 0)
		{
			program = argv + i + 1;
			break;
		}
		else if (file == nullptr)
			file = argv[i];
		else
			return Fail("usage: xdnddrop [--name NAME] [--timeout S] FILE [-- PROGRAM ARGS...]");
	}
	if (file == nullptr)
		return Fail("usage: xdnddrop [--name NAME] [--timeout S] FILE [-- PROGRAM ARGS...]");
	char path[4096];
	if (realpath(file, path) == nullptr)
		return Fail("no such file to drop");

	if (program != nullptr && program[0] != nullptr)
	{
		gChild = fork();
		if (gChild == 0)
		{
			execvp(program[0], program);
			_exit(127);
		}
	}

	gDisplay = XOpenDisplay(nullptr);
	if (gDisplay == nullptr)
	{
		Fail("no X display: skipped");
		return 77;					// (ctest's SKIP_RETURN_CODE)
	}
	aAware = XInternAtom(gDisplay, "XdndAware", False);
	aEnter = XInternAtom(gDisplay, "XdndEnter", False);
	aPosition = XInternAtom(gDisplay, "XdndPosition", False);
	aStatus = XInternAtom(gDisplay, "XdndStatus", False);
	aDrop = XInternAtom(gDisplay, "XdndDrop", False);
	aFinished = XInternAtom(gDisplay, "XdndFinished", False);
	aActionCopy = XInternAtom(gDisplay, "XdndActionCopy", False);
	aSelection = XInternAtom(gDisplay, "XdndSelection", False);
	aUriList = XInternAtom(gDisplay, "text/uri-list", False);
	aNetName = XInternAtom(gDisplay, "_NET_WM_NAME", False);
	aUtf8 = XInternAtom(gDisplay, "UTF8_STRING", False);
	Window root = DefaultRootWindow(gDisplay);

	// the target, once it is up
	double deadline = Now() + timeout;
	Window target = 0;
	while ((target = FindTarget(root, name, 0)) == 0)
	{
		if (Now() > deadline)
			return Fail("no window to drop onto");
		if (gChild > 0 && waitpid(gChild, nullptr, WNOHANG) == gChild)
		{
			gChild = 0;
			return Fail("the program ended before its window came up");
		}
		usleep(200 * 1000);
	}
	// (a moment for the window to be mapped and its loop to run)
	usleep(500 * 1000);

	// our own window: the source the target answers, and the selection's owner
	Window source = XCreateSimpleWindow(gDisplay, root, 0, 0, 1, 1, 0, 0, 0);
	XSetSelectionOwner(gDisplay, aSelection, source, CurrentTime);
	XWindowAttributes attrs;
	XGetWindowAttributes(gDisplay, target, &attrs);
	int x = 0, y = 0;
	Window child;
	XTranslateCoordinates(gDisplay, target, root, attrs.width / 2, attrs.height / 2, &x, &y, &child);

	Send(target, aEnter, (long) source, 5L << 24, (long) aUriList, 0, 0);
	Send(target, aPosition, (long) source, 0, ((long) x << 16) | (y & 0xFFFF), CurrentTime, (long) aActionCopy);
	deadline = Now() + 10;
	bool accepted = false, finished = false, dropped = false;
	char uris[4200];
	snprintf(uris, sizeof(uris), "file://");
	// (the path's characters escaped where a URI needs it)
	size_t n = strlen(uris);
	for (const char* p = path; *p != 0 && n + 4 < sizeof(uris); p++)
	{
		unsigned char c = (unsigned char) *p;
		if (c <= ' ' || c == '%' || c == '#' || c >= 0x7f)
			n += (size_t) snprintf(uris + n, sizeof(uris) - n, "%%%02X", c);
		else
			uris[n++] = (char) c;
	}
	snprintf(uris + n, sizeof(uris) - n, "\r\n");

	XEvent e;
	while (!finished && NextEvent(&e, deadline))
	{
		if (e.type == ClientMessage && e.xclient.message_type == aStatus && !dropped)
		{
			if ((e.xclient.data.l[1] & 1) == 0)
				return Fail("the window refused the drag");
			Send(target, aDrop, (long) source, 0, CurrentTime, 0, 0);
			dropped = true;
		}
		else if (e.type == SelectionRequest)
		{
			XSelectionRequestEvent& r = e.xselectionrequest;
			XEvent answer;
			memset(&answer, 0, sizeof(answer));
			answer.xselection.type = SelectionNotify;
			answer.xselection.display = r.display;
			answer.xselection.requestor = r.requestor;
			answer.xselection.selection = r.selection;
			answer.xselection.target = r.target;
			answer.xselection.time = r.time;
			answer.xselection.property = None;
			if (r.target == aUriList)
			{
				XChangeProperty(gDisplay, r.requestor, r.property, aUriList, 8, PropModeReplace,
								(unsigned char*) uris, (int) strlen(uris));
				answer.xselection.property = r.property;
			}
			XSendEvent(gDisplay, r.requestor, False, NoEventMask, &answer);
			XFlush(gDisplay);
		}
		else if (e.type == ClientMessage && e.xclient.message_type == aFinished)
		{
			finished = true;
			accepted = (e.xclient.data.l[1] & 1) != 0;
		}
	}
	if (!finished)
		return Fail(dropped ? "the drop was never finished" : "the window never answered the drag");
	printf("xdnddrop: dropped %s onto \"%s\" (0x%lx): %s\n", path, name, (unsigned long) target, accepted ? "accepted" : "refused");
	fflush(stdout);
	XDestroyWindow(gDisplay, source);
	XCloseDisplay(gDisplay);
	if (!accepted)
		return Fail("the drop was refused");

	if (gChild > 0)
	{
		int status = 0;
		waitpid(gChild, &status, 0);
		return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
	}
	return 0;
}
