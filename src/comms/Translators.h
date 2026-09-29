/*
	File:		comms/Translators.h

	Contains:	The frame translators - the protocols a NewtonScript value is
				turned into bytes through (PFrameSink) and bytes back into a
				value (PFrameSource) - and the four implementations the
				NewtonScript endpoint works with:

				PScriptDataOut	a value into a block of bytes by its form
								(a string, a character, a number, a byte, a
								binary, or a 'template frame marshalled by its
								typelist), after a header of the caller's size
				PScriptDataIn	bytes back into a value by a form
				POptionDataOut	an option frame (label, type, opCode, form,
								data) or an array of them into a TOptionArray,
								the data through a PFrameSink
				POptionDataIn	an option array's results and data back into
								the frames it was made from, through a
								PFrameSource

				The forms are FormType; GetDataForm turns a script's form
				symbol into one ('raw/'binary, 'template, 'string, 'number,
				'export, 'frame, 'bytes, 'char), a nil form meaning 'template
				for an option and 'string for data.

				The bytes are the MessagePad's (big-endian): a 'number is a
				big-endian long, a 'template marshalled by frames/Marshalling.h,
				which writes the device's byte order.  An option's label is
				the first four characters of its label string, a big-endian
				word as the ROM reads its four bytes.  DEVIATION (pointer
				size): an option's header is sizeof(TOption), not twelve bytes
				(comms/Options.h), and a 'sid ' option made for a 'service
				frame is the host's TCMOServiceIdentifier.

				The context each Translate is given is a struct the ROM builds
				on its stack; its fields are named here (FrameSinkParms,
				FrameSourceParms, OptionDataParms) with the ROM's offsets.

				And the flatteners, a value as NSOF (stores/ObjectStreamer.h)
				and back: PFlattenPtr into a new block (after a header), and
				PUnFlattenPtr out of one; PFlattenRef into a new binary (in
				the heap or a large binary on a store), and PUnFlattenRef out
				of one (functions refused when asked).

				InitTranslators puts them in the protocol registry, as the
				ROM's does (NOT YET: PStreamInRef and PStreamOutRef, NSOF
				read from and written to an endpoint through a
				TEndpointPipe).

	Reconstructed from the MP2x00 US ROM (0x00139e40, 0x0014b9f4-0x0014c49c,
	0x001cd534-0x001cdf00, 0x00256220, 0x00389f18-0x00389fd4); each
	function cites its origin.  docs/comms/README.md.
*/

#ifndef __COMMS_TRANSLATORS_H
#define __COMMS_TRANSLATORS_H

#ifndef __PROTOCOLS_H
#include "protocols/Protocols.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __COMMS_OPTIONS_H
#include "Options.h"
#endif

class PipeCallBack;

// What a value is turned into (GetDataForm)
typedef long FormType;
enum
{
	kFormNone		= 0,		// not a form this knows
	kFormExport		= 1,		// whatever the value is: a string, a character, a byte or a binary
	kFormChar		= 2,
	kFormString		= 3,
	kFormNumber		= 4,		// a long
	kFormBytes		= 5,		// a byte (an array of them, in)
	kFormBinary		= 6,		// 'raw or 'binary
	kFormFrame		= 7,		// (flattened: not by these translators)
	kFormTemplate	= 8			// {arglist, typelist}, marshalled
};

// Who is asking (GetDataForm): an option's data, or data sent or received
typedef long FormUser;
enum
{
	kFormUserOption	= 0,		// nil is 'template, and 'frame is no form
	kFormUserData	= 1,		// nil is 'string
	kFormUserAny	= 2			// nil is 'export, and anything goes
};

FormType	GetDataForm(RefArg form, FormUser user);

// The translators' errors (and the result an option's status becomes:
// kNSErrOptionResultBase + its signed result byte)
#define kCommScriptErrBadForm			(-54001)	// a value the form cannot take, or no form
#define kCommScriptErrNoData			(-54002)	// nothing came of it
#define kCommScriptErrNotAnOption		(-54004)	// not a frame, no label, or a type this does not know
#define kCommScriptErrBadTypelist		(-54011)	// an option's template data has no typelist (the ROM's 0xffff2d05)
#define kCommScriptErrBadTemplate		(-54011)	// a 'template with no arglist or typelist
#define kCommScriptOptionResultBase		(-54020)


/* -------------------------------------------------------------------------------
	The protocols
------------------------------------------------------------------------------- */

PROTOCOL PFrameSink : public TProtocol
{
public:
	static PFrameSink*	New(char* implementation);
	void			Delete();
	VIRTUAL void*	Translate(void* context, PipeCallBack* callback) ENDVIRTUAL;	// ==> what was made
};

PROTOCOL PFrameSource : public TProtocol
{
public:
	static PFrameSource*	New(char* implementation);
	void			Delete();
	VIRTUAL Ref		Translate(void* context, PipeCallBack* callback) ENDVIRTUAL;	// ==> the value
};


// PScriptDataOut's context: the value, its form, the text encoding, whether
// the block is a Handle (else a Ptr), and how many bytes to leave in front
// of the data
struct FrameSinkParms
{
	RefVar		fValue;			// +0x00
	FormType	fForm;			// +0x04
	long		fEncoding;		// +0x08
	Boolean		fUseHandle;		// +0x0c
	long		fHeaderSize;	// +0x10
};

// PScriptDataIn's context: the bytes, how many, their form, the encoding,
// and the typelist a 'template's bytes are read by
struct FrameSourceParms
{
	UByte*		fData;			// +0x00
	long		fLength;		// +0x04
	FormType	fForm;			// +0x08
	long		fEncoding;		// +0x0c
	RefVar		fTemplate;		// +0x10
};

// POptionDataOut's and POptionDataIn's context: the option array, the
// option frame (or array of them), and the translator the data goes through
struct OptionDataParms
{
	TOptionArray*	fOptions;	// +0x00
	RefVar			fFrame;		// +0x04
	TProtocol*		fXlator;	// +0x08  a PFrameSink out, a PFrameSource in
};


// PFlattenPtr's context: the value, whether the block is a Handle, and how
// many bytes to leave in front of the stream
struct FlattenPtrParms
{
	RefVar		fValue;			// +0x00
	Boolean		fUseHandle;		// +0x04
	long		fHeaderSize;	// +0x08
};

// PUnFlattenPtr's: the stream's bytes and the store large binaries go to
struct UnflattenPtrParms
{
	void*		fData;			// +0x00
	long		fLength;		// +0x04
	RefVar		fStore;			// +0x08
};

// PFlattenRef's: the value, and the store the binary goes on (nil: the heap)
struct FlattenRefParms
{
	RefVar		fValue;			// +0x00
	RefVar		fStore;			// +0x04
};

// PUnFlattenRef's: the binary, the store, and whether functions are refused
struct UnflattenRefParms
{
	RefVar		fBinary;		// +0x00
	RefVar		fStore;			// +0x04
	Boolean		fNoFunctions;	// +0x08
};


/* -------------------------------------------------------------------------------
	The implementations
------------------------------------------------------------------------------- */

PROTOCOL PScriptDataOut : public PFrameSink
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PScriptDataOut);
	PScriptDataOut*	New();
	void			Delete();
	void*			Translate(void* context, PipeCallBack* callback);	// context: FrameSinkParms

	long			ParseOutputLength(RefArg value, FormType form, long encoding, long* length);
	long			ParseOutput(RefArg value, FormType form, long encoding, long* length, UByte* buf);
};

PROTOCOL PScriptDataIn : public PFrameSource
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PScriptDataIn);
	PScriptDataIn*	New();
	void			Delete();
	Ref				Translate(void* context, PipeCallBack* callback);	// context: FrameSourceParms

	Ref				ParseInput(FormType form, long encoding, long length, UByte* data, RefArg typelist, long* error);
};

PROTOCOL POptionDataOut : public PFrameSink
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(POptionDataOut);
	POptionDataOut*	New();
	void			Delete();
	void*			Translate(void* context, PipeCallBack* callback);	// context: OptionDataParms ==> the option array

	long			ConvertToOptionArray(RefArg frame, TOptionArray* options, PFrameSink* sink);
	TOption*		ConvertToOption(RefArg frame, long& error, PFrameSink* sink);
	void*			ParseOutput(PFrameSink* sink, RefArg data, FormType form, long* error);
};

PROTOCOL POptionDataIn : public PFrameSource
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(POptionDataIn);
	POptionDataIn*	New();
	void			Delete();
	Ref				Translate(void* context, PipeCallBack* callback);	// context: OptionDataParms ==> the frame(s)

	long			ConvertFromOptionArray(RefArg frame, TOptionArray* options, PFrameSource* source);
	long			ConvertFromOption(RefArg frame, TOption* option, PFrameSource* source);
	Ref				ParseInput(PFrameSource* source, FormType form, long length, UByte* data, RefArg typelist, long* error);
};


PROTOCOL PFlattenPtr : public PFrameSink
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PFlattenPtr);
	PFlattenPtr*	New();
	void			Delete();
	void*			Translate(void* context, PipeCallBack* callback);	// context: FlattenPtrParms
};

PROTOCOL PUnFlattenPtr : public PFrameSource
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PUnFlattenPtr);
	PUnFlattenPtr*	New();
	void			Delete();
	Ref				Translate(void* context, PipeCallBack* callback);	// context: UnflattenPtrParms
};

PROTOCOL PFlattenRef : public PFrameSink
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PFlattenRef);
	PFlattenRef*	New();
	void			Delete();
	void*			Translate(void* context, PipeCallBack* callback);	// context: FlattenRefParms ==> the binary, as a Ref
};

PROTOCOL PUnFlattenRef : public PFrameSource
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(PUnFlattenRef);
	PUnFlattenRef*	New();
	void			Delete();
	Ref				Translate(void* context, PipeCallBack* callback);	// context: UnflattenRefParms
};


void		InitTranslators(void);

#endif
