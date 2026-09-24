/*
	File:		text/TXAttributes.cpp

	Contains:	The attributes a run of text carries - see TXAttributes.h.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TXAttributes.h"
#include "NewtonMemory.h"

#include <string.h>


/*------------------------------------------------------------------------------
	T X A t t r V a l u e s
------------------------------------------------------------------------------*/

// ROM 0x00231340 __ct__12TXAttrValuesFv
// An empty list.  The element size is 0x20 - four bytes of tag, a flag,
// a length and twenty bytes of value - and the chunk is nought, which
// TXArray takes as one: a list of attributes is short and grows one at
// a time.
TXAttrValues::TXAttrValues()
	: TXArray(0x20, 0)
{ }


// ROM 0x00231388 __dt__12TXAttrValuesFv
TXAttrValues::~TXAttrValues()
{ }


// ROM 0x00231408 Add__12TXAttrValuesFUlPCviUc
// One attribute on the end.  `ownsValue` says the value is a pointer to
// an object the list is to free with the entry.
void
TXAttrValues::Add(TXAttrTag tag, const void* value, int length, Boolean ownsValue)
{
	TXAttrValue* entry = (TXAttrValue*) Insert(nil, 1, -1);
	entry->fLength = length;
	entry->fTag = tag;
	BlockMove(value, entry->fValue, length);
	entry->fOwnsValue = ownsValue;
}


// ROM 0x0023145c Remove__12TXAttrValuesFlT1
// The array's Remove, with the owned values deleted first.
long
TXAttrValues::Remove(long at, long count)
{
	for (long i = 0; i < count; i++)
	{
		TXAttrValue* entry = (TXAttrValue*) GetElementPtr(at + i);
		if (entry->fOwnsValue)
		{
			TXVirtualObject* owned = *(TXVirtualObject**) entry->fValue;
			if (owned != nil)
				delete owned;
		}
	}
	long after = fCount - (at + count);
	if (after > 0)
		Stuff(at, GetElementPtr(at + count), after);
	fCount -= count;
	CheckUnusedCount();
	return fCount;
}


// ROM 0x002314cc GetIndAttrData__12TXAttrValuesCFlPUlPvPi
void
TXAttrValues::GetIndAttrData(long index, TXAttrTag* tag, void* value, int* length) const
{
	TXAttrValue* entry = (TXAttrValue*) GetElementPtr(index);
	*tag = entry->fTag;
	*length = entry->fLength;
	BlockMove(entry->fValue, value, entry->fLength);
}


// ROM 0x0023150c SetIndAttrData__12TXAttrValuesFlUlPCvi
void
TXAttrValues::SetIndAttrData(long index, TXAttrTag tag, const void* value, int length)
{
	TXAttrValue* entry = (TXAttrValue*) GetElementPtr(index);
	entry->fTag = tag;
	entry->fLength = length;
	BlockMove(value, entry->fValue, length);
}


// ROM 0x00231544 GetValue__12TXAttrValuesCFUlPv
// The value of that tag copied out.  The list is walked from the front
// - it is short enough that nothing better is worth it.
Boolean
TXAttrValues::GetValue(TXAttrTag tag, void* value) const
{
	TXAttrValue* entry = (TXAttrValue*) GetElementPtr(0);
	TXAttrValue* last = (TXAttrValue*) GetLastElementPtr();
	while (entry <= last)
	{
		if (entry->fTag == tag)
		{
			BlockMove(entry->fValue, value, entry->fLength);
			return true;
		}
		entry++;
	}
	return false;
}


/*------------------------------------------------------------------------------
	T X A t t r O b j e c t
------------------------------------------------------------------------------*/

// ROM 0x002310e4 __ct__12TXAttrObjectFv
// One reference to begin with: whoever made it holds it.
TXAttrObject::TXAttrObject()
{
	fCountReferences = 1;
}


// ROM 0x00231120 __dt__12TXAttrObjectFv
TXAttrObject::~TXAttrObject()
{ }


// ROM 0x002312fc Free__12TXAttrObjectFv
// One reference given back; the object goes when the last one does.
void
TXAttrObject::Free(void)
{
	if (--fCountReferences != 0)
		return;
	FreeData();
	delete this;
}


// ROM 0x002315bc Reference__12TXAttrObjectFv
void
TXAttrObject::Reference(void)
{
	fCountReferences++;
}


// ROM 0x002315b4 GetCountReferences__12TXAttrObjectFv
long
TXAttrObject::GetCountReferences(void)
{
	return fCountReferences;
}


// The base answers nothing about itself: these are all here for the
// subclasses to override.
// ROM 0x002315b0 FreeData__12TXAttrObjectFv
void			TXAttrObject::FreeData(void)								{ }
// ROM 0x002315cc GetObjFlags__12TXAttrObjectCFv
unsigned long	TXAttrObject::GetObjFlags(void) const						{ return 0; }
// ROM 0x00231294 GetAttributesValues__12TXAttrObjectFP12TXAttrValues
void			TXAttrObject::GetAttributesValues(TXAttrValues*)			{ }
// ROM 0x002315dc GetAttributeValue__12TXAttrObjectCFUlPv
Boolean			TXAttrObject::GetAttributeValue(TXAttrTag, void*) const		{ return false; }
// ROM 0x002315e4 SetAttributeValue__12TXAttrObjectFUlPCv
void			TXAttrObject::SetAttributeValue(TXAttrTag, const void*)		{ }
// ROM 0x002315d4 GetPublicType__12TXAttrObjectCFv
long			TXAttrObject::GetPublicType(void) const						{ return 0; }
// ROM 0x002312ec WritePublicData__12TXAttrObjectFP8TXStreamPl
NewtonErr		TXAttrObject::WritePublicData(TXStream*, long*)				{ return noErr; }
// ROM 0x002312f4 ReadPublicData__12TXAttrObjectFP8TXStreaml
NewtonErr		TXAttrObject::ReadPublicData(TXStream*, long)				{ return noErr; }
// ROM 0x002311dc GetCommonAttrValue__12TXAttrObjectCFUlPv
Boolean			TXAttrObject::GetCommonAttrValue(TXAttrTag, void*) const		{ return false; }
// ROM 0x00231138 GetAttributeFlags__12TXAttrObjectCFUl
unsigned long	TXAttrObject::GetAttributeFlags(TXAttrTag) const				{ return 0; }


// ROM 0x00231140 UpdateAttribute__12TXAttrObjectFUlPCvl
// The base takes no notice of `how` and simply sets the value.
void
TXAttrObject::UpdateAttribute(TXAttrTag tag, const void* value, long /*how*/)
{
	SetAttributeValue(tag, value);
}


// ROM 0x00231298 IsEqual__12TXAttrObjectCFPC12TXAttrObject
// The base can only tell that two objects are of the same kind - the
// same object, or the same class id.  A subclass that has values to
// compare overrides this.
Boolean
TXAttrObject::IsEqual(const TXAttrObject* other) const
{
	if (other != this && GetClassId() != other->GetClassId())
		return false;
	return true;
}


// ROM 0x00231148 Update__12TXAttrObjectFPC12TXAttrValuesl
// Every value of the list applied, last first; ==> the attribute flags
// of the ones that were, or-ed together, which is what tells the caller
// how much of the layout has to be done again.
unsigned long
TXAttrObject::Update(const TXAttrValues* values, long how)
{
	unsigned long flags = 0;
	for (long i = values->GetCount() - 1; i >= 0; i--)
	{
		TXAttrTag tag;
		char value[kTXAttrValueSize];
		int length;
		((TXAttrValues*) values)->GetIndAttrData(i, &tag, value, &length);
		UpdateAttribute(tag, value, how);
		flags |= GetAttributeFlags(tag);
	}
	return flags;
}


// ROM 0x002311e4 GetCommonAttrValues__12TXAttrObjectFP12TXAttrValues
// The list narrowed to the attributes this object agrees about: an
// entry it does not share is taken out, and one it does is written back
// with its own value.  Run over every object of a selection in turn,
// what is left is what they all have in common - which is what a style
// slip shows, and why it shows a blank where they disagree.
void
TXAttrObject::GetCommonAttrValues(TXAttrValues* values)
{
	for (long i = values->GetCount() - 1; i >= 0; i--)
	{
		TXAttrTag tag;
		char value[kTXAttrValueSize];
		int length;
		values->GetIndAttrData(i, &tag, value, &length);
		if (!GetCommonAttrValue(tag, value))
			values->Remove(i, 1);
		else
			values->SetIndAttrData(i, tag, value, length);
	}
}
