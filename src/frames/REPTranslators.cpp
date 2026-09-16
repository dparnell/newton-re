/*
	File:		frames/REPTranslators.cpp

	Contains:	The REP's in and out translators (REPTranslators.h): the
				interfaces' glue, the null translators, the stdio out
				translator, the REP's globals and entry points.

	Host: the registry of protocols is a monitor of the kernel, so a
	standalone run of the object system (the frames tests) has none;
	the translators are then made from their class infos directly
	(DEVIATION in CreateNull...Translator).  REPFormat is the ROM's
	printf with %U, which its C library had (__vfprintf, 0x00311ce0).
	The stdio translator writes the Newton's carriage-return line ends
	as the host's newlines.
*/

#include "REPTranslators.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

PInTranslator*	gREPin = nil;			// 0x0c10190c
POutTranslator*	gREPout = nil;			// 0x0c101910
Ref				gREPContext = NILREF;	// 0x0c101900
long			gREPLevel = 0;			// 0x0c101904

extern const ExceptionName exTranslatorException;		// "evt.ex.translator"
extern const ExceptionName exCompiler;					// "evt.ex.fr.comp"


/* -------------------------------------------------------------------------------
	The interfaces' glue
------------------------------------------------------------------------------- */

PInTranslator*
PInTranslator::New(const char* implementation)
{
	PInTranslator* translator = (PInTranslator*) AllocInstanceByName("PInTranslator", implementation);
	return translator != nil ? (PInTranslator*) translator->GlueNew() : nil;
}

void
PInTranslator::Delete()
{
	GlueDelete();
}


POutTranslator*
POutTranslator::New(const char* implementation)
{
	POutTranslator* translator = (POutTranslator*) AllocInstanceByName("POutTranslator", implementation);
	return translator != nil ? (POutTranslator*) translator->GlueNew() : nil;
}

void
POutTranslator::Delete()
{
	GlueDelete();
}


/* -------------------------------------------------------------------------------
	printf with %U
------------------------------------------------------------------------------- */

// Each conversion goes to snprintf on its own, with the argument its type
// asks for; %U takes a UniChar string (narrowed as ConvertFromUnicode
// does).  ==> the length of the whole output, buffer holding at most
// size - 1 characters of it.
long
REPFormat(char* buffer, long size, const char* format, va_list args)
{
	long length = 0;
	char spec[32];
	char item[512];

	// append text to the output
	#define APPEND(text, n) \
		do { long count = (n); if (length < size - 1) { long room = size - 1 - length; if (count < room) room = count; memcpy(buffer + length, (text), room); } length += count; } while (0)

	const char* p = format;
	while (*p != 0)
	{
		if (*p != '%')
		{
			const char* start = p;
			while (*p != 0 && *p != '%')
				p++;
			APPEND(start, p - start);
			continue;
		}
		// a conversion: flags, width, precision, length, conversion
		const char* start = p++;
		if (*p == '%')
		{
			APPEND("%", 1);
			p++;
			continue;
		}
		while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0')
			p++;
		int width = -1, precision = -1;
		Boolean widthArg = false, precisionArg = false;
		if (*p == '*')
			widthArg = true, p++;
		else
			while (*p >= '0' && *p <= '9')
				p++;
		if (*p == '.')
		{
			p++;
			if (*p == '*')
				precisionArg = true, p++;
			else
				while (*p >= '0' && *p <= '9')
					p++;
		}
		Boolean isLong = false;
		while (*p == 'l' || *p == 'h')
		{
			if (*p == 'l')
				isLong = true;
			p++;
		}
		char conversion = *p++;
		if (widthArg)
			width = va_arg(args, int);
		if (precisionArg)
			precision = va_arg(args, int);
		// the spec for snprintf: the flags, then explicit width and precision, long for every integer
		long specLength = 0;
		spec[specLength++] = '%';
		for (const char* q = start + 1; *q == '-' || *q == '+' || *q == ' ' || *q == '#' || *q == '0'; q++)
			spec[specLength++] = *q;
		if (widthArg)
			specLength += snprintf(spec + specLength, sizeof(spec) - specLength, "%d", width);
		else
			for (const char* q = start + 1; q < p; q++)
				if (*q >= '1' && *q <= '9') { while (*q >= '0' && *q <= '9') spec[specLength++] = *q++; break; }
		if (precisionArg)
			specLength += snprintf(spec + specLength, sizeof(spec) - specLength, ".%d", precision);
		else
		{
			const char* dot = (const char*) memchr(start, '.', p - start);
			if (dot != nil)
			{
				spec[specLength++] = '.';
				for (const char* q = dot + 1; *q >= '0' && *q <= '9'; q++)
					spec[specLength++] = *q;
			}
		}
		spec[specLength] = 0;
		// the item, in the buffer or (a long string) one of its own
		int n = 0;
		char* text = item;
		#define FORMAT_ITEM(value) \
			do { n = snprintf(item, sizeof(item), spec, value); if (n >= (int) sizeof(item)) { text = new char[n + 1]; snprintf(text, n + 1, spec, value); } } while (0)
		switch (conversion)
		{
		case 'd': case 'i':
			strcat(spec, "ld");
			FORMAT_ITEM(isLong ? va_arg(args, long) : (long) va_arg(args, int));
			break;
		case 'u': case 'x': case 'X': case 'o':
			{
				char tail[3] = { 'l', conversion, 0 };
				strcat(spec, tail);
				FORMAT_ITEM(isLong ? va_arg(args, unsigned long) : (unsigned long) va_arg(args, unsigned int));
			}
			break;
		case 'c':
			strcat(spec, "c");
			FORMAT_ITEM(va_arg(args, int));
			break;
		case 's':
			{
				strcat(spec, "s");
				const char* s = va_arg(args, const char*);
				if (s == nil)
					s = "(null)";
				FORMAT_ITEM(s);
			}
			break;
		case 'e': case 'E': case 'f': case 'g': case 'G':
			{
				char tail[2] = { conversion, 0 };
				strcat(spec, tail);
				FORMAT_ITEM(va_arg(args, double));
			}
			break;
		case 'p':
			strcat(spec, "p");
			FORMAT_ITEM(va_arg(args, void*));
			break;
		case 'U':
			{
				const UniChar* s = va_arg(args, const UniChar*);
				long count = s != nil ? Ustrlen(s) : 0;
				char* narrow = new char[count + 1];
				if (s != nil)
					ConvertFromUnicode(s, narrow, kMacRomanEncoding, count);
				else
					narrow[0] = 0;
				strcat(spec, "s");
				FORMAT_ITEM(narrow);
				delete[] narrow;
			}
			break;
		default:
			// not a conversion: the text as it is
			APPEND(start, p - start);
			continue;
		}
		#undef FORMAT_ITEM
		if (n < 0)
			n = 0;
		APPEND(text, n);
		if (text != item)
			delete[] text;
	}
	#undef APPEND
	if (size > 0)
		buffer[length < size - 1 ? length : size - 1] = 0;
	return length;
}


/* -------------------------------------------------------------------------------
	PNullInTranslator
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PNullInTranslator)
PROTOCOL_CLASSINFO(PNullInTranslator, "PInTranslator", "", 0, 0, nil)

// ROM 0x00148c34 New__17PNullInTranslatorFv
PNullInTranslator*	PNullInTranslator::New()				{ return this; }
// ROM 0x00148c38 Delete__17PNullInTranslatorFv
void				PNullInTranslator::Delete()				{ }
// ROM 0x00148c3c Init__17PNullInTranslatorFPv
long				PNullInTranslator::Init(void*)			{ return noErr; }
// ROM 0x00148c44 Idle__17PNullInTranslatorFv
long				PNullInTranslator::Idle()				{ return 0; }
// ROM 0x00148c4c FrameAvailable__17PNullInTranslatorFv
Boolean				PNullInTranslator::FrameAvailable()		{ return false; }
// ROM 0x00148c54 ProduceFrame__17PNullInTranslatorFi
Ref					PNullInTranslator::ProduceFrame(int)	{ return NILREF; }


/* -------------------------------------------------------------------------------
	PNullOutTranslator
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PNullOutTranslator)
PROTOCOL_CLASSINFO(PNullOutTranslator, "POutTranslator", "", 0, 0, nil)

Boolean* gBreakLoopDone = nil;		// 0x0c10226c  the break loop's "done" flag, when one is running

// ROM 0x00148c64 New__18PNullOutTranslatorFv
PNullOutTranslator*	PNullOutTranslator::New()				{ return this; }
// ROM 0x00148c88 Delete__18PNullOutTranslatorFv
void				PNullOutTranslator::Delete()			{ }
// ROM 0x00148c8c Init__18PNullOutTranslatorFPv
long				PNullOutTranslator::Init(void*)			{ fInBreakLoop = false; return noErr; }

// ROM 0x00148c9c Idle__18PNullOutTranslatorFv
// A break loop with nowhere to print ends at once.
long
PNullOutTranslator::Idle()
{
	if (fInBreakLoop && gBreakLoopDone != nil)
		*gBreakLoopDone = true;
	return 0;
}

// ROM 0x00148cc4 ConsumeFrame__18PNullOutTranslatorFRC6RefVariT2
void				PNullOutTranslator::ConsumeFrame(RefArg, int, long)	{ }
// ROM 0x00148ccc Flush__18PNullOutTranslatorFv
void				PNullOutTranslator::Flush()				{ }
// ROM 0x00148cc8 Prompt__18PNullOutTranslatorFi
void				PNullOutTranslator::Prompt(int)			{ }
// ROM 0x00148cd0 Print__18PNullOutTranslatorFPCce
long				PNullOutTranslator::Print(const char*, ...)	{ return 0; }
// ROM 0x00148cd8 Putc__18PNullOutTranslatorFi
int					PNullOutTranslator::Putc(int)			{ return 0; }
// ROM 0x00148c68 EnterBreakLoop__18PNullOutTranslatorFi
void				PNullOutTranslator::EnterBreakLoop(int)	{ fInBreakLoop = true; }
// ROM 0x00148c74 ExitBreakLoop__18PNullOutTranslatorFv
void				PNullOutTranslator::ExitBreakLoop()		{ fInBreakLoop = false; }
// ROM 0x00148c80 StackTrace__18PNullOutTranslatorFPv
void				PNullOutTranslator::StackTrace(void*)	{ }
// ROM 0x00148c84 ExceptionNotify__18PNullOutTranslatorFP9Exception
void				PNullOutTranslator::ExceptionNotify(Exception*)	{ }


/* -------------------------------------------------------------------------------
	PStdioOutTranslator
------------------------------------------------------------------------------- */

PROTOCOL_IMPL_SOURCE_MACRO(PStdioOutTranslator)
PROTOCOL_CLASSINFO(PStdioOutTranslator, "POutTranslator", "", 0, 0, nil)

// ROM 0x001f70c0 New__19PStdioOutTranslatorFv
PStdioOutTranslator*	PStdioOutTranslator::New()			{ fFile = nil; return this; }
// ROM 0x001f7144 Delete__19PStdioOutTranslatorFv
void				PStdioOutTranslator::Delete()			{ }

// ROM 0x001f7148 Init__19PStdioOutTranslatorFPv
// The context is a FILE** (nil: print nothing).
long
PStdioOutTranslator::Init(void* context)
{
	fFile = context != nil ? *(FILE**) context : nil;
	return noErr;
}

// ROM 0x001f7158 Idle__19PStdioOutTranslatorFv
long				PStdioOutTranslator::Idle()				{ return 0; }

// ROM 0x001f7160 ConsumeFrame__19PStdioOutTranslatorFRC6RefVariT2
// (the ROM inlines PrintObjectAux here)
void
PStdioOutTranslator::ConsumeFrame(RefArg obj, int depth, long indent)
{
	PrintObjectAux(obj, indent, depth);
}

// ROM 0x001f717c Flush__19PStdioOutTranslatorFv
void
PStdioOutTranslator::Flush()
{
	if (fFile != nil && fflush(fFile) != 0)
		Throw(exTranslatorException, (void*) -1, nil);
}

// ROM 0x001f7178 Prompt__19PStdioOutTranslatorFi
void				PStdioOutTranslator::Prompt(int)		{ }

// text to the stream, the Newton's carriage-return line ends as the
// host's newlines
static void
PutText(FILE* file, const char* text)
{
	for (; *text != 0; text++)
		fputc(*text == '\r' ? '\n' : *text, file);
}

// ROM 0x001f71bc Print__19PStdioOutTranslatorFPCce
long
PStdioOutTranslator::Print(const char* format, ...)
{
	if (fFile == nil)
		return 0;
	va_list args;
	va_start(args, format);
	char buffer[1024];
	long length = REPFormat(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (length >= (long) sizeof(buffer))
	{
		// a long one: format again into a buffer of its size
		char* big = new char[length + 1];
		va_start(args, format);
		REPFormat(big, length + 1, format, args);
		va_end(args);
		PutText(fFile, big);
		delete[] big;
	}
	else
		PutText(fFile, buffer);
	return length;
}

// ROM 0x001f71fc Putc__19PStdioOutTranslatorFi
int
PStdioOutTranslator::Putc(int c)
{
	return fFile != nil ? fputc(c == '\r' ? '\n' : c, fFile) : 0;
}

// ROM 0x001f70cc EnterBreakLoop__19PStdioOutTranslatorFi
void
PStdioOutTranslator::EnterBreakLoop(int level)
{
	Print("Entering break loop\r");
	Prompt(level);
	Flush();
}

// ROM 0x001f7118 ExitBreakLoop__19PStdioOutTranslatorFv
void
PStdioOutTranslator::ExitBreakLoop()
{
	Print("Exiting break loop\r");
}

// ROM 0x001f7134 StackTrace__19PStdioOutTranslatorFPv
void
PStdioOutTranslator::StackTrace(void* interpreter)
{
	REPStackTrace(interpreter);
}

// ROM 0x001f713c ExceptionNotify__19PStdioOutTranslatorFP9Exception
void
PStdioOutTranslator::ExceptionNotify(Exception* exception)
{
	REPExceptionNotify(exception);
}


/* -------------------------------------------------------------------------------
	Making translators
------------------------------------------------------------------------------- */

// Host: the implementations this file has are registered when there is a
// registry (the ROM's are in its protocol registry from InitTranslators).
void
RegisterREPTranslators(void)
{
	if (gProtocolRegistry == nil)
		return;
	PNullInTranslator::ClassInfo()->Register();
	PNullOutTranslator::ClassInfo()->Register();
	PStdioOutTranslator::ClassInfo()->Register();
}


// an instance by name through the registry when there is one, else
// (DEVIATION: a standalone run of the object system) from the class info
static TProtocol*
NewTranslator(const char* interface, const char* implementation, const TClassInfo* info)
{
	if (gProtocolRegistry != nil)
		return NewByName(interface, implementation);
	return info->New();
}


// ROM 0x0012dce8 CreateNullInTranslator__FPP13PInTranslator
NewtonErr
CreateNullInTranslator(PInTranslator** translator)
{
	if (translator == nil)
		return -1;
	*translator = (PInTranslator*) NewTranslator("PInTranslator", "PNullInTranslator", PNullInTranslator::ClassInfo());
	if (*translator == nil)
		return MemError();
	NewtonErr err = (*translator)->Init(nil);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}


// ROM 0x0012e0ac CreateNullOutTranslator__FPP14POutTranslator
NewtonErr
CreateNullOutTranslator(POutTranslator** translator)
{
	if (translator == nil)
		return -1;
	*translator = (POutTranslator*) NewTranslator("POutTranslator", "PNullOutTranslator", PNullOutTranslator::ClassInfo());
	if (*translator == nil)
		return MemError();
	NewtonErr err = (*translator)->Init(nil);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}


// ROM 0x0012c460 InitREPIn__Fv
PInTranslator*
InitREPIn(void)
{
	PInTranslator* translator = nil;
	CreateNullInTranslator(&translator);
	return translator;
}


// ROM 0x0012c4a0 InitREPOut__Fv
POutTranslator*
InitREPOut(void)
{
	POutTranslator* translator = nil;
	CreateNullOutTranslator(&translator);
	return translator;
}


/* -------------------------------------------------------------------------------
	The REP
------------------------------------------------------------------------------- */

// ROM 0x0019d268 REPInit__Fv
// The globals the REP defines (trace, vars, functions, printDepth,
// prettyPrint), its context, and the greeting.
void
REPInit(void)
{
	if (gREPin == nil || gREPout == nil)
		Throw(exCompiler, (void*) kNSErrNoREPTranslators, nil);
	SetFrameSlot(RefVar(gVarFrame), RSSYMtrace, RefVar(NILREF));
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMprintdepth, RefVar(MAKEINT(3)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMprettyprint, RefVar(TRUEREF));
	gREPContext = gVarFrame;
	AddGCRoot(gREPContext);
	gREPLevel = 0;
	gREPout->Print("\rWelcome to NewtonScript!\r\r");
	gREPout->Prompt(gREPLevel);
	gREPout->Flush();
}


// Host: the REP started on a stdio stream, as TNewtWorld::MainConstructor
// (0x002e7ef8) starts it after InitObjects: InitTranslators, InitREPIn,
// InitREPOut, REPInit - the out translator being the stdio one here
// (the ROM's InitREPOut makes a null one and the debugger nub or the
// serial debugger replaces it).  A translator InitPrinter installed is
// replaced.
void
HostInitREP(FILE* out)
{
	RegisterREPTranslators();
	if (gREPin == nil)
		gREPin = InitREPIn();
	if (gREPout != nil)
	{
		gREPout->Delete();
		gREPout = nil;
	}
	if (out == nil)
		gREPout = InitREPOut();
	else
	{
		gREPout = (POutTranslator*) NewTranslator("POutTranslator", "PStdioOutTranslator", PStdioOutTranslator::ClassInfo());
		gREPout->Init(&out);
	}
	REPInit();
}


// ROM 0x0019d694 REPprintf__FPCce
void
REPprintf(const char* format, ...)
{
	if (gREPout == nil)
		return;
	va_list args;
	va_start(args, format);
	char buffer[1024];
	long length = REPFormat(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (length >= (long) sizeof(buffer))
	{
		char* big = new char[length + 1];
		va_start(args, format);
		REPFormat(big, length + 1, format, args);
		va_end(args);
		gREPout->Print("%s", big);
		delete[] big;
	}
	else
		gREPout->Print("%s", buffer);
}


// ROM 0x0019d6c8 REPflush__Fv
void
REPflush(void)
{
	gREPout->Flush();
}


// ROM 0x002d01e4 REPExceptionNotify__FP9Exception
// The REP's report of an exception: a message exception's text, a frames
// exception's frame (with the file and line of a compiled form when it
// has them) or an error code.
void
REPExceptionNotify(Exception* exception)
{
	ExceptionName name = (ExceptionName) exception->name;
	if (Subexception(name, (ExceptionName) "evt.ex.msg"))
	{
		gREPout->Print("    !!! Exception: %s\r", (const char*) exception->data);
		return;
	}
	if (!Subexception(name, (ExceptionName) "type.ref"))
	{
		const char* message = GetFramesErrorString((long) (Long) exception->data);
		if (message != nil)
			gREPout->Print("    !!! Exception: %s\r", message);
		else
			gREPout->Print("    !!! Exception: %s (%ld)\r", name, (long) (Long) exception->data);
		return;
	}
	RefVar data(**(RefStruct**) &exception->data);
	if (!IsFrame(data))
	{
		long indent = gREPout->Print("    !!! Exception: %s ", name);
		PrintObject(data, indent);
	}
	else
	{
		RefVar filename(GetFrameSlotRef(data, RSSYMfilename));
		RefVar linenumber(GetFrameSlotRef(data, RSSYMlinenumber));
		RefVar errorCode(GetFrameSlotRef(data, RSSYMerrorcode));
		if ((Ref) filename == NILREF || (Ref) linenumber == NILREF)
		{
			const char* message = ISINT(errorCode) ? GetFramesErrorString(RVALUE(errorCode)) : nil;
			if (message == nil)
			{
				long indent = gREPout->Print("    !!! Exception: %s ", name);
				PrintObject(data, indent);
			}
			else
			{
				gREPout->Print("    !!! Exception: ");
				PrintFramesErrorMsg(message, data);
			}
		}
		else
		{
			RemoveSlot(data, RSSYMfilename);
			RemoveSlot(data, RSSYMlinenumber);
			const char* message = ISINT(errorCode) ? GetFramesErrorString(RVALUE(errorCode)) : nil;
			RefVar ascii(ASCIIString(filename));
			if (message != nil)
			{
				gREPout->Print("    File \"%s\"; Line %ld !!! Exception: ", BinaryData(ascii), RINT(linenumber));
				PrintFramesErrorMsg(message, data);
			}
			else
			{
				long indent = gREPout->Print("    File \"%s\"; Line %ld !!! Exception: %s ", BinaryData(ascii), RINT(linenumber), name);
				PrintObject(data, indent);
			}
		}
	}
	gREPout->Print("\r");
}


// ROM 0x002ae830 REPStackTrace__FPv
// NOT YET RECONSTRUCTED: the debugger's view of the interpreter's stacks
// (TNSDebugAPI: NumStackFrames, Function, Receiver, Locals, FindVar) and
// SearchForObjectName; the trace prints its heading only.
void
REPStackTrace(void* /*interpreter*/)
{
	gREPout->Print("\rStack trace:\r");
}
