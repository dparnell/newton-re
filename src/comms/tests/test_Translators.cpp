// Frame translators test (comms/Translators.h): script values turned into
// bytes by their form and back (PScriptDataOut/In), and option frames as a
// script writes them - the NIE's 'inet service and its 'itrs remote socket
// among them - turned into an option array and its results and data read
// back into the frames (POptionDataOut/In).  The ROM's objects are imported
// for the marshalling type symbols; the frames are compiled NewtonScript.

#include "Translators.h"
#include "CommOptions.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "ROMImport.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "NewtonExceptions.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Ref
Eval(const char* source)
{
	RefVar fn(ParseString(RefVar(MakeString(source))));
	if ((Ref) fn == NILREF)
		return NILREF;
	return InterpretBlock(fn, RefVar(gVarFrame));
}

static Ref SYMBOL(const char* name) { return Intern((char*) name); }

// a value through PScriptDataOut into a Ptr with `header` bytes in front;
// the error thrown, if one was
static Ptr
Out(PScriptDataOut* out, RefArg value, FormType form, long header, long* error)
{
	FrameSinkParms parms;
	parms.fValue = value;
	parms.fForm = form;
	parms.fEncoding = 1;
	parms.fUseHandle = false;
	parms.fHeaderSize = header;
	Ptr p = nil;
	*error = noErr;
	newton_try
	{
		p = (Ptr) out->Translate(&parms, nil);
	}
	newton_catch_all
	{
		*error = (long) (Long) CurrentException()->data;
	}
	end_try;
	return p;
}

static Ref
In(PScriptDataIn* in, const void* data, long length, FormType form, RefArg typelist, long* error)
{
	FrameSourceParms parms;
	parms.fData = (UByte*) data;
	parms.fLength = length;
	parms.fForm = form;
	parms.fEncoding = 1;
	parms.fTemplate = typelist;
	RefVar result;
	*error = noErr;
	newton_try
	{
		result = in->Translate(&parms, nil);
	}
	newton_catch_all
	{
		*error = (long) (Long) CurrentException()->data;
	}
	end_try;
	return result;
}


static void
TestForms(void)
{
	EXPECT(GetDataForm(RefVar(), kFormUserOption) == kFormTemplate);
	EXPECT(GetDataForm(RefVar(), kFormUserData) == kFormString);
	EXPECT(GetDataForm(RefVar(), kFormUserAny) == kFormExport);
	EXPECT(GetDataForm(RefVar(SYMBOL("raw")), kFormUserData) == kFormBinary);
	EXPECT(GetDataForm(RefVar(SYMBOL("binary")), kFormUserData) == kFormBinary);
	EXPECT(GetDataForm(RefVar(SYMBOL("number")), kFormUserOption) == kFormNumber);
	EXPECT(GetDataForm(RefVar(SYMBOL("frame")), kFormUserOption) == kFormNone);
	EXPECT(GetDataForm(RefVar(SYMBOL("frame")), kFormUserData) == kFormFrame);
	EXPECT(GetDataForm(RefVar(SYMBOL("export")), kFormUserData) == kFormNone);
	EXPECT(GetDataForm(RefVar(SYMBOL("export")), kFormUserAny) == kFormExport);
	EXPECT(GetDataForm(RefVar(SYMBOL("bytes")), kFormUserData) == kFormBytes);
	EXPECT(GetDataForm(RefVar(SYMBOL("char")), kFormUserData) == kFormChar);
	EXPECT(GetDataForm(RefVar(SYMBOL("nonsense")), kFormUserData) == kFormNone);
}


static void
TestScriptData(void)
{
	PScriptDataOut* out = (PScriptDataOut*) PScriptDataOut::ClassInfo()->New();
	PScriptDataIn* in = (PScriptDataIn*) PScriptDataIn::ClassInfo()->New();
	EXPECT(out != nil && in != nil);
	long err;

	// a string: its characters, no terminator, after the header
	Ptr p = Out(out, RefVar(MakeString("hello")), kFormString, 4, &err);
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 9 && memcmp(p + 4, "hello", 5) == 0);
	RefVar s(In(in, p + 4, 5, kFormString, RefVar(), &err));
	EXPECT(err == noErr && IsString(s) && Length(s) == 6 * (long) sizeof(UniChar) && GetCString(s)[4] == 'o');
	DisposPtr(p);

	// an array of strings, one after another
	p = Out(out, RefVar(Eval("[\"ab\", \"cde\"]")), kFormString, 0, &err);
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 5 && memcmp(p, "abcde", 5) == 0);
	DisposPtr(p);

	// a number: a big-endian long
	p = Out(out, RefVar(MAKEINT(0x10203)), kFormNumber, 0, &err);
	static const UByte kNumber[4] = { 0, 1, 2, 3 };
	EXPECT(err == noErr && p != nil && memcmp(p, kNumber, 4) == 0);
	RefVar n(In(in, p, 4, kFormNumber, RefVar(), &err));
	EXPECT(err == noErr && ISINT(n) && RINT(n) == 0x10203);
	DisposPtr(p);

	// a character, a byte (export takes either)
	p = Out(out, RefVar(MAKECHAR('Z')), kFormExport, 0, &err);
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 1 && p[0] == 'Z');
	RefVar c(In(in, p, 1, kFormChar, RefVar(), &err));
	EXPECT(err == noErr && ISCHAR(c) && RCHAR(c) == 'Z');
	DisposPtr(p);
	p = Out(out, RefVar(Eval("[1, 2, 255]")), kFormBytes, 0, &err);
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 3 && (UByte) p[2] == 255);
	RefVar bytes(In(in, p, 3, kFormBytes, RefVar(), &err));
	EXPECT(err == noErr && IsArray(bytes) && Length(bytes) == 3 && RINT(GetArraySlotRef(bytes, 2)) == 255);
	DisposPtr(p);

	// a binary, as it is; and back into a binary
	RefVar bin(AllocateBinary(RefVar(SYMBOL("binary")), 3));
	memcpy(BinaryData(bin), "xyz", 3);
	p = Out(out, bin, kFormBinary, 0, &err);
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 3 && memcmp(p, "xyz", 3) == 0);
	RefVar back(In(in, p, 3, kFormBinary, RefVar(), &err));
	EXPECT(err == noErr && IsBinary(back) && Length(back) == 3 && memcmp(BinaryData(back), "xyz", 3) == 0);
	DisposPtr(p);

	// a 'template: marshalled
	p = Out(out, RefVar(Eval("{arglist: [[10, 0, 0, 2], 80], typelist: ['struct, ['array, 'byte, 4], 'short]}")),
			kFormTemplate, 0, &err);
	static const UByte kSocket[6] = { 10, 0, 0, 2, 0, 80 };
	EXPECT(err == noErr && p != nil && GetPtrSize(p) == 8 && memcmp(p, kSocket, 6) == 0);
	DisposPtr(p);

	// in a Handle
	{
		FrameSinkParms parms;
		parms.fValue = MakeString("hi");
		parms.fForm = kFormString;
		parms.fEncoding = 1;
		parms.fUseHandle = true;
		parms.fHeaderSize = 2;
		Handle h = (Handle) out->Translate(&parms, nil);
		EXPECT(h != nil && GetHandleSize(h) == 4 && memcmp(*h + 2, "hi", 2) == 0);
		if (h != nil)
			DisposHandle(h);
	}

	// what the form cannot take is thrown; so is nothing
	p = Out(out, RefVar(MakeString("12")), kFormNumber, 0, &err);
	EXPECT(p == nil && err == kCommScriptErrBadForm);
	p = Out(out, RefVar(MakeString("")), kFormString, 0, &err);
	EXPECT(p == nil && err == kCommScriptErrNoData);
	p = Out(out, RefVar(Eval("{arglist: [1]}")), kFormTemplate, 0, &err);
	EXPECT(p == nil && err == kCommScriptErrBadTemplate);
	RefVar none(In(in, "x", 1, kFormFrame, RefVar(), &err));
	EXPECT(ISNIL(none) && err == kCommScriptErrBadForm);

	out->Delete();
	in->Delete();
}


static void
TestOptions(void)
{
	PScriptDataOut* sink = (PScriptDataOut*) PScriptDataOut::ClassInfo()->New();
	PScriptDataIn* source = (PScriptDataIn*) PScriptDataIn::ClassInfo()->New();
	POptionDataOut* optionsOut = (POptionDataOut*) POptionDataOut::ClassInfo()->New();
	POptionDataIn* optionsIn = (POptionDataIn*) POptionDataIn::ClassInfo()->New();

	RefVar frames(Eval(
		"[{label: \"inet\", type: 'service},"
		" [{label: \"itrs\", type: 'option, opCode: 512,"
		"   data: {arglist: [[127, 0, 0, 1], 0x1234], typelist: ['struct, ['array, 'byte, 4], 'short]}}],"
		" {label: \"ilpt\", type: 'option, form: 'number, data: 7},"
		" {label: \"cfg \", type: 'config}]"));
	EXPECT(IsArray(frames) && Length(frames) == 4);

	TOptionArray options;
	options.Init();
	OptionDataParms parms;
	parms.fOptions = &options;
	parms.fFrame = frames;
	parms.fXlator = sink;
	EXPECT(optionsOut->Translate(&parms, nil) == &options);
	EXPECT(options.GetArrayCount() == 4);

	// a service by name is a 'sid ' option naming it
	TCMOServiceIdentifier* sid = (TCMOServiceIdentifier*) options.OptionAt(0);
	EXPECT(sid->Label() == kCMOServiceIdentifier && sid->IsService());
	EXPECT(sid->fServiceId == 'inet' && sid->fPortId == 0 && sid->GetOpCode() == opSetRequired);

	// the remote socket: the template's bytes, big-endian, rounded to a word
	TOption* itrs = options.OptionAt(1);
	static const UByte kSocket[6] = { 0x7f, 0, 0, 1, 0x12, 0x34 };
	EXPECT(itrs->Label() == 'itrs' && itrs->IsOption() && itrs->GetOpCode() == opSetRequired);
	EXPECT(itrs->Length() == 8 && memcmp(itrs + 1, kSocket, 6) == 0);

	// a number, opSetNegotiate when no op code is given
	TOption* ilpt = options.OptionAt(2);
	static const UByte kPort[4] = { 0, 0, 0, 7 };
	EXPECT(ilpt->Label() == 'ilpt' && ilpt->GetOpCode() == opSetNegotiate);
	EXPECT(ilpt->Length() == 4 && memcmp(ilpt + 1, kPort, 4) == 0);

	// no data: an option of no length
	TOption* cfg = options.OptionAt(3);
	EXPECT(cfg->Label() == 'cfg ' && cfg->IsConfig() && cfg->Length() == 0);

	// back: the results and the data into the frames (option i into frame i)
	itrs->SetOpCodeResult(opFailure);
	{
		UByte* data = (UByte*) (ilpt + 1);
		data[3] = 9;
	}
	RefVar flat(Eval("[{form: 'number}, {label: \"itrs\", data: {typelist: ['struct, ['array, 'byte, 4], 'short]}}, {form: 'number}, {form: 'binary}]"));
	parms.fFrame = flat;
	parms.fXlator = source;
	long err = noErr;
	newton_try
	{
		optionsIn->Translate(&parms, nil);
	}
	newton_catch_all
	{
		err = (long) (Long) CurrentException()->data;
	}
	end_try;
	if (err != noErr)
		printf("options in: %ld\n", err);
	EXPECT(err == noErr);
	RefVar f1(GetArraySlotRef(flat, 1));
	EXPECT(RINT(GetFrameSlot(f1, RSSYMresult)) == kCommScriptOptionResultBase - 1);
	RefVar d1(GetFrameSlot(f1, RSSYMdata));
	RefVar args(GetFrameSlot(d1, RSSYMarglist));
	EXPECT(IsArray(args) && Length(args) == 2 && RINT(GetArraySlotRef(args, 1)) == 0x1234);
	RefVar address(GetArraySlotRef(args, 0));
	EXPECT(IsArray(address) && Length(address) == 4 && RINT(GetArraySlotRef(address, 0)) == 0x7f && RINT(GetArraySlotRef(address, 3)) == 1);
	RefVar f2(GetArraySlotRef(flat, 2));
	EXPECT(ISNIL(GetFrameSlot(f2, RSSYMresult)) && RINT(GetFrameSlot(f2, RSSYMdata)) == 9);

	// not an option: nothing added, the error kept to itself
	long error = noErr;
	TOption* bad = optionsOut->ConvertToOption(RefVar(Eval("{type: 'option}")), error, sink);
	EXPECT(bad == nil && error == kCommScriptErrNotAnOption);
	bad = optionsOut->ConvertToOption(RefVar(Eval("{label: \"abcd\", type: 'nonsense}")), error, sink);
	EXPECT(bad == nil && error == kCommScriptErrNotAnOption);
	bad = optionsOut->ConvertToOption(RefVar(Eval("{label: \"abcd\", type: 'option, form: 'frame}")), error, sink);
	EXPECT(bad == nil && error == kCommScriptErrBadForm);

	sink->Delete();
	source->Delete();
	optionsOut->Delete();
	optionsIn->Delete();
}


int
main()
{
	InitHostStandaloneHeap();
	if (ImportROMObjectsFromFile(NEWTON_ROM_BIN) != noErr)
	{
		printf("test_Translators: cannot import %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	gObjectHeapSize = 0x200000;
	InitObjects();

	TestForms();
	TestScriptData();
	TestOptions();

	printf("test_Translators: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
