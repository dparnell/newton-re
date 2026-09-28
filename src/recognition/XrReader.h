/*
	File:		XrReader.h

	Contains:	The cursive reader's xr reader: a word's xrs read into a
				graph of words (`xrw_algs`).

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	`xrw_algs` takes the xrs the low level made of a word and answers a
	*word graph* (`RWG_type`): what the writing may say, as a list of
	symbols with, for each, the xrs it was read from.  How it gets there
	depends on the field: a field that expects one fixed string
	(`rc +0xc0`) is only matched against that (`GetCMPAliases`); any other
	is read by `xrlv`, a Viterbi along the xrs.

	`xrlv` works on *locations* - the xrs a letter may start or end at
	(the breaks, and with rc +0x0e = 1 every one of them) - and keeps, for
	each location, the best few *partial readings* that end there
	(`xrlv_var_data_type`, 100 bytes: the letters so far, what each
	added, the variant and xrs each was read from, and where the
	dictionary walk that allows them has got to).  From each location
	(`XrlvDevelopPos`) every reading is offered the symbols that may come
	next - the vocabularies' next letters (`XrlvGetNextSymbols` over the
	Airus dictionaries: `GF_VocSymbolSet`), the lexical database's, or
	with no dictionary the field's character set weighed by the trigram
	table - and each symbol is matched against the xrs by the matrix
	(`CountSym`, remembered per symbol), every location the letter may end
	at getting a new reading when it beats that location's threshold
	(`XrlvDevelopCell`).  At each location the readings are then sorted
	and trimmed (`XrlvSortXrlvPos`, `XrlvTrimXrlvPos`), their letters
	checked against the base line - how tall and how low each is against
	the line and the letter before (`XrlvCHLXrlvPos`) - and the readings
	at the last location become the answers (`XrlvSortAns`,
	`XrlvCleanAns`), the word graph (`XrlvCreateRWG`) and the xrs of each
	of its letters (`XrlvGetRwgSymAliases`, a traced `CountWord` of the one
	letter over the xrs it was read from).

	DEVIATION: the ROM's `xrlv_data_type` is 0x3258 bytes, its
	`lex_data_type` 0xb0 and its `RWG_type` 0x14; they hold pointers, so on
	the host they are allocated and cleared by `sizeof`.  A position block
	(`XrlvPos`) is its header of five words and the readings after it, as
	in the ROM.  The fields are at the ROM's offsets in the comments.

	Reconstructed from the MP2x00 US ROM (0x0027ab60-0x0027e508,
	0x0036227c-0x003633fc, 0x00168aec-0x00168f48, 0x00087aa4-0x00087c84);
	each function cites its origin.
*/

#ifndef __XRREADER_H
#define __XRREADER_H

#ifndef __XRMATRIX_H
#include "XrMatrix.h"
#endif

struct _RECT;
struct rec_w_type;
class TDictChain;

/*------------------------------------------------------------------------------
	T h e   w o r d   g r a p h
------------------------------------------------------------------------------*/

// A symbol of the word graph (ROM 0x10 bytes, no pointers).
struct RWS_type
{
	UByte		sym;			// +00  the symbol
	UByte		realSym;		// +01  as the reader read it (another case, when the dictionary capitalised it)
	UByte		type;			// +02  1 a symbol, 2 the start of the alternatives, 3 their end, 4 between two alternatives
	UByte		var;			// +03  the variant it was read as (0xf: any)
	UByte		xrStart;		// +04  the first xr it was read from (counting from one)
	UByte		xrLen;			// +05  and how many
	UByte		weight;			// +06  the answer's score, a tenth (at most 100)
	UByte		f07;			// +07  the answer's penalty
	UByte		f08;
	UByte		letWeight;		// +09  what the letter added (FillLetterWeights)
	UByte		attr;			// +0a  the word's dictionary attribute (its id)
	UByte		f0b;
	UByte		f0c;
	UByte		f0d;
	UByte		src;			// +0e  1
	UByte		f0f;
};

// The xrs each symbol of the graph was read from (ROM 0x34 bytes): up to
// twelve of (the xr, how it was read: 1 the prototype skipped, 2 an xr
// read by a prototype, 3 the diagonal), a type of nought ending them.
struct RWG_PPD_type
{
	UByte		el[13][4];
};

// The word graph (ROM 0x14 bytes).
struct RWG_type
{
	UByte			type;		// +00  1 a list of answers, 2 one answer, 4 ...
	UByte			f01[3];
	long			size;		// +04  symbols
	RWS_type*		rws;		// +08
	RWG_PPD_type*	ppd;		// +0c
	ULong			f10;		// +10
};

/*------------------------------------------------------------------------------
	T h e   V i t e r b i
------------------------------------------------------------------------------*/

// A partial reading (ROM 100 bytes, no pointers): the letters so far and
// what it has cost.
struct xrlv_var_data_type
{
	UByte		sym;			// +00  the letter it ends with
	UByte		loc;			// +01  the location it ended at when it grew
	UByte		src;			// +02  the reading at that location it grew from
	UByte		cls;			// +03  the last letter's class: 1 other, 2 lower case, 3 upper, 4 a digit, 5 in a dictionary word, 6 math
	UByte		f04[2];
	UByte		chl[2];			// +06  big-endian: what the base-line check charged (XrlvCHLXrlvPos) - bits 0-3 the height against the letter before, 4-7 the size, 8-11 against the line's middle
	UByte		penGap;			// +08  what the xrs the letter left out cost
	UByte		penBrk;			// +09  what ending away from a break cost
	UByte		var;			// +0a  the last letter's variant
	UByte		count;			// +0b
	UByte		letValue;		// +0c  what the last letter read, less 100
	UByte		f0d;
	UByte		score[2];		// +0e  big-endian, signed: the reading's score
	UByte		kind;			// +10  what allows the letters: 1/0x41/0x81 the vocabulary, 2 the lexical database, 8/0xc the character set, 0x10 leading punctuation, 0x20 ending
	UByte		len;			// +11  letters
	UByte		wlen;			// +12  letters of the current word
	UByte		flags;			// +13  1 ..., 0x80 the last letter was capitalised for the dictionary
	UByte		status;			// +14  the dictionary's word so far: 1 none, 2 a prefix, 3 a word that goes on, 4 a word that ends
	UByte		attr;			// +15  its attribute
	UByte		dmask;			// +16  which dictionaries of the chain allow it
	UByte		f17;
	uint32_t	state;			// +18  where the dictionary walk has got to (a node, or the trigram state)
	char		word[0x18];		// +1c
	signed char	letScore[0x18];	// +34  what each letter added
	UByte		letInfo[0x18];	// +4c  each letter's variant (high nibble) and how many locations it spans (low)
};

// What may come next (ROM fw_buf_type, 12 bytes): a symbol and the state
// a dictionary walk reaches with it.
struct fw_buf_type
{
	UByte		sym;			// +00
	UByte		status;			// +01  low nibble the dictionary's status (2 prefix, 3 a word that goes on, 4 a leaf), 0x80 capitalised
	UByte		attr;			// +02  a word's attribute; in the character set the symbol's group
	UByte		dmask;			// +03  the dictionaries of the chain it came from
	uint32_t	state;			// +04
	UByte		pen;			// +08  what the symbol costs (the trigram table's)
	UByte		f09[3];
};

// The readings ending at one location (ROM: five words and the readings).
struct XrlvPos
{
	int32_t				best;		// +00  the best score here
	int32_t				threshold;	// +04  what a new reading must beat
	int32_t				slot;		// +08  where the next goes
	int32_t				total;		// +0c  readings offered
	int32_t				used;		// +10  readings kept
	xrlv_var_data_type	entries[1];	// +14
};

inline short	VarScore(const xrlv_var_data_type* v)				{ return (short) ((v->score[0] << 8) | v->score[1]); }
inline void		SetVarScore(xrlv_var_data_type* v, long score)	{ v->score[0] = (UByte) (score >> 8); v->score[1] = (UByte) score; }

// What a symbol read from the current location came to (ROM 0x24 bytes
// a symbol, kept as bytes: +1 the out line's first position, +2 the
// position after its last, +3 flags - 1 counted, 2 of no use here - then
// from +4 the out line's values from the first position and from +0x14
// the variant that won each.  ROM QUIRK: a line longer than sixteen
// positions runs its values into its variants and its variants into the
// next symbol's entry, as the ROM's does.)
enum { kXrlvCacheEntry = 0x24, kXrlvCacheSyms = 0x82 };

// Where the dictionary walk is (ROM 0xb0 bytes, the word after it).
struct lex_data_type
{
	long		reverse;		// +00  the word is read backwards
	UByte		f04[0x12];
	UByte		flags;			// +16  1 a vocabulary word, 2 a lexical-database word
	UByte		f17;
	UByte		vocStatus;		// +18
	UByte		f19;
	UByte		vocMask;		// +1a  which dictionaries of the chain
	UByte		f1b;
	uint32_t	vocState;		// +1c
	UByte		lexStatus;		// +20
	UByte		f21;
	UByte		lexMask;		// +22
	UByte		f23;
	uint32_t	lexState;		// +24
	UByte		f28[0x20];
	long		wlen;			// +48  letters of the word so far
	UByte		f4c[4];
	long		f50;			// +50
	TDictChain*	vocChain;		// +54
	UByte		f58[0x40];
	long		vocKind;		// +98  1, 0x41 (0x7a dictionaries only), 0x81 (0x7b)
	TDictChain*	lexChain;		// +9c
	UByte*		trigrams;		// +a0
	const char*	alpha;			// +a4
	const char*	lPunct;			// +a8
	const char*	ePunct;			// +ac
	char		word[0xc0];		// +b0
};

struct XrlvAnswer
{
	short		score;			// +00
	UByte		index;			// +02  the reading at the last location
	UByte		pen;			// +03
};

// The Viterbi's state (ROM 0x3258 bytes).
struct xrlv_data_type
{
	long			nLocs;		// +000  the locations
	long			width;		// +004  how far ahead of a location a letter may end (the position blocks kept at once)
	long			posSize;	// +008  a position block's bytes
	long			nEntries;	// +00c  readings a position keeps
	long			beam;		// +010  (rc +0x14)/4: how far below the best a reading may be
	long			flags;		// +014  (rc +0x08): 1 vocabulary, 2 character set, 4 trigrams, 8 lexical database
	long			charsets;	// +018  (rc +0x02): 1 letters, 2 digits, 4 math, 8 leading punctuation, 0x10 ending, 0x20 others
	long			caps;		// +01c  (rc +0x1e)
	long			f020;		// +020  ten per xr, less ten
	long			f024;		// +024  100
	long			nCharset;	// +028
	long			nLPunct;	// +02c
	long			nEPunct;	// +030
	uint32_t		lastState;	// +034  the last symbol set asked for, remembered
	long			lastCount;	// +038
	ULong			lastKind;	// +03c
	ULong			lastMask;	// +040
	xrcm_type*		xrcm;		// +044
	rc_type*		rc;			// +048
	xrdata_type*	xr;			// +04c
	short*			traceX;		// +050
	short*			traceY;		// +054
	XrlvPos*		pos[0x78];	// +058
	lex_data_type	lex;		// +238
	UByte			xrLoc[0x78];	// +3a8  each xr's location (0 for none)
	UByte			locXr[0x78];	// +420  each location's xr
	XrlvAnswer		ans[100];	// +498
	UByte			order[100];	// +628  the position's readings, best first
	fw_buf_type*	syms;		// +68c  the symbols XrlvGetNextSymbols answered
	fw_buf_type		symBuf[256];	// +690
	fw_buf_type		charset[256];	// +1290
	fw_buf_type		lPunct[16];		// +1e90
	fw_buf_type		ePunct[16];		// +1f50
	UByte			cache[kXrlvCacheSyms * kXrlvCacheEntry];	// +2010
	// DEVIATION: room for the last symbol's entry to run over (see the
	// cache), which in the ROM runs past the end of the block
	UByte			cacheSlack[0x100];
};

/*------------------------------------------------------------------------------
	F u n c t i o n s
------------------------------------------------------------------------------*/

// the graph (XrWordGraph.cpp)
long	xrw_algs(xrdata_type* xr, rec_w_type* readings, RWG_type* rwg, rc_type* rc);	// ROM 0x00362f08 xrw_algs__FP11xrdata_typePA10_10rec_w_typeP8RWG_typeP7rc_type - ==> 0, 1
long	create_rwg_ppd(xrcm_type* x, rc_type* rc, xrdata_type* xr, RWG_type* rwg);	// ROM 0x0036227c create_rwg_ppd__FP9xrcm_typeP7rc_typeP11xrdata_typeP8RWG_type - ==> 0, 1
long	create_rwg_ppd_node(xrcm_type* x, rc_type* rc, long offset, xrdata_type* xr, long from, long to, RWG_type* rwg);	// ROM 0x003625c8 create_rwg_ppd_node__FP9xrcm_typeP7rc_typeiP11xrdata_typeN23P8RWG_type - ==> 0, 1
long	FreeRWGMem(RWG_type* rwg);											// ROM 0x00362990 FreeRWGMem__FP8RWG_type - ==> 0
long	fill_RW_aliases(rec_w_type* readings, RWG_type* rwg);				// ROM 0x003629d8 fill_RW_aliases__FPA10_10rec_w_typeP8RWG_type - ==> 0, 1
long	GetCMPAliases(xrdata_type* xr, RWG_type* rwg, const char* word, rc_type* rc);	// ROM 0x00362bf8 GetCMPAliases__FP11xrdata_typeP8RWG_typePcP7rc_type - ==> 0, 1
long	SortGraph(int (*order)[10], RWG_type* rwg);							// ROM 0x00362d00 SortGraph__FPA10_iP8RWG_type - ==> 0, 1
long	GetBaseBord(rc_type* rc);											// ROM 0x00363038 GetBaseBord__FP7rc_type - the writing's slant from the stroka data's histogram
long	GetSymBox(UByte sym, long first, long end, xrdata_type* xr, _RECT* box);	// ROM 0x003630c8 GetSymBox__FUciT2P11xrdata_typeP5_RECT - ==> 0, 1 for no xrs

// the Viterbi (Xrlv.cpp)
long	xrlv(xrdata_type* xr, RWG_type* rwg, rc_type* rc);					// ROM 0x0027ab60 xrlv__FP11xrdata_typeP8RWG_typeP7rc_type - ==> 0, 1
long	XrlvDevelopPos(long loc, xrlv_data_type* d);						// ROM 0x0027af08 XrlvDevelopPos__FiP14xrlv_data_type - ==> 0
long	XrlvCleanAns(xrlv_data_type* d);									// ROM 0x0027b57c XrlvCleanAns__FP14xrlv_data_type - ==> 0
long	XrlvCreateRWG(RWG_type* rwg, xrlv_data_type* d);					// ROM 0x0027b638 XrlvCreateRWG__FP8RWG_typeP14xrlv_data_type - ==> 0, 1
long	XrlvSetLocations(xrlv_data_type* d, long everyXr);					// ROM 0x0027bad0 XrlvSetLocations__FP14xrlv_data_typei - ==> 0, 1 for fewer than two
long	XrlvGetCharset(xrlv_data_type* d);									// ROM 0x0027bbc8 XrlvGetCharset__FP14xrlv_data_type - ==> how many
long	XrlvGetRwgSymAliases(long i, RWG_type* rwg, xrlv_data_type* d);		// ROM 0x0027bf70 XrlvGetRwgSymAliases__FiP8RWG_typeP14xrlv_data_type - ==> 0
long	XrlvAlloc(xrlv_data_type** d, xrdata_type* xr, rc_type* rc);		// ROM 0x0027c0c8 XrlvAlloc__FPP14xrlv_data_typeP11xrdata_typeP7rc_type - ==> 0, non-zero for a failure
long	XrlvDealloc(xrlv_data_type** d);									// ROM 0x0027c39c XrlvDealloc__FPP14xrlv_data_type - ==> 0
long	XrlvFreeSomePos(xrlv_data_type* d);									// ROM 0x0027c420 XrlvFreeSomePos__FP14xrlv_data_type - ==> 0
long	XrlvCheckDictCap(xrlv_var_data_type* v, xrlv_data_type* d);			// ROM 0x0027c480 XrlvCheckDictCap__FP18xrlv_var_data_typeP14xrlv_data_type - ==> 0
long	XrlvApplyWordEndInfo(long loc, xrlv_var_data_type* v, xrlv_data_type* d);	// ROM 0x0027c5ec XrlvApplyWordEndInfo__FiP18xrlv_var_data_typeP14xrlv_data_type - ==> 0
long	XrlvDevelopCell(long loc, long capFirst, long extra, xrlv_var_data_type* v, xrlv_data_type* d);	// ROM 0x0027c6d8 XrlvDevelopCell__FiN21P18xrlv_var_data_typeP14xrlv_data_type - ==> 0
long	XrlvSortXrlvPos(long loc, xrlv_data_type* d);						// ROM 0x0027cce4 XrlvSortXrlvPos__FiP14xrlv_data_type - ==> 0
long	XrlvTrimXrlvPos(long loc, xrlv_data_type* d);						// ROM 0x0027cdbc XrlvTrimXrlvPos__FiP14xrlv_data_type - ==> the readings kept
void	XrlvGuessFutureGws(long loc, xrlv_data_type* d);					// ROM 0x0027cf48 XrlvGuessFutureGws__FiP14xrlv_data_type
long	XrlvCHLXrlvPos(long loc, xrlv_data_type* d);						// ROM 0x0027cfb8 XrlvCHLXrlvPos__FiP14xrlv_data_type - ==> 0, 1 for nothing there
long	XrlvGetNextSymbols(xrlv_var_data_type* v, long capFirst, xrlv_data_type* d);	// ROM 0x0027dd00 XrlvGetNextSymbols__FP18xrlv_var_data_typeiP14xrlv_data_type - ==> how many (d->syms)
long	XrlvGetSymAliases(UByte sym, long var, long first, long last, UByte* aliases, xrcm_type* x);	// ROM 0x0027e1e8 XrlvGetSymAliases__FUciN22PUcP9xrcm_type - ==> 0
long	XrlvSortAns(xrlv_data_type* d);										// ROM 0x0027e38c XrlvSortAns__FP14xrlv_data_type - ==> 0
long	SetupVocHandle(lex_data_type* lex, long kind);						// ROM 0x0027e4e8 SetupVocHandle__FP13lex_data_typei - ==> 0, 1 for a kind that is no vocabulary

// the dictionaries (XrLex.cpp)
void	Enum_fcn9CB(void* context, ULong sym, ULong node, ULong attr);		// ROM 0x00168994 Enum_fcn9CB__FUlN31
void	Lex_fcn9CB(void* context, ULong syms, ULong node, ULong attr);		// ROM 0x00168a54 Lex_fcn9CB__FUlN31
long	GetWordAttributeAndID(lex_data_type* lex, long* id, long* attr);	// ROM 0x00168aec GetWordAttributeAndID__FP13lex_data_typePiT2 - ==> 0, 1 for a word no dictionary has
long	SortSymBuf(long count, fw_buf_type* buf);							// ROM 0x00168c90 SortSymBuf__FiP11fw_buf_type - (nothing)
Boolean	AssignDictionaries(long which, long index, lex_data_type* lex, rc_type* rc);	// ROM 0x00168c98 AssignDictionaries__FiT1P13lex_data_typeP7rc_type - ==> whether there is none
long	GF_VocOrLexSymbolSet(lex_data_type* lex, fw_buf_type* buf, long lexical, TDictChain* chain);	// ROM 0x00168cc4 GF_VocOrLexSymbolSet__FP13lex_data_typePA256_11fw_buf_typeiPP15AirusAParmBlock - ==> how many
long	GF_VocSymbolSet(lex_data_type* lex, fw_buf_type* buf);				// ROM 0x00168f30 GF_VocSymbolSet__FP13lex_data_typePA256_11fw_buf_type
long	GF_LexDbSymbolSet(lex_data_type* lex, fw_buf_type* buf);			// ROM 0x00168f3c GF_LexDbSymbolSet__FP13lex_data_typePA256_11fw_buf_type

// tables (XrReaderTables.cpp, generated)
extern const unsigned char	triads_mapping[256];		// a character's number in the trigram table (0: none)
extern const unsigned char	DiacriticsLetter[52];		// the letters that carry a diacritical mark
extern const unsigned char	__ctype[256];				// the C library's character classes (0x20 a digit)
extern const unsigned char	kXrlvClassPenalty[49];		// (unnamed) what a letter of each class costs after one of each class, 7 by 7

#endif	/* __XRREADER_H */
