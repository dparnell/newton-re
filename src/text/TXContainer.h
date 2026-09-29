/*
	File:		text/TXContainer.h

	Contains:	The containers: a piece of a document - its characters,
				its runs and its rulers - as *values* of a type, so that it
				can be moved from one document (or one place) to another.

				A container holds up to three values: 'TEXT' (the
				characters), 'txrn' (the runs - or a picture's run, by its
				public type, 'shap') and 'txrl' (the rulers), each a count
				of objects or characters and a size.  `Import` copies the
				values one container has into another, value by value
				(`ImportText`, `ImportObjects`), bracketed by `BeginWrite`
				and `EndWrite`; `TXContainerImportInfo` says which values to
				take and answers which were taken and how many of each.

				`TXStdContainer` keeps its values on a TXStream: a count and
				a table of three (type, count, size) entries, then the
				values one after another (`FocusOnValue` finds one by its
				type).  `TXLocalContainer` is the one an undo buffer is:
				its objects are written as a length and a pointer to the
				object itself, a reference taken - so it only lives as long
				as the objects do, and `FreeObjects` gives them back.
				`TXPrivateContainer` is a stretch of a live document
				(Textension): reading it reads the document's characters
				and objects in place, and writing to it *replaces* that
				stretch - the new runs and rulers gathered into ranges of
				their own and put in when the write ends.  A picture put in
				on its own brings the character that stands for it
				(`gTXGraphicsRunChar`).

				TXNewtContainer (a NewtonScript frame of the same values)
				is NOT YET; it comes with TXView.

				No destructors are in the ROM's vtables; the containers are
				stack objects.

	Reconstructed from the MP2x00 US ROM (0x00234804-0x00235b00); each
	function cites its origin.  ImportObjects was read from the assembly.
*/

#ifndef __TXCONTAINER_H
#define __TXCONTAINER_H

#ifndef __TXCHARS_H
#include "TXChars.h"
#endif
#ifndef __TXOBJECTRANGE_H
#include "TXObjectRange.h"
#endif

class TXStream;
class TXFormatter;

// the value types
const unsigned long	kTXValueText	= 0x54455854;	// 'TEXT'
const unsigned long	kTXValueRuns	= 0x7478726e;	// 'txrn'
const unsigned long	kTXValueRulers	= 0x7478726c;	// 'txrl'

// which values (TXContainerImportInfo::fTypes, GetAvailTypes)
enum
{
	kTXImportText	= 1,
	kTXImportRuns	= 2,
	kTXImportRulers	= 4,
	kTXImportAll	= 7
};

const NewtonErr	kTXErrNoValue	= -102;					// a container without that value

// The character a picture put in on its own stands on.
extern const unsigned short	gTXGraphicsRunChar[1];		// (TXGraphicsRunChar.cpp, generated)

// What to import, and what was: the ROM's is 0x10 bytes.
struct TXContainerImportInfo
{
					TXContainerImportInfo(unsigned char types);		// ROM 0x002348c4 __ct__21TXContainerImportInfoFUc

	unsigned char	fTypes;			// +0x00  asked for, then taken
	long			fTextCount;		// +0x04  characters
	long			fRunCount;		// +0x08  runs
	long			fRulerCount;	// +0x0c  rulers
};


class TXContainer
{
public:
					TXContainer(TXStream* stream);					// ROM 0x00234804 __ct__11TXContainerFP8TXStream

	virtual NewtonErr BeginWrite(void);								// ROM 0x00235ae8 BeginWrite__11TXContainerFv
	virtual NewtonErr AppendNewValue(unsigned long type, long count);	// ROM 0x00235704 AppendNewValue__11TXContainerFUll
	virtual NewtonErr WriteText(TXTextDescriptor* text);			// ROM 0x00234888 WriteText__11TXContainerFP16TXTextDescriptor
	// `*reference`: a reference to the object is to be taken.
	virtual NewtonErr WriteObject(long index, TXAttrObject* object, long length, unsigned char* reference) = 0;	// (pure: +0x0c)
	virtual NewtonErr EndValueWrite(void);							// ROM 0x00235af0 EndValueWrite__11TXContainerFv
	virtual NewtonErr EndWrite(Boolean failed, TXContainerImportInfo* info);	// ROM 0x00235af8 EndWrite__11TXContainerFUcP21TXContainerImportInfo
	virtual NewtonErr FocusOnValue(unsigned long type);				// ROM 0x00235274 FocusOnValue__11TXContainerFUl
	virtual NewtonErr GetCountObjects(long* count) = 0;				// (pure: +0x1c)
	virtual NewtonErr GetValueSize(long* size) = 0;					// (pure: +0x20) in bytes
	virtual void	AcquireTextDescriptor(TXTextDescriptor* text);	// ROM 0x00234844 AcquireTextDescriptor__11TXContainerFP16TXTextDescriptor
	virtual void	ReleaseTextDescriptor(TXTextDescriptor* text);	// ROM 0x00234884 ReleaseTextDescriptor__11TXContainerFP16TXTextDescriptor
	// `*owned`: the object is the caller's to give back.
	virtual NewtonErr ReadObject(long index, TXAttrObject** object, long* length, unsigned char* owned) = 0;	// (pure: +0x2c)
	virtual unsigned long ConvertValueType(unsigned long type);		// ROM 0x00235a9c ConvertValueType__11TXContainerFUl - 0: not kept
	virtual void	SetStream(TXStream* stream);					// ROM 0x00234e60 SetStream__11TXContainerFP8TXStream
	virtual			~TXContainer()	{ }								// (host: after the ROM's entries)

	NewtonErr		ConvertAndFocusOnValue(unsigned long type);		// ROM 0x00235ab0 ConvertAndFocusOnValue__11TXContainerFUl
	unsigned char	GetAvailTypes(void);							// ROM 0x00234904 GetAvailTypes__11TXContainerFv
	NewtonErr		Import(TXContainer* source, TXContainerImportInfo* info);	// ROM 0x002349d0 Import__11TXContainerFP11TXContainerP21TXContainerImportInfo
	NewtonErr		ImportText(TXContainer* source, long* count);	// ROM 0x00234b7c ImportText__11TXContainerFP11TXContainerPl
	NewtonErr		ImportObjects(TXContainer* source, unsigned long type, long* count);	// ROM 0x00234c80 ImportObjects__11TXContainerFP11TXContainerUlPl

	TXStream*		fStream;		// +0x04
	unsigned long	fType;			// +0x08  the value in hand
};


// One entry of a TXStdContainer's table.
struct TXContainerValue
{
	unsigned long	fType;			// +0x00
	long			fCount;			// +0x04
	long			fSize;			// +0x08
};

const long	kTXContainerValuesMax	= 3;


class TXStdContainer : public TXContainer
{
public:
					TXStdContainer(TXStream* stream);				// ROM 0x00234de8 __ct__14TXStdContainerFP8TXStream

	virtual NewtonErr BeginWrite(void);								// ROM 0x00234e98 BeginWrite__14TXStdContainerFv
	virtual NewtonErr AppendNewValue(unsigned long type, long count);	// ROM 0x00234fd0 AppendNewValue__14TXStdContainerFUll
	virtual NewtonErr WriteText(TXTextDescriptor* text);			// ROM 0x002350c0 WriteText__14TXStdContainerFP16TXTextDescriptor
	virtual NewtonErr WriteObject(long index, TXAttrObject* object, long length, unsigned char* reference);	// ROM 0x00235028 WriteObject__14TXStdContainerFlP12TXAttrObjectT1PUc
	virtual NewtonErr EndWrite(Boolean failed, TXContainerImportInfo* info);	// ROM 0x00234f20 EndWrite__14TXStdContainerFUcP21TXContainerImportInfo
	virtual NewtonErr FocusOnValue(unsigned long type);				// ROM 0x00235100 FocusOnValue__14TXStdContainerFUl
	virtual NewtonErr GetCountObjects(long* count);					// ROM 0x002351f4 GetCountObjects__14TXStdContainerFPl
	virtual NewtonErr GetValueSize(long* size);						// ROM 0x00235210 GetValueSize__14TXStdContainerFPl
	virtual void	SetStream(TXStream* stream);					// ROM 0x00234e68 SetStream__14TXStdContainerFP8TXStream
	virtual NewtonErr WriteObjectData(TXAttrObject* object, long length, unsigned char* reference) = 0;	// (pure: +0x38)

	// the table at fBase (host: words big-endian)
	NewtonErr		WriteTable(void);

	long			fBase;			// +0x0c  where on the stream the container starts
	long			fCount;			// +0x10  values (-1: the table is to be read)
	long			fCurrent;		// +0x14  the value focused on
	TXContainerValue fValues[kTXContainerValuesMax];	// +0x18
};


class TXLocalContainer : public TXStdContainer
{
public:
					TXLocalContainer(TXStream* stream);				// ROM 0x0023522c __ct__16TXLocalContainerFP8TXStream

	virtual NewtonErr EndWrite(Boolean failed, TXContainerImportInfo* info);	// ROM 0x0023530c EndWrite__16TXLocalContainerFUcP21TXContainerImportInfo
	virtual NewtonErr ReadObject(long index, TXAttrObject** object, long* length, unsigned char* owned);	// ROM 0x0023534c ReadObject__16TXLocalContainerFlPP12TXAttrObjectPlPUc
	virtual NewtonErr WriteObjectData(TXAttrObject* object, long length, unsigned char* reference);	// ROM 0x00235280 WriteObjectData__16TXLocalContainerFP12TXAttrObjectlPUc
	// Every object of every value given back.  ==> whether there were runs.
	virtual Boolean	FreeObjects(void);								// ROM 0x00235468 FreeObjects__16TXLocalContainerFv

	Boolean			FreeObjects(unsigned long type);				// ROM 0x002353b8 FreeObjects__16TXLocalContainerFUl
};


class TXPrivateContainer : public TXContainer
{
public:
					TXPrivateContainer(TXOffset start, long length, TXObjectRange* runs, TXObjectRange* rulers, TXChars* chars, TXFormatter* formatter);	// ROM 0x002354e8 __ct__18TXPrivateContainerFlT1P13TXObjectRangeT3P7TXCharsP11TXFormatter

	virtual NewtonErr AppendNewValue(unsigned long type, long count);	// ROM 0x00235570 AppendNewValue__18TXPrivateContainerFUll
	virtual NewtonErr WriteText(TXTextDescriptor* text);			// ROM 0x0023563c WriteText__18TXPrivateContainerFP16TXTextDescriptor
	virtual NewtonErr WriteObject(long index, TXAttrObject* object, long length, unsigned char* reference);	// ROM 0x00235658 WriteObject__18TXPrivateContainerFlP12TXAttrObjectT1PUc
	virtual NewtonErr EndValueWrite(void);							// ROM 0x002356c4 EndValueWrite__18TXPrivateContainerFv
	virtual NewtonErr EndWrite(Boolean failed, TXContainerImportInfo* info);	// ROM 0x00235710 EndWrite__18TXPrivateContainerFUcP21TXContainerImportInfo
	virtual NewtonErr FocusOnValue(unsigned long type);				// ROM 0x002358a4 FocusOnValue__18TXPrivateContainerFUl
	virtual NewtonErr GetCountObjects(long* count);					// ROM 0x0023590c GetCountObjects__18TXPrivateContainerFPl
	virtual NewtonErr GetValueSize(long* size);						// ROM 0x00235950 GetValueSize__18TXPrivateContainerFPl
	virtual void	AcquireTextDescriptor(TXTextDescriptor* text);	// ROM 0x002358f4 AcquireTextDescriptor__18TXPrivateContainerFP16TXTextDescriptor
	virtual NewtonErr ReadObject(long index, TXAttrObject** object, long* length, unsigned char* owned);	// ROM 0x00235964 ReadObject__18TXPrivateContainerFlPP12TXAttrObjectPlPUc

	TXOffset		fStart;			// +0x0c
	long			fLength;		// +0x10
	TXObjectRange*	fRuns;			// +0x14  the document's
	TXObjectRange*	fNewRuns;		// +0x18  gathered while writing
	TXObjectRange*	fRulers;		// +0x1c
	TXObjectRange*	fNewRulers;		// +0x20
	TXChars*		fChars;			// +0x24
	TXFormatter*	fFormatter;		// +0x28
	TXOffset		fReadPos;		// +0x2c
	long			fWritten;		// +0x30
	Boolean			fGraphics;		// +0x34  runs written by a picture's public type
};

#endif	/* __TXCONTAINER_H */
