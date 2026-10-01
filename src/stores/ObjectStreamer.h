/*
	File:		stores/ObjectStreamer.h

	Contains:	NSOF, the Newton Streamed Object Format: TObjectWriter
				streams an object graph to a CPipe and TObjectReader reads it
				back (packages' frames parts, the connection protocols, the
				clipboard).  The stream is a version byte (2) then one object,
				each object a tag byte and its parts: immediate (an xlong of
				the ref), character (a byte), unicodeCharacter (two bytes),
				binaryObject (xlong length, the class object, the data),
				array (xlong length, the class, the elements), plainArray (no
				class), frame (xlong count, the slot symbols, the values),
				symbol (xlong length, the name), string (xlong length in
				bytes, the UniChars), precedent (xlong index of an object
				already in the stream), nil, smallRect (four bytes: top,
				left, bottom, right), largeBinary (its class, compander, and the data).
				An xlong is one byte for 0..254, else 0xff and four bytes.
				The precedents give the graph its sharing and cycles: every
				pointer object is numbered as it is met (a frame and a large
				binary before their parts, a binary before its class).

				The writer's precedents are the store object writer's
				(TPrecedentsForWriting, StoreObject.h); a reader's are
				TPrecedentsForReading.  The ROM's layouts: TObjectWriter 0x28,
				TObjectReader 0x10.

	Reconstructed from the MP2x00 US ROM, 0x0035627c-0x00357fb0 (the small
	rects and precedents are StoreObject.h's, TPrecedentsVar ObjectHeap.h's).
*/

#ifndef __OBJECTSTREAMER_H
#define __OBJECTSTREAMER_H

#ifndef __STOREOBJECT_H
#include "StoreObject.h"
#endif
#ifndef __PIPES_H
#include "Pipes.h"
#endif
#ifndef __INTERPRETER_H
#include "Interpreter.h"
#endif

// the tags
enum
{
	kNSOFImmediate = 0, kNSOFCharacter, kNSOFUnicodeCharacter, kNSOFBinaryObject, kNSOFArray, kNSOFPlainArray,
	kNSOFFrame, kNSOFSymbol, kNSOFString, kNSOFPrecedent, kNSOFNil, kNSOFSmallRect, kNSOFLargeBinary
};
const UByte kNSOFVersion = 2;

void	LongToPipe(CPipe& pipe, long value);		// an xlong
long	LongFromPipe(CPipe& pipe);


class TObjectWriter
{
public:
				TObjectWriter(RefArg obj, CPipe& pipe, Boolean includeProto);	// includeProto: a frame's _proto slot goes too
				~TObjectWriter();

	void		SetCompressLargeBinaries(void);
	long		Size(void);						// the stream's bytes (Prescan)
	void		Write(void);					// the version byte and the object

	void		Prescan(void);
	void		Scan(void);

	TPrecedentsForWriting*	fPrecedents;	// +0x00
	TRefStack	fStack;						// +0x04  the objects on the way down
	RefStruct	fObject;					// +0x14  the object being scanned
	CPipe*		fPipe;						// +0x18
	long		fSize;						// +0x1c  counted by Prescan
	Boolean		fIncludeProto;				// +0x20
	Boolean		fCompressLargeBinaries;		// +0x24
};


class TObjectReader
{
public:
				TObjectReader(CPipe& pipe);
				TObjectReader(CPipe& pipe, RefArg storeOrSoup);	// the store large binaries go to
				~TObjectReader();

	void		SetPrecedentsForReading(void);
	void		SetAllowFunctions(Boolean allow);		// functions in the stream are refused when not
	Ref			Read(void);								// the version byte checked, the object read

	Ref			Scan(void);
	Ref			ReadImmediate(void);
	Ref			ReadCharacter(void);
	Ref			ReadUnicodeCharacter(void);
	Ref			ReadBinaryObject(UByte tag);
	Ref			ReadArray(UByte tag);
	Ref			ReadFrame(void);
	Ref			ReadSymbol(void);
	Ref			ReadPrecedent(void);
	Ref			ReadSmallRect(void);
	Ref			ReadLargeBinary(void);					// made on fStore

	TPrecedentsForReading*	fPrecedents;	// +0x00
	CPipe*		fPipe;						// +0x04
	TStore*		fStore;						// +0x08  for large binaries; nil: none can be read
	Boolean		fAllowFunctions;			// +0x0c
};

#endif	/* __OBJECTSTREAMER_H */
