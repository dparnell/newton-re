/*
	File:		host/HostAppDefaults.cpp

	Contains:	newton as an application of the host's desktop
				(HostAppDefaults.h).  Host only (not in the ROM).
*/

#include "HostAppDefaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(__APPLE__)
#ifndef NEWTON_APP_IDENTIFIER
#define NEWTON_APP_IDENTIFIER "com.matthiasm.newton-re"	// (the build's, Newton.app's Info.plist)
#endif
#include <mach-o/dyld.h>
#include <pwd.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#endif


#if defined(__APPLE__)

// The directory made, with its parents (==> whether it is there)
static bool
MakeDirectories(const char* path)
{
	char partial[2048];
	snprintf(partial, sizeof(partial), "%s", path);
	for (char* p = partial + 1; *p != 0; p++)
		if (*p == '/')
		{
			*p = 0;
			mkdir(partial, 0755);
			*p = '/';
		}
	mkdir(partial, 0755);
	struct stat info;
	return stat(partial, &info) == 0 && S_ISDIR(info.st_mode);
}


char**
HostAppDefaults(int* argc, char** argv)
{
	// the program in an application bundle, started by launchd (the Finder,
	// the Dock, open): otherwise nothing changes
	char program[2048];
	uint32_t size = sizeof(program);
	if (_NSGetExecutablePath(program, &size) != 0 || strstr(program, ".app/Contents/MacOS/") == nullptr || getppid() != 1)
		return argv;
	const char* home = getenv("HOME");
	if (home == nullptr || home[0] == 0)
	{
		struct passwd* user = getpwuid(getuid());
		home = user != nullptr ? user->pw_dir : nullptr;
	}
	if (home == nullptr)
		return argv;

	// the output: the log, appended to (unless it goes somewhere already)
	char logs[2048];
	snprintf(logs, sizeof(logs), "%s/Library/Logs/%s", home, NEWTON_APP_IDENTIFIER);
	if (!isatty(2) && MakeDirectories(logs))
	{
		char log[2100];
		snprintf(log, sizeof(log), "%s/newton.log", logs);
		if (freopen(log, "a", stderr) != nullptr)
		{
			dup2(fileno(stderr), fileno(stdout));
			setvbuf(stderr, nullptr, _IONBF, 0);
			setvbuf(stdout, nullptr, _IOLBF, 0);
			time_t now = time(nullptr);
			char when[64];
			strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", localtime(&now));
			fprintf(stderr, "\n[host] %s: %s started as an application (pid %d)\n", when, program, (int) getpid());
		}
	}

	// the working directory: the application's own
	static char support[2048];
	snprintf(support, sizeof(support), "%s/Library/Application Support/%s", home, NEWTON_APP_IDENTIFIER);
	if (!MakeDirectories(support) || chdir(support) != 0)
		return argv;

	// the arguments: what came (but a -psn_ process serial number, which an
	// older system's Finder adds), and the internal store if none was asked
	bool store = false;
	for (int i = 1; i < *argc; i++)
		store = store || strcmp(argv[i], "--store") == 0;
	char** args = (char**) calloc((size_t) *argc + 3, sizeof(char*));
	int n = 0;
	for (int i = 0; i < *argc; i++)
		if (i == 0 || strncmp(argv[i], "-psn_", 5) != 0)
			args[n++] = argv[i];
	if (!store)
	{
		static char storeFile[2100];
		snprintf(storeFile, sizeof(storeFile), "%s/Internal.store", support);
		args[n++] = (char*) "--store";
		args[n++] = storeFile;
	}
	args[n] = nullptr;
	*argc = n;
	fprintf(stderr, "[host] the application's directory is %s\n", support);
	return args;
}

#else

char**
HostAppDefaults(int* /*argc*/, char** argv)
{
	return argv;
}

#endif
