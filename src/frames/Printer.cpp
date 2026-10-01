/*
	File:		frames/Printer.cpp

	Contains:	The object printer: NewtonScript objects as source text on
				the REP's out translator (REPTranslators.h) - PrintObject
				and PrintObjectAux, the bytecode disassembler
				(PrintInstructions, Disassemble), objects as strings
				(StringObject, SPrintObject) and the printing natives.

	The globals printDepth, printLength, prettyPrint and printInstructions
	(gVarFrame's slots) shape the output as the NTK's Inspector knows them.
	The ROM's line end is a carriage return; the strings here keep it.
*/

#include "REPTranslators.h"
#include "NumberFormat.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "Unicode.h"
#include "RichString.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

// the objects being printed, by depth, so that a cycle prints as a
// reference back (0x0c1027b0, unnamed in the debug symbols)
static Ref			gPrintPrecedents = NILREF;
const long			kPrintPrecedentsSize = 16;

extern const char* const	gPrintLiterals[35];		// PrintLiterals.cpp: the opcode names - [1 + a] for a << 3, [27 + b] for the simple ones
extern const FreqFuncInfo	gFreqFuncInfo[];


/* -------------------------------------------------------------------------------
	Setup
------------------------------------------------------------------------------- */

// ROM 0x0033c7cc InitPrinter__Fv
// DEVIATION: the ROM's boot has a translator in gREPout (gBootOut) before
// InitObjects; here, until the REP is started (HostInitREP), a null one
// keeps the printer from printing to nothing.
void
InitPrinter(void)
{
	gPrintPrecedents = AllocateArray(RSSYMarray, kPrintPrecedentsSize);
	AddGCRoot(gPrintPrecedents);
	if (gREPout == nil)
		CreateNullOutTranslator(&gREPout);
}


// ROM 0x0033c800 IsAggregate__FRC6RefVar
// A frame or an array (a slotted object that is not a symbol).
Boolean
IsAggregate(RefArg obj)
{
	return ISPTR(obj) && (ObjectFlags(obj) & kObjSlotted) != 0 && !IsSymbol(obj);
}


/* -------------------------------------------------------------------------------
	Printing
------------------------------------------------------------------------------- */

// ROM 0x0033c730 SafelyPrintString__FPUs
// A string through Print's %U, at most 250 characters at a time.
void
SafelyPrintString(UniChar* str)
{
	long length = Ustrlen(str);
	if (length < 250)
	{
		gREPout->Print("%U", str);
		return;
	}
	UniChar buffer[256];
	while (length > 0)
	{
		long chunk = length > 250 ? 250 : length;
		BlockMove(str, buffer, chunk * sizeof(UniChar));
		buffer[chunk] = 0;
		gREPout->Print("%U", buffer);
		str += chunk;
		length -= chunk;
	}
}


// a symbol's name, |quoted| when it is not an identifier
static void
PrintSymbol(RefArg sym)
{
	const char* name = SymbolName(sym);
	const unsigned char* p = (const unsigned char*) name;
	Boolean needsBars = true;
	if (*p != 0 && (isalpha(*p) || *p == '_'))
	{
		needsBars = false;
		for (; *p != 0; p++)
			if (!isalnum(*p) && *p != '_')
			{
				needsBars = true;
				break;
			}
	}
	if (!needsBars)
	{
		gREPout->Print("%s", name);
		return;
	}
	gREPout->Print("|");
	Boolean inEscape = false;			// after \u, hex digit pairs until the next \u
	for (p = (const unsigned char*) name; *p != 0; p++)
	{
		unsigned char c = *p;
		if (inEscape)
		{
			if (c < 0x20 || c > 0x7f)
				gREPout->Print("%02X", c);
			else
			{
				if (c == '\\')
					gREPout->Print("\\u\\\\");
				else if (c == '|')
					gREPout->Print("\\u\\|");
				else
					gREPout->Print("\\u%c", c);
				inEscape = false;
			}
		}
		else
		{
			if (c < 0x20 || c > 0x7f)
			{
				if (c == '\r')
					gREPout->Print("\\n");
				else if (c == '\t')
					gREPout->Print("\\t");
				else
				{
					gREPout->Print("\\u%02X", c);
					inEscape = true;
				}
			}
			else if (c == '\\')
				gREPout->Print("\\\\");
			else if (c == '|')
				gREPout->Print("\\|");
			else
				gREPout->Print("%c", c);
		}
	}
	gREPout->Print("|");
}


// whether the first printLength (all when -1) elements of an aggregate
// hold an aggregate - then each element goes on a line of its own
static Boolean
HasAggregateElements(RefArg obj, long printLength)
{
	Boolean hasAggregate = false;
	TObjectIterator iter(obj);
	for (long i = 0; !iter.Done() && !hasAggregate && (printLength < 0 || i < printLength); i++)
	{
		hasAggregate = IsAggregate(iter.fValue);
		iter.Next();
	}
	return hasAggregate;
}


// ROM 0x0033c850 PrintObjectAux__FRC6RefVarlT2
// obj as NewtonScript source, indented by indent (for the lines after the
// first) at nesting depth.  Integers, characters ($c, $\xx, $\uxxxx), the
// immediates, symbols, reals (%#g), strings (quoted), other binaries
// (<class, length n>), path expressions (a.b.c), arrays ([class: a, b])
// and frames ({tag: value, ...}) to printDepth, beyond which an aggregate
// is [#addr] or {#addr} ([@n], {@n} for a magic pointer); functions print
// as <function, n arg(s) #addr> when prettyPrint is set; a cycle prints
// as <depth>; printLength elements at most, then "...".  An exception
// while printing prints as *** name ***.
void
PrintObjectAux(RefArg obj, long indent, long depth)
{
	RefVar object(ForwardReference(obj));
	RefVar objClass(NILREF);
	newton_try
	{
		Ref ref = object;
		switch (ref & kRefTagMask)
		{
		case kTagInteger:
			gREPout->Print("%lld", (long long) RVALUE(ref));
			break;

		case kTagImmed:
			if (EQRef(ref, TRUEREF))
				gREPout->Print("TRUE");
			else if (ref == NILREF)
				gREPout->Print("NIL");
			else if (ISCHAR(ref))
			{
				ULong c = RCHAR(ref) & 0xffff;
				if (c >= 0x100)
					gREPout->Print("$\\u%04lX", (long) c);
				else if (c < 0x20 || c > 0x7f)
					gREPout->Print("$\\%02lX", (long) c);
				else if (c == '\\')
					gREPout->Print("$\\");
				else if (c == 8)				// (unreachable: 8 and 13 are under 0x20)
					gREPout->Print("$\t");
				else if (c == 13)
					gREPout->Print("$\r");
				else
					gREPout->Print("$%c", (int) c);
			}
			else if (ref == kSymbolClass)
				gREPout->Print("<symbol class>");
			else if (ref == kWeakArrayClass)
				gREPout->Print("<weak array class>");
			else if (ref == kDeclawedRef)
				gREPout->Print("<bad pkg ref>");
			else
				gREPout->Print("#%lX", (long) ref);
			break;

		case kTagPointer:
		case kTagMagicPtr:
		{
			// a cycle: the object is already being printed at a lesser depth
			long limit = depth < kPrintPrecedentsSize ? depth : kPrintPrecedentsSize;
			long i;
			for (i = 0; i < limit; i++)
				if (EQRef(GetArraySlotRef(gPrintPrecedents, i), ref))
					break;
			if (i < limit)
			{
				gREPout->Print("<%d>", i);
				break;
			}
			if (depth < kPrintPrecedentsSize)
				SetArraySlotRef(gPrintPrecedents, depth, ref);

			long printDepth = RINT(GetFrameSlotRef(gVarFrame, RSSYMprintdepth));
			if (printDepth > 15)
				printDepth = 15;
			Ref printLengthRef = GetFrameSlotRef(gVarFrame, RSSYMprintlength);
			long printLength = printLengthRef == NILREF ? -1 : RINT(printLengthRef);
			Boolean prettyPrint = GetFrameSlotRef(gVarFrame, RSSYMprettyprint) != NILREF;
			ULong flags = ObjectFlags(ref);

			if ((flags & kObjSlotted) == 0)
			{
				// a binary
				objClass = ClassOf(object);
				if (IsSymbol(object))
					PrintSymbol(object);
				else if (EQRef(objClass, RSSYMreal))
					gREPout->Print("%#g", CDouble(object));
				else if (EQRef(objClass, RSSYMinstructions) && GetFrameSlotRef(gVarFrame, RSSYMprintinstructions) != NILREF)
					PrintInstructions(object);
				else if (!IsSymbol(objClass))
				{
					long n = gREPout->Print("<Binary, class ");
					PrintObjectAux(objClass, indent + n, depth + 1);
					gREPout->Print(", length %d>", Length(object));
				}
				else if (IsSubclassRef(objClass, RSSYMstring))
				{
					gREPout->Print("\"");
					SafelyPrintString((UniChar*) BinaryData(object));
					gREPout->Print("\"");
				}
				else
					gREPout->Print("<%s, length %d>", SymbolName(objClass), Length(object));
			}

			else if ((flags & kObjFrame) == 0)
			{
				// an array
				if (flags & kObjForward)
				{
					gREPout->Print("<forwarding pointer #%X...WTF!!!>", (long) ref);
					break;
				}
				objClass = ClassOf(object);
				if (EQRef(objClass, RSSYMpathexpr))
				{
					long count = Length(object);
					Boolean allSymbols = true;
					for (i = 0; i < count; i++)
						if (!IsSymbol(GetArraySlotRef(object, i)))
						{
							allSymbols = false;
							break;
						}
					if (allSymbols)
					{
						for (i = 0; i < count; i++)
						{
							PrintObjectAux(RefVar(GetArraySlotRef(object, i)), indent, depth + 1);
							if (i < count - 1)
								gREPout->Print(".");
						}
					}
					else
					{
						long n = gREPout->Print("[pathExpr: ");
						for (i = 0; i < count; i++)
						{
							PrintObjectAux(RefVar(GetArraySlotRef(object, i)), indent + n, depth + 1);
							if (i < count - 1)
								gREPout->Print(", ");
						}
						gREPout->Print("]");
					}
				}
				else if (depth > printDepth)
				{
					if ((ref & kRefTagMask) == kTagMagicPtr)
						gREPout->Print("[@%ld]", (long) (ref >> kRefTagBits));
					else
						gREPout->Print("[#%lX]", (long) ref);
				}
				else
				{
					long count = Length(object);
					long inner = indent + gREPout->Print("[");
					Boolean hasAggregate = prettyPrint && HasAggregateElements(object, printLength);
					if (!EQRef(objClass, RSSYMarray))
					{
						if (!IsSymbol(objClass))
						{
							PrintObjectAux(objClass, inner, depth + 1);
							if (IsAggregate(objClass))
								gREPout->Print(": \r%*s", inner, "");
							else
								gREPout->Print(": ");
						}
						else
						{
							long n = gREPout->Print("%s: ", SymbolName(objClass));
							if (hasAggregate)
								gREPout->Print("\r%*s", inner, "");
							else
								inner += n;
						}
					}
					TObjectIterator iter(object);
					for (i = 0; !iter.Done(); i++)
					{
						if (printLength >= 0 && i >= printLength)
						{
							gREPout->Print("...");
							break;
						}
						PrintObjectAux(iter.fValue, inner, depth + 1);
						if (--count != 0)
						{
							gREPout->Print(", ");
							if (hasAggregate)
								gREPout->Print("\r%*s", inner, "");
						}
						iter.Next();
					}
					gREPout->Print("]");
				}
			}

			else
			{
				// a frame
				if (IsFunction(object) && prettyPrint)
				{
					gREPout->Print(IsNativeFunction(object) ? "<native function, " : "<function, ");
					gREPout->Print("%ld arg(s) #%lX>", GetFunctionArgCount(object), (long) ref);
				}
				else if (depth > printDepth)
				{
					if ((ref & kRefTagMask) == kTagMagicPtr)
						gREPout->Print("{@%ld}", (long) (ref >> kRefTagBits));
					else
						gREPout->Print("{#%lX}", (long) ref);
				}
				else
				{
					long count = Length(object);
					long inner = indent + gREPout->Print("{");
					Boolean hasAggregate = prettyPrint && HasAggregateElements(object, printLength);
					TObjectIterator iter(object);
					for (i = 0; !iter.Done(); i++)
					{
						if (printLength >= 0 && i >= printLength)
						{
							gREPout->Print("...");
							break;
						}
						Ref tag = iter.fTag;
						Ref value = iter.fValue;
						if (EQRef(tag, RSSYMviewcobject) && ISINT(value))
							gREPout->Print("%s: 0x%lX", SymbolName(tag), RVALUE(value));
						else if (EQRef(tag, RSSYMfuncptr))
							gREPout->Print("%s: 0x%lX", SymbolName(tag), (long) value);
						else
						{
							long n = gREPout->Print("%s: ", SymbolName(tag));
							PrintObjectAux(iter.fValue, inner + n, depth + 1);
						}
						if (--count != 0)
						{
							gREPout->Print(", ");
							if (hasAggregate)
								gREPout->Print("\r%*s", inner, "");
						}
						iter.Next();
					}
					gREPout->Print("}");
				}
			}
			if (depth < kPrintPrecedentsSize)
				SetArraySlotRef(gPrintPrecedents, depth, NILREF);
			break;
		}
		}
	}
	newton_catch((ExceptionName) "evt.ex.msg")
	{
		gREPout->Print("*** %s ***", (const char*) _info.exception.data);
	}
	newton_catch((ExceptionName) "evt.ex")
	{
		gREPout->Print("*** %s ***", _info.exception.name);
	}
	end_try;
}


// ROM 0x0033d7dc PrintObject__FRC6RefVarUl
void
PrintObject(RefArg obj, long indent)
{
	gREPout->ConsumeFrame(obj, 0, indent);
}


/* -------------------------------------------------------------------------------
	Bytecode
------------------------------------------------------------------------------- */

// ROM 0x002c19d8 PrintInstructions__FRC6RefVar
// The instructions of a function, on one line: <Instrs: get-var 3, ...>.
void
PrintInstructions(RefArg instructions)
{
	long count = Length(instructions);
	const unsigned char* pc = (const unsigned char*) BinaryData(instructions);
	gREPout->Print("<Instrs: ");
	while (count > 0)
	{
		unsigned int a = *pc >> 3;
		unsigned int b = *pc & 7;
		if (a == 0)
			gREPout->Print("%s", gPrintLiterals[27 + b]);
		else
		{
			gREPout->Print("%s", gPrintLiterals[1 + a]);
			if (a > 2)
			{
				long operand = b;
				if (b == 7)
					operand = (short) ((pc[1] << 8) | pc[2]);
				gREPout->Print(" %ld", operand);
			}
		}
		long size = b == 7 ? 3 : 1;
		count -= size;
		pc += size;
		if (count != 0)
			gREPout->Print(", ");
	}
	gREPout->Print(">");
}


// ROM 0x002c1b18 PrintInstruction__FiPUcRC6RefVarN23
// One instruction with what its operand names: the literal for push,
// find-var and set-find-var; the constant for push-constant; the local's
// name for get-var and set-var (from the argFrame of a 1.x code block,
// or from the debug information - a 'dbg1 array, its first element the
// index of the first named local - of a 2.x function); the function for
// freq-func.
static void
PrintInstruction(Boolean is2xFunction, const unsigned char* pc, RefArg literals, RefArg argFrame, RefArg debugInfo)
{
	long firstLocal = 0;
	if ((Ref) debugInfo != NILREF)
		firstLocal = RINT(GetArraySlotRef(debugInfo, 0)) + 1;
	unsigned int a = *pc >> 3;
	long b = *pc & 7;
	if (b == 7)
		b = (short) ((pc[1] << 8) | pc[2]);
	if (a == 0)
	{
		gREPout->Print("%s", gPrintLiterals[27 + b]);
		return;
	}
	gREPout->Print("%s ", gPrintLiterals[1 + a]);
	if (a == kBCPush || a == kBCFindVar || a == kBCFindAndSetVar)
		PrintObject(RefVar(GetArraySlotRef(literals, b)), 0);
	else if (a == kBCPushConstant)
		PrintObject(RefVar((Ref) b), 0);
	else if (a == kBCGetVar || a == kBCSetVar)
	{
		if (!is2xFunction)
		{
			RefVar map(ObjClass(OBJ(argFrame)));
			gREPout->Print("%ld [%s]", b, SymbolName(GetTag(map, b, nil)));
			return;
		}
		if ((Ref) debugInfo != NILREF)
		{
			long index = b + firstLocal - 3;
			Ref name = GetArraySlotRef(debugInfo, index);
			if (name != NILREF)
			{
				gREPout->Print("%ld [%s]", b, SymbolName(name));
				return;
			}
		}
		gREPout->Print("%ld", b);
	}
	else if (a == kBCFreqFunc)
		gREPout->Print("%ld [%s/%ld]", b, gFreqFuncInfo[b].fName, gFreqFuncInfo[b].fNumArgs);
	else
		gREPout->Print("%ld", b);
}


// ROM 0x002c1e68 Disassemble__FRC6RefVar
// A function's instructions, one per line with its pc.
void
Disassemble(RefArg fn)
{
	if (!EQRef(ClassOf(fn), RSSYMcodeblock) && !EQRef(ClassOf(fn), RSSYM_function))
		ThrowMsg((char*) "not a codeblock");
	RefVar instructions(GetArraySlotRef(fn, kFunctionInstructionsSlot));
	RefVar literals(GetArraySlotRef(fn, kFunctionLiteralsSlot));
	RefVar argFrame(GetArraySlotRef(fn, kFunctionArgFrameSlot));
	RefVar debugInfo(NILREF);
	if (Length(fn) > 5)
		debugInfo = GetArraySlotRef(fn, 5);
	if (!EQRef(ClassOf(debugInfo), Intern((char*) "dbg1")))
		debugInfo = NILREF;
	const unsigned char* code = (const unsigned char*) BinaryData(instructions);
	long count = Length(instructions);
	Boolean is2xFunction = EQRef(ClassOf(fn), RSSYM_function);
	for (long pc = 0; pc < count; )
	{
		gREPout->Print("%4d: ", pc);
		PrintInstruction(is2xFunction, code + pc, literals, argFrame, debugInfo);
		gREPout->Print("\r");
		pc += (code[pc] & 7) == 7 ? 3 : 1;
	}
}


/* -------------------------------------------------------------------------------
	Well-known objects and slot names (the stack trace's helpers)
------------------------------------------------------------------------------- */

// ROM 0x002d22a0 FindSlotName__FRC6RefVarT1
// The tag of the slot of context holding value, or nil.
Ref
FindSlotName(RefArg context, RefArg value)
{
	TObjectIterator iter(context, true);
	for (; !iter.Done(); iter.Next())
		if (EQRef(iter.fValue, value))
			return iter.fTag;
	return NILREF;
}


// ROM 0x002bf3c0 GetFramesErrorString__Fl
// The message of a frames error code; this ROM has none.
const char*
GetFramesErrorString(long /*error*/)
{
	return nil;
}


// ROM 0x002bf3c8 PrintFramesErrorMsg__FPCcRC6RefVar
// A message with %slot. references to the exception frame's slots
// printed in place.
void
PrintFramesErrorMsg(const char* message, RefArg data)
{
	for (const char* p = message; *p != 0; )
	{
		if (*p != '%')
		{
			gREPout->Putc((unsigned char) *p++);
			continue;
		}
		const char* nameStart = p + 1;
		const char* dot = strchr(nameStart, '.');
		if (dot == nil)
		{
			gREPout->Putc((unsigned char) *p++);
			continue;
		}
		long length = dot - nameStart;
		char* name = new char[length + 1];
		newton_try
		{
			strncpy(name, nameStart, length);
			name[length] = 0;
			RefVar value(GetFrameSlotRef(data, RefVar(Intern(name))));
			PrintObject(value, 0);
		}
		cleanup
		{
			delete[] name;
		}
		end_try;
		delete[] name;
		p = dot + 1;
	}
}


/* -------------------------------------------------------------------------------
	Objects as strings
------------------------------------------------------------------------------- */

// ROM 0x001ab8b0 GetStringFormat__FRC6RefVar
// The format bits of a string: the low two bits of its last UniChar (the
// terminator, 0, for a plain string).
long
GetStringFormat(RefArg str)
{
	long length = Length(str);
	const UniChar* s = (const UniChar*) BinaryData(str);
	return s[length / sizeof(UniChar) - 1] & 3;
}


// ROM 0x001ab1c8 IsRichString__FRC6RefVar
Boolean
IsRichString(RefArg str)
{
	return IsString(str) && GetStringFormat(str) != 0;
}


// ROM 0x000ecf04 IntegerString__FlPUs
void
IntegerString(Long i, UniChar* str)
{
	char buffer[32];
	snprintf(buffer, sizeof(buffer), "%lld", (long long) i);
	ConvertToUnicode(buffer, str, kMacRomanEncoding, sizeof(buffer));
}


// ROM 0x001298c4 StringObject__FRC6RefVarPUsRll
Boolean
StringObject(RefArg obj, UniChar* buffer, long& length, long maxLength)
{
	UniChar text[64];
	const UniChar* source = text;
	long count;
	Boolean hasText = true;
	TBinaryDataPtr locked;
	if (IsString(obj))
	{
		locked = (Ref) obj;
		source = (const UniChar*) (char*) locked;
		count = (Length(obj) - 2) / 2;
	}
	else
	{
		Ref ref = obj;
		if (ref == NILREF)
			count = 0;
		else if (ISCHAR(ref))
		{
			text[0] = RCHAR(ref);
			count = 1;
		}
		else
		{
			if (ISINT(ref))
				IntegerString(RVALUE(ref), text);
			else if (ISREAL(ref))
				NumberString(CDouble(obj), text, 63, "%.15g");
			else if (EQRef(ClassOf(obj), Intern((char*) "symbol")))
				ConvertToUnicode(SymbolName(obj), text, kMacRomanEncoding, 63);
			else
			{
				length = 0;
				return false;
			}
			count = Ustrlen(text);
		}
	}
	if (count > maxLength)
		count = maxLength;
	if (buffer != nil && count != 0)
	{
		BlockMove(source, buffer, count * sizeof(UniChar));
		buffer[count] = 0;
	}
	length = count;
	return hasText;
}


// ROM 0x00129ae4 SPrintObject__FRC6RefVar
// obj's text as a new string.
Ref
SPrintObject(RefArg obj)
{
	long length;
	StringObject(obj, nil, length, 0x7fffffff);
	RefVar str(AllocateBinary(RSSYMstring, (length + 1) * sizeof(UniChar)));
	TBinaryDataPtr locked(str);
	StringObject(obj, (UniChar*) (char*) locked, length, length);
	return str;
}


// ROM 0x002b683c StringerStringObject__FRC6RefVarPcPlT2T3
// The text of one object for Stringer: its UniChars into text (nil: only
// the length, in bytes) and, for a rich string, its ink data into
// inkData (inkLength).  A string, nil (nothing), a character, an
// integer, a real (%g, "0.0" for zero) or a symbol; ==> false for
// anything else.  A string's text and ink come from
// TRichString::DoStringerStuff, so a rich string's ink is carried over.
static Boolean
StringerStringObject(RefArg obj, char* text, long* length, char* inkData, long* inkLength)
{
	*inkLength = 0;
	if (IsString(obj))
	{
		TRichString s(obj);
		s.DoStringerStuff(text, length, inkData, inkLength);
		return true;
	}
	Ref ref = obj;
	char buffer[32];
	if (ref == NILREF)
	{
		*length = 0;
		return true;
	}
	if (ISCHAR(ref))
	{
		*length = sizeof(UniChar);
		if (text != nil)
			*(UniChar*) text = RCHAR(ref);
		return true;
	}
	if (ISINT(ref))
		*length = snprintf(buffer, sizeof(buffer), "%lld", (long long) RVALUE(ref)) * sizeof(UniChar);
	else if (ISREAL(ref))
	{
		double d = CDouble(obj);
		if (d == 0.0)
			strcpy(buffer, "0.0");
		else
			snprintf(buffer, sizeof(buffer), "%g", d);
		*length = strlen(buffer) * sizeof(UniChar);
	}
	else if (IsSymbol(obj))
	{
		const char* name = SymbolName(obj);
		*length = strlen(name) * sizeof(UniChar);
		if (text != nil)
			ConvertToUnicode(name, (UniChar*) text, kMacRomanEncoding, strlen(name));
		return true;
	}
	else
	{
		*length = 0;
		return false;
	}
	if (text != nil)
		ConvertToUnicode(buffer, (UniChar*) text, kMacRomanEncoding, strlen(buffer));
	return true;
}


// ROM 0x002b6a68 Stringer__FRC6RefVar
// The objects of an array as one string (the & operator's).  With ink
// data among them the string is a rich one: the text, padding, the ink
// and a trailer word (text length << 4 | 1).
Ref
Stringer(RefArg array)
{
	long count = Length(array);
	long textLength = 0;
	long inkLength = 0;
	RefVar element;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array, i);
		long length, ink;
		StringerStringObject(element, nil, &length, nil, &ink);
		textLength += length;
		inkLength += ink;
	}
	long inkOffset = 0;
	long size;
	if (inkLength == 0)
		size = textLength + sizeof(UniChar);
	else
	{
		inkOffset = (textLength + 5) & ~3;
		size = inkOffset + inkLength + 4;
	}
	RefVar str(AllocateBinary(RSSYMstring, size));
	TBinaryDataPtr locked(str);
	char* text = (char*) locked;
	char* ink = text + inkOffset;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array, i);
		long length, inkSize;
		StringerStringObject(element, text, &length, ink, &inkSize);
		text += length;
		ink += inkSize;
	}
	if (inkLength > 0)
	{
		// (host: written as the two UniChars TRichString::SetFormatAndLength
		// reads it back as, as MungeRange and MakeRichString write it,
		// rather than as the ROM's one word.)
		UniChar* trailer = (UniChar*) ((char*) locked + size - 4);
		ULong word = ((textLength / 2) << 4) | 1;
		trailer[0] = (UniChar) (word >> 16);
		trailer[1] = (UniChar) word;
	}
	return str;
}


/* -------------------------------------------------------------------------------
	Natives
------------------------------------------------------------------------------- */

// ROM 0x002b6c2c FFramesStringer
Ref
FFramesStringer(RefArg /*rcvr*/, RefArg array)
{
	return Stringer(array);
}


// ROM 0x002b6c34 FEvalStringer
// EvalStringer(context, array): the elements stringed together, with
// each symbol among them standing for the variable of that name in the
// context given - not in the receiver, which the ROM does not look at.
Ref
FEvalStringer(RefArg /*rcvr*/, RefArg context, RefArg array)
{
	long count = Length(array);
	RefVar parts(Clone(array));
	RefVar element;
	for (long i = 0; i < count; i++)
	{
		element = GetArraySlotRef(array, i);
		if (EQRef(ClassOf(element), RSSYMsymbol))
		{
			element = GetVariable(context, element, nil, 0);
			SetArraySlotRef(parts, i, element);
		}
	}
	return Stringer(parts);
}


// ROM 0x002b7ee0 FPrint
Ref
FPrint(RefArg /*rcvr*/, RefArg obj)
{
	PrintObject(obj, 0);
	gREPout->Print("\r");
	return NILREF;
}


// ROM 0x002b7f18 FDisplay
Ref
FDisplay(RefArg /*rcvr*/, RefArg obj)
{
	PrintObject(obj, 0);
	return NILREF;
}


// ROM 0x001ed0ec FSPrintObject__FRC6RefVarT1
// obj's text as a string (a rich string becomes a plain one), at most
// 1000 characters.
Ref
FSPrintObject(RefArg /*rcvr*/, RefArg obj)
{
	RefVar str(NILREF);
	if (IsRichString(obj))
	{
		str = Clone(obj);
		SetClass(str, RSSYMstring);
		return str;
	}
	long length;
	StringObject(obj, nil, length, 1000);
	length++;
	str = AllocateBinary(RSSYMstring, length * sizeof(UniChar));
	TBinaryDataPtr locked(str);
	StringObject(obj, (UniChar*) (char*) locked, length, length);
	return str;
}


void
RegisterPrinterNatives(void)
{
	RegisterNativeFunction("FPrint", (void*) FPrint, 1);
	RegisterNativeFunction("FDisplay", (void*) FDisplay, 1);
	RegisterNativeFunction("FSPrintObject__FRC6RefVarT1", (void*) FSPrintObject, 1);
	RegisterNativeFunction("FFramesStringer", (void*) FFramesStringer, 1);
	RegisterNativeFunction("FEvalStringer", (void*) FEvalStringer, 2);
}
