/*
	File:		XrLex.cpp

	Contains:	The cursive reader's dictionaries: what may come next in
				a word (see XrReader.h).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The reader asks a chain of Airus dictionaries (`Dictionaries.h`'s
	TDictChain) which characters may follow the word so far: each
	dictionary of the chain the reading still allows is walked to where
	the word has got to (selector 2, Verify) and asked for what follows
	(selector 9, NextSet9), its walk callback setting a character's bit in
	a 256-bit set and writing, into that character's entry of the symbol
	buffer, the state the dictionary is in after it.  The set is then
	read out in order of character code, packing the buffer.
*/

#include "XrReader.h"
#include "Airus.h"
#include "Dictionaries.h"
#include "XrDomains.h"

#include <string.h>

// What the walk callback is handed (ROM: 0x24 bytes and a byte on the
// stack): the set of characters found, the symbol buffer, and which
// dictionary of the chain it is walking (one bit).
struct Fcn9Context
{
	uint32_t		set[8];		// +00
	fw_buf_type*	buf;		// +20
	UByte			dmask;		// +24
};


// ROM 0x00168994 Enum_fcn9CB__FUlN31
// One character that may follow: the node's top two bits say what the
// word is with it - 2 a prefix, 3 a word that goes on, 4 a word that
// ends there.  The first dictionary to offer a character fills its entry;
// a later one adds its bit, and upgrades an entry that only went on (2)
// to a word (3), or one that ended (4) to a word that also goes on.
void
Enum_fcn9CB(void* context, ULong sym, ULong node, ULong attr)
{
	Fcn9Context* c = (Fcn9Context*) context;
	fw_buf_type* e = &c->buf[sym & 0xff];
	UByte dmask = c->dmask;
	uint32_t n = (uint32_t) node;
	UByte status = (UByte) ((((n >> 30) & ~(n >> 31)) + 2) & 0xff);
	ULong word = (sym & 0xff) >> 5;
	uint32_t bit = 0x80000000u >> (sym & 0x1f);
	UByte a = (UByte) attr;
	n &= ~0xc0000000u;
	if ((c->set[word] & bit) == 0)
	{
		c->set[word] |= bit;
		e->status = status;
		e->attr = a;
		e->dmask = dmask;
		e->state = n;
		return;
	}
	if (status < 4 && e->status == 4)
	{
		e->dmask = dmask;
		e->state = n;
		e->status = 3;
		return;
	}
	e->dmask |= status >= 4 ? 0 : dmask;
	if (status < 3)
		return;
	if (e->status == 2)
	{
		e->status = 3;
		e->attr = a;
	}
}


// ROM 0x00168a54 Lex_fcn9CB__FUlN31
// A lexicon's node carries a string of characters: each of them may
// follow, with the node's state and no attribute.
void
Lex_fcn9CB(void* context, ULong syms, ULong node, ULong /*attr*/)
{
	const UByte* s = (const UByte*) (uintptr_t) syms;
	for (; *s != 0; s++)
		Enum_fcn9CB(context, *s, node, 0);
}


// ROM 0x00168aec GetWordAttributeAndID__FP13lex_data_typePiT2
// The word (lex->word) looked up in the chain its flags name - the
// vocabulary's (1, only the dictionaries of lex->vocKind) or the lexical
// database's (2) - for the id of the first dictionary that has it as a
// word and its attribute (nought for the lexical database's).  ==> 0, 1
// for no such word.
long
GetWordAttributeAndID(lex_data_type* lex, long* id, long* attr)
{
	TDictChain* chain;
	long kind = 0;
	Boolean vocabulary = false;
	// (the ROM leaves `kind` unset for the lexical database, where it is
	//  never read)
	ULong attribute = 0;
	if ((lex->flags & 1) != 0)
	{
		chain = lex->vocChain;
		kind = lex->vocKind;
		vocabulary = true;
	}
	else if ((lex->flags & 2) != 0)
		chain = lex->lexChain;
	else
		goto none;
	{
		char word[260];
		long last = HWRStrLen(lex->word);
		last = last < 1 ? 0 : last - 1;
		ULong i;
		Handle h = nil;
		AirusAParmBlock* pb = nil;
		for (i = 0; i < 0xf; i++)
		{
			h = chain->PositionToHandle(i & 0xffff);
			if (h == nil)
				goto none;
			pb = *(AirusAParmBlock**) h;
			if (vocabulary)
			{
				if (kind == 1)
				{
					if (pb->fDictID == 0x7a || pb->fDictID == 0x7b)
						continue;
				}
				else if (kind == 0x41)
				{
					if (pb->fDictID != 0x7a)
						continue;
				}
				else if (kind == 0x81 && pb->fDictID != 0x7b)
					continue;
			}
			pb->fWord = (UByte*) word;
			HWRStrCpy(word, lex->word);
			if (lex->reverse != 0)
				HWRStrRev(word);
			pb->fResult = 0;
			pb->fIndex = last;
			pb->fNode = 0;
			CallAirusANoLock(h, kAirusVerify);
			if (pb->fResult == 1 || pb->fResult == 2)
			{
				attribute = 0;
				if (AttributeLength(h) != 0)
					attribute = pb->fAttribute;
				break;
			}
		}
		if (h != nil && i < 0xf)
		{
			*attr = attribute;
			*id = pb->fDictID;
			if (lex->lexChain == chain)
				*attr = 0;
			return 0;
		}
	}
none:
	*attr = 0;
	*id = 0;
	return 1;
}


// ROM 0x00168c90 SortSymBuf__FiP11fw_buf_type
// (The buffer comes out of the walk sorted already.)
long
SortSymBuf(long /*count*/, fw_buf_type* /*buf*/)
{
	return 0;
}


// ROM 0x00168c98 AssignDictionaries__FiT1P13lex_data_typeP7rc_type
// The vocabulary chain of the block's `index`th vocabulary set, the main
// one or (which bit 0) the auxiliary.  ==> whether there is none.
Boolean
AssignDictionaries(long which, long index, lex_data_type* lex, rc_type* rc)
{
	VocAdders* vocs = (VocAdders*) rc->fVocs[index];
	// DEVIATION: a block with no vocabularies at all has no chain (the
	// ROM would read the words at nought; the word domain always gives it
	// its vocabularies)
	if (vocs == nil)
		lex->vocChain = nil;
	else
		lex->vocChain = (TDictChain*) ((which & 1) == 0 ? vocs->fMain[0] : vocs->fAux[0]);
	return lex->vocChain == nil;
}


// ROM 0x00168cc4 GF_VocOrLexSymbolSet__FP13lex_data_typePA256_11fw_buf_typeiPP15AirusAParmBlock
// What may follow the word so far in a chain, into `buf` (one entry per
// character, packed in character order, a nought symbol after the last):
// each dictionary the reading allows (its bit in the mask; all of them
// for an empty word, and every one past the eighth) walked to the word -
// the first from the node the reading had got to, the rest from the root
// - and asked for its next characters.  The lexical database's cost 4
// each, the vocabulary's nothing.  ==> how many.
long
GF_VocOrLexSymbolSet(lex_data_type* lex, fw_buf_type* buf, long lexical, TDictChain* chain)
{
	Fcn9Context context;
	memset(context.set, 0, sizeof(context.set));
	context.dmask = 0;
	long len;
	if (chain == nil || lex == nil || buf == nil || (len = lex->wlen - lex->f50) < 0)
		return 0;
	AirusNextSetProc proc = lexical == 0 ? Enum_fcn9CB : Lex_fcn9CB;
	context.buf = buf;
	long kind = lex->vocKind;
	ULong mask;
	uint32_t node = 0;
	if (len == 0)
		mask = 0xffffffff;
	else
	{
		mask = lexical == 0 ? lex->vocMask : lex->lexMask;
		if (mask != 0)
			node = lexical == 0 ? lex->vocState : lex->lexState;
	}
	lex->word[len] = 0;
	for (ULong i = 0; i < 0xf; i++)
	{
		ULong bit = 1 << i;
		if ((bit & mask) == 0 && i < 8)
			continue;
		Handle h = chain->PositionToHandle(i & 0xffff);
		if (h == nil)
			break;
		AirusAParmBlock* pb = *(AirusAParmBlock**) h;
		if (lexical == 0)
		{
			if (kind == 1)
			{
				if (pb->fDictID == 0x7a || pb->fDictID == 0x7b)
					continue;
			}
			else if (kind == 0x41)
			{
				if (pb->fDictID != 0x7a)
					continue;
			}
			else if (kind == 0x81 && pb->fDictID != 0x7b)
				continue;
		}
		pb->fResult = 0;
		pb->fIndex = 0;
		pb->fNode = node;
		if (0 < len && node == 0)
		{
			pb->fIndex = len - 1;
			pb->fWord = (UByte*) lex->word;
			CallAirusANoLock(h, kAirusVerify);
			node = 0;
			if (2 < (ULong) pb->fResult)
				continue;
		}
		context.dmask = (UByte) bit;
		pb->fWalkContext = &context;
		pb->fWalkProc = proc;
		CallAirusANoLock(h, kAirusNextSet9);
		node = 0;
	}
	UByte pen = lexical == 0 ? 0 : 4;
	long out = 0;
	for (ULong base = 0; base < 0x100; base += 0x20)
	{
		ULong c = base;
		for (uint32_t w = context.set[base >> 5]; w != 0; w <<= 1, c++)
		{
			if ((w & 0x80000000u) != 0)
			{
				buf[out] = buf[c];
				buf[out].sym = (UByte) c;
				buf[out].pen = pen;
				out++;
			}
		}
	}
	if (out < 0x100)
		buf[out].sym = 0;
	return out;
}


// ROM 0x00168f30 GF_VocSymbolSet__FP13lex_data_typePA256_11fw_buf_type
long
GF_VocSymbolSet(lex_data_type* lex, fw_buf_type* buf)
{
	return GF_VocOrLexSymbolSet(lex, buf, 0, lex->vocChain);
}


// ROM 0x00168f3c GF_LexDbSymbolSet__FP13lex_data_typePA256_11fw_buf_type
long
GF_LexDbSymbolSet(lex_data_type* lex, fw_buf_type* buf)
{
	return GF_VocOrLexSymbolSet(lex, buf, 1, lex->lexChain);
}
