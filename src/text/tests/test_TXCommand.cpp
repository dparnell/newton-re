// TXCommand test (src/text/TXCommand.h): each kind of edit command done,
// undone and redone through Execute - a replacement, a restyle, typing
// (keys added as they come, a delete key reaching back) and a move and a
// copy - with the text, the styles and the selection checked at each
// step.  The ROM image is imported for its fonts and its U.S. locale
// bundle.
#include "Textension.h"
#include "TXContainer.h"
#include "TXCommand.h"
#include "TXNewtTextRun.h"
#include "TXGraphicsRun.h"
#include "TXRuler.h"
#include "TXRulerRange.h"
#include "TXStream.h"
#include "TXChars.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMImport.h"
#include "ROMConstants.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "Fonts.h"
#include "Ports.h"
#include "Rects.h"
#include "Regions.h"
#include "Locale.h"
#include "ByteOrder.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const ULong kUSABundle = 0x004a4d09;		// the ROM's locale bundle 'USA (as test_Dates)

const long kWidth = 160;
const long kHeight = 64;
const long kRowBytes = kWidth / 8;
static unsigned char gBits[kRowBytes * kHeight];
static PixelMap gMap;
static GrafPort gPort;


// A chunked storage over heap blocks (as test_TXDisplay's).
class TestChars : public TXChunkedChars
{
public:
			TestChars() : TXChunkedChars(8), fBlockCount(0)	{ memset(fBlocks, 0, sizeof(fBlocks)); }
	virtual	~TestChars()
			{
				for (long i = 0; i < fBlockCount; i++)
					delete[] fBlocks[i];
			}
	virtual UniChar* GetChunkPtr(long chunk, Boolean, Boolean)	{ return fBlocks[chunk]; }
	virtual NewtonErr AllocateChunks(long at, long count)
			{
				for (long i = fBlockCount - 1; i >= at; i--)
					fBlocks[i + count] = fBlocks[i];
				for (long i = 0; i < count; i++)
					fBlocks[at + i] = new UniChar[fChunkSize];
				fBlockCount += count;
				return noErr;
			}
	virtual void RemoveChunks(long at, long count)
			{
				for (long i = 0; i < count; i++)
					delete[] fBlocks[at + i];
				for (long i = at; i + count < fBlockCount; i++)
					fBlocks[i] = fBlocks[i + count];
				fBlockCount -= count;
			}
	UniChar*	fBlocks[256];
	long		fBlockCount;
};


static Ref
FontSpec(const char* family, long size, long face)
{
	RefVar spec(AllocateFrame());
	SetFrameSlot(spec, RSSYMfamily, RefVar(MakeSymbol((char*) family)));
	SetFrameSlot(spec, RSSYMsize, RefVar(MAKEINT(size)));
	SetFrameSlot(spec, RSSYMface, RefVar(MAKEINT(face)));
	return spec;
}


static Textension*
MakeDocument(const char* s)
{
	TXHandlers handlers;
	handlers.fChars = new TestChars;
	Textension* doc = new Textension;
	EXPECT(doc->ITextension(&gPort, handlers, 2) == noErr);
	TXLongPoint size = { 0, kWidth };
	doc->fDisplay->fFrames->SetTextBoundsSize(size, nil, 0);
	RgnHandle view = NewRgn();
	RectRgn(view, &gMap.bounds);
	doc->fDisplay->SetViewRgn(view);
	DisposeRgn(view);
	UniChar buffer[512];
	long n = (long) strlen(s);
	for (long i = 0; i < n; i++)
		buffer[i] = (UniChar) s[i];
	TXTextDescriptor text;
	text.Set(buffer, n);
	TXReplaceParams params(text);
	EXPECT(doc->ReplaceRange(0, 0, &params) == noErr);
	return doc;
}


// The document's text, as a C string.
static const char*
TextOf(Textension* doc)
{
	static char s[512];
	UniChar buffer[512];
	long n = doc->fChars->Count();
	TXTextDescriptor to;
	to.Set(buffer, n);
	doc->fChars->CopyTo(&to, 0, n);
	for (long i = 0; i < n; i++)
		s[i] = (char) buffer[i];
	s[n] = 0;
	return s;
}



static TXOffsetRange
Selection(Textension* doc)
{
	TXOffsetRange range;
	doc->fHilite->GetHiliteRange(&range);
	return range;
}


static void
Select(Textension* doc, TXOffset start, TXOffset end)
{
	TXOffsetRange range(start, end, false, true);
	doc->SetHiliteRange(range, true, false);
}


static void
TestReplace()
{
	Textension* doc = MakeDocument("Hello world");
	doc->Activate(true, true);
	Select(doc, 6, 11);
	UniChar there[] = { 't', 'h', 'e', 'r', 'e', '!' };
	TXTextDescriptor text;
	text.Set(there, 6);
	TXReplaceParams params(text);
	TXReplaceTextCommand* command = new TXReplaceTextCommand;
	unsigned char failed = true;
	TXOffsetRange range(6, 11, false, true);
	EXPECT(command->ITXReplaceTextCommand(doc, range, &params, &failed) == noErr);
	EXPECT(!failed && command->fState == kTXCommandToDo && command->fUndoTypes == kTXImportAll);
	EXPECT(command->fUndoStream != nil && command->fUndoRulers != nil);
	int action = -1;
	EXPECT(command->Execute(&action) == noErr);
	EXPECT(strcmp(TextOf(doc), "Hello there!") == 0);
	EXPECT(command->fState == kTXCommandDone && command->fEnd.fOffset == 12);

	EXPECT(command->Execute(&action) == noErr);				// undone
	EXPECT(strcmp(TextOf(doc), "Hello world") == 0);
	EXPECT(command->fState == kTXCommandUndone && command->fRedoStream != nil);
	TXOffsetRange selected = Selection(doc);
	EXPECT(selected.fStart.fOffset == 6 && selected.fEnd.fOffset == 11);
	EXPECT(doc->fRuns->GetLastRangeEnd() == 11 && doc->fRulers->GetLastRangeEnd() == 11);

	EXPECT(command->Execute(&action) == noErr);				// redone
	EXPECT(strcmp(TextOf(doc), "Hello there!") == 0);
	selected = Selection(doc);
	EXPECT(selected.fStart.fOffset == 6 && selected.fEnd.fOffset == 12);

	EXPECT(command->Execute(&action) == noErr);				// and undone again
	EXPECT(strcmp(TextOf(doc), "Hello world") == 0);
	delete command;
	delete doc;
}


static void
TestRestyle()
{
	Textension* doc = MakeDocument("Hello world");
	TXOffsetRange hello(0, 5, false, true);
	long length;
	TXNewtTextRun* plain = (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(0, &length);
	EXPECT(length == 11);
	TXNewtTextRun* original = (TXNewtTextRun*) plain->CreateNew();
	original->Assign(plain);
	TXAttrValues* bold = TXGetRunAttrValues(RefVar(FontSpec("geneva", 9, 1)));
	TXEditCommand* command = new TXEditCommand;
	unsigned char failed = true;
	EXPECT(command->ITXEditCommand(doc, kTXRunsCommand, bold, 0, hello, &failed) == noErr);
	EXPECT(!failed && command->fUndoTypes == kTXImportRuns && command->fCanUndo);
	int action;
	EXPECT(command->Execute(&action) == noErr);
	TXNewtTextRun* run = (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(0, &length);
	EXPECT(length == 5 && EQRef(run->fFamily, SYM(geneva)) && run->fFace == 1);

	EXPECT(command->Execute(&action) == noErr);				// undone: one plain run again
	EXPECT(strcmp(TextOf(doc), "Hello world") == 0);
	run = (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(0, &length);
	EXPECT(run->IsEqual(original));
	run = (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(5, &length);
	EXPECT(run->IsEqual(original));

	EXPECT(command->Execute(&action) == noErr);				// redone
	run = (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(0, &length);
	EXPECT(length == 5 && run->fFace == 1);
	delete command;											// deletes the values
	original->Free();
	delete doc;
}


static void
TestTyping()
{
	Textension* doc = MakeDocument("Hello world");
	doc->Activate(true, true);
	Select(doc, 5, 5);
	const UniChar typed[] = { ',', ' ', 'm', 'y' };
	TXKeyCommand* command = new TXKeyCommand;
	unsigned char failed = true;
	command->ITXKeyCommand(doc, &typed[0], 1, doc->GetKeyDownFlags(typed[0]), &failed);
	EXPECT(!failed && command->fState == kTXCommandDone && command->fNothingSaved);
	EXPECT(command->fDeleteStart.fOffset == -1);
	for (long i = 0; i < 4; i++)
		EXPECT(command->NewKey(&typed[i], 1, 0, doc->GetKeyDownFlags(typed[i]), nil) == 0);
	EXPECT(strcmp(TextOf(doc), "Hello, my world") == 0);
	EXPECT(command->fEnd.fOffset == 9);
	// a key somewhere else is not this command's
	Select(doc, 0, 0);
	EXPECT(command->NewKey(&typed[0], 1, 0, 3, nil) == 3);
	EXPECT(strcmp(TextOf(doc), "Hello, my world") == 0);

	int action;
	EXPECT(command->Execute(&action) == noErr);				// undone: the typing taken away
	EXPECT(strcmp(TextOf(doc), "Hello world") == 0);
	EXPECT(command->fUndone && command->fRedoStream != nil);
	EXPECT(!command->AcceptKey(3));
	EXPECT(command->Execute(&action) == noErr);				// redone
	EXPECT(strcmp(TextOf(doc), "Hello, my world") == 0);
	TXOffsetRange selected = Selection(doc);
	EXPECT(selected.fStart.fOffset == 5 && selected.fEnd.fOffset == 9);
	delete command;

	// backspacing: kept from the start of the line (the line before it,
	// when there is one), and the selection shown from where it reached
	Select(doc, 9, 9);
	const UniChar bs = 8;
	unsigned int flags = doc->GetKeyDownFlags(bs);
	EXPECT(flags & 0x10);
	command = new TXKeyCommand;
	command->ITXKeyCommand(doc, &bs, 1, flags, &failed);
	EXPECT(!failed && command->fRange.fStart.fOffset == 0 && command->fRange.fEnd.fOffset == 9);
	EXPECT(command->fUndoStream != nil);
	EXPECT(command->NewKey(&bs, 1, 0, flags, nil) == 0);
	EXPECT(command->NewKey(&bs, 1, 0, flags, nil) == 0);
	EXPECT(strcmp(TextOf(doc), "Hello,  world") == 0);
	EXPECT(command->fDeleteStart.fOffset == 7 && command->fEnd.fOffset == 7);
	EXPECT(command->Execute(&action) == noErr);				// undone
	EXPECT(strcmp(TextOf(doc), "Hello, my world") == 0);
	selected = Selection(doc);
	EXPECT(selected.fStart.fOffset == 7 && selected.fEnd.fOffset == 9);
	EXPECT(command->Execute(&action) == noErr);				// redone
	EXPECT(strcmp(TextOf(doc), "Hello,  world") == 0);
	delete command;
	delete doc;
}


static void
TestMove()
{
	Textension* doc = MakeDocument("one two three");
	doc->Activate(true, true);
	// "one " moved to the end
	TXMoveTextCommand* command = new TXMoveTextCommand;
	TXOffsetPos end;
	end.fOffset = 13;
	end.fAtStart = false;
	command->ITXMoveTextCommand(doc, TXOffsetRange(0, 4, false, true), end, false);
	EXPECT(command->fUndoTypes == 0 && command->fCanUndo);
	int action;
	EXPECT(command->Execute(&action) == noErr);
	EXPECT(strcmp(TextOf(doc), "two threeone ") == 0);
	EXPECT(command->fFrom.fStart.fOffset == 9 && command->fFrom.fEnd.fOffset == 13 && command->fTo.fOffset == 0);
	TXOffsetRange selected = Selection(doc);
	EXPECT(selected.fStart.fOffset == 9 && selected.fEnd.fOffset == 13);
	EXPECT(command->Execute(&action) == noErr);				// undone: moved back
	EXPECT(strcmp(TextOf(doc), "one two three") == 0);
	EXPECT(command->Execute(&action) == noErr);				// redone
	EXPECT(strcmp(TextOf(doc), "two threeone ") == 0);
	delete command;

	// "two" copied to the start
	command = new TXMoveTextCommand;
	TXOffsetPos start;
	start.fOffset = 0;
	start.fAtStart = false;
	command->ITXMoveTextCommand(doc, TXOffsetRange(0, 3, false, true), start, true);
	EXPECT(command->Execute(&action) == noErr);
	EXPECT(strcmp(TextOf(doc), "twotwo threeone ") == 0);
	EXPECT(command->Execute(&action) == noErr);				// undone: the copy taken away
	EXPECT(strcmp(TextOf(doc), "two threeone ") == 0);
	delete command;
	delete doc;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_TXCommand: cannot import %s\n", NEWTON_OBJECTS);
		return 1;
	}
	gObjectHeapSize = 0x100000;
	InitObjects();
	InitGraf();
	InitFonts();
	SetFrameSlot(RefVar(gVarFrame), RSSYMvars, RefVar(gVarFrame));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfunctions, RefVar(gFunctionFrame));
	RefVar fonts(AllocateFrame());
	RefVar list(Rromfontlist);
	for (long i = 0; i < Length(list); i++)
		SetFrameSlot(fonts, RefVar(FamilyNumToSym(i)), RefVar(GetArraySlotRef(list, i)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMfonts, fonts);
	RefVar intl(AllocateFrame());
	SetFrameSlot(intl, RSSYMcurrentlocalebundle, RefVar(TranslateROMRef(kUSABundle)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMinternational, intl);
	RefVar config(AllocateFrame());
	SetFrameSlot(config, RSSYMuserfont, RefVar(FontSpec("espy", 10, 0)));
	SetFrameSlot(RefVar(gVarFrame), RSSYMuserconfiguration, config);
	InitInternationalUtils();

	gMap.baseAddr = (Ptr) gBits;
	gMap.rowBytes = kRowBytes;
	SetRect(&gMap.bounds, 0, 0, kWidth, kHeight);
	gMap.pixMapFlags = kPixMapPtr | 1;
	gMap.deviceRes.v = kDefaultDPI;
	gMap.deviceRes.h = kDefaultDPI;
	gMap.grayTable = nil;
	OpenPort(&gPort);
	SetPortBits(&gMap);
	gPort.portRect = gMap.bounds;
	RectRgn(gPort.visRgn, &gMap.bounds);

	newton_try
	{
		EXPECT(Textension::TextensionStart() == noErr);
		Textension::RegisterRun(new TXNewtTextRun);
		Textension::RegisterRuler(new TXAdvancedRuler);
		TestReplace();
		TestRestyle();
		TestTyping();
		TestMove();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXCommand: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
