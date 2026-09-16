/*
	File:		frames/REPTranslators.h

	Contains:	The read-eval-print loop's translators - the protocols the
				REP reads NewtonScript through (PInTranslator) and prints
				through (POutTranslator) - the null and stdio
				implementations, the REP's globals and entry points, and
				the object printer (PrintObject and friends).

	The DDK has no header for these; the interfaces are the ROM's
	(tools/newton-rom/analysis/classinfo.py --name PStdioOutTranslator
	lists the dispatch slots: Init, Idle, ConsumeFrame, Flush, Prompt,
	Print, Putc, EnterBreakLoop, ExitBreakLoop, StackTrace,
	ExceptionNotify; PNullInTranslator: Init, Idle, FrameAvailable,
	ProduceFrame), expressed as protocols/Protocols.h expresses protocols.

	Print takes printf's formats plus the ROM's %U, a UniChar string
	(Printer.cpp REPFormat); PrintObject(obj, indent) is the out
	translator's ConsumeFrame(obj, 0, indent), which prints the object as
	NewtonScript source through Print, to the depth the printDepth global
	allows (Printer.cpp PrintObjectAux).
*/

#ifndef __REPTRANSLATORS_H
#define __REPTRANSLATORS_H

#ifndef __PROTOCOLS_H
#include "protocols/Protocols.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __NEWTONEXCEPTIONS_H
#include "NewtonExceptions.h"
#endif

#include <stdarg.h>
#include <stdio.h>


/* -------------------------------------------------------------------------------
	PInTranslator: where the REP reads from
------------------------------------------------------------------------------- */

PROTOCOL PInTranslator : public TProtocol
{
public:
	static PInTranslator*	New(const char* implementation);
	void			Delete();

	VIRTUAL long	Init(void* context) ENDVIRTUAL;
	VIRTUAL long	Idle() ENDVIRTUAL;					// ==> when to idle again, 0 for never
	VIRTUAL Boolean	FrameAvailable() ENDVIRTUAL;		// a form has been read
	VIRTUAL Ref		ProduceFrame(int level) ENDVIRTUAL;	// the form, compiled
};


/* -------------------------------------------------------------------------------
	POutTranslator: where the REP prints to
------------------------------------------------------------------------------- */

PROTOCOL POutTranslator : public TProtocol
{
public:
	static POutTranslator*	New(const char* implementation);
	void			Delete();

	VIRTUAL long	Init(void* context) ENDVIRTUAL;
	VIRTUAL long	Idle() ENDVIRTUAL;
	VIRTUAL void	ConsumeFrame(RefArg obj, int depth, long indent) ENDVIRTUAL;	// print an object
	VIRTUAL void	Flush() ENDVIRTUAL;
	VIRTUAL void	Prompt(int level) ENDVIRTUAL;
	VIRTUAL long	Print(const char* format, ...) ENDVIRTUAL;	// ==> characters printed
	VIRTUAL int		Putc(int c) ENDVIRTUAL;
	VIRTUAL void	EnterBreakLoop(int level) ENDVIRTUAL;
	VIRTUAL void	ExitBreakLoop() ENDVIRTUAL;
	VIRTUAL void	StackTrace(void* interpreter) ENDVIRTUAL;
	VIRTUAL void	ExceptionNotify(Exception* exception) ENDVIRTUAL;
};


/* -------------------------------------------------------------------------------
	The null translators: nothing read, nothing printed
------------------------------------------------------------------------------- */

PROTOCOL PNullInTranslator : public PInTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PNullInTranslator);

	PNullInTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	Boolean			FrameAvailable();
	Ref				ProduceFrame(int level);
};


PROTOCOL PNullOutTranslator : public POutTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PNullOutTranslator);

	PNullOutTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	void			ConsumeFrame(RefArg obj, int depth, long indent);
	void			Flush();
	void			Prompt(int level);
	long			Print(const char* format, ...);
	int				Putc(int c);
	void			EnterBreakLoop(int level);
	void			ExitBreakLoop();
	void			StackTrace(void* interpreter);
	void			ExceptionNotify(Exception* exception);

	Boolean			fInBreakLoop;		// +0x10
};


/* -------------------------------------------------------------------------------
	PStdioInTranslator: reads forms a line at a time from a C stdio stream
	and compiles them (Init's context is a StdioInTranslatorContext)
------------------------------------------------------------------------------- */

struct StdioInTranslatorContext
{
	FILE*		fInput;
	FILE*		fUnused;			// (the ROM's context has a second stream it does not use)
	size_t		fBufferSize;		// of a line
};

PROTOCOL PStdioInTranslator : public PInTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PStdioInTranslator);

	PStdioInTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	Boolean			FrameAvailable();
	Ref				ProduceFrame(int level);

	FILE*			fInput;				// +0x10
	FILE*			fUnused;			// +0x14
	char*			fBuffer;			// +0x18
	size_t			fBufferSize;		// +0x1c
};


/* -------------------------------------------------------------------------------
	PStdioOutTranslator: prints to a C stdio stream (Init's context is a
	FILE**; nil prints nothing)
------------------------------------------------------------------------------- */

PROTOCOL PStdioOutTranslator : public POutTranslator
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PStdioOutTranslator);

	PStdioOutTranslator*	New();
	void			Delete();

	long			Init(void* context);
	long			Idle();
	void			ConsumeFrame(RefArg obj, int depth, long indent);
	void			Flush();
	void			Prompt(int level);
	long			Print(const char* format, ...);
	int				Putc(int c);
	void			EnterBreakLoop(int level);
	void			ExitBreakLoop();
	void			StackTrace(void* interpreter);
	void			ExceptionNotify(Exception* exception);

	FILE*			fFile;				// +0x10
};


/* -------------------------------------------------------------------------------
	The REP
------------------------------------------------------------------------------- */

extern PInTranslator*	gREPin;			// 0x0c10190c
extern POutTranslator*	gREPout;		// 0x0c101910
extern Ref				gREPContext;	// 0x0c101900  the frame top-level forms run in (gVarFrame)
extern long				gREPLevel;		// 0x0c101904  the break loop level
extern Boolean*			gBreakLoopDone;	// 0x0c10226c  the running break loop's done flag (nil: none)

NewtonErr		CreateNullInTranslator(PInTranslator** translator);
NewtonErr		CreateNullOutTranslator(POutTranslator** translator);
PInTranslator*	InitREPIn(void);
POutTranslator*	InitREPOut(void);
void			RegisterREPTranslators(void);		// host: their class infos in the registry

void	REPInit(void);
// host: what TNewtWorld::MainConstructor does around REPInit - the
// translators registered, an in translator on the stdio stream (a null
// one for nil), an out translator on the other (a null one for nil),
// then REPInit
void	HostInitREP(FILE* out, FILE* in = nil);
void	REPAcceptLine(void);				// one form read, compiled, run and its result printed
void	REPIdle(void);						// the translators idled, then REPAcceptLine
long	REPTime(void);						// when to idle next (0: never)
void	REPprintf(const char* format, ...);
void	REPflush(void);
void	REPExceptionNotify(Exception* exception);
void	REPStackTrace(void* interpreter);

// printf with %U (a UniChar string) into a buffer, as the ROM's printf
// family has it; ==> characters the whole output would have
long	REPFormat(char* buffer, long size, const char* format, va_list args);


/* -------------------------------------------------------------------------------
	The object printer (Printer.cpp)
------------------------------------------------------------------------------- */

void	InitPrinter(void);
Boolean	IsAggregate(RefArg obj);
void	PrintObject(RefArg obj, long indent);
void	PrintObjectAux(RefArg obj, long indent, long depth);
void	SafelyPrintString(UniChar* str);
void	PrintInstructions(RefArg instructions);
void	Disassemble(RefArg fn);
void	PrintWellKnownObject(RefArg obj, long indent);
Ref		FindSlotName(RefArg context, RefArg value);
const char*	GetFramesErrorString(long error);
void	PrintFramesErrorMsg(const char* message, RefArg data);

// objects as text: the characters of a string, number, character or
// symbol into buffer (nil: only the length), at most maxLength; ==> true
// when the object has a text
Boolean	StringObject(RefArg obj, UniChar* buffer, long& length, long maxLength);
Ref		SPrintObject(RefArg obj);
Ref		Stringer(RefArg array);					// the objects of an array as one string (&)
Boolean	IsRichString(RefArg str);
long	GetStringFormat(RefArg str);
void	IntegerString(long i, UniChar* str);

// natives
Ref		FPrint(RefArg rcvr, RefArg obj);
Ref		FDisplay(RefArg rcvr, RefArg obj);
Ref		FSPrintObject(RefArg rcvr, RefArg obj);
Ref		FFramesStringer(RefArg rcvr, RefArg array);
Ref		FEvalStringer(RefArg rcvr, RefArg array);
void	RegisterPrinterNatives(void);

#endif	/* __REPTRANSLATORS_H */
