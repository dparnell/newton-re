/*
	File:		frames/Builtins.cpp

	Contains:	The core NewtonScript built-in functions (the ROM's F...
				functions the built-in functions frame maps names to):
				arithmetic and comparison, booleans and bits, objects and
				their slots, classes and predicates, variables, calling and
				sending, exceptions, the foreach iterator, symbols and
				characters, globals.  RegisterBuiltinNatives binds them to
				the ROM's function objects (NativeFunctions.h).

	Reconstructed from the MP2100 D ROM (0x002900ec-0x00295100 mostly);
	each function cites its origin.  A native function takes the receiver
	then its arguments and answers a Ref.  NOT YET RECONSTRUCTED here: the
	string functions (TRichString: strings with ink), Stringer, the printer
	(Print, Write), the REP's break loop, random numbers, Compile, the soup
	entry functions.
*/

#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "Compiler.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "OSErrors.h"

#include <math.h>
#include <fenv.h>
#include <string.h>

#define NSBOOL(b)	((b) ? TRUEREF : NILREF)

static char gFramesExceptionName[0x80];			// 0x0c102b40 gFramesExceptionName: the name FThrow throws under


/* -------------------------------------------------------------------------------
	Numbers
------------------------------------------------------------------------------- */

// ROM 0x002900ec NumberAdd__FRC6RefVarT1
Ref
NumberAdd(RefArg a, RefArg b)
{
	return MakeReal(CoerceToDouble(a) + CoerceToDouble(b));
}


// ROM 0x00291864 NumberSubtract__FRC6RefVarT1
Ref
NumberSubtract(RefArg a, RefArg b)
{
	return MakeReal(CoerceToDouble(a) - CoerceToDouble(b));
}


// ROM 0x00293800 NumberMultiply__FRC6RefVarT1
Ref
NumberMultiply(RefArg a, RefArg b)
{
	return MakeReal(CoerceToDouble(a) * CoerceToDouble(b));
}


// ROM 0x00293d10 NumberDivide__FRC6RefVarT1
Ref
NumberDivide(RefArg a, RefArg b)
{
	return MakeReal(CoerceToDouble(a) / CoerceToDouble(b));
}


// ROM 0x00290230 FAdd
Ref
FAdd(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return MAKEINT(RVALUE(a) + RVALUE(b));
	return NumberAdd(a, b);
}


// ROM 0x00293578 FSubtract
Ref
FSubtract(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return MAKEINT(RVALUE(a) - RVALUE(b));
	return NumberSubtract(a, b);
}


// ROM 0x00293a88 FMultiply
Ref
FMultiply(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
		return MAKEINT(RVALUE(a) * RVALUE(b));
	return NumberMultiply(a, b);
}


// ROM 0x0029434c FDivide
// Integers divide to an integer when it comes out exact, else a real.
Ref
FDivide(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	if (ISINT(a) && ISINT(b))
	{
		long divisor = RVALUE(b);
		long dividend = RVALUE(a);
		if (divisor != 0 && dividend % divisor == 0)
			return MAKEINT(dividend / divisor);
		return MakeReal((double) dividend / (double) divisor);
	}
	return NumberDivide(a, b);
}


// ROM 0x00294660 FDiv
Ref
FDiv(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	long divisor = RINT(b);
	long dividend = RINT(a);
	if (divisor == 0)
		Throw(exDivideByZero, nil, nil);					// DEVIATION: the ROM's __rt_sdiv traps
	return MAKEINT(dividend / divisor);
}


// ROM 0x0029026c FMod
Ref
FMod(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	long divisor = RINT(b);
	long dividend = RINT(a);
	if (divisor == 0)
		Throw(exDivideByZero, nil, nil);					// DEVIATION: as FDiv
	return MAKEINT(dividend % divisor);
}


// ROM 0x00290648 FNegate
Ref
FNegate(RefArg /*rcvr*/, RefArg a)
{
	if (ISINT(a))
		return MAKEINT(-RVALUE(a));
	return MakeReal(-CoerceToDouble(a));
}


// ROM 0x00290828 FAbs
Ref
FAbs(RefArg /*rcvr*/, RefArg a)
{
	if (ISINT(a))
	{
		long v = RVALUE(a);
		return MAKEINT(v < 0 ? -v : v);
	}
	return MakeReal(fabs(CoerceToDouble(a)));
}


// ROM 0x00290964 FSignum
Ref
FSignum(RefArg /*rcvr*/, RefArg a)
{
	if (ISINT(a))
	{
		long v = RVALUE(a);
		return MAKEINT(v < 0 ? -1 : v > 0 ? 1 : 0);
	}
	double d = CoerceToDouble(a);
	return MAKEINT(d < 0 ? -1 : d > 0 ? 1 : 0);
}


// ROM 0x00290b9c FCeiling
Ref
FCeiling(RefArg /*rcvr*/, RefArg a)
{
	if (ISINT(a))
		return a;
	return MAKEINT((long) ceil(CoerceToDouble(a)));
}


// ROM 0x00290d54 FFloor
Ref
FFloor(RefArg /*rcvr*/, RefArg a)
{
	if (ISINT(a))
		return a;
	return MAKEINT((long) floor(CoerceToDouble(a)));
}


// ROM 0x00290e80 FReal
Ref
FReal(RefArg /*rcvr*/, RefArg a)
{
	return MakeReal(CoerceToDouble(a));
}


// numbers compared: integers as such, else as doubles; characters by code;
// strings by their characters (NOT YET RECONSTRUCTED: TRichString's
// comparison); anything else is an error.  Answers <0, 0, >0.
static long
CompareForOrder(RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return RVALUE(ra) < RVALUE(rb) ? -1 : RVALUE(ra) > RVALUE(rb) ? 1 : 0;
	int numbers = (ISREAL(ra) ? 1 : 0) + (ISREAL(rb) ? 1 : 0) + (ISINT(ra) ? 1 : 0) + (ISINT(rb) ? 1 : 0);
	if (numbers >= 2)
	{
		double da = CoerceToDouble(a), db = CoerceToDouble(b);
		return da < db ? -1 : da > db ? 1 : 0;
	}
	if (ISCHAR(ra) && ISCHAR(rb))
	{
		UniChar ca = RCHAR(ra), cb = RCHAR(rb);
		return ca < cb ? -1 : ca > cb ? 1 : 0;
	}
	if (IsString(a) && IsString(b))
	{
		const UniChar* sa = GetCString(a);
		const UniChar* sb = GetCString(b);
		for (long i = 0; ; i++)
		{
			if (sa[i] != sb[i])
				return sa[i] < sb[i] ? -1 : 1;
			if (sa[i] == 0)
				return 0;
		}
	}
	Throw(exFrames, (void*) kNSErrBadArgs, nil);
	return 0;
}


// ROM 0x0029493c FLessThan
Ref
FLessThan(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(CompareForOrder(a, b) < 0);
}


// ROM 0x00294b4c FLessOrEqual
Ref
FLessOrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(CompareForOrder(a, b) <= 0);
}


// ROM 0x00294d60 FGreaterThan
Ref
FGreaterThan(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(CompareForOrder(a, b) > 0);
}


// ROM 0x00294f70 FGreaterOrEqual
Ref
FGreaterOrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(CompareForOrder(a, b) >= 0);
}


// the = of NewtonScript: identical refs, numbers by value, else EQ
static Boolean
NumberOrRefEqual(RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (((ra | rb) & 1) == 0)
		return ra == rb;
	Boolean aReal = ISREAL(ra), bReal = ISREAL(rb);
	if (aReal && bReal)
		return CDouble(a) == CDouble(b);
	if (aReal || bReal)
	{
		if ((ra & 3) != 0 && (rb & 3) != 0)
			return false;
		return CoerceToDouble(a) == CoerceToDouble(b);
	}
	return EQRef(ra, rb) != 0;
}


// ROM 0x002946dc FEqual
Ref
FEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(NumberOrRefEqual(a, b));
}


// ROM 0x0029480c FUnorderedLessOrGreater
Ref
FUnorderedLessOrGreater(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL(!NumberOrRefEqual(a, b));
}


// the IEEE relation of two numbers as doubles (the ROM's relation()):
// 0 less, 1 greater, 2 equal, 3 unordered (a NaN)
static int
NumberRelation(RefArg a, RefArg b)
{
	double da = CoerceToDouble(a);
	double db = CoerceToDouble(b);
	if (da < db)
		return 0;
	if (da > db)
		return 1;
	if (da == db)
		return 2;
	return 3;
}


// ROM 0x002902c4 FUnorderedOrGreater
Ref
FUnorderedOrGreater(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) > RVALUE(rb));
	int relation = NumberRelation(a, b);
	return NSBOOL(relation != 0 && relation != 2);
}


// ROM 0x00290354 FUnorderedGreaterOrEqual
Ref
FUnorderedGreaterOrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) >= RVALUE(rb));
	return NSBOOL(NumberRelation(a, b) != 0);
}


// ROM 0x002903e4 FUnorderedOrLess
Ref
FUnorderedOrLess(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) < RVALUE(rb));
	int relation = NumberRelation(a, b);
	return NSBOOL(relation != 1 && relation != 2);
}


// ROM 0x00290474 FUnorderedLessOrEqual
Ref
FUnorderedLessOrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) <= RVALUE(rb));
	return NSBOOL(NumberRelation(a, b) != 1);
}


// ROM 0x00290504 FUnorderedOrEqual
Ref
FUnorderedOrEqual(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) == RVALUE(rb));
	int relation = NumberRelation(a, b);
	return NSBOOL(relation == 2 || relation == 3);
}


// ROM 0x00295184 FUnordered
Ref
FUnordered(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NILREF;
	return NSBOOL(NumberRelation(a, b) == 3);
}


// ROM 0x002951fc FLessOrGreater
Ref
FLessOrGreater(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return NSBOOL(RVALUE(ra) != RVALUE(rb));
	return NSBOOL(NumberRelation(a, b) < 2);
}


// ROM 0x0029528c FLessEqualOrGreater
Ref
FLessEqualOrGreater(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	Ref ra = a, rb = b;
	if (ISINT(ra) && ISINT(rb))
		return TRUEREF;
	return NSBOOL(NumberRelation(a, b) != 3);
}


// ROM 0x00290594 FMin
Ref
FMin(RefArg rcvr, RefArg a, RefArg b)
{
	return (FLessOrEqual(rcvr, a, b) != NILREF) ? (Ref) a : (Ref) b;
}


// ROM 0x002905c0 FMax
Ref
FMax(RefArg rcvr, RefArg a, RefArg b)
{
	return (FGreaterOrEqual(rcvr, a, b) != NILREF) ? (Ref) a : (Ref) b;
}


/* -------------------------------------------------------------------------------
	Booleans and bits
------------------------------------------------------------------------------- */

// ROM 0x00294574 FNot
Ref
FNot(RefArg /*rcvr*/, RefArg a)
{
	return NSBOOL((Ref) a == NILREF);
}


// ROM 0x0029452c FBoolAnd
Ref
FBoolAnd(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL((Ref) a != NILREF && (Ref) b != NILREF);
}


// ROM 0x00294550 FBoolOr
Ref
FBoolOr(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return NSBOOL((Ref) a != NILREF || (Ref) b != NILREF);
}


// ROM 0x0029440c FBitAnd
Ref
FBitAnd(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEINT(RINT(a) & RINT(b));
}


// ROM 0x0029445c FBitOr
Ref
FBitOr(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEINT(RINT(a) | RINT(b));
}


// ROM 0x002944ac FBitXor
Ref
FBitXor(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEINT(RINT(a) ^ RINT(b));
}


// ROM 0x002944fc FBitNot
Ref
FBitNot(RefArg /*rcvr*/, RefArg a)
{
	return MAKEINT(~RINT(a));
}


// ROM 0x002942fc FLShift
Ref
FLShift(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEINT((long) ((ULong) RINT(a) << (RINT(b) & 0xff)));
}


// ROM 0x002943bc FRShift
Ref
FRShift(RefArg /*rcvr*/, RefArg a, RefArg b)
{
	return MAKEINT(RINT(a) >> (RINT(b) & 0xff));
}


/* -------------------------------------------------------------------------------
	Objects
------------------------------------------------------------------------------- */

// ROM 0x002907e8 FLength
Ref
FLength(RefArg /*rcvr*/, RefArg obj)
{
	return MAKEINT(Length(obj));
}


// ROM 0x002918d0 FSetLength
Ref
FSetLength(RefArg /*rcvr*/, RefArg obj, RefArg length)
{
	SetLength(obj, RINT(length));
	return obj;
}


// ROM 0x00290934 FClassOf
Ref
FClassOf(RefArg /*rcvr*/, RefArg obj)
{
	return ClassOf(obj);
}


// ROM 0x00290a20 FSetClass
Ref
FSetClass(RefArg /*rcvr*/, RefArg obj, RefArg theClass)
{
	SetClass(obj, theClass);
	return obj;
}


// ROM 0x00290a48 FPrimClassOf
Ref
FPrimClassOf(RefArg /*rcvr*/, RefArg obj)
{
	if (!ISPTR(obj))
		return RSSYMimmediate;
	ULong flags = ObjectFlags(obj);
	if ((flags & kObjSlotted) == 0)
		return RSSYMbinary;
	if ((flags & kObjFrame) == 0)
		return RSSYMarray;
	return RSSYMframe;
}


// ROM 0x0029093c FIsInstance
Ref
FIsInstance(RefArg /*rcvr*/, RefArg obj, RefArg theClass)
{
	return NSBOOL(IsInstance(obj, theClass));
}


// ROM 0x002909f0 FIsSubclass
Ref
FIsSubclass(RefArg /*rcvr*/, RefArg sub, RefArg super)
{
	return NSBOOL(IsSubclassRef(sub, super));
}


// ROM 0x002908c8 FClone
Ref
FClone(RefArg /*rcvr*/, RefArg obj)
{
	return Clone(obj);
}


// ROM 0x002908d0 FDeepClone
Ref
FDeepClone(RefArg /*rcvr*/, RefArg obj)
{
	return DeepClone(obj);
}


// ROM 0x002908d8 FTotalClone
Ref
FTotalClone(RefArg /*rcvr*/, RefArg obj)
{
	return TotalClone(obj);
}


// ROM 0x002908e0 FEnsureInternal
Ref
FEnsureInternal(RefArg /*rcvr*/, RefArg obj)
{
	return EnsureInternal(obj);
}


// ROM 0x002908e8 FReplaceObject
Ref
FReplaceObject(RefArg /*rcvr*/, RefArg target, RefArg replacement)
{
	ReplaceObject(target, replacement);
	return NILREF;
}


// ROM 0x00290808 FLock
Ref
FLock(RefArg /*rcvr*/, RefArg obj)
{
	LockRef(obj);
	return NILREF;
}


// ROM 0x002908a8 FUnlock
Ref
FUnlock(RefArg /*rcvr*/, RefArg obj)
{
	UnlockRef(obj);
	return NILREF;
}


// ROM 0x002907c8 FRef
Ref
FRef(RefArg /*rcvr*/, RefArg n)
{
	return RVALUE(n);
}


// ROM 0x002907d8 FRefOf
Ref
FRefOf(RefArg /*rcvr*/, RefArg obj)
{
	return MAKEINT((Ref) obj);
}


// ROM 0x002905ec FGetSlot
Ref
FGetSlot(RefArg /*rcvr*/, RefArg frame, RefArg slot)
{
	return GetFrameSlotRef(frame, slot);
}


// ROM 0x0029143c FSetSlot
Ref
FSetSlot(RefArg /*rcvr*/, RefArg frame, RefArg slot, RefArg value)
{
	SetFrameSlot(frame, slot, value);
	return value;
}


// ROM 0x00290798 FHasSlot
Ref
FHasSlot(RefArg /*rcvr*/, RefArg frame, RefArg slot)
{
	return NSBOOL(FrameHasSlotRef(frame, slot));
}


// ROM 0x00290714 FRemoveSlot
// A frame's slot by name, an array's by index.
Ref
FRemoveSlot(RefArg /*rcvr*/, RefArg obj, RefArg slot)
{
	ULong flags = ObjectFlags(obj);
	if ((flags & kObjSlotted) == 0)
		ThrowBadTypeWithFrameData(kNSErrNotAFrameOrArray, obj);
	if ((flags & kObjFrame) == 0)
		ArrayRemoveCount(obj, RINT(slot), 1);
	else
		RemoveSlot(obj, slot);
	return obj;
}


// ROM 0x002906b4 FGetPath
Ref
FGetPath(RefArg /*rcvr*/, RefArg obj, RefArg path)
{
	return GetFramePath(obj, path);
}


// ROM 0x002906c0 FSetPath
Ref
FSetPath(RefArg /*rcvr*/, RefArg obj, RefArg path, RefArg value)
{
	SetFramePath(obj, path, value);
	return NILREF;
}


// ROM 0x002906ec FHasPath
Ref
FHasPath(RefArg /*rcvr*/, RefArg obj, RefArg path)
{
	return NSBOOL(FrameHasPath(obj, path));
}


// the out-of-bounds frame the array accessors throw
static void
ThrowOutOfBounds(RefArg obj, long index)
{
	RefVar frame(AllocateFrame());
	SetFrameSlot(frame, RSSYMerrorcode, RefVar(MAKEINT(kNSErrOutOfBounds)));
	SetFrameSlot(frame, RSSYMvalue, obj);
	SetFrameSlot(frame, RSSYMindex, RefVar(MAKEINT(index)));
	ThrowRefException(exFramesWithFrameData, frame);
}


// ROM 0x00291470 FAref
// An array's slot or a string's character.  NOT YET RECONSTRUCTED:
// TRichString (ink in strings) - the characters are the UniChars.
Ref
FAref(RefArg /*rcvr*/, RefArg obj, RefArg index)
{
	long i = RINT(index);
	ULong flags = ObjectFlags(obj);
	if ((flags & kObjSlotted) == 0)
	{
		if (!IsString(obj))
			ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, obj);
		long length = Length(obj) / 2 - 1;
		if (i < 0 || i >= length)
			ThrowOutOfBounds(obj, i);
		return MAKECHAR(((UniChar*) BinaryData(obj))[i]);
	}
	if ((flags & kObjFrame) != 0)
		ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, obj);
	return GetArraySlotRef(obj, i);
}


// ROM 0x002915dc FSetAref
Ref
FSetAref(RefArg /*rcvr*/, RefArg obj, RefArg index, RefArg value)
{
	long i = RINT(index);
	ULong flags = ObjectFlags(obj);
	if ((flags & kObjSlotted) == 0)
	{
		if (!IsString(obj))
			ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, obj);
		long length = Length(obj) / 2 - 1;
		if (i < 0 || i >= length)
			ThrowOutOfBounds(obj, i);
		UniChar c = RCHAR(value);
		if (c == 0)
			SetLength(obj, (i + 1) * 2);
		else
			((UniChar*) BinaryData(obj))[i] = c;
		return value;
	}
	if ((flags & kObjFrame) != 0)
		ThrowBadTypeWithFrameData(kNSErrNotAnArrayOrString, obj);
	SetArraySlotRef(obj, i, value);
	return value;
}


// ROM 0x00291df4 FAddArraySlot
Ref
FAddArraySlot(RefArg /*rcvr*/, RefArg array, RefArg value)
{
	AddArraySlot(array, value);
	return value;
}


// ROM 0x002930cc FNewWeakArray
Ref
FNewWeakArray(RefArg /*rcvr*/, RefArg length)
{
	return AllocateArray(RefVar(kWeakArrayClass), RINT(length));
}


// ROM 0x00291ee0 FMakeBinary
Ref
FMakeBinary(RefArg /*rcvr*/, RefArg length, RefArg theClass)
{
	return AllocateBinary(theClass, RINT(length));
}


/* -------------------------------------------------------------------------------
	Predicates
------------------------------------------------------------------------------- */

// ROM 0x00290aa0 FIsImmediate
Ref
FIsImmediate(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(!ISPTR(obj));
}


// ROM 0x00290ab8 FIsBinary
Ref
FIsBinary(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(ISPTR(obj) && (ObjectFlags(obj) & kObjSlotted) == 0);
}


// ROM 0x00290aec FIsArray
Ref
FIsArray(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsArray(obj));
}


// ROM 0x00290b10 FIsFrame
Ref
FIsFrame(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsFrame(obj));
}


// ROM 0x00290b34 FIsString
Ref
FIsString(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsString(obj));
}


// ROM 0x00290b58 FIsSymbol
Ref
FIsSymbol(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsSymbol(obj));
}


// ROM 0x00290b80 FIsMagicPtr
Ref
FIsMagicPtr(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(RTAG(obj) == kTagMagicPtr);
}


// ROM 0x00290bfc FIsFunction
Ref
FIsFunction(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsFunction(obj));
}


// ROM 0x00290c20 FIsNativeFunction
Ref
FIsNativeFunction(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsFunction(obj) && IsNativeFunction(obj));
}


// ROM 0x00290c44 FIsNumber
Ref
FIsNumber(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsNumber(obj));
}


// ROM 0x00290c6c FIsInteger
Ref
FIsInteger(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(ISINT(obj));
}


// ROM 0x00290c84 FIsReal
Ref
FIsReal(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(ISREAL(obj));
}


// ROM 0x00290cac FIsCharacter
Ref
FIsCharacter(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(ISCHAR(obj));
}


// ROM 0x00290cd4 FIsPathExpr
Ref
FIsPathExpr(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL(IsPathExpr(obj));
}


// ROM 0x00290cf8 FIsReadOnly
Ref
FIsReadOnly(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL((ObjectFlags(obj) & kObjReadOnly) != 0);
}


// ROM 0x00290d20 FIsDirty
Ref
FIsDirty(RefArg /*rcvr*/, RefArg obj)
{
	return NSBOOL((ObjectFlags(obj) & kObjDirty) != 0);
}


/* -------------------------------------------------------------------------------
	Variables
------------------------------------------------------------------------------- */

// ROM 0x002911a8 FGetVariable
// A variable seen from a frame: its _proto chain, then its _parent's.
Ref
FGetVariable(RefArg /*rcvr*/, RefArg context, RefArg name)
{
	return GetVariable(context, name, nil, 0);
}


// ROM 0x002911bc FSetVariable
Ref
FSetVariable(RefArg /*rcvr*/, RefArg context, RefArg name, RefArg value)
{
	SetVariable(context, name, value);
	return value;
}


// ROM 0x0029131c FHasVariable
Ref
FHasVariable(RefArg /*rcvr*/, RefArg context, RefArg name)
{
	long exists;
	GetVariable(context, name, &exists, 0);
	return NSBOOL(exists);
}


// ROM 0x002911f0 FGetVar
// The receiver's variable (an error when it has none).
Ref
FGetVar(RefArg rcvr, RefArg name)
{
	long exists;
	RefVar value(GetVariable(rcvr, name, &exists, 0));
	if (!exists)
		ThrowExInterpreterWithSymbol(kNSErrUndefinedVariable, name);
	return value;
}


// ROM 0x00291258 FSetVar
Ref
FSetVar(RefArg rcvr, RefArg name, RefArg value)
{
	SetVariable(rcvr, name, value);
	return name;
}


// ROM 0x00291278 FHasVar
// The receiver's, or a global's.
Ref
FHasVar(RefArg rcvr, RefArg name)
{
	long exists = 0;
	if ((Ref) rcvr != NILREF)
		GetVariable(rcvr, name, &exists, 0);
	if (!exists)
		exists = FrameHasSlotRef(gVarFrame, name);
	return NSBOOL(exists);
}


// ROM 0x00291354 FLocalVar
Ref
FLocalVar(RefArg rcvr, RefArg name)
{
	SetFrameSlot(rcvr, name, RefVar(NILREF));
	return NILREF;
}


// ROM 0x0029238c FGetGlobals
Ref
FGetGlobals(RefArg /*rcvr*/)
{
	return gVarFrame;
}


// ROM 0x0029239c FSetGlobal
Ref
FSetGlobal(RefArg /*rcvr*/, RefArg value, RefArg name)
{
	RefVar sym(EnsureInternal(name));
	RefVar vars(gVarFrame);
	SetFrameSlot(vars, sym, value);
	return name;
}


// ROM 0x00292408 FDefGlobalFn
Ref
FDefGlobalFn(RefArg /*rcvr*/, RefArg name, RefArg fn)
{
	if (!IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	RefVar sym(EnsureInternal(name));
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, sym, fn);
	return name;
}


/* -------------------------------------------------------------------------------
	Calling and sending
------------------------------------------------------------------------------- */

// ROM 0x0029139c FApply
Ref
FApply(RefArg /*rcvr*/, RefArg fn, RefArg args)
{
	if (!ISPTR(fn) || !IsFunction(fn))
		ThrowBadTypeWithFrameData(kNSErrNotAFunction, fn);
	return DoBlock(fn, args);
}


// ROM 0x002913dc FPerform
Ref
FPerform(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args)
{
	return DoMessage(receiver, message, args);
}


// ROM 0x002913f4 FPerformIfDefined
Ref
FPerformIfDefined(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args)
{
	return DoMessageIfDefined(receiver, message, args, nil);
}


// ROM 0x0029140c FProtoPerform
Ref
FProtoPerform(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args)
{
	return DoProtoMessage(receiver, message, args);
}


// ROM 0x00291424 FProtoPerformIfDefined
Ref
FProtoPerformIfDefined(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args)
{
	return DoProtoMessageIfDefined(receiver, message, args, nil);
}


// ROM 0x00292490 FMap
// fn(tag, value) for each slot.
Ref
FMap(RefArg /*rcvr*/, RefArg obj, RefArg fn)
{
	TObjectIterator iter(obj);
	RefVar args(AllocateArray(RSSYMarray, 2));
	while (!iter.Done())
	{
		SetArraySlotRef(args, 0, iter.fTag);
		SetArraySlotRef(args, 1, iter.fValue);
		DoBlock(fn, args);
		iter.Next();
	}
	return NILREF;
}


// ROM 0x00292554 FCollect
// The array of fn(tag, value) for each slot.
Ref
FCollect(RefArg /*rcvr*/, RefArg obj, RefArg fn)
{
	TObjectIterator iter(obj);
	RefVar args(AllocateArray(RSSYMarray, 2));
	RefVar result(AllocateArray(RSSYMarray, Length(obj)));
	long i = 0;
	while (!iter.Done())
	{
		SetArraySlotRef(args, 0, iter.fTag);
		SetArraySlotRef(args, 1, iter.fValue);
		RefVar value(DoBlock(fn, args));
		SetArraySlotRef(result, i, value);
		iter.Next();
		i++;
	}
	return result;
}


/* -------------------------------------------------------------------------------
	Exceptions
------------------------------------------------------------------------------- */

// the C string of a NewtonScript string, for an exception's message
static char*
ExceptionMessage(RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	const UniChar* s = GetCString(str);
	long length = Length(str) / 2;
	char* text = new char[length + 1];
	if (text == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	ConvertFromUnicode(s, text, kMacRomanEncoding, length);
	return text;
}


// (host) the exception data destructor for a text message
static void
DeleteCharArray(void* text)
{
	delete[] (char*) text;
}


// ROM 0x00291f18 FThrow
// Throw name (a symbol under evt.ex) with data: a frame for a type.ref
// name, a string's text for an evt.ex.msg name, else an error code.
Ref
FThrow(RefArg /*rcvr*/, RefArg name, RefArg data)
{
	if (!IsSymbol(name))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, name);
	if (!Subexception(SymbolName(name), (ExceptionName) "evt.ex"))
		ThrowExFramesWithBadValue(kNSErrBadExceptionName, name);
	strncpy(gFramesExceptionName, SymbolName(name), sizeof(gFramesExceptionName) - 1);
	if (Subexception(gFramesExceptionName, (ExceptionName) "type.ref"))
		ThrowRefException(gFramesExceptionName, data);
	if (Subexception(gFramesExceptionName, (ExceptionName) "evt.ex.msg"))
		Throw(gFramesExceptionName, ExceptionMessage(data), DeleteCharArray);
	long code = RINT(data);
	Throw(gFramesExceptionName, (void*) (Long) code, nil);
	return NILREF;
}


// ROM 0x002920e0 FRethrow
Ref
FRethrow(RefArg /*rcvr*/)
{
	RefVar exception(gInterpreter->ExceptionBeingHandled());
	if ((Ref) exception == NILREF)
		Throw(exInterpreter, (void*) kNSErrNoCurrentException, nil);
	strncpy(gFramesExceptionName, SymbolName(GetFrameSlotRef(exception, RSSYMname)), sizeof(gFramesExceptionName) - 1);
	if (Subexception(gFramesExceptionName, (ExceptionName) "evt.ex.msg"))
	{
		RefVar message(GetFrameSlotRef(exception, RSSYMmessage));
		Throw(gFramesExceptionName, ExceptionMessage(message), DeleteCharArray);
	}
	if (Subexception(gFramesExceptionName, (ExceptionName) "type.ref"))
	{
		RefVar data(GetFrameSlotRef(exception, RSSYMdata));
		ThrowRefException(gFramesExceptionName, data);
	}
	long code = RINT(GetFrameSlotRef(exception, RSSYMerror));
	Throw(gFramesExceptionName, (void*) (Long) code, nil);
	return NILREF;
}


// ROM 0x002922f4 FCurrentException
Ref
FCurrentException(RefArg /*rcvr*/)
{
	return gInterpreter->ExceptionBeingHandled();
}


// ROM 0x00292304 FIsSubexception
Ref
FIsSubexception(RefArg /*rcvr*/, RefArg sub, RefArg super)
{
	if (!IsSymbol(sub))
		ThrowExFramesWithBadValue(kNSErrNotASymbol, sub);
	if (!IsSymbol(super))
		ThrowExFramesWithBadValue(kNSErrNotASymbol, super);
	return NSBOOL(Subexception(SymbolName(sub), SymbolName(super)));
}


/* -------------------------------------------------------------------------------
	The foreach iterator
	An array of class forEachState: tag, value, the object, whether the
	iteration goes deeply (through the _protos; the count of slots then),
	the index, the length, the object's map (nil for an array).
------------------------------------------------------------------------------- */

enum { kIterTag = 0, kIterValue, kIterObject, kIterDeeply, kIterIndex, kIterLength, kIterMap };

// ROM 0x00292838 ForEachLoopNext__FRC6RefVar
// On to the next slot (the object may have changed length); when deep, a
// _proto slot is skipped and the end of a frame goes on into its _proto.
// Answers whether there is one.
Boolean
ForEachLoopNext(RefArg iter)
{
	long length = RINT(GetArraySlotRef(iter, kIterLength));
	RefVar obj(GetArraySlotRef(iter, kIterObject));
	long objLength = Length(obj);
	long index = RINT(GetArraySlotRef(iter, kIterIndex));
	if (length <= objLength)
	{
		index++;
		SetArraySlotRef(iter, kIterIndex, MAKEINT(index));
	}
	if (objLength != length)
		SetArraySlotRef(iter, kIterLength, MAKEINT(objLength));
	Boolean deeply = GetArraySlotRef(iter, kIterDeeply) != NILREF;
	RefVar map(GetArraySlotRef(iter, kIterMap));
	if (index < objLength)
	{
		RefVar tag(((Ref) map == NILREF) ? MAKEINT(index) : GetTag(map, index, nil));
		SetArraySlotRef(iter, kIterTag, tag);
		if (deeply && EQRef(tag, RSSYM_proto))
			return ForEachLoopNext(iter);
		SetArraySlotRef(iter, kIterValue, GetArraySlotRef(obj, index));
		return true;
	}
	if (deeply && (Ref) map != NILREF)
	{
		RefVar proto(GetFrameSlotRef(obj, RSSYM_proto));
		if ((Ref) proto != NILREF)
		{
			ForEachLoopReset(iter, proto);
			return !ForEachLoopDone(iter);
		}
	}
	SetArraySlotRef(iter, kIterTag, NILREF);
	SetArraySlotRef(iter, kIterValue, NILREF);
	return false;
}


// ROM 0x00292674 ForEachLoopReset__FRC6RefVarT1
// Start over on obj (the _proto of the last, when deep).
Boolean
ForEachLoopReset(RefArg iter, RefArg obj)
{
	SetArraySlotRef(iter, kIterObject, obj);
	SetArraySlotRef(iter, kIterMap, ((ObjectFlags(obj) & kObjFrame) == 0) ? NILREF : ObjClass(OBJ(obj)));
	SetArraySlotRef(iter, kIterLength, MAKEINT(Length(obj)));
	SetArraySlotRef(iter, kIterIndex, MAKEINT(-1));
	return ForEachLoopNext(iter);
}


// ROM 0x00292760 ForEachLoopDone__FRC6RefVar
Boolean
ForEachLoopDone(RefArg iter)
{
	long index = RINT(GetArraySlotRef(iter, kIterIndex));
	long length = RINT(GetArraySlotRef(iter, kIterLength));
	if (GetArraySlotRef(iter, kIterDeeply) == NILREF || GetArraySlotRef(iter, kIterMap) == NILREF)
		return length <= index;
	if (length <= index)
		return GetFrameSlotRef(GetArraySlotRef(iter, kIterObject), RSSYM_proto) == NILREF;
	return false;
}


// ROM 0x00292b58 FNewIterator
Ref
FNewIterator(RefArg /*rcvr*/, RefArg obj, RefArg deeply)
{
	if (!ISPTR(obj) || (ObjectFlags(obj) & kObjSlotted) == 0)
		ThrowBadTypeWithFrameData(kNSErrNotAFrameOrArray, obj);
	RefVar iter(AllocateArray(RSSYMforeachstate, 7));
	SetArraySlotRef(iter, kIterObject, obj);
	SetArraySlotRef(iter, kIterLength, MAKEINT(Length(obj)));
	if ((ObjectFlags(obj) & kObjFrame) == 0)
	{
		SetArraySlotRef(iter, kIterMap, NILREF);
		SetArraySlotRef(iter, kIterDeeply, ((Ref) deeply == NILREF) ? NILREF : GetArraySlotRef(iter, kIterLength));
	}
	else
	{
		SetArraySlotRef(iter, kIterMap, ObjClass(OBJ(obj)));
		if ((Ref) deeply == NILREF)
			SetArraySlotRef(iter, kIterDeeply, NILREF);
		else
		{
			long count = RINT(GetArraySlotRef(iter, kIterLength));
			RefVar proto(GetFrameSlotRef(obj, RSSYM_proto));
			while ((Ref) proto != NILREF)
			{
				count += Length(proto) - 1;
				proto = GetFrameSlotRef(proto, RSSYM_proto);
			}
			SetArraySlotRef(iter, kIterDeeply, MAKEINT(count));
		}
	}
	SetArraySlotRef(iter, kIterIndex, MAKEINT(-1));
	ForEachLoopNext(iter);
	return iter;
}


/* -------------------------------------------------------------------------------
	Symbols and characters
------------------------------------------------------------------------------- */

// ROM 0x00292ef4 FIntern
Ref
FIntern(RefArg /*rcvr*/, RefArg str)
{
	if (!IsString(str))
		ThrowBadTypeWithFrameData(kNSErrNotAString, str);
	return Intern(GetCString(str));
}


// ROM 0x00292f70 FSymbolName
Ref
FSymbolName(RefArg /*rcvr*/, RefArg sym)
{
	if (!IsSymbol(sym))
		ThrowBadTypeWithFrameData(kNSErrNotASymbol, sym);
	return MakeString(SymbolName(sym));
}


// ROM 0x002910f8 FCHR
Ref
FCHR(RefArg /*rcvr*/, RefArg n)
{
	return MAKECHAR(RINT(n));
}


// ROM 0x00290fd4 FORD
Ref
FORD(RefArg /*rcvr*/, RefArg c)
{
	return MAKEINT(RCHAR(c));
}


// ROM 0x0029118c FGC
Ref
FGC(RefArg /*rcvr*/)
{
	GC();
	return NILREF;
}


/* -------------------------------------------------------------------------------
	Real functions (<math.h> over CoerceToDouble)
------------------------------------------------------------------------------- */

#define REAL_FUNCTION(name, fn) \
	Ref name(RefArg /*rcvr*/, RefArg a) { return MakeReal(fn(CoerceToDouble(a))); }
#define REAL_FUNCTION2(name, fn) \
	Ref name(RefArg /*rcvr*/, RefArg a, RefArg b) { return MakeReal(fn(CoerceToDouble(a), CoerceToDouble(b))); }

REAL_FUNCTION(Facos, acos)			// ROM 0x002912ec Facos
REAL_FUNCTION(Fasin, asin)			// ROM 0x002918a0 Fasin
REAL_FUNCTION(Fatan, atan)			// ROM 0x002922c4 Fatan
REAL_FUNCTION(Fcos, cos)			// ROM 0x00292b28 Fcos
REAL_FUNCTION(Fsin, sin)			// ROM 0x002931d8 Fsin
REAL_FUNCTION(Ftan, tan)			// ROM 0x00293458 Ftan
REAL_FUNCTION(Fcosh, cosh)			// ROM 0x00293488 Fcosh
REAL_FUNCTION(Fsinh, sinh)			// ROM 0x002934b8 Fsinh
REAL_FUNCTION(Ftanh, tanh)			// ROM 0x002934e8 Ftanh
REAL_FUNCTION(Fexp, exp)			// ROM 0x00293518 Fexp
REAL_FUNCTION(Flog, log)			// ROM 0x00293548 Flog
REAL_FUNCTION(Flog10, log10)		// ROM 0x002935b4 Flog10
REAL_FUNCTION(Fsqrt, sqrt)			// ROM 0x002935e4 Fsqrt
REAL_FUNCTION(Ffabs, fabs)			// ROM 0x00293614 Ffabs
REAL_FUNCTION2(Fatan2, atan2)		// ROM 0x00293644 Fatan2
REAL_FUNCTION2(Fpow, pow)			// ROM 0x00293698 Fpow
REAL_FUNCTION2(Ffmod, fmod)			// ROM 0x002936ec Ffmod
REAL_FUNCTION2(Fhypot, hypot)		// ROM 0x00293944 Fhypot
REAL_FUNCTION(Ftrunc, trunc)		// ROM 0x00293b54 Ftrunc
REAL_FUNCTION(Fround, round)		// ROM 0x00293b24 Fround
REAL_FUNCTION2(Ffmax, fmax)			// ROM 0x00293dcc Fmax
REAL_FUNCTION2(Ffmin, fmin)			// ROM 0x00293e20 Fmin
REAL_FUNCTION(Facosh, acosh)		// ROM 0x00293740 Facosh
REAL_FUNCTION(Fasinh, asinh)		// ROM 0x00293770 Fasinh
REAL_FUNCTION(Fatanh, atanh)		// ROM 0x002937a0 Fatanh
REAL_FUNCTION(Fexpm1, expm1)		// ROM 0x002937d0 Fexpm1
REAL_FUNCTION(Flog1p, log1p)		// ROM 0x00293890 Flog1p
REAL_FUNCTION(Flogb, logb)			// ROM 0x002938c0 Flogb
REAL_FUNCTION(Ferf, erf)			// ROM 0x00293998 Ferf
REAL_FUNCTION(Ferfc, erfc)			// ROM 0x002939c8 Ferfc
REAL_FUNCTION(Fgamma, tgamma)		// ROM 0x002939f8 Fgamma  (the ROM's gamma is the true gamma)
REAL_FUNCTION(Flgamma, lgamma)		// ROM 0x00293a28 Flgamma
REAL_FUNCTION(Frint, rint)			// ROM 0x00293a58 Frint
REAL_FUNCTION(Fnearbyint, nearbyint)	// ROM 0x00293acc Fnearbyint
REAL_FUNCTION2(Fremainder, remainder)	// ROM 0x00293b84 Fremainder
REAL_FUNCTION2(Fcopysign, copysign)	// ROM 0x00293bd8 Fcopysign
REAL_FUNCTION2(Fnextafterd, nextafter)	// ROM 0x00293c2c Fnextafterd
REAL_FUNCTION2(Fdim, fdim)			// ROM 0x00293d78 Fdim


// ROM 0x0029383c Fldexp
Ref
Fldexp(RefArg /*rcvr*/, RefArg x, RefArg n)
{
	return MakeReal(ldexp(CoerceToDouble(x), (int) RINT(n)));
}


// ROM 0x002938f0 Fscalb
Ref
Fscalb(RefArg /*rcvr*/, RefArg x, RefArg n)
{
	return MakeReal(scalbn(CoerceToDouble(x), (int) RINT(n)));
}


// ROM 0x00293afc Frinttol
// The nearest integer (in the current rounding direction).
Ref
Frinttol(RefArg /*rcvr*/, RefArg x)
{
	return MAKEINT(lrint(CoerceToDouble(x)));
}


// ROM 0x00293c80 Fisnormal
Ref
Fisnormal(RefArg /*rcvr*/, RefArg x)
{
	return NSBOOL(isnormal(CoerceToDouble(x)));
}


// ROM 0x00293cb0 Fisfinite
Ref
Fisfinite(RefArg /*rcvr*/, RefArg x)
{
	return NSBOOL(isfinite(CoerceToDouble(x)));
}


// ROM 0x00293ce0 Fisnan
Ref
Fisnan(RefArg /*rcvr*/, RefArg x)
{
	return NSBOOL(isnan(CoerceToDouble(x)));
}


// ROM 0x00293d50 Fsignbit
// ==> non-zero for a negative sign (the sign bit as an integer).
Ref
Fsignbit(RefArg /*rcvr*/, RefArg x)
{
	return MAKEINT(signbit(CoerceToDouble(x)) ? 1 : 0);
}


// ROM 0x00293e74 Fcompound
// (1 + rate) ^ periods (SANE's compound, 0x002a2904).
Ref
Fcompound(RefArg /*rcvr*/, RefArg rate, RefArg periods)
{
	return MakeReal(pow(1.0 + CoerceToDouble(rate), CoerceToDouble(periods)));
}


// ROM 0x00293ec8 Fannuity
// (1 - (1 + rate) ^ -periods) / rate (SANE's annuity, 0x00285ae0).
Ref
Fannuity(RefArg /*rcvr*/, RefArg rate, RefArg periods)
{
	double r = CoerceToDouble(rate);
	double n = CoerceToDouble(periods);
	if (r == 0.0)
		return MakeReal(n);
	return MakeReal((1.0 - pow(1.0 + r, -n)) / r);
}


// ROM 0x00293f1c Fremquo
// ==> [remainder, quotient]
Ref
Fremquo(RefArg /*rcvr*/, RefArg x, RefArg y)
{
	RefVar result(AllocateArray(RSSYMarray, 2));
	int quotient = 0;
	double remainder = remquo(CoerceToDouble(x), CoerceToDouble(y), &quotient);
	SetArraySlotRef(result, 0, MakeReal(remainder));
	SetArraySlotRef(result, 1, MAKEINT(quotient));
	return result;
}


// ROM 0x00293ff4 Frandomx
// SANE's randomx (0x00313ffc): the next value of the Lehmer sequence
// x' = 7^5 x mod (2^31 - 1); ==> [value, seed] (both x').
Ref
Frandomx(RefArg /*rcvr*/, RefArg x)
{
	double seed = CoerceToDouble(x);
	seed = fmod(16807.0 * seed, 2147483647.0);
	RefVar result(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(result, 0, MakeReal(seed));
	SetArraySlotRef(result, 1, MakeReal(seed));
	return result;
}


/* -------------------------------------------------------------------------------
	The floating-point environment (<fenv.h>).  DEVIATION: the exception and
	rounding-mode values are the host's, not the ARM FPE's.
------------------------------------------------------------------------------- */

// ROM 0x00294098 Ffeclearexcept
Ref
Ffeclearexcept(RefArg /*rcvr*/, RefArg excepts)
{
	feclearexcept((int) RINT(excepts));
	return NILREF;
}


// ROM 0x002940c8 Ffegetexcept
// The flags raised among excepts (fegetexceptflag).
Ref
Ffegetexcept(RefArg /*rcvr*/, RefArg excepts)
{
	fexcept_t flags = 0;
	fegetexceptflag(&flags, (int) RINT(excepts));
	return MAKEINT((long) flags);
}


// ROM 0x00294120 Fferaiseexcept
Ref
Fferaiseexcept(RefArg /*rcvr*/, RefArg excepts)
{
	feraiseexcept((int) RINT(excepts));
	return NILREF;
}


// ROM 0x00294150 Ffesetexcept
Ref
Ffesetexcept(RefArg /*rcvr*/, RefArg flags, RefArg excepts)
{
	fexcept_t f = (fexcept_t) RINT(flags);
	fesetexceptflag(&f, (int) RINT(excepts));
	return NILREF;
}


// ROM 0x002941ac Ffetestexcept
Ref
Ffetestexcept(RefArg /*rcvr*/, RefArg excepts)
{
	return MAKEINT(fetestexcept((int) RINT(excepts)));
}


// ROM 0x002941dc Ffegetround
Ref
Ffegetround(RefArg /*rcvr*/)
{
	return MAKEINT(fegetround());
}


// ROM 0x002941f4 Ffesetround
Ref
Ffesetround(RefArg /*rcvr*/, RefArg mode)
{
	return MAKEINT(fesetround((int) RINT(mode)));
}


// the environment as one integer: the raised flags with the rounding mode
// (the ROM's fenv_t is one word)
static long
EnvironmentWord(void)
{
	return fetestexcept(FE_ALL_EXCEPT) | (fegetround() << 16);
}


static void
SetEnvironmentWord(long env)
{
	feclearexcept(FE_ALL_EXCEPT);
	feraiseexcept((int) (env & 0xffff));
	fesetround((int) (env >> 16));
}


// ROM 0x00294224 Ffegetenv
Ref
Ffegetenv(RefArg /*rcvr*/)
{
	return MAKEINT(EnvironmentWord());
}


// ROM 0x00294248 Ffeholdexcept
// ==> the environment saved; the flags cleared.
Ref
Ffeholdexcept(RefArg /*rcvr*/, RefArg /*env*/)
{
	long env = EnvironmentWord();
	feclearexcept(FE_ALL_EXCEPT);
	return MAKEINT(env);
}


// ROM 0x00294284 Ffesetenv
Ref
Ffesetenv(RefArg /*rcvr*/, RefArg env)
{
	SetEnvironmentWord(RINT(env));
	return NILREF;
}


// ROM 0x002942c0 Ffeupdateenv
// The environment restored, the flags raised meanwhile raised again.
Ref
Ffeupdateenv(RefArg /*rcvr*/, RefArg env)
{
	int raised = fetestexcept(FE_ALL_EXCEPT);
	SetEnvironmentWord(RINT(env));
	feraiseexcept(raised);
	return NILREF;
}


/* -------------------------------------------------------------------------------
	Random numbers.  The ROM's Random uses its C library's rand() (srand at
	UserBoot with the time), whose state GetRandomState/SetRandomState
	save and restore as a 'randomState binary; NOT YET RECONSTRUCTED: that
	library's generator - the host keeps the ANSI C example generator
	(state one 32-bit word) so that states round-trip.
------------------------------------------------------------------------------- */

static ULong gRandomSeed = 1;		// the C library's rand() state

static int
NewtonRand(void)
{
	gRandomSeed = gRandomSeed * 1103515245 + 12345;
	return (int) ((gRandomSeed >> 16) & 0x7fff);
}


// the seed from the time, as UserBoot's srand
void
SeedRandom(ULong seed)
{
	gRandomSeed = seed;
}


// ROM 0x0029458c FRandom
// An integer from low to high inclusive.
Ref
FRandom(RefArg /*rcvr*/, RefArg low, RefArg high)
{
	long lo = RINT(low);
	long hi = RINT(high);
	if (hi < lo)
		Throw(exFrames, (void*) kNSErrBadArgs, nil);
	long r = NewtonRand();
	return MAKEINT(lo + r % (hi - lo + 1));
}


// ROM 0x00294618 FGetRandomState
Ref
FGetRandomState(RefArg /*rcvr*/)
{
	RefVar state(AllocateBinary(RSSYMrandomstate, sizeof(gRandomSeed)));
	memcpy(BinaryData(state), &gRandomSeed, sizeof(gRandomSeed));
	return state;
}


// ROM 0x002946b8 FSetRandomState
Ref
FSetRandomState(RefArg /*rcvr*/, RefArg state)
{
	if (!IsBinary(state) || Length(state) < (long) sizeof(gRandomSeed))
		ThrowBadTypeWithFrameData(kNSErrNotABinaryObject, state);
	memcpy(&gRandomSeed, BinaryData(state), sizeof(gRandomSeed));
	return NILREF;
}


// ROM 0x002d1844 FGetFunctionArgCount
Ref
FGetFunctionArgCount(RefArg /*rcvr*/, RefArg fn)
{
	return MAKEINT(GetFunctionArgCount(fn));
}


// ROM 0x00292df4 FForLoop
// fn called with each integer from start to end (a 1.x helper).
Ref
FForLoop(RefArg /*rcvr*/, RefArg start, RefArg end, RefArg fn)
{
	if (!ISINT((Ref) start))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, start);
	if (!ISINT((Ref) end))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, end);
	long last = RINT(end);
	RefVar args(AllocateArray(RSSYMarray, 1));
	for (long i = RINT(start); i <= last; i++)
	{
		SetArraySlotRef(args, 0, MAKEINT(i));
		DoBlock(fn, args);
	}
	return NILREF;
}


// ROM 0x00290600 FGetSiblingSlot
// A slot looked up along the _proto chain only (GetProtoVariable).
Ref
FGetSiblingSlot(RefArg /*rcvr*/, RefArg context, RefArg name)
{
	if ((Ref) context == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrNilContext, name);
	long exists;
	return GetProtoVariable(context, name, &exists);
}


// ROM 0x00290610 FHasSiblingSlot
Ref
FHasSiblingSlot(RefArg /*rcvr*/, RefArg context, RefArg name)
{
	long exists;
	GetProtoVariable(context, name, &exists);
	return NSBOOL(exists);
}


/* -------------------------------------------------------------------------------
	Registration
------------------------------------------------------------------------------- */

#define NATIVE(symbol, fn, n)	RegisterNativeFunction(symbol, (void*) (NativeFn##n) fn, n)

void	RegisterMungerNatives(void);		// Munger.cpp
void	RegisterStringNatives(void);		// StringNatives.cpp
void	RegisterArrayNatives(void);			// ArrayNatives.cpp

void
RegisterBuiltinNatives(void)
{
	RegisterMungerNatives();
	RegisterStringNatives();
	RegisterArrayNatives();
	RegisterPrinterNatives();
	RegisterCompilerNatives();
	NATIVE("FAdd", FAdd, 2);
	NATIVE("FSubtract", FSubtract, 2);
	NATIVE("FMultiply", FMultiply, 2);
	NATIVE("FDivide", FDivide, 2);
	NATIVE("FDiv", FDiv, 2);
	NATIVE("FMod", FMod, 2);
	NATIVE("FNegate", FNegate, 1);
	NATIVE("FAbs", FAbs, 1);
	NATIVE("FSignum", FSignum, 1);
	NATIVE("FCeiling", FCeiling, 1);
	NATIVE("FFloor", FFloor, 1);
	NATIVE("FReal", FReal, 1);
	NATIVE("FLessThan", FLessThan, 2);
	NATIVE("FLessOrEqual", FLessOrEqual, 2);
	NATIVE("FGreaterThan", FGreaterThan, 2);
	NATIVE("FGreaterOrEqual", FGreaterOrEqual, 2);
	NATIVE("FEqual", FEqual, 2);
	NATIVE("FUnorderedLessOrGreater", FUnorderedLessOrGreater, 2);
	NATIVE("FUnorderedOrGreater", FUnorderedOrGreater, 2);
	NATIVE("FUnorderedGreaterOrEqual", FUnorderedGreaterOrEqual, 2);
	NATIVE("FUnorderedOrLess", FUnorderedOrLess, 2);
	NATIVE("FUnorderedLessOrEqual", FUnorderedLessOrEqual, 2);
	NATIVE("FUnorderedOrEqual", FUnorderedOrEqual, 2);
	NATIVE("FUnordered", FUnordered, 2);
	NATIVE("FLessOrGreater", FLessOrGreater, 2);
	NATIVE("FLessEqualOrGreater", FLessEqualOrGreater, 2);
	NATIVE("FForLoop", FForLoop, 3);
	NATIVE("Facosh", Facosh, 1);
	NATIVE("Fasinh", Fasinh, 1);
	NATIVE("Fatanh", Fatanh, 1);
	NATIVE("Fexpm1", Fexpm1, 1);
	NATIVE("Fldexp", Fldexp, 2);
	NATIVE("Flog1p", Flog1p, 1);
	NATIVE("Flogb", Flogb, 1);
	NATIVE("Fscalb", Fscalb, 2);
	NATIVE("Ferf", Ferf, 1);
	NATIVE("Ferfc", Ferfc, 1);
	NATIVE("Fgamma", Fgamma, 1);
	NATIVE("Flgamma", Flgamma, 1);
	NATIVE("Frint", Frint, 1);
	NATIVE("Fnearbyint", Fnearbyint, 1);
	NATIVE("Frinttol", Frinttol, 1);
	NATIVE("Fremainder", Fremainder, 2);
	NATIVE("Fcopysign", Fcopysign, 2);
	NATIVE("Fnextafterd", Fnextafterd, 2);
	NATIVE("Fisnormal", Fisnormal, 1);
	NATIVE("Fisfinite", Fisfinite, 1);
	NATIVE("Fisnan", Fisnan, 1);
	NATIVE("Fsignbit", Fsignbit, 1);
	NATIVE("Fdim", Fdim, 2);
	NATIVE("Fcompound", Fcompound, 2);
	NATIVE("Fannuity", Fannuity, 2);
	NATIVE("Fremquo", Fremquo, 2);
	NATIVE("Frandomx", Frandomx, 1);
	NATIVE("Ffeclearexcept", Ffeclearexcept, 1);
	NATIVE("Ffegetexcept", Ffegetexcept, 1);
	NATIVE("Fferaiseexcept", Fferaiseexcept, 1);
	NATIVE("Ffesetexcept", Ffesetexcept, 2);
	NATIVE("Ffetestexcept", Ffetestexcept, 1);
	NATIVE("Ffegetround", Ffegetround, 0);
	NATIVE("Ffesetround", Ffesetround, 1);
	NATIVE("Ffegetenv", Ffegetenv, 0);
	NATIVE("Ffeholdexcept", Ffeholdexcept, 1);
	NATIVE("Ffesetenv", Ffesetenv, 1);
	NATIVE("Ffeupdateenv", Ffeupdateenv, 1);
	NATIVE("FRandom", FRandom, 2);
	NATIVE("FGetRandomState", FGetRandomState, 0);
	NATIVE("FSetRandomState", FSetRandomState, 1);
	NATIVE("FGetFunctionArgCount", FGetFunctionArgCount, 1);
	NATIVE("FGetSiblingSlot", FGetSiblingSlot, 2);
	NATIVE("FHasSiblingSlot", FHasSiblingSlot, 2);
	NATIVE("FMin", FMin, 2);
	NATIVE("FMax", FMax, 2);
	NATIVE("FNot", FNot, 1);
	NATIVE("FBoolAnd", FBoolAnd, 2);
	NATIVE("FBoolOr", FBoolOr, 2);
	NATIVE("FBitAnd", FBitAnd, 2);
	NATIVE("FBitOr", FBitOr, 2);
	NATIVE("FBitXor", FBitXor, 2);
	NATIVE("FBitNot", FBitNot, 1);
	NATIVE("FLShift", FLShift, 2);
	NATIVE("FRShift", FRShift, 2);
	NATIVE("FLength", FLength, 1);
	NATIVE("FSetLength", FSetLength, 2);
	NATIVE("FClassOf", FClassOf, 1);
	NATIVE("FSetClass", FSetClass, 2);
	NATIVE("FPrimClassOf", FPrimClassOf, 1);
	NATIVE("FIsInstance", FIsInstance, 2);
	NATIVE("FIsSubclass", FIsSubclass, 2);
	NATIVE("FClone", FClone, 1);
	NATIVE("FDeepClone", FDeepClone, 1);
	NATIVE("FTotalClone", FTotalClone, 1);
	NATIVE("FEnsureInternal", FEnsureInternal, 1);
	NATIVE("FReplaceObject", FReplaceObject, 2);
	NATIVE("FLock", FLock, 1);
	NATIVE("FUnlock", FUnlock, 1);
	NATIVE("FRef", FRef, 1);
	NATIVE("FRefOf", FRefOf, 1);
	NATIVE("FGetSlot", FGetSlot, 2);
	NATIVE("FSetSlot", FSetSlot, 3);
	NATIVE("FHasSlot", FHasSlot, 2);
	NATIVE("FRemoveSlot", FRemoveSlot, 2);
	NATIVE("FGetPath", FGetPath, 2);
	NATIVE("FSetPath", FSetPath, 3);
	NATIVE("FHasPath", FHasPath, 2);
	NATIVE("FAref", FAref, 2);
	NATIVE("FSetAref", FSetAref, 3);
	NATIVE("FAddArraySlot", FAddArraySlot, 2);
	NATIVE("FNewWeakArray", FNewWeakArray, 1);
	NATIVE("FMakeBinary", FMakeBinary, 2);
	NATIVE("FIsImmediate", FIsImmediate, 1);
	NATIVE("FIsBinary", FIsBinary, 1);
	NATIVE("FIsArray", FIsArray, 1);
	NATIVE("FIsFrame", FIsFrame, 1);
	NATIVE("FIsString", FIsString, 1);
	NATIVE("FIsSymbol", FIsSymbol, 1);
	NATIVE("FIsMagicPtr", FIsMagicPtr, 1);
	NATIVE("FIsFunction", FIsFunction, 1);
	NATIVE("FIsNativeFunction", FIsNativeFunction, 1);
	NATIVE("FIsNumber", FIsNumber, 1);
	NATIVE("FIsInteger", FIsInteger, 1);
	NATIVE("FIsReal", FIsReal, 1);
	NATIVE("FIsCharacter", FIsCharacter, 1);
	NATIVE("FIsPathExpr", FIsPathExpr, 1);
	NATIVE("FIsReadOnly", FIsReadOnly, 1);
	NATIVE("FIsDirty", FIsDirty, 1);
	NATIVE("FGetVariable", FGetVariable, 2);
	NATIVE("FSetVariable", FSetVariable, 3);
	NATIVE("FHasVariable", FHasVariable, 2);
	NATIVE("FGetVar", FGetVar, 1);
	NATIVE("FSetVar", FSetVar, 2);
	NATIVE("FHasVar", FHasVar, 1);
	NATIVE("FLocalVar", FLocalVar, 1);
	NATIVE("FGetGlobals", FGetGlobals, 0);
	NATIVE("FSetGlobal", FSetGlobal, 2);
	NATIVE("FDefGlobalFn", FDefGlobalFn, 2);
	NATIVE("FApply", FApply, 2);
	NATIVE("FPerform", FPerform, 3);
	NATIVE("FPerformIfDefined", FPerformIfDefined, 3);
	NATIVE("FProtoPerform", FProtoPerform, 3);
	NATIVE("FProtoPerformIfDefined", FProtoPerformIfDefined, 3);
	NATIVE("FMap", FMap, 2);
	NATIVE("FCollect", FCollect, 2);
	NATIVE("FThrow", FThrow, 2);
	NATIVE("FRethrow", FRethrow, 0);
	NATIVE("FCurrentException", FCurrentException, 0);
	NATIVE("FIsSubexception", FIsSubexception, 2);
	NATIVE("FNewIterator", FNewIterator, 2);
	NATIVE("FIntern", FIntern, 1);
	NATIVE("FSymbolName", FSymbolName, 1);
	NATIVE("FCHR", FCHR, 1);
	NATIVE("FORD", FORD, 1);
	NATIVE("FGC", FGC, 0);
	NATIVE("Facos", Facos, 1);
	NATIVE("Fasin", Fasin, 1);
	NATIVE("Fatan", Fatan, 1);
	NATIVE("Fcos", Fcos, 1);
	NATIVE("Fsin", Fsin, 1);
	NATIVE("Ftan", Ftan, 1);
	NATIVE("Fcosh", Fcosh, 1);
	NATIVE("Fsinh", Fsinh, 1);
	NATIVE("Ftanh", Ftanh, 1);
	NATIVE("Fexp", Fexp, 1);
	NATIVE("Flog", Flog, 1);
	NATIVE("Flog10", Flog10, 1);
	NATIVE("Fsqrt", Fsqrt, 1);
	NATIVE("Ffabs", Ffabs, 1);
	NATIVE("Fatan2", Fatan2, 2);
	NATIVE("Fpow", Fpow, 2);
	NATIVE("Ffmod", Ffmod, 2);
	NATIVE("Fhypot", Fhypot, 2);
	NATIVE("Ftrunc", Ftrunc, 1);
	NATIVE("Fround", Fround, 1);
	NATIVE("Fmax", Ffmax, 2);
	NATIVE("Fmin", Ffmin, 2);
}
