/*
	File:		host/remarkable/LinuxKeys.h

	Contains:	A key as Linux numbers it (linux/input-event-codes.h) as a
				Windows virtual key code - what host/HostKeyboard.cpp maps
				to the Newton's, for the type folio read from its own
				input device (Folio.cpp).  The positions are a US keyboard's: the
				Newton's own key map does the rest (Shift and the symbols).
*/

#ifndef __LINUXKEYS_H
#define __LINUXKEYS_H

#include <linux/input-event-codes.h>

static inline long
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
	case KEY_ENTER:
	case KEY_KPENTER:		return 0x0d;
	case KEY_SPACE:			return 0x20;
	case KEY_LEFTCTRL: case KEY_RIGHTCTRL:		return 0x11;
	case KEY_LEFTSHIFT: case KEY_RIGHTSHIFT:	return 0x10;
	case KEY_LEFTALT: case KEY_RIGHTALT:
	case KEY_LEFTMETA: case KEY_RIGHTMETA:		return 0x12;	// the Newton's command key, as Alt is on a PC
	case KEY_CAPSLOCK:		return 0x14;
	case KEY_COMMA:			return 0xbc;
	case KEY_DOT:			return 0xbe;
	case KEY_SLASH:			return 0xbf;
	case KEY_MINUS:			return 0xbd;
	case KEY_EQUAL:			return 0xbb;
	case KEY_LEFTBRACE:		return 0xdb;
	case KEY_RIGHTBRACE:	return 0xdd;
	case KEY_BACKSLASH:		return 0xdc;
	case KEY_SEMICOLON:		return 0xba;
	case KEY_APOSTROPHE:	return 0xde;
	case KEY_GRAVE:			return 0xc0;
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

#endif	/* __LINUXKEYS_H */
