/*
	File:		host/HostObjectsFile.cpp

	Contains:	The object file newton and newtonscript boot from by default
				(HostObjectsFile.h).
*/

#include "HostObjectsFile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
// (not <windows.h>: its names collide with the DDK's)
extern "C" __declspec(dllimport) unsigned long __stdcall GetModuleFileNameA(void* module, char* name, unsigned long size);
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

static const char kObjectsFileName[] = "romsrc-objects.bin";


static bool
IsFile(const char* path)
{
	FILE* f = fopen(path, "rb");
	if (f == NULL)
		return false;
	fclose(f);
	return true;
}


// the directory the running program is in, with its separator (empty when
// it cannot be told)
static void
ProgramDirectory(const char* argv0, char* dir, size_t size)
{
	dir[0] = 0;
#if defined(_WIN32)
	unsigned long n = GetModuleFileNameA(NULL, dir, (unsigned long) size);
	if (n == 0 || n >= size)
		dir[0] = 0;
#elif defined(__APPLE__)
	uint32_t n = (uint32_t) size;
	if (_NSGetExecutablePath(dir, &n) != 0)
		dir[0] = 0;
#else
	ssize_t n = readlink("/proc/self/exe", dir, size - 1);
	dir[n > 0 ? n : 0] = 0;
#endif
	if (dir[0] == 0 && argv0 != NULL && strlen(argv0) < size)
		strcpy(dir, argv0);
	char* slash = NULL;
	for (char* p = dir; *p; p++)
		if (*p == '/' || *p == '\\')
			slash = p;
	if (slash != NULL)
		slash[1] = 0;
	else
		dir[0] = 0;
}


const char*
HostDefaultObjectsFile(const char* argv0, const char* compiledIn)
{
	const char* env = getenv("NEWTON_OBJECTS");
	if (env != NULL && env[0] != 0)
		return env;
	static char path[4096];
	char dir[4096];
	ProgramDirectory(argv0, dir, sizeof(dir) - 64);
	if (dir[0] != 0)
	{
		snprintf(path, sizeof(path), "%s%s", dir, kObjectsFileName);
		if (IsFile(path))
			return path;
		snprintf(path, sizeof(path), "%s../%s", dir, kObjectsFileName);
		if (IsFile(path))
			return path;
	}
	if (compiledIn != NULL && IsFile(compiledIn))
		return compiledIn;
	return NULL;
}


void
HostObjectsFileMissing(const char* program, const char* compiledIn)
{
	fprintf(stderr,
			"%s: no object file to boot from.  It is the build's romsrc-objects.bin,\n"
			"made from the ROM source tree (romsrc/) with Python 3 - no ROM image needed:\n"
			"    cmake --build <build dir> --target romsrc\n"
			"(or by hand: python tools/newton-rom/analysis/romsrc.py build romsrc --relayout\n"
			"     -o <file> --newtonscript <newtonscript>).  It is looked for beside %s, in\n"
			"the directory above, and at %s; NEWTON_OBJECTS=<file>\n"
			"or --objects <file> names one.  --rom <image> boots a ROM image instead.\n",
			program, program, compiledIn != NULL ? compiledIn : "(none compiled in)");
}
