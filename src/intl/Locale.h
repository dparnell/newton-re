/*
	File:		intl/Locale.h

	Contains:	The international utilities: the current locale (a locale
				bundle frame - the ROM has one per country, e.g. 'Germany
				with 'Austria and 'SwitzGerman over it - kept in the
				`international` global frame: currentLocaleBundle, locales,
				localeTable, systemLocaleBundle), the locale's slots, the
				attribute cache the date and number formatting read (the
				day and month names, the number format's strings), and the
				date and time strings (Dates.h does the work).

	Reconstructed from the MP2x00 US ROM (0x000eba78-0x000ecfec,
	0x001f12a0); each function cites its origin.  The
	lexical dictionaries CacheLocaleAttributes replaces are LexParse.h's.
*/

#ifndef __LOCALE_H
#define __LOCALE_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// the international global frame (vars.international) and the locale
Ref		IntlResources(void);
Ref		GetCurrentLocale(void);
Ref		SetCurrentLocale(RefArg locale);				// a name, a symbol or a bundle; ==> the bundle, nil when unknown
Ref		FindLocaleBundleByName(RefArg name);			// a symbol (the localeTable's) or a title string (the locales')
Ref		GetLocaleSlot(RefArg locale, RefArg slot);		// locale nil/'currentLocaleBundle: the current; 'systemLocaleBundle: the system's
Ref		GetLocaleSlot(RefArg slot);						// of the current locale
Boolean	CacheLocaleAttributes(void);					// ==> whether the locale has what the cache needs
Boolean	ROMCacheLocaleAttributes(void);
NewtonErr	InitInternationalUtils(void);				// the cache made and filled; the natives registered

// the cached attributes of the current locale (ROM 0x0c103464 gLocaleCache
// and the ten words after it: RefVar pointers there)
struct LocaleCache
{
	RefStruct	fLongDayOfWeek;			// +0x00  gLocaleCache
	RefStruct	fAbbrDayOfWeek;			// +0x04
	RefStruct	fTerseDayOfWeek;		// +0x08
	RefStruct	fShortDayOfWeek;		// +0x0c
	RefStruct	fLongMonth;				// +0x10
	RefStruct	fDecimalPoint;			// +0x14
	RefStruct	fGroupSepStr;			// +0x18
	RefStruct	fMinusPrefix;			// +0x1c
	RefStruct	fMinusSuffix;			// +0x20
	RefStruct	fCurrencyPrefix;		// +0x24
	RefStruct	fCurrencySuffix;		// +0x28
};
extern LocaleCache*	gLocaleCache;

// preferences (vars.userConfiguration)
Ref		GetPreference(RefArg slot);
void	SetPreference(RefArg slot, RefArg value);

// where the machine is: the seconds the local time is ahead of the clock's
// (which counts GMT).  Both come from the preferences and both throw when
// they are not there or are not integers, as the ROM does.
long	GMTOffset(void);						// ROM 0x002554c0 GMTOffset__Fv - userConfiguration.location.gmt
long	DaylightSavingsOffset(void);			// ROM 0x0025551c DaylightSavingsOffset__Fv - userConfiguration.daylightSavings

// the date and time strings: minutes since 1904 (or a date frame), the
// format spec (0: every element), into str of at most max UniChars, the
// formats of the locale (nil: the current one)
void	LongDateString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale);
void	ShortDateString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale);
void	TimeString(ULong minutes, ULong spec, UniChar* str, ULong max, RefArg locale);
void	TimeFrameString(RefArg dateFrame, ULong spec, UniChar* str, ULong max, RefArg locale);

extern const UniChar*	gSpaceStr;		// 0x0c101074  " "
extern const UniChar*	gZeroStr;		// 0x0c101078  "0"

void	RegisterLocaleNatives(void);

#endif	/* __LOCALE_H */
