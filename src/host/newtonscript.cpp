/*
	File:		host/newtonscript.cpp

	Contains:	newtonscript - the reconstructed NewtonScript running on the
				host: the object system over the ROM's objects, the compiler and
				the interpreter, with the REP reading from stdin (or files
				given on the command line) and printing to stdout, the way
				the ROM's REP runs over its stdio translators.

	Usage:
		newtonscript [--objects <file> | --rom <image> | --no-objects] [--heap <bytes>] [--display <w>x<h>[x<depth>]] [-e <source>] [file.ns ...]
		newtonscript [--rom <image>] --roundtrip <records> <results>
		newtonscript [--no-objects] --compile-records <records> <output>
		newtonscript --ima-expand <ima blocks> <pcm> | --ima-compress <pcm> <ima blocks>

	Each file is loaded with ParseFile (each form compiled and run, as the
	NTK loads a text file); -e compiles and runs a string; with no files
	and no -e, or with -i, forms are read from stdin one line at a time
	(REPAcceptLine) until the end of the input.  The ROM's objects come by
	default from the object file the build makes from the committed ROM
	source tree (<build>/romsrc-objects.bin, romsrc/README.md), found as
	newton finds it (host/HostObjectsFile.h: NEWTON_OBJECTS, beside the
	program, the build's path); with none newtonscript says how to build it
	and stops.  --rom loads a ROM image instead, for checking against the
	ROM.  --no-objects - or a file named by --rom, --objects or NEWTON_ROM
	that cannot be read (romsrc.py's builder names a file that is not there
	on purpose) - runs the object system without the ROM's objects (its
	built-in NewtonScript functions are then missing, the reconstructed
	natives are not), which is all compiling needs.  The host adds the global
	function ROMConstant(name): the ROM's R constant of that name (a
	string or symbol, case as in ROMConstants.h without the R:
	ROMConstant("canonicalTextShape")), nil for none - for looking at the
	ROM's objects from the REP.  --display starts the view system over a
	host display of the size (and depth, 1 by default: 320x480 is the
	MessagePad's, 4 deep): AddView(GetRoot(), template), :Open(),
	RefreshViews() and ScreenSnapshot("file.pgm") then draw a view
	hierarchy into an image (host/HostViews.h).

	--objects <file> loads another object file built from the ROM source
	tree (tools/newton-rom/analysis/romsrc.py build -o; an edited tree's):
	the object system with no image behind it (docs/rom-free/README.md).
	Either option takes either kind of file, told by its signature.

	--roundtrip compiles each function the NewtonScript decompiler wrote
	(tools/newton-rom/analysis/nsdecompile.py) and compares it with the ROM's
	own, a result line per function (host/NSRoundTrip.cpp).
	--compile-records compiles such records and writes each function in the
	notation of the ROM object source, for romsrc.py's builder.

	--ima-expand / --ima-compress run the ROM's IMA/DVI ADPCM codec
	(sound/IMACodec.h) over a file: the Newton's IMA blocks to raw
	big-endian 16-bit PCM and back (host/IMATool.cpp), which is how
	romsrc.py turns a compressed sound into a WAV file and back.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "HostViews.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HostNatives.h"
#include "HostStores.h"
#include "HostObjectsFile.h"

#ifndef NEWTON_DEFAULT_OBJECTS
#define NEWTON_DEFAULT_OBJECTS "romsrc-objects.bin"
#endif


// Host: ROMConstant(name) - the ROM's R constant by name
static Ref
FROMConstant(RefArg /*rcvr*/, RefArg name)
{
	RefVar sym(IsSymbol(name) ? (Ref) name : Intern((UniChar*) BinaryData(name)));
	const char* wanted = SymbolName(sym);
	for (long i = 0; i < gROMConstantCount; i++)
		if (symcmp((char*) gROMConstantEntries[i].fName, (char*) wanted) == 0)
			return *gROMConstantEntries[i].fRef;
	return NILREF;
}


int		RunRoundTrip(const char* inputPath, const char* outputPath);		// NSRoundTrip.cpp
int		RunCompileRecords(const char* inputPath, const char* outputPath);	// NSRoundTrip.cpp
int		RunIMAExpand(const char* inPath, const char* outPath);				// IMATool.cpp
int		RunIMACompress(const char* inPath, const char* outPath);			// IMATool.cpp


static int
Usage(void)
{
	fprintf(stderr, "usage: newtonscript [--objects <file> | --rom <image> | --no-objects] [--heap <bytes>] [-e <source>] [-i] [file.ns ...]\n"
					"By default it loads the object file built from romsrc/ (NEWTON_OBJECTS overrides).\n");
	return 2;
}


int
main(int argc, char** argv)
{
	// (NEWTON_ROM: a file named as --rom names one - the builder's way of
	//  saying there is to be none)
	const char* romImage = getenv("NEWTON_ROM");
	Boolean noObjects = false;
	long heapSize = 0x400000;
	long displayWidth = 0, displayHeight = 0, displayDepth = 1;
	Boolean interactive = false;
	Boolean ranSomething = false;
	const char* roundTripIn = nil;
	const char* roundTripOut = nil;
	const char* compileIn = nil;
	const char* compileOut = nil;
	int first = 1;
	// (the codec needs nothing of the object system)
	if (argc == 4 && strcmp(argv[1], "--ima-expand") == 0)
		return RunIMAExpand(argv[2], argv[3]);
	if (argc == 4 && strcmp(argv[1], "--ima-compress") == 0)
		return RunIMACompress(argv[2], argv[3]);
	while (first < argc && argv[first][0] == '-' && argv[first][1] == '-')
	{
		if ((strcmp(argv[first], "--rom") == 0 || strcmp(argv[first], "--objects") == 0) && first + 1 < argc)
		{
			romImage = argv[first + 1];
			first += 2;
		}
		else if (strcmp(argv[first], "--no-objects") == 0)
		{
			noObjects = true;
			first += 1;
		}
		else if (strcmp(argv[first], "--heap") == 0 && first + 1 < argc)
		{
			heapSize = strtol(argv[first + 1], nil, 0);
			first += 2;
		}
		else if (strcmp(argv[first], "--roundtrip") == 0 && first + 2 < argc)
		{
			roundTripIn = argv[first + 1];
			roundTripOut = argv[first + 2];
			first += 3;
		}
		else if (strcmp(argv[first], "--compile-records") == 0 && first + 2 < argc)
		{
			compileIn = argv[first + 1];
			compileOut = argv[first + 2];
			first += 3;
		}
		else if (strcmp(argv[first], "--display") == 0 && first + 1 < argc)
		{
			char* rest;
			displayWidth = strtol(argv[first + 1], &rest, 0);
			displayHeight = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 0;
			displayDepth = *rest == 'x' ? strtol(rest + 1, &rest, 0) : 1;
			if (displayWidth <= 0 || displayHeight <= 0)
				return Usage();
			first += 2;
		}
		else
			return Usage();
	}

	if (noObjects)
		romImage = nil;
	else if (romImage == nil)
	{
		romImage = HostDefaultObjectsFile(argv[0], NEWTON_DEFAULT_OBJECTS);
		if (romImage == nil)
		{
			HostObjectsFileMissing("newtonscript", NEWTON_DEFAULT_OBJECTS);
			fprintf(stderr, "(--no-objects runs it without the ROM's objects)\n");
			return 1;
		}
	}
	InitHostStandaloneHeap(heapSize + 0x400000);
	if (romImage != nil && ImportROMObjectsFromFile(romImage) != noErr)
		fprintf(stderr, "newtonscript: no ROM image at %s; running without the ROM's objects\n", romImage);
	gObjectHeapSize = heapSize;
	InitObjects();
	RegisterAllNatives();
	HostMountStores();
	HostInitREP(stdout, stdin);
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "ROMConstant")), RefVar(MakeCFunction((void*) FROMConstant, 1, nil)));
	if (roundTripIn != nil)
		return RunRoundTrip(roundTripIn, roundTripOut);
	if (compileIn != nil)
		return RunCompileRecords(compileIn, compileOut);
	if (displayWidth > 0)
	{
		newton_try
		{
			HostStartViews(displayWidth, displayHeight, displayDepth);
		}
		newton_catch_all
		{
			gREPout->ExceptionNotify(&_info.exception);
			return 1;
		}
		end_try;
	}

	for (int i = first; i < argc; i++)
	{
		if (strcmp(argv[i], "-e") == 0 && i + 1 < argc)
		{
			newton_try
			{
				RefVar fn(ParseString(RefVar(MakeString(argv[i + 1]))));
				if ((Ref) fn != NILREF)
				{
					RefVar result(InterpretBlock(fn, RefVar(gREPContext)));
					PrintObject(result, 0);
					gREPout->Print("\r");
				}
			}
			newton_catch_all
			{
				gREPout->ExceptionNotify(&_info.exception);
			}
			end_try;
			ClearRefHandles();
			i++;
			ranSomething = true;
		}
		else if (strcmp(argv[i], "-i") == 0)
			interactive = true;
		else
		{
			newton_try
			{
				ParseFile(argv[i]);
			}
			newton_catch_all
			{
				gREPout->ExceptionNotify(&_info.exception);
			}
			end_try;
			ClearRefHandles();
			ranSomething = true;
		}
	}
	if (!ranSomething || interactive)
	{
		while (gREPin->FrameAvailable())
			REPAcceptLine();
		gREPout->Print("\r");
	}
	gREPout->Flush();
	return 0;
}
