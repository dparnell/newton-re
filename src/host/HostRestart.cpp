/*
	File:		host/HostRestart.cpp

	Contains:	A restart of the machine on the host (HostRestart.h).

				On the MessagePad, Reboot (os600/kernel/Reboot.cpp) records
				why in gGlobalsThatLiveAcrossReboot - RAM the boot does not
				clear - and jumps to address 0, the reset vector: the ROM
				boots again over the same RAM, so what lives across a
				reboot (the reason, the patch pages, the tablet
				calibration's copy, the boot counters) and the stores
				(flash, cards) are still there, and everything else - the
				tasks, the heaps, the frames world - is made afresh.

				DEVIATION: on the host the reset stops the tasks
				(gHostResetHook) and newton starts itself again as a new
				process with the same arguments, waiting for it and
				answering its exit status - so a console or a test sees
				the whole run.  The new process is the machine booted
				again: its stores are the same files; what lived across the
				reboot in RAM is handed over in the environment
				(NEWTON_REBOOT_REASON, NEWTON_REBOOT_COUNT, and the
				kernel's copy of the tablet's calibration in
				NEWTON_REBOOT_TABLET) and put back
				into gGlobalsThatLiveAcrossReboot before the boot, so the
				Gestalt reboot info answers as on the machine; a window
				opens where the old one was (NEWTON_WINDOW_POSITION).
*/

#include "HostRestart.h"
#include "HostWindow.h"
#include "HostSockets.h"
#include "Host.h"
#include "VirtualMemory.h"
#include "os600/kernel/KernelGlobals.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

static long	gRebootCount = 0;

static void
SetEnv(const char* name, const char* value)
{
#ifdef _WIN32
	_putenv_s(name, value);
#else
	setenv(name, value, 1);
#endif
}


void
HostRestartReceive(void)
{
	const char* count = getenv("NEWTON_REBOOT_COUNT");
	gRebootCount = count != nil ? strtol(count, nil, 0) : 0;
	const char* reason = getenv("NEWTON_REBOOT_REASON");
	if (reason != nil)
	{
		gGlobalsThatLiveAcrossReboot.fRebootReason = (ULong) strtol(reason, nil, 0);
		gGlobalsThatLiveAcrossReboot.fMagicNumber = kRebootMagicNumber;
	}
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	const char* tablet = getenv("NEWTON_REBOOT_TABLET");
	long valid, xScale, xOffset, yScale, yOffset;
	if (tablet != nil && sscanf(tablet, "%ld,%ld,%ld,%ld,%ld", &valid, &xScale, &xOffset, &yScale, &yOffset) == 5)
	{
		g.fTabletValid = valid;
		g.fTabletXScale = xScale;
		g.fTabletXOffset = xOffset;
		g.fTabletYScale = yScale;
		g.fTabletYOffset = yOffset;
	}
	long x, y;
	const char* where = getenv("NEWTON_WINDOW_POSITION");
	if (where != nil && sscanf(where, "%ld,%ld", &x, &y) == 2)
		HostWindowSetPosition(x, y);
	if (gRebootCount > 0)
		fprintf(stderr, "[host] restarted (%ld), the reboot reason %ld\n", gRebootCount,
				(long) (Long32) gGlobalsThatLiveAcrossReboot.fRebootReason);
}


long
HostRebootCount(void)
{
	return gRebootCount;
}


int
HostRestartIfReset(int argc, char** argv, bool scriptQuit)
{
	if (gHostResetCount == 0 || gHostPoweredOff || scriptQuit)
		return -1;
	const char* limitText = getenv("NEWTON_REBOOT_LIMIT");
	long limit = limitText != nil ? strtol(limitText, nil, 0) : 5;
	if (gRebootCount >= limit)
	{
		fprintf(stderr, "[host] the machine reset itself; no restart (%ld of NEWTON_REBOOT_LIMIT %ld made)\n", gRebootCount, limit);
		return -1;
	}
	char text[64];
	snprintf(text, sizeof(text), "%ld", gRebootCount + 1);
	SetEnv("NEWTON_REBOOT_COUNT", text);
	snprintf(text, sizeof(text), "%ld", (long) (Long32) gGlobalsThatLiveAcrossReboot.fRebootReason);
	SetEnv("NEWTON_REBOOT_REASON", text);
	SGlobalsThatLiveAcrossReboot& g = gGlobalsThatLiveAcrossReboot;
	snprintf(text, sizeof(text), "%ld,%ld,%ld,%ld,%ld", (long) g.fTabletValid, (long) g.fTabletXScale,
			 (long) g.fTabletXOffset, (long) g.fTabletYScale, (long) g.fTabletYOffset);
	SetEnv("NEWTON_REBOOT_TABLET", text);
	long x, y;
	if (HostWindowPosition(&x, &y))
	{
		snprintf(text, sizeof(text), "%ld,%ld", x, y);
		SetEnv("NEWTON_WINDOW_POSITION", text);
	}
	// the same arguments, but the store kept
	char** args = (char**) calloc((size_t) argc + 1, sizeof(char*));
	int n = 0;
	for (int i = 0; i < argc; i++)
		if (i == 0 || strcmp(argv[i], "--erase") != 0)
			args[n++] = argv[i];
	args[n] = nil;
	fprintf(stderr, "[host] the machine reset itself (reason %s): restarting\n", getenv("NEWTON_REBOOT_REASON"));
	fflush(nil);			// (every stream: the flash and card files are written through them)
	HostSocketsCloseAll();	// (the new run listens on the same ports)
	int status = -1;
#ifdef _WIN32
	// the program itself, wherever argv[0] found it, given a command line
	// quoted as the C runtime splits one; it inherits the standard handles
	// and nothing else (a socket or a file of this run's would otherwise
	// stay open in it)
	char program[1024];
	if (GetModuleFileNameA(nil, program, sizeof(program)) == 0)
		strncpy(program, argv[0], sizeof(program) - 1);
	size_t room = 1;
	for (int i = 0; i < n; i++)
		room += strlen(args[i]) * 2 + 3;
	char* line = (char*) malloc(room);
	char* q = line;
	for (int i = 0; i < n; i++)
	{
		if (i > 0)
			*q++ = ' ';
		*q++ = '"';
		size_t slashes = 0;
		for (const char* a = args[i]; ; a++)
		{
			if (*a == '\\')
			{
				slashes++;
				continue;
			}
			// backslashes are literal unless a quote follows them
			size_t times = (*a == '"' || *a == 0) ? slashes * 2 : slashes;
			for (size_t k = 0; k < times; k++)
				*q++ = '\\';
			slashes = 0;
			if (*a == 0)
				break;
			if (*a == '"')
				*q++ = '\\';
			*q++ = *a;
		}
		*q++ = '"';
	}
	*q = 0;
	HANDLE inherited[3];
	DWORD count = 0;
	DWORD stdHandles[3] = { STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE };
	for (int i = 0; i < 3; i++)
	{
		HANDLE h = GetStdHandle(stdHandles[i]);
		if (h == nil || h == INVALID_HANDLE_VALUE)
			continue;
		bool seen = false;
		for (DWORD k = 0; k < count; k++)
			seen = seen || inherited[k] == h;
		if (!seen && SetHandleInformation(h, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT))
			inherited[count++] = h;
	}
	SIZE_T attributesSize = 0;
	InitializeProcThreadAttributeList(nil, 1, 0, &attributesSize);
	LPPROC_THREAD_ATTRIBUTE_LIST attributes = (LPPROC_THREAD_ATTRIBUTE_LIST) malloc(attributesSize);
	STARTUPINFOEXA startup;
	memset(&startup, 0, sizeof(startup));
	startup.StartupInfo.cb = sizeof(startup);
	startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
	startup.StartupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	startup.StartupInfo.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	startup.StartupInfo.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	bool listed = attributes != nil && InitializeProcThreadAttributeList(attributes, 1, 0, &attributesSize)
				&& (count == 0 || UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited, count * sizeof(HANDLE), nil, nil));
	if (listed)
		startup.lpAttributeList = attributes;
	PROCESS_INFORMATION process;
	if (CreateProcessA(program, line, nil, nil, count > 0 && listed, listed ? EXTENDED_STARTUPINFO_PRESENT : 0,
					   nil, nil, &startup.StartupInfo, &process))
	{
		CloseHandle(process.hThread);
		WaitForSingleObject(process.hProcess, INFINITE);
		DWORD code = 1;
		GetExitCodeProcess(process.hProcess, &code);
		CloseHandle(process.hProcess);
		status = (int) code;
	}
	else
	{
		fprintf(stderr, "[host] the restart could not be started (%lu)\n", (unsigned long) GetLastError());
		status = 1;
	}
	if (listed)
		DeleteProcThreadAttributeList(attributes);
	free(attributes);
	free(line);
#else
	char program[1024];
	ssize_t length = readlink("/proc/self/exe", program, sizeof(program) - 1);
	if (length > 0)
		program[length] = 0;
	else
		strncpy(program, argv[0], sizeof(program) - 1);
	pid_t child = fork();
	if (child == 0)
	{
		execv(program, args);
		execvp(argv[0], args);
		_exit(127);
	}
	int how = 0;
	if (child > 0 && waitpid(child, &how, 0) == child)
		status = WIFEXITED(how) ? WEXITSTATUS(how) : 1;
	else
		status = 1;
#endif
	free(args);
	return status;
}
