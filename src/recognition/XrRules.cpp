/*
	File:		XrRules.cpp

	Contains:	The cursive reader's post-processing rules: where the
				prototype data (the letter table's PDF part) keeps the
				rule for a letter, a variant and a letter next to it.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The rules are reached through three levels of header, each a bit set
	saying which of its children there are and a table of the offsets of
	the ones there are, in the order of their bits - a child's slot being
	the number of set bits before its own (`PDFReturnIndex`):

		the main header (the PDF's first section, 0x10 bytes into the
		table): +0x10 a bit per character code, +0x30 the characters'
		offsets
		a character's header: +4 a bit per variant, +8 the variants'
		offsets
		a variant's header: +4 a bit per character it may stand next to,
		+0x24 whether it has a rule of its own, +0x28 the connections'
		offsets - and its own rule after them
		a connection's header: +4 a bit per rule, +8 the rules' offsets

	Every offset is from the header it is in, and every word of the table
	is big-endian (the ROM's, read in place).  The bits are numbered from
	the top bit of the first byte (`pdfMaskArray`).

	Reconstructed from the MP2x00 US ROM (0x0032934c-0x00329660); each
	function cites its origin.
*/

#include "XrReader.h"
#include "toolbox/ByteOrder.h"


// ROM 0x00329540 PDFReturnNumberOfBits__FPUcs
// How many bits are set in the first `bytes` bytes.
long
PDFReturnNumberOfBits(const UByte* bits, short bytes)
{
	short n = 0;
	for (short i = 0; i < bytes; i++)
		for (short j = 0; j < 8; j++)
			if ((bits[i] & pdfMaskArray[j]) != 0)
				n++;
	return n;
}


// ROM 0x003295b0 PDFReturnIndex__FPUcs
// How many bits are set before bit `bit` - its slot in the offsets.
long
PDFReturnIndex(const UByte* bits, short bit)
{
	short byte = bit / 8;
	short within = bit % 8;
	short n = 0;
	short count = 8;
	for (short i = 0; i <= byte; i++)
	{
		if (i == byte)
			count = within;
		for (short j = 0; j < count; j++)
			if ((bits[i] & pdfMaskArray[j]) != 0)
				n++;
	}
	return n;
}


// ROM 0x00329440 PDFReturnBitNumber__FPUcs
// Bit `bit`'s slot, -1 when it is not set.
long
PDFReturnBitNumber(const UByte* bits, short bit)
{
	if ((bits[bit / 8] & pdfMaskArray[bit % 8]) != 0)
		return PDFReturnIndex(bits, bit);
	return -1;
}


// ROM 0x0032934c PDFGetCharAddress__FP15PDF_MAIN_HEADERs
// A character's header; nil for a character with no rules.
const UByte*
PDFGetCharAddress(const UByte* main, short c)
{
	long slot = PDFReturnBitNumber(main + 0x10, c);
	if (slot == -1)
		return nil;
	return main + GetBigEndianWord(main + 0x30 + slot * 4);
}


// ROM 0x00329498 PDFGetVarAddress__FP15PDF_CHAR_HEADERs
// A variant's header; nil for none.
const UByte*
PDFGetVarAddress(const UByte* ch, short var)
{
	long slot = PDFReturnBitNumber(ch + 4, var);
	if (slot == -1)
		return nil;
	return ch + GetBigEndianWord(ch + 8 + slot * 4);
}


// ROM 0x003294d0 PDFGetConnectionAddress__FP14PDF_VAR_HEADERs
// The rules for a variant next to a character; nil for none.
const UByte*
PDFGetConnectionAddress(const UByte* var, short c)
{
	long slot = PDFReturnBitNumber(var + 4, c);
	if (slot == -1)
		return nil;
	return var + GetBigEndianWord(var + 0x28 + slot * 4);
}


// ROM 0x00329508 PDFGetRuleAddress__FP21PDF_CONNECTION_HEADERs
// One of a connection's rules; nil for none.
const UByte*
PDFGetRuleAddress(const UByte* connection, short rule)
{
	long slot = PDFReturnBitNumber(connection + 4, rule);
	if (slot == -1)
		return nil;
	return connection + GetBigEndianWord(connection + 8 + slot * 4);
}


// ROM 0x00329384 PDFGetRule__FP15PDF_MAIN_HEADERsN32PP15PDF_RULE_HEADER
// The rule for a character's variant: with `connection` -1 the variant's
// own (after its connections' offsets - when it has one), otherwise rule
// `rule` of those for the character `connection` next to it.  ==> whether
// there is one (*found: it, or nil).
Boolean
PDFGetRule(const UByte* main, short c, short var, short connection, short rule, const UByte** found)
{
	*found = nil;
	const UByte* ch = PDFGetCharAddress(main, c);
	if (ch == nil)
		return false;
	const UByte* v = PDFGetVarAddress(ch, var);
	if (v == nil)
		return false;
	if (connection == -1)
	{
		if (GetBigEndianWord(v + 0x24) == 0)
			return false;
		*found = v + (PDFReturnNumberOfBits(v + 4, 0x20) - 1) * 4 + 0x2c;
	}
	else
	{
		const UByte* conn = PDFGetConnectionAddress(v, connection);
		if (conn == nil)
			return false;
		*found = PDFGetRuleAddress(conn, rule);
	}
	return *found != nil;
}
