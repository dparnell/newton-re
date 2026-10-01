/*
	File:		stores/ObjectStreamer.cpp

	Contains:	TObjectWriter and TObjectReader, the NSOF streamer
				(ObjectStreamer.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "ObjectStreamer.h"
#include "NarrowRef.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"
#include "ByteOrder.h"
#include "HostOrder.h"
#include "LargeBinaries.h"
#include "LargeObjects.h"
#include <stdlib.h>
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


// ROM 0x003563cc LongToPipe__FR5CPipel
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


// ROM 0x00356458 LongFromPipe__FR5CPipe
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

// ROM 0x0035627c __ct__13TObjectWriterFRC6RefVarR5CPipei
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


// ROM 0x0035634c __dt__13TObjectWriterFv
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


// ROM 0x003563c0 SetCompressLargeBinaries__13TObjectWriterFv
void
TObjectWriter::SetCompressLargeBinaries(void)
{
	fCompressLargeBinaries = true;
}


// ROM 0x0035649c Size__13TObjectWriterFv
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


// ROM 0x003564dc Write__13TObjectWriterFv
void
TObjectWriter::Write(void)
{
	fPrecedents->Reset();
	*fPipe << kNSOFVersion;
	Scan();
}


#define DESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Scan(); fObject = *--fStack.fTop; }
#define PRESCEND(part)		{ *fStack.fTop++ = fObject; fObject = (part); Prescan(); fObject = *--fStack.fTop; }

// ROM 0x0035650c Prescan__13TObjectWriterFv
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
			fSize += XLongSize(NarrowRef(ref, "NSOF"));		// NEWTON_NS64: the device's word (NarrowRef.h)
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
		if (IsLargeBinary(fObject))
		{
			// a large binary: the tag, the class, the compress flag and four
			// longs, the compander's name and parameters, and the stream
			LBData* lb = LargeBinaryData(fObject);
			TStoreWrapper* wrapper = lb->GetStore();
			fSize += 1;
			PRESCEND(ClassOf(fObject));
			fSize += 0x11;
			long nameLength = 0, parametersSize = 0;
			LOCompanderNameStrLen(wrapper->fStore, lb->fId, &nameLength);
			LOCompanderParameterSize(wrapper->fStore, lb->fId, &parametersSize);
			fSize += nameLength;
			fSize += parametersSize;
			fSize += LOSizeOfStream(wrapper->fStore, lb->fId, fCompressLargeBinaries);
			return;
		}
		// any other indirect binary: the unstreamable immediate
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


// ROM 0x00356b60 Scan__13TObjectWriterFv
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
			LongToPipe(pipe, NarrowRef(ref, "NSOF"));
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
		if (IsLargeBinary(fObject))
		{
			// a large binary: the tag and the class; whether it goes
			// compressed, the stream's size, the compander's name's length,
			// its parameters' size and a nought; the name and the
			// parameters; then the large object itself (LOWrite)
			//
			// ROM BUG kept: the name's and parameters' blocks are given back
			// only when something throws
			LBData* lb = LargeBinaryData(fObject);
			TStoreWrapper* wrapper = lb->GetStore();
			TStore* store = wrapper->fStore;
			PSSId id = lb->fId;
			long nameLength = 0, parametersSize = 0;
			LOCompanderNameStrLen(store, id, &nameLength);
			LOCompanderParameterSize(store, id, &parametersSize);
			long streamSize = LOSizeOfStream(store, id, fCompressLargeBinaries);
			char* name = nil;
			if (nameLength != 0)
			{
				name = (char*) malloc(nameLength + 1);
				if (name == nil)
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				LOCompanderName(store, id, name);
			}
			void* parameters = nil;
			if (parametersSize != 0)
			{
				parameters = malloc(parametersSize);
				if (parameters == nil)
				{
					free(name);
					Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
				}
				LOCompanderParameters(store, id, parameters);
			}
			newton_try
			{
				pipe << (UByte) kNSOFLargeBinary;
				DESCEND(ClassOf(fObject));
				pipe << (UByte) fCompressLargeBinaries;
				pipe << streamSize;
				pipe << nameLength;
				pipe << parametersSize;
				pipe << (long) 0;
				if (name != nil)
					pipe.WriteChunk(name, nameLength, false);
				if (parameters != nil)
					pipe.WriteChunk(parameters, parametersSize, false);
				LOWrite(&pipe, store, id, fCompressLargeBinaries, nil);
			}
			newton_catch_all
			{
				free(name);
				free(parameters);
				rethrow;
			}
			end_try;
			return;
		}
		// any other indirect binary cannot be streamed
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
	// DEVIATION: a string (of any string class), a real or a shape's
	// halfwords is kept in the host's order (frames/HostOrder.h), and goes
	// out as a MessagePad's bytes
	EHostOrder kind = HostOrderOf(fObject);
	if (kind != kROMOrder && !HostIsBigEndian())
	{
		char* bytes = new char[length];
		memcpy(bytes, data, length);
		SwapHostOrder(kind, bytes, length);
		pipe.WriteChunk(bytes, length, false);
		delete[] bytes;
	}
	else
		pipe.WriteChunk(data, length, false);
	UnlockRef(fObject);
}


/*------------------------------------------------------------------------------
	T O b j e c t R e a d e r
------------------------------------------------------------------------------*/

// ROM 0x00357368 __ct__13TObjectReaderFR5CPipe
TObjectReader::TObjectReader(CPipe& pipe)
{
	fStore = nil;
	fPipe = &pipe;
	fAllowFunctions = true;
	SetPrecedentsForReading();
}


// ROM 0x00357414 __ct__13TObjectReaderFR5CPipeRC6RefVar
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


// ROM 0x003573b4 __dt__13TObjectReaderFv
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


// ROM 0x003574d0 SetPrecedentsForReading__13TObjectReaderFv
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


// ROM 0x0035753c SetAllowFunctions__13TObjectReaderFUc
void
TObjectReader::SetAllowFunctions(Boolean allow)
{
	fAllowFunctions = allow;
}


// ROM 0x00357544 Read__13TObjectReaderFv
Ref
TObjectReader::Read(void)
{
	UByte version;
	*fPipe >> version;
	if (version != kNSOFVersion)
		Throw(exStoreError, (void*) kNSErrBadStreamVersion, nil);
	return Scan();
}


// ROM 0x00357e3c Scan__13TObjectReaderFv
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


// ROM 0x00357598 ReadImmediate__13TObjectReaderFv
// An xlong that is the ref itself (never a pointer).
Ref
TObjectReader::ReadImmediate(void)
{
	Ref ref = (Ref) (Long32) LongFromPipe(*fPipe);
	if (ISPTR(ref))
		Throw(exFrames, (void*) kNSErrBadStream, nil);
	return ref;
}


// ROM 0x003575e0 ReadCharacter__13TObjectReaderFv
Ref
TObjectReader::ReadCharacter(void)
{
	UByte c;
	*fPipe >> c;
	return MAKECHAR(c);
}


// ROM 0x00357614 ReadUnicodeCharacter__13TObjectReaderFv
Ref
TObjectReader::ReadUnicodeCharacter(void)
{
	UByte hi, lo;
	*fPipe >> hi >> lo;
	return MAKECHAR((UniChar) ((hi << 8) | lo));
}


// ROM 0x0035766c ReadBinaryObject__13TObjectReaderFUc
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
	// DEVIATION: into the host's order if it is kept so (a binary's class
	// was read before its data)
	SwapHostOrder(HostOrderOf(obj), BinaryData(obj), length);
	UnlockRef(obj);
	return obj;
}


// ROM 0x0035776c ReadArray__13TObjectReaderFUc
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


// ROM 0x00357880 ReadFrame__13TObjectReaderFv
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


// ROM 0x003579d8 ReadSymbol__13TObjectReaderFv
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


// ROM 0x00357a94 ReadPrecedent__13TObjectReaderFv
Ref
TObjectReader::ReadPrecedent(void)
{
	long index = LongFromPipe(*fPipe);
	return fPrecedents->Get(index);
}


// ROM 0x00357ac0 ReadSmallRect__13TObjectReaderFv
Ref
TObjectReader::ReadSmallRect(void)
{
	long packed;
	*fPipe >> packed;
	RefVar obj(UnpackSmallRect(packed));
	fPrecedents->Append(obj);
	return obj;
}


// ROM 0x00357b78 ReadLargeBinary__13TObjectReaderFv
// A large binary: its place among the precedents kept (nil for now), the
// class, the compress flag, the stream size, the compander's name's
// length, its parameters' size and a reserved long, the name and the
// parameters, then the large object made on fStore from the stream
// (kNSErrNoStoreForLargeBinary without a store), mapped and wrapped - an
// ephemeral until an entry takes it.
//
// ROM BUG kept: the name's and parameters' blocks are given back only
// when something throws.  (A stream written compressed is made again by
// CreateLargeObject's fromCompressed branch, LODefCreateFromComp.)
Ref
TObjectReader::ReadLargeBinary(void)
{
	RefVar obj;
	if (fStore == nil)
		Throw(exStoreError, (void*) kNSErrNoStoreForLargeBinary, nil);
	long precedent = fPrecedents->Append(RefVar(NILREF));
	RefVar cls(Scan());
	UByte compressed;
	long streamSize, nameLength, parametersSize, reserved;
	*fPipe >> compressed;
	*fPipe >> streamSize;
	*fPipe >> nameLength;
	*fPipe >> parametersSize;
	*fPipe >> reserved;
	char* name = nil;
	void* parameters = nil;
	if (nameLength != 0 && (name = (char*) malloc(nameLength + 1)) == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	if (parametersSize != 0 && (parameters = malloc(parametersSize)) == nil)
	{
		free(name);
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	}
	NewtonErr err;
	ULong id;
	newton_try
	{
		Boolean eof;
		if (nameLength != 0)
		{
			fPipe->ReadChunk(name, nameLength, eof);
			name[nameLength] = 0;
		}
		if (parametersSize != 0)
			fPipe->ReadChunk(parameters, parametersSize, eof);
		err = CreateLargeObject(&id, fStore, fPipe, streamSize, false, name, parameters, parametersSize, nil, compressed);
	}
	newton_catch_all
	{
		free(name);
		free(parameters);
		rethrow;
	}
	end_try;
	if (err != noErr)
		Throw(exFrames, (void*) (Long) err, nil);
	ULong address;
	if ((err = MapLargeObject(&address, fStore, id, false)) != noErr)
	{
		DeleteLargeObject(fStore, id);
		Throw(exFrames, (void*) (Long) err, nil);
	}
	obj = WrapLargeObject(fStore, cls, id, address);
	fPrecedents->Replace(precedent, obj);
	return obj;
}
