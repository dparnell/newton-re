/*
	File:		host/remarkable/QTFB.h

	Contains:	The qtfb protocol, as AppLoad (github.com/asivery/rm-appload,
				src/qtfb/common.h) speaks it on the reMarkable Paper Pro:
				the client - newton - connects to a SOCK_SEQPACKET Unix
				socket, asks for a framebuffer (the key AppLoad put in
				QTFB_KEY, a pixel format, optionally its own size), is
				answered with the name of a POSIX shared-memory object to
				map, draws into it and says which rectangle to put on the
				glass; AppLoad sends back the pen, the touches and the keys.

				Written here from the protocol, not copied (AppLoad is GPL-3;
				the messages are the interface between two programs).  A
				message is the server's C struct as the aarch64 compiler lays
				it out, so the layouts are pinned by the static_asserts below
				- check them again if AppLoad's common.h changes.
*/

#ifndef __REMARKABLE_QTFB_H
#define __REMARKABLE_QTFB_H

#include <stdint.h>
#include <stddef.h>

#define QTFB_SOCKET_PATH			"/tmp/qtfb.sock"		// (NEWTON_QTFB_SOCKET overrides it: tools/remarkable/qtfbserver.py)
#define QTFB_SHM_NAME_FORMAT		"/qtfb_%d"				// shm_open's name for the key the server answers

// a client's messages
enum
{
	kQTFBInitialize				= 0,
	kQTFBUpdate					= 1,
	kQTFBCustomInitialize		= 2,
	kQTFBTerminate				= 3,
	kQTFBUserInput				= 4,		// (the server's)
	kQTFBSetRefreshMode			= 5,
	kQTFBRequestFullRefresh		= 6,
	kQTFBDeviceStateChanged		= 7,		// (the server's)
	kQTFBDeviceStateInit		= 8
};

// the pixel formats (the size comes with the format unless the custom
// initialisation gives one)
enum
{
	kQTFBFormatRM2FB			= 0,		// RGB565, 1404 x 1872
	kQTFBFormatRMPP_RGB888		= 1,		// 1620 x 2160 (the Paper Pro)
	kQTFBFormatRMPP_RGBA8888	= 2,
	kQTFBFormatRMPP_RGB565		= 3,
	kQTFBFormatRMPPM_RGB888		= 4,		// 954 x 1696 (the Paper Pro Move)
	kQTFBFormatRMPPM_RGBA8888	= 5,
	kQTFBFormatRMPPM_RGB565		= 6
};

enum { kQTFBUpdateAll = 0, kQTFBUpdatePartial = 1 };

// the refresh modes (waveforms), fastest first
enum
{
	kQTFBRefreshUFast			= 0,
	kQTFBRefreshFast			= 1,
	kQTFBRefreshAnimate			= 2,
	kQTFBRefreshContent			= 3,
	kQTFBRefreshUI				= 4
};

// the server's input events
enum
{
	kQTFBTouchPress				= 0x10,
	kQTFBTouchRelease			= 0x11,
	kQTFBTouchUpdate			= 0x12,
	kQTFBPenPress				= 0x20,
	kQTFBPenRelease				= 0x21,
	kQTFBPenUpdate				= 0x22,
	kQTFBButtonPress			= 0x30,		// a key (Qt's key code in x): the type folio, the buttons
	kQTFBButtonRelease			= 0x31,
	kQTFBVirtualKeyPress		= 0x40,		// a key of AppLoad's own on-screen keyboard (Qt's code)
	kQTFBVirtualKeyRelease		= 0x41
};

struct QTFBClientMessage
{
	uint8_t		type;
	union
	{
		struct { int32_t key; uint8_t format; } init;
		struct { int32_t type, x, y, w, h; } update;
		struct { int32_t key; uint8_t format; uint16_t width, height; } customInit;
		int32_t		refreshMode;
	};
};

struct QTFBServerMessage
{
	uint8_t		type;
	union
	{
		struct { int32_t shmKey; size_t shmSize; } init;
		struct { int32_t inputType, devId, x, y, d; } userInput;		// d: the pen's pressure, 0..100
		struct { int32_t reason; int32_t rotation; } deviceState;
	};
};

#if defined(__LP64__) || defined(_LP64)
static_assert(sizeof(QTFBClientMessage) == 24, "qtfb: the client message is AppLoad's 24 bytes on aarch64");
static_assert(offsetof(QTFBClientMessage, customInit.width) == 10, "qtfb: the custom size at +10");
static_assert(sizeof(QTFBServerMessage) == 32, "qtfb: the server message is AppLoad's 32 bytes on aarch64");
static_assert(offsetof(QTFBServerMessage, init.shmSize) == 16, "qtfb: the shared memory's size at +16");
static_assert(offsetof(QTFBServerMessage, userInput.x) == 16, "qtfb: an input's x at +16");
#endif

#endif	/* __REMARKABLE_QTFB_H */
