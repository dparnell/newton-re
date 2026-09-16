/*
	File:		host/newtonscript.cpp

	Contains:	newtonscript - the reconstructed NewtonScript running on the
				host: the object system over a ROM image, the compiler and
				the interpreter, with the REP reading from stdin (or files
				given on the command line) and printing to stdout, the way
				the ROM's REP runs over its stdio translators.

	Usage:
		newtonscript [--rom <image>] [--heap <bytes>] [-e <source>] [file.ns ...]

	Each file is loaded with ParseFile (each form compiled and run, as the
	NTK loads a text file); -e compiles and runs a string; with no files
	and no -e, or with -i, forms are read from stdin one line at a time
	(REPAcceptLine) until the end of the input.  The ROM image defaults to
	the MP2100 D image in DebugRom/ next to the source tree (NEWTON_ROM
	overrides); without a readable image the object system runs without
	the ROM's objects (its built-in NewtonScript functions are then
	missing, the reconstructed natives are not).  The host adds the global
	function ROMConstant(name): the ROM's R constant of that name (a
	string or symbol, case as in ROMConstants.h without the R:
	ROMConstant("canonicalTextShape")), nil for none - for looking at the
	ROM's objects from the REP.
*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "REPTranslators.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef NEWTON_DEFAULT_ROM_IMAGE
#define NEWTON_DEFAULT_ROM_IMAGE "DebugRom/MP2100 D/Senior DCirrusNoDebug image"
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
	Boolean interactive = false;
	Boolean ranSomething = false;
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
		else
			return Usage();
	}

	InitHostStandaloneHeap(heapSize + 0x400000);
	if (ImportROMObjectsFromFile(romImage) != noErr)
		fprintf(stderr, "newtonscript: no ROM image at %s; running without the ROM's objects\n", romImage);
	gObjectHeapSize = heapSize;
	InitObjects();
	HostInitREP(stdout, stdin);
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "ROMConstant")), RefVar(MakeCFunction((void*) FROMConstant, 1, nil)));

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
