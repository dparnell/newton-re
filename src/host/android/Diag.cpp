/*
	File:		host/android/Diag.cpp

	Contains:	A report of what the tablet's display offers an app, drawn on
				its screen (a tablet whose adb refuses logcat and shell has no
				other way to say): the model and board, which device nodes
				that look like an e-ink driver exist and may be opened, and the
				answer of the Supernote driver's info request.  Built into an
				APK made with tools/android/build_apk.py --diag; it is shown
				for 100 seconds before the machine starts.  docs/host-android.md.
*/

#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/system_properties.h>
#include <android/native_window.h>
#include <android/bitmap.h>
#include <jni.h>

#include "HostAndroid.h"

static void
Add(std::vector<std::string>& lines, const char* format, const char* a = "", const char* b = "", const char* c = "")
{
	char buffer[400];
	snprintf(buffer, sizeof(buffer), format, a, b, c);
	lines.push_back(buffer);
}

static std::string
Property(const char* name)
{
	char value[PROP_VALUE_MAX] = "";
	__system_property_get(name, value);
	return value;
}

// names in a directory that contain one of the words
static void
List(std::vector<std::string>& lines, const char* directory, const char* const* words, bool all)
{
	DIR* dir = opendir(directory);
	if (dir == nullptr)
	{
		Add(lines, "%s: cannot list (%s)", directory, strerror(errno));
		return;
	}
	std::string found;
	int count = 0;
	while (dirent* e = readdir(dir))
	{
		count++;
		bool match = all;
		for (const char* const* w = words; !match && *w != nullptr; w++)
			if (strstr(e->d_name, *w) != nullptr)
				match = true;
		if (match && e->d_name[0] != '.')
		{
			found += e->d_name;
			found += ' ';
		}
	}
	closedir(dir);
	char head[64];
	snprintf(head, sizeof(head), "%d entries: ", count);
	Add(lines, "%s %s%s", directory, head, found.c_str());
}

static void
Probe(std::vector<std::string>& lines, const char* path)
{
	int fd = open(path, O_RDWR | O_CLOEXEC);
	int error = errno;
	if (fd < 0 && error == EACCES)
	{
		fd = open(path, O_RDONLY | O_CLOEXEC);
		if (fd >= 0)
		{
			Add(lines, "%s: opens read-only", path);
			close(fd);
			return;
		}
	}
	if (fd < 0)
	{
		if (error == ENOENT)
			return;					// (not there: not worth a line)
		Add(lines, "%s: exists, open fails: %s", path, strerror(error));
		return;
	}
	Add(lines, "%s: OPENS read-write", path);
	int32_t info[32];
	memset(info, 0, sizeof(info));
	if (ioctl(fd, 0x48545201UL, info) == 0)
	{
		char text[300];
		snprintf(text, sizeof(text), "  Supernote info request answers: %d %d %d %d (w|h<<16 = %d x %d)",
				 info[0], info[1], info[2], info[3], (int) ((uint32_t) info[1] & 0xffff), (int) ((uint32_t) info[1] >> 16));
		lines.push_back(text);
	}
	else
		Add(lines, "  Supernote info request: no (%s)", strerror(errno));
	close(fd);
}

void
HostAndroidDiagnose(JavaVM* vm, ANativeWindow* window)
{
	std::vector<std::string> lines;
	lines.push_back("NEWTON DISPLAY REPORT (show this to Claude)");
	Add(lines, "model %s / %s", Property("ro.product.manufacturer").c_str(), Property("ro.product.model").c_str());
	Add(lines, "build %s", Property("ro.build.display.id").c_str());
	Add(lines, "board %s hardware %s", Property("ro.board.platform").c_str(), Property("ro.hardware").c_str());
	Add(lines, "android %s sdk %s", Property("ro.build.version.release").c_str(), Property("ro.build.version.sdk").c_str());
	Add(lines, "window %d x %d", std::to_string(ANativeWindow_getWidth(window)).c_str(), std::to_string(ANativeWindow_getHeight(window)).c_str());

	static const char* const kWords[] = { "ebc", "epd", "eink", "ink", "fb", "graphics", "disp", "wb", "waveform", "tcon",
										  "rk", "mxc", "sunxi", "ht", "wisky", "viwoods", "vsp", "mtk", "pen", "tp", nullptr };
	List(lines, "/dev", kWords, false);
	List(lines, "/dev/graphics", kWords, true);
	List(lines, "/sys/class/graphics", kWords, true);
	List(lines, "/sys/class", (const char* const[]) { "ebc", "epd", "eink", "ink", "disp", "tcon", nullptr }, false);
	static const char* const kPaths[] = {
		"/dev/ebc", "/dev/ebc_buf0", "/dev/ebc_buf1", "/dev/epd", "/dev/eink", "/dev/graphics/fb0", "/dev/fb0", "/dev/fb1",
		"/dev/mxc_epdc_fb", "/dev/disp", "/dev/dri/card0", "/dev/ion", "/dev/ht_ebc", "/dev/epdc", "/dev/ebook", nullptr };
	for (const char* const* p = kPaths; *p != nullptr; p++)
		Probe(lines, *p);
	// the tablet's own library names
	for (const char* lib : { "/system/lib64/libeinkutils.so", "/system/lib64/libpaintworker.so", "/vendor/lib64/libepd.so", "/system/lib64/libenote.so" })
		if (access(lib, F_OK) == 0)
			Add(lines, "%s present", lib);

	JNIEnv* env = nullptr;
	bool attached = false;
	if (vm->GetEnv((void**) &env, JNI_VERSION_1_6) != JNI_OK)
		attached = vm->AttachCurrentThread(&env, nullptr) == JNI_OK;
	if (env == nullptr)
		return;
	int width = ANativeWindow_getWidth(window), height = ANativeWindow_getHeight(window);
	jclass bitmapClass = env->FindClass("android/graphics/Bitmap");
	jclass configClass = env->FindClass("android/graphics/Bitmap$Config");
	jobject config = env->GetStaticObjectField(configClass, env->GetStaticFieldID(configClass, "ARGB_8888", "Landroid/graphics/Bitmap$Config;"));
	jobject bitmap = env->CallStaticObjectMethod(bitmapClass,
		env->GetStaticMethodID(bitmapClass, "createBitmap", "(IILandroid/graphics/Bitmap$Config;)Landroid/graphics/Bitmap;"), width, height, config);
	jclass canvasClass = env->FindClass("android/graphics/Canvas");
	jobject canvas = env->NewObject(canvasClass, env->GetMethodID(canvasClass, "<init>", "(Landroid/graphics/Bitmap;)V"), bitmap);
	env->CallVoidMethod(canvas, env->GetMethodID(canvasClass, "drawColor", "(I)V"), (jint) 0xffffffff);
	jclass paintClass = env->FindClass("android/graphics/Paint");
	jobject paint = env->NewObject(paintClass, env->GetMethodID(paintClass, "<init>", "()V"));
	env->CallVoidMethod(paint, env->GetMethodID(paintClass, "setColor", "(I)V"), (jint) 0xff000000);
	env->CallVoidMethod(paint, env->GetMethodID(paintClass, "setTextSize", "(F)V"), 26.0f);
	jmethodID drawText = env->GetMethodID(canvasClass, "drawText", "(Ljava/lang/String;FFLandroid/graphics/Paint;)V");
	float y = 40;
	for (const std::string& line : lines)
	{
		// (wrapped to the width: about 70 characters of this size)
		for (size_t at = 0; at < line.size() || at == 0; at += 70)
		{
			std::string part = line.substr(at, 70);
			env->CallVoidMethod(canvas, drawText, env->NewStringUTF(part.c_str()), 20.0f, y, paint);
			y += 32;
			if (at + 70 >= line.size())
				break;
		}
	}
	void* bits = nullptr;
	AndroidBitmapInfo info;
	if (AndroidBitmap_getInfo(env, bitmap, &info) == 0 && AndroidBitmap_lockPixels(env, bitmap, &bits) == 0)
	{
		ANativeWindow_setBuffersGeometry(window, 0, 0, 1 /* RGBA_8888 */);
		ANativeWindow_Buffer buffer;
		if (ANativeWindow_lock(window, &buffer, nullptr) == 0)
		{
			for (int row = 0; row < buffer.height && row < (int) info.height; row++)
				memcpy((char*) buffer.bits + (size_t) row * buffer.stride * 4, (char*) bits + (size_t) row * info.stride,
					   (size_t) (buffer.width < (int) info.width ? buffer.width : info.width) * 4);
			ANativeWindow_unlockAndPost(window);
		}
		AndroidBitmap_unlockPixels(env, bitmap);
	}
	if (attached)
		vm->DetachCurrentThread();
	std::this_thread::sleep_for(std::chrono::seconds(100));
}
