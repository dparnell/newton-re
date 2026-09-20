# International utilities: locales and dates

Reverse-engineering notes on the ROM's international utilities - the
locale bundles and the date and time formatting built on them.
Reconstructed source: `src/intl/` (`Locale.h`, `Dates.h`); the host test
is `src/intl/tests/test_Dates.cpp`, which formats dates through the
'USA locale bundle read out of the ROM image.  How each fact was
established is stated with it.

## Locale bundles (`src/intl/Locale.h`)

A locale is a NewtonScript frame, the *locale bundle*.  The ROM has one
per country; the MP2x00 US's are `'USA` (ROM object 0x4a4d09, title
"U.S."), `'Canada` (0x4a5029), `'CanadaFr` (0x4a5225), `'UK` (0x4a5451),
`'Australia` (0x4a5641) and `'Sweden` (0x4a5775), the others prototyped on
'USA and overriding a few slots.  (The MP2100 D carries `'Germany`,
`'Austria` and `'SwitzGerman` instead, and nothing English.)  A bundle
holds (read with `analysis/nsfunctions.py build/MP2x00US --object 0x4a4d09`):

- `title`, `localeSym`/`localeslot`, `sortID`, `firstDayOfWeek` (1:
  Monday), `useWeekNumber`, `keyboardLayout`, `defaultPaperSize`;
- `longDateFormat` (0x4a3769): `longDofWeek`, `abbrDofWeek` ("Sun",
  "Mon", "Tue", "Wed", ...), `terseDofWeek`, `shortDofWeek`, `longMonth`,
  `abbrMonth` ("Jan", ..., "Oct", ...), `longDateOrder` (an element
  order, below), `dayLeadingZ`, `longDateDelim` (the strings between the
  elements: "", ", ", " ", ", ") and the optional
  `long{Day,Month,Year}Suffix` strings;
- `shortDateFormat` (0x4a3179): `shortDateOrder`, `shortDateDelim` ("/"),
  `dayLeadingZ`, `monthLeadingZ`, `yearLeading` (1: two-digit years) and
  the short suffixes;
- `timeformat` (0x4a30e9): `timeSepStr1`/`timeSepStr2` (":"),
  `morningStr`/`eveningStr` (" am"/" pm"), `suffixStr` (""),
  `hourLeadingZ`, `minuteLeadingZ`, `timeCycle` (1: 12-hour),
  `midNightForm`, `noonForm`;
- `numberformat`, the labels (`postalCodeLabel`, `streetLabel`, ...), the
  recognition dictionaries and the filters (not reconstructed).

A `...LeadingZ` slot of **0** means *add* the leading zero (the ROM tests
`== 0`; `dayLeadingZ 1` in the U.S. bundle gives "10/3/90").

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

The same encoding orders the elements: `longDateOrder` 11702986 in the
U.S. bundle decodes to *day of week (long), month (long), day (long),
year (numeric)* and, with the delimiters, gives "Wednesday, October 3,
1990" both for a spec of 0 and when the spec asks for a long month
(the spec's format wins over the order's).  The MP2100 D's German bundle
has 11711050 there - day of week, day, month, year - and gives
"Mittwoch, 3. 10 1990" and "Mittwoch, 3. Oktober 1990".

`LongDateString` 0x0008e088 walks the order, emitting the wanted
elements through `DateElementString` 0x0008eba4 (which picks the name
array for the format, falling back short → terse → abbreviated → long,
adds the leading zeros and the suffixes) and the delimiter after each
but the last; the year is dropped when out of range.  `ShortDateString`
0x0008e3e0 does the same numerically with `shortDateOrder`; `TimeString`
0x0008e68c emits the hour (12-hour with `timeCycle` 1, `midNightForm`/
`noonForm` for 0 and 12), `timeSepStr1`, the minutes, `timeSepStr2` and
the seconds, then the AM/PM string and the suffix ("2:05:00 pm").

### NewtonScript functions

`Time`, `TimeInSeconds`, `Ticks`, `Date`, `DateFromSeconds`,
`TotalMinutes`, `DateNTime` ("10/3/90 2:05 pm"), `HourMinute`,
`ShortDate` ("Wed 10/3"), `LongDateStr`, `ShortDateStr`, `TimeStr`,
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

## Numbers (`src/intl/NumberFormat.h`)

The locale's `numberformat` frame (U.S. 0x4a3065: `decimalpoint` ".",
`groupSepStr` ",", `groupWidth` 3, `minusPrefix` "-", `minusSuffix` "",
`currencyPrefix` "$", `currencySuffix` "", `decimalLeadingZ` 0) is
cached with the date names; `ROMCacheLocaleAttributes` also sets
`gNumberGroupWidth` (0x0c10108c) and `gNumberLeadingZero` (0x0c101090)
and drops the *prototype strings* so that they are remade for the new
locale: `PositiveIntProtoStr` 0x000ed530 "^0", `PositiveNumberProtoStr`
0x000ed358 "^0" + decimal point + "^1", and the negative ones
(0x000ed584, 0x000ed440) wrapped in the minus prefix and suffix.
`ParamString` 0x000ef0d8 fills such a prototype: each `^digit` is replaced
by a UniChar string argument (the arguments taken in the order the markers
appear, the digit choosing among them), within a maximum length.  The ROM
has a slip here: `gNumberLeadingZero` is set from a second read of
`groupWidth` instead of `decimalLeadingZ` (the register holding the
symbol was reused); the host reads `decimalLeadingZ` (`DEVIATION`).

Two paths put a number into text.  `NumberString` 0x000eec44 (what
`NumberStr`, `StringObject` and a `FormattedNumberStr` with a format
*string* use) prints |d| with the caller's printf format, then localises
the result: the integer digits are grouped (`gNumberGroupWidth` from the
right), the sign becomes the minus prefix and suffix through the negative
prototype, the fraction follows the locale's decimal point; whatever the
format put before the first digit (a width's spaces) and after the number
is carried over; an exponent form (`1e+21`) only gets its decimal point
replaced.  It answers -2 `kNumberTooLarge`/-3 `kNumberTooSmall` beyond the
doubles or when the text would not fit a positive/negative number.

`IntegerStringSpec` 0x000eea4c and `NumberStringSpec` 0x000eeae0 (a
`FormattedNumberStr` with an *integer* spec) print the digits themselves
(the ROM with its C library's `_fp_display`: 17 significant digits, the
rest `<`/`>` markers turned into zeros; the host with snprintf) and hand
them to `_IntlNumberMunge` 0x000ee5a4 with the spec's bits, which the ROM
symbols do not name: 0x0f the decimal places, 0x10 the currency prefix and
suffix, 0x20 group the digits, 0x40 parentheses instead of the minus
prefix and suffix, 0x80 a real with the low bits' decimal places (an
integer is converted; without it a real gets six places with the trailing
zeros dropped), 0x100 times 100 with a `%`, 0x200 trailing zeros dropped.
More than twenty digits is `kNumberTooLarge` (`FormattedNumberStr` returns
the ROM's "Zahl zu groß"), a text longer than the caller's room -10 (nil).

## Repeating meetings (`src/intl/Meetings.h`)

The ROM keeps the Dates application's repeating-meeting engine next to
`TDate` (0x0008bc98-0x0008d910).  A *repeat template* is an entry of the
repeating-meeting soup: `mtgStartDate` (the first instance; its time of
day is every instance's), `mtgDuration`, `repeatType`, `mtgInfo`,
`mtgStopDate` (0x1fffffff: forever) and `exceptions`, an array of
`[date, entry]` pairs - the instance at `date` is deleted (`entry` nil)
or replaced by `entry`, a meeting of its own.  `repeatType` selects the
stepper and says what `mtgInfo` encodes (read out of `NextMeeting`
0x0008d5fc, a switch that inlines them):

| repeatType | mtgInfo | stepper |
|---|---|---|
| 0 kDayOfWeek, 1 kWeekInMonth | day-of-week bits 0x800 Sunday .. 0x20 Saturday, week-of-month bits 0x10 first .. 0x01 last (0x1f every week) | `NextDayOfWeek` 0x0008bc98 |
| 2 kDateInMonth | the date in the low 6 bits | `NextDateOfMonth` 0x0008bdcc |
| 3 kDateInYear | `(month << 8) \| date` | `NextDateOfYear` 0x0008ceb4 |
| 4 kPeriod | `(first instance's TotalDays << 8) \| period in days` | `NextPeriod` 0x0008d5a0 |
| 5 kNever, 6 | - | the date is left alone |
| 7 kWeekInYear | `(month << 12) \|` the day-of-week and week bits | `NextDateByWeekInYear` 0x0008d4dc |

Each stepper moves a `TDate` forward to the next instance on or after it
(the same day counts), leaving the time of day alone; `NextDayOfWeek`
first finds the next wanted weekday, then the next wanted week of the
month (the last-week bit is the month's last such day), stepping into the
next month when needed.  `NextDateOfMonth` has a ROM bug: the day-of-week
shift is uninitialised when the wanted date is past the month's end
(harmless - every caller normalises the date afterwards; the host computes
it, `DEVIATION`).  `simplePrevMeeting` 0x0008cc90 goes back a month, a
year or two weeks and forward again to the next instance.

`FNextMeeting` 0x0008cf14 (`NextMeeting(startTime, template)`) steps from
`max(startTime, mtgStartDate)` at the template's time of day (the day
after when that time is already past on the start day), skipping the
instances that are exceptions, until it finds one; a replacement meeting
between start and the instance is the answer instead
(`FindExceptionMeetingInRange` 0x0008cb68); 0 past `mtgStopDate`.
`FPrevMeeting` 0x0008d174 steps back with `simplePrevMeeting` and refines
forward with `NextMeeting` so that exceptions are honoured.
`GetNextMeetingTime(template, startTime)` 0x0008d494 is `NextMeeting`
with the arguments swapped.  A template whose slots cannot be read makes
these repair it (`FixupRepeatFrame` 0x0008be64: integer slots defaulted,
the first malformed exception dropped, the entry written back) and return
0.

`GetAllMeetings(meetingSoup, repeatSoup, start, end)` 0x0008c908 (`end`
nil: a day; `GetAllMeetingsUnique` 0x0008caec stops at the first instance
of each template) queries the meeting soup by `mtgStartDate`
(`dateQuerySpec` 0x6297e1, `beginKey`/`endExclKey` the range) and the
repeating-meeting soup by `mtgStopDate` (`repeatQuerySpec` 0x62e551,
templates not yet stopped), generates each template's instances in the
range (`GetRepeatingMeetings` 0x0008c374) as clones of
`protoInstanceOfRepeatingMeeting` 0x50f82d (`viewStationery
'RepeatingMeeting`, `class 'meeting`, `mtgStartDate`, `repeatTemplate`
the entry; a cribNote template's instances are cribNotes, `viewBounds`
carried) and merges them with the template's exceptions
(`MergeMeetingLists` 0x0008c180: an exception at an instance's time
replaces it, replacement meetings in the range are added, everything
kept in `mtgStartDate` order with `BInsert`); nil when there is nothing.

## The world map's coordinates

`intl/Coordinates.h` is the arithmetic the Time Zones application's map of
the world is driven by.  A place's longitude and latitude are integers of
2^28 to a half turn (so the whole map is 2^29 across and the poles are at
+/-2^27), which is exactly a 2.30 `Fract` of a turn once doubled - and
doubling is all `LongitudeToCoordinate` 0x002551c0 does before adding half
a map and scaling by the map's width:

    x = FractMultiply(width, longitude * 2 + 0x20000000)

`LatitudeToCoordinate` 0x00255220 takes the latitude four times over and
measures down from the top (`0x20000000 - latitude * 4`), since a latitude
only reaches a quarter turn.

`CoordinateToLongitude` 0x00255280 and `CoordinateToLatitude` 0x002552e4
undo them through `FractDivide`.  The longitude's inverse *adds* half a
turn where it ought to subtract one - `(FractDivide(x, width) * 4 +
0x80000000) / 8` - and gets the right answer because the two halves
overflow to nothing on an ARM; the reconstruction does the same arithmetic
in `uint32_t` so that the host wraps where the ARM wraps.  A coordinate of
exactly 2^28 overflows the other way, which is why both ends of the date
line are the left edge of the map.

`CircleDistance` 0x00255348 (`CircleDistance(long1, lat1, long2, lat2,
units)`) is the spherical law of cosines over the same units - the angles
scaled to 16.16 radians by `pi/2`, `FractSin`/`FractCos` (0x00038060,
0x00038088: `FractSineCosine` with the radians turned into degrees first)
for the sines and cosines, `FixedACos` for the arc - times 3959 for
`'miles` or 6371 otherwise, and rounded to the nearest ten.  An arc below
0x60 (a sixty-fourth of a degree) is called zero.

## Not yet reconstructed

- Reading dates and times out of strings (`StringToDateFields`, the
  AirusA lexical dictionaries `dateDictionary`, `timeDictionary`, ...).
- Reading numbers out of strings (`StringToNumber` uses the C library's
  strtod; the ROM's `TNumberParser` honours the locale's separators) and
  the recognition dictionaries the locale cache rebuilds.
