/*
	File:		stores/RefPipe.h

	Contains:	CRefPipe, a CPtrPipe (utility/Pipes.h) over a binary
				object's data, kept locked while the pipe has it: a sink
				made for the length a flattened object comes to (a binary in
				the heap, or a large binary on a store), or a source over a
				binary already there (the flatten and unflatten translators,
				comms/Translators.h).  (0x1c bytes in the ROM.)

	Reconstructed from the MP2x00 US ROM (0x001a43fc-0x001a45dc); each
	function cites its origin.
*/

#ifndef __STORES_REFPIPE_H
#define __STORES_REFPIPE_H

#ifndef __PIPES_H
#include "Pipes.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif

class CRefPipe : public CPtrPipe
{
public:
					CRefPipe();
	virtual			~CRefPipe();

	void			InitSink(long size, RefArg store, PipeCallBack* callback);		// store nil: a binary in the heap
	void			InitSource(RefArg binary, PipeCallBack* callback);

	RefStruct		fBinary;		// +0x18
};

#endif
