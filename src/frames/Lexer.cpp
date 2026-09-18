/*
	File:		frames/Lexer.cpp

	Contains:	The NewtonScript lexer: TCompiler::GetToken and the input
				streams it reads (Compiler.h).

	yylex0 reads one token from the stream: comments (// to the line's end
	and the bracketed kind) and
	white space skipped; |symbols| and identifiers (NIL and TRUE are
	constants, the reserved words their tokens, the rest symbols);
	numbers (GetNumber: decimal, 0x hex, reals with a fraction or
	exponent); #line directives; @n magic pointers; "strings" and
	$characters with the \n \t \\ \xx \uxxxx escapes (a string's \u
	toggles hex mode: "A\u"); the two-character operators.
	GetToken drops a ; before end, else, ), ], }, comma, until or
	onexception and a comma before ] or }, looking one token ahead.
*/

#include "Compiler.h"
#include "ParserTables.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Unicode.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// the compiler's encoding for its 8-bit text (0x0c10..., gNewtonScriptCompilerCharacterEncoding)
long	gNewtonScriptCompilerCharacterEncoding = kMacRomanEncoding;

// with yydebug set, the lexer prints what it reads
static void dbprint(UniChar) { }
static void dbprint(const char*) { }


/* -------------------------------------------------------------------------------
	Input streams
------------------------------------------------------------------------------- */

// ROM 0x000eb360 __ct__12TInputStreamFv
TInputStream::TInputStream()
{
	fLineNumber = 0;
	fFilename[0] = 0;
}


// ROM 0x000eb3b8 __dt__12TInputStreamFv
TInputStream::~TInputStream()
{ }


// ROM 0x000eb464 GetFilename__12TInputStreamFv
const char*
TInputStream::GetFilename(void)
{
	return fFilename;
}


// ROM 0x000eb46c SetFilename__12TInputStreamFPc
void
TInputStream::SetFilename(const char* name)
{
	strncpy(fFilename, name, sizeof(fFilename) - 1);
	fFilename[sizeof(fFilename) - 1] = 0;
}


// ROM 0x000eb478 __ct__18TStringInputStreamFRC6RefVar
TStringInputStream::TStringInputStream(RefArg str)
{
	fString = str;
	fPosition = 0;
}


// ROM 0x000eb4e4 GetChar__18TStringInputStreamFv
UniChar
TStringInputStream::GetChar(void)
{
	if (End())
		return kEndOfStream;
	UniChar c = ((const UniChar*) BinaryData(fString))[fPosition++];
	if (c == '\r')
		fLineNumber++;
	return c;
}


// ROM 0x000eb544 UngetChar__18TStringInputStreamFUs
void
TStringInputStream::UngetChar(UniChar c)
{
	if (c == kEndOfStream)
		return;
	if (c == '\r')
		fLineNumber--;
	if (fPosition != 0)
		fPosition--;
}


// ROM 0x000eb57c End__18TStringInputStreamFv
// (the string's terminator is not read)
Boolean
TStringInputStream::End(void)
{
	return (ULong) (Length(fString) / sizeof(UniChar)) <= (ULong) (fPosition + 1);
}


// ROM 0x000eb5b0 __ct__17TStdioInputStreamFP13__FILE_structPc
TStdioInputStream::TStdioInputStream(FILE* file, const char* filename)
{
	fFile = file;
	SetFilename(filename);
}


// ROM 0x000eb614 GetChar__17TStdioInputStreamFv
// Host: a file's line feed is the Newton's carriage return (the ROM
// converts each byte through the compiler's encoding, two-byte
// characters included - NOT YET RECONSTRUCTED: IsFirstByteOf2Byte).
UniChar
TStdioInputStream::GetChar(void)
{
	int c = getc(fFile);
	if (c == EOF)
		return kEndOfStream;
	if (c == '\n')
		c = '\r';
	UniChar u = U_CONST_CHAR((unsigned char) c);
	if (u == '\r')
		fLineNumber++;
	return u;
}


// ROM 0x000eb3d0 UngetChar__17TStdioInputStreamFUs
void
TStdioInputStream::UngetChar(UniChar c)
{
	if (c == kEndOfStream)
		return;
	if (c == '\r')
	{
		fLineNumber--;
		ungetc('\n', fFile);
		return;
	}
	ungetc(A_CONST_CHAR(c), fFile);
}


// ROM 0x000eb45c End__17TStdioInputStreamFv
Boolean
TStdioInputStream::End(void)
{
	return feof(fFile) != 0;
}


/* -------------------------------------------------------------------------------
	Tokens
------------------------------------------------------------------------------- */

// ROM 0x00326860 ReservedWordToken__9TCompilerFPc
// The token of a reserved word (compared as symbols are: case
// insensitively), 0 for none.
int
TCompiler::ReservedWordToken(const char* name)
{
	for (long i = 0; i < kNumReservedWords; i++)
		if (symcmp((char*) name, (char*) gReservedWords[i].fName) == 0)
			return gReservedWords[i].fToken;
	return 0;
}


// ROM 0x003268b0 GetToken__9TCompilerFv
int
TCompiler::GetToken(void)
{
	if (fHasPushedToken)
	{
		fHasPushedToken = false;
		yylval = fPushedValue;
		fPushedValue = NILREF;
		return fPushedToken;
	}
	int token = yylex0();
	if (token == ';')
	{
		int next = yylex0();
		if (next == -1 || next == tokenEND || next == tokenELSE || next == ']' || next == ')' || next == '}' || next == ',' || next == tokenUNTIL || next == tokenONEXCEPTION)
			return next;
		fHasPushedToken = true;
		fPushedValue = yylval;
		fPushedToken = next;
		return ';';
	}
	if (token == ',')
	{
		int next = yylex0();
		if (next == '}' || next == ']')
			return next;
		fHasPushedToken = true;
		fPushedValue = yylval;
		fPushedToken = next;
		return ',';
	}
	return token;
}


// ROM 0x0032639c GetCharsUntil__9TCompilerFUsiRl
// The characters up to terminator, escapes resolved, in a malloc'd
// UniChar buffer (nil after an error); length is the buffer's size in
// bytes, terminator included.  A string (isString) goes on after its
// closing quote when, past white space and back-quotes, another opening
// quote follows.  In \u hex mode an escape takes 2 hex digits (a symbol,
// stored as one byte - the ROM's way) or 4 (a string).
UniChar*
TCompiler::GetCharsUntil(UniChar terminator, Boolean isString, long& length)
{
	long digitsPerChar = isString ? 4 : 2;
	long capacity = 0;
	long pos = 0;					// in bytes
	unsigned char* buffer = nil;
	Boolean inEscape = false;
	long hexDigits = -1;			// -1: not in hex mode; else the digits gathered so far
	long hexValue = 0;

	for (;;)
	{
		if (buffer == nil || pos + 4 >= capacity)
		{
			unsigned char* grown = (unsigned char*) malloc(capacity + 0x80);
			if (grown == nil)
				ThrowMsg((char*) "Compiler can't get buffer space");
			if (buffer != nil)
			{
				BlockMove(buffer, grown, capacity);
				free(buffer);
			}
			buffer = grown;
			capacity += 0x80;
		}
		UniChar c = fStream->GetChar();
		dbprint(c);
		if (c == kEndOfStream)
		{
			free(buffer);
			Error(kNSErrEOFInString);
			return nil;
		}
		if (inEscape)
		{
			if (c == 'u' || c == 'U')
			{
				if (hexDigits < 0)
					hexDigits = 0;
				else if (hexDigits == 0)
					hexDigits = -1;
				else
				{
					free(buffer);
					Error(kNSErrOddHexDigits);
					return nil;
				}
			}
			else if (hexDigits >= 0)
			{
				free(buffer);
				Error(kNSErrEscapeInHex);
				return nil;
			}
			else
			{
				UniChar escaped = c;
				if (c == 'n' || c == 'N')
					escaped = '\r';
				else if (c == 't' || c == 'T')
					escaped = '\t';
				*(UniChar*) (buffer + pos) = escaped;
				pos += 2;
			}
			inEscape = false;
			continue;
		}
		if (c == '\\')
		{
			inEscape = true;
			continue;
		}
		if (c == terminator)
		{
			if (hexDigits > 0)
			{
				free(buffer);
				Error(kNSErrOddHexDigits);
				return nil;
			}
			if (isString)
			{
				// another string after white space continues this one
				UniChar next;
				do
				{
					next = fStream->GetChar();
					dbprint(next);
				} while (IsWhiteSpace(next) || next == '`');
				if (next != kEndOfStream)
				{
					if (next == terminator)
						continue;
					fStream->UngetChar(next);
					dbprint("un");
				}
			}
			*(UniChar*) (buffer + pos) = 0;
			pos += 2;
			length = pos;
			return (UniChar*) buffer;
		}
		if (hexDigits >= 0)
		{
			long digit;
			if (IsDigit(c))
				digit = c - '0';
			else if (c >= 'a' && c <= 'f')
				digit = c - 'a' + 10;
			else if (c >= 'A' && c <= 'F')
				digit = c - 'A' + 10;
			else
			{
				free(buffer);
				Error(kNSErrBadHexDigit, RefVar(MAKECHAR(c)));
				return nil;
			}
			if (hexDigits < digitsPerChar)
			{
				hexValue = (hexValue << 4) | digit;
				hexDigits++;
			}
			if (hexDigits == digitsPerChar)
			{
				if (digitsPerChar == 4)
				{
					*(UniChar*) (buffer + pos) = (UniChar) hexValue;
					pos += 2;
				}
				else
					buffer[pos++] = (unsigned char) hexValue;
				hexValue = 0;
				hexDigits = 0;
			}
			continue;
		}
		*(UniChar*) (buffer + pos) = c;
		pos += 2;
	}
}


// ROM 0x00327314 GetNumber__9TCompilerFUs
// A number starting with digit c: 0x hex, an integer, or a real with a
// fraction and/or an exponent.
int
TCompiler::GetNumber(UniChar c)
{
	char buffer[256];
	long n = 0;
	if (c == '0')
	{
		UniChar next = fStream->GetChar();
		if (next == 'x' || next == 'X')
		{
			buffer[0] = '0';
			buffer[1] = (char) next;
			n = 2;
			UniChar d = fStream->GetChar();
			while (d != kEndOfStream)
			{
				dbprint(d);
				if (!(IsDigit(d) || (d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')))
					break;
				if (n < 256)
					buffer[n++] = (char) d;
				else
					Error(kNSErrNumberTooLong);
				d = fStream->GetChar();
			}
			fStream->UngetChar(d);
			dbprint("un");
			if (n < 256)
				buffer[n] = 0;
			else
				Error(kNSErrNumberTooLong);
			long value = strtol(buffer, nil, 16);
			if (value >= 0x40000000)
				Error(kNSErrIntegerTooLarge, RefVar(MakeString(buffer)));
			yylval = MAKEINT(value);
			return tokenINTEGER;
		}
		fStream->UngetChar(next);
		dbprint("un");
	}
	buffer[0] = (char) c;
	n = 1;
	UniChar d = fStream->GetChar();
	while (d != kEndOfStream && IsDigit(d))
	{
		dbprint(d);
		buffer[n++] = (char) d;
		d = fStream->GetChar();
	}
	Boolean isReal = false;
	if (d != kEndOfStream && d == '.')
	{
		isReal = true;
		dbprint(d);
		if (n < 256)
			buffer[n++] = '.';
		else
			Error(kNSErrNumberTooLong);
		d = fStream->GetChar();
		while (d != kEndOfStream && IsDigit(d))
		{
			dbprint(d);
			if (n < 256)
				buffer[n++] = (char) d;
			else
				Error(kNSErrNumberTooLong);
			d = fStream->GetChar();
		}
		if (d != kEndOfStream && (d == 'e' || d == 'E'))
		{
			dbprint(d);
			if (n < 256)
				buffer[n++] = (char) d;
			else
				Error(kNSErrNumberTooLong);
			d = fStream->GetChar();
			if (d == '+' || d == '-')
			{
				dbprint(d);
				if (n < 256)
					buffer[n++] = (char) d;
				else
					Error(kNSErrNumberTooLong);
				d = fStream->GetChar();
			}
			while (d != kEndOfStream && IsDigit(d))
			{
				dbprint(d);
				if (n < 256)
					buffer[n++] = (char) d;
				else
					Error(kNSErrNumberTooLong);
				d = fStream->GetChar();
			}
		}
	}
	dbprint(d);
	fStream->UngetChar(d);
	dbprint("un");
	if (n < 256)
		buffer[n] = 0;
	else
		Error(kNSErrNumberTooLong);
	if (isReal)
	{
		double value = strtod(buffer, nil);
		if (value == HUGE_VAL || value == -HUGE_VAL)
			Error(kNSErrRealTooLarge, RefVar(MakeString(buffer)));
		yylval = MakeReal(value);
		return tokenREAL;
	}
	long value = strtol(buffer, nil, 10);
	if (value >= 0x40000000)
		Error(kNSErrIntegerTooLarge, RefVar(MakeString(buffer)));
	yylval = MAKEINT(value);
	return tokenINTEGER;
}


// ROM 0x0032699c yylex0__9TCompilerFv
int
TCompiler::yylex0(void)
{
	for (;;)
	{
		UniChar c = fStream->GetChar();
		dbprint(c);
		if (c == '/')
		{
			// a comment, or division
			UniChar next = fStream->GetChar();
			dbprint(next);
			if (next == '/')
			{
				dbprint("//");
				do
				{
					c = fStream->GetChar();
				} while (!IsBreaker(c) && c != kEndOfStream);
				continue;
			}
			if (next == '*')
			{
				dbprint("/*");
				for (;;)
				{
					c = fStream->GetChar();
					if (c == kEndOfStream)
						break;
					if (c == '*')
					{
						do
							c = fStream->GetChar();
						while (c == '*');
						if (c == '/' || c == kEndOfStream)
							break;
					}
				}
				continue;
			}
			fStream->UngetChar(next);
			dbprint("un");
			return '/';
		}
		if (c == kEndOfStream)
			return -1;
		if (IsWhiteSpace(c))
			continue;

		if (c == '|')
		{
			// |a symbol|
			long length;
			UniChar* text = GetCharsUntil('|', false, length);
			if (text == nil)
				return -1;
			char name[256];
			ConvertFromUnicode(text, name, gNewtonScriptCompilerCharacterEncoding, 0x100);
			yylval = Intern(name);
			free(text);
			return tokenSYMBOL;
		}

		if (IsAlphabet(c) || c == '_')
		{
			// an identifier: a constant, a reserved word or a symbol
			char name[256];
			char* p = name;
			char* end = name + sizeof(name) - 2;
			for (;;)
			{
				if (p >= end)
					ThrowMsg((char*) "Symbol too big");
				*p++ = A_CONST_CHAR(c);
				c = fStream->GetChar();
				dbprint(c);
				if (!IsAlphaNumeric(c) && c != '_')
					break;
			}
			fStream->UngetChar(c);
			dbprint("un");
			*p = 0;
			if (symcmp(name, (char*) "NIL") == 0)
			{
				yylval = NILREF;
				return tokenCONST;
			}
			if (symcmp(name, (char*) "TRUE") == 0)
			{
				yylval = TRUEREF;
				return tokenCONST;
			}
			int token = ReservedWordToken(name);
			if (token != 0)
				return token;
			yylval = Intern(name);
			return tokenSYMBOL;
		}

		if (IsDigit(c))
			return GetNumber(c);

		if (c == '#')
		{
			// #line n "file"
			c = fStream->GetChar();
			dbprint(c);
			if (c != 'l')
			{
				Error(kNSErrBadDirective);
				return 0;
			}
			static const char* rest = "ine ";
			for (const char* q = rest; *q != 0; q++)
			{
				c = fStream->GetChar();
				dbprint(c);
				if (c != (UniChar) *q)
					Error(kNSErrBadLineDirective);
			}
			long line = 0;
			c = fStream->GetChar();
			dbprint(c);
			if (!IsDigit(c))
				Error(kNSErrBadLineNumber);
			while (IsDigit(c))
			{
				line = line * 10 + (c - '0');
				c = fStream->GetChar();
				dbprint(c);
			}
			if (c != ' ')
				Error(kNSErrBadLineFilename);
			c = fStream->GetChar();
			dbprint(c);
			if (c != '"')
				Error(kNSErrBadLineFilename);
			char filename[256];
			long n = 0;
			c = fStream->GetChar();
			dbprint(c);
			while (c != '"')
			{
				if (c == kEndOfStream || n == 255)
					Error(kNSErrBadLineFilename);
				filename[n++] = (char) c;
				c = fStream->GetChar();
				dbprint(c);
			}
			if (n == 0)
				Error(kNSErrBadLineFilename);
			filename[n] = 0;
			fStream->fLineNumber = line - 1;
			fStream->SetFilename(filename);
			return yylex0();
		}

		if (c == '@')
		{
			// @n, a magic pointer
			long index = 0;
			c = fStream->GetChar();
			dbprint(c);
			if (!IsDigit(c))
				Error(kNSErrBadMagicPointerRef);
			do
			{
				index = index * 10 + (c - '0');
				c = fStream->GetChar();
				dbprint(c);
			} while (c != kEndOfStream && IsDigit(c));
			fStream->UngetChar(c);
			dbprint("un");
			yylval = MAKEMAGICPTR(index);
			return tokenREFCONST;
		}

		if (c == '"')
		{
			long length;
			UniChar* text = GetCharsUntil('"', true, length);
			if (text == nil)
				return -1;
			yylval = AllocateBinary(RSSYMstring, length);
			BlockMove(text, BinaryData(yylval), length);
			free(text);
			return tokenCONST;
		}

		if (c == '$')
		{
			// $c, $\n, $\t, $\\, $\xx, $\uxxxx
			c = fStream->GetChar();
			dbprint(c);
			if (c != '\\')
			{
				yylval = MAKECHAR(c);
				return tokenCONST;
			}
			c = fStream->GetChar();
			dbprint(c);
			if (c == '\\')
				yylval = MAKECHAR('\\');
			else if (c == 'n' || c == 'N')
				yylval = MAKECHAR('\r');
			else if (c == 't' || c == 'T')
				yylval = MAKECHAR('\t');
			else
			{
				long digits = 2;
				if (c == 'u' || c == 'U')
				{
					digits = 4;
					c = fStream->GetChar();
					dbprint(c);
				}
				long value = 0;
				for (;;)
				{
					if (!IsHexDigit(c))
						Error(digits == 2 ? kNSErrBadCharEscape : kNSErrBadUnicodeEscape);
					long digit = IsDigit(c) ? c - '0' : UToLower(c) - 'a' + 10;
					value = (value << 4) | digit;
					if (--digits == 0)
						break;
					c = fStream->GetChar();
					dbprint(c);
				}
				yylval = MAKECHAR(value);
			}
			return tokenCONST;
		}

		if (c == '<')
		{
			UniChar next = fStream->GetChar();
			if (next == '=')
				return tokenLEQ;
			if (next == '<')
				return tokenLSHIFT;
			if (next == '>')
				return tokenNEQ;
			fStream->UngetChar(next);
			return '<';
		}
		if (c == '>')
		{
			UniChar next = fStream->GetChar();
			if (next == '=')
				return tokenGEQ;
			if (next == '>')
				return tokenRSHIFT;
			fStream->UngetChar(next);
			return '>';
		}
		if (c == '=')
			return tokenEQL;
		if (c == ':')
		{
			UniChar next = fStream->GetChar();
			if (next == '=')
				return tokenASSIGN;
			if (next == '?')
				return tokenSENDIFDEFINED;
			fStream->UngetChar(next);
			return ':';
		}
		if (c == '&')
		{
			UniChar next = fStream->GetChar();
			if (next == '&')
				return tokenAMPERAMPER;
			fStream->UngetChar(next);
			return '&';
		}
		if (strchr("+-*/()'.[],;{}", A_CONST_CHAR(c)) != nil)
			return c;
		Error(kNSErrBadCharacter, RefVar(MAKECHAR(c)));
		return 0;
	}
}
