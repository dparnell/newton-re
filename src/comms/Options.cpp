/*
	File:		comms/Options.cpp

	Contains:	TOption, TOptionExtended, TSubArrayOption, TOptionArray and
				TOptionIterator (Options.h).

	Reconstructed from the MP2x00 US ROM (0x0014aa38-0x0014b9f0); each
	function cites its origin.
*/

#include "Options.h"
#include "NewtonMemory.h"
#include "UserSharedMem.h"
#include "CommAddresses.h"
#include "CommOptions.h"


// ---------------------------------------------------------------------------
//	TOption
// ---------------------------------------------------------------------------

// ROM 0x0014aa38 __ct__7TOptionFUl
TOption::TOption(ULong type)
{
	fLabel = 0;
	fLength = 0;
	fFlags = type | opSetNegotiate;
}


// ROM 0x0014aa78 Reset__7TOptionFv
// Forget the processed flag and the op code's result (the op code stays).
void
TOption::Reset()
{
	fFlags &= 0x7fffff00;
}


// ROM 0x0014ad04 SetAsService__7TOptionFUl
void
TOption::SetAsService(ULong serviceId)
{
	fLabel = serviceId;
	fFlags = (fFlags & 0xc0ffffff) | kServiceType;
}


// ROM 0x0014afc8 SetAsService__7TOptionFv
void
TOption::SetAsService()
{
	fFlags = (fFlags & 0xc0ffffff) | kServiceType;
}


// ROM 0x0014b250 SetAsOption__7TOptionFUl
void
TOption::SetAsOption(ULong optionId)
{
	fLabel = optionId;
	fFlags = (fFlags & 0xc0ffffff) | kOptionType;
}


// ROM 0x0014b844 SetAsConfig__7TOptionFUl
void
TOption::SetAsConfig(ULong configId)
{
	fLabel = configId;
	fFlags = (fFlags & 0xc0ffffff) | kConfigType;
}


// ROM 0x0014b944 SetAsAddress__7TOptionFUl
void
TOption::SetAsAddress(ULong addrId)
{
	fLabel = addrId;
	fFlags = (fFlags & 0xc0ffffff) | kAddressType;
}


// ROM 0x0014b95c CopyDataFrom__7TOptionFP7TOption
// Copy another option's data into this one's, as much as fits: opTruncated
// if the source had more, opPadded if it had less (the rest left as it was).
NewtonErr
TOption::CopyDataFrom(TOption* source)
{
	Size count = source->fLength;
	NewtonErr result = noErr;
	if (count > fLength)
	{
		result = opTruncated;
		count = fLength;
	}
	else if (count < fLength)
		result = opPadded;
	BlockMove(source + 1, this + 1, count);
	return result;
}


// ---------------------------------------------------------------------------
//	TOptionExtended
// ---------------------------------------------------------------------------

// ROM 0x0014b9ac __ct__15TOptionExtendedFUl
TOptionExtended::TOptionExtended(ULong type)
	: TOption(type)
{
	fServiceLabel = 0;
	fExtendedResult = noErr;
}


// ROM 0x0014b92c SetAsServiceSpecific__15TOptionExtendedFUl
void
TOptionExtended::SetAsServiceSpecific(ULong service)
{
	fFlags = (fFlags & 0xc0ffffff) | kServiceSpecificType;
	fServiceLabel = service;
}


// ---------------------------------------------------------------------------
//	TSubArrayOption
// ---------------------------------------------------------------------------

// ROM 0x0014b014 __ct__15TSubArrayOptionFUll
// `size` bytes of options, `count` of them, to follow.
TSubArrayOption::TSubArrayOption(ULong size, ArrayIndex count)
	: TOption(kOptionType)
{
	SetLabel(kSubArrayOptionLabel);
	// the ROM's size + 4: the count is part of the data (DEVIATION: the host
	// measures it, padding and all - Options.h)
	SetLength(size + (sizeof(TSubArrayOption) - sizeof(TOption)));
	fCount = count;
}


// ---------------------------------------------------------------------------
//	The addresses and the service identifier (CommAddresses.h, CommOptions.h)
// ---------------------------------------------------------------------------

// ROM 0x000663cc __ct__16TCMARouteAddressFl
// A 'rout' address of a kind (its length is the subclass's to set).
TCMARouteAddress::TCMARouteAddress(RouteAddrType type)
	: TOption(kAddressType)
{
	SetLabel(kCMARouteLabel);
	fType = type;
}


// ROM 0x00066418 __ct__15TCMAPhoneNumberFUl
// A phone number, its characters following the option (InsertVarOptionAt
// puts them there).  DEVIATION: the ROM's length is the number's plus 8,
// its two words; the host's words are larger, so it measures them.
TCMAPhoneNumber::TCMAPhoneNumber(ULong phoneLen)
	: TCMARouteAddress(kPhoneNumber)
{
	SetLength(phoneLen + (sizeof(TCMAPhoneNumber) - sizeof(TOption)));
	fPhoneLen = phoneLen;
}


// ROM 0x0006cc4c __ct__21TCMOServiceIdentifierFv
// A service to start ('serv'), by its identifier (or its port).
// DEVIATION: the ROM's length is 8, the two words; the host measures them.
TCMOServiceIdentifier::TCMOServiceIdentifier()
	: TOption(kOptionType)
{
	SetLabel(kCMOServiceIdentifier);
	SetLength(sizeof(TCMOServiceIdentifier) - sizeof(TOption));
	fServiceId = 0;
	fPortId = 0;
	SetAsService();
}


// ---------------------------------------------------------------------------
//	TOptionIterator
// ---------------------------------------------------------------------------

// ROM 0x0014aa8c AppendToList__15TOptionIteratorFP15TOptionIterator
// Put this iterator on the ring after toList's; the ring it answers.
TOptionIterator*
TOptionIterator::AppendToList(TOptionIterator* toList)
{
	if (toList == nil)
		return this;
	TOptionIterator* next = toList->fNextLink;
	fPreviousLink = toList;
	fNextLink = next;
	next->fPreviousLink = this;
	toList->fNextLink = this;
	return this;
}


// ROM 0x0014aab0 RemoveFromList__15TOptionIteratorFv
// Take this iterator off its ring; what is left of the ring (nil if this
// was the only one).
TOptionIterator*
TOptionIterator::RemoveFromList()
{
	TOptionIterator* next = fNextLink;
	TOptionIterator* rest = (next == this) ? nil : next;
	next->fPreviousLink = fPreviousLink;
	fPreviousLink->fNextLink = fNextLink;
	fNextLink = this;
	fPreviousLink = this;
	return rest;
}


// ROM 0x0014aae4 __ct__15TOptionIteratorFv
TOptionIterator::TOptionIterator()
{
	fNextLink = this;
	fPreviousLink = this;
	fHighBound = -1;
	fLowBound = -1;
	fCurrentIndex = -1;
	fOptionArray = nil;
	fCurrentOption = nil;
}


// ROM 0x0014ab30 __ct__15TOptionIteratorFP12TOptionArray
TOptionIterator::TOptionIterator(TOptionArray* itsOptionArray)
{
	Init(itsOptionArray, 0, itsOptionArray->fCount - 1);
}


// ROM 0x0014ab78 __ct__15TOptionIteratorFP12TOptionArraylT2
TOptionIterator::TOptionIterator(TOptionArray* itsOptionArray, ArrayIndex itsLowBound, ArrayIndex itsHighBound)
{
	Init(itsOptionArray, itsLowBound, itsHighBound);
}


// ROM 0x0014abc4 __dt__15TOptionIteratorFv
TOptionIterator::~TOptionIterator()
{
	if (fOptionArray != nil)
		fOptionArray->fIterator = RemoveFromList();
}


// The bounds clamped to the array: the high bound to the last option (-1
// for an empty array), the low bound to [0, high]; the iteration starts at
// the low bound.  (Init and InitBounds do this in line in the ROM.)
static inline void
ClampBounds(ArrayIndex count, ArrayIndex& lowBound, ArrayIndex& highBound)
{
	if (count < 1)
		highBound = -1;
	else
	{
		if (highBound < 0)
			highBound = 0;
		if (highBound >= count - 1)
			highBound = count - 1;
	}
	if (highBound < 0)
		lowBound = -1;
	else
	{
		if (lowBound < 0)
			lowBound = 0;
		if (lowBound >= highBound)
			lowBound = highBound;
	}
}


// ROM 0x0014ac08 Init__15TOptionIteratorFP12TOptionArraylT2
void
TOptionIterator::Init(TOptionArray* itsOptionArray, ArrayIndex itsLowBound, ArrayIndex itsHighBound)
{
	fNextLink = this;
	fPreviousLink = this;
	fOptionArray = itsOptionArray;
	fCurrentOption = nil;
	fOptionArray->fIterator = AppendToList(itsOptionArray->fIterator);
	ClampBounds(fOptionArray->fCount, itsLowBound, itsHighBound);
	fHighBound = itsHighBound;
	fLowBound = itsLowBound;
	fCurrentIndex = fLowBound;
	// the block's first option whatever the low bound (as Reset)
	fCurrentOption = (fCurrentIndex >= 0) ? (TOption*) fOptionArray->fArrayBlock : nil;
}


// ROM 0x0014ac5c InitBounds__15TOptionIteratorFlT1
void
TOptionIterator::InitBounds(ArrayIndex itsLowBound, ArrayIndex itsHighBound)
{
	ClampBounds(fOptionArray->fCount, itsLowBound, itsHighBound);
	fHighBound = itsHighBound;
	fLowBound = itsLowBound;
	fCurrentIndex = fLowBound;
	fCurrentOption = (fCurrentIndex >= 0) ? (TOption*) fOptionArray->fArrayBlock : nil;
}


// ROM 0x0014acb8 ResetBounds__15TOptionIteratorFv
// The whole array again.
void
TOptionIterator::ResetBounds()
{
	ArrayIndex count = fOptionArray->fCount;
	fHighBound = (count < 1) ? -1 : count - 1;
	fLowBound = (fHighBound >= 0) ? 0 : -1;
	fCurrentIndex = fLowBound;
	fCurrentOption = (fCurrentIndex >= 0) ? (TOption*) fOptionArray->fArrayBlock : nil;
}


// ROM 0x0014ace4 More__15TOptionIteratorFv
Boolean
TOptionIterator::More()
{
	return fOptionArray != nil && fCurrentIndex != -1;
}


// ROM 0x0014ad0c Reset__15TOptionIteratorFv
// Back to the low bound.  The option there is the first of the block
// whatever the low bound is - the ROM does not walk to it (a bug when the
// low bound is above nought; kept).
void
TOptionIterator::Reset()
{
	fCurrentIndex = fLowBound;
	fCurrentOption = (fCurrentIndex >= 0) ? (TOption*) fOptionArray->fArrayBlock : nil;
}


// ROM 0x0014ad2c DeleteArray__15TOptionIteratorFv
// The array is going: every iterator on its ring forgets it.
void
TOptionIterator::DeleteArray()
{
	if (fNextLink != fOptionArray->fIterator)
		fNextLink->DeleteArray();
	fOptionArray = nil;
	fCurrentOption = nil;
}


// ROM 0x0014ad60 Advance__15TOptionIteratorFv
void
TOptionIterator::Advance()
{
	if (fCurrentIndex < fHighBound)
	{
		fCurrentIndex++;
		fCurrentOption = NextOptionAfter(fCurrentOption);
	}
	else
	{
		fCurrentIndex = -1;
		fCurrentOption = nil;
	}
}


// ROM 0x0014ada0 RemoveOptionAt__15TOptionIteratorFl
// An option has gone from the array: every iterator on the ring moves its
// bounds and its place down over it, and finds its option again.
void
TOptionIterator::RemoveOptionAt(ArrayIndex theIndex)
{
	TOptionIterator* iter = this;
	TOptionArray* array;
	do
	{
		if (theIndex < iter->fLowBound)
			iter->fLowBound--;
		if (theIndex <= iter->fHighBound)
			iter->fHighBound--;
		if (theIndex <= iter->fCurrentIndex)
			iter->fCurrentIndex--;
		ArrayIndex current = iter->fCurrentIndex;
		if (current >= 0)
		{
			iter->fCurrentOption = (TOption*) iter->fOptionArray->fArrayBlock;
			for (ArrayIndex i = 0; i < current; i++)
				iter->fCurrentOption = NextOptionAfter(iter->fCurrentOption);
		}
		array = iter->fOptionArray;
		if (array != nil)
			iter = iter->fNextLink;
	} while (array != nil && iter != array->fIterator);
}


// ROM 0x0014ae3c InsertOptionAt__15TOptionIteratorFl
// An option has been put into the array: every iterator on the ring moves
// its bounds and its place up over it.  (A low bound at the index moves up,
// where RemoveOptionAt leaves one at the index alone.)
void
TOptionIterator::InsertOptionAt(ArrayIndex theIndex)
{
	TOptionIterator* iter = this;
	TOptionArray* array;
	do
	{
		if (theIndex <= iter->fLowBound)
			iter->fLowBound++;
		if (theIndex <= iter->fHighBound)
			iter->fHighBound++;
		if (theIndex <= iter->fCurrentIndex)
			iter->fCurrentIndex++;
		ArrayIndex current = iter->fCurrentIndex;
		if (current >= 0)
		{
			iter->fCurrentOption = (TOption*) iter->fOptionArray->fArrayBlock;
			for (ArrayIndex i = 0; i < current; i++)
				iter->fCurrentOption = NextOptionAfter(iter->fCurrentOption);
		}
		array = iter->fOptionArray;
		if (array != nil)
			iter = iter->fNextLink;
	} while (array != nil && iter != array->fIterator);
}


// ROM 0x0014aed8 CurrentIndex__15TOptionIteratorFv
ArrayIndex
TOptionIterator::CurrentIndex()
{
	return (fOptionArray == nil) ? -1 : fCurrentIndex;
}


// ROM 0x0014aeec FirstIndex__15TOptionIteratorFv
ArrayIndex
TOptionIterator::FirstIndex()
{
	Reset();
	return More() ? fCurrentIndex : -1;
}


// ROM 0x0014af18 NextIndex__15TOptionIteratorFv
ArrayIndex
TOptionIterator::NextIndex()
{
	Advance();
	return More() ? fCurrentIndex : -1;
}


// ROM 0x0014af44 CurrentOption__15TOptionIteratorFv
TOption*
TOptionIterator::CurrentOption()
{
	return (fOptionArray == nil) ? nil : fCurrentOption;
}


// ROM 0x0014af58 FindOption__15TOptionIteratorFUl
// The first option in the bounds with the label; nil if there is none.
TOption*
TOptionIterator::FindOption(ULong label)
{
	TOption* option = FirstOption();
	while (More())
	{
		if (option->fLabel == label)
			return option;
		option = NextOption();
	}
	return nil;
}


// ROM 0x0014afdc FirstOption__15TOptionIteratorFv
TOption*
TOptionIterator::FirstOption()
{
	FirstIndex();
	return fCurrentOption;
}


// ROM 0x0014aff8 NextOption__15TOptionIteratorFv
TOption*
TOptionIterator::NextOption()
{
	NextIndex();
	return fCurrentOption;
}


// ---------------------------------------------------------------------------
//	TOptionArray
// ---------------------------------------------------------------------------

// ROM 0x0014b06c __ct__12TOptionArrayFv
TOptionArray::TOptionArray()
{
	fArrayBlock = nil;
	fIterator = nil;
	fCount = 0;
	fIsShared = false;
	fSharedMemoryObject.CopyObject(0);
	fShadow = false;
}


// ROM 0x0014b0c4 __dt__12TOptionArrayFv
TOptionArray::~TOptionArray()
{
	if (fIterator != nil)
		fIterator->DeleteArray();
	if (fArrayBlock != nil)
		DisposePtr(fArrayBlock);
}


// ROM 0x0014b110 Init__12TOptionArrayFv
NewtonErr
TOptionArray::Init()
{
	fArrayBlock = NewPtr(0);
	return MemError();
}


// ROM 0x0014b134 Init__12TOptionArrayFUl
NewtonErr
TOptionArray::Init(ULong initialSize)
{
	fArrayBlock = NewPtr(initialSize);
	return MemError();
}


// ROM 0x0014b158 Init__12TOptionArrayFUlT1
// A copy of the options in another task's shared-memory object, which this
// array is then the shadow of.
NewtonErr
TOptionArray::Init(TObjectId sharedId, ULong optionCount)
{
	NewtonErr err = CopyFromShared(sharedId, optionCount);
	if (err == noErr)
	{
		fSharedMemoryObject.CopyObject(sharedId);
		fShadow = true;
	}
	return err;
}


// ROM 0x0014b194 Init__12TOptionArrayFP15TSubArrayOption
// A copy of the options a sub-array option holds.
NewtonErr
TOptionArray::Init(TSubArrayOption* array)
{
	fCount = array->fCount;
	Size count = array->Length() - (sizeof(TSubArrayOption) - sizeof(TOption));
	fArrayBlock = NewPtr(count);
	if (fArrayBlock == nil)
		// ROM BUG: the ROM answers r7 here, which nothing set - whatever
		// its caller had in it.  The host answers the memory error.
		return MemError();
	BlockMove(array + 1, fArrayBlock, count);
	return noErr;
}


// ROM 0x0014b1ec Reset__12TOptionArrayFv
// Every option's processed flag and result cleared, and the array
// no longer shared.
void
TOptionArray::Reset()
{
	TOptionIterator iter(this);
	for (TOption* option = iter.FirstOption(); option != nil; option = iter.NextOption())
		option->Reset();
	if (fIsShared)
		UnShare();
}


// ROM 0x0014b268 CopyOptionAt__12TOptionArrayFlP7TOption
// Copy the option at index into `copy`, whose length says how much room it
// has: opNotFound if there is no such option, opTruncated if it has more
// data than the room.  ROM BUG: when it is truncated the header is copied
// too, so `copy`'s length becomes the source's although only the room's
// worth of data came with it; kept.
NewtonErr
TOptionArray::CopyOptionAt(ArrayIndex index, TOption* copy)
{
	TOption* source = OptionAt(index);
	if (source == nil)
		return opNotFound;
	NewtonErr result = noErr;
	if (source->fLength > copy->fLength)
		result = opTruncated;
	else if (source->fLength < copy->fLength)
		copy->fLength = source->fLength;
	BlockMove(source, copy, copy->fLength + sizeof(TOption));
	// ROM BUG: on success the ROM answers r5, which it never set (its
	// caller's r5); the host answers noErr.
	return result;
}


// ROM 0x0014b2bc OptionAt__12TOptionArrayFl
// The option at index, walked to from the start of the block; no check of
// the index against the count (nil only for an array with no block).
TOption*
TOptionArray::OptionAt(ArrayIndex index)
{
	TOption* option = (TOption*) fArrayBlock;
	for (ArrayIndex i = 0; i < index; i++)
		option = NextOptionAfter(option);
	return option;
}


// ROM 0x0014b2ec RemoveOptionAt__12TOptionArrayFl
NewtonErr
TOptionArray::RemoveOptionAt(ArrayIndex index)
{
	if (fIsShared)
		UnShare();
	if (fCount == 0)
		return noErr;

	TOption* option = (TOption*) fArrayBlock;
	for (ArrayIndex i = 0; i < index; i++)
		option = NextOptionAfter(option);
	ULong step = OptionStep(option->fLength);
	Ptr next = (Ptr) option + step;
	Size size = GetPtrSize(fArrayBlock);
	if (next < fArrayBlock + size)
		BlockMove(next, option, (fArrayBlock + size) - next);
	Ptr block = ReallocPtr(fArrayBlock, size - step);
	if (block == nil)
		return MemError();
	fArrayBlock = block;
	fCount--;
	if (fIterator != nil)
		fIterator->RemoveOptionAt(index);
	return noErr;
}


// ROM 0x0014b3d0 InsertOptionAt__12TOptionArrayFlP7TOption
// A copy of the option put in at index (the end if index is past it).  The
// whole rounded step is copied from `opt`, so up to a step's rounding past
// its data is read (the ROM's three bytes; the option classes are sized so
// that is inside them).
NewtonErr
TOptionArray::InsertOptionAt(ArrayIndex index, TOption* opt)
{
	if (fIsShared)
		UnShare();
	if (index > fCount)
		index = fCount;
	Size size = GetPtrSize(fArrayBlock);
	ULong step = OptionStep(opt->fLength);
	Ptr block = ReallocPtr(fArrayBlock, size + step);
	if (block == nil)
		return MemError();
	fArrayBlock = block;
	Ptr where;
	if (index < fCount)
	{
		where = block;
		for (ArrayIndex i = 0; i < index; i++)
			where = (Ptr) NextOptionAfter((TOption*) where);
		BlockMove(where, where + step, (block + size) - where);
	}
	else
		where = block + size;
	BlockMove(opt, where, step);
	fCount++;
	if (fIterator != nil)
		fIterator->InsertOptionAt(index);
	return noErr;
}


// ROM 0x0014b718 InsertVarOptionAt__12TOptionArrayFlP7TOptionPvUl
// An option whose last varLen bytes of data are somewhere else: the header
// and the rest of the data come from `opt`, the tail from varData.
NewtonErr
TOptionArray::InsertVarOptionAt(ArrayIndex index, TOption* opt, void* varData, ULong varLen)
{
	if (fIsShared)
		UnShare();
	if (index > fCount)
		index = fCount;
	Size size = GetPtrSize(fArrayBlock);
	ULong step = OptionStep(opt->fLength);
	Ptr block = ReallocPtr(fArrayBlock, size + step);
	if (block == nil)
		return MemError();
	fArrayBlock = block;
	Ptr where;
	if (index < fCount)
	{
		where = block;
		for (ArrayIndex i = 0; i < index; i++)
			where = (Ptr) NextOptionAfter((TOption*) where);
		BlockMove(where, where + step, (block + size) - where);
	}
	else
		where = block + size;
	Size fixedLength = opt->fLength - varLen;
	BlockMove(opt, where, fixedLength + sizeof(TOption));
	BlockMove(varData, where + fixedLength + sizeof(TOption), varLen);
	fCount++;
	if (fIterator != nil)
		fIterator->InsertOptionAt(index);
	return noErr;
}


// ROM 0x0014b85c InsertSubArrayAt__12TOptionArrayFlP12TOptionArray
// Another array's options put in at index as one sub-array option.
NewtonErr
TOptionArray::InsertSubArrayAt(ArrayIndex index, TOptionArray* subArray)
{
	if (fIsShared)
		UnShare();
	ULong size = GetPtrSize(subArray->fArrayBlock);
	TSubArrayOption option(size, subArray->fCount);
	return InsertVarOptionAt(index, &option, subArray->fArrayBlock, size);
}


// ROM 0x0014b4cc Merge__12TOptionArrayFP12TOptionArray
// Another array's options added at the end.  The iterators on this array
// are not told (their high bounds stay where they were).
NewtonErr
TOptionArray::Merge(TOptionArray* optionArray)
{
	if (fIsShared)
		UnShare();
	Size size = GetPtrSize(fArrayBlock);
	Size addSize = GetPtrSize(optionArray->fArrayBlock);
	Ptr block = ReallocPtr(fArrayBlock, size + addSize);
	if (block == nil)
		return MemError();
	fArrayBlock = block;
	BlockMove(optionArray->fArrayBlock, block + size, addSize);
	fCount += optionArray->fCount;
	return noErr;
}


// ROM 0x0014b558 RemoveAllOptions__12TOptionArrayFv
// (The iterators are not told.)
NewtonErr
TOptionArray::RemoveAllOptions()
{
	NewtonErr err = noErr;
	if (!fIsShared || (err = UnShare()) == noErr)
	{
		Ptr block = ReallocPtr(fArrayBlock, 0);
		if (block == nil)
			err = MemError();
		else
		{
			fArrayBlock = block;
			fCount = 0;
		}
	}
	return err;
}


// ROM 0x0014b5bc CopyFromShared__12TOptionArrayFUlT1
// This array becomes a copy of the `count` options in a shared-memory
// object.  (The count is taken before the copy is known to have worked.)
NewtonErr
TOptionArray::CopyFromShared(TObjectId sharedId, ULong count)
{
	fCount = count;
	TUSharedMem mem;
	mem.fId = sharedId;
	ULong size;
	NewtonErr err = mem.GetSize(&size, nil);
	if (err == noErr)
	{
		Ptr block = ReallocPtr(fArrayBlock, size);
		if (block == nil)
			err = MemError();
		else
		{
			fArrayBlock = block;
			ULong copied;
			err = mem.CopyFromShared(&copied, block, size, 0, nil);
		}
	}
	return err;
}


// ROM 0x0014b658 CopyToShared__12TOptionArrayFUl
NewtonErr
TOptionArray::CopyToShared(TObjectId sharedId)
{
	TUSharedMem mem;
	mem.fId = sharedId;
	return mem.CopyToShared(fArrayBlock, GetPtrSize(fArrayBlock), 0, nil);
}


// ROM 0x0014b6b4 MakeShared__12TOptionArrayFUl
// The array's block handed out as a shared-memory object (another task
// reads it with CopyFromShared).
NewtonErr
TOptionArray::MakeShared(ULong permissions)
{
	if (fIsShared)
		return noErr;
	NewtonErr err = fSharedMemoryObject.Init();
	if (err == noErr)
		err = fSharedMemoryObject.SetBuffer(fArrayBlock, GetPtrSize(fArrayBlock), permissions);
	if (err == noErr)
		fIsShared = true;
	return err;
}


// ROM 0x0014b8c4 ShadowCopyBack__12TOptionArrayFv
// A shadow's options written back into the object it was copied from.
NewtonErr
TOptionArray::ShadowCopyBack()
{
	if (fShadow)
		return CopyToShared(fSharedMemoryObject.fId);
	return noErr;
}


// ROM 0x0014b8f4 UnShare__12TOptionArrayFv
NewtonErr
TOptionArray::UnShare()
{
	if (fIsShared)
	{
		fSharedMemoryObject.DestroyObject();
		fIsShared = false;
	}
	return noErr;
}
