/*
	File:		host/cocoa/HostWindow.mm

	Contains:	The host's window on macOS (HostWindow.h): AppKit, the
				third implementation beside host/win32/HostWindow.cpp and
				host/x11/HostWindow.cpp, answering the same calls.

	The one real difference from the other two: AppKit must have the
	process's main thread, where they run their window on a thread of their
	own.  So newton hands the machine to HostWindowRunMachine, which runs it
	on a thread of its own (with the main thread's stack size: the boot
	context becomes the idle task there) and gives the main thread to
	[NSApp run] until the machine has ended.  HostWindowStart and
	HostWindowStop, which the machine's thread calls, have the main thread
	open and close the window.  The main thread is then none of the
	machine's (HostWindowThreadStarted): everything it does - the pen, the
	keys, a dropped package - is a plain memory write through the shims.

	The display is shown as a layer's contents: each refresh (thirty times
	a second, when the display has changed) the display's bytes are copied
	and made a CGImage in an indexed colour space - the grays, 0 white to
	255 black, or HostWindowSetPalette's colours - which the layer scales
	to the window nearest-neighbour, so a Retina screen shows the Newton's
	pixels sharp.  The mouse is the pen, in eighths of a display pixel
	(HostWindowPenDownFine: a Retina mouse is finer than the display).  A
	key is handed over as its Windows virtual key code, as on the other
	hosts; a Mac key code is the ADB code the Newton keyboard sends, so the
	table below is host/HostKeyboard.cpp's the other way round.

	A package dropped onto the window, opened with File > Install Package,
	or opened in the Finder (Newton.app's document type .nspkg, or Open
	With on a .pkg) is installed (HostWindowFileDropped).  Closing the
	window, or Quit, ends the run as on the other hosts (HostWindowClosed).
*/

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>
#include <Carbon/Carbon.h>			// (the kVK_ key codes)

#include "HostWindow.h"

#include <atomic>
#include <pthread.h>
#include <sys/resource.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>
#include <stdlib.h>

// the pen and the keyboard: C-linkage shims over hal/host/HostTablet.h
// and host/HostKeyboard.h (HostKeyboard.cpp), the Newton headers kept out
extern "C" {
void	HostWindowPenDown(long x, long y);
void	HostWindowPenMove(long x, long y);
void	HostWindowPenUp(void);
void	HostWindowPenDownFine(long x8, long y8);		// eighths of a display pixel
void	HostWindowPenMoveFine(long x8, long y8);
void	HostWindowKey(long virtualKey, int down);
void	HostWindowClosed(void);
void	HostWindowThreadStarted(void);
void	HostWindowFileDropped(const char* path);		// host/HostPackages.cpp: a package to install
}

static std::atomic<long>	gPositionX(LONG_MIN), gPositionY(LONG_MIN);	// where it is (or is to open): the frame's top left, from the top of the main screen

static long					gWidth = 0;			// the display's size
static long					gHeight = 0;
static const unsigned char*	gPixels = nullptr;
static long					gScale = 1;
static char					gTitle[128];
static std::atomic<bool>	gPenDown(false);
static unsigned char		gPalette[256 * 3];		// HostWindowSetPalette's colours
static std::atomic<int>		gPaletteVersion(0);		// changed with each palette (or its taking away)
static std::atomic<bool>	gHasPalette(false);

static std::atomic<bool>	gApplication(false);	// HostWindowRunMachine has the main thread
static std::atomic<bool>	gMachineEnded(false);
static bool					gTerminateAsked = false;	// (main thread) Quit asked while the machine ran
static int					gMachineStatus = 0;
static int					(*gMachine)(void) = nullptr;

@class NewtonView;
static NSWindow*			gWindow = nil;			// (main thread)
static NewtonView*			gView = nil;
static NSTimer*				gTimer = nil;


void
HostWindowSetPalette(const unsigned char* rgb)
{
	if (rgb != nullptr)
		memcpy(gPalette, rgb, sizeof(gPalette));
	gHasPalette.store(rgb != nullptr);
	gPaletteVersion.fetch_add(1);
}


/*------------------------------------------------------------------------------
	A Mac key as its Windows virtual key code, which is what
	host/HostKeyboard.cpp's HostKeyCodeForVirtualKey names the Newton's
	(ADB) key codes by.  A Mac's key codes are the ADB codes, so this is
	that map the other way round - by position (the Newton applies its own
	keyboard layout to the codes, as it does to a Newton keyboard's) - plus
	the keys it has no code for that the other windows hand over: the
	keypad's digits and Enter, the right-hand modifiers, Control (the
	Newton's Command, as a PC's Ctrl is), and F11 and F12 (the backlight
	and the power switch).
------------------------------------------------------------------------------*/

static long
VirtualKeyForKeyCode(unsigned short code)
{
	switch (code)
	{
	case kVK_ANSI_A: return 'A';	case kVK_ANSI_B: return 'B';	case kVK_ANSI_C: return 'C';
	case kVK_ANSI_D: return 'D';	case kVK_ANSI_E: return 'E';	case kVK_ANSI_F: return 'F';
	case kVK_ANSI_G: return 'G';	case kVK_ANSI_H: return 'H';	case kVK_ANSI_I: return 'I';
	case kVK_ANSI_J: return 'J';	case kVK_ANSI_K: return 'K';	case kVK_ANSI_L: return 'L';
	case kVK_ANSI_M: return 'M';	case kVK_ANSI_N: return 'N';	case kVK_ANSI_O: return 'O';
	case kVK_ANSI_P: return 'P';	case kVK_ANSI_Q: return 'Q';	case kVK_ANSI_R: return 'R';
	case kVK_ANSI_S: return 'S';	case kVK_ANSI_T: return 'T';	case kVK_ANSI_U: return 'U';
	case kVK_ANSI_V: return 'V';	case kVK_ANSI_W: return 'W';	case kVK_ANSI_X: return 'X';
	case kVK_ANSI_Y: return 'Y';	case kVK_ANSI_Z: return 'Z';
	case kVK_ANSI_0: case kVK_ANSI_Keypad0:	return '0';
	case kVK_ANSI_1: case kVK_ANSI_Keypad1:	return '1';
	case kVK_ANSI_2: case kVK_ANSI_Keypad2:	return '2';
	case kVK_ANSI_3: case kVK_ANSI_Keypad3:	return '3';
	case kVK_ANSI_4: case kVK_ANSI_Keypad4:	return '4';
	case kVK_ANSI_5: case kVK_ANSI_Keypad5:	return '5';
	case kVK_ANSI_6: case kVK_ANSI_Keypad6:	return '6';
	case kVK_ANSI_7: case kVK_ANSI_Keypad7:	return '7';
	case kVK_ANSI_8: case kVK_ANSI_Keypad8:	return '8';
	case kVK_ANSI_9: case kVK_ANSI_Keypad9:	return '9';
	case kVK_Return: case kVK_ANSI_KeypadEnter:	return 0x0d;
	case kVK_Tab:					return 0x09;
	case kVK_Space:					return 0x20;
	case kVK_Delete:				return 0x08;		// (the Mac's delete is a backspace)
	case kVK_Escape:				return 0x1b;
	case kVK_Command: case kVK_RightCommand:
	case kVK_Control: case kVK_RightControl:	return 0x11;
	case kVK_Shift: case kVK_RightShift:		return 0x10;
	case kVK_CapsLock:				return 0x14;
	case kVK_Option: case kVK_RightOption:		return 0x12;
	case kVK_LeftArrow:				return 0x25;
	case kVK_UpArrow:				return 0x26;
	case kVK_RightArrow:			return 0x27;
	case kVK_DownArrow:				return 0x28;
	case kVK_ForwardDelete:			return 0x2e;
	case kVK_Home:					return 0x24;
	case kVK_End:					return 0x23;
	case kVK_PageUp:				return 0x21;
	case kVK_PageDown:				return 0x22;
	case kVK_F11:					return 0x7a;
	case kVK_F12:					return 0x7b;
	// the punctuation (the US layout's VK_OEM_* keys)
	case kVK_ANSI_Comma:			return 0xbc;
	case kVK_ANSI_Period:			return 0xbe;
	case kVK_ANSI_Slash:			return 0xbf;
	case kVK_ANSI_Minus:			return 0xbd;
	case kVK_ANSI_Equal:			return 0xbb;
	case kVK_ANSI_LeftBracket:		return 0xdb;
	case kVK_ANSI_RightBracket:		return 0xdd;
	case kVK_ANSI_Backslash:		return 0xdc;
	case kVK_ANSI_Semicolon:		return 0xba;
	case kVK_ANSI_Quote:			return 0xde;
	case kVK_ANSI_Grave:			return 0xc0;
	}
	return -1;
}


// the modifier flag a modifier key's press sets (0: not a modifier)
static NSEventModifierFlags
FlagForModifierKey(unsigned short code)
{
	switch (code)
	{
	case kVK_Command: case kVK_RightCommand:	return NSEventModifierFlagCommand;
	case kVK_Control: case kVK_RightControl:	return NSEventModifierFlagControl;
	case kVK_Shift: case kVK_RightShift:		return NSEventModifierFlagShift;
	case kVK_Option: case kVK_RightOption:		return NSEventModifierFlagOption;
	case kVK_CapsLock:							return NSEventModifierFlagCapsLock;
	}
	return 0;
}


/*------------------------------------------------------------------------------
	The view: the display as its layer's contents, the mouse, the keys and
	the drops.
------------------------------------------------------------------------------*/

@interface NewtonView : NSView
{
@public
	CGImageRef		fImage;				// the image the layer was last given
	unsigned char*	fShown;				// the display's bytes as last shown
	int				fShownVersion;		// and the palette they were shown in
	CGColorSpaceRef	fColours;			// the 256 entries: grays, or the palette
	BOOL			fCapsDown;
}
- (void)refresh;
@end

@implementation NewtonView

- (instancetype)initWithFrame:(NSRect)frame
{
	self = [super initWithFrame:frame];
	if (self != nil)
	{
		fShown = (unsigned char*) calloc((size_t) (gWidth * gHeight), 1);
		fShownVersion = -1;
		fColours = nullptr;
		fImage = nullptr;
		self.wantsLayer = YES;
		self.layer.magnificationFilter = kCAFilterNearest;
		self.layer.minificationFilter = kCAFilterNearest;
		self.layer.contentsGravity = kCAGravityResize;
		self.layer.backgroundColor = CGColorGetConstantColor(kCGColorWhite);
		[self registerForDraggedTypes:@[ NSPasteboardTypeFileURL ]];
	}
	return self;
}

- (void)dealloc
{
	free(fShown);
	if (fImage != nullptr)
		CGImageRelease(fImage);
	if (fColours != nullptr)
		CGColorSpaceRelease(fColours);
}

- (BOOL)isFlipped					{ return YES; }
- (BOOL)acceptsFirstResponder		{ return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent*)event	{ (void) event; return YES; }
- (BOOL)wantsUpdateLayer			{ return YES; }
- (void)updateLayer					{ }

// The display shown, if it (or its colours) changed since it last was
- (void)refresh
{
	const unsigned char* pixels = gPixels;
	if (pixels == nullptr)
		return;
	size_t size = (size_t) (gWidth * gHeight);
	int version = gPaletteVersion.load();
	if (version == fShownVersion && memcmp(fShown, pixels, size) == 0)
		return;
	memcpy(fShown, pixels, size);
	if (version != fShownVersion || fColours == nullptr)
	{
		fShownVersion = version;
		unsigned char table[256 * 3];
		if (gHasPalette.load())
			memcpy(table, gPalette, sizeof(table));		// (a colour screen: each value its palette entry - qd/Colour.h)
		else
			for (int i = 0; i < 256; i++)
				table[i * 3] = table[i * 3 + 1] = table[i * 3 + 2] = (unsigned char) (255 - i);	// the display's 0 is white, 255 black
		CGColorSpaceRef rgb = CGColorSpaceCreateDeviceRGB();
		if (fColours != nullptr)
			CGColorSpaceRelease(fColours);
		fColours = CGColorSpaceCreateIndexed(rgb, 255, table);
		CGColorSpaceRelease(rgb);
	}
	// (the image gets a copy of its own: the layer may read it later)
	CFDataRef bytes = CFDataCreate(nullptr, fShown, (CFIndex) size);
	CGDataProviderRef provider = CGDataProviderCreateWithCFData(bytes);
	CGImageRef image = CGImageCreate((size_t) gWidth, (size_t) gHeight, 8, 8, (size_t) gWidth, fColours,
									 kCGImageAlphaNone, provider, nullptr, false, kCGRenderingIntentDefault);
	[CATransaction begin];
	[CATransaction setDisableActions:YES];			// (no cross-fade between frames)
	self.layer.contents = (__bridge id) image;
	[CATransaction commit];
	if (fImage != nullptr)
		CGImageRelease(fImage);
	fImage = image;
	CGDataProviderRelease(provider);
	CFRelease(bytes);
}

// A point of the view in eighths of a display pixel, kept on the display
- (void)penAt:(NSEvent*)event x8:(long*)x8 y8:(long*)y8
{
	NSPoint p = [self convertPoint:event.locationInWindow fromView:nil];
	NSSize size = self.bounds.size;
	long x = size.width > 0 ? (long) (p.x * gWidth * 8 / size.width) : 0;
	long y = size.height > 0 ? (long) (p.y * gHeight * 8 / size.height) : 0;
	*x8 = x < 0 ? 0 : x >= gWidth * 8 ? gWidth * 8 - 1 : x;
	*y8 = y < 0 ? 0 : y >= gHeight * 8 ? gHeight * 8 - 1 : y;
}

- (void)mouseDown:(NSEvent*)event
{
	long x8, y8;
	[self penAt:event x8:&x8 y8:&y8];
	gPenDown.store(true);
	HostWindowPenDownFine(x8, y8);
}

- (void)mouseDragged:(NSEvent*)event
{
	if (!gPenDown.load())
		return;
	long x8, y8;
	[self penAt:event x8:&x8 y8:&y8];
	HostWindowPenMoveFine(x8, y8);
}

- (void)mouseUp:(NSEvent*)event
{
	(void) event;
	if (gPenDown.exchange(false))
		HostWindowPenUp();
}

// (only the first press is sent: the Newton repeats its own keys)
- (void)keyDown:(NSEvent*)event
{
	if (event.isARepeat)
		return;
	long vk = VirtualKeyForKeyCode(event.keyCode);
	if (vk >= 0)
		HostWindowKey(vk, 1);
}

- (void)keyUp:(NSEvent*)event
{
	long vk = VirtualKeyForKeyCode(event.keyCode);
	if (vk >= 0)
		HostWindowKey(vk, 0);
}

// A modifier pressed or let go.  Caps Lock reports only its locking and
// unlocking: each is a press and a release, as a PC's key sends.
- (void)flagsChanged:(NSEvent*)event
{
	NSEventModifierFlags flag = FlagForModifierKey(event.keyCode);
	long vk = VirtualKeyForKeyCode(event.keyCode);
	if (flag == 0 || vk < 0)
		return;
	if (flag == NSEventModifierFlagCapsLock)
	{
		HostWindowKey(vk, 1);
		HostWindowKey(vk, 0);
		return;
	}
	HostWindowKey(vk, (event.modifierFlags & flag) != 0 ? 1 : 0);
}

// Files dragged onto the window: each one installed
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender
{
	(void) sender;
	return NSDragOperationCopy;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender
{
	NSArray<NSURL*>* urls = [sender.draggingPasteboard readObjectsForClasses:@[ [NSURL class] ]
																	 options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
	for (NSURL* url in urls)
		HostWindowFileDropped(url.fileSystemRepresentation);
	return urls.count > 0;
}

@end


/*------------------------------------------------------------------------------
	The application: its delegate (the files the Finder opens, Quit), the
	window's delegate (closed, moved) and the menus.
------------------------------------------------------------------------------*/

@interface NewtonApplication : NSObject <NSApplicationDelegate, NSWindowDelegate>
- (void)installPackage:(id)sender;
@end

// Where the window is: its frame's top left, measured from the main
// screen's top left (y down, as the other hosts' positions are)
static void
NotePosition(NSWindow* window)
{
	NSScreen* screen = NSScreen.screens.firstObject;
	if (window == nil || screen == nil)
		return;
	NSRect frame = window.frame;
	gPositionX.store((long) frame.origin.x);
	gPositionY.store((long) (NSMaxY(screen.frame) - NSMaxY(frame)));
}

@implementation NewtonApplication

- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls
{
	(void) application;
	for (NSURL* url in urls)
		if (url.isFileURL)
		{
			fprintf(stderr, "[host] %s: opened in the Finder, to install\n", url.fileSystemRepresentation);
			HostWindowFileDropped(url.fileSystemRepresentation);
		}
}

// Quit (or the Dock's, or logging out) ends the run as closing the window
// does; the application is let go once the machine has ended
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)application
{
	(void) application;
	if (gMachineEnded.load())
		return NSTerminateNow;
	gTerminateAsked = true;
	HostWindowClosed();
	return NSTerminateLater;
}

- (void)windowWillClose:(NSNotification*)notification
{
	(void) notification;
	NotePosition(gWindow);
	if (!gMachineEnded.load())
		HostWindowClosed();
}

- (void)windowDidMove:(NSNotification*)notification
{
	NotePosition((NSWindow*) notification.object);
}

- (void)installPackage:(id)sender
{
	(void) sender;
	NSOpenPanel* panel = [NSOpenPanel openPanel];
	panel.allowsMultipleSelection = YES;
	panel.canChooseDirectories = NO;
	panel.message = @"Packages to install on the Newton";
	if ([panel runModal] == NSModalResponseOK)
		for (NSURL* url in panel.URLs)
			HostWindowFileDropped(url.fileSystemRepresentation);
}

- (void)refresh:(NSTimer*)timer
{
	(void) timer;
	[gView refresh];
}

@end

static NewtonApplication*	gDelegate = nil;


static void
BuildMenus(void)
{
	NSString* name = [NSProcessInfo processInfo].processName;
	NSMenu* bar = [[NSMenu alloc] init];

	NSMenuItem* appItem = [[NSMenuItem alloc] init];
	NSMenu* appMenu = [[NSMenu alloc] init];
	[appMenu addItemWithTitle:[@"About " stringByAppendingString:name]
					   action:@selector(orderFrontStandardAboutPanel:) keyEquivalent:@""];
	[appMenu addItem:[NSMenuItem separatorItem]];
	[appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
	NSMenuItem* others = [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
	others.keyEquivalentModifierMask = NSEventModifierFlagCommand | NSEventModifierFlagOption;
	[appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
	[appMenu addItem:[NSMenuItem separatorItem]];
	[appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
	appItem.submenu = appMenu;
	[bar addItem:appItem];

	NSMenuItem* fileItem = [[NSMenuItem alloc] init];
	NSMenu* fileMenu = [[NSMenu alloc] initWithTitle:@"File"];
	NSMenuItem* install = [fileMenu addItemWithTitle:@"Install Package…" action:@selector(installPackage:) keyEquivalent:@"o"];
	install.target = gDelegate;
	[fileMenu addItem:[NSMenuItem separatorItem]];
	[fileMenu addItemWithTitle:@"Close Window" action:@selector(performClose:) keyEquivalent:@"w"];
	fileItem.submenu = fileMenu;
	[bar addItem:fileItem];

	NSMenuItem* windowItem = [[NSMenuItem alloc] init];
	NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
	[windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
	windowItem.submenu = windowMenu;
	[bar addItem:windowItem];
	NSApp.windowsMenu = windowMenu;

	NSApp.mainMenu = bar;
}


/*------------------------------------------------------------------------------
	The machine on a thread of its own, the application on the main thread.
------------------------------------------------------------------------------*/

// The machine has ended: the application stops (or, Quit having been
// asked, is let go), and HostWindowRunMachine answers the machine's status
static void
MachineEnded(void)
{
	gMachineEnded.store(true);
	if (gTerminateAsked)
	{
		[NSApp replyToApplicationShouldTerminate:YES];
		return;
	}
	[NSApp stop:nil];
	// ([NSApp stop:] is noticed after the next event: one is posted)
	NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint
								  modifierFlags:0 timestamp:0 windowNumber:0 context:nil subtype:0 data1:0 data2:0];
	[NSApp postEvent:wake atStart:NO];
}


static void*
MachineThread(void*)
{
	@autoreleasepool
	{
		gMachineStatus = gMachine();
	}
	dispatch_async(dispatch_get_main_queue(), ^{ MachineEnded(); });
	return nullptr;
}


int
HostWindowRunMachine(int (*machine)(void), bool windowed)
{
	if (!windowed)
		return machine();
	@autoreleasepool
	{
		[NSApplication sharedApplication];
		NSApp.activationPolicy = NSApplicationActivationPolicyRegular;
		gDelegate = [[NewtonApplication alloc] init];
		NSApp.delegate = gDelegate;
		BuildMenus();
		gApplication.store(true);

		// the machine's thread, with the stack the main thread would have
		// given the boot (it becomes the idle task: GetStackBounds)
		gMachine = machine;
		struct rlimit limit;
		size_t stack = 8 << 20;
		if (getrlimit(RLIMIT_STACK, &limit) == 0 && limit.rlim_cur != RLIM_INFINITY && limit.rlim_cur > stack)
			stack = (size_t) limit.rlim_cur;
		pthread_attr_t attr;
		pthread_attr_init(&attr);
		pthread_attr_setstacksize(&attr, stack);
		pthread_t thread;
		if (pthread_create(&thread, &attr, MachineThread, nullptr) != 0)
		{
			fprintf(stderr, "[host] the machine's thread could not be made\n");
			return 1;
		}
		pthread_attr_destroy(&attr);

		if (@available(macOS 14, *))
			[NSApp activate];
		else
			[NSApp activateIgnoringOtherApps:YES];
		[NSApp run];
		pthread_join(thread, nullptr);
		gApplication.store(false);
	}
	return gMachineStatus;
}


bool
HostWindowOwnsProcess(void)
{
	return gApplication.load();
}


/*------------------------------------------------------------------------------
	The window, opened and closed for the machine's thread
------------------------------------------------------------------------------*/

static void
OpenWindow(void)
{
	// the main thread is none of the machine's from here (HostKeyboard.cpp)
	static bool started = false;
	if (!started)
	{
		started = true;
		HostWindowThreadStarted();
	}
	NSRect content = NSMakeRect(0, 0, gWidth * gScale, gHeight * gScale);
	gWindow = [[NSWindow alloc] initWithContentRect:content
										  styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable
													| NSWindowStyleMaskResizable
											backing:NSBackingStoreBuffered defer:NO];
	gWindow.releasedWhenClosed = NO;
	gWindow.title = [NSString stringWithUTF8String:gTitle];
	gWindow.delegate = gDelegate;
	gWindow.contentAspectRatio = content.size;		// (it may be made bigger or smaller, the display's shape kept)
	gWindow.contentMinSize = NSMakeSize(gWidth / 2, gHeight / 2);
	gView = [[NewtonView alloc] initWithFrame:content];
	gWindow.contentView = gView;
	gWindow.initialFirstResponder = gView;
	long x = gPositionX.load(), y = gPositionY.load();
	NSScreen* screen = NSScreen.screens.firstObject;
	if (x != LONG_MIN && screen != nil)
	{
		// (a restarted newton's window where the old one was)
		[gWindow setFrameTopLeftPoint:NSMakePoint(x, NSMaxY(screen.frame) - y)];
		fprintf(stderr, "[host] window asked to open at %ld,%ld\n", x, y);
	}
	else
		[gWindow center];
	[gView refresh];
	[gWindow makeKeyAndOrderFront:nil];
	gTimer = [NSTimer timerWithTimeInterval:1.0 / 30 target:gDelegate selector:@selector(refresh:) userInfo:nil repeats:YES];
	[[NSRunLoop mainRunLoop] addTimer:gTimer forMode:NSRunLoopCommonModes];		// (refreshed while a menu is open too)
}


// NEWTON_WINDOW_SNAPSHOT=<file>: the image the window showed last written
// as it closes, as a PGM (255 white) drawn back through a gray context - so
// that what the window's colour space made of the display can be compared
// with newton's own ScreenSnapshot of it
static void
WriteSnapshot(NewtonView* view)
{
	const char* path = getenv("NEWTON_WINDOW_SNAPSHOT");
	if (path == nullptr || view == nil || view->fImage == nullptr)
		return;
	size_t width = CGImageGetWidth(view->fImage), height = CGImageGetHeight(view->fImage);
	unsigned char* gray = (unsigned char*) calloc(width * height, 1);
	CGColorSpaceRef space = CGColorSpaceCreateDeviceGray();
	CGContextRef context = CGBitmapContextCreate(gray, width, height, 8, width, space, kCGImageAlphaNone);
	CGColorSpaceRelease(space);
	FILE* f = context != nullptr ? fopen(path, "wb") : nullptr;
	if (f != nullptr)
	{
		CGContextSetInterpolationQuality(context, kCGInterpolationNone);
		CGContextDrawImage(context, CGRectMake(0, 0, width, height), view->fImage);
		fprintf(f, "P5\n%zu %zu\n255\n", width, height);
		fwrite(gray, 1, width * height, f);
		fclose(f);
		fprintf(stderr, "[host] window snapshot written to %s\n", path);
	}
	if (context != nullptr)
		CGContextRelease(context);
	free(gray);
}


static void
CloseWindow(void)
{
	[gTimer invalidate];
	gTimer = nil;
	WriteSnapshot(gView);
	if (gWindow != nil)
	{
		NotePosition(gWindow);
		gWindow.delegate = nil;
		[gWindow close];
	}
	gWindow = nil;
	gView = nil;
}


bool
HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale)
{
	if (!gApplication.load())
		return false;				// (not run through HostWindowRunMachine: no application)
	gWidth = width;
	gHeight = height;
	gPixels = pixels;
	gScale = scale < 1 ? 1 : scale;
	strncpy(gTitle, title, sizeof(gTitle) - 1);
	gTitle[sizeof(gTitle) - 1] = 0;
	if ([NSThread isMainThread])
		OpenWindow();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ OpenWindow(); });
	return true;
}


void
HostWindowStop(void)
{
	if (!gApplication.load())
		return;
	if ([NSThread isMainThread])
		CloseWindow();
	else
		dispatch_sync(dispatch_get_main_queue(), ^{ CloseWindow(); });
}


// (the shims called as the event loop would call them, as on X11)
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


// a desktop window is the shape the display is
void
HostWindowPreferredDisplay(long* width, long* height)
{
	(void) width;
	(void) height;
}


// a desktop window has none of the panel's settings
bool
HostWindowOption(const char* name, long* value)
{
	(void) name;
	(void) value;
	return false;
}


bool
HostWindowSetOption(const char* name, long value)
{
	(void) name;
	(void) value;
	return false;
}
