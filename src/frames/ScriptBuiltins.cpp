/*
	File:		frames/ScriptBuiltins.cpp

	Contains:	The ROM's built-in functions that are NewtonScript, not
				native: function objects in its built-in functions frame
				(nsfunctions.py --list marks them "script"), re-expressed as
				NewtonScript source and compiled into gFunctionFrame when the
				host runs without the ROM's objects.  Each cites the ROM
				object it re-expresses; nsfunctions.py --disasm <name>
				shows the bytecode it was read from.  The tables here are
				the interpreter's own; other areas keep theirs (the union
				soups' in stores/UnionSoups.cpp) and install them with
				InstallScriptFunctions.

	Host re-expression: the ROM has the function objects in its object area.
*/

#include "NativeFunctions.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"

// ROM 0x005d206d (object) GlobalFnExists
// ROM 0x005d10fd (object) GlobalVarExists
// ROM 0x005d196d (object) GetGlobalFn
// ROM 0x005d211d (object) UnDefGlobalFn
static const ScriptFunctionEntry gScriptBuiltins[] = {
	{ "GlobalFnExists", "func(name) HasSlot(functions, name)" },
	{ "GlobalVarExists", "func(name) HasSlot(vars, name)" },
	{ "GetGlobalFn", "func(name) functions.(name)" },
	{ "UnDefGlobalFn", "func(name) begin RemoveSlot(functions, name); nil end" },
	{ nil, nil }
};


// A function object compiled from its source (a func expression).
Ref
CompileScriptFunction(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


// The table's functions compiled into gFunctionFrame under their names.
void
InstallScriptFunctions(const ScriptFunctionEntry* table)
{
	RefVar functions(gFunctionFrame);
	RefVar fn;
	for (const ScriptFunctionEntry* e = table; e->fName != nil; e++)
	{
		fn = CompileScriptFunction(e->fSource);
		SetFrameSlot(functions, RefVar(Intern((char*) e->fName)), fn);
	}
}


// Host: with no ROM objects imported the ROM's NewtonScript built-ins are
// compiled from the source above.
void
InstallHostScriptBuiltins(void)
{
	if (gROMBuiltinFunctions != NILREF)
		return;
	InstallScriptFunctions(gScriptBuiltins);
}
