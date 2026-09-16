/*
	File:		stores/ObjectStreamer.cpp

	Contains:	TObjectWriter and TObjectReader, the NSOF streamer
				(ObjectStreamer.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "ObjectStreamer.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"
#include <string.h>

extern const ExceptionName exStoreError;

// the errors (the names are ours; see NSErrors.h)
#define kNSErrBadStreamVersion		(ERRBASE_FRAMES - 6)		// TObjectReader::Read: not version 2 (0xffff447a)
#define kNSErrNoStoreForLargeBinary	(ERRBASE_FRAMES - 17)		// a large binary in a stream read without a store (0xffff446f)
#define kNSErrBadStream				(ERRBASE_FRAMES - 223)		// an unknown tag, a pointer as an immediate, a symbol over 255 bytes (0xffff43a1)
#define kNSErrFunctionInStream		(ERRBASE_FRAMES - 224)		// a function read when functions are not allowed (0xffff43a0)

// what the writer streams for a large binary it cannot (one whose procs
// are not the large binaries'): the special immediate 0x52
const Ref kUnstreamableRef = MAKEIMMED(kImmedSpecial, 5);

// the bytes an xlong of the value takes
static inline long XLongSize(long value)		{ return (value >= 0 && value < 0xff) ? 1 : 5; }


// ROM 0x0032b230 LongToPipe__FR5CPipel
// An xlong: one byte for 0..254, else 0xff and the four bytes.
void
LongToPipe(CPipe& pipe, long value)
{
	if (value >= 0 && value < 0xff)
	{
		UByte b = (UByte) value;
		pipe.WriteChunk(&b, 1, false);
	}
	else
	{
		pipe << (UByte) 0xff;
		UByte bytes[4];
		PutBigEndianWord(bytes, (unsigned int) value);
		pipe.WriteChunk(bytes, 4, false);
	}
}


// ROM 0x0032b2bc LongFromPipe__FR5CPipe
long
LongFromPipe(CPipe& pipe)
{
	UByte b;
	pipe >> b;
	if (b == 0xff)
	{
		long value;
		pipe >> value;
		return value;
	}
	return b;
}


/*------------------------------------------------------------------------------
	T O b j e c t W r i t e r
------------------------------------------------------------------------------*/

// ROM 0x0032b0e0 __ct__13TObjectWriterFRC6RefVarR5CPipei
// The precedents are the shared ones when free, else a new set.
TObjectWriter::TObjectWriter(RefArg obj, CPipe& pipe, Boolean includeProto)
{
	fObject = obj;
	fPipe = &pipe;
	fSize = 0;
	fIncludeProto = includeProto;
	fCompressLargeBinaries = false;
	if (!gPrecedentsForWritingUsed)
	{
		if (gPrecedentsForWriting == nil)
			gPrecedentsForWriting = new TPrecedentsForWriting;
		fPrecedents = gPrecedentsForWriting;
		gPrecedentsForWritingUsed = true;
	}
	else
	{
		fPrecedents = new TPrecedentsForWriting;
		if (fPrecedents == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
}


// ROM 0x0032b1b0 __dt__13TObjectWriterFv
TObjectWriter::~TObjectWriter()
{
	if (fPrecedents == gPrecedentsForWriting)
	{
		gPrecedentsForWritingUsed = false;
		fPrecedents->Reset();
	}
	else if (fPrecedents != nil)
		delete fPrecedents;
}


// ROM 0x0032b224 SetCompressLargeBinaries__13TObjectWriterFv
void
TObjectWriter::SetCompressLargeBinaries(void)
{
	fCompressLargeBinaries = true;
}


// ROM 0x0032b300 Size__13TObjectWriterFv
// The stream's size in bytes, counted once (Prescan; the version byte
// too).
long
TObjectWriter::Size(void)
{
	if (fSize == 0)
	{
		fPrecedents->Reset();
		Prescan();
		fSize++;
	}
	return fSize;
}


// ROM 0x0032b340 Write__13TObjectWriterFv
void
TObjectWriter::Write(void)
{
	fPrecedents->Reset();
	*fPipe << kNSOFVersion;
	Scan();
}


#define DESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Scan(); fObject = *--fStack.fTop; }
#define PRESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Prescan(); fObject = *--fStack.fTop; }

// ROM 0x0032b370 Prescan__13TObjectWriterFv
// The bytes fObject will take in the stream added to fSize, every pointer
// object entered as a precedent so that its later occurrences count as
// one.
void
TObjectWriter::Prescan(void)
{
	Ref ref = fObject;
	if (!ISPTR(ref))
	{
		if (ref == NILREF)
			fSize += 1;
		else if (ISCHAR(ref))
		{
			fSize += 1;
			fSize += (RCHAR(ref) > 0xff) ? 2 : 1;
		}
		else
		{
			fSize += 1;
			fSize += XLongSize(ref);
		}
		return;
	}
	long precedent = fPrecedents->Find(fObject);
	if (precedent != -1)
	{
		fSize += 1 + XLongSize(precedent);
		return;
	}
	fPrecedents->Append(fObject);
	ULong flags = ObjectFlags(ref);
	if ((flags & kObjSlotted) != 0)
	{
		fSize += 1;
		if ((flags & kObjFrame) == 0)
		{
			// an array
			long length = Length(ref);
			fSize += XLongSize(length);
			if (!EQRef(ObjClass(OBJ(ref)), RSSYMarray))
				PRESCEND(ObjClass(OBJ((Ref) fObject)));
			for (long i = 0; i < length; i++)
				PRESCEND(GetArraySlotRef(fObject, i));
			return;
		}
		if (PackSmallRect(ref, nil))
		{
			fSize += 4;
			return;
		}
		// a frame: the slot symbols (the _proto slot left out unless wanted), then the values
		long length = Length(ref);
		long count = length;
		if (!fIncludeProto && FrameHasSlotRef(ref, RSSYM_proto))
			count--;
		fSize += XLongSize(count);
		long protoIndex = -1;
		*fStack.fTop++ = fObject;
		{
			RefVar map(ObjClass(OBJ(ref)));
			for (long i = 0; i < length; i++)
			{
				fObject = GetTag(map, i, nil);
				if (!fIncludeProto && EQRef(fObject, RSSYM_proto))
					protoIndex = i;
				else
					Prescan();
			}
		}
		fObject = *--fStack.fTop;
		for (long i = 0; i < length; i++)
			if (i != protoIndex)
				PRESCEND(GetArraySlotRef(fObject, i));
		return;
	}
	if ((flags & 3) == kObjFrame)
	{
		// a large binary: NOT YET RECONSTRUCTED (the tag, the class, the
		// compress flag, three sizes and a reserved long, the compander's
		// name and parameters, the stream) - counted as the unstreamable
		// immediate the ROM writes for a large binary that is not one
		fSize += 1;
		fSize += 1;
		return;
	}
	fSize += 1;
	if (IsSymbol(ref))
	{
		long length = strlen(SymbolName(ref));
		fSize += XLongSize(length) + length;
		return;
	}
	long length = Length(ref);
	fSize += XLongSize(length);
	if (!EQRef(ClassOf(fObject), RSSYMstring))
		PRESCEND(ObjClass(OBJ((Ref) fObject)));
	fSize += length;
}


// ROM 0x0032b9c4 Scan__13TObjectWriterFv
// fObject written: its tag and parts, its pointer parts recursively (the
// stack keeps the way down for the collector).
void
TObjectWriter::Scan(void)
{
	Ref ref = fObject;
	CPipe& pipe = *fPipe;
	if (!ISPTR(ref))
	{
		if (ref == NILREF)
			pipe << (UByte) kNSOFNil;
		else if (ISCHAR(ref))
		{
			UniChar c = RCHAR(ref);
			if (c < 0x100)
				pipe << (UByte) kNSOFCharacter;
			else
				pipe << (UByte) kNSOFUnicodeCharacter << (UByte) (c >> 8);
			pipe << (UByte) c;
		}
		else
		{
			pipe << (UByte) kNSOFImmediate;
			LongToPipe(pipe, ref);
		}
		return;
	}
	long precedent = fPrecedents->Find(fObject);
	if (precedent != -1)
	{
		pipe << (UByte) kNSOFPrecedent;
		LongToPipe(pipe, precedent);
		return;
	}
	fPrecedents->Append(fObject);
	ULong flags = ObjectFlags(ref);
	if ((flags & kObjSlotted) != 0)
	{
		if ((flags & kObjFrame) == 0)
		{
			// an array
			Boolean plain = EQRef(ObjClass(OBJ(ref)), RSSYMarray);
			pipe << (UByte) (plain ? kNSOFPlainArray : kNSOFArray);
			long length = Length(ref);
			LongToPipe(pipe, length);
			if (!plain)
				DESCEND(ObjClass(OBJ((Ref) fObject)));
			for (long i = 0; i < length; i++)
				DESCEND(GetArraySlotRef(fObject, i));
			return;
		}
		long packed;
		if (PackSmallRect(ref, &packed))
		{
			pipe << (UByte) kNSOFSmallRect;
			pipe << packed;
			return;
		}
		// a frame
		pipe << (UByte) kNSOFFrame;
		long length = Length(ref);
		long count = length;
		if (!fIncludeProto && FrameHasSlotRef(ref, RSSYM_proto))
			count--;
		LongToPipe(pipe, count);
		long protoIndex = -1;
		*fStack.fTop++ = fObject;
		{
			RefVar map(ObjClass(OBJ(ref)));
			for (long i = 0; i < length; i++)
			{
				fObject = GetTag(map, i, nil);
				if (!fIncludeProto && EQRef(fObject, RSSYM_proto))
					protoIndex = i;
				else
					Scan();
			}
		}
		fObject = *--fStack.fTop;
		for (long i = 0; i < length; i++)
			if (i != protoIndex)
				DESCEND(GetArraySlotRef(fObject, i));
		return;
	}
	if ((flags & 3) == kObjFrame)
	{
		// a large binary: NOT YET RECONSTRUCTED (kNSOFLargeBinary, the class,
		// fCompressLargeBinaries, LOSizeOfStream, the compander's name and
		// parameters, LOWrite); the ROM writes this for one that is not a
		// large binary of the store's
		pipe << (UByte) kNSOFImmediate;
		LongToPipe(pipe, kUnstreamableRef);
		return;
	}
	if (IsSymbol(ref))
	{
		pipe << (UByte) kNSOFSymbol;
		const char* name = SymbolName(ref);
		long length = strlen(name);
		LongToPipe(pipe, length);
		pipe.WriteChunk(name, length, false);
		return;
	}
	// a binary: the class (not a string's), then the data - a string's
	// UniChars high byte first
	Boolean isString = EQRef(ClassOf(fObject), RSSYMstring);
	pipe << (UByte) (isString ? kNSOFString : kNSOFBinaryObject);
	long length = Length(ref);
	LongToPipe(pipe, length);
	if (!isString)
		DESCEND(ObjClass(OBJ((Ref) fObject)));
	LockRef(fObject);
	char* data = BinaryData(fObject);
	if (isString && !HostIsBigEndian())
	{
		char* text = new char[length];
		memcpy(text, data, length);
		SwapUniChars(text, length / 2);
		pipe.WriteChunk(text, length, false);
		delete[] text;
	}
	else
		pipe.WriteChunk(data, length, false);
	UnlockRef(fObject);
}


/*------------------------------------------------------------------------------
	T O b j e c t R e a d e r
------------------------------------------------------------------------------*/

// ROM 0x0032c1cc __ct__13TObjectReaderFR5CPipe
TObjectReader::TObjectReader(CPipe& pipe)
{
	fStore = nil;
	fPipe = &pipe;
	fAllowFunctions = true;
	SetPrecedentsForReading();
}


// ROM 0x0032c278 __ct__13TObjectReaderFR5CPipeRC6RefVar
// With the store (a store or soup frame) large binaries are created on.
TObjectReader::TObjectReader(CPipe& pipe, RefArg storeOrSoup)
{
	fStore = nil;
	fPipe = &pipe;
	fAllowFunctions = true;
	SetPrecedentsForReading();
	if ((Ref) storeOrSoup != NILREF)
	{
		Ref wrapper = GetFrameSlotRef(storeOrSoup, RSSYMtstore);
		if (wrapper == NILREF)
			wrapper = GetFrameSlotRef(storeOrSoup, RSSYMstore);
		if (wrapper != NILREF)
			fStore = ((TStoreWrapper*) wrapper)->Store();
	}
}


// ROM 0x0032c218 __dt__13TObjectReaderFv
TObjectReader::~TObjectReader()
{
	if (fPrecedents == gPrecedentsForReading)
	{
		gPrecedentsForReadingUsed = false;
		fPrecedents->Reset();
	}
	else if (fPrecedents != nil)
		delete fPrecedents;
}


// ROM 0x0032c334 SetPrecedentsForReading__13TObjectReaderFv
// The shared precedents when free, else a new set.
void
TObjectReader::SetPrecedentsForReading(void)
{
	if (!gPrecedentsForReadingUsed)
	{
		if (gPrecedentsForReading == nil)
			gPrecedentsForReading = new TPrecedentsForReading;
		fPrecedents = gPrecedentsForReading;
		gPrecedentsForReadingUsed = true;
	}
	else
	{
		fPrecedents = new TPrecedentsForReading;
		if (fPrecedents == nil)
			Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
}


// ROM 0x0032c3a0 SetAllowFunctions__13TObjectReaderFUc
void
TObjectReader::SetAllowFunctions(Boolean allow)
{
	fAllowFunctions = allow;
}


// ROM 0x0032c3a8 Read__13TObjectReaderFv
Ref
TObjectReader::Read(void)
{
	UByte version;
	*fPipe >> version;
	if (version != kNSOFVersion)
		Throw(exStoreError, (void*) kNSErrBadStreamVersion, nil);
	return Scan();
}


// ROM 0x0032cca0 Scan__13TObjectReaderFv
// One object by its tag; a function refused when not allowed.
Ref
TObjectReader::Scan(void)
{
	UByte tag;
	*fPipe >> tag;
	Ref obj;
	switch (tag)
	{
	case kNSOFImmediate:		obj = ReadImmediate(); break;
	case kNSOFCharacter:		obj = ReadCharacter(); break;
	case kNSOFUnicodeCharacter:	obj = ReadUnicodeCharacter(); break;
	case kNSOFBinaryObject:
	case kNSOFString:			obj = ReadBinaryObject(tag); break;
	case kNSOFArray:
	case kNSOFPlainArray:		obj = ReadArray(tag); break;
	case kNSOFFrame:			obj = ReadFrame(); break;
	case kNSOFSymbol:			obj = ReadSymbol(); break;
	case kNSOFPrecedent:		obj = ReadPrecedent(); break;
	case kNSOFNil:				obj = NILREF; break;
	case kNSOFSmallRect:		obj = ReadSmallRect(); break;
	case kNSOFLargeBinary:		obj = ReadLargeBinary(); break;
	default:
		Throw(exFrames, (void*) kNSErrBadStream, nil);
		obj = NILREF;
	}
	if (!fAllowFunctions)
	{
		RefVar theObj(obj);
		if (IsFunction(theObj))
			Throw(exFrames, (void*) kNSErrFunctionInStream, nil);
		obj = theObj;
	}
	return obj;
}


// ROM 0x0032c3fc ReadImmediate__13TObjectReaderFv
// An xlong that is the ref itself (never a pointer).
Ref
TObjectReader::ReadImmediate(void)
{
	Ref ref = (Ref) (Long32) LongFromPipe(*fPipe);
	if (ISPTR(ref))
		Throw(exFrames, (void*) kNSErrBadStream, nil);
	return ref;
}


// ROM 0x0032c444 ReadCharacter__13TObjectReaderFv
Ref
TObjectReader::ReadCharacter(void)
{
	UByte c;
	*fPipe >> c;
	return MAKECHAR(c);
}


// ROM 0x0032c478 ReadUnicodeCharacter__13TObjectReaderFv
Ref
TObjectReader::ReadUnicodeCharacter(void)
{
	UByte hi, lo;
	*fPipe >> hi >> lo;
	return MAKECHAR((UniChar) ((hi << 8) | lo));
}


// ROM 0x0032c4d0 ReadBinaryObject__13TObjectReaderFUc
// The length, the class (a string's is 'string), then the data; the
// object is a precedent before its class is read.
Ref
TObjectReader::ReadBinaryObject(UByte tag)
{
	long length = LongFromPipe(*fPipe);
	RefVar obj;
	if (tag == kNSOFString)
		obj = AllocateBinary(RSSYMstring, length);
	else
		obj = AllocateBinary(RefVar(NILREF), length);
	fPrecedents->Append(obj);
	if (tag == kNSOFBinaryObject)
	{
		RefVar theClass(Scan());
		ObjClass(OBJ((Ref) obj)) = theClass;
	}
	LockRef(obj);
	long count = length;
	Boolean eof;
	fPipe->ReadChunk(BinaryData(obj), count, eof);
	if (tag == kNSOFString)
		SwapUniChars(BinaryData(obj), length / 2);
	UnlockRef(obj);
	return obj;
}


// ROM 0x0032c5d0 ReadArray__13TObjectReaderFUc
Ref
TObjectReader::ReadArray(UByte tag)
{
	long length = LongFromPipe(*fPipe);
	RefVar obj(AllocateArray(RefVar(NILREF), length));
	fPrecedents->Append(obj);
	if (tag == kNSOFPlainArray)
		ObjClass(OBJ((Ref) obj)) = RSSYMarray;
	else
	{
		RefVar theClass(Scan());
		ObjClass(OBJ((Ref) obj)) = theClass;
	}
	RefVar element;
	for (long i = 0; i < length; i++)
	{
		element = Scan();
		SetArraySlotRef(obj, i, element);
	}
	return obj;
}


// ROM 0x0032c6e4 ReadFrame__13TObjectReaderFv
// The slot symbols into a map, the frame made over it (its precedent
// place reserved first), then the values.
Ref
TObjectReader::ReadFrame(void)
{
	long length = LongFromPipe(*fPipe);
	long precedent = fPrecedents->Append(RefVar(NILREF));
	RefVar obj(AllocateArray(RSSYMarray, length));
	RefVar item;
	for (long i = 0; i < length; i++)
	{
		item = Scan();
		SetArraySlotRef(obj, i, item);
	}
	obj = AllocateMapWithTags(RefVar(NILREF), obj);
	obj = AllocateFrameWithMap(obj);
	fPrecedents->Replace(precedent, obj);
	for (long i = 0; i < length; i++)
	{
		item = Scan();
		SetArraySlotRef(obj, i, item);
	}
	return obj;
}


// ROM 0x0032c83c ReadSymbol__13TObjectReaderFv
// The name (at most 255 bytes), interned.
Ref
TObjectReader::ReadSymbol(void)
{
	long length = LongFromPipe(*fPipe);
	if (length < 0 || length > 0xff)
		Throw(exFrames, (void*) kNSErrBadStream, nil);
	char name[256];
	long count = length;
	Boolean eof;
	fPipe->ReadChunk(name, count, eof);
	name[length] = 0;
	RefVar sym(Intern(name));
	fPrecedents->Append(sym);
	return sym;
}


// ROM 0x0032c8f8 ReadPrecedent__13TObjectReaderFv
Ref
TObjectReader::ReadPrecedent(void)
{
	long index = LongFromPipe(*fPipe);
	return fPrecedents->Get(index);
}


// ROM 0x0032c924 ReadSmallRect__13TObjectReaderFv
Ref
TObjectReader::ReadSmallRect(void)
{
	long packed;
	*fPipe >> packed;
	RefVar obj(UnpackSmallRect(packed));
	fPrecedents->Append(obj);
	return obj;
}


// ROM 0x0032c9dc ReadLargeBinary__13TObjectReaderFv
// NOT YET RECONSTRUCTED: the class, the compress flag, the stream size,
// the compander name and parameter sizes, a reserved long, the name and
// parameters, then CreateLargeObject from the stream on fStore
// (kNSErrNoStoreForLargeBinary without one), mapped and wrapped.
Ref
TObjectReader::ReadLargeBinary(void)
{
	if (fStore == nil)
		Throw(exStoreError, (void*) kNSErrNoStoreForLargeBinary, nil);
	Throw(exInterpreter, (void*) kNSErrNativeNotReconstructed, nil);
	return NILREF;
}
