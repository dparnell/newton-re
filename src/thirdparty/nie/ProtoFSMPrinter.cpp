/*
	File:		thirdparty/nie/ProtoFSMPrinter.cpp

	Contains:	The printer ObjectToString is built on (the function at
				0x14649, which ObjectToString makes as its local f): one
				object as NewtonScript-ish source followed by ", ", frames
				and arrays recursively.  Re-expressed from the native code
				(15360 bytes, read with tools/newton-rom/analysis/ntknative.py);
				in NewtonScript:

		func(x, depth)
		begin
			local count := 0, s, value;
			if not IsValid(x) then value := "<invalid object reference>"
			else if IsMagicPtr(x) then value := Stringer(["@+", RefOf(x)])
			else if IsFunction(x) then begin
				count := GetFunctionArgCount(x);
				value := ParamStr("func(^0 arg^?1|s|)", [count, count = 1]);
			end else if IsFrame(x) then begin
				if backIndex := LSearch(backList, x, 1, '|=|, nil) then
					value := Stringer(["<", backIndex, ">"])
				else begin
					s := Stringer(["{<", Length(backList), "> "]);
					AddArraySlot(backList, x);
					if depth > maxDepth then s := Stringer([s, "+", RefOf(x)])
					else foreach tag, v in x do begin
						if (count := count + 1) > maxLength then begin
							s := Stringer([s, "..."]); break;
						end;
						s := Stringer([s, tag, ": ",
							if (tag = '_parent and not runParent)
							or (tag = '_proto and not runProto) then "<ignored>, "
							else f(v, depth + 1)]);
					end;
					value := Stringer([p(s), "}"]);
				end
			end else if IsArray(x) then begin
				<the same with "[<", the class unless it is 'Array
				 (Stringer([ClassOf(x), ": "])), and f(v, depth + 1) alone
				 for each element, closed by "]">
			end else if IsString(x) then value := Stringer([$", x, $"])
			else if IsSymbol(x) then value := Stringer([$', x])
			else if IsInteger(x) then value := Stringer([if x > 0 then "+" else "", NumberStr(x)])
			else if IsNumber(x) then
				value := Stringer([if x > 0 then "+" else "", FormattedNumberStr(x, floatFormat)])
			else if IsImmediate(x) then
				value := if x = nil then "nil" else if x = true then "true" else SPrintObject(x)
			else if IsBinary(x) then value := Stringer(["<", ClassOf(x), ", length ", Length(x), ">"])
			else value := SPrintObject(x);
			Stringer([value, ", "])
		end

				backList (the frames and arrays printed so far, after a
				leading nil so that LSearch from 1 answers an index to print),
				backIndex, maxDepth, maxLength, runParent, runProto, floatFormat,
				f and p are ObjectToString's locals, found through the lexical
				scope.  (A positive number is printed with a "+"; ParamStr's
				^?1|s| adds the "s" when parameter 1, count = 1, is nil.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"

Ref FNewIterator(RefArg rcvr, RefArg obj, RefArg deeply);		// frames/Builtins.cpp

enum
{
	kLitIsValid, kLitInvalid, kLitIsMagicPtr, kLitMagicPrefix, kLitRefOf,
	kLitArray, kLitIsFunction, kLitGetFunctionArgCount, kLitFuncFormat, kLitParamStr,
	kLitIsFrame, kLitBackList, kLitEqual, kLitLSearch, kLitBackIndex,
	kLitLess, kLitGreater, kLitFrameOpen, kLitCountClose, kLitMaxDepth,
	kLitPlus, kLitMaxLength, kLitEllipsis, kLitColon, kLitParent,
	kLitRunParent, kLitProto, kLitRunProto, kLitIgnored, kLitF,
	kLitP, kLitFrameClose, kLitIsArray, kLitLess2, kLitGreater2,
	kLitArrayOpen, kLitCountClose2, kLitColon2, kLitPlus2, kLitEllipsis2,
	kLitArrayClose, kLitIsString, kLitIsSymbol, kLitIsInteger, kLitPlus3,
	kLitEmpty, kLitNumberStr, kLitIsNumber, kLitPlus4, kLitEmpty2,
	kLitFloatFormat, kLitFormattedNumberStr, kLitIsImmediate, kLitNil, kLitTrue,
	kLitSPrintObject, kLitIsBinary, kLitLess3, kLitLength, kLitGreater3,
	kLitSeparator, kLitStringer
};

struct Printer
{
	const RefVar*	closure;
	const RefVar*	env;
	RefVar			stringer;

	Ref		Lit(long i)	{ return NIELiteral(*closure, i); }
	Ref		Var(long i)	{ return NIEFindVariable(*env, RefVar(Lit(i))); }
	Ref		Call1(long fn, RefArg a)
			{ RefVar f(NIEGlobalFunction(RefVar(Lit(fn)))); return NSCall(f, a); }
	Ref		Array(long n)	{ return AllocateArray(RefVar(Lit(kLitArray)), n); }
	Ref		Stringer(RefArg parts)	{ return NSCall(stringer, parts); }
};


// the "{<n> " or "[<n> " (and the class) that opens a frame or array, or
// "<n>" when it has been printed already; *known says which
static Ref
OpenAggregate(Printer& pr, RefArg x, bool isArray, bool* known)
{
	RefVar lsearch(NIEGlobalFunction(RefVar(pr.Lit(kLitLSearch))));
	RefVar backList(pr.Var(kLitBackList));
	RefVar index(NSCall(lsearch, backList, x, RefVar(MAKEINT(1)), RefVar(pr.Lit(kLitEqual)), RefVar()));
	NIESetVariable(*pr.env, RefVar(pr.Lit(kLitBackIndex)), index);
	if (NOTNIL(index))
	{
		*known = true;
		RefVar parts(pr.Array(3));
		SetArraySlotRef(parts, 0, pr.Lit(isArray ? kLitLess2 : kLitLess));
		SetArraySlotRef(parts, 1, pr.Var(kLitBackIndex));
		SetArraySlotRef(parts, 2, pr.Lit(isArray ? kLitGreater2 : kLitGreater));
		return pr.Stringer(parts);
	}
	*known = false;
	RefVar parts(pr.Array(isArray ? 4 : 3));
	SetArraySlotRef(parts, 0, pr.Lit(isArray ? kLitArrayOpen : kLitFrameOpen));
	SetArraySlotRef(parts, 1, MAKEINT(Length(RefVar(pr.Var(kLitBackList)))));
	SetArraySlotRef(parts, 2, pr.Lit(isArray ? kLitCountClose2 : kLitCountClose));
	if (isArray)
	{
		RefVar cls(ClassOf(x));
		RefVar classPart;
		if (NIENotEqual(cls, RefVar(pr.Lit(kLitArray))))
		{
			RefVar stringer(NIEGlobalFunction(RefVar(pr.Lit(kLitStringer))));
			RefVar p2(pr.Array(2));
			SetArraySlotRef(p2, 0, ClassOf(x));
			SetArraySlotRef(p2, 1, pr.Lit(kLitColon2));
			classPart = NSCall(stringer, p2);
		}
		SetArraySlot(parts, 3, classPart);
	}
	return pr.Stringer(parts);
}


static Ref
PrintAggregate(Printer& pr, RefArg x, RefArg depth, bool isArray)
{
	bool known;
	RefVar s(OpenAggregate(pr, x, isArray, &known));
	if (known)
		return s;
	RefVar backList(pr.Var(kLitBackList));
	AddArraySlot(backList, x);
	if (NIEGreaterThan(depth, RefVar(pr.Var(kLitMaxDepth))))
	{
		RefVar parts(pr.Array(3));
		SetArraySlot(parts, 0, s);
		SetArraySlotRef(parts, 1, pr.Lit(isArray ? kLitPlus2 : kLitPlus));
		SetArraySlot(parts, 2, RefVar(pr.Call1(kLitRefOf, x)));
		s = pr.Stringer(parts);
	}
	else
	{
		RefVar count(MAKEINT(0));
		RefVar iter(FNewIterator(RefVar(), x, RefVar()));
		while (!ForEachLoopDone(iter))
		{
			RefVar tag(GetArraySlotRef(iter, 0));
			RefVar value(GetArraySlotRef(iter, 1));
			count = NIEAdd(count, RefVar(MAKEINT(1)));
			if (NIEGreaterThan(count, RefVar(pr.Var(kLitMaxLength))))
			{
				RefVar parts(pr.Array(2));
				SetArraySlot(parts, 0, s);
				SetArraySlotRef(parts, 1, pr.Lit(isArray ? kLitEllipsis2 : kLitEllipsis));
				s = pr.Stringer(parts);
				break;
			}
			if (isArray)
			{
				RefVar parts(pr.Array(2));
				SetArraySlot(parts, 0, s);
				RefVar f(pr.Var(kLitF));
				SetArraySlot(parts, 1, RefVar(NSCall(f, value, RefVar(NIEAdd(depth, RefVar(MAKEINT(1)))))));
				s = pr.Stringer(parts);
			}
			else
			{
				RefVar parts(pr.Array(4));
				SetArraySlot(parts, 0, s);
				SetArraySlot(parts, 1, tag);
				SetArraySlotRef(parts, 2, pr.Lit(kLitColon));
				RefVar printed;
				bool ignored = (NIEEqual(tag, RefVar(pr.Lit(kLitParent))) && ISNIL(pr.Var(kLitRunParent)))
							|| (NIEEqual(tag, RefVar(pr.Lit(kLitProto))) && ISNIL(pr.Var(kLitRunProto)));
				if (ignored)
					printed = pr.Lit(kLitIgnored);
				else
				{
					RefVar f(pr.Var(kLitF));
					printed = NSCall(f, value, RefVar(NIEAdd(depth, RefVar(MAKEINT(1)))));
				}
				SetArraySlot(parts, 3, printed);
				s = pr.Stringer(parts);
			}
			ForEachLoopNext(iter);
		}
	}
	RefVar parts(pr.Array(2));
	RefVar p(pr.Var(kLitP));
	SetArraySlot(parts, 0, RefVar(NSCall(p, s)));
	SetArraySlotRef(parts, 1, pr.Lit(isArray ? kLitArrayClose : kLitFrameClose));
	return pr.Stringer(parts);
}


static Ref
SignedNumber(Printer& pr, RefArg x, RefArg text, long plus, long empty)
{
	RefVar parts(pr.Array(2));
	SetArraySlotRef(parts, 0, pr.Lit(NIEGreaterThan(x, RefVar(MAKEINT(0))) ? plus : empty));
	SetArraySlot(parts, 1, text);
	return pr.Stringer(parts);
}


// NIE inetenbl.pkg part 1 +0x8898 (0x14649, ObjectToString's printer f)
Ref
NIEObjectPrinter(RefArg rcvr, RefArg x, RefArg depth, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	Printer pr;
	pr.closure = &closure;
	pr.env = &env;
	pr.stringer = NIEGlobalFunction(RefVar(pr.Lit(kLitStringer)));
	RefVar outer(pr.Array(2));
	RefVar value;

	if (ISNIL(pr.Call1(kLitIsValid, x)))
		value = pr.Lit(kLitInvalid);
	else if (NOTNIL(pr.Call1(kLitIsMagicPtr, x)))
	{
		RefVar stringer(NIEGlobalFunction(RefVar(pr.Lit(kLitStringer))));
		RefVar parts(pr.Array(2));
		SetArraySlotRef(parts, 0, pr.Lit(kLitMagicPrefix));
		SetArraySlot(parts, 1, RefVar(pr.Call1(kLitRefOf, x)));
		value = NSCall(stringer, parts);
	}
	else if (NOTNIL(pr.Call1(kLitIsFunction, x)))
	{
		RefVar count(pr.Call1(kLitGetFunctionArgCount, x));
		RefVar paramStr(NIEGlobalFunction(RefVar(pr.Lit(kLitParamStr))));
		RefVar params(pr.Array(2));
		SetArraySlot(params, 0, count);
		SetArraySlotRef(params, 1, NIEEqual(count, RefVar(MAKEINT(1))) ? TRUEREF : NILREF);
		value = NSCall(paramStr, RefVar(pr.Lit(kLitFuncFormat)), params);
	}
	else if (NOTNIL(pr.Call1(kLitIsFrame, x)))
		value = PrintAggregate(pr, x, depth, false);
	else if (NOTNIL(pr.Call1(kLitIsArray, x)))
		value = PrintAggregate(pr, x, depth, true);
	else if (IsString(x))
	{
		RefVar parts(pr.Array(3));
		SetArraySlotRef(parts, 0, MAKECHAR('"'));
		SetArraySlot(parts, 1, x);
		SetArraySlotRef(parts, 2, MAKECHAR('"'));
		value = pr.Stringer(parts);
	}
	else if (IsSymbol(x))
	{
		RefVar parts(pr.Array(2));
		SetArraySlotRef(parts, 0, MAKECHAR('\''));
		SetArraySlot(parts, 1, x);
		value = pr.Stringer(parts);
	}
	else if (NOTNIL(pr.Call1(kLitIsInteger, x)))
		value = SignedNumber(pr, x, RefVar(pr.Call1(kLitNumberStr, x)), kLitPlus3, kLitEmpty);
	else if (NOTNIL(pr.Call1(kLitIsNumber, x)))
	{
		RefVar formatted(NIEGlobalFunction(RefVar(pr.Lit(kLitFormattedNumberStr))));
		RefVar format(pr.Var(kLitFloatFormat));
		value = SignedNumber(pr, x, RefVar(NSCall(formatted, x, format)), kLitPlus4, kLitEmpty2);
	}
	else if (NOTNIL(pr.Call1(kLitIsImmediate, x)))
	{
		if (ISNIL(x))
			value = pr.Lit(kLitNil);
		else if ((Ref) x == TRUEREF)
			value = pr.Lit(kLitTrue);
		else
			value = pr.Call1(kLitSPrintObject, x);
	}
	else if (NOTNIL(pr.Call1(kLitIsBinary, x)))
	{
		RefVar parts(pr.Array(5));
		SetArraySlotRef(parts, 0, pr.Lit(kLitLess3));
		SetArraySlotRef(parts, 1, ClassOf(x));
		SetArraySlotRef(parts, 2, pr.Lit(kLitLength));
		SetArraySlotRef(parts, 3, MAKEINT(Length(x)));
		SetArraySlotRef(parts, 4, pr.Lit(kLitGreater3));
		value = pr.Stringer(parts);
	}
	else
		value = pr.Call1(kLitSPrintObject, x);

	SetArraySlot(outer, 0, value);
	SetArraySlotRef(outer, 1, pr.Lit(kLitSeparator));
	return pr.Stringer(outer);
}
