/*
	File:		comms/V42bis.cpp

	Contains:	V.42bis compression for MNP (MNP.h): the compressor's state
				(TCompressVars, a 0x39d4-byte block) and, NOT YET, the
				encoder and decoder themselves (BTEncode/BTDecode,
				0x0025ce5c-0x0025dab0) - an MNP link negotiates V.42bis only
				when the other end asks for it, which the dock's peers here
				do not; until then a link that negotiates it stops here.

	Reconstructed from the MP2x00 US ROM (0x0025ce1c-0x0025ce5c); each
	function cites its origin.
*/

#include "MNP.h"
#include "NewtErrors.h"

#include <stdio.h>
#include <stdlib.h>

struct TCompressVars
{
	UByte			b[0x39d4];			// (the ROM's layout; the fields are NOT YET named)
};


// ROM 0x0025ce1c V42CreateCompressVars__FPP13TCompressVars
NewtonErr
V42CreateCompressVars(TCompressVars** vars)
{
	*vars = (TCompressVars*) malloc(sizeof(TCompressVars));
	return (*vars != nil) ? noErr : -10007;		// (kOSErrNoMemory)
}


// ROM 0x0025ce50 V42DisposeCompressVars__FP13TCompressVars
void
V42DisposeCompressVars(TCompressVars* vars)
{
	if (vars != nil)
		free(vars);
}


// NOT YET: V42InitCompress (0x0025dab0), BTEncode (0x0025ce5c), BTFlush
// (0x0025d3b4), BTDecode (0x0025d438).
static void
V42NotYet(const char* what)
{
	fprintf(stderr, "V.42bis %s: NOT YET reconstructed\n", what);
	abort();
}

void
V42InitCompress(TCompressVars* vars, ULong directions, ULong dictionarySize, ULong maxString,
				MNPByteProc compressOut, MNPByteProc decompressOut, void* refCon)
{
	V42NotYet("V42InitCompress");
}

void
BTEncode(void* vars, UByte byte)
{
	V42NotYet("BTEncode");
}

void
BTFlush(void* vars, UByte byte)
{
	V42NotYet("BTFlush");
}

void
BTDecode(void* vars, UByte byte)
{
	V42NotYet("BTDecode");
}
