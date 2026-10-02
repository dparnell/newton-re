/*
	File:		host/HostPenReplay.h

	Contains:	A pen recorded on one host played back on another: a file
				of pen events (one a line, "milliseconds what x y" - what
				0 down, 1 move, 2 up; x and y in display pixels), as the
				reMarkable window writes it with NEWTON_RM_PENLOG
				(host/remarkable/HostWindow.cpp), handed to the window's pen
				shims at the times it was recorded at (HostWindowPostPen),
				so the tablet driver samples it as it sampled the real one.
				docs/host-remarkable.md, "Handwriting".

				No Newton headers: newton.cpp's HostPenReplay native calls it.
*/

#ifndef __HOSTPENREPLAY_H
#define __HOSTPENREPLAY_H

long	HostPenReplayStart(const char* path);	// the playing started on a thread of its own: ==> how many events, -1 if the file could not be read
bool	HostPenReplayDone(void);					// the last event has been played

#endif	/* __HOSTPENREPLAY_H */
