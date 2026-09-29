/*
	File:		comms/NTKTranslators.cpp

	Contains:	The NTK's REP translators (PNTKInTranslator,
				PNTKOutTranslator) and the tethered listener's
				(PSerialInTranslator, PSerialOutTranslator) - NTK.h.

	Reconstructed from the MP2x00 US ROM (0x00129ef4-0x0012a9fc,
	0x0012d02c-0x0012d174, 0x001dd574-0x001dd720, 0x001de770-0x001de9f4);
	each function cites its origin.
*/

#include "NTK.h"
#include "ObjectStreamer.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "StorePackages.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "NewtonTime.h"
#include "NewtErrors.h"

#include <stdlib.h>
#include <string.h>

extern const ExceptionName exTranslatorException;

#define kNTKPause			(50 * kMilliseconds)
#define kNTKTimeout			(30 * kSeconds)
#define kErrTranslatorText	(-48211)			// Print's text too long


/*------------------------------------------------------------------------------
	PNTKInTranslator
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(PNTKInTranslator)
PROTOCOL_CLASSINFO(PNTKInTranslator, "PInTranslator", "", 0, 0, nil)

// ROM 0x00129efc New__16PNTKInTranslatorFv
PNTKInTranslator*
PNTKInTranslator::New()
{
	fBuffer = nil;
	fPipe = nil;
	fPause = 0;
	fTimeout = 0;
	fFrameAvailable = false;
	return this;
}


// ROM 0x00129f20 Delete__16PNTKInTranslatorFv
void
PNTKInTranslator::Delete()
{
	if (fPipe != nil)
		delete fPipe;
}


// ROM 0x00129f34 Init__16PNTKInTranslatorFPv
// A pipe over the buffer the connection's bytes come into.
long
PNTKInTranslator::Init(void* context)
{
	if (context == nil)
		return -1;
	NTKTranslatorContext* c = (NTKTranslatorContext*) context;
	fPause = c->fPause;
	fTimeout = c->fTimeout;
	fBuffer = c->fBuffer;
	fPipe = new TTaskSafeRingPipe;
	if (fPipe == nil)
		return MemError();
	// (the ROM names it 'ntkP': DEVIATION, host blocks are not the pointer heap's)
	fPipe->Init(fBuffer, false, fPause, fTimeout);
	return noErr;
}


// ROM 0x00129f18 SetTimeout__16PNTKInTranslatorFUl
void
PNTKInTranslator::SetTimeout(ULong timeout)
{
	fTimeout = timeout;
}


// ROM 0x00129fc8 Idle__16PNTKInTranslatorFv
// A command waiting: the nub does it (a code block for the REP makes a
// frame available); a failure ends the connection.  Every second.
long
PNTKInTranslator::Idle()
{
	newton_try
	{
		if (!fFrameAvailable && fBuffer->DataCount() > 0)
		{
			NewtonErr result = gNTKNub->DoCommand();
			if (result < 0)
				NTKShutdown(result);
			else if (result > 0)
				fFrameAvailable = true;
		}
	}
	newton_catch_all
	{
		NTKShutdown((NewtonErr) (Long) CurrentException()->data);
	}
	end_try;
	return kSeconds;
}


// ROM 0x0012a058 FrameAvailable__16PNTKInTranslatorFv
Boolean
PNTKInTranslator::FrameAvailable()
{
	return fFrameAvailable;
}


// ROM 0x0012a060 ProduceFrame__16PNTKInTranslatorFi
// The code block, as NSOF; its receipt acknowledged.
Ref
PNTKInTranslator::ProduceFrame(int level)
{
	fFrameAvailable = false;
	TObjectReader reader(*fPipe);
	RefVar frame(reader.Read());
	NewtonErr err = gNTKNub->SendResult(noErr);
	if (err != noErr)
		NTKShutdown(err);
	return frame;
}


// ROM 0x0012a118 ReadHeader__16PNTKInTranslatorFPUlT1
// Two words (big-endian on the connection).
void
PNTKInTranslator::ReadHeader(ULong* word1, ULong* word2)
{
	UByte bytes[4];
	fBuffer->GetnCompletely(bytes, 4, fPause, fTimeout);
	*word1 = ((ULong) bytes[0] << 24) | ((ULong) bytes[1] << 16) | ((ULong) bytes[2] << 8) | bytes[3];
	fBuffer->GetnCompletely(bytes, 4, fPause, fTimeout);
	*word2 = ((ULong) bytes[0] << 24) | ((ULong) bytes[1] << 16) | ((ULong) bytes[2] << 8) | bytes[3];
}


// ROM 0x0012a180 ReadData__16PNTKInTranslatorFPvl
void
PNTKInTranslator::ReadData(void* data, long size)
{
	fBuffer->GetnCompletely((UByte*) data, size, fPause, fTimeout);
}


// ROM 0x0012a1b0 LoadPackage__16PNTKInTranslatorFv
// The package that follows, onto the default store.
NewtonErr
PNTKInTranslator::LoadPackage(void)
{
	RefVar callback(NILREF);
	RefVar store(NSCallGlobalFn(RefVar(RSSYMgetdefaultstore)));
	return NewPackage(fPipe, store, callback, 0);
}


// ROM 0x0012a8c4 CreateNTKInTranslator__FPP13PInTranslatorPcP19TTaskSafeRingBuffer
NewtonErr
CreateNTKInTranslator(PInTranslator** translator, char* name, TTaskSafeRingBuffer* buffer)
{
	if (translator == nil)
		return -1;
	*translator = (PInTranslator*) NewByName("PInTranslator", name);
	if (*translator == nil)
		return MemError();
	NTKTranslatorContext context = { buffer, kNTKPause, kNTKTimeout, 0 };
	NewtonErr err = (*translator)->Init(&context);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}


/*------------------------------------------------------------------------------
	PNTKOutTranslator
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(PNTKOutTranslator)
PROTOCOL_CLASSINFO(PNTKOutTranslator, "POutTranslator", "", 0, 0, nil)

// ROM 0x0012a220 New__17PNTKOutTranslatorFv
PNTKOutTranslator*
PNTKOutTranslator::New()
{
	fBuffer = nil;
	fPause = 0;
	fTimeout = 0;
	fText = nil;
	fTextSize = 0;
	fPipe = nil;
	fTextPtr = nil;
	fTextLeft = 0;
	return this;
}


// ROM 0x0012a624 Delete__17PNTKOutTranslatorFv
void
PNTKOutTranslator::Delete()
{
	if (fPipe != nil)
		delete fPipe;
	if (fText != nil)
		free(fText);
}


// ROM 0x0012a668 Init__17PNTKOutTranslatorFPv
// The text buffer, and a pipe over the buffer the connection's bytes go
// out of.
long
PNTKOutTranslator::Init(void* context)
{
	if (context == nil)
		return -1;
	NTKTranslatorContext* c = (NTKTranslatorContext*) context;
	fPause = c->fPause;
	fTimeout = c->fTimeout;
	fBuffer = c->fBuffer;
	fTextSize = c->fTextSize;
	fText = (char*) malloc(fTextSize);
	if (fText != nil)
	{
		// (the ROM names it 'repb': DEVIATION, host blocks are not the pointer heap's)
		fTextPtr = fText;
		fTextLeft = fTextSize;
	}
	else
	{
		NewtonErr err = MemError();
		if (err != noErr)
			return err;
	}
	fPipe = new TTaskSafeRingPipe;
	if (fPipe == nil)
		return MemError();
	// (the ROM names it 'ntkP': DEVIATION, host blocks are not the pointer heap's)
	fPipe->Init(fBuffer, false, fPause, fTimeout);
	return noErr;
}


// ROM 0x0012a660 SetTimeout__17PNTKOutTranslatorFUl
void
PNTKOutTranslator::SetTimeout(ULong timeout)
{
	fTimeout = timeout;
}


// ROM 0x0012a740 Idle__17PNTKOutTranslatorFv
long
PNTKOutTranslator::Idle()
{
	return kSeconds;
}


// ROM 0x0012a2f0 SendHeader__17PNTKOutTranslatorFUlT1
// Two words, big-endian.
void
PNTKOutTranslator::SendHeader(ULong word1, ULong word2)
{
	UByte bytes[4];
	bytes[0] = word1 >> 24; bytes[1] = word1 >> 16; bytes[2] = word1 >> 8; bytes[3] = word1;
	fBuffer->PutnCompletely(bytes, 4, fPause, fTimeout);
	bytes[0] = word2 >> 24; bytes[1] = word2 >> 16; bytes[2] = word2 >> 8; bytes[3] = word2;
	fBuffer->PutnCompletely(bytes, 4, fPause, fTimeout);
}


// ROM 0x0012a35c SendCommand__17PNTKOutTranslatorFUlT1
void
PNTKOutTranslator::SendCommand(ULong command, ULong length)
{
	SendHeader(command, length);
}


// ROM 0x0012a3c8 SendData__17PNTKOutTranslatorFPvl
void
PNTKOutTranslator::SendData(const void* data, long size)
{
	fBuffer->PutnCompletely((const UByte*) data, size, fPause, fTimeout);
}


// the words the protocol's data carries are big-endian too
static void
SendWord(PNTKOutTranslator* out, ULong word)
{
	UByte bytes[4];
	bytes[0] = word >> 24; bytes[1] = word >> 16; bytes[2] = word >> 8; bytes[3] = word;
	out->SendData(bytes, 4);
}


// ROM 0x0012a3f8 ConsumeFrameReally__17PNTKOutTranslatorFRC6RefVar
// An object: its size, then its NSOF.
void
PNTKOutTranslator::ConsumeFrameReally(RefArg obj)
{
	TObjectWriter writer(obj, *fPipe, false);
	SendWord(this, writer.Size());
	writer.Write();
	Flush();
}


// ROM 0x0012a4a4 ConsumeExceptionFrame__17PNTKOutTranslatorFRC6RefVarPc
// An exception's name and object: the whole length, the name's (with its
// terminator) and the name, the object's size and its NSOF.
void
PNTKOutTranslator::ConsumeExceptionFrame(RefArg obj, char* name)
{
	long nameLength = strlen(name) + 1;
	TObjectWriter writer(obj, *fPipe, false);
	long size = writer.Size();
	SendWord(this, nameLength + size);
	SendWord(this, nameLength);
	SendData(name, nameLength);
	SendWord(this, size);
	writer.Write();
	Flush();
}


// ROM 0x0012a5a8 FlushText__17PNTKOutTranslatorFv
// What has been printed, as a 'text' message.
void
PNTKOutTranslator::FlushText(void)
{
	long length = fTextSize - fTextLeft;
	if (length <= 0)
		return;
	NewtonErr err = gNTKNub->SendTextHeader(length);
	if (err != noErr)
		// ROM BUG: NTKShutdown deletes the nub and with it this translator
		// and its buffer, which are then written to all the same
		NTKShutdown(err);
	fBuffer->PutnCompletely((const UByte*) fText, length, fPause, fTimeout);
	fTextPtr = fText;
	fTextLeft = fTextSize;
}


// ROM 0x0012a748 ConsumeFrame__17PNTKOutTranslatorFRC6RefVariT2
// An object printed as source.
void
PNTKOutTranslator::ConsumeFrame(RefArg obj, int depth, long indent)
{
	PrintObjectAux(obj, indent, depth);
	FlushText();
}


// ROM 0x0012a88c Flush__17PNTKOutTranslatorFv
void
PNTKOutTranslator::Flush()
{
	FlushText();
}


// ROM 0x0012a888 Prompt__17PNTKOutTranslatorFi
void
PNTKOutTranslator::Prompt(int level)
{ }


// ROM 0x0012a770 Print__17PNTKOutTranslatorFPCce
// Into the text buffer (flushed first if it has not the room), sent at the
// first carriage return.  More than 256 characters, or more than the
// buffer holds, is an exception.
long
PNTKOutTranslator::Print(const char* format, ...)
{
	// DEVIATION: the host formats into a buffer of its own size first (the
	// ROM's vsprintf into 256 bytes on the stack overruns it on a longer
	// text before the length is checked)
	char buffer[1024];
	va_list args;
	va_start(args, format);
	long length = REPFormat(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (length > 0x100 || length > fTextSize)
		Throw(exTranslatorException, (void*) kErrTranslatorText, nil);
	if (length > 0)
	{
		if (fTextLeft < length)
			FlushText();
		BlockMove(buffer, fTextPtr, length);
		fTextPtr += length;
		fTextLeft -= length;
		for (long i = 0; i < length; i++)
		{
			if (buffer[i] == '\r')
			{
				FlushText();
				break;
			}
		}
	}
	return length;
}


// ROM 0x0012a860 Putc__17PNTKOutTranslatorFi
int
PNTKOutTranslator::Putc(int c)
{
	Print("%c", c);
	return c;
}


// ROM 0x0012a248 EnterBreakLoop__17PNTKOutTranslatorFi
void
PNTKOutTranslator::EnterBreakLoop(int level)
{
	NewtonErr err = gNTKNub->EnterBreakLoop(level);
	if (err != noErr)
		NTKShutdown(err);
}


// ROM 0x0012a274 ExitBreakLoop__17PNTKOutTranslatorFv
void
PNTKOutTranslator::ExitBreakLoop()
{
	NewtonErr err = gNTKNub->ExitBreakLoop();
	if (err != noErr)
		NTKShutdown(err);
}


// ROM 0x0012a2a0 StackTrace__17PNTKOutTranslatorFPv
void
PNTKOutTranslator::StackTrace(void* interpreter)
{
	NewtonErr err = NTKStackTrace(interpreter);
	if (err != noErr)
		NTKShutdown(err);
}


// ROM 0x0012a2c4 ExceptionNotify__17PNTKOutTranslatorFP9Exception
void
PNTKOutTranslator::ExceptionNotify(Exception* exception)
{
	NewtonErr err = gNTKNub->ExceptionNotify(exception);
	if (err != noErr)
		NTKShutdown(err);
}


// ROM 0x0012a960 CreateNTKOutTranslator__FPP14POutTranslatorPcP19TTaskSafeRingBuffer
NewtonErr
CreateNTKOutTranslator(POutTranslator** translator, char* name, TTaskSafeRingBuffer* buffer)
{
	if (translator == nil)
		return -1;
	*translator = (POutTranslator*) NewByName("POutTranslator", name);
	if (*translator == nil)
		return MemError();
	NTKTranslatorContext context = { buffer, kNTKPause, kNTKTimeout, 0xff };
	NewtonErr err = (*translator)->Init(&context);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}


/*------------------------------------------------------------------------------
	PSerialInTranslator
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(PSerialInTranslator)
PROTOCOL_CLASSINFO(PSerialInTranslator, "PInTranslator", "", 0, 0, nil)

// ROM 0x001dd57c New__19PSerialInTranslatorFv
PSerialInTranslator*
PSerialInTranslator::New()
{
	fBuffer = nil;
	fLine = nil;
	fSize = 0;
	return this;
}


// ROM 0x001dd590 Delete__19PSerialInTranslatorFv
void
PSerialInTranslator::Delete()
{
	if (fLine != nil)
		free(fLine);
}


// ROM 0x001dd5a0 Init__19PSerialInTranslatorFPv
long
PSerialInTranslator::Init(void* context)
{
	if (context == nil)
		return -1;
	SerialTranslatorContext* c = (SerialTranslatorContext*) context;
	fBuffer = c->fBuffer;
	fSize = c->fSize;
	fLine = (char*) malloc(fSize);
	if (fLine == nil)
		return MemError();
	// (the ROM names it 'repb': DEVIATION, host blocks are not the pointer heap's)
	return noErr;
}


// ROM 0x001dd604 Idle__19PSerialInTranslatorFv
long
PSerialInTranslator::Idle()
{
	return kSeconds;
}


// ROM 0x001dd60c FrameAvailable__19PSerialInTranslatorFv
// Something typed.
Boolean
PSerialInTranslator::FrameAvailable()
{
	return fBuffer->DataCount() > 0;
}


// ROM 0x001dd63c ProduceFrame__19PSerialInTranslatorFi
// A line (backspace and delete take a character back), compiled.
Ref
PSerialInTranslator::ProduceFrame(int level)
{
	RefVar frame(NILREF);
	char* p = fLine;
	char* end = fLine + fSize;
	while (p < end)
	{
		int c = fBuffer->GetCompletely(250 * kMilliseconds, 0);
		if (c == 8 || c == 0x7f)
		{
			if (fLine < p)
				p--;
		}
		else if (c == '\r' || c == '\n')
		{
			*p = 0;
			break;
		}
		else
			*p++ = c;
	}
	gREPout->Putc('\r');
	RefVar source(MakeString(fLine));
	frame = ParseString(source);
	return frame;
}


// ROM 0x0012d02c CreateSerialInTranslator__FPP13PInTranslatorP19TTaskSafeRingBuffer
NewtonErr
CreateSerialInTranslator(PInTranslator** translator, TTaskSafeRingBuffer* buffer)
{
	if (translator == nil)
		return -1;
	*translator = (PInTranslator*) NewByName("PInTranslator", "PSerialInTranslator");
	if (*translator == nil)
		return MemError();
	SerialTranslatorContext context = { buffer, 0x100 };
	NewtonErr err = (*translator)->Init(&context);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}


/*------------------------------------------------------------------------------
	PSerialOutTranslator
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(PSerialOutTranslator)
PROTOCOL_CLASSINFO(PSerialOutTranslator, "POutTranslator", "", 0, 0, nil)

// ROM 0x001de778 New__20PSerialOutTranslatorFv
PSerialOutTranslator*
PSerialOutTranslator::New()
{
	fBuffer = nil;
	fText = nil;
	fSize = 0;
	return this;
}


// ROM 0x001de804 Delete__20PSerialOutTranslatorFv
void
PSerialOutTranslator::Delete()
{
	if (fText != nil)
		free(fText);
}


// ROM 0x001de814 Init__20PSerialOutTranslatorFPv
long
PSerialOutTranslator::Init(void* context)
{
	if (context == nil)
		return -1;
	SerialTranslatorContext* c = (SerialTranslatorContext*) context;
	fBuffer = c->fBuffer;
	fSize = c->fSize;
	fText = (char*) malloc(fSize);
	if (fText == nil)
		return MemError();
	// (the ROM names it 'repb': DEVIATION, host blocks are not the pointer heap's)
	return noErr;
}


// ROM 0x001de878 Idle__20PSerialOutTranslatorFv
long
PSerialOutTranslator::Idle()
{
	return kSeconds;
}


// ROM 0x001de880 ConsumeFrame__20PSerialOutTranslatorFRC6RefVariT2
void
PSerialOutTranslator::ConsumeFrame(RefArg obj, int depth, long indent)
{
	PrintObjectAux(obj, indent, depth);
}


// ROM 0x001de88c Prompt__20PSerialOutTranslatorFi
void
PSerialOutTranslator::Prompt(int level)
{
	Print("%7d > ", level);
}


// ROM 0x001de8a0 Flush__20PSerialOutTranslatorFv
// Until the connection has sent it all.
void
PSerialOutTranslator::Flush()
{
	while (fBuffer->DataCount() > 0)
		Sleep(250 * kMilliseconds);
}


// ROM 0x001de8f4 Print__20PSerialOutTranslatorFPCce
long
PSerialOutTranslator::Print(const char* format, ...)
{
	// DEVIATION: formatted into a buffer of the host's size first (the
	// ROM's vsprintf into the text buffer overruns it before the length
	// is checked)
	char buffer[1024];
	va_list args;
	va_start(args, format);
	long length = REPFormat(buffer, sizeof(buffer), format, args);
	va_end(args);
	if (fSize < length)
		Throw(exTranslatorException, (void*) kErrTranslatorText, nil);
	else if (length > 0)
	{
		memcpy(fText, buffer, length + 1);
		for (char* p = fText; *p != 0; p++)
			Putc(*p);
	}
	return length;
}


// ROM 0x001de990 Putc__20PSerialOutTranslatorFi
// A carriage return followed by a line feed.
int
PSerialOutTranslator::Putc(int c)
{
	fBuffer->PutCompletely(c, 250 * kMilliseconds, 0);
	if (c == '\r')
		fBuffer->PutCompletely('\n', 250 * kMilliseconds, 0);
	return c;
}


// ROM 0x001de78c EnterBreakLoop__20PSerialOutTranslatorFi
void
PSerialOutTranslator::EnterBreakLoop(int level)
{
	Print("Entering break loop\r");
	Prompt(level);
	Flush();
}


// ROM 0x001de7d8 ExitBreakLoop__20PSerialOutTranslatorFv
void
PSerialOutTranslator::ExitBreakLoop()
{
	Print("Exiting break loop\r");
}


// ROM 0x001de7f4 StackTrace__20PSerialOutTranslatorFPv
void
PSerialOutTranslator::StackTrace(void* interpreter)
{
	REPStackTrace(interpreter);
}


// ROM 0x001de7fc ExceptionNotify__20PSerialOutTranslatorFP9Exception
void
PSerialOutTranslator::ExceptionNotify(Exception* exception)
{
	REPExceptionNotify(exception);
}


// ROM 0x0012d0d0 CreateSerialOutTranslator__FPP14POutTranslatorP19TTaskSafeRingBuffer
NewtonErr
CreateSerialOutTranslator(POutTranslator** translator, TTaskSafeRingBuffer* buffer)
{
	if (translator == nil)
		return -1;
	*translator = (POutTranslator*) NewByName("POutTranslator", "PSerialOutTranslator");
	if (*translator == nil)
		return MemError();
	SerialTranslatorContext context = { buffer, 0x100 };
	NewtonErr err = (*translator)->Init(&context);
	if (err != noErr)
	{
		(*translator)->Delete();
		*translator = nil;
	}
	return err;
}
