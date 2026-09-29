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
		<source: one func expression, or a value evaluated>
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

	RunCompileRecords (newtonscript --compile-records) compiles the same
	records and writes each function as a value in the notation of the ROM
	object source (tools/newton-rom/analysis/romsrc.py, whose builder lays
	it out in the ROM's object area): `@@ 0x<ref>`, the value on one line,
	`@@end`; or `@@ 0x<ref> FAIL <why>`.  Strings, reals and the shapes
	kept as shorts are written back in the ROM's byte order, the reverse
	of frames/ObjectAreaImport.cpp.

	and --roundtrip's output is a line per record: `0x<ref> OK`, or `0x<ref> FAIL
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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static char gWhy[512];

static Boolean
IsBigEndianHost(void)
{
	const unsigned short one = 1;
	return *(const unsigned char*) &one == 0;
}

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
	// the main function compiled on its own - or, when it is not a func, a
	// value evaluated: a closure the NTK made when the project was built
	// (its argFrame's _nextArgFrame the argFrame of the call that made it)
	char* start = main;
	while (*start == ' ' || *start == '\t' || *start == '\n')
		start++;
	if (strncmp(start, "func", 4) != 0)
		return InterpretBlock(RefVar(ParseString(RefVar(MakeString(main)))), RefVar(NILREF));
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


/* -------------------------------------------------------------------------------
	The compiled functions in the notation of the ROM object source
------------------------------------------------------------------------------- */

static const char* const kReservedNames[] = { "nil", "true", "real", "string", "binary", "array", "map", "bytes" };

static void
WriteName(FILE* out, const char* name)
{
	Boolean plain = (isalpha((unsigned char) name[0]) || name[0] == '_');
	for (const char* p = name; *p && plain; p++)
		if (!isalnum((unsigned char) *p) && *p != '_')
			plain = false;
	for (size_t i = 0; plain && i < sizeof(kReservedNames) / sizeof(kReservedNames[0]); i++)
		if (strcasecmp(name, kReservedNames[i]) == 0)
			plain = false;
	if (plain)
	{
		fputs(name, out);
		return;
	}
	fputc('|', out);
	for (const char* p = name; *p; p++)
	{
		if (*p == '|' || *p == '\\')
			fputc('\\', out);
		fputc(*p, out);
	}
	fputc('|', out);
}


// UTF-16 units as the notation's string: printable ones as UTF-8, the
// rest \uXXXX (as romsrc.py's string_text)
static void
WriteStringText(FILE* out, const UniChar* s, long count)
{
	fputc('"', out);
	for (long i = 0; i < count; i++)
	{
		unsigned c = s[i];
		if (c == '"')
			fputs("\\\"", out);
		else if (c == '\\')
			fputs("\\\\", out);
		else if (c >= 0x20 && c < 0x7f)
			fputc((int) c, out);
		else if (c >= 0xa0 && !(c >= 0xd800 && c < 0xe000) && c != 0xfffe && c != 0xffff)
		{
			if (c < 0x800)
			{
				fputc(0xc0 | (c >> 6), out);
				fputc(0x80 | (c & 0x3f), out);
			}
			else
			{
				fputc(0xe0 | (c >> 12), out);
				fputc(0x80 | ((c >> 6) & 0x3f), out);
				fputc(0x80 | (c & 0x3f), out);
			}
		}
		else
			fprintf(out, "\\u%04x", c);
	}
	fputc('"', out);
}


static Boolean
IsClassNamedHost(Ref cls, const char* name)
{
	return ISPTR(cls) && IsSymbol(cls) && strcasecmp(SymbolName(cls), name) == 0;
}


// the classes the ROM importer keeps as shorts in host order
// (IsHalfwordShapeClass in frames/ObjectAreaImport.cpp)
static Boolean
IsHalfwordClassHost(Ref cls)
{
	static const char* const kClasses[] = { "boundsrect", "rectangle", "oval", "roundrectangle", "line",
											"polygonshape", "polygondata", "regiondata" };
	for (size_t i = 0; i < sizeof(kClasses) / sizeof(kClasses[0]); i++)
		if (IsClassNamedHost(cls, kClasses[i]))
			return true;
	return false;
}


static void WriteNotation(FILE* out, RefArg value, int depth);

static void
WriteBinary(FILE* out, RefArg obj, int depth)
{
	RefVar cls(ClassOf(obj));
	long length = Length(obj);
	const unsigned char* data = (const unsigned char*) BinaryData(obj);
	if (ISPTR(cls) && IsSymbol(cls) && IsSubclass(cls, RSSYMstring) && (length % 2) == 0)
	{
		// a string: its units, the terminator the ROM's has included
		// (the notation's string adds it back)
		const UniChar* s = (const UniChar*) data;
		long count = length / 2;
		if (count > 0 && s[count - 1] == 0)
		{
			fputs("string('", out);
			WriteName(out, SymbolName(cls));
			fputs(", ", out);
			WriteStringText(out, s, count - 1);
			fputc(')', out);
			return;
		}
	}
	if (IsClassNamedHost(cls, "real") && length == 8)
	{
		double v;
		memcpy(&v, data, sizeof(v));
		fputs("real('", out);
		WriteName(out, SymbolName(cls));
		fprintf(out, ", %.17g)", v);
		return;
	}
	fputs("bytes(", out);
	WriteNotation(out, cls, depth + 1);
	fputs(", \"", out);
	Boolean shorts = IsHalfwordClassHost(cls);
	for (long i = 0; i < length; i++)
	{
		// (the host's shorts back into the ROM's order)
		long k = shorts && (length % 2) == 0 ? (i ^ (IsBigEndianHost() ? 0 : 1)) : i;
		fprintf(out, "%02x", data[k]);
	}
	fputs("\")", out);
}


static void
WriteNotation(FILE* out, RefArg value, int depth)
{
	Ref r = value;
	if (depth > 64)
		ThrowExFramesWithBadValue(kNSErrOutOfRange, value);
	if (ISINT(r))
	{
		fprintf(out, "%ld", (long) RINT(r));
		return;
	}
	if (r == NILREF)
	{
		fputs("nil", out);
		return;
	}
	if (r == TRUEREF)
	{
		fputs("true", out);
		return;
	}
	if (ISCHAR(r))
	{
		fprintf(out, "$\\u%04x", (unsigned) RCHAR(r));
		return;
	}
	if ((r & 3) == 3)
	{
		// (a magic pointer's tag has the pointer bit too: before ISPTR)
		fprintf(out, "@%lu", (unsigned long) RVALUE(r));
		return;
	}
	if (!ISPTR(r))
	{
		fprintf(out, "#%lx", (unsigned long) (r & 0xffffffff));
		return;
	}
	if (IsSymbol(value))
	{
		fputc('\'', out);
		WriteName(out, SymbolName(value));
		return;
	}
	long flags = ObjectFlags(value);
	if (!(flags & kObjSlotted))
	{
		WriteBinary(out, value, depth);
		return;
	}
	if (flags & kObjFrame)
	{
		fputc('{', out);
		TObjectIterator iter(value);
		Boolean first = true;
		for (; !iter.Done(); iter.Next())
		{
			if (!first)
				fputs(", ", out);
			first = false;
			WriteName(out, SymbolName(iter.fTag));
			fputs(": ", out);
			WriteNotation(out, RefVar(iter.fValue), depth + 1);
		}
		fputc('}', out);
		return;
	}
	RefVar cls(ClassOf(value));
	long count = Length(value);
	if (ISPTR(cls) && IsSymbol(cls))
	{
		fputc('[', out);
		WriteName(out, SymbolName(cls));
		fputs(": ", out);
	}
	else
	{
		fputs("array(", out);
		WriteNotation(out, cls, depth + 1);
		if (count > 0)
			fputs(", ", out);
	}
	for (long i = 0; i < count; i++)
	{
		if (i > 0)
			fputs(", ", out);
		WriteNotation(out, RefVar(GetArraySlotRef(value, i)), depth + 1);
	}
	fputc(ISPTR(cls) && IsSymbol(cls) ? ']' : ')', out);
}


int
RunCompileRecords(const char* inputPath, const char* outputPath)
{
	FILE* in = fopen(inputPath, "rb");
	if (in == nil)
	{
		fprintf(stderr, "newtonscript: cannot read %s\n", inputPath);
		return 1;
	}
	FILE* out = strcmp(outputPath, "-") == 0 ? stdout : fopen(outputPath, "wb");
	if (out == nil)
	{
		fprintf(stderr, "newtonscript: cannot write %s\n", outputPath);
		fclose(in);
		return 1;
	}
	char line[4096];
	char* source = (char*) malloc(1);
	size_t length = 0;
	unsigned long ref = 0;
	long total = 0, failed = 0;
	Boolean inRecord = false;
	SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "dbgNoVarNames")), RefVar(TRUEREF));
	gCompilerNTKConstants = true;
	RefVar constants(AllocateArray(RSSYMarray, 0));
	while (fgets(line, sizeof(line), in) != nil)
	{
		size_t lineLength = strlen(line);
		if (lineLength >= 2 && line[lineLength - 2] == '\r' && line[lineLength - 1] == '\n')
		{
			line[lineLength - 2] = '\n';
			line[lineLength - 1] = 0;
		}
		if (strncmp(line, "@@ ", 3) == 0)
		{
			ref = strtoul(line + 3, nil, 16);
			length = 0;
			source[0] = 0;
			inRecord = true;
			continue;
		}
		if (strncmp(line, "@@end", 5) == 0 && inRecord)
		{
			inRecord = false;
			total++;
			newton_try
			{
				RefVar fn(CompileRecord(source, constants));
				fprintf(out, "@@ %#lx\n", ref);
				WriteNotation(out, fn, 0);
				fprintf(out, "\n@@end\n");
			}
			newton_catch_all
			{
				failed++;
				fprintf(out, "\n@@ %#lx FAIL %s\n", ref, CurrentException()->name);
			}
			end_try;
			for (long k = 0; k < Length(constants); k++)
				RemoveSlot(RefVar(gConstantsFrame), RefVar(GetArraySlotRef(constants, k)));
			SetLength(constants, 0);
			ClearRefHandles();
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
	if (failed != 0)
		fprintf(stderr, "newtonscript: %ld of %ld functions did not compile\n", failed, total);
	free(source);
	fclose(in);
	if (out != stdout)
		fclose(out);
	return failed != 0;
}
