/*
	File:		host/android/AndroidMain.cpp

	Contains:	The Newton as an Android application: a NativeActivity whose
				library is newton itself (docs/host-android.md).  android_main
				runs on the thread the activity glue gives it and does what an
				event loop does - the activity's window to the display
				(HostAndroidSetWindow), its input to the pen (HostAndroidInput)
				- while the machine, newton's own main, runs on a thread of
				its own, the way the other hosts' windows leave it.

				Before the machine starts the object file it boots from is
				copied out of the package's assets (an asset is not a file the
				machine can map), into the application's private directory,
				where its store is kept too; what the machine prints goes to
				the log (adb logcat -s newton).
*/

#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <string>
#include <vector>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <android/log.h>
#include <android/asset_manager.h>
#include <android_native_app_glue.h>

#include "HostAndroid.h"

extern "C" int	main(int argc, char** argv);		// host/newton.cpp's

#define LOG(...)	__android_log_print(ANDROID_LOG_INFO, "newton", __VA_ARGS__)

static const char*	kObjectsAsset = "romsrc-objects.bin";


// stdout and stderr to the log, a line at a time
static void
StartLogPipe(void)
{
	static int fds[2];
	if (pipe(fds) != 0)
		return;
	dup2(fds[1], 1);
	dup2(fds[1], 2);
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);
	std::thread([] {
		std::string line;
		char buffer[512];
		for (ssize_t n; (n = read(fds[0], buffer, sizeof(buffer))) > 0; )
			for (ssize_t i = 0; i < n; i++)
			{
				if (buffer[i] == '\n')
				{
					__android_log_write(ANDROID_LOG_INFO, "newton", line.c_str());
					line.clear();
				}
				else
					line += buffer[i];
			}
	}).detach();
}


// the object file out of the assets, unless the copy is the right size already
static bool
ExtractObjects(android_app* app, const std::string& to)
{
	AAsset* asset = AAssetManager_open(app->activity->assetManager, kObjectsAsset, AASSET_MODE_STREAMING);
	if (asset == nullptr)
	{
		LOG("no %s in the package's assets", kObjectsAsset);
		return false;
	}
	off_t size = AAsset_getLength(asset);
	struct stat st;
	if (stat(to.c_str(), &st) == 0 && st.st_size == size)
	{
		AAsset_close(asset);
		return true;
	}
	FILE* f = fopen(to.c_str(), "wb");
	if (f == nullptr)
	{
		AAsset_close(asset);
		LOG("cannot write %s", to.c_str());
		return false;
	}
	std::vector<char> buffer(1 << 20);
	for (int n; (n = AAsset_read(asset, buffer.data(), buffer.size())) > 0; )
		fwrite(buffer.data(), 1, (size_t) n, f);
	fclose(f);
	AAsset_close(asset);
	LOG("object file extracted: %ld bytes", (long) size);
	return true;
}


struct MachineArgs
{
	std::vector<std::string>	args;
};

static void*
MachineThread(void* arg)
{
	MachineArgs* m = (MachineArgs*) arg;
	std::vector<char*> argv;
	for (std::string& a : m->args)
		argv.push_back(const_cast<char*>(a.c_str()));
	argv.push_back(nullptr);
	int status = main((int) m->args.size(), argv.data());
	LOG("the machine ended: %d", status);
	_exit(status);
	return nullptr;
}


static void
OnCommand(android_app* app, int32_t command)
{
	switch (command)
	{
	case APP_CMD_INIT_WINDOW:
		HostAndroidSetWindow(app->window);
		break;
	case APP_CMD_TERM_WINDOW:
		HostAndroidSetWindow(nullptr);
		break;
	case APP_CMD_PAUSE:
		HostAndroidPaused(true);
		break;
	case APP_CMD_RESUME:
		HostAndroidPaused(false);
		break;
	case APP_CMD_DESTROY:
		HostAndroidDestroyed();
		break;
	}
}


static int32_t
OnInput(android_app* app, AInputEvent* event)
{
	(void) app;
	return HostAndroidInput(event);
}


extern "C" void
android_main(android_app* app)
{
	app->onAppCmd = OnCommand;
	app->onInputEvent = OnInput;
	StartLogPipe();
	{
		// (activity->env is the UI thread's: this thread gets its own)
		JNIEnv* env = nullptr;
		if (app->activity->vm->AttachCurrentThread(&env, nullptr) == JNI_OK)
			HostAndroidSetActivity(app->activity->vm, env, app->activity->clazz);
	}

	// an APK made with --diag shows the display report first
	if (AAsset* flag = AAssetManager_open(app->activity->assetManager, "diag.flag", AASSET_MODE_BUFFER))
	{
		AAsset_close(flag);
		HostAndroidSetDiag(true);
	}
	std::string dir = app->activity->internalDataPath;
	mkdir(dir.c_str(), 0700);
	chdir(dir.c_str());
	std::string objects = dir + "/romsrc-objects.bin";
	if (!ExtractObjects(app, objects))
	{
		ANativeActivity_finish(app->activity);
		return;
	}
	HostAndroidSetActive(true);

	static MachineArgs machine;
	machine.args = { "newton", "--objects", objects, "--store", dir + "/Internal.store" };
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 16 << 20);
	pthread_t thread;
	pthread_create(&thread, &attr, MachineThread, &machine);

	// the event loop: until the activity is destroyed (the machine ends the process)
	while (!app->destroyRequested)
	{
		int events;
		android_poll_source* source;
		while (ALooper_pollOnce(-1, nullptr, &events, (void**) &source) >= 0)
		{
			if (source != nullptr)
				source->process(app, source);
			if (app->destroyRequested)
				break;
		}
	}
	_exit(0);
}
