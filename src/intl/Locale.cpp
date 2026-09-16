/*
	File:		intl/Locale.cpp

	Contains:	The international utilities (Locale.h): the current locale,
				its slots, the attribute cache, preferences, the date and
				time string wrappers over TDate (Dates.h).

	Reconstructed from the MP2100 D ROM; each function cites its origin.
*/

#include "Locale.h"
#include "Dates.h"
#include "Meetings.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "Unicode.h"
#include "REPTranslators.h"
#include "OSErrors.h"
#include "NewtonMemory.h"

// ROM 0x0c101074 gSpaceStr
// ROM 0x0c101078 gZeroStr
static const UniChar kSpaceStr[2] = { ' ', 0 };
static const UniChar kZeroStr[2] = { '0', 0 };
const UniChar*	gSpaceStr = kSpaceStr;
const UniChar*	gZeroStr = kZeroStr;

// ROM 0x0c103464 gLocaleCache (and the ten cached attributes after it)
LocaleCache*	gLocaleCache = nil;


/*------------------------------------------------------------------------------
	T h e   l o c a l e
------------------------------------------------------------------------------*/

// ROM 0x000ed21c IntlResources__Fv
// The international global frame.
Ref
IntlResources(void)
{
	return GetFrameSlotRef(gVarFrame, RSSYMinternational);
}


// ROM 0x000edafc GetCurrentLocale__Fv
Ref
GetCurrentLocale(void)
{
	RefVar intl(IntlResources());
	return GetProtoVariable(intl, RSSYMcurrentlocalebundle, nil);
}


// ROM 0x000edcb4 FindLocaleBundleByName__FRC6RefVar
// A symbol: the localeTable's bundle of that name; a string: the
// bundle of the locales array whose title it is.
Ref
FindLocaleBundleByName(RefArg name)
{
	RefVar found;
	RefVar intl(IntlResources());
	if (IsSymbol(name))
	{
		RefVar table(GetProtoVariable(intl, RSSYMlocaletable, nil));
		if ((Ref) table != NILREF)
			found = GetProtoVariable(table, name, nil);
	}
	else if (IsString(name))
	{
		RefVar locales(GetProtoVariable(intl, RSSYMlocales, nil));
		if (IsArray(locales))
		{
			long count = Length(locales);
			RefVar bundle;
			for (long i = 0; i < count; i++)
			{
				bundle = GetArraySlotRef(locales, i);
				if ((Ref) bundle == NILREF)
					continue;
				RefVar title(GetProtoVariable(bundle, RSSYMtitle, nil));
				if (Ustrcmp(GetCString(title), GetCString(name)) == 0)
				{
					found = bundle;
					break;
				}
			}
		}
	}
	return found;
}


// ROM 0x000edb40 SetCurrentLocale__FRC6RefVar
// The locale (a bundle, or a name FindLocaleBundleByName knows) made
// current and its attributes cached - put back when it lacks them; the
// preference set and UpdateLocaleFromUserConfig called.
Ref
SetCurrentLocale(RefArg locale)
{
	RefVar bundle;
	if (IsString(locale) || IsSymbol(locale))
		bundle = FindLocaleBundleByName(locale);
	else if (IsFrame(locale))
		bundle = locale;
	if ((Ref) bundle != NILREF)
	{
		RefVar previous(GetCurrentLocale());
		RefVar intl(IntlResources());
		SetFrameSlot(intl, RSSYMcurrentlocalebundle, bundle);
		if (!CacheLocaleAttributes())
		{
			SetFrameSlot(intl, RSSYMcurrentlocalebundle, previous);
			bundle = NILREF;
		}
		else
			SetPreference(RSSYMlocale, RefVar(GetFrameSlotRef(bundle, RSSYMtitle)));
	}
	NSCallGlobalFn(RSSYMupdatelocalefromuserconfig);
	return bundle;
}


// ROM 0x000ed268 GetLocaleSlot__FRC6RefVarT1
// A slot of the locale: nil or 'currentLocaleBundle for the current one,
// 'systemLocaleBundle for the system's, else the bundle given.
Ref
GetLocaleSlot(RefArg locale, RefArg slot)
{
	RefVar bundle;
	if ((Ref) locale == NILREF || EQRef(locale, RSSYMcurrentlocalebundle))
		bundle = GetCurrentLocale();
	else if (EQRef(locale, RSSYMsystemlocalebundle))
	{
		RefVar intl(IntlResources());
		bundle = GetProtoVariable(intl, RSSYMsystemlocalebundle, nil);
	}
	else
		bundle = locale;
	return GetProtoVariable(bundle, slot, nil);
}


// ROM 0x000ee478 GetLocaleSlot__FRC6RefVar
Ref
GetLocaleSlot(RefArg slot)
{
	RefVar bundle(GetCurrentLocale());
	return GetProtoVariable(bundle, slot, nil);
}


// ROM 0x000ede98 ROMCacheLocaleAttributes__Fv
// The current locale's day and month names and number format strings
// cached (the shorter day names fall back on the longer); false when it
// has no long day names or a number format string is missing, and the
// cache is left as it was.  NOT YET RECONSTRUCTED: the lexical
// dictionaries (ReplaceDictionaryHandle) that follow.
Boolean
ROMCacheLocaleAttributes(void)
{
	RefVar locale(GetCurrentLocale());
	RefVar dateFormat(GetProtoVariable(locale, RSSYMlongdateformat, nil));
	RefVar longDofWeek(GetProtoVariable(dateFormat, RSSYMlongdofweek, nil));
	RefVar abbrDofWeek(GetProtoVariable(dateFormat, RSSYMabbrdofweek, nil));
	RefVar terseDofWeek(GetProtoVariable(dateFormat, RSSYMtersedofweek, nil));
	RefVar shortDofWeek(GetProtoVariable(dateFormat, RSSYMshortdofweek, nil));
	if ((Ref) longDofWeek == NILREF)
		return false;
	if ((Ref) abbrDofWeek == NILREF)
		abbrDofWeek = longDofWeek;
	if ((Ref) terseDofWeek == NILREF)
		terseDofWeek = abbrDofWeek;
	if ((Ref) shortDofWeek == NILREF)
		shortDofWeek = terseDofWeek;
	RefVar longMonth(GetProtoVariable(dateFormat, RSSYMlongmonth, nil));
	RefVar numberFormat(GetProtoVariable(locale, RSSYMnumberformat, nil));
	RefVar decimalPoint(GetProtoVariable(numberFormat, RSSYMdecimalpoint, nil));
	RefVar groupSepStr(GetProtoVariable(numberFormat, RSSYMgroupsepstr, nil));
	RefVar minusPrefix(GetProtoVariable(numberFormat, RSSYMminusprefix, nil));
	RefVar minusSuffix(GetProtoVariable(numberFormat, RSSYMminussuffix, nil));
	RefVar currencyPrefix(GetProtoVariable(numberFormat, RSSYMcurrencyprefix, nil));
	RefVar currencySuffix(GetProtoVariable(numberFormat, RSSYMcurrencysuffix, nil));
	RINT(GetProtoVariable(numberFormat, RSSYMgroupwidth, nil));
	if ((Ref) decimalPoint == NILREF || (Ref) groupSepStr == NILREF || (Ref) minusPrefix == NILREF || (Ref) minusSuffix == NILREF)
		return false;
	// NOT YET RECONSTRUCTED: ReplaceDictionaryHandle(gTimeLexDictionary, 'timeDictionary),
	// (gDateLexDictionary, 'dateDictionary), (gPhoneLexDictionary, 'phoneDictionary),
	// (gNumberLexDictionary, 'numberDictionary)
	gLocaleCache->fLongDayOfWeek = longDofWeek;
	gLocaleCache->fAbbrDayOfWeek = abbrDofWeek;
	gLocaleCache->fTerseDayOfWeek = terseDofWeek;
	gLocaleCache->fShortDayOfWeek = shortDofWeek;
	gLocaleCache->fLongMonth = longMonth;
	gLocaleCache->fDecimalPoint = decimalPoint;
	gLocaleCache->fGroupSepStr = groupSepStr;
	gLocaleCache->fMinusPrefix = minusPrefix;
	gLocaleCache->fMinusSuffix = minusSuffix;
	gLocaleCache->fCurrencyPrefix = currencyPrefix;
	gLocaleCache->fCurrencySuffix = currencySuffix;
	return true;
}


// ROM 0x000ee3e0 CacheLocaleAttributes__Fv
// The attributes cached, and the number prototype strings remade
// (NOT YET RECONSTRUCTED: NegativeNumberProtoStr, PositiveNumberProtoStr).
Boolean
CacheLocaleAttributes(void)
{
	return ROMCacheLocaleAttributes();
}


// ROM 0x000ed050 InitInternationalUtils__Fv
// The cache made (its refs GC roots) and filled from the current locale.
NewtonErr
InitInternationalUtils(void)
{
	if (gLocaleCache == nil)
		gLocaleCache = new LocaleCache;
	if (gLocaleCache == nil)
		return kError_No_Memory;
	RegisterLocaleNatives();
	RegisterDateNatives();
	RegisterMeetingNatives();
	InstallHostNatives();						// host: into the function frame when there are no ROM objects
	if (GetCurrentLocale() != NILREF && CacheLocaleAttributes())
		return noErr;
	return kError_No_Memory;
}


/*------------------------------------------------------------------------------
	P r e f e r e n c e s
------------------------------------------------------------------------------*/

// ROM 0x0012ab34 GetPreference__FRC6RefVar
// A slot of the user configuration (vars.userConfiguration).
Ref
GetPreference(RefArg slot)
{
	RefVar config(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
	return GetProtoVariable(config, slot, nil);
}


// ROM 0x0012aba8 SetPreference__FRC6RefVarT1
void
SetPreference(RefArg slot, RefArg value)
{
	RefVar config(GetFrameSlotRef(gVarFrame, RSSYMuserconfiguration));
	SetFrameSlot(config, slot, value);
}


/*------------------------------------------------------------------------------
	D a t e   a n d   t i m e   s t r i n g s
------------------------------------------------------------------------------*/

// ROM 0x000ed8f4 LongDateString__FUlT1PUsT1RC6RefVar
void
LongDateString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale)
{
	TDate date;
	str[0] = 0;
	date.InitWithMinutes(minutes);
	date.SetFormatResource(locale);
	date.LongDateString(spec, str, max);
}


// ROM 0x000ed974 ShortDateString__FUlT1PUsT1RC6RefVar
void
ShortDateString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale)
{
	TDate date;
	str[0] = 0;
	date.InitWithMinutes(minutes);
	date.SetFormatResource(locale);
	date.ShortDateString(spec, str, max);
}


// ROM 0x000ed9f4 TimeString__FUlT1PUsT1RC6RefVar
void
TimeString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale)
{
	TDate date;
	str[0] = 0;
	date.InitWithMinutes(minutes);
	date.SetFormatResource(locale);
	date.TimeString(spec, str, max);
}


// ROM 0x000eda78 TimeFrameString__FRC6RefVarUlPUsT2T1
void
TimeFrameString(RefArg dateFrame, ULong spec, UniChar* str, ULong max, RefArg locale)
{
	TDate date;
	str[0] = 0;
	date.InitWithDateFrame(dateFrame, false);
	date.SetFormatResource(locale);
	date.TimeString(spec, str, max);
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x001f36b8 FGetLocale__FRC6RefVar
static Ref
FGetLocale(RefArg /*rcvr*/)
{
	return GetCurrentLocale();
}


// ROM 0x001f36bc FSetLocale__FRC6RefVarT1
static Ref
FSetLocale(RefArg /*rcvr*/, RefArg locale)
{
	return SetCurrentLocale(locale);
}


void
RegisterLocaleNatives(void)
{
	RegisterNativeFunction("FGetLocale__FRC6RefVar", (void*) FGetLocale, 0);
	RegisterNativeFunction("FSetLocale__FRC6RefVarT1", (void*) FSetLocale, 1);
}
