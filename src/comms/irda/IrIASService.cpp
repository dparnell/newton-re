/*
	File:		comms/irda/IrIASService.cpp

	Contains:	The IAS database (IrIASService.h).

	Reconstructed from the MP2x00 US ROM (0x000f1924-0x000f23dc); each
	function cites its origin.
*/

#include "IrIASService.h"
#include "BufferSegment.h"
#include "ListIterator.h"
#include "CommErrors.h"
#include "NewtonMemory.h"

#include <string.h>
#include <stdlib.h>

#define kIASErrNoMemory		(-7000)		// the ROM's for a failed allocation

// ROM 0x00371760 kIASDeviceClassStr
const char	kIASDeviceClassStr[] = "Device";
// ROM 0x00371768 kIASDeviceNameAttrStr
const char	kIASDeviceNameAttrStr[] = "DeviceName";
// ROM 0x00371774 kIASLMPSupportAttrStr
const char	kIASLMPSupportAttrStr[] = "IrLMPSupport";


/*------------------------------------------------------------------------------
	TIASNamedList
------------------------------------------------------------------------------*/

// ROM 0x000f1cbc __ct__13TIASNamedListFv
TIASNamedList::TIASNamedList()
{
	fName = nil;
}


// ROM 0x000f1cf8 __dt__13TIASNamedListFv
TIASNamedList::~TIASNamedList()
{
	if (fName != nil)
	{
		free(fName);
		fName = nil;
	}
}


// ROM 0x000f1d44 Init__13TIASNamedListFPCUc
NewtonErr
TIASNamedList::Init(const char* name)
{
	fName = (char*) malloc(strlen(name) + 1);
	if (fName == nil)
		return kIASErrNoMemory;
	strcpy(fName, name);
	return noErr;
}


// ROM 0x000f1d8c Search__13TIASNamedListFPCUc
// The item of that name (items are named lists themselves).
void*
TIASNamedList::Search(const char* name)
{
	TIASNamedList* found = nil;
	CListIterator iter(this);
	for (TIASNamedList* item = (TIASNamedList*) iter.FirstItem(); iter.More(); item = (TIASNamedList*) iter.NextItem())
	{
		if (strcmp(item->fName, name) == 0)
		{
			found = item;
			break;
		}
	}
	return found;
}


/*------------------------------------------------------------------------------
	TIASElement
------------------------------------------------------------------------------*/

// ROM 0x000f1e14 __ct__11TIASElementFv
TIASElement::TIASElement()
{
	fType = kIASValueMissing;
	fLength = 0;
	fValue = 0;
	fData = nil;
}


// ROM 0x000f1e50 __dt__11TIASElementFv
TIASElement::~TIASElement()
{
	if (fData != nil && fData != &fValue)
	{
		free(fData);
		fData = nil;
	}
}


// ROM 0x000f1e98 SetInteger__11TIASElementFUl
void
TIASElement::SetInteger(ULong value)
{
	fType = kIASValueInteger;
	fValue = value;
	fLength = 4;
	fData = &fValue;
}


// ROM 0x000f1eb8 SetNBytes__11TIASElementFUlT1
// Up to four bytes, kept in the integer's word (the first of them its most
// significant byte).
void
TIASElement::SetNBytes(ULong bytes, ULong length)
{
	fType = kIASValueNBytes;
	fValue = bytes;
	fLength = length;
	fData = &fValue;
}


// ROM 0x000f1ed4 SetString__11TIASElementFPCUc
NewtonErr
TIASElement::SetString(const char* string)
{
	fType = kIASValueString;
	fLength = strlen(string);
	fValue = 0;
	fData = malloc(fLength + 1);
	if (fData == nil)
		return kIASErrNoMemory;
	strcpy((char*) fData, string);
	return noErr;
}


// ROM 0x000f1f9c GetInteger__11TIASElementFRUl
NewtonErr
TIASElement::GetInteger(ULong* value)
{
	if (fType != kIASValueInteger)
		return kIrDAErrProtocolError;
	*value = fValue;
	return noErr;
}


// ROM 0x000f1fc0 AddInfoToBuffer__11TIASElementFP7CBuffer
// Object id 0, the type, (bytes and strings: a zero - the character set, or
// the length's high byte - and the length,) then the value.
// DEVIATION (byte order): an integer's or a few bytes' word is put
// big-endian, which is how the ROM's word lies in memory.
void
TIASElement::AddInfoToBuffer(CBuffer* buffer)
{
	UByte header[8];
	UByte* p = header;
	*p++ = 0;
	*p++ = 0;
	*p++ = fType;
	if (fType == kIASValueNBytes || fType == kIASValueString)
	{
		*p++ = 0;
		*p++ = (UByte) fLength;
	}
	buffer->Putn(header, p - header);
	if (fData == &fValue)
	{
		UByte word[4] = { (UByte) (fValue >> 24), (UByte) (fValue >> 16), (UByte) (fValue >> 8), (UByte) fValue };
		buffer->Putn(word, fLength);
	}
	else
		buffer->Putn((const UByte*) fData, fLength);
}


// ROM 0x000f2050 ExtractInfoFromBuffer__11TIASElementFP7CBuffer
// Integers (and a missing value) only; bytes and strings are not supported.
NewtonErr
TIASElement::ExtractInfoFromBuffer(CBuffer* buffer)
{
	UByte header[3];
	if (buffer->Getn(header, 3) != 3)
		return kIrDAErrProtocolError;
	NewtonErr err = noErr;
	UByte type = header[2];
	if (type == kIASValueMissing)
		return noErr;
	if (type == kIASValueInteger)
	{
		UByte word[4];
		if (buffer->Getn(word, 4) != 4)
			return kIrDAErrProtocolError;
		SetInteger(((ULong) word[0] << 24) | ((ULong) word[1] << 16) | ((ULong) word[2] << 8) | word[3]);
		return noErr;
	}
	err = kCommErrNotSupported;
	if (type != kIASValueNBytes && type != kIASValueString)
		err = kIrDAErrProtocolError;
	return err;
}


/*------------------------------------------------------------------------------
	TIASAttribute
------------------------------------------------------------------------------*/

// ROM 0x000f1a78 __ct__13TIASAttributeFv
TIASAttribute::TIASAttribute()
{ }


// ROM 0x000f1aac __dt__13TIASAttributeFv
TIASAttribute::~TIASAttribute()
{
	for (ArrayIndex i = 0; i < GetArraySize(); i++)
	{
		TIASElement* element = (TIASElement*) At(i);
		if (element != nil)
			delete element;
	}
}


// ROM 0x000f1b18 Insert__13TIASAttributeFP11TIASElement
NewtonErr
TIASAttribute::Insert(TIASElement* element)
{
	return InsertAt(GetArraySize(), element);
}


// ROM 0x000f1b24 AddInfoToBuffer__13TIASAttributeFP7CBuffer
// The count of elements (two bytes, big-endian), then each.
void
TIASAttribute::AddInfoToBuffer(CBuffer* buffer)
{
	ArrayIndex count = GetArraySize();
	buffer->Put((count >> 8) & 0xff);
	buffer->Put(count & 0xff);
	for (ArrayIndex i = 0; i < GetArraySize(); i++)
		((TIASElement*) At(i))->AddInfoToBuffer(buffer);
}


// ROM 0x000f1bfc ExtractInfoFromBuffer__13TIASAttributeFP7CBuffer
// The elements of an answer.  (ROM QUIRK: an answer of no elements is a
// protocol error.)
NewtonErr
TIASAttribute::ExtractInfoFromBuffer(CBuffer* buffer)
{
	NewtonErr err = kIrDAErrProtocolError;
	UByte count[2];
	if (buffer->Getn(count, 2) != 2)
		return err;
	ULong n = (count[0] << 8) + count[1];
	for (ULong i = 0; i < n; i++)
	{
		TIASElement* element = new TIASElement;
		if (element == nil)
			return kIASErrNoMemory;
		err = Insert(element);
		if (err != noErr)
		{
			delete element;
			return err;
		}
		err = element->ExtractInfoFromBuffer(buffer);
		if (err != noErr)
			return err;
	}
	return err;
}


/*------------------------------------------------------------------------------
	TIASClass
------------------------------------------------------------------------------*/

// ROM 0x000f19c8 __ct__9TIASClassFv
TIASClass::TIASClass()
{ }


// ROM 0x000f19fc __dt__9TIASClassFv
TIASClass::~TIASClass()
{
	for (ArrayIndex i = 0; i < GetArraySize(); i++)
	{
		TIASAttribute* attribute = (TIASAttribute*) At(i);
		if (attribute != nil)
			delete attribute;
	}
}


// ROM 0x000f1a68 Insert__9TIASClassFP13TIASAttribute
NewtonErr
TIASClass::Insert(TIASAttribute* attribute)
{
	return InsertAt(GetArraySize(), attribute);
}


// ROM 0x000f1a74 FindAttribute__9TIASClassFPCUc
TIASAttribute*
TIASClass::FindAttribute(const char* name)
{
	return (TIASAttribute*) Search(name);
}


/*------------------------------------------------------------------------------
	TIASService
------------------------------------------------------------------------------*/

// ROM 0x000f1924 __ct__11TIASServiceFv
TIASService::TIASService()
{ }


// ROM 0x000f1958 __dt__11TIASServiceFv
TIASService::~TIASService()
{
	for (ArrayIndex i = 0; i < GetArraySize(); i++)
	{
		TIASClass* klass = (TIASClass*) At(i);
		if (klass != nil)
			delete klass;
	}
}


// ROM 0x000f19c4 FindClass__11TIASServiceFPCUc
TIASClass*
TIASService::FindClass(const char* name)
{
	return (TIASClass*) Search(name);
}


// ROM 0x000f2284 AddClass__11TIASServiceFPCUcRUl
// The class of that name, made if there is none (and *added says so: 1).
TIASClass*
TIASService::AddClass(const char* name, ULong* added)
{
	*added = 0;
	TIASClass* klass = FindClass(name);
	if (klass != nil)
		return klass;
	klass = new TIASClass;
	if (klass == nil)
		return nil;
	if (klass->Init(name) == noErr && InsertAt(GetArraySize(), klass) == noErr)
	{
		*added |= 1;
		return klass;
	}
	delete klass;
	return nil;
}


// ROM 0x000f21d4 AddAttribute__11TIASServiceFPCUcT1RUl
// The attribute of that name in that class, either made if need be (and
// *added says so: 1 the class, 2 the attribute).
TIASAttribute*
TIASService::AddAttribute(const char* className, const char* attrName, ULong* added)
{
	TIASClass* klass = AddClass(className, added);
	if (klass == nil)
		return nil;
	TIASAttribute* attribute = klass->FindAttribute(attrName);
	if (attribute != nil)
		return attribute;
	attribute = new TIASAttribute;
	if (attribute != nil)
	{
		if (attribute->Init(attrName) == noErr && klass->Insert(attribute) == noErr)
		{
			*added |= 2;
			return attribute;
		}
		delete attribute;
	}
	RemoveClass(className, *added);
	return nil;
}


// ROM 0x000f2160 AddAttributeEntry__11TIASServiceFPCUcT1P11TIASElement
// The element added to the attribute; failing, what was made for it undone
// and the element thrown away.
NewtonErr
TIASService::AddAttributeEntry(const char* className, const char* attrName, TIASElement* element)
{
	NewtonErr err = kIASErrNoMemory;
	ULong added;
	TIASAttribute* attribute = AddAttribute(className, attrName, &added);
	if (attribute != nil)
	{
		err = attribute->Insert(element);
		if (err == noErr)
			return noErr;
		RemoveAttribute(className, attrName, added);
	}
	if (element != nil)
		delete element;
	return err;
}


// ROM 0x000f1ba4 AddIntegerEntry__11TIASServiceFPCUcT1Ul
NewtonErr
TIASService::AddIntegerEntry(const char* className, const char* attrName, ULong value)
{
	TIASElement* element = new TIASElement;
	if (element == nil)
		return kIASErrNoMemory;
	element->SetInteger(value);
	return AddAttributeEntry(className, attrName, element);
}


// ROM 0x000f1f2c AddStringEntry__11TIASServiceFPCUcN21
NewtonErr
TIASService::AddStringEntry(const char* className, const char* attrName, const char* string)
{
	TIASElement* element = new TIASElement;
	if (element != nil)
	{
		if (element->SetString(string) == noErr)
			return AddAttributeEntry(className, attrName, element);
		delete element;
	}
	return kIASErrNoMemory;
}


// ROM 0x000f2100 AddNBytesEntry__11TIASServiceFPCUcT1UlT3
NewtonErr
TIASService::AddNBytesEntry(const char* className, const char* attrName, ULong bytes, ULong length)
{
	TIASElement* element = new TIASElement;
	if (element == nil)
		return kIASErrNoMemory;
	element->SetNBytes(bytes, length);
	return AddAttributeEntry(className, attrName, element);
}


// ROM 0x000f2314 RemoveClass__11TIASServiceFPCUcUl
// The class taken out, if it was made (added & 1).
NewtonErr
TIASService::RemoveClass(const char* className, ULong added)
{
	TIASClass* klass = FindClass(className);
	if (klass != nil && (added & 1))
	{
		Remove(klass);
		delete klass;
	}
	return noErr;
}


// ROM 0x000f235c RemoveAttribute__11TIASServiceFPCUcT1Ul
// The attribute taken out if it was made (2), and the class if it was (1).
NewtonErr
TIASService::RemoveAttribute(const char* className, const char* attrName, ULong added)
{
	TIASClass* klass = FindClass(className);
	if (klass == nil)
		return noErr;
	TIASAttribute* attribute = klass->FindAttribute(attrName);
	if (attribute != nil && (added & 2))
	{
		klass->Remove(attribute);
		delete attribute;
	}
	if (added & 1)
	{
		Remove(klass);
		delete klass;
	}
	return noErr;
}
