// NSOF test (src/stores/ObjectStreamer.h): TObjectWriter streams an
// object graph into a memory pipe (the utility tests' CMemoryPipe) and
// TObjectReader reads it back - every tag (immediates, characters,
// strings, symbols, binaries with a class, arrays plain and classed,
// frames, nil, small rects, precedents for shared and cyclic references),
// the exact bytes of a small stream against the format, Size() against
// what was written, the _proto slot left out unless wanted, functions
// refused when not allowed, a bad version and a bad tag.

#include "ObjectStreamer.h"
#include "../../utility/tests/MemoryPipe.h"
#include "Compiler.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "memory/host/KernelHeap.h"
#include "NewtonExceptions.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

extern const ExceptionName exStoreError;
extern const ExceptionName exFrames;


static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	return InterpretBlock(fn, RefVar(gVarFrame));
}


static Boolean
StringIs(RefArg str, const char* text)
{
	if (!IsString(str))
		return false;
	UniChar* chars = GetCString(str);
	for (; *text != 0; text++, chars++)
		if (*chars != (UniChar) (unsigned char) *text)
			return false;
	return *chars == 0;
}


// obj streamed and read back; the bytes written come back in pipe
static Ref
RoundTrip(RefArg obj, CMemoryPipe& pipe, Boolean includeProto = true, long* streamSize = nil)
{
	{
		TObjectWriter writer(obj, pipe, includeProto);
		if (streamSize != nil)
			*streamSize = writer.Size();
		writer.Write();
	}
	pipe.Rewind();
	TObjectReader reader(pipe);
	return reader.Read();
}


static void
TestBytes()
{
	// {name: "x"}: version, frame of 1, symbol "name", string of 4 bytes
	CMemoryPipe pipe(16);
	RefVar frame(Eval("{name: \"x\"}"));
	long size;
	RefVar back(RoundTrip(frame, pipe, true, &size));
	static const UByte expected[] = { 0x02, 0x06, 0x01, 0x07, 0x04, 'n', 'a', 'm', 'e', 0x08, 0x04, 0x00, 'x', 0x00, 0x00 };
	EXPECT(pipe.fWriteBuffer->Position() == (long) sizeof(expected));
	EXPECT(memcmp(pipe.fWriteBuffer->fBuffer, expected, sizeof(expected)) == 0);
	EXPECT(size == (long) sizeof(expected));
	EXPECT(IsFrame(back) && StringIs(RefVar(GetFrameSlotRef(back, SYMBOL("name"))), "x"));

	// immediates and characters: an xlong of the ref, a byte, two bytes
	CMemoryPipe pipe2(16);
	back = RoundTrip(RefVar(MAKEINT(7)), pipe2, true, &size);
	static const UByte expectedInt[] = { 0x02, 0x00, 0x1c };
	EXPECT(pipe2.fWriteBuffer->Position() == 3 && memcmp(pipe2.fWriteBuffer->fBuffer, expectedInt, 3) == 0 && size == 3);
	EXPECT((Ref) back == MAKEINT(7));
	CMemoryPipe pipe3(16);
	back = RoundTrip(RefVar(MAKEINT(-1)), pipe3, true, &size);
	static const UByte expectedNeg[] = { 0x02, 0x00, 0xff, 0xff, 0xff, 0xff, 0xfc };
	EXPECT(pipe3.fWriteBuffer->Position() == 7 && memcmp(pipe3.fWriteBuffer->fBuffer, expectedNeg, 7) == 0 && size == 7);
	EXPECT((Ref) back == MAKEINT(-1));
	CMemoryPipe pipe4(16);
	back = RoundTrip(RefVar(MAKECHAR('a')), pipe4, true, &size);
	static const UByte expectedChar[] = { 0x02, 0x01, 'a' };
	EXPECT(memcmp(pipe4.fWriteBuffer->fBuffer, expectedChar, 3) == 0 && size == 3 && (Ref) back == MAKECHAR('a'));
	CMemoryPipe pipe5(16);
	back = RoundTrip(RefVar(MAKECHAR(0x263a)), pipe5, true, &size);
	static const UByte expectedUni[] = { 0x02, 0x02, 0x26, 0x3a };
	EXPECT(memcmp(pipe5.fWriteBuffer->fBuffer, expectedUni, 4) == 0 && size == 4 && (Ref) back == MAKECHAR(0x263a));
	CMemoryPipe pipe6(16);
	back = RoundTrip(RefVar(NILREF), pipe6, true, &size);
	EXPECT(pipe6.fWriteBuffer->fBuffer[1] == 0x0a && size == 2 && (Ref) back == NILREF);
	// a small rect is packed
	CMemoryPipe pipe7(16);
	back = RoundTrip(RefVar(Eval("{top: 1, left: 2, bottom: 3, right: 4}")), pipe7, true, &size);
	static const UByte expectedRect[] = { 0x02, 0x0b, 0x01, 0x02, 0x03, 0x04 };
	EXPECT(memcmp(pipe7.fWriteBuffer->fBuffer, expectedRect, 6) == 0 && size == 6);
	EXPECT(IsFrame(back) && RINT(GetFrameSlotRef(back, RSSYMbottom)) == 3 && Length(back) == 4);
	// a long array: the xlong form
	CMemoryPipe pipe8(16);
	RefVar big(AllocateArray(RSSYMarray, 300));
	back = RoundTrip(big, pipe8, true, &size);
	static const UByte expectedBig[] = { 0x02, 0x05, 0xff, 0x00, 0x00, 0x01, 0x2c, 0x0a };
	EXPECT(memcmp(pipe8.fWriteBuffer->fBuffer, expectedBig, 8) == 0 && size == 7 + 300);
	EXPECT(IsArray(back) && Length(back) == 300 && GetArraySlotRef(back, 299) == NILREF);
}


static void
TestGraph()
{
	CMemoryPipe pipe(64);
	RefVar obj(Eval("begin local shared := [1, 2, 3]; local f := {a: shared, b: shared, c: 'sym, d: $z, e: 3.5, f: \"hello\", g: nil, h: true, i: -100000, j: [array: 1, 2], k: [typed: 'x]}; f.me := f; f end"));
	long size;
	RefVar back(RoundTrip(obj, pipe, true, &size));
	EXPECT(size == pipe.fWriteBuffer->Position());
	EXPECT(IsFrame(back) && Length(back) == 12);
	RefVar a(GetFrameSlotRef(back, SYMBOL("a")));
	EXPECT(IsArray(a) && Length(a) == 3 && RINT(GetArraySlotRef(a, 2)) == 3);
	EXPECT(EQRef(GetFrameSlotRef(back, SYMBOL("b")), a));				// shared once
	EXPECT(EQRef(GetFrameSlotRef(back, SYMBOL("c")), SYMBOL("sym")));
	EXPECT(GetFrameSlotRef(back, SYMBOL("d")) == MAKECHAR('z'));
	RefVar real(GetFrameSlotRef(back, SYMBOL("e")));
	EXPECT(IsReal(real) && CDouble(real) == 3.5);
	EXPECT(StringIs(RefVar(GetFrameSlotRef(back, SYMBOL("f"))), "hello"));
	EXPECT(GetFrameSlotRef(back, SYMBOL("g")) == NILREF && GetFrameSlotRef(back, SYMBOL("h")) == TRUEREF);
	EXPECT(RINT(GetFrameSlotRef(back, SYMBOL("i"))) == -100000);
	RefVar j(GetFrameSlotRef(back, SYMBOL("j")));
	EXPECT(IsArray(j) && EQRef(ClassOf(j), RSSYMarray) && Length(j) == 2);
	RefVar k(GetFrameSlotRef(back, SYMBOL("k")));
	EXPECT(IsArray(k) && EQRef(ClassOf(k), SYMBOL("typed")) && EQRef(GetArraySlotRef(k, 0), SYMBOL("x")));
	EXPECT(EQRef(GetFrameSlotRef(back, SYMBOL("me")), back));		// the cycle
	EXPECT(!EQRef(back, obj));

	// the same symbol twice is a precedent the second time (the stream is smaller)
	CMemoryPipe pipe2(64);
	RefVar twice(Eval("['abcdefgh, 'abcdefgh]"));
	long twiceSize;
	RoundTrip(twice, pipe2, true, &twiceSize);
	CMemoryPipe pipe3(64);
	RefVar once(Eval("['abcdefgh, 'abcdefgi]"));
	long onceSize;
	RoundTrip(once, pipe3, true, &onceSize);
	EXPECT(twiceSize == onceSize - 10 + 2);

	// _proto: left out unless asked for
	RefVar protod(Eval("{_proto: {x: 1}, y: 2}"));
	CMemoryPipe pipe4(64);
	back = RoundTrip(protod, pipe4, false, &size);
	EXPECT(Length(back) == 1 && !FrameHasSlot(back, RSSYM_proto) && RINT(GetFrameSlotRef(back, SYMBOL("y"))) == 2);
	EXPECT(size == pipe4.fWriteBuffer->Position());
	CMemoryPipe pipe5(64);
	back = RoundTrip(protod, pipe5, true, &size);
	EXPECT(Length(back) == 2 && RINT(GetFrameSlotRef(RefVar(GetFrameSlotRef(back, RSSYM_proto)), SYMBOL("x"))) == 1);

	// a function: read when allowed, refused when not
	RefVar fn(Eval("func(x) x + 1"));
	CMemoryPipe pipe6(64);
	back = RoundTrip(fn, pipe6, true, &size);
	EXPECT(IsFunction(back));
	pipe6.Rewind();
	Boolean threw = false;
	newton_try
	{
		TObjectReader reader(pipe6);
		reader.SetAllowFunctions(false);
		reader.Read();
	}
	newton_catch(exFrames)
	{
		threw = (long) (Long) _info.exception.data == ERRBASE_FRAMES - 224;
	}
	end_try;
	EXPECT(threw);
	// a wrong version, a wrong tag
	CMemoryPipe bad(16);
	bad << (UByte) 1 << (UByte) 0x0a;
	bad.Rewind();
	threw = false;
	newton_try
	{
		TObjectReader reader(bad);
		reader.Read();
	}
	newton_catch(exStoreError)
	{
		threw = (long) (Long) _info.exception.data == ERRBASE_FRAMES - 6;
	}
	end_try;
	EXPECT(threw);
	CMemoryPipe bad2(16);
	bad2 << (UByte) 2 << (UByte) 0x20;
	bad2.Rewind();
	threw = false;
	newton_try
	{
		TObjectReader reader(bad2);
		reader.Read();
	}
	newton_catch(exFrames)
	{
		threw = (long) (Long) _info.exception.data == ERRBASE_FRAMES - 223;
	}
	end_try;
	EXPECT(threw);
	// two writers at once: the second gets precedents of its own
	CMemoryPipe pipe7(64);
	CMemoryPipe pipe8(64);
	{
		TObjectWriter writer1(obj, pipe7, true);
		TObjectWriter writer2(obj, pipe8, true);
		EXPECT(writer1.fPrecedents == gPrecedentsForWriting && writer2.fPrecedents != gPrecedentsForWriting);
		writer1.Write();
		writer2.Write();
	}
	EXPECT(!gPrecedentsForWritingUsed);
	EXPECT(pipe7.fWriteBuffer->Position() == pipe8.fWriteBuffer->Position()
		&& memcmp(pipe7.fWriteBuffer->fBuffer, pipe8.fWriteBuffer->fBuffer, pipe7.fWriteBuffer->Position()) == 0);
}


int
main()
{
	InitHostStandaloneHeap();
	gObjectHeapSize = 0x100000;
	InitObjects();
	newton_try
	{
		TestBytes();
		TestGraph();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	if (failures == 0)
		printf("test_ObjectStreamer: all passed\n");
	else
		printf("test_ObjectStreamer: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
