/*
	File:		host/remarkable/Folio.cpp

	Contains:	The type folio read from its own input device (Folio.h).
*/

#include "Folio.h"
#include "LinuxKeys.h"
#include <linux/input.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>

#define nil 0

static long long
NowMs(void)
{
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return (long long) t.tv_sec * 1000 + t.tv_nsec / 1000000;
}


// Is this input device a keyboard?  The folio by its name; any other with a
// row of letter keys and LEDs (a keyboard on a USB adapter, the folio
// under another name) as well - not the power button, the Marker or the
// glass, which have neither.
static bool
IsKeyboard(int fd, char* name, size_t nameSize)
{
	name[0] = 0;
	ioctl(fd, EVIOCGNAME(nameSize), name);
	if (strstr(name, "Keyboard") != nil || strstr(name, "keyboard") != nil)
		return true;
	const size_t bits = 8 * sizeof(long);
	unsigned long keys[(KEY_MAX + 8 * sizeof(long)) / (8 * sizeof(long))];
	memset(keys, 0, sizeof(keys));
	unsigned long types = 0;
	if (ioctl(fd, EVIOCGBIT(0, sizeof(types)), &types) < 0 || (types & (1UL << EV_LED)) == 0)
		return false;
	ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
	for (int code = KEY_Q; code <= KEY_P; code++)
		if (((keys[code / bits] >> (code % bits)) & 1) == 0)
			return false;
	return true;
}


// The keyboard's device opened (non-blocking), or -1
static int
OpenKeyboard(char* name, size_t nameSize, char* path, size_t pathSize)
{
	for (int i = 0; i < 32; i++)
	{
		snprintf(path, pathSize, "/dev/input/event%d", i);
		int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (fd < 0)
			continue;
		if (IsKeyboard(fd, name, nameSize))
			return fd;
		close(fd);
	}
	return -1;
}


bool
RemarkableFolioAttached(void)
{
	char name[128], path[32];
	int fd = OpenKeyboard(name, sizeof(name), path, sizeof(path));
	if (fd < 0)
		return false;
	close(fd);
	return true;
}


void
RemarkableFolio::Look(void)
{
	if (!fStarted)
	{
		fStarted = true;
		const char* how = getenv("NEWTON_RM_KEYBOARD");
		fOff = how != nil && strcmp(how, "off") == 0;
		const char* grab = getenv("NEWTON_RM_KEYBOARD_GRAB");
		fGrab = grab == nil || strcmp(grab, "0") != 0;
	}
	if (fOff || fFd >= 0)
		return;
	long long now = NowMs();
	if (fLastLook != 0 && now - fLastLook < 1000)
		return;
	fLastLook = now;
	char name[128], path[32];
	fFd = OpenKeyboard(name, sizeof(name), path, sizeof(path));
	if (fFd < 0)
		return;
	bool grabbed = fGrab && ioctl(fFd, EVIOCGRAB, (void*) 1) == 0;
	fprintf(stderr, "[host] reMarkable: the keyboard '%s' on %s%s\n", name, path, grabbed ? ", taken from xochitl" : "");
}


bool
RemarkableFolio::Read(RemarkableEvent* event)
{
	if (fFd < 0)
		return false;
	struct input_event e;
	for (;;)
	{
		ssize_t got = read(fFd, &e, sizeof(e));
		if (got != (ssize_t) sizeof(e))
		{
			if (got < 0 && errno != EAGAIN && errno != EINTR)
			{
				fprintf(stderr, "[host] reMarkable: the keyboard went (%d)\n", errno);
				Close();							// detached: looked for again in a second
			}
			return false;
		}
		// a key down (1) or up (0); a repeat (2) is the Newton's own to make
		if (e.type != EV_KEY || e.value == 2)
			continue;
		long key = VirtualKeyForLinuxKey(e.code);
		if (key < 0)
			continue;
		event->kind = e.value != 0 ? RemarkableEvent::kKeyDown : RemarkableEvent::kKeyUp;
		event->key = key;
		event->x = event->y = 0;
		event->id = 0;
		return true;
	}
}


void
RemarkableFolio::Close(void)
{
	if (fFd >= 0)
	{
		ioctl(fFd, EVIOCGRAB, (void*) 0);
		close(fFd);
		fFd = -1;
		fLastLook = NowMs();
	}
}
