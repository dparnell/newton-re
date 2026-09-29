/*
	File:		host/newtonscript.cpp

	Contains:	newtonscript - the reconstructed NewtonScript running on the
				host: the object system over a ROM image, the compiler and
				the interpreter, with the REP reading from stdin (or files
				given on the command line) and printing to stdout, the way
				the ROM's REP runs over its stdio translators.

	Usage:
		newtonscript [--rom <image>] [--heap <bytes>] [--display <w>x<h>[x<depth>]] [-e <source>] [file.ns ...]
		newtonscript [--rom <image>] --roundtrip <records> <results>
		newtonscript [--rom <image>] --compile-records <records> <output>

	Each file is loaded with ParseFile (each form compiled and run, as the
	NTK loads a text file); -e compiles and runs a string; with no files
	and no -e, or with -i, forms are read from stdin one line at a time
	(REPAcceptLine) until the end of the input.  The ROM image defaults to
	the MP2x00 US image in DebugRom/ next to the source tree (NEWTON_ROM
	overrides); without a readable image the object system runs without
	the ROM's objects (its built-in NewtonScript functions are then
	missing, the reconstructed natives are not).  The host adds the global
	function ROMConstant(name): the ROM's R constant of that name (a
	string or symbol, case as in ROMConstants.h without the R:
	ROMConstant("canonicalTextShape")), nil for none - for looking at the
	ROM's objects from the REP.  --display starts the view system over a
	host display of the size (and depth, 1 by default: 320x480 is the
	MessagePad's, 4 deep): AddView(GetRoot(), template), :Open(),
	RefreshViews() and ScreenSnapshot("file.pgm") then draw a view
	hierarchy into an image (host/HostViews.h).

	--roundtrip compiles each function the NewtonScript decompiler wrote
	(tools/newton-rom/analysis/nsdecompile.py) and compares it with the ROM's
	own, a result line per function (host/NSRoundTrip.cpp).
	--compile-records compiles such records and writes each function in the
	notation of the ROM object source, for romsrc.py's builder.
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

#ifndef NEWTON_DEFAULT_ROM_IMAGE
#define NEWTON_DEFAULT_ROM_IMAGE "DebugRom/MP2x00 US/Senior CirrusNoDebug image"
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


static int
Usage(void)
{
	fprintf(stderr, "usage: newtonscript [--rom <image>] [--heap <bytes>] [-e <source>] [-i] [file.ns ...]\n");
	return 2;
}


int
main(int argc, char** argv)
{
	const char* romImage = getenv("NEWTON_ROM");
	if (romImage == nil)
		romImage = NEWTON_DEFAULT_ROM_IMAGE;
	long heapSize = 0x400000;
	long displayWidth = 0, displayHeight = 0, displayDepth = 1;
	Boolean interactive = false;
	Boolean ranSomething = false;
	const char* roundTripIn = nil;
	const char* roundTripOut = nil;
	const char* compileIn = nil;
	const char* compileOut = nil;
	int first = 1;
	while (first < argc && argv[first][0] == '-' && argv[first][1] == '-')
	{
		if (strcmp(argv[first], "--rom") == 0 && first + 1 < argc)
		{
			romImage = argv[first + 1];
			first += 2;
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

	InitHostStandaloneHeap(heapSize + 0x400000);
	if (ImportROMObjectsFromFile(romImage) != noErr)
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
