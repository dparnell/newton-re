/*
	File:		text/TXNewtContainer.cpp

	Contains:	A container over a NewtonScript frame (TXNewtContainer.h).

	Reconstructed from the MP2x00 US ROM (0x0023e290-0x0023e5cc,
	0x0023f1bc-0x0023f648); each function cites its origin.
*/

#include "TXNewtContainer.h"
#include "TXGraphicsRun.h"
#include "TXUtilities.h"
#include "objects.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "OSErrors.h"
#include "Soups.h"
#include "LargeBinaries.h"
#include "NewtWorld.h"


// ROM 0x0023f1bc __ct__15TXNewtContainerFRC6RefVar
TXNewtContainer::TXNewtContainer(RefArg frame)
	: TXContainer(nil)
{
	fFrame = frame;
}


// ROM 0x0023f228 __dt__15TXNewtContainerFv
TXNewtContainer::~TXNewtContainer()
{ }


// ROM 0x0023e290 FocusOnValue__15TXNewtContainerFUl
// Whether the frame has the value: its slot (the runs: styles, or failing
// them viewFont), or for a picture its class.
NewtonErr
TXNewtContainer::FocusOnValue(unsigned long type)
{
	TXContainer::FocusOnValue(type);
	Ref slot;
	if (type == kTXValueText)
		slot = GetFrameSlotRef(fFrame, RSSYMtext);
	else if (type == kTXValueGraphics)
	{
		RefVar cls(ClassOf(fFrame));
		if (EQRef(cls, RSSYMgraphics))
			return noErr;
		return kTXErrNoValue;
	}
	else if (type == kTXValueRuns)
	{
		if (GetFrameSlotRef(fFrame, RSSYMstyles) != NILREF)
			return noErr;
		slot = GetFrameSlotRef(fFrame, RSSYMviewfont);
	}
	else if (type == kTXValueRulers)
		slot = GetFrameSlotRef(fFrame, RSSYMrulers);
	else
		return kTXErrNoValue;
	if (slot != NILREF)
		return noErr;
	return kTXErrNoValue;
}


// ROM 0x0023e3d8 GetCountObjects__15TXNewtContainerFPl
NewtonErr
TXNewtContainer::GetCountObjects(long* count)
{
	if (fType == kTXValueGraphics)
		*count = 1;
	else if (fType == kTXValueRuns)
	{
		RefVar styles(GetFrameSlotRef(fFrame, RSSYMstyles));
		if (ISNIL(styles))
			*count = 1;
		else
			*count = Length(styles) / 2;
	}
	else
		*count = Length(GetFrameSlotRef(fFrame, RSSYMrulers)) / 2;
	return noErr;
}


// ROM 0x0023e4a4 GetCountTextChars__15TXNewtContainerFv
// (the string less its nought)
long
TXNewtContainer::GetCountTextChars(void)
{
	RefVar text(GetFrameSlotRef(fFrame, RSSYMtext));
	return ((unsigned long) Length(text) >> 1) - 1;
}


// ROM 0x0023e4f4 GetValueSize__15TXNewtContainerFPl
NewtonErr
TXNewtContainer::GetValueSize(long* size)
{
	*size = GetCountTextChars() << 1;
	return noErr;
}


// ROM 0x0023e518 AcquireTextDescriptor__15TXNewtContainerFP16TXTextDescriptor
// The string itself, locked until ReleaseTextDescriptor.
void
TXNewtContainer::AcquireTextDescriptor(TXTextDescriptor* text)
{
	RefVar string(GetFrameSlotRef(fFrame, RSSYMtext));
	LockRef(string);
	unsigned long length = Length(string);
	text->Set((UniChar*) BinaryData(string), (length >> 1) - 1);
}


// ROM 0x0023e588 ReleaseTextDescriptor__15TXNewtContainerFP16TXTextDescriptor
void
TXNewtContainer::ReleaseTextDescriptor(TXTextDescriptor* /*text*/)
{
	RefVar string(GetFrameSlotRef(fFrame, RSSYMtext));
	UnlockRef(string);
}


// ROM 0x0023e5cc ReadObject__15TXNewtContainerFlPP12TXAttrObjectPlPUc
// A new object made from the slot's frame, the reader's to give back:
// the frame itself for a picture, the view font for text with no
// styles, otherwise the pair's length and frame.
NewtonErr
TXNewtContainer::ReadObject(long index, TXAttrObject** object, long* length, unsigned char* owned)
{
	*owned = true;
	RefVar value;
	TXAttrObject* made;
	if (fType == kTXValueGraphics)
	{
		*length = 1;
		value = fFrame;
		made = new TXNewtGraphicsRun;
	}
	else
	{
		RefVar array;
		if (fType == kTXValueRuns)
		{
			array = GetFrameSlotRef(fFrame, RSSYMstyles);
			if (ISNIL(array))
			{
				*length = GetCountTextChars();
				value = GetFrameSlotRef(fFrame, RSSYMviewfont);
			}
		}
		else
			array = GetFrameSlotRef(fFrame, RSSYMrulers);
		if (ISNIL(value))
		{
			*length = RINT(GetArraySlotRef(array, index << 1));
			value = GetArraySlotRef(array, index * 2 + 1);
		}
		unsigned long kind = kTXValueRuns;
		if (fType == kTXValueRuns)
		{
			if (IsFrame(value) && EQRef(RefVar(ClassOf(value)), RSSYMgraphics))
				made = new TXNewtGraphicsRun;
			else
				made = TXGetNewDefaultObject(kind);
		}
		else
		{
			kind = kTXValueRulers;
			made = TXGetNewDefaultObject(kind);
		}
	}
	*object = made;
	if (made == nil)
		return kError_No_Memory;
	made->SetNSObject(value);
	return noErr;
}


// ROM 0x0023f264 AppendNewValue__15TXNewtContainerFUll
// The slot made: a string of the length (a compressed large binary on the
// first store for 2K characters or more), or an array of twice as many
// slots as objects.
NewtonErr
TXNewtContainer::AppendNewValue(unsigned long type, long count)
{
	TXContainer::AppendNewValue(type, count);
	NewtonErr err = noErr;
	newton_try
	{
		if (type == kTXValueText)
		{
			RefVar string;
			if ((unsigned long) count < 0x800)
				string = AllocateBinary(RSSYMstring, (count + 1) * 2);
			else
			{
				BusyBoxSend(0x33);
				RefVar stores(GetStores());
				RefVar store(GetArraySlotRef(stores, 0));
				RefVar compander(MakeString("TLZStoreCompander"));
				RefVar data(NILREF);
				RefVar size(MAKEINT((count + 1) * 2));
				string = FLBAllocCompressed(store, RSSYMstring, size, compander, data);
			}
			SetFrameSlot(fFrame, RSSYMtext, string);
		}
		else
		{
			RefVar array(AllocateArray(RSSYMarray, count << 1));
			SetFrameSlot(fFrame, type == kTXValueRuns ? RSSYMstyles : RSSYMrulers, array);
		}
	}
	newton_catch_all
	{
		err = GetExceptionErr(&_info.exception);
	}
	end_try;
	return err;
}


// ROM 0x0023f440 WriteObject__15TXNewtContainerFlP12TXAttrObjectT1PUc
// The object's frame and length into the pair; the object itself is not
// kept (the writer is asked to give it back).
NewtonErr
TXNewtContainer::WriteObject(long index, TXAttrObject* object, long length, unsigned char* reference)
{
	*reference = true;
	NewtonErr err = noErr;
	newton_try
	{
		RefVar array(GetFrameSlotRef(fFrame, fType == kTXValueRuns ? RSSYMstyles : RSSYMrulers));
		SetArraySlotRef(array, index << 1, MAKEINT(length));
		RefVar frame(object->GetNSObject());
		SetArraySlotRef(array, index * 2 + 1, frame);
	}
	newton_catch_all
	{
		err = GetExceptionErr(&_info.exception);
	}
	end_try;
	return err;
}


// ROM 0x0023f55c WriteText__15TXNewtContainerFP16TXTextDescriptor
// The characters into the string AppendNewValue made, and the nought.
NewtonErr
TXNewtContainer::WriteText(TXTextDescriptor* text)
{
	long count = text->fCount;
	NewtonErr err = noErr;
	newton_try
	{
		RefVar string(GetFrameSlotRef(fFrame, RSSYMtext));
		// (the ROM holds it locked through a TObjectPtr)
		LockRef(string);
		UniChar* chars = (UniChar*) BinaryData(string);
		TXTextDescriptor into;
		into.Set(chars, count);
		err = text->CopyTo(&into, count);
		chars[count] = 0;
		UnlockRef(string);
	}
	newton_catch_all
	{
		err = GetExceptionErr(&_info.exception);
	}
	end_try;
	return err;
}
