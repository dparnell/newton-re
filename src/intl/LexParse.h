/*
	File:		LexParse.h

	Contains:	Text read through the locale's lexical dictionaries: the
				parse buffer a word's characters are gathered in, the
				converters that turn what it holds into a field of a date
				or a part of a number (ConvertBuffer), and the number parser
				(TNumberParser) StringToNumber reads a number with.

				A lexical dictionary is an Airus trie whose words carry an
				attribute byte: the top two bits say what the character just
				walked does to the buffer - 0x40 keep it, 0x80 finish the
				buffer (convert it) without it, 0xc0 finish the buffer and
				start the next one with it - and the low six bits name the
				converter the buffer goes through when it is finished (5 a
				year, 6 a month, 9 an hour, 12 am, 0x27 tomorrow, ...).  The
				walk itself is the recognition system's (ParseString over
				its Airus engine, recognition/ParseString.cpp), reached
				through gParseStringProc.

	Written by:	Newton Research Group, 2026.
*/

#if !defined(__LEXPARSE_H)
#define __LEXPARSE_H 1

#include "Newton.h"
#include "objects.h"

// ROM 0x0c100f8c-0x0c100f98 - the lexicons the locale carries (their
// time, date, phone and number dictionaries), opened by the recognition
// system's InitDictionaries.
extern Handle	gTimeLexDictionary;		// ROM 0x0c100f8c gTimeLexDictionary
extern Handle	gDateLexDictionary;		// ROM 0x0c100f90 gDateLexDictionary
extern Handle	gPhoneLexDictionary;	// ROM 0x0c100f94 gPhoneLexDictionary
extern Handle	gNumberLexDictionary;	// ROM 0x0c100f98 gNumberLexDictionary

// The characters of one field gathered as they are walked: 0x42 bytes.
struct TParseBuffer
{
	char		fChars[64];		// +0x00
	UByte		fCount;			// +0x40  characters gathered
	UByte		fConverter;		// +0x41  what they become (the attribute's low six bits)
};

void	InitParseBuffer(TParseBuffer* buffer);					// ROM 0x00181ac8 InitParseBuffer__FP12TParseBuffer
long	ConvertBuffer(TParseBuffer* buffer, void* into);		// ROM 0x00181adc ConvertBuffer__FP12TParseBufferPv

// The pieces of a number a string spells, filled in by ConvertBuffer's
// number converters (0xe the integer part, 0xf the decimal part, 0x10 a
// minus sign, 0x33/0x34 a prefix and a suffix).  0x28 bytes.
enum TNumberType { kNumberTypeNone = 0 };

class TNumberParser
{
public:
				TNumberParser();									// ROM 0x001473a4 __ct__13TNumberParserFv

	void		Reset(void);										// ROM 0x001473d8 Reset__13TNumberParserFv
	double		StringToNumber(const UniChar* str, ULong* consumed, ULong length);	// ROM 0x00147420 StringToNumber__13TNumberParserFPCUsPUlUl
	void		SetInteger(char* digits);							// ROM 0x001474bc SetInteger__13TNumberParserFPc
	void		SetDecimal(char* digits);							// ROM 0x001474e8 SetDecimal__13TNumberParserFPc
	void		SetPrefix(char* prefix);							// ROM 0x00147520 SetPrefix__13TNumberParserFPc
	void		SetSuffix(char* suffix);							// ROM 0x0014752c SetSuffix__13TNumberParserFPc
	void		SetSign(UByte positive);							// ROM 0x00147538 SetSign__13TNumberParserFUc
	void		SetNumberType(TNumberType type);					// ROM 0x00147540 SetNumberType__13TNumberParserF11TNumberType

	double		fInteger;			// +0x00
	double		fDecimal;			// +0x08  the digits after the point, as a fraction
	ULong		fDecimalLength;		// +0x10  how many there were
	char		fPrefix[4];			// +0x14
	char		fSuffix[4];			// +0x18
	TNumberType	fType;				// +0x1c
	UByte		fSign;				// +0x20  0: negative
	long		fField24;			// +0x24
};

double	DecimalStrToDouble(char* digits);						// ROM 0x00147330 DecimalStrToDouble__FPc

// DEVIATION (library layering): ParseString walks an Airus dictionary,
// which is the recognition system's, above this library; it installs its
// ParseString here (InitDictionaries) and the dates and numbers call it
// through CallParseString, which answers -1 with nothing consumed until
// it has.  ==> the dictionary's id, -1 when nothing was parsed.
typedef long (*ParseStringProcPtr)(Handle dictionary, void* into, const UniChar* str, ULong* consumed, ULong length);
extern ParseStringProcPtr	gParseStringProc;
long	CallParseString(Handle dictionary, void* into, const UniChar* str, ULong* consumed, ULong length);

// ... and ReplaceDictionaryHandle likewise (the Airus dictionary opened
// over the locale's binary is the recognition system's too): a lexicon
// opened afresh from the current locale's slot, the one it replaces
// disposed of.  Nothing happens until it is installed.
typedef void (*ReplaceDictionaryHandleProcPtr)(Handle* dictionary, RefArg slot);
extern ReplaceDictionaryHandleProcPtr	gReplaceDictionaryHandleProc;
void	CallReplaceDictionaryHandle(Handle* dictionary, RefArg slot);

#endif	/* __LEXPARSE_H */
