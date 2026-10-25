/*
	File:		host/HostAppDefaults.cpp

	Contains:	newton as an application of the host's desktop, and the
				internal store's default home (HostAppDefaults.h).  Host only (not in the ROM).
*/

#include "HostAppDefaults.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MakeDirectory(path) _mkdir(path)
#else
#include <pwd.h>
#include <unistd.h>
#define MakeDirectory(path) mkdir(path, 0755)
#endif
#if defined(__APPLE__)
#ifndef NEWTON_APP_IDENTIFIER
#define NEWTON_APP_IDENTIFIER "com.matthiasm.newton-re"	// (the build's, Newton.app's Info.plist)
#endif
#include <mach-o/dyld.h>
#include <time.h>
#endif


// The directory made, with its parents (==> whether it is there)
static bool
MakeDirectories(const char* path)
{
	char partial[2048];
	snprintf(partial, sizeof(partial), "%s", path);
	for (char* p = partial + 1; *p != 0; p++)
		if (*p == '/' || *p == '\\')
		{
			char separator = *p;
			*p = 0;
			MakeDirectory(partial);
			*p = separator;
		}
	MakeDirectory(partial);
	struct stat info;
	return stat(partial, &info) == 0 && (info.st_mode & S_IFMT) == S_IFDIR;
}


const char*
HostDefaultStoreFile(void)
{
	static char storeFile[2100];
	static bool looked = false;
	if (looked)
		return storeFile[0] != 0 ? storeFile : nullptr;
	looked = true;
	char directory[2048];
	const char* separator = "/";
#ifdef _WIN32
	// %LOCALAPPDATA%\Newton (C:\Users\<user>\AppData\Local\Newton)
	const char* local = getenv("LOCALAPPDATA");
	const char* profile = getenv("USERPROFILE");
	if (local != nullptr && local[0] != 0)
		snprintf(directory, sizeof(directory), "%s\\Newton", local);
	else if (profile != nullptr && profile[0] != 0)
		snprintf(directory, sizeof(directory), "%s\\AppData\\Local\\Newton", profile);
	else
		return nullptr;
	separator = "\\";
#else
	// ~/.local/newton
	const char* home = getenv("HOME");
	if (home == nullptr || home[0] == 0)
	{
		struct passwd* user = getpwuid(getuid());
		home = user != nullptr ? user->pw_dir : nullptr;
	}
	if (home == nullptr || home[0] == 0)
		return nullptr;
	snprintf(directory, sizeof(directory), "%s/.local/newton", home);
#endif
	if (!MakeDirectories(directory))
	{
		fprintf(stderr, "[host] %s cannot be made: the internal store is kept in memory only\n", directory);
		return nullptr;
	}
	snprintf(storeFile, sizeof(storeFile), "%s%sInternal.store", directory, separator);
	return storeFile;
}


#if defined(__APPLE__)

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

	// the arguments: what came, but a -psn_ process serial number, which an
	// older system's Finder adds (the internal store is newton's default,
	// HostDefaultStoreFile, as it is from a shell)
	char** args = (char**) calloc((size_t) *argc + 1, sizeof(char*));
	int n = 0;
	for (int i = 0; i < *argc; i++)
		if (i == 0 || strncmp(argv[i], "-psn_", 5) != 0)
			args[n++] = argv[i];
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
