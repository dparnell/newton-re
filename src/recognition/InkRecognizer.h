/*
	File:		recognition/InkRecognizer.h

	Contains:	`TInkOnlyRecognizer`, a handwriting engine that reads
				nothing.

				The MP2x00's own engine is the CIC handwriting library,
				which is not reconstructed.  Because the engine plugs in
				through the `TWRecognizer` protocol (WRecDomain.h) the
				system does not need to know which one it has, and this
				is the smallest thing that can sit in the socket: it
				gathers strokes into words and then says, of every one
				of them, that it could not read it.

				That is not a dummy.  It is exactly the answer the ROM's
				own engine gives for writing it cannot make out, and it
				is the answer the whole ink path is built on - a unit
				the engine answers `kWRecInk` for becomes ink on the
				page (`WordRecognizerHandleUnit`, `GetInkCommand`).  So
				with this engine installed the pen leaves ink behind it,
				which is what a Newton with recognition turned off does.

				When the ROM's engine - or a modern one - is
				reconstructed it registers itself as another
				implementation of `TWRecognizer` and takes over; nothing
				above it changes.

	DEVIATION: the ROM's `RegisterWRec` registers the CIC engine.  This
	stands in its place and is registered by the host, not by
	`RegisterWRec`, so that it is obvious which engine a running system
	has.

	Reconstructed from nothing: the ROM has no such engine.  The
	grouping follows the shape the ROM's engines use
	(`GetPartialGroup`/`AddSub`/`EndSubs`), and the baseline comes from
	the word recogniser's own `FindBaseline` (Words.h).
*/

#ifndef __INKRECOGNIZER_H
#define __INKRECOGNIZER_H

#include "WRecDomain.h"

// Registers the engine, so that InstallWRecRecognizer finds one.  Does
// nothing when there is no protocol registry (a host program running
// part of the system without the operating system under it).
void	RegisterInkOnlyRecognizer(void);

#endif	/* __INKRECOGNIZER_H */
