/*
	File:		host/HostPenReplay.cpp

	Contains:	A recorded pen played back (HostPenReplay.h).
*/

#include <thread>				// before anything else: a C header first upsets libc++'s locale support
#include <atomic>
#include <chrono>
#include <vector>
#include "HostPenReplay.h"
#include "HostWindow.h"
#include <stdio.h>
#include <stdlib.h>

struct PenEvent { long ms; int what; double x, y; };

// the window's pen shims (host/HostKeyboard.cpp), in eighths of a pixel
extern "C" {
void	HostWindowPenDownFine(long x8, long y8);
void	HostWindowPenMoveFine(long x8, long y8);
void	HostWindowPenUp(void);
}

static std::atomic<bool>	gReplayDone(true);
static std::vector<PenEvent>*	gReplay = nullptr;

long
HostPenReplayStart(const char* path)
{
	FILE* f = fopen(path, "r");
	if (f == nullptr)
		return -1;
	std::vector<PenEvent>* events = new std::vector<PenEvent>;
	PenEvent e;
	while (fscanf(f, "%ld %d %lf %lf", &e.ms, &e.what, &e.x, &e.y) == 4)
		events->push_back(e);
	fclose(f);
	if (gReplay != nullptr && !gReplayDone.load())
	{
		delete events;
		return -1;					// one at a time
	}
	delete gReplay;
	gReplay = events;
	gReplayDone.store(false);
	std::thread([events]() {
		// (this thread is none of the machine's: the pen shims only write
		// the panel's state, which the tablet driver samples)
		auto start = std::chrono::steady_clock::now();
		// NEWTON_PENREPLAY_WHOLE: the points cut to whole pixels, as the
		// reMarkable window gave them before it passed eighths on
		const bool whole = getenv("NEWTON_PENREPLAY_WHOLE") != nullptr;
		long first = events->empty() ? 0 : (*events)[0].ms;
		for (const PenEvent& p : *events)
		{
			std::this_thread::sleep_until(start + std::chrono::milliseconds(p.ms - first));
			long x8 = whole ? ((long) p.x) * 8 : (long) (p.x * 8.0 + 0.5);
			long y8 = whole ? ((long) p.y) * 8 : (long) (p.y * 8.0 + 0.5);
			if (p.what == 0)
				HostWindowPenDownFine(x8, y8);
			else if (p.what == 1)
				HostWindowPenMoveFine(x8, y8);
			else
				HostWindowPenUp();
		}
		gReplayDone.store(true);
	}).detach();
	return (long) events->size();
}


bool
HostPenReplayDone(void)
{
	return gReplayDone.load();
}
