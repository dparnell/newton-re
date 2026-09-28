/*
	File:		XrMatrix.h

	Contains:	The cursive reader's xr matching matrix: a word's xrs
				matched against the letter table's prototypes.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	The low level hands the reader a word as a string of xrs
	(`LowLevel.h`); the letter table (the DTE, `ParaGraph.h`) says how
	each letter may be written, as up to sixteen *variants* each of which
	is a string of up to twelve *prototype* xrs (`xrp`, 0x4c bytes: how
	much each kind of xr, each height, shift, link and direction is worth
	at that point of the letter, as nibbles).  Matching a letter against
	the xrs is dynamic programming along the xr string: a *line* is a
	halfword per xr position, the best score of a path that has read so
	far and ends there, and a letter turns the line it is given (`inp`,
	where the letter may start) into the line it leaves (`out`, where it
	may end).  Each prototype of a variant is one column of the matrix -
	`CountXrAsm`, a hand-written assembly loop, takes the line before
	and writes the line after, each position the best of reading the xr
	against the prototype (the diagonal, 50 less plus what the
	prototype's tables give the xr), skipping the xr (its penalty) or
	skipping the prototype (the prototype's own penalty) - and the
	variants' lines are merged into the letter's, each variant charged
	twice its *vex* (how rarely the writer uses it).

	`xrcm_type` is the matrix: the lines, the xrs' first eight bytes
	(`xrinp`), which xrs are breaks (`brk`: a break's number, counting
	from one), and the state of the letter being counted.  `CountWord`
	counts a whole word letter by letter, each letter's best ending
	becoming the next one's start; with flag 4 it keeps a *trace* of
	which way each cell was reached (`TCountXrAsm`) and walks it back into
	a *layout* - which prototype of which variant read which xr - which
	is what the word graph's per-letter xr ranges are made of.

	The ROM keeps the lines as big-endian halfwords written a byte at a
	time and read with a word load's top half; on the host they are
	native shorts, which is the same value either way.

	DEVIATION: the matrix, the trace and the layout hold pointers, so on
	the host they are bigger than the ROM's 0x2cc, 0x9c, 0x8c, 0xbc,
	0x34 and 0x6c bytes; they are allocated and cleared by `sizeof` of
	the host's types, and a trace block is carved on eight-byte
	boundaries (the ROM's on four) so the pointers in it are aligned.
	The fields are at the ROM's offsets in the comments.

	Reconstructed from the MP2x00 US ROM (0x003606e4-0x00362f08,
	0x0038cd38-0x0038ce6c, 0x003ad244-0x003ad390); each function cites
	its origin.
*/

#ifndef __XRMATRIX_H
#define __XRMATRIX_H

#ifndef __PARAGRAPH_H
#include "ParaGraph.h"
#endif

struct rc_type;
struct xrdata_type;
struct RWG_type;
struct rec_w_type;

// An xr's first eight bytes, which is all the matrix reads of it (ROM:
// copied as two words).
struct xrinp_type
{
	UByte		type;			// +00
	UByte		attrib;			// +01  0x80 next to a break (a prototype with bit 7 of its byte 2 only reads such an xr)
	UByte		penalty;		// +02  what skipping it costs
	UByte		height;			// +03
	UByte		shift;			// +04
	UByte		orient;			// +05
	UByte		link;			// +06
	UByte		f07;
};

enum { kXrpSize = 0x4c };		// a prototype xr in the DTE (ROM data)
enum { kXrMaxVarLen = 12 };		// the prototypes a variant may have (a trace variant has room for twelve)
enum { kXrcmMaxXrs = 0x78 };	// the xrs a word may have

// The trace of one prototype: the letter, the prototype's first byte,
// where it started, how many positions, then a byte per position - how
// the cell was reached: 1 skipping the xr, 2 skipping the prototype, 3
// the diagonal (ROM: 4 + positions bytes, no pointers).
typedef UByte XrTraceXr;

// The trace of one variant (ROM 0x34 bytes).
struct XrTraceVar
{
	UByte		sym;			// +00
	UByte		var;			// +01
	UByte		len;			// +02
	UByte		f03;
	XrTraceXr*	xrs[kXrMaxVarLen];	// +04
};

// The trace of one letter in one case (ROM 0xbc bytes).
struct XrTraceSym
{
	UByte		sym;			// +00
	UByte		nVars;			// +01
	UByte		start;			// +02
	UByte		end;			// +03
	XrTraceVar*	vars[16];		// +04
	UByte		varOfPos[kXrcmMaxXrs];	// +44  the variant that won each position
};

// The trace of one letter of the word (ROM 0x8c bytes).
struct XrTraceLetter
{
	UByte		sym[2];			// +00  as written, and in the other case
	UByte		f02[2];
	long		start;			// +04
	long		end;			// +08
	XrTraceSym*	syms[2];		// +0c
	UByte		caseOfPos[kXrcmMaxXrs];	// +14  which of the two won each position
};

// The trace: blocks of 0xff0 bytes the pieces above are carved out of,
// the lines each letter left and the letters (ROM 0x9c bytes).
struct XrTrace
{
	UByte*			blocks[10];	// +00  (block 0 is the trace itself)
	UByte*			cur;		// +28  the block being carved
	short*			lines;		// +2c  the letters' out lines, one after another (at block 0 + 0xff0)
	long			nLines;		// +30  shorts written there
	long			nBlocks;	// +34  blocks after the first
	long			used;		// +38  bytes of the current block used
	XrTraceLetter*	letters[0x18];	// +3c
};

// One letter of a layout: the path read back through the trace (ROM
// 0x218 bytes, no pointers): the letter, its variant, how many steps,
// the xr positions it starts and ends at, and each step - the xr
// position, the prototype and how it was reached.
struct XrLayoutLetter
{
	UByte		sym;			// +00
	UByte		var;			// +01
	UByte		count;			// +02
	UByte		start;			// +03
	UByte		end;			// +04
	UByte		f05[3];
	UByte		steps[0x84][4];	// +08
};

// The layout of a word (ROM 0x6c bytes and the letters after it).
struct XrLayout
{
	void*			self;		// +00
	long			size;		// +04
	long			used;		// +08
	XrLayoutLetter*	letters[0x18];	// +0c
};

// The matrix (ROM 0x2cc bytes, the lines after it).
struct xrcm_type
{
	long			st;			// +000  the first position the current prototype reads
	long			end;		// +004  and the position after its last
	short*			inp;		// +008  the line it reads
	short*			out;		// +00c  the line it writes
	UByte*			xrp;		// +010  the prototype
	xrinp_type*		xrinp;		// +014  the xrs' first eight bytes
	XrTraceXr*		txr;		// +018  where the traced loop writes its directions
	long			dir;		// +01c  0 reading forwards, 1 backwards (the xrs reversed)
	long			varLen;		// +020  the prototypes in the variant
	long			inpSt;		// +024  the input line's first position
	long			inpEnd;		// +028  and the position after its last
	long			pos;		// +02c  where the letter starts (the last letter's best end)
	long			varSt;		// +030  where the variant's last prototype read
	long			varEnd;		// +034
	long			f038;		// +038
	UByte*			varXrps;	// +03c  the variant's prototypes
	XrTraceVar*		tvar;		// +040
	UByte			sym;		// +044  the letter being counted
	UByte			f045[3];
	ULong			flags;		// +048  1 the writer's vexes (the learn info), 2 leave out vex 7, 4 trace, 8 leave out the capitals the learn info bars, 0x10, 0x20 the lower case, 0x40 two cases
	UByte*			symDescr;	// +04c  the letter's DTE descriptor
	UByte			varRes[16][4];	// +050  each variant's first and after-last position
	XrTraceSym*		tsym;		// +090
	UByte*			vexes;		// +094  a byte per variant (the low three bits its vex)
	UShort			varMask;	// +098  the variants left out of this letter
	UByte			letter;		// +09a  the letter of the word being counted
	UByte			f09b;
	ULong			caseFlags;	// +09c  1|4 as written, 2|8 the upper case of a lower-case letter, 0x10 the lower of an upper
	long			fwd;		// +0a0  1 reading forwards
	long			merge;		// +0a4  merge the two cases' lines (forwards)
	XrTraceLetter*	tletter;	// +0a8
	char			word[0x18];	// +0ac
	long			wordEnd;	// +0c4  where the layout walks back from (<1: the end of the out line)
	short			varMasks[0x18];	// +0c8  for each letter of the word the variants left out
	short			weights[0x18];	// +0f8  what each letter added to the score (FillLetterWeights)
	XrTrace*		trace;		// +128
	long			caps;		// +12c  (rc +0x1e)
	long			rcFlags;	// +130  (rc +0x0a; 2: a position that is no break costs 4)
	long			style;		// +134  (rc +0x04: the letter set's style, a bit for each)
	long			lang;		// +138  (rc +0x06)
	long			wwc;		// +13c  (rc +0x1c: what a position is charged in the word's length, 40 less it)
	long			nXrs;		// +140
	long			size;		// +144  the allocation
	UByte			bestSym;	// +148  the case that read the letter best
	UByte			symDiff;	// +149  by how much (1..0xff)
	UByte			varOfPos[kXrcmMaxXrs];	// +14a  the variant that won each position of the out line
	UByte			f1c2[2];
	long			outSt;		// +1c4  the out line's first position
	long			outEnd;		// +1c8  and the one after its last
	long			bestVal;	// +1cc  the best position's value
	long			bestScore;	// +1d0  and its score: four times the value less the length charge
	long			bestPos;	// +1d4
	long			endVal;		// +1d8  the value at the last xr, when the line reaches it
	long			f1dc;		// +1dc  ten per xr, less ten
	long			wwcValue;	// +1e0
	XrLayout*		layout;		// +1e4
	short*			inpLine;	// +1e8
	short*			outLine;	// +1ec
	short*			varOut[16];	// +1f0
	short*			wwcLine;	// +230  the length charge at each position
	long			capsSize;	// +234
	UByte*			capsBuf;	// +238  what each letter read from each break, remembered (0x208 bytes a break)
	DTIHeader*		dti;		// +23c
	UByte*			learnInfo;	// +240
	UByte			brk[kXrcmMaxXrs];	// +244  a break's number, 0 for any other xr
	long			letterIdx;	// +2bc
	long			caseIdx;	// +2c0
	long			varIdx;		// +2c4
	UByte			f2c8[4];
};

long	xrmatr_alloc(rc_type* rc, xrdata_type* xr, xrcm_type** matrix);	// ROM 0x003606e4 xrmatr_alloc__FP7rc_typeP11xrdata_typePP9xrcm_type - ==> 0, 1 for no memory
void	xrmatr_dealloc(xrcm_type** matrix);								// ROM 0x00360908 xrmatr_dealloc__FPP9xrcm_type
long	SetInpLineByValue(long value, long first, long count, xrcm_type* x);	// ROM 0x00360938 SetInpLineByValue__FiN21P9xrcm_type - ==> 0
long	SetOutLine(short* line, long first, long count, xrcm_type* x);	// ROM 0x003609f4 SetOutLine__FPsiT2P9xrcm_type - ==> 0
long	MergeWithOutLine(short* line, long first, long count, xrcm_type* x);	// ROM 0x00360a34 MergeWithOutLine__FPsiT2P9xrcm_type - ==> 0
void	GetOutLine(short* line, long first, long count, xrcm_type* x);	// ROM 0x00360bdc GetOutLine__FPsiT2P9xrcm_type
long	TraceAlloc(long letters, xrcm_type* x);							// ROM 0x00360c50 TraceAlloc__FiP9xrcm_type - ==> 0, 1
long	TraceAddAlloc(xrcm_type* x);									// ROM 0x00360cc8 TraceAddAlloc__FP9xrcm_type - ==> 0, 1
long	TraceDealloc(xrcm_type* x);										// ROM 0x00360d24 TraceDealloc__FP9xrcm_type - ==> 0, 1
long	CreateLayout(xrcm_type* x);										// ROM 0x00360d78 CreateLayout__FP9xrcm_type - ==> 0, 1
long	change_direction(long dir, xrcm_type* x);						// ROM 0x00360fd8 change_direction__FiP9xrcm_type - ==> 0 (2 and more: the other way)
void	FreeLayout(xrcm_type* x);										// ROM 0x00361190 FreeLayout__FP9xrcm_type
void*	TDwordAdvance(long size, xrcm_type* x);							// ROM 0x003611c4 TDwordAdvance__FiP9xrcm_type - nil when there is no room
long	SetWWCLine(long wwc, xrcm_type* x);								// ROM 0x00361248 SetWWCLine__FiP9xrcm_type - ==> 0
long	FillLetterWeights(short* lines, xrcm_type* x);					// ROM 0x00361294 FillLetterWeights__FPsP9xrcm_type - ==> 0, 1
long	CountWord(const UByte* word, long caps, long flags, xrcm_type* x);	// ROM 0x0036132c CountWord__FPUciT2P9xrcm_type - ==> 0, 1
long	CountLetter(xrcm_type* x);										// ROM 0x00361610 CountLetter__FP9xrcm_type - ==> 0, 1
long	CountSym(xrcm_type* x);											// ROM 0x00361934 CountSym__FP9xrcm_type - ==> 0, 1 (no such letter, no memory)
long	CountVar(xrcm_type* x);											// ROM 0x00361d90 CountVar__FP9xrcm_type - ==> 0, 1
long	MergeVarResults(xrcm_type* x);									// ROM 0x00361ef8 MergeVarResults__FP9xrcm_type - ==> 0
void	SetInitialLine(long count, xrcm_type* x);						// ROM 0x003621e4 SetInitialLine__FiP9xrcm_type
void	SetInpLine(short* line, long first, long count, xrcm_type* x);	// ROM 0x00362214 SetInpLine__FPsiT2P9xrcm_type
void	CountXrAsm(xrcm_type* x);										// ROM 0x0038cd38 CountXrAsm
void	TCountXrAsm(xrcm_type* x);										// ROM 0x003ad244 TCountXrAsm

inline Boolean	XrcmIsBreak(UByte type)		{ return type == 1 || type == 3 || type == 4 || type == 2 || type == 5; }

#endif	/* __XRMATRIX_H */
