# International utilities: locales and dates

Reverse-engineering notes on the ROM's international utilities - the
locale bundles and the date and time formatting built on them.
Reconstructed source: `src/intl/` (`Locale.h`, `Dates.h`); the host test
is `src/intl/tests/test_Dates.cpp`, which formats dates through the
German locale bundle read out of the ROM image.  How each fact was
established is stated with it.

## Locale bundles (`src/intl/Locale.h`)

A locale is a NewtonScript frame, the *locale bundle*.  The ROM has one
per country; the MP2100 D's are `'Germany` (ROM object 0x3c10ed, title
"Deutschland"), `'Austria` (0x3c15d5) and `'SwitzGerman` (0x3c1345), the
latter two prototyped on Germany and overriding a few slots.  A bundle
holds (read with `analysis/nsfunctions.py build/MP2100D --object 0x3c10ed`):

- `title`, `localeSym`/`localeslot`, `sortID`, `firstDayOfWeek` (1:
  Monday), `useWeekNumber`, `keyboardLayout`, `defaultPaperSize`;
- `longDateFormat` (0x3bf511): `longDofWeek`, `abbrDofWeek` ("Son",
  "Mon", "Die", "Mit", ...), `terseDofWeek`, `shortDofWeek`, `longMonth`,
  `abbrMonth` ("Jan.", ..., "März", ..., "Okt.", ...), `longDateOrder`
  (an element order, below), `dayLeadingZ`, `longDateDelim` (the strings
  between the elements: ", ", ". ", " ", "") and the optional
  `long{Day,Month,Year}Suffix` strings;
- `shortDateFormat` (0x3bf021): `shortDateOrder`, `shortDateDelim`,
  `dayLeadingZ`, `monthLeadingZ`, `yearLeading` (1: two-digit years) and
  the short suffixes;
- `timeformat` (0x3bef95): `timeSepStr1`/`timeSepStr2` (":"),
  `morningStr`/`eveningStr` (AM/PM), `suffixStr` (" Uhr"), `hourLeadingZ`,
  `minuteLeadingZ`, `timeCycle` (0: 24-hour), `midNightForm`, `noonForm`;
- `numberformat`, the labels (`postalCodeLabel`, `streetLabel`, ...), the
  recognition dictionaries and the filters (not reconstructed).

A `...LeadingZ` slot of **0** means *add* the leading zero (the ROM tests
`== 0`; `dayLeadingZ 1` in the German bundle gives "3.10.1990").

The locale globals live in `vars.international` (`IntlResources`
0x000ed21c): `currentLocaleBundle`, `systemLocaleBundle`, `locales` (an
array of the bundles, found by `title`) and `localeTable` (a frame of
them by symbol).  `SetCurrentLocale`
(0x000edb40) accepts a bundle, its symbol or its title, installs it and
re-fills the *locale cache* - eleven RefVars at 0x0c103464 `gLocaleCache`
holding the four day-name arrays, the long month names and the number
format's strings, which the formatting code reads instead of walking the
frame each time (`CacheLocaleAttributes` 0x000ee3e0; the ROM's version
also rebuilds the recognition system's lexical dictionaries, NOT YET).
`GetLocaleSlot` (0x000ed268) resolves `nil` and `'currentLocaleBundle` to
the current bundle and `'systemLocaleBundle` to the system's.  The
NewtonScript `GetLocale`/`SetLocale` are `FGetLocale`/`FSetLocale`
(0x001f36b8, 0x001f36bc).  Preferences (`GetPreference`/`SetPreference`,
0x0012ab34, 0x0012aba8) read `vars.userConfiguration`.

## Dates (`src/intl/Dates.h`)

`TDate` (0x28 bytes: `fYear`, `fMonth` 1-12, `fDate` 1-31, `fHour`,
`fMinute`, `fSecond`, `fDayOfWeek` Sunday 0, and three RefVars caching
the locale's three format frames) converts between the calendar fields
and the Newton's time scales:

- **minutes since 1904** - the unit of `Time()`, of `RealClock()` and of
  every soup's dates; `InitWithMinutes` 0x0008f9a8 divides into days,
  then works in *quarter days* (`days*4 / 1461` years, the Julian-style
  leap cycle) and finds the month with the fixed-point constant 3919/128
  from the 1st of March, so February's length only matters before it;
  `TotalMinutes` 0x0008fae8 is the inverse (`TotalDays`: `(years*1461+3)
  >> 2` plus `(months*3917 + 52) >> 7` from March);
- **seconds since 1904** (`InitWithSeconds` 0x0008f978, `TotalSeconds`
  0x0008d980) in a 32-bit word that wrapped in 1972 - `TimeInSeconds()`
  (0x0008adb0) therefore counts from the start of **1993** (0xa7693a00
  seconds after 1904) and `DateFromSeconds`/`SetTimeInSeconds` add it
  back;
- ticks (`Ticks()` 0x002531b0: the global time in 1/60 s, masked to 31
  bits).

`fDayOfWeek` is `(days + 5) % 7` - the 1st of January 1904 was a Friday.
Years run to 2919 (`kLastYear`); `IsValidDate` 0x0008dbc4 checks the
month and the day against `DaysInMonth` (0x0008d9c4: the 30/31 table,
29 in a leap February).  `IncrementMonth` 0x0008dc38 moves by whole
months clamping the day (`IncrementMonth(31 Jan, 1)` is 28 Feb).

### Format specs

The date and time strings take a *format spec*: a run of 6-bit groups,
each a 3-bit element type followed by a 3-bit format, read from the low
bits up (`GetNextElementType` 0x0008dfe0, `GetNextElementFormat`
0x0008e074).  Date elements are 1 day, 2 day of week, 3 month, 4 year;
formats 1 long, 2 abbreviated, 3 terse, 4 short, 5 numeric.  Time
elements are 1 hour, 2 minute, 3 second, 4 AM/PM, 5 the suffix.  A spec
of **0** asks for every element in the locale's default format.  The ROM
keeps the standard specs in the frame `Rdatetimestrspecs` (0x6297fd,
magic pointer @66): `longDateStrSpec`, `abbrDateStrSpec`,
`yearMonthDayStrSpec`, `shortTimeStrSpec`, ... - the values the
NewtonScript constants `kIncludeAllElements`, `kFormatLongDate` etc.
expand to.

The same encoding orders the elements: `longDateOrder` 11711050 in the
German bundle decodes to *day of week (long), day (long), month
(numeric), year (numeric)* and, with the delimiters, gives "Mittwoch, 3.
10 1990" for a spec of 0 and "Mittwoch, 3. Oktober 1990" when the spec
asks for a long month (the spec's format wins over the order's).

`LongDateString` 0x0008e088 walks the order, emitting the wanted
elements through `DateElementString` 0x0008eba4 (which picks the name
array for the format, falling back short → terse → abbreviated → long,
adds the leading zeros and the suffixes) and the delimiter after each
but the last; the year is dropped when out of range.  `ShortDateString`
0x0008e3e0 does the same numerically with `shortDateOrder`; `TimeString`
0x0008e68c emits the hour (12-hour with `timeCycle` 1, `midNightForm`/
`noonForm` for 0 and 12), `timeSepStr1`, the minutes, `timeSepStr2` and
the seconds, then the AM/PM string and the suffix ("14:05:00 Uhr").

### NewtonScript functions

`Time`, `TimeInSeconds`, `Ticks`, `Date`, `DateFromSeconds`,
`TotalMinutes`, `DateNTime` ("3.10.1990 14:05"), `HourMinute`,
`ShortDate` ("Mit 3.10."), `LongDateStr`, `ShortDateStr`, `TimeStr`,
`TimeFrameStr`, `SetTime`, `SetTimeInSeconds`, `IsValidDate`,
`IncrementMonth`, `WeekNumber` (0x0008b5b0 `WeekNumCalc`: the week
of the year counted from the week holding the 1st of January, weeks
starting on `firstDayOfWeek`; with the locale's `weekNumberType` 1 the
ISO rule - the week is the one holding its Thursday, and week 1 is the
first with four or more days in the year), `StringToDate`, `StringToDateFrame`, `StringToTime` (the
string parsers return nil/-1: they need the recognition system's
lexical dictionaries, NOT YET).  `Date()` returns a `canonicalDate`
frame (`Rcanonicaldate`: year, month, date, dayOfWeek, hour, minute,
second, daysInMonth); `TotalMinutes` accepts a partial
frame (missing slots are 1904/1/1 0:00).

## Not yet reconstructed

- Reading dates and times out of strings (`StringToDateFields`, the
  AirusA lexical dictionaries `dateDictionary`, `timeDictionary`, ...).
- The meeting and repeat functions (0x0008bc98-0x0008d8b0).
- Number formatting (`numberformat`, `_IntlNumberMunge`) and the
  recognition dictionaries the locale cache rebuilds.
