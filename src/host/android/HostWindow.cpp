/*
	File:		host/android/HostWindow.cpp

	Contains:	The host window (host/HostWindow.h) on an Android tablet - the
				Viwoods AiPaper Mini (docs/host-android.md).  A NativeActivity
				owns the screen: android/AndroidMain.cpp's android_main pumps
				its events on the thread the activity glue gives it and runs
				the machine on a thread of its own, so this file's two halves
				meet in the middle:

				- the display's grays drawn into the activity's ANativeWindow
				  (a render thread, thirty looks a second, a frame drawn only
				  when the display changed - an e-ink panel does not want
				  redrawing for nothing), each display pixel a whole number of
				  screen pixels, centred, the rest white;
				- the stylus (or a finger, or a mouse) as the pen, in eighths
				  of a display pixel (HostWindowPenDownFine and its kin,
				  host/HostKeyboard.cpp), the moves of a stroke between two
				  frames handed over from the event's history.

				No keyboard yet, no sound.
*/

#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <android/native_window.h>
#include <android/input.h>
#include <android/log.h>
#include <jni.h>

#include "HostAndroid.h"
#include "EinkDirect.h"

// the pen and the keyboard: C-linkage shims over hal/host/HostTablet.h
// and host/HostKeyboard.h (HostKeyboard.cpp), the Newton headers kept out
extern "C" {
void	HostWindowPenDownFine(long x8, long y8);
void	HostWindowPenMoveFine(long x8, long y8);
void	HostWindowPenUp(void);
void	HostWindowClosed(void);
void	HostWindowThreadStarted(void);
}

#define LOG(...)	__android_log_print(ANDROID_LOG_INFO, "newton", __VA_ARGS__)

static std::atomic<long>	gPositionX(LONG_MIN), gPositionY(LONG_MIN);

static long					gWidth = 0;			// the display's size
static long					gHeight = 0;
static const unsigned char*	gPixels = nullptr;
static std::thread*			gThread = nullptr;
static std::atomic<bool>	gStopping(false);
static std::atomic<bool>	gPenDown(false);

static unsigned char		gPalette[256 * 3];		// HostWindowSetPalette's colours
static std::atomic<int>		gPaletteVersion(0);
static std::atomic<bool>	gHasPalette(false);

// the activity's window: set and taken away by the event thread, drawn on
// by the render thread, which holds the lock while it does
static std::mutex			gWindowLock;
static ANativeWindow*		gWindow = nullptr;
static std::atomic<int>		gWindowVersion(0);		// changed with each window (or its going)

// where the display is on the screen: scale and the top left of the picture
static std::atomic<long>	gScaleNow(1), gOriginX(0), gOriginY(0);


void
HostWindowSetPalette(const unsigned char* rgb)
{
	if (rgb != nullptr)
		memcpy(gPalette, rgb, sizeof(gPalette));
	gHasPalette.store(rgb != nullptr);
	gPaletteVersion.fetch_add(1);
}


/*------------------------------------------------------------------------------
	The event thread's half: the activity's window, and its input.
------------------------------------------------------------------------------*/

static std::atomic<bool>	gActive(false);

void
HostAndroidSetActive(bool active)
{
	gActive.store(active);
}


bool
HostAndroidActive(void)
{
	return gActive.load();
}


void
HostAndroidSetWindow(ANativeWindow* window)
{
	std::lock_guard<std::mutex> lock(gWindowLock);
	if (gWindow != nullptr)
		ANativeWindow_release(gWindow);
	gWindow = window;
	if (gWindow != nullptr)
		ANativeWindow_acquire(gWindow);
	gWindowVersion.fetch_add(1);
}


// a point of the screen as eighths of a display pixel, kept on the display
static void
PenAt(float x, float y, long* x8, long* y8)
{
	long scale = gScaleNow.load();
	long px = (long) (((double) x - (double) gOriginX.load()) * 8.0 / (double) scale);
	long py = (long) (((double) y - (double) gOriginY.load()) * 8.0 / (double) scale);
	long maxX = gWidth * 8 - 1, maxY = gHeight * 8 - 1;
	*x8 = px < 0 ? 0 : px > maxX ? maxX : px;
	*y8 = py < 0 ? 0 : py > maxY ? maxY : py;
}


// ==> whether the event was taken
int
HostAndroidInput(AInputEvent* event)
{
	if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION || gPixels == nullptr)
		return 0;
	int32_t action = AMotionEvent_getAction(event) & AMOTION_EVENT_ACTION_MASK;
	if (AMotionEvent_getToolType(event, 0) == AMOTION_EVENT_TOOL_TYPE_ERASER)
		return 1;				// (the pen's eraser end is not a pen yet)
	long x8, y8;
	switch (action)
	{
	case AMOTION_EVENT_ACTION_DOWN:
		PenAt(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0), &x8, &y8);
		gPenDown.store(true);
		HostWindowPenDownFine(x8, y8);
		return 1;
	case AMOTION_EVENT_ACTION_MOVE:
		if (!gPenDown.load())
			return 1;
		// (what the stylus did between two events first, so that a quick stroke keeps its shape)
		for (size_t i = 0, n = AMotionEvent_getHistorySize(event); i < n; i++)
		{
			PenAt(AMotionEvent_getHistoricalX(event, 0, i), AMotionEvent_getHistoricalY(event, 0, i), &x8, &y8);
			HostWindowPenMoveFine(x8, y8);
		}
		PenAt(AMotionEvent_getX(event, 0), AMotionEvent_getY(event, 0), &x8, &y8);
		HostWindowPenMoveFine(x8, y8);
		return 1;
	case AMOTION_EVENT_ACTION_UP:
	case AMOTION_EVENT_ACTION_CANCEL:
		if (gPenDown.exchange(false))
			HostWindowPenUp();
		return 1;
	}
	return 1;					// (a hover, a second finger: nothing the pen does)
}


void
HostAndroidDestroyed(void)
{
	gStopping.store(true);
	HostWindowClosed();
}


/*------------------------------------------------------------------------------
	The render thread.
------------------------------------------------------------------------------*/

static uint32_t		gRamp[256];

static void
BuildRamp(void)
{
	for (int i = 0; i < 256; i++)
	{
		if (gHasPalette.load())
			gRamp[i] = 0xff000000u | ((uint32_t) gPalette[i * 3 + 2] << 16) | ((uint32_t) gPalette[i * 3 + 1] << 8) | gPalette[i * 3];
		else
		{
			uint32_t level = (uint32_t) (255 - i);		// the display's 0 is white, 255 black
			gRamp[i] = 0xff000000u | (level << 16) | (level << 8) | level;
		}
	}
}


// the rows [top,bottom) x [left,right) of a width x height picture of the display (RGBA)
static void
PaintRows(uint32_t* bits, long stride, long width, long height, long scale, long originX, long originY,
			long left, long top, long right, long bottom)
{
	long shownW = gWidth * scale < width ? gWidth * scale : width;
	long shownH = gHeight * scale < height ? gHeight * scale : height;
	if (left < 0) left = 0;
	if (right > width) right = width;
	if (top < 0) top = 0;
	if (bottom > height) bottom = height;
	for (long y = top; y < bottom; y++)
	{
		uint32_t* out = bits + y * stride;
		long dy = y - originY;
		if (dy < 0 || dy >= shownH)
		{
			for (long x = left; x < right; x++)
				out[x] = 0xffffffffu;
			continue;
		}
		// (a row is made once and copied to the other rows of the same display row)
		if (dy % scale != 0 && y > top && (y - 1 - originY) / scale == dy / scale)
		{
			memcpy(out + left, out - stride + left, (size_t) (right - left) * sizeof(uint32_t));
			continue;
		}
		const unsigned char* row = gPixels + (dy / scale) * gWidth;
		for (long x = left; x < right; x++)
		{
			long dx = x - originX;
			out[x] = (dx < 0 || dx >= shownW) ? 0xffffffffu : gRamp[row[dx / scale]];
		}
	}
}


// the display drawn into the window, nearest-neighbour, whole pixels.  Only
// the part that changed is drawn (dx0..dx1, dy0..dy1 in display pixels; all
// of it when full): the full 1920x1440 picture took longer than the pen moves,
// and an e-ink panel refreshes just what the window says it touched.
static void
Draw(ANativeWindow* window, bool full, long dx0, long dy0, long dx1, long dy1)
{
	long width = ANativeWindow_getWidth(window), height = ANativeWindow_getHeight(window);
	long scale = width / gWidth < height / gHeight ? width / gWidth : height / gHeight;
	if (scale < 1)
		scale = 1;
	long originX = (width - gWidth * scale) / 2, originY = (height - gHeight * scale) / 2;
	if (originX < 0) originX = 0;
	if (originY < 0) originY = 0;
	gScaleNow.store(scale);
	gOriginX.store(originX);
	gOriginY.store(originY);

	ARect dirty;
	if (full)
		dirty = { 0, 0, (int32_t) width, (int32_t) height };
	else
		dirty = { (int32_t) (originX + dx0 * scale), (int32_t) (originY + dy0 * scale),
					 (int32_t) (originX + (dx1 + 1) * scale), (int32_t) (originY + (dy1 + 1) * scale) };
	ANativeWindow_Buffer buffer;
	if (ANativeWindow_lock(window, &buffer, &dirty) != 0)
		return;
	PaintRows((uint32_t*) buffer.bits, buffer.stride, width, height, scale, originX, originY,
				dirty.left, dirty.top, dirty.right, dirty.bottom);
	ANativeWindow_unlockAndPost(window);
}


// Where the panel has a driver of its own (the Supernote Manta's /dev/ebc,
// EinkDirect.h), what changed goes there at once: through the window it waits
// for Android's compositor and the panel's refresh policy, which is the lag.
// The first picture, and any change of window, still go through the window.
struct DisplayDamage
{
	bool	any = false;
	long	x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	void	add(long ax0, long ay0, long ax1, long ay1)
	{
		if (!any)
			x0 = ax0, y0 = ay0, x1 = ax1, y1 = ay1, any = true;
		else
		{
			if (ax0 < x0) x0 = ax0;
			if (ay0 < y0) y0 = ay0;
			if (ax1 > x1) x1 = ax1;
			if (ay1 > y1) y1 = ay1;
		}
	}
};

static unsigned char
PanelGray(int x, int y, void*)
{
	long scale = gScaleNow.load();
	long dx = (x - gOriginX.load()) / scale, dy = (y - gOriginY.load()) / scale;
	if (x < gOriginX.load() || y < gOriginY.load() || dx >= gWidth || dy >= gHeight)
		return 255;
	return (unsigned char) ((gRamp[gPixels[dy * gWidth + dx]] >> 8) & 0xff);
}

static bool
DirectUsable(ANativeWindow* window)
{
	static bool tried = false, opened = false;
	if (!tried)
	{
		tried = true;
		const char* off = getenv("NEWTON_EINK_DIRECT");
		opened = (off == nullptr || off[0] != '0') && EinkDirectOpen();
	}
	return opened && ANativeWindow_getWidth(window) == EinkDirectWidth() && ANativeWindow_getHeight(window) == EinkDirectHeight();
}

// ==> 0 sent, otherwise kept: busy
static int
SendDirect(const DisplayDamage& d)
{
	long scale = gScaleNow.load();
	int left = (int) (gOriginX.load() + d.x0 * scale), top = (int) (gOriginY.load() + d.y0 * scale);
	int right = (int) (gOriginX.load() + (d.x1 + 1) * scale), bottom = (int) (gOriginY.load() + (d.y1 + 1) * scale);
	int result = EinkDirectPresent(left, top, right, bottom, PanelGray, nullptr);
	return result < 0 ? 0 : result;			// (a failure drops the damage: the window will show it at the next full draw)
}


/*------------------------------------------------------------------------------
	The Viwoods AiPaper Mini's refresh modes (tools/android/java/.../FastInk.java,
	docs/host-android.md): the window is drawn as always; the panel is told to
	refresh it FAST while the pen is down, and to clear the ghosts once the
	pen has been up a while.
------------------------------------------------------------------------------*/

static std::atomic<bool>	gDiag(false);
void HostAndroidSetDiag(bool on) { gDiag.store(on); }

static JavaVM*		gVM = nullptr;
static jobject		gActivityRef = nullptr;
static jobject		gEnote = nullptr;			// the ENoteSetting
static jobject		gSetMode = nullptr;			// its setPictureMode(int), a java.lang.reflect.Method
static bool			gInkOn = false;

// on the activity's own thread (android_main)
void
HostAndroidSetActivity(JavaVM* vm, JNIEnv* env, jobject activity)
{
	gVM = vm;
	gActivityRef = env->NewGlobalRef(activity);
}

struct JavaThread			// the current thread in the VM, for as long as this lives
{
	JNIEnv*		env = nullptr;
	bool		attached = false;
	JavaThread()
	{
		if (gVM != nullptr && gVM->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK)
			attached = gVM->AttachCurrentThread(&env, nullptr) == JNI_OK;
	}
	~JavaThread() { if (attached) gVM->DetachCurrentThread(); }
};

void
HostAndroidPaused(bool paused)
{
	(void) paused;				// (the modes are the panel's, not a queue we hold)
}

enum { kInkFast = 4, kInkReading = 3, kInkClear = 17 };

// cls.getMethod(name, types) ==> a java.lang.reflect.Method or nil.  The vendor's API is called
// as java reflection does (the types of its arguments are all that is known of it), but from
// here: an APK with Java code of its own makes Play Protect warn, one with only a library does not
static jobject
FindMethod(JNIEnv* env, jclass cls, const char* name, jobjectArray types)
{
	jmethodID getMethod = env->GetMethodID(env->FindClass("java/lang/Class"), "getMethod", "(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
	jstring jname = env->NewStringUTF(name);
	jobject method = env->CallObjectMethod(cls, getMethod, jname, types);
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		return nullptr;
	}
	return method;
}

static jobject
Invoke(JNIEnv* env, jobject method, jobject target, jobjectArray args)
{
	jmethodID invoke = env->GetMethodID(env->FindClass("java/lang/reflect/Method"), "invoke", "(Ljava/lang/Object;[Ljava/lang/Object;)Ljava/lang/Object;");
	jobject result = env->CallObjectMethod(method, invoke, target, args);
	if (env->ExceptionCheck())
	{
		env->ExceptionClear();
		return nullptr;
	}
	return result;
}

static void
InkOpen(void)
{
	static bool tried = false;
	if (tried || gVM == nullptr)
		return;
	tried = true;
	const char* off = getenv("NEWTON_FAST_INK");
	if (off != nullptr && off[0] == '0')
		return;
	JavaThread t;
	JNIEnv* env = t.env;
	if (env == nullptr)
		return;
	jclass classClass = env->FindClass("java/lang/Class");
	jclass enote = env->FindClass("android/os/enote/ENoteSetting");
	if (env->ExceptionCheck() || enote == nullptr)
	{
		// (a native thread sees the system's classes only: the activity's loader may see more)
		env->ExceptionClear();
		jclass activityClass = env->GetObjectClass(gActivityRef);
		jobject loader = env->CallObjectMethod(gActivityRef, env->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;"));
		jclass loaderClass = env->GetObjectClass(loader);
		enote = (jclass) env->CallObjectMethod(loader, env->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;"),
												env->NewStringUTF("android.os.enote.ENoteSetting"));
		if (env->ExceptionCheck() || enote == nullptr)
		{
			env->ExceptionClear();
			return;				// (not the Viwoods)
		}
	}
	jobjectArray none = env->NewObjectArray(0, env->FindClass("java/lang/Object"), nullptr);
	jobjectArray noTypes = env->NewObjectArray(0, classClass, nullptr);
	jobject getInstance = FindMethod(env, enote, "getInstance", noTypes);
	jobject instance = getInstance ? Invoke(env, getInstance, nullptr, none) : nullptr;
	if (instance == nullptr)
		return;
	gEnote = env->NewGlobalRef(instance);

	// setApplicationContext(Context)
	jclass activityClass = env->GetObjectClass(gActivityRef);
	jobject context = env->CallObjectMethod(gActivityRef, env->GetMethodID(activityClass, "getApplicationContext", "()Landroid/content/Context;"));
	jobjectArray contextType = env->NewObjectArray(1, classClass, env->FindClass("android/content/Context"));
	jobject setContext = FindMethod(env, enote, "setApplicationContext", contextType);
	if (setContext == nullptr)
		return;
	jobjectArray contextArg = env->NewObjectArray(1, env->FindClass("java/lang/Object"), context);
	Invoke(env, setContext, gEnote, contextArg);
	// initWriting(), then the mode setter
	jobject init = FindMethod(env, enote, "initWriting", noTypes);
	if (init != nullptr)
		Invoke(env, init, gEnote, none);
	jclass integer = env->FindClass("java/lang/Integer");
	jobject intClass = env->GetStaticObjectField(integer, env->GetStaticFieldID(integer, "TYPE", "Ljava/lang/Class;"));
	jobjectArray intType = env->NewObjectArray(1, classClass, (jobject) intClass);
	jobject setMode = FindMethod(env, enote, "setPictureMode", intType);
	if (setMode == nullptr)
		return;
	gSetMode = env->NewGlobalRef(setMode);
	gInkOn = true;
	LOG("fast ink: on");
}

static void
InkMode(int mode)
{
	if (!gInkOn)
		return;
	JavaThread t;
	JNIEnv* env = t.env;
	if (env == nullptr)
		return;
	jclass integer = env->FindClass("java/lang/Integer");
	jobject value = env->CallStaticObjectMethod(integer, env->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;"), (jint) mode);
	jobjectArray args = env->NewObjectArray(1, env->FindClass("java/lang/Object"), value);
	Invoke(env, gSetMode, gEnote, args);
}


static void
RenderThread(void)
{
	std::vector<unsigned char> shadow((size_t) (gWidth * gHeight), 0);
	bool haveShadow = false;
	int windowVersion = -1, paletteVersion = -1;
	bool direct = false;
	DisplayDamage pending;
	bool penWas = false, ghosts = false;
	auto penUpAt = std::chrono::steady_clock::now();
	BuildRamp();
	while (!gStopping.load())
	{
		// (short, so that ink shows soon after the pen: the picture is only looked at, not redrawn)
		std::this_thread::sleep_for(std::chrono::milliseconds(8));
		std::lock_guard<std::mutex> lock(gWindowLock);
		if (gWindow == nullptr)
		{
			windowVersion = -1;
			continue;
		}
		bool full = false;
		if (windowVersion != gWindowVersion.load())
		{
			windowVersion = gWindowVersion.load();
			ANativeWindow_setBuffersGeometry(gWindow, 0, 0, 1 /* WINDOW_FORMAT_RGBA_8888 */);
			full = true;
		}
		if (paletteVersion != gPaletteVersion.load())
		{
			paletteVersion = gPaletteVersion.load();
			BuildRamp();
			full = true;
		}
		// what changed: the rows and columns the pixels differ in
		long x0 = gWidth, x1 = -1, y0 = gHeight, y1 = -1;
		for (long y = 0; y < gHeight; y++)
		{
			const unsigned char* now = gPixels + y * gWidth;
			unsigned char* was = shadow.data() + y * gWidth;
			if (memcmp(was, now, (size_t) gWidth) == 0)
				continue;
			long first = 0, last = gWidth - 1;
			while (was[first] == now[first]) first++;
			while (was[last] == now[last]) last--;
			if (first < x0) x0 = first;
			if (last > x1) x1 = last;
			if (y < y0) y0 = y;
			y1 = y;
			memcpy(was, now, (size_t) gWidth);
		}
		if (!haveShadow)
		{
			haveShadow = true;
			full = true;
		}
		// the panel's refresh: fast while the pen writes, a clearing refresh when it has rested
		bool penNow = gPenDown.load();
		if (penNow != penWas)
		{
			penWas = penNow;
			InkMode(penNow ? kInkFast : kInkReading);
			if (penNow)
				ghosts = true;
			else
				penUpAt = std::chrono::steady_clock::now();
		}
		if (ghosts && !penNow && std::chrono::steady_clock::now() - penUpAt > std::chrono::milliseconds(1200))
		{
			ghosts = false;
			InkMode(kInkClear);
			Draw(gWindow, true, 0, 0, 0, 0);
			InkMode(kInkReading);
		}
		if (!full && direct && (x1 >= 0 || pending.any))
		{
			// (the damage kept from a busy panel, and now's)
			if (x1 >= 0)
				pending.add(x0, y0, x1, y1);
			if (SendDirect(pending) == 0)
				pending.any = false;
		}
		else if (full || x1 >= 0)
		{
			Draw(gWindow, full, x0, y0, x1, y1);
			if (full)
			{
				direct = DirectUsable(gWindow);
				if (!direct)
					InkOpen();
			}
		}
	}
}


/*------------------------------------------------------------------------------
	host/HostWindow.h
------------------------------------------------------------------------------*/

bool
HostWindowStart(long width, long height, const unsigned char* pixels, const char* title, long scale)
{
	(void) title;
	(void) scale;
	if (!HostAndroidActive())
		return false;			// (not run as the activity: the world runs without a window)
	gWidth = width;
	gHeight = height;
	gPixels = pixels;
	gStopping.store(false);
	// the activity's window comes with its first APP_CMD_INIT_WINDOW; wait for it a while
	for (int i = 0; i < 10000; i++)
	{
		{
			std::lock_guard<std::mutex> lock(gWindowLock);
			if (gWindow != nullptr)
				break;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	if (gDiag.load() && gVM != nullptr)
	{
		std::lock_guard<std::mutex> lock(gWindowLock);
		if (gWindow != nullptr)
			HostAndroidDiagnose(gVM, gWindow);
	}
	gThread = new std::thread(RenderThread);
	LOG("window up: display %ldx%ld", width, height);
	return true;
}


void
HostWindowPostPen(long x, long y, int what)
{
	if (what == 0)
	{
		gPenDown.store(true);
		HostWindowPenDownFine(x * 8, y * 8);
	}
	else if (what == 1)
		HostWindowPenMoveFine(x * 8, y * 8);
	else
	{
		gPenDown.store(false);
		HostWindowPenUp();
	}
}


bool
HostWindowPosition(long* x, long* y)
{
	(void) x;
	(void) y;
	return false;				// (the activity fills the screen)
}


void
HostWindowSetPosition(long x, long y)
{
	(void) x;
	(void) y;
}


void
HostWindowStop(void)
{
	gStopping.store(true);
	if (gThread != nullptr)
	{
		gThread->join();
		delete gThread;
		gThread = nullptr;
	}
}


void
HostWindowPreferredDisplay(long* width, long* height)
{
	(void) width;
	(void) height;
}


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


// the window runs on threads of its own: the machine is simply run
int
HostWindowRunMachine(int (*machine)(void), bool windowed)
{
	(void) windowed;
	return machine();
}


bool
HostWindowOwnsProcess(void)
{
	return false;
}
