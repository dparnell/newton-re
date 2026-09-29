/*
	File:		text/TXContainer.cpp

	Contains:	The containers (TXContainer.h).

	Reconstructed from the MP2x00 US ROM (0x00234804-0x00235b00); each
	function cites its origin.
*/

#include "TXContainer.h"
#include "TXStream.h"
#include "TXFormatter.h"
#include "TXUtilities.h"
#include "ByteOrder.h"
#include "OSErrors.h"

#include <string.h>


// ROM 0x002348c4 __ct__21TXContainerImportInfoFUc
TXContainerImportInfo::TXContainerImportInfo(unsigned char types)
{
	fTypes = types;
	fTextCount = 0;
	fRunCount = 0;
	fRulerCount = 0;
}


// ROM 0x00234804 __ct__11TXContainerFP8TXStream
TXContainer::TXContainer(TXStream* stream)
{
	fStream = stream;
	fType = 0;
}


// ROM 0x00235ae8 BeginWrite__11TXContainerFv
NewtonErr
TXContainer::BeginWrite(void)
{
	return noErr;
}


// ROM 0x00235704 AppendNewValue__11TXContainerFUll
NewtonErr
TXContainer::AppendNewValue(unsigned long type, long /*count*/)
{
	fType = type;
	return noErr;
}


// ROM 0x00234888 WriteText__11TXContainerFP16TXTextDescriptor
// The characters copied onto the stream.
NewtonErr
TXContainer::WriteText(TXTextDescriptor* text)
{
	long count = text->fCount;
	TXTextDescriptor to;
	to.Set(fStream, count);
	return text->CopyTo(&to, count);
}


// ROM 0x00235af0 EndValueWrite__11TXContainerFv
NewtonErr
TXContainer::EndValueWrite(void)
{
	return noErr;
}


// ROM 0x00235af8 EndWrite__11TXContainerFUcP21TXContainerImportInfo
NewtonErr
TXContainer::EndWrite(Boolean /*failed*/, TXContainerImportInfo* /*info*/)
{
	return noErr;
}


// ROM 0x00235274 FocusOnValue__11TXContainerFUl
NewtonErr
TXContainer::FocusOnValue(unsigned long type)
{
	fType = type;
	return noErr;
}


// ROM 0x00234844 AcquireTextDescriptor__11TXContainerFP16TXTextDescriptor
// The characters of the value in hand, read off the stream.
void
TXContainer::AcquireTextDescriptor(TXTextDescriptor* text)
{
	long size;
	GetValueSize(&size);
	text->Set(fStream, (unsigned long) size >> 1);
}


// ROM 0x00234884 ReleaseTextDescriptor__11TXContainerFP16TXTextDescriptor
void
TXContainer::ReleaseTextDescriptor(TXTextDescriptor* /*text*/)
{ }


// ROM 0x00235a9c ConvertValueType__11TXContainerFUl
unsigned long
TXContainer::ConvertValueType(unsigned long type)
{
	return type;
}


// ROM 0x00234e60 SetStream__11TXContainerFP8TXStream
void
TXContainer::SetStream(TXStream* stream)
{
	fStream = stream;
}


// ROM 0x00235ab0 ConvertAndFocusOnValue__11TXContainerFUl
NewtonErr
TXContainer::ConvertAndFocusOnValue(unsigned long type)
{
	unsigned long converted = ConvertValueType(type);
	if (converted == 0)
		return kTXErrNoValue;
	return FocusOnValue(converted);
}


// ROM 0x00234904 GetAvailTypes__11TXContainerFv
// Runs (or a picture's run: text and runs both, since it brings its own
// character), rulers, text.
unsigned char
TXContainer::GetAvailTypes(void)
{
	unsigned char types = 0;
	if (ConvertAndFocusOnValue(kTXValueRuns) == noErr)
		types = kTXImportRuns;
	else
	{
		long n = gRegisteredRuns->GetCount();
		for (long i = 0; i < n; i++)
		{
			unsigned long type = gRegisteredRuns->GetIndObject(i)->GetPublicType();
			if (type != 0 && ConvertAndFocusOnValue(type) == noErr)
			{
				types = kTXImportText | kTXImportRuns;
				break;
			}
		}
	}
	if (ConvertAndFocusOnValue(kTXValueRulers) == noErr)
		types |= kTXImportRulers;
	if (ConvertAndFocusOnValue(kTXValueText) == noErr)
		types |= kTXImportText;
	return types;
}


// ROM 0x002349d0 Import__11TXContainerFP11TXContainerP21TXContainerImportInfo
// The values the info asks for copied from `source`: the runs (or, when
// it has none by that name, a picture's by its public type - and then no
// text, the picture bringing its own character), the rulers, the text.
// A value the source does not have is skipped; any other error ends the
// write as failed.  The info comes back with what was taken.
NewtonErr
TXContainer::Import(TXContainer* source, TXContainerImportInfo* info)
{
	NewtonErr err = BeginWrite();
	if (err != noErr)
		return err;
	unsigned char taken = 0;
	if (info->fTypes & kTXImportRuns)
	{
		err = ImportObjects(source, kTXValueRuns, &info->fRunCount);
		if (err == kTXErrNoValue)
		{
			long n = gRegisteredRuns->GetCount();
			for (long i = 0; i < n; i++)
			{
				unsigned long type = gRegisteredRuns->GetIndObject(i)->GetPublicType();
				if (type != 0)
				{
					err = ImportObjects(source, type, &info->fRunCount);
					if (err == noErr)
					{
						info->fTypes &= ~kTXImportText;
						break;
					}
					if (err != kTXErrNoValue)
						goto failed;
				}
			}
			if (err == noErr)
				taken = kTXImportRuns;
		}
		else if (err == noErr)
			taken = kTXImportRuns;
		else
			goto failed;
	}
	if (info->fTypes & kTXImportRulers)
	{
		err = ImportObjects(source, kTXValueRulers, &info->fRulerCount);
		if (err == noErr)
			taken |= kTXImportRulers;
		else if (err != kTXErrNoValue)
			goto failed;
	}
	if (info->fTypes & kTXImportText)
	{
		err = ImportText(source, &info->fTextCount);
		if (err == noErr)
			taken |= kTXImportText;
		else if (err != kTXErrNoValue)
			goto failed;
	}
	info->fTypes = taken;
	return EndWrite(false, info);

failed:
	info->fTypes = taken;
	EndWrite(true, info);
	return err;
}


// ROM 0x00234b7c ImportText__11TXContainerFP11TXContainerPl
NewtonErr
TXContainer::ImportText(TXContainer* source, long* count)
{
	NewtonErr err = source->ConvertAndFocusOnValue(kTXValueText);
	if (err != noErr)
		return err;
	err = source->GetValueSize(count);
	if (err != noErr)
		return err;
	*count = (unsigned long) *count >> 1;
	unsigned long type = ConvertValueType(kTXValueText);
	if (type == 0)
		return kTXErrNoValue;
	err = AppendNewValue(type, *count);
	if (err != noErr)
		return err;
	TXTextDescriptor text;
	source->AcquireTextDescriptor(&text);
	err = WriteText(&text);
	source->ReleaseTextDescriptor(&text);
	if (err == noErr)
		err = EndValueWrite();
	return err;
}


// ROM 0x00234c80 ImportObjects__11TXContainerFP11TXContainerUlPl
// Read from the assembly.  Each object read from the source and written
// here - a reference taken to it unless the source hands it over, and an
// object handed over given back when the write did not keep it.
NewtonErr
TXContainer::ImportObjects(TXContainer* source, unsigned long type, long* count)
{
	NewtonErr err = source->ConvertAndFocusOnValue(type);
	if (err != noErr)
		return err;
	err = source->GetCountObjects(count);
	if (err != noErr)
		return err;
	long n = *count;
	unsigned long mine = ConvertValueType(type);
	if (mine == 0)
		return kTXErrNoValue;
	err = AppendNewValue(mine, n);
	if (err != noErr)
		return err;
	for (long i = 0; i < n; i++)
	{
		TXAttrObject* object;
		long length;
		unsigned char owned;
		err = source->ReadObject(i, &object, &length, &owned);
		if (err != noErr)
			return err;
		unsigned char reference = (owned == 0);
		err = WriteObject(i, object, length, &reference);
		if (owned != 0 && (err != noErr || reference != 0))
			object->Free();
		if (err != noErr)
			return err;
	}
	return EndValueWrite();
}


/*------------------------------------------------------------------------------
	T X S t d C o n t a i n e r
------------------------------------------------------------------------------*/

// ROM 0x00234de8 __ct__14TXStdContainerFP8TXStream
TXStdContainer::TXStdContainer(TXStream* stream)
	: TXContainer(stream)
{
	fBase = (stream == nil) ? 0 : stream->GetPosition();
	fCount = -1;
	fCurrent = -1;
	memset(fValues, 0, sizeof(fValues));
}


// ROM 0x00234e68 SetStream__14TXStdContainerFP8TXStream
void
TXStdContainer::SetStream(TXStream* stream)
{
	TXContainer::SetStream(stream);
	fBase = stream->GetPosition();
}


// (host) The table as it goes on the stream: the count, then three
// entries of (type, count, size), every word big-endian.
static void
EncodeValues(const TXContainerValue* values, long n, unsigned char* out)
{
	for (long i = 0; i < n; i++)
	{
		PutBigEndianWord(out + i * 12, (unsigned int) values[i].fType);
		PutBigEndianWord(out + i * 12 + 4, (unsigned int) values[i].fCount);
		PutBigEndianWord(out + i * 12 + 8, (unsigned int) values[i].fSize);
	}
}


// ROM 0x00234e98 BeginWrite__14TXStdContainerFv
// Room made for the count and the table, the values to follow them.
NewtonErr
TXStdContainer::BeginWrite(void)
{
	if (fStream == nil)
		return kTXErrNoValue;
	fCount = 0;
	for (long i = 0; i < kTXContainerValuesMax; i++)
		fValues[i].fType = 0;
	unsigned char word[4] = { 0, 0, 0, 0 };
	NewtonErr err = fStream->WriteBytes(word, 4);
	if (err != noErr)
		return err;
	unsigned char table[kTXContainerValuesMax * 12];
	EncodeValues(fValues, kTXContainerValuesMax, table);
	return fStream->WriteBytes(table, sizeof(table));
}


// ROM 0x00234f20 EndWrite__14TXStdContainerFUcP21TXContainerImportInfo
// The count and the table written into the room made for them.
NewtonErr
TXStdContainer::EndWrite(Boolean failed, TXContainerImportInfo* /*info*/)
{
	if (failed)
		return noErr;
	return WriteTable();
}


NewtonErr
TXStdContainer::WriteTable(void)
{
	long end = fStream->GetPosition();
	fStream->SetPosition(fBase);
	unsigned char word[4];
	PutBigEndianWord(word, (unsigned int) fCount);
	NewtonErr err = fStream->WriteBytes(word, 4);
	if (err == noErr)
	{
		unsigned char table[kTXContainerValuesMax * 12];
		EncodeValues(fValues, fCount, table);
		fStream->WriteBytes(table, fCount * 12);
	}
	fStream->SetPosition(end);
	return err;
}


// ROM 0x00234fd0 AppendNewValue__14TXStdContainerFUll
NewtonErr
TXStdContainer::AppendNewValue(unsigned long type, long count)
{
	TXContainer::AppendNewValue(type, count);
	if (fStream == nil)
		return kTXErrNoValue;
	TXContainerValue* value = &fValues[fCount];
	value->fCount = 0;
	value->fType = type;
	value->fSize = 0;
	fCount = fCount + 1;
	return noErr;
}


// ROM 0x00235028 WriteObject__14TXStdContainerFlP12TXAttrObjectT1PUc
// The object's data (the subclass's) and its size counted into the value.
NewtonErr
TXStdContainer::WriteObject(long /*index*/, TXAttrObject* object, long length, unsigned char* reference)
{
	long start = fStream->GetPosition();
	NewtonErr err = WriteObjectData(object, length, reference);
	if (err == noErr)
	{
		TXContainerValue* value = &fValues[fCount - 1];
		value->fCount = value->fCount + 1;
		value->fSize = (fStream->GetPosition() - start) + value->fSize;
	}
	return err;
}


// ROM 0x002350c0 WriteText__14TXStdContainerFP16TXTextDescriptor
NewtonErr
TXStdContainer::WriteText(TXTextDescriptor* text)
{
	NewtonErr err = TXContainer::WriteText(text);
	if (err != noErr)
		return err;
	fValues[fCount - 1].fSize = fValues[fCount - 1].fSize + text->fCount * 2;
	return noErr;
}


// ROM 0x00235100 FocusOnValue__14TXStdContainerFUl
// The table read (once), and the stream put at the start of the value.
NewtonErr
TXStdContainer::FocusOnValue(unsigned long type)
{
	TXContainer::FocusOnValue(type);
	if (fStream != nil)
	{
		if (fCount < 0)
		{
			fStream->SetPosition(fBase);
			unsigned char word[4];
			NewtonErr err = fStream->ReadBytes(word, 4);
			if (err == noErr)
			{
				fCount = (long) (int) GetBigEndianWord(word);
				unsigned char table[kTXContainerValuesMax * 12];
				err = fStream->ReadBytes(table, fCount * 12);
				for (long i = 0; i < fCount && err == noErr; i++)
				{
					fValues[i].fType = GetBigEndianWord(table + i * 12);
					fValues[i].fCount = (long) (int) GetBigEndianWord(table + i * 12 + 4);
					fValues[i].fSize = (long) (int) GetBigEndianWord(table + i * 12 + 8);
				}
			}
			if (err != noErr)
				return err;
		}
		long at = fBase + 4 + kTXContainerValuesMax * 12;
		for (long i = 0; i < fCount; i++)
		{
			if (fValues[i].fType == type)
			{
				fStream->SetPosition(at);
				fCurrent = i;
				return noErr;
			}
			at = fValues[i].fSize + at;
		}
		fCurrent = -1;
	}
	return kTXErrNoValue;
}


// ROM 0x002351f4 GetCountObjects__14TXStdContainerFPl
NewtonErr
TXStdContainer::GetCountObjects(long* count)
{
	*count = fValues[fCurrent].fCount;
	return noErr;
}


// ROM 0x00235210 GetValueSize__14TXStdContainerFPl
NewtonErr
TXStdContainer::GetValueSize(long* size)
{
	*size = fValues[fCurrent].fSize;
	return noErr;
}


/*------------------------------------------------------------------------------
	T X L o c a l C o n t a i n e r
------------------------------------------------------------------------------*/

// ROM 0x0023522c __ct__16TXLocalContainerFP8TXStream
TXLocalContainer::TXLocalContainer(TXStream* stream)
	: TXStdContainer(stream)
{ }


// ROM 0x00235280 WriteObjectData__16TXLocalContainerFP12TXAttrObjectlPUc
// The length, and the object itself (a reference to it, when asked).
// DEVIATION: the pointer is written as the host's pointer, eight bytes -
// a local container never leaves the machine that wrote it.
NewtonErr
TXLocalContainer::WriteObjectData(TXAttrObject* object, long length, unsigned char* reference)
{
	unsigned char word[4];
	PutBigEndianWord(word, (unsigned int) length);
	NewtonErr err = fStream->WriteBytes(word, 4);
	if (err != noErr)
		return err;
	if (*reference)
	{
		object = object->Reference();
		if (object == nil)
			return kError_No_Memory;
	}
	return fStream->WriteBytes(&object, sizeof(object));
}


// ROM 0x0023530c EndWrite__16TXLocalContainerFUcP21TXContainerImportInfo
// A failed write gives back the references it took.
NewtonErr
TXLocalContainer::EndWrite(Boolean failed, TXContainerImportInfo* /*info*/)
{
	if (!failed)
		return WriteTable();
	FreeObjects();
	return noErr;
}


// ROM 0x0023534c ReadObject__16TXLocalContainerFlPP12TXAttrObjectPlPUc
NewtonErr
TXLocalContainer::ReadObject(long /*index*/, TXAttrObject** object, long* length, unsigned char* owned)
{
	*object = nil;
	*length = 0;
	unsigned char word[4];
	NewtonErr err = fStream->ReadBytes(word, 4);
	if (err != noErr)
		return err;
	*length = (long) (int) GetBigEndianWord(word);
	TXAttrObject* read;
	err = fStream->ReadBytes(&read, sizeof(read));
	if (err == noErr)
	{
		*object = read;
		*owned = 0;
	}
	return err;
}


// ROM 0x002353b8 FreeObjects__16TXLocalContainerFUl
// ==> whether the container had that value.
Boolean
TXLocalContainer::FreeObjects(unsigned long type)
{
	if (ConvertAndFocusOnValue(type) != noErr)
		return false;
	long count;
	if (GetCountObjects(&count) == noErr)
	{
		for (long i = 0; i < count; i++)
		{
			TXAttrObject* object;
			long length;
			unsigned char owned;
			if (ReadObject(i, &object, &length, &owned) != noErr)
				return true;
			object->Free();
		}
	}
	return true;
}


// ROM 0x00235468 FreeObjects__16TXLocalContainerFv
// The runs (by name, or else by every registered run's public type) and
// the rulers.
Boolean
TXLocalContainer::FreeObjects(void)
{
	if (!FreeObjects(kTXValueRuns))
	{
		for (long i = gRegisteredRuns->GetCount() - 1; i >= 0; i--)
		{
			unsigned long type = gRegisteredRuns->GetIndObject(i)->GetPublicType();
			if (type != 0)
				FreeObjects(type);
		}
	}
	return FreeObjects(kTXValueRulers);
}


/*------------------------------------------------------------------------------
	T X P r i v a t e C o n t a i n e r
------------------------------------------------------------------------------*/

// ROM 0x002354e8 __ct__18TXPrivateContainerFlT1P13TXObjectRangeT3P7TXCharsP11TXFormatter
TXPrivateContainer::TXPrivateContainer(TXOffset start, long length, TXObjectRange* runs, TXObjectRange* rulers, TXChars* chars, TXFormatter* formatter)
	: TXContainer(nil), fReadPos(0)
{
	fNewRuns = nil;
	fRulers = rulers;
	fRuns = runs;
	fLength = length;
	fStart = start;
	fFormatter = formatter;
	fNewRulers = nil;
	fChars = chars;
	fWritten = 0;
	fGraphics = false;
}


// ROM 0x00235570 AppendNewValue__18TXPrivateContainerFUll
// Text: the line ends given room for the lines it will add (one for every
// fifty characters more); runs and rulers: a range gathered for them.
NewtonErr
TXPrivateContainer::AppendNewValue(unsigned long type, long count)
{
	TXContainer::AppendNewValue(type, count);
	if (type == kTXValueText)
	{
		if (count - fLength > 0)
		{
			NewtonErr err = fFormatter->ReserveLines((count - fLength) / 50);
			if (err != noErr)
				return err;
		}
		return noErr;
	}
	fWritten = 0;
	if (type == kTXValueRulers)
	{
		fNewRulers = new TXObjectRange(count);
		if (fNewRulers == nil)
			return kError_No_Memory;
	}
	else
	{
		fNewRuns = new TXObjectRange(count);
		if (fNewRuns == nil)
			return kError_No_Memory;
		if (type != kTXValueRuns)
			fGraphics = true;
	}
	return noErr;
}


// ROM 0x0023563c WriteText__18TXPrivateContainerFP16TXTextDescriptor
// The document's stretch replaced by the characters.
NewtonErr
TXPrivateContainer::WriteText(TXTextDescriptor* text)
{
	return fChars->Replace(fStart, fLength, text);
}


// ROM 0x00235658 WriteObject__18TXPrivateContainerFlP12TXAttrObjectT1PUc
NewtonErr
TXPrivateContainer::WriteObject(long /*index*/, TXAttrObject* object, long length, unsigned char* reference)
{
	fWritten = fWritten + length;
	TXObjectRange* range = (fType == kTXValueRulers) ? fNewRulers : fNewRuns;
	if (range->InsertObjectRange(-1, fWritten, object, *reference) == nil)
		return kError_No_Memory;
	return noErr;
}


// ROM 0x002356c4 EndValueWrite__18TXPrivateContainerFv
// Room made in the document's ranges for the ones gathered.
NewtonErr
TXPrivateContainer::EndValueWrite(void)
{
	if (fType == kTXValueText)
		return noErr;
	TXObjectRange* into;
	TXObjectRange* from;
	if (fType == kTXValueRulers)
	{
		into = fRulers;
		from = fNewRulers;
	}
	else
	{
		into = fRuns;
		from = fNewRuns;
	}
	if (from->fCount > 0)
	{
		long had = into->fCount;
		NewtonErr err = into->SetCount(had + from->fCount);
		into->fCount = had;
		return err;
	}
	return noErr;
}


// ROM 0x00235710 EndWrite__18TXPrivateContainerFUcP21TXContainerImportInfo
// The gathered runs and rulers put in place of the stretch's (they now
// belong to the document); a picture written without text brings its own
// character.  A failed write gives back the room it took.
NewtonErr
TXPrivateContainer::EndWrite(Boolean failed, TXContainerImportInfo* info)
{
	if (!failed)
	{
		if (fNewRuns != nil)
		{
			fRuns->ReplaceRange(fStart, fLength, fNewRuns, false);
			fNewRuns->fOwnsObjects = false;
			if (fGraphics && info->fTextCount == 0 && fNewRuns->fCount == 1
			 && fNewRuns->RangeIndexToObject(0)->GetPublicType() != 0)
			{
				TXTextDescriptor text;
				text.Set((UniChar*) gTXGraphicsRunChar, 1);
				fChars->Replace(fStart, fLength, &text);
				info->fTypes |= kTXImportText;
				info->fTextCount = 1;
			}
		}
		if (fNewRulers != nil)
		{
			fRulers->ReplaceRange(fStart, fLength, fNewRulers, false);
			fNewRulers->fOwnsObjects = false;
		}
	}
	else
	{
		fChars->Compact();
		fFormatter->Compact();
	}
	if (fNewRuns != nil)
	{
		fRuns->Compact();
		delete fNewRuns;
	}
	if (fNewRulers != nil)
	{
		fRulers->Compact();
		delete fNewRulers;
	}
	return noErr;
}


// ROM 0x002358a4 FocusOnValue__18TXPrivateContainerFUl
NewtonErr
TXPrivateContainer::FocusOnValue(unsigned long type)
{
	TXContainer::FocusOnValue(type);
	if (type == kTXValueText || type == kTXValueRuns || type == kTXValueRulers)
	{
		fReadPos = fStart;
		return noErr;
	}
	return kTXErrNoValue;
}


// ROM 0x002358f4 AcquireTextDescriptor__18TXPrivateContainerFP16TXTextDescriptor
void
TXPrivateContainer::AcquireTextDescriptor(TXTextDescriptor* text)
{
	text->fChars = fChars;
	text->fText = nil;
	text->fCount = fLength;
	text->fPosition = fStart;
	text->fStream = nil;
}


// ROM 0x0023590c GetCountObjects__18TXPrivateContainerFPl
NewtonErr
TXPrivateContainer::GetCountObjects(long* count)
{
	TXObjectRange* range = (fType == kTXValueRulers) ? fRulers : fRuns;
	*count = range->CountRangeObjects(fStart, fLength);
	return noErr;
}


// ROM 0x00235950 GetValueSize__18TXPrivateContainerFPl
NewtonErr
TXPrivateContainer::GetValueSize(long* size)
{
	*size = fLength << 1;
	return noErr;
}


// ROM 0x00235964 ReadObject__18TXPrivateContainerFlPP12TXAttrObjectPlPUc
// The object at the read position, and how much of the stretch it covers.
NewtonErr
TXPrivateContainer::ReadObject(long /*index*/, TXAttrObject** object, long* length, unsigned char* owned)
{
	TXObjectRange* range = (fType == kTXValueRulers) ? fRulers : fRuns;
	*object = range->GetNextObjectRange(fReadPos, length);
	long left = (fStart + fLength) - fReadPos;
	if (left < *length)
		*length = left;
	fReadPos = fReadPos + *length;
	*owned = 0;
	return noErr;
}
