/*
	File:		stores/RefPipe.cpp

	Contains:	CRefPipe (RefPipe.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "RefPipe.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "LargeBinaries.h"


// ROM 0x001a43fc __ct__8CRefPipeFv
CRefPipe::CRefPipe()
{
	fBinary = NILREF;
}


// ROM 0x001a445c __dt__8CRefPipeFv
CRefPipe::~CRefPipe()
{
	if (NOTNIL(fBinary))
		UnlockRef(fBinary);
	fBinary = NILREF;
}


// ROM 0x001a44c8 InitSink__8CRefPipeFlRC6RefVarP12PipeCallBack
// A binary of size bytes to write into: in the heap, or a large binary on
// the store.
void
CRefPipe::InitSink(long size, RefArg store, PipeCallBack* callback)
{
	if (ISNIL(store))
		fBinary = AllocateBinary(RSSYMbinary, size);
	else
		fBinary = FLBAlloc(store, RSSYMbinary, RefVar(MAKEINT(size)));
	LockRef(fBinary);
	Init(BinaryData(fBinary), size, false, callback);
}


// ROM 0x001a458c InitSource__8CRefPipeFRC6RefVarP12PipeCallBack
void
CRefPipe::InitSource(RefArg binary, PipeCallBack* callback)
{
	fBinary = binary;
	LockRef(fBinary);
	Init(BinaryData(fBinary), Length(fBinary), false, callback);
}
