/*
	File:		frames/Munger.cpp

	Contains:	Destructive editing of arrays, binaries and strings
				(ArrayMunger, BinaryMunger, StrMunger: replace a stretch of
				one object by a stretch of another), and the array functions
				built on it: ArrayRemoveCount, ArrayRemove, ArrayInsert,
				ArrayPosition and the built-ins over them (the objects.h
				functions "in builtins.c").

	Reconstructed from the MP2100 D ROM (0x002eee68-0x002f1318); each
	function cites its origin.  NOT YET RECONSTRUCTED: TRichString (strings
	with ink runs), through which the ROM's StrMunger edits; here a string
	is its UniChars up to the terminating 0.
*/

#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "NSErrors.h"

#include <string.h>


// ROM 0x002efe38 ArrayMunger__FRC6RefVarlT2T1N22
// Replace a1[a1start, a1start + a1count) by a2[a2start, a2start + a2count)
// (a count of -1: to the end; a2 nil: remove).  The ranges are clipped to
// the arrays; the two must be different objects.
void
ArrayMunger(RefArg a1, long a1start, long a1count, RefArg a2, long a2start, long a2count)
{
	if ((ObjectFlags(a1) & (kObjSlotted | kObjFrame)) != kObjSlotted)
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, a1);
	if ((Ref) a2 != NILREF && (ObjectFlags(a2) & (kObjSlotted | kObjFrame)) != kObjSlotted)
		ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrNil, a2);
	if (EQRef(a1, a2))
		ThrowExFramesWithBadValue(kNSErrObjectsNotDistinct, a1);
	if ((ObjectFlags(a1) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, a1);
	long a1length = Length(a1);
	if (a1count == -1)
		a1count = a1length - a1start;
	long a2length = 0;
	if ((Ref) a2 == NILREF)
		a2start = a2count = 0;
	else
	{
		a2length = Length(a2);
		if (a2count == -1)
			a2count = a2length - a2start;
	}
	if (a1start < 0)
		a1start = 0;
	else if (a1start > a1length)
		a1start = a1length;
	long a1rest = a1length - a1start;
	if (a1count < 0)
		a1count = 0;
	else if (a1count > a1rest)
		a1count = a1rest;
	if (a2start < 0)
		a2start = 0;
	else if (a2start > a2length)
		a2start = a2length;
	if (a2count < 0)
		a2count = 0;
	else if (a2count > a2length - a2start)
		a2count = a2length - a2start;
	long delta = a2count - a1count;
	if (delta > 0)
		SetLength(a1, a1length + delta);
	Ref* slots1 = Slots(a1);
	Ref* slots2 = ((Ref) a2 == NILREF) ? nil : Slots(a2);
	if (delta != 0)
		memmove(slots1 + a1start + a2count, slots1 + a1start + a1count, (a1rest - a1count) * sizeof(Ref));	// (the ROM's memcpy moves within the object)
	if (a2count != 0)
		memmove(slots1 + a1start, slots2 + a2start, a2count * sizeof(Ref));
	if (delta < 0)
		SetLength(a1, a1length + delta);
}


// ROM 0x002f0e60 BinaryMunger__FRC6RefVarlT2T1N22
// The same for the bytes of binaries.
void
BinaryMunger(RefArg a1, long a1start, long a1count, RefArg a2, long a2start, long a2count)
{
	if ((ObjectFlags(a1) & kObjSlotted) != 0)
		ThrowBadTypeWithFrameData(kNSErrNotABinaryObject, a1);
	if ((Ref) a2 != NILREF && (ObjectFlags(a2) & kObjSlotted) != 0)
		ThrowBadTypeWithFrameData(kNSErrNotABinaryObjectOrNil, a2);
	if (EQRef(a1, a2))
		ThrowExFramesWithBadValue(kNSErrObjectsNotDistinct, a1);
	if ((ObjectFlags(a1) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, a1);
	long a1length = Length(a1);
	if (a1count == -1)
		a1count = a1length - a1start;
	long a2length = 0;
	if ((Ref) a2 == NILREF)
		a2start = a2count = 0;
	else
	{
		a2length = Length(a2);
		if (a2count == -1)
			a2count = a2length - a2start;
	}
	if (a1start < 0)
		a1start = 0;
	else if (a1start > a1length)
		a1start = a1length;
	long a1rest = a1length - a1start;
	if (a1count < 0)
		a1count = 0;
	else if (a1count > a1rest)
		a1count = a1rest;
	if (a2start < 0)
		a2start = 0;
	else if (a2start > a2length)
		a2start = a2length;
	if (a2count < 0)
		a2count = 0;
	else if (a2count > a2length - a2start)
		a2count = a2length - a2start;
	long delta = a2count - a1count;
	if (delta > 0)
		SetLength(a1, a1length + delta);
	char* data1 = BinaryData(a1);
	char* data2 = ((Ref) a2 == NILREF) ? nil : BinaryData(a2);
	if (delta != 0)
		memmove(data1 + a1start + a2count, data1 + a1start + a1count, a1rest - a1count);
	if (a2count != 0)
		memmove(data1 + a1start, data2 + a2start, a2count);
	if (delta < 0)
		SetLength(a1, a1length + delta);
}


// ROM 0x002eee68 StrMunger__FRC6RefVarlT2T1N22
// The same for the characters of strings (counts in characters, the
// terminating 0 kept).
void
StrMunger(RefArg s1, long s1start, long s1count, RefArg s2, long s2start, long s2count)
{
	if (!IsString(s1))
		ThrowBadTypeWithFrameData(kNSErrNotAString, s1);
	if ((Ref) s2 != NILREF && !IsString(s2))
		ThrowBadTypeWithFrameData(kNSErrNotAStringOrNil, s2);
	if (EQRef(s1, s2))
		ThrowExFramesWithBadValue(kNSErrObjectsNotDistinct, s1);
	if ((ObjectFlags(s1) & kObjReadOnly) != 0)
		ThrowExFramesWithBadValue(kNSErrObjectReadOnly, s1);
	long s1length = Length(s1) / (long) sizeof(UniChar) - 1;
	if (s1count == -1)
		s1count = s1length - s1start;
	if (s1start < 0)
		s1start = 0;
	else if (s1start > s1length)
		s1start = s1length;
	if (s1count < 0)
		s1count = 0;
	else if (s1count > s1length - s1start)
		s1count = s1length - s1start;
	long s2length = 0;
	if ((Ref) s2 == NILREF)
		s2start = s2count = 0;
	else
	{
		s2length = Length(s2) / (long) sizeof(UniChar) - 1;
		if (s2count == -1)
			s2count = s2length - s2start;
		if (s2start < 0)
			s2start = 0;
		else if (s2start > s2length)
			s2start = s2length;
		if (s2count < 0)
			s2count = 0;
		else if (s2count > s2length - s2start)
			s2count = s2length - s2start;
	}
	long delta = s2count - s1count;
	if (delta > 0)
		SetLength(s1, (s1length + delta + 1) * sizeof(UniChar));
	UniChar* text1 = (UniChar*) BinaryData(s1);
	UniChar* text2 = ((Ref) s2 == NILREF) ? nil : (UniChar*) BinaryData(s2);
	if (delta != 0)
		memmove(text1 + s1start + s2count, text1 + s1start + s1count, (s1length - s1start - s1count + 1) * sizeof(UniChar));
	if (s2count != 0)
		memmove(text1 + s1start, text2 + s2start, s2count * sizeof(UniChar));
	if (delta < 0)
		SetLength(s1, (s1length + delta + 1) * sizeof(UniChar));
}


// ROM 0x002ee75c ArrayRemoveCount__FRC6RefVarlT2
void
ArrayRemoveCount(RefArg array, FastInt start, FastInt removeCount)
{
	ArrayMunger(array, start, removeCount, RefVar(NILREF), 0, 0);
}


// ROM 0x002ee7b0 ArrayRemove__FRC6RefVarT1
// Remove the first slot EQ to element; whether there was one.
Boolean
ArrayRemove(RefArg array, RefArg element)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long length = Length(array);
	for (long i = 0; i < length; i++)
	{
		if (EQRef(element, GetArraySlotRef(array, i)))
		{
			ArrayRemoveCount(array, i, 1);
			return true;
		}
	}
	return false;
}


// ROM 0x002f1218 ArrayInsert__FRC6RefVarT1l
void
ArrayInsert(RefArg array, RefArg element, long index)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long length = Length(array);
	if (index < 0 || index > length)
	{
		RefVar value(MAKEINT(index));
		ThrowExFramesWithBadValue(kNSErrOutOfBounds, value);
	}
	SetLength(array, length + 1);
	Ref* slots = Slots(array);
	memmove(slots + index + 1, slots + index, (length - index) * sizeof(Ref));
	slots[index] = element;
}


// ROM 0x002ef0d0 ArrayPosition__FRC6RefVarT1lT1
// The index from start of the first element EQ to item - or for which
// test(item, element) is true - else -1.
long
ArrayPosition(RefArg array, RefArg item, long start, RefArg test)
{
	long length = Length(array);
	if (length == 0)
		return -1;
	if (start < 0)
		start = 0;
	else if (start > length - 1)
		start = length - 1;
	if ((Ref) test == NILREF)
	{
		for ( ; start < length; start++)
			if (EQRef(item, GetArraySlotRef(array, start)))
				break;
	}
	else
	{
		RefVar args(AllocateArray(RSSYMarray, 2));
		SetArraySlotRef(args, 0, item);
		for ( ; start < length; start++)
		{
			SetArraySlotRef(args, 1, GetArraySlotRef(array, start));
			if (DoBlock(test, args) != NILREF)
				break;
		}
	}
	return (start != length) ? start : -1;
}


/* -------------------------------------------------------------------------------
	The built-ins
------------------------------------------------------------------------------- */

// an integer argument, or nil for -1 (the whole rest)
static inline long
CountArg(RefArg count)
{
	return ((Ref) count == NILREF) ? -1 : RINT(count);
}


// ROM 0x002f0050 FArrayMunger
Ref
FArrayMunger(RefArg /*rcvr*/, RefArg a1, RefArg a1start, RefArg a1count, RefArg a2, RefArg a2start, RefArg a2count)
{
	long count2 = CountArg(a2count);
	long start2 = RINT(a2start);
	long count1 = CountArg(a1count);
	long start1 = RINT(a1start);
	ArrayMunger(a1, start1, count1, a2, start2, count2);
	return a1;
}


// ROM 0x002f106c FBinaryMunger
Ref
FBinaryMunger(RefArg /*rcvr*/, RefArg a1, RefArg a1start, RefArg a1count, RefArg a2, RefArg a2start, RefArg a2count)
{
	long count2 = CountArg(a2count);
	long start2 = RINT(a2start);
	long count1 = CountArg(a1count);
	long start1 = RINT(a1start);
	BinaryMunger(a1, start1, count1, a2, start2, count2);
	return a1;
}


// ROM 0x002ef004 FStrMunger
Ref
FStrMunger(RefArg /*rcvr*/, RefArg s1, RefArg s1start, RefArg s1count, RefArg s2, RefArg s2start, RefArg s2count)
{
	long count2 = CountArg(s2count);
	long start2 = RINT(s2start);
	long count1 = CountArg(s1count);
	long start1 = RINT(s1start);
	StrMunger(s1, start1, count1, s2, start2, count2);
	return s1;
}


// ROM 0x002f011c FArray
// An array of length slots, each initialValue.
Ref
FArray(RefArg /*rcvr*/, RefArg length, RefArg initialValue)
{
	long n = RINT(length);
	RefVar array(AllocateArray(RSSYMarray, n));
	if ((Ref) initialValue != NILREF)
	{
		Ref* slots = Slots(array);
		for (long i = 0; i < n; i++)
			slots[i] = initialValue;
	}
	return array;
}


// ROM 0x002ef9c4 FSetContains
// The index of target in the array, nil if absent.
Ref
FSetContains(RefArg /*rcvr*/, RefArg array, RefArg target)
{
	if (!IsArray(array))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, array);
	long length = Length(array);
	for (long i = 0; i < length; i++)
		if (EQRef(target, GetArraySlotRef(array, i)))
			return MAKEINT(i);
	return NILREF;
}


// ROM 0x002f01b4 FSetAdd
// Add member to the array (unless unique and it is there already, when
// nil is answered).
Ref
FSetAdd(RefArg rcvr, RefArg members, RefArg member, RefArg unique)
{
	if (!IsArray(members))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, members);
	if ((Ref) unique != NILREF && FSetContains(rcvr, members, member) != NILREF)
		return NILREF;
	AddArraySlot(members, member);
	return members;
}


// ROM 0x002f07cc FSetRemove
Ref
FSetRemove(RefArg /*rcvr*/, RefArg members, RefArg member)
{
	if (!IsArray(members))
		ThrowBadTypeWithFrameData(kNSErrNotAnArray, members);
	if (!ArrayRemove(members, member))
		return NILREF;
	return members;
}


// ROM 0x002ef524 FArrayRemoveCount
Ref
FArrayRemoveCount(RefArg /*rcvr*/, RefArg array, RefArg start, RefArg count)
{
	long n = RINT(count);
	long i = RINT(start);
	ArrayRemoveCount(array, i, n);
	return NILREF;
}


// ROM 0x002f12d0 FArrayInsert
Ref
FArrayInsert(RefArg /*rcvr*/, RefArg array, RefArg element, RefArg index)
{
	ArrayInsert(array, element, RINT(index));
	return array;
}


// ROM 0x002ef220 FArrayPos
Ref
FArrayPos(RefArg /*rcvr*/, RefArg array, RefArg item, RefArg start, RefArg test)
{
	long from = ((Ref) start == NILREF) ? -1 : RINT(start);
	long position = ArrayPosition(array, item, from, test);
	return (position == -1) ? NILREF : MAKEINT(position);
}


#define NATIVE(symbol, fn, n)	RegisterNativeFunction(symbol, (void*) (NativeFn##n) fn, n)

void
RegisterMungerNatives(void)
{
	NATIVE("FArrayMunger", FArrayMunger, 6);
	NATIVE("FBinaryMunger", FBinaryMunger, 6);
	NATIVE("FStrMunger", FStrMunger, 6);
	NATIVE("FArray", FArray, 2);
	NATIVE("FSetContains", FSetContains, 2);
	NATIVE("FSetAdd", FSetAdd, 3);
	NATIVE("FSetRemove", FSetRemove, 2);
	NATIVE("FArrayRemoveCount", FArrayRemoveCount, 3);
	NATIVE("FArrayInsert", FArrayInsert, 3);
	NATIVE("FArrayPos", FArrayPos, 4);
}
