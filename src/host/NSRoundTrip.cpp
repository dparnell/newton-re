/*
	File:		host/NSRoundTrip.cpp

	Contains:	The NewtonScript decompiler's round trip (newtonscript
				--roundtrip): each function the decompiler wrote as source
				(tools/newton-rom/analysis/nsdecompile.py) compiled by the
				reconstructed compiler (frames/Compiler.h) and compared with
				the ROM's own function object - its instructions byte for
				byte, its literals (symbols by name, strings and reals by
				their bytes, arrays and frames slot by slot, nested
				functions the same way), its argFrame and its numArgs.

	The input is a list of records:

		@@ 0x<ROM ref> <label>
		[@@const <name>
		 <source: one func expression> ...
		 @@main]
		<source: one func expression>
		@@end

	Each function is compiled as the NTK compiled the functions of a
	project, on its own at the top level (CompileFunctionString), with the
	variables' names not kept (dbgNoVarNames) and the NTK's constants
	(gCompilerNTKConstants).  A @@const section is a
	function the ROM's pushes as a literal it did not close over - which
	the NTK made from a constant evaluated when the project was built - and
	is compiled first and bound as a global constant of that name, so that
	the main function's reference to it pushes the same literal.  A
	section that is not a func is a value evaluated: a magic pointer or an
	object the ROM's pushes as a literal where the compiler would push an
	immediate or make a new literal each time - an NTK constant too.

	and the output a line per record: `0x<ref> OK`, or `0x<ref> FAIL
	<category> <detail>` with the category one of compile (the source
	does not compile: the message), instructions, literals, argFrame,
	numArgs or shape (the object's own layout), the detail saying where the
	first difference is.

*/

#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char gWhy[512];

static Boolean	Same(Ref oursRef, Ref romRef, const char* where);

static Boolean
IsCodeBlock(Ref r)
{
	if (!ISPTR(r) || !(ObjectFlags(r) & kObjSlotted))
		return false;
	return Length(r) >= 5 && GetArraySlotRef(r, 0) == kFuncClass;
}


static Boolean
Differ(const char* where, const char* what)
{
	if (gWhy[0] == 0)
		snprintf(gWhy, sizeof(gWhy), "%s: %s", where, what);
	return false;
}


// a function object against the ROM's: its instructions, literals, argFrame, numArgs
static const char*
CompareCode(Ref oursRef, Ref romRef, const char* where)
{
	char here[256];
	RefVar ours(oursRef), rom(romRef);
	if (!IsCodeBlock(ours) || !IsCodeBlock(rom))
	{
		Differ(where, "not a function");
		return "shape";
	}
	RefVar mine(GetArraySlotRef(ours, 1)), theirs(GetArraySlotRef(rom, 1));
	long n = Length(mine);
	if (n != Length(theirs) || memcmp(BinaryData(mine), BinaryData(theirs), n) != 0)
	{
		long first = 0;
		long m = Length(theirs) < n ? Length(theirs) : n;
		while (first < m && ((unsigned char*) BinaryData(mine))[first] == ((unsigned char*) BinaryData(theirs))[first])
			first++;
		snprintf(here, sizeof(here), "%s instructions (%ld bytes, the ROM's %ld; first difference at %ld)", where, n, (long) Length(theirs), first);
		Differ(here, "differ");
		// NSROUNDTRIP_DUMP=file: both instruction strings in hex, and our
		// literals, for looking at the difference (nsdecompile.py --compare)
		const char* dump = getenv("NSROUNDTRIP_DUMP");
		if (dump != nil && gWhy[0] != 0)
		{
			FILE* f = fopen(dump, "a");
			if (f != nil)
			{
				fprintf(f, "%s\nours ", where[0] ? where : "(the function)");
				for (long k = 0; k < n; k++)
					fprintf(f, "%02x", ((unsigned char*) BinaryData(mine))[k]);
				fprintf(f, "\nrom  ");
				for (long k = 0; k < Length(theirs); k++)
					fprintf(f, "%02x", ((unsigned char*) BinaryData(theirs))[k]);
				fprintf(f, "\n");
				fclose(f);
			}
		}
		return "instructions";
	}
	Ref lm = GetArraySlotRef(ours, 2), lt = GetArraySlotRef(rom, 2);
	snprintf(here, sizeof(here), "%s literals", where);
	if (!Same(lm, lt, here))
		return "literals";
	snprintf(here, sizeof(here), "%s argFrame", where);
	if (!Same(GetArraySlotRef(ours, 3), GetArraySlotRef(rom, 3), here))
		return "argFrame";
	if (GetArraySlotRef(ours, 4) != GetArraySlotRef(rom, 4))
	{
		snprintf(here, sizeof(here), "%s numArgs %ld, the ROM's %ld", where, (long) RINT(GetArraySlotRef(ours, 4)), (long) RINT(GetArraySlotRef(rom, 4)));
		Differ(here, "differ");
		return "numArgs";
	}
	return nil;
}


static Boolean
Same(Ref oursRef, Ref romRef, const char* where)
{
	// (held: the comparison allocates, and the heap may move objects)
	RefVar ours(oursRef), rom(romRef);
	char here[256];
	if ((Ref) ours == (Ref) rom)
		return true;
	if (!ISPTR(ours) || !ISPTR(rom))
	{
		snprintf(here, sizeof(here), "%#lx, the ROM's %#lx", (unsigned long) (Ref) ours, (unsigned long) (Ref) rom);
		return Differ(where, here);
	}
	if (IsSymbol(ours) || IsSymbol(rom))
	{
		if (IsSymbol(ours) && IsSymbol(rom) && strcmp(SymbolName(ours), SymbolName(rom)) == 0)
			return true;
		snprintf(here, sizeof(here), "%s, the ROM's %s", IsSymbol(ours) ? SymbolName(ours) : "(not a symbol)",
				 IsSymbol(rom) ? SymbolName(rom) : "(not a symbol)");
		return Differ(where, here);
	}
	if (IsCodeBlock(ours) || IsCodeBlock(rom))
	{
		char inner[300];
		snprintf(inner, sizeof(inner), "%s (a function)", where);
		return CompareCode(ours, rom, inner) == nil;
	}
	long fo = ObjectFlags(ours), fr = ObjectFlags(rom);
	if ((fo & (kObjSlotted | kObjFrame)) != (fr & (kObjSlotted | kObjFrame)))
		return Differ(where, "a different kind of object");
	if (!(fo & kObjSlotted))
	{
		if (!Same(ClassOf(ours), ClassOf(rom), where))
			return false;
		if (Length(ours) != Length(rom) || memcmp(BinaryData(ours), BinaryData(rom), Length(ours)) != 0)
			return Differ(where, "the bytes differ");
		return true;
	}
	if (fo & kObjFrame)
	{
		if (Length(ours) != Length(rom))
			return Differ(where, "frames of different sizes");
		// the tags, in order, then the values
		long count = Length(ours);
		RefVar tagsOurs(AllocateArray(RSSYMarray, count)), tagsRom(AllocateArray(RSSYMarray, count));
		long i = 0;
		{
			TObjectIterator iter(ours);
			for (; !iter.Done() && i < count; iter.Next(), i++)
				SetArraySlotRef(tagsOurs, i, iter.fTag);
		}
		i = 0;
		{
			TObjectIterator iter(rom);
			for (; !iter.Done() && i < count; iter.Next(), i++)
				SetArraySlotRef(tagsRom, i, iter.fTag);
		}
		for (i = 0; i < count; i++)
		{
			snprintf(here, sizeof(here), "%s tag %ld", where, i);
			if (!Same(GetArraySlotRef(tagsOurs, i), GetArraySlotRef(tagsRom, i), here))
				return false;
			snprintf(here, sizeof(here), "%s.%s", where, SymbolName(GetArraySlotRef(tagsRom, i)));
			if (!Same(GetArraySlotRef(ours, i), GetArraySlotRef(rom, i), here))
				return false;
		}
		return true;
	}
	if (!Same(ClassOf(ours), ClassOf(rom), where))
		return false;
	if (Length(ours) != Length(rom))
	{
		snprintf(here, sizeof(here), "%ld slots, the ROM's %ld", (long) Length(ours), (long) Length(rom));
		return Differ(where, here);
	}
	for (long i = 0; i < Length(ours); i++)
	{
		snprintf(here, sizeof(here), "%s[%ld]", where, i);
		if (!Same(GetArraySlotRef(ours, i), GetArraySlotRef(rom, i), here))
			return false;
	}
	return true;
}


// A record's source: its constants' sections, then the main function's.
// ==> the main function (the constants bound into gConstantsFrame, their
// names added to `names` to be taken away after).
static Ref
CompileRecord(char* source, RefArg names)
{
	char* main = source;
	char* p = source;
	while (strncmp(p, "@@const ", 8) == 0)
	{
		char* nameEnd = strchr(p + 8, '\n');
		if (nameEnd == nil)
			break;
		*nameEnd = 0;
		char* name = p + 8;
		char* body = nameEnd + 1;
		char* next = strstr(body, "\n@@");
		if (next == nil)
			break;
		*next = 0;
		// a function compiled on its own, or any other value evaluated
		char* start = body;
		while (*start == ' ' || *start == '\t' || *start == '\n')
			start++;
		RefVar fn;
		if (strncmp(start, "func", 4) == 0)
			fn = CompileFunctionString(RefVar(MakeString(body)));
		else
			fn = InterpretBlock(RefVar(ParseString(RefVar(MakeString(body)))), RefVar(NILREF));
		RefVar sym(Intern(name));
		SetFrameSlot(RefVar(gConstantsFrame), sym, fn);
		AddArraySlot(names, sym);
		p = next + 1;
		if (strncmp(p, "@@main", 6) == 0)
		{
			main = strchr(p, '\n');
			main = main != nil ? main + 1 : p + 6;
			break;
		}
	}
	return CompileFunctionString(RefVar(MakeString(main)));
}


int
RunRoundTrip(const char* inputPath, const char* outputPath)
{
	FILE* in = fopen(inputPath, "rb");
	if (in == nil)
	{
		fprintf(stderr, "newtonscript: cannot read %s\n", inputPath);
		return 1;
	}
	FILE* out = strcmp(outputPath, "-") == 0 ? stdout : fopen(outputPath, "w");
	if (out == nil)
	{
		fprintf(stderr, "newtonscript: cannot write %s\n", outputPath);
		fclose(in);
		return 1;
	}
	char line[4096];
	char* source = (char*) malloc(1);
	size_t length = 0;
	unsigned long romRef = 0;
	long total = 0, ok = 0;
	Boolean inRecord = false;
	// as the NTK built the ROM: no variable names kept in the functions
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "dbgNoVarNames")), RefVar(TRUEREF));
	gCompilerNTKConstants = true;
	RefVar constants(AllocateArray(RSSYMarray, 0));
	while (fgets(line, sizeof(line), in) != nil)
	{
		// (a line ends at its \n, whatever wrote the file)
		size_t lineLength = strlen(line);
		if (lineLength >= 2 && line[lineLength - 2] == '\r' && line[lineLength - 1] == '\n')
		{
			line[lineLength - 2] = '\n';
			line[lineLength - 1] = 0;
		}
		if (strncmp(line, "@@ ", 3) == 0)
		{
			romRef = strtoul(line + 3, nil, 16);
			length = 0;
			source[0] = 0;
			inRecord = true;
			continue;
		}
		if (strncmp(line, "@@end", 5) == 0 && inRecord)
		{
			inRecord = false;
			total++;
			gWhy[0] = 0;
			const char* category = nil;
			newton_try
			{
				RefVar rom(TranslateROMRef((ULong32) romRef));
				RefVar fn(CompileRecord(source, constants));
				if ((Ref) fn == NILREF)
				{
					category = "compile";
					snprintf(gWhy, sizeof(gWhy), "no function came of it");
				}
				else
					category = CompareCode(fn, rom, "");
			}
			newton_catch_all
			{
				category = "compile";
				Exception* e = CurrentException();
				snprintf(gWhy, sizeof(gWhy), "%s", e->name);
				if (e->data != nil && Subexception(e->name, (ExceptionName) "type.ref"))
				{
					RefVar data(*(RefStruct*) e->data);
					if (IsFrame(data))
					{
						Ref err = GetFrameSlotRef(data, RSSYMerrorcode);
						if (ISINT(err))
							snprintf(gWhy + strlen(gWhy), sizeof(gWhy) - strlen(gWhy), " %ld", (long) RINT(err));
					}
				}
			}
			end_try;
			for (long k = 0; k < Length(constants); k++)
				RemoveSlot(RefVar(gConstantsFrame), RefVar(GetArraySlotRef(constants, k)));
			SetLength(constants, 0);
			ClearRefHandles();
			if (category == nil)
			{
				ok++;
				fprintf(out, "%#lx OK\n", romRef);
			}
			else
				fprintf(out, "%#lx FAIL %s %s\n", romRef, category, gWhy);
			if ((total % 200) == 0)
				GC();
			continue;
		}
		if (inRecord)
		{
			size_t n = strlen(line);
			source = (char*) realloc(source, length + n + 1);
			memcpy(source + length, line, n + 1);
			length += n;
		}
	}
	fprintf(stderr, "newtonscript: %ld of %ld functions round-trip\n", ok, total);
	free(source);
	fclose(in);
	if (out != stdout)
		fclose(out);
	return 0;
}
