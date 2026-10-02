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

struct PenEvent { long ms; int what; long x, y; };

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
	while (fscanf(f, "%ld %d %ld %ld", &e.ms, &e.what, &e.x, &e.y) == 4)
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
		long first = events->empty() ? 0 : (*events)[0].ms;
		for (const PenEvent& p : *events)
		{
			std::this_thread::sleep_until(start + std::chrono::milliseconds(p.ms - first));
			HostWindowPostPen(p.x, p.y, p.what);
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
