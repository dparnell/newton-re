// TXContainer test (src/text/TXContainer.h, Textension's Export and
// ReplaceRange from a container): a stretch of styled text exported into
// a local container on a handle stream - its table checked word by word -
// and imported into another document whole and as text alone; a picture
// put in on its own bringing its character; a failed import giving back
// the references it took.  The ROM image is imported for its fonts and
// its U.S. locale bundle.
#include "Textension.h"
#include "TXContainer.h"
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


// A container whose every write fails at the objects - after it has
// taken them.
class FailingContainer : public TXLocalContainer
{
public:
			FailingContainer(TXStream* stream) : TXLocalContainer(stream), fFreed(false)	{ }
	virtual NewtonErr EndValueWrite(void)	{ return -12345; }
	virtual Boolean FreeObjects(void)		{ fFreed = true; return TXLocalContainer::FreeObjects(); }
	Boolean	fFreed;
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


static TXNewtTextRun*
RunAt(Textension* doc, TXOffset at, long* length)
{
	return (TXNewtTextRun*) doc->fRuns->GetNextObjectRange(at, length);
}


static void
TestExportImport()
{
	Textension* from = MakeDocument("Hello big world");
	TXOffsetRange big(6, 9, false, true);
	TXAttrValues* bold = TXGetRunAttrValues(RefVar(FontSpec("geneva", 9, 1)));
	EXPECT(from->UpdateRangeRuns(big, bold, 0) == noErr);
	delete bold;
	long length;
	TXNewtTextRun* style = RunAt(from, 6, &length);
	EXPECT(length == 3 && EQRef(style->fFamily, SYM(geneva)) && style->fFace == 1);
	long references = style->GetCountReferences();

	// exported: three values on the stream, a table in front of them
	TXHandleStream stream;
	TXLocalContainer local(&stream);
	EXPECT(local.fBase == 0);
	TXOffsetRange range(6, 9, false, true);
	from->Export(&range, &local, kTXImportAll);
	EXPECT(style->GetCountReferences() == references + 1);		// the container holds one
	long size;
	stream.GetSize(&size);
	EXPECT(size == 0x28 + (4 + (long) sizeof(void*)) + (4 + (long) sizeof(void*)) + 3 * 2);
	unsigned char head[0x28];
	stream.SetPosition(0);
	EXPECT(stream.ReadBytes(head, sizeof(head)) == noErr);
	EXPECT(GetBigEndianWord(head) == 3);
	// runs first, then rulers, then text - Import's order
	EXPECT(GetBigEndianWord(head + 4) == kTXValueRuns && GetBigEndianWord(head + 8) == 1 && GetBigEndianWord(head + 12) == 4 + sizeof(void*));
	EXPECT(GetBigEndianWord(head + 16) == kTXValueRulers && GetBigEndianWord(head + 20) == 1);
	EXPECT(GetBigEndianWord(head + 28) == kTXValueText && GetBigEndianWord(head + 32) == 0 && GetBigEndianWord(head + 36) == 6);	// text has no objects: only a size

	// read back through a fresh container on the same stream
	TXLocalContainer again(&stream);
	stream.SetPosition(0);
	TXLocalContainer reread(&stream);
	EXPECT(reread.GetAvailTypes() == kTXImportAll);
	EXPECT(reread.ConvertAndFocusOnValue(kTXValueRuns) == noErr);
	long count = 0;
	EXPECT(reread.GetCountObjects(&count) == noErr && count == 1);
	TXAttrObject* object;
	unsigned char owned = 1;
	EXPECT(reread.ReadObject(0, &object, &length, &owned) == noErr);
	EXPECT(object == style && length == 3 && owned == 0);
	EXPECT(reread.ConvertAndFocusOnValue('none') == kTXErrNoValue);

	// imported whole into another document: text and style
	Textension* into = MakeDocument("ab");
	TXReplaceParams whole(&local, kTXImportAll);
	EXPECT(whole.fTypes == kTXImportAll);
	EXPECT(into->ReplaceRange(1, 1, &whole) == noErr);
	EXPECT(strcmp(TextOf(into), "abigb") == 0);
	EXPECT(into->fRuns->GetLastRangeEnd() == 5 && into->fRulers->GetLastRangeEnd() == 5);
	TXNewtTextRun* got = RunAt(into, 1, &length);
	EXPECT(length == 3 && got->IsEqual(style));
	got = RunAt(into, 4, &length);
	EXPECT(length == 1 && !got->IsEqual(style));
	EXPECT(into->fFormatter->fLineEnds->GetLastRangeEnd() == 5);
	TXOffsetRange caret;
	into->fHilite->GetHiliteRange(&caret);
	EXPECT(caret.fStart.fOffset == 4);

	// text alone: in the style of the text it goes into
	Textension* plain = MakeDocument("xy");
	TXReplaceParams text(&local, kTXImportText);
	EXPECT(text.fTypes == kTXImportText);
	EXPECT(plain->ReplaceRange(2, 2, &text) == noErr);
	EXPECT(strcmp(TextOf(plain), "xybig") == 0);
	got = RunAt(plain, 0, &length);
	EXPECT(length == 5 && !got->IsEqual(style));

	// the runs alone (no text): the stretch restyled in place
	Textension* restyle = MakeDocument("12345");
	TXReplaceParams runs(&local, kTXImportRuns);
	EXPECT(restyle->ReplaceRange(1, 4, &runs) == noErr);
	EXPECT(strcmp(TextOf(restyle), "12345") == 0);
	got = RunAt(restyle, 1, &length);
	EXPECT(length == 3 && got->IsEqual(style));
	EXPECT(restyle->fPendingRunInvalid);

	// the container's reference given back (the documents it was
	// imported into share the style too)
	references = style->GetCountReferences();
	EXPECT(local.FreeObjects());
	EXPECT(style->GetCountReferences() == references - 1);

	delete restyle;
	delete plain;
	delete into;
	delete from;
}


// A picture on its own: written by its public type, it brings the
// character that stands for it.
static void
TestPicture()
{
	TXNewtGraphicsRun* picture = new TXNewtGraphicsRun;
	TXHandleStream stream;
	TXLocalContainer local(&stream);
	TXContainerImportInfo written(kTXImportRuns);
	EXPECT(local.BeginWrite() == noErr);
	EXPECT(local.AppendNewValue(picture->GetPublicType(), 1) == noErr);
	unsigned char reference = true;
	EXPECT(local.WriteObject(0, picture, 1, &reference) == noErr);
	EXPECT(local.EndValueWrite() == noErr);
	EXPECT(local.EndWrite(false, &written) == noErr);
	stream.SetPosition(0);
	TXLocalContainer reread(&stream);
	EXPECT(reread.GetAvailTypes() == (kTXImportText | kTXImportRuns));

	Textension* doc = MakeDocument("ab");
	TXReplaceParams params(&reread, kTXImportAll);
	EXPECT(params.fTypes == (kTXImportText | kTXImportRuns));
	EXPECT(doc->ReplaceRange(1, 1, &params) == noErr);
	EXPECT(doc->fChars->Count() == 3);
	UniChar chars[3];
	TXTextDescriptor to;
	to.Set(chars, 3);
	doc->fChars->CopyTo(&to, 0, 3);
	EXPECT(chars[0] == 'a' && chars[1] == gTXGraphicsRunChar[0] && chars[2] == 'b');
	long length;
	TXAttrObject* run = doc->fRuns->GetNextObjectRange(1, &length);
	EXPECT(length == 1 && run->GetPublicType() == picture->GetPublicType());
	TXOffsetRange one(1, 2, false, true);
	EXPECT(doc->IsRangeGraphicsRun(&one) != nil);
	delete doc;
	reread.FreeObjects();
	picture->Free();
}


// A failed write gives back what it took.
static void
TestFailedImport()
{
	Textension* from = MakeDocument("abc");
	TXAttrObject* run = from->fRuns->RangeIndexToObject(0);
	long references = run->GetCountReferences();
	TXHandleStream stream;
	FailingContainer failing(&stream);
	TXOffsetRange range(0, 3, false, true);
	TXPrivateContainer here(0, 3, from->fRuns, from->fRulers, from->fChars, from->fFormatter);
	TXContainerImportInfo info(kTXImportAll);
	EXPECT(failing.Import(&here, &info) == -12345);
	EXPECT(failing.fFreed);
	EXPECT(info.fTypes == 0);
	EXPECT(run->GetCountReferences() == references);
	delete from;
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_OBJECTS) != noErr)
	{
		printf("test_TXContainer: cannot import %s\n", NEWTON_OBJECTS);
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
		TestExportImport();
		TestFailedImport();
		Textension::RegisterRun(new TXNewtGraphicsRun);
		TestPicture();
	}
	newton_catch_all
	{
		failures++;
		fprintf(stderr, "FAIL: unhandled exception %s (%ld)\n", _info.exception.name, (long) (Long) _info.exception.data);
	}
	end_try;
	ClosePort(&gPort);
	printf("test_TXContainer: %s\n", failures == 0 ? "ok" : "FAILED");
	return failures == 0 ? 0 : 1;
}
