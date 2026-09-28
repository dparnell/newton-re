/*
	File:		recognition/ParaGraph.h

	Contains:	The data the machine's *cursive* recogniser reads writing
				against, and what it learns about the writer.

				The MP2x00 has two word recognisers.  One is Rosetta, the
				printed-writing recogniser (`Rosetta.h`), whose domain is
				'WREC'.  The other is ParaGraph's cursive recogniser -
				ParaGraph International's "xr" engine, licensed by Apple -
				which reads writing as a string of *xrs* (the pieces a
				stroke is cut into: arcs, hooks, loops) and matches them
				against a table of how each letter may be written.  Its two
				domains are 'STXR' (strokes to xrs, `XrDomains.h`) and
				'XRWR' (xrs to words), and the writer's handwriting style -
				the letter set, `userConfiguration.letterSetSelection` - is
				what decides which of the two engines is in use: set 2 is
				printed and goes to Rosetta, the others are cursive
				(`Recognizer.h`'s `SetUpRosetta`/`SetUpParaGraph`).

				This file is the engine's data, which lives in the ROM as
				five binaries in the `charsetInfoResources` frame:

				  DTEHeader    216 bytes: the "DTE" (the letter table's)
				               header, copied into a handle and used as a
				               struct - its words from +0x8c on are
				               filled in with pointers to the rest
				  DTEMain      the symbol descriptors: for each character
				               code a big-endian word offset to a
				               descriptor that says how many ways the
				               letter may be written (its *variants*), and
				               for each one its default weight (*vex*,
				               the low three bits of byte +0x14+v) and its
				               group and the letter sets it belongs to
				               (byte +0x24+v: bits 1-3 the group, 4-7 one
				               bit for each letter set's style)
				  PPDMain      the prototype data ("PDF"), which the
				               reading engine matches against
				  DTETrigrams  the trigram table
				  letterimages what the Letter Shapes preference draws
				               (`LetterShapes.h`)

				and of what the engine keeps in RAM about the writer: one
				*learning info* per letter set (0x924 bytes: a byte for
				each variant of each character code from 0x20 - its vex in
				the low three bits and a use counter in the high five -
				and 0x104 bytes of capitalisation weights), made from the
				descriptors' defaults when a letter set is first used and
				changed as the writer corrects the recogniser or picks
				letter shapes by hand.  The weights are what
				`GetLetterWeights`/`SetLetterWeights` hand a script, and
				what the machine keeps in the System soup under
				"LetterWeights2.0".

				The engine's memory comes from its own allocator
				(`HWRMemory*`): a handle whose first word holds the handle
				itself, so that the pointer the engine is given can be
				turned back into the handle.  DEVIATION: that word is a
				host pointer, `kHWRHeader` bytes rather than four; every
				piece of the engine that steps over it uses the constant.

	Reconstructed from the MP2x00 US ROM (the xr engine's data layer,
	0x00087280-0x000880a0, 0x000e63a8-0x000e6620, 0x002d4cbc-0x002d5034,
	0x002fd944-0x002fea8c); each function cites its origin.
*/

#ifndef __PARAGRAPH_H
#define __PARAGRAPH_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __OBJECTS_H
#include "objects.h"
#endif
#ifndef __NEWTONMEMORY_H
#include "NewtonMemory.h"
#endif

/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   m e m o r y
------------------------------------------------------------------------------*/

// The word in front of every block the engine allocates, holding its
// handle.  DEVIATION: a host pointer (the ROM's is four bytes).
enum { kHWRHeader = sizeof(Handle) };

// A handle of `size` bytes after the header, named 'para'; nil (and
// `gRecMemErrCount` counted) when there is no room, or when more than
// 0x493e0 bytes are asked for.
Handle	HWRMemoryAllocHandle(ULong size);					// ROM 0x000e63a8 HWRMemoryAllocHandle__FUl
// The handle locked, its own address written in front of the block.
// ==> the block after the header.
Ptr		HWRMemoryLockHandle(Handle h);						// ROM 0x000e640c HWRMemoryLockHandle__FUl
long	HWRMemoryUnlockHandle(Handle h);					// ROM 0x000e6434 HWRMemoryUnlockHandle__FUl
long	HWRMemoryFreeHandle(Handle h);						// ROM 0x000e644c HWRMemoryFreeHandle__FUl
// A locked block of `size` bytes: the handle allocated and locked for
// good.  ==> the block after the header, or nil.
long	HWRAbs(long x);										// ROM 0x000e64fc HWRAbs__Fi
Ptr		HWRMemoryAlloc(ULong size);							// ROM 0x000e6464 HWRMemoryAlloc__FUl
long	HWRMemoryFree(Ptr block);							// ROM 0x000e64e0 HWRMemoryFree__FPv

long	HWRStrCmp(const char* a, const char* b);			// ROM 0x000e661c HWRStrCmp__FPcT1 - the difference of the first bytes that differ (unsigned)
long	HWRStrLen(const char* s);							// ROM 0x000e6514 HWRStrLen__FPc
char*	HWRStrCpy(char* dest, const char* src);				// ROM 0x000e6560 HWRStrCpy__FPcT1
void	HWRStrCat(char* dest, const char* src);				// ROM 0x000e657c HWRStrCat__FPcT1
char*	HWRStrrChr(char* s, int c);							// ROM 0x000e65e8 HWRStrrChr__FPci - the last c in s; nil for none (and for an empty s)
char*	HWRStrChr(const char* s, int c);						// ROM 0x000e652c HWRStrChr__FPci - the first c in s; nil for none (and for c nought)
void	HWRStrRev(char* s);									// ROM 0x000e65a0 HWRStrRev__FPc

/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   c h a r a c t e r   c l a s s e s

	Over its own tables (`_xctype`, `_xupper`, `_xlower`: Mac Roman, the
	top half folded through the two 128-byte tables).
------------------------------------------------------------------------------*/

int		IsUpper(int c);										// ROM 0x00283e54 IsUpper
int		IsLower(int c);										// ROM 0x00283e84 IsLower
int		IsAlpha(int c);										// ROM 0x00283f20 IsAlpha
int		ToUpper(int c);										// ROM 0x00283f58 ToUpper
int		ToLower(int c);										// ROM 0x00283fb0 ToLower
int		IsPunct(int c);										// ROM 0x00283eb4 IsPunct

// Which of the two tables of (Mac Roman code, the engine's own code)
// pairs characters above 0x7e are translated through, and which letters
// count as letters: 0 English, 1 Swedish, 2 Swedish without diacriticals
// (the eight-bit letters left out).
void	SetOsRecTableAndCharSet(int which);					// ROM 0x00087c84 SetOsRecTableAndCharSet__Fi
// A character's code in the engine: below 0x7f the same, above it
// looked up in the current table; 0 when it has none.
UByte	OSToRec(int c);										// ROM 0x00087d7c OSToRec__Fi

extern const UByte*	p_os_rec_ctbl;							// ROM 0x0c100bcc p_os_rec_ctbl
extern const char*	alpha_charset;							// ROM 0x0c100bd0 alpha_charset

/*------------------------------------------------------------------------------
	T h e   l e t t e r   t a b l e   ( D T E )
------------------------------------------------------------------------------*/

// The DTE header: the DTEHeader resource copied into an engine handle,
// its bytes after the handle word, and the pointers the engine keeps in
// it.  DEVIATION: the pointer words are host pointers; in the ROM they
// are the resource's own words from +0x90, which the resource ships
// holding the build machine's stale addresses (the host starts them at
// nought).
struct DTIHeader
{
	UByte		fHeader[0x8c];		// +0x00  the resource's bytes 4-0x8f: its file name, date and version ("DTI4EngM2.00")
	void*		fRAMDTE;			// +0x8c  (never set)
	UByte*		fDTEMain;			// +0x90  the symbol descriptors: DTEMain's data after its first word
	ULong		fRAMDTEMain;		// +0x94  a replacement for them in RAM (SetRamParaData): 0, 1, or a handle
	UByte*		fRAMDTEMainPtr;		// +0x98  ... locked
	ULong		f9C;				// +0x9c
	Handle		fLearnInfo;			// +0xa0  the letter set's learning info (AllocLearnInfo)
	UByte*		fLearnInfoPtr;		// +0xa4  ... locked (dti_lock)
	void*		fA8;				// +0xa8  (never set)
	void*		fAC;				// +0xac  (never set)
	ULong		fB0;				// +0xb0
	Handle		fPDF;				// +0xb4  the prototype data's header (CreatePDFHeader)
	void*		fPDFPtr;			// +0xb8  ... locked
	ULong		fRAMPDF;			// +0xbc  a replacement prototype table in RAM: 0, 2, or a handle
	ULong		fC0;				// +0xc0
	void*		fC4;				// +0xc4  (never set)
	void*		fC8;				// +0xc8  (never set)
	ULong		fCC;				// +0xcc
	ULong		fD0;				// +0xd0
};

// The prototype data's header: the first 16 bytes of the table with
// its three sections' addresses written over the first three words.
// DEVIATION: host pointers; the fourth word is kept as it was (big-
// endian) because the engine reads its top half.
struct PDFHeader
{
	UByte*		fSection0;			// +0x00  the table + 0x10
	UByte*		fSection1;			// +0x04  ... + the word at +0x1c
	UByte*		fSection2;			// +0x08  ... + twice the halfword at +0x0c
	UByte		fWord3[4];			// +0x0c
};

// The trigram table's header: 0x98 bytes, of which the engine's
// loader here fills two.  DEVIATION: host pointers.
struct TrigramHeader
{
	UByte		fHeader[0x8c];		// +0x00
	ULong		fRAMTrigram;		// +0x8c  a replacement in RAM: 0, 3, or a handle
	UByte*		fTrigram;			// +0x90  the table (DTETrigrams' data after its first word), or the locked replacement
	ULong		f94;				// +0x94
};

// The two chains of dictionaries the engine reads words against, in
// slots of fifteen (SetUpVocAdders).  DEVIATION: host pointers - the
// ROM's is two arrays of 15 words, 0x78 bytes.
struct VocAdders
{
	void*		fMain[15];			// +0x00
	void*		fAux[15];			// +0x3c
};

enum { kLearnInfoSize = 0x924 };	// GetDTELearnInfoSize: a byte per variant of 0x82 codes, and the capitals

// A symbol descriptor in DTEMain (big-endian ROM data, read a byte at a
// time).
typedef UByte dte_sym_header_type;

// The DTE header read out of the ROM's resources (`name` is only ever
// "avp.dte"): the header copied into an engine handle and its pointers
// set up; with `trigrams`, the trigram header made too.  ==> 0, or 1
// when there was no room.
long	ReadDteResource(const char* name, short which, Handle* dti, Handle* trigrams, ULong makeTrigrams);	// ROM 0x002fe68c ReadDteResource__FPcsPPvT3Ui
Boolean	SetUpDteAddres(DTIHeader** dti, void* ramDTE, ULong ramDTEMain, void* a8, Handle pdf, ULong ramPDF, void* c4,
					   UByte* dteMain, UByte* ramDTEMainPtr, void* ac, UByte* ppdMain, void* c8);	// ROM 0x002d4eec SetUpDteAddres__FPPvPPcN52PvN48 - ==> whether the prototype header could not be made
Handle	CreatePDFHeader(UByte* ppd);						// ROM 0x002d4d3c CreatePDFHeader__FPv
long	CreateTrigramHeader(Handle* header, ULong ramTrigram, UByte* trigrams);	// ROM 0x002d4cbc CreateTrigramHeader__FPPvPPcPv - ==> 1 when made
PDFHeader*	LockRAMPDF(ULong ramPDF);						// ROM 0x002d4ddc LockRAMPDF__FPUc
long	UnlockRAMPDF(ULong ramPDF, PDFHeader* header);		// ROM 0x002d4e74 UnlockRAMPDF__FPUcT1
long	ARM_MAC_PDFUnloadFile(Handle* pdf, void** locked);	// ROM 0x002d4e9c ARM_MAC_PDFUnloadFile__FPUlPPUc

// The DTE header's pointers locked (the RAM replacements, the prototype
// header and the learning info) and let go again.  ==> 0; 1 for nil.
long	dti_lock(DTIHeader* dti);							// ROM 0x00087900 dti_lock__FPv
long	dti_unlock(DTIHeader* dti);							// ROM 0x00087cdc dti_unlock__FPv
long	dti_unload(Handle* dti);							// ROM 0x0008788c dti_unload__FPPv
long	triads_unlock(TrigramHeader* header);				// ROM 0x0021c16c triads_unlock__FPv
long	triads_unload(Handle* header);						// ROM 0x0021c0cc triads_unload__FPPv
long	voc_unload(Handle* vocs);							// ROM 0x00169058 voc_unload__FPPv
long	UnloadVoc(Handle* vocs);							// ROM 0x002ba334 UnloadVoc__FPPv
long	UnloadData(Handle* dti);							// ROM 0x002ba338 UnloadData__FPPv
long	UnloadTrigram(Handle* header);						// ROM 0x002ba33c UnloadTrigram__FPPv

// The two dictionary chains put in slot `index` of the vocabulary block,
// which is made (and cleared) the first time.  ==> 1; 0 without room.
long	SetUpVocAdders(void* main, void* aux, Handle* vocs, const char* name, short index);	// ROM 0x00168f48 SetUpVocAdders__FPP15AirusAParmBlockT1PPvPcs
long	ReadVocResource(const char* name, Handle* vocs);	// ROM 0x002fe894 ReadVocResource__FPUcPPv
long	LoadVocAndData(const char* voc, const char* voc2, const char* dte, Handle* vocs, Handle* dti,
					   Handle* trigrams, void** main, void** aux);	// ROM 0x002ba340 LoadVocAndData__FPcN21PPvN24PPPcT7

Ptr		GetLearnInfoPtr(DTIHeader* dti);					// ROM 0x002d4d2c GetLearnInfoPtr__FPv - the locked learning info, or nil
Handle	GetDTELearnInfoHandle(Handle dti);					// ROM 0x002d4fc0 GetDTELearnInfoHandle__FPv
long	GetDTELearnInfoSize(void* dti);						// ROM 0x002d5004 GetDTELearnInfoSize__FPv - kLearnInfoSize, 0 for nil
Boolean	SetLearnInfoAddress(DTIHeader* dti, Handle info);	// ROM 0x002d5018 SetLearnInfoAddress__FPvUl

// The letter set's learning info given to the DTE header: made from the
// descriptors' defaults the first time a set is asked for, the same one
// from then on.  Set 4 has none.  ==> 0, or 1.
long	AllocLearnInfo(Handle* dti, ULong letterSet);		// ROM 0x002fe90c AllocLearnInfo__FPPvUl
Handle	AllocOrtographLearnInfo(void);						// ROM 0x002fea10 AllocOrtographLearnInfo__Fv - the orthographic learning's database, made once
ULong	ORGetDBSize(void);									// ROM 0x00148064 ORGetDBSize__Fv
void	ORInitDB(void* db, ULong size);						// ROM 0x0014806c ORInitDB__FPvUl

// The learning info put back to what the descriptors say.  ==> 0, or 1
// (-1 from SetDefaultsWeights) when there is none locked.
long	SetDefaultsWeights(DTIHeader* dti);					// ROM 0x000873d8 SetDefaultsWeights__FPv
long	SetDefCaps(DTIHeader* dti);							// ROM 0x000879b0 SetDefCaps__FPv
long	SetDefVexes(DTIHeader* dti);						// ROM 0x000879e8 SetDefVexes__FPv

// A variant's descriptor: `variant` counted across the ROM's table and
// then the RAM one (up to 16 in all).  ==> the index into *descriptor, or
// -1.
long	GetSymDescriptor(UByte sym, UByte variant, dte_sym_header_type** descriptor, DTIHeader* dti);	// ROM 0x00087dc4 GetSymDescriptor__FUcT1PP19dte_sym_header_typePv
long	GetNumVarsOfChar(UByte c, DTIHeader* dti);			// ROM 0x00087eec GetNumVarsOfChar__FUcPv
long	GetVarGroup(UByte c, UByte variant, DTIHeader* dti);	// ROM 0x00087b90 GetVarGroup__FUcT1Pv
long	CheckVarActive(UByte c, UByte variant, UByte style, DTIHeader* dti);	// ROM 0x00087e84 CheckVarActive__FUcN21Pv
long	GetVarRewcapAllow(UByte c, UByte variant, DTIHeader* dti);	// ROM 0x00087aa4 GetVarRewcapAllow__FUcT1Pv - 1 the variant may be capitalised, 0 not, -1 no such variant
ULong	GetVarPosSize(UByte c, UByte variant, DTIHeader* dti);	// ROM 0x00087bf4 GetVarPosSize__FUcT1Pv - the descriptor's byte 1 << 16, the variant's position << 8 and its size; 0 for neither, 0xffffffff for no such variant
long	GetVarVex(UByte c, UByte variant, DTIHeader* dti);	// ROM 0x0008801c GetVarVex__FUcT1Pv
long	SetVarVex(UByte c, UByte variant, UByte vex, DTIHeader* dti);	// ROM 0x00088094 SetVarVex__FUcN21Pv
long	SetVarCounter(UByte c, UByte variant, UByte count, DTIHeader* dti);	// ROM 0x00087b00 SetVarCounter__FUcN21Pv
// The weight of a group of a letter's variants in the letter set of
// that style: the least vex of the group's variants the set uses (-1
// when it uses none) - and setting it: every one of them given the vex,
// and a counter of 0, 15 or 31 by how strong it is.
long	GetDteVariantState(UByte c, UByte group, UByte style, DTIHeader* dti);	// ROM 0x0008731c GetDteVariantState__FUcN21Pv
long	SetDteVariantState(UByte c, UByte group, int vex, UByte style, DTIHeader* dti);	// ROM 0x00087754 SetDteVariantState__FUcT1iT1Pv
long	GetVariantState(UByte c, UByte group, UByte style, DTIHeader* dti);	// ROM 0x00087744 GetVariantState__FUcN21Pv
long	SetVariantState(UByte c, UByte group, int vex, UByte style, DTIHeader* dti);	// ROM 0x00087714 SetVariantState__FUcT1iT1Pv

/*------------------------------------------------------------------------------
	T h e   l e t t e r   s e t s '   l e a r n i n g   i n f o s
------------------------------------------------------------------------------*/

// ROM 0x0c105474: the RAM replacements for the ROM's tables
// (SetRamParaData), and the five letter sets' learning infos and the
// orthographic database.
struct ParaRamData
{
	ULong		fDTEMain;			// 0x0c105474  0, 1 (in `RamParaGraphData`), or a handle
	ULong		fPPDMain;			// 0x0c105478  0, 2, or a handle
	ULong		fTrigrams;			// 0x0c10547c  0, 3, or a handle
	Handle		fLearnInfo[5];		// 0x0c105480
	Handle		fOrtho;				// 0x0c105494
};
extern ParaRamData	gParaRamData;							// ROM 0x0c105474 (unnamed)

void	PGFreeAllLearningData(void);						// ROM 0x002fdbfc PGFreeAllLearningData__Fv
// Whether the learning info is still the defaults.
Boolean	PGLetterSetInfoUnchanged(Handle info, ULong letterSet);	// ROM 0x002fdc54 PGLetterSetInfoUnchanged__FPPcUl
// The letter set's learning info when it is not the defaults; nil when
// there is none or it is.
Handle	PGGetLetterSetInfo(ULong letterSet);				// ROM 0x002fdd94 PGGetLetterSetInfo__FUl

// The learning info of one letter set mapped onto another's variants
// (the MessagePad 100/110/120's weights and this machine's are laid out
// differently): `how`'s low three bits pick the table (0 default, 1
// Palmer, 2 block), bit 16 the direction.  ==> 0, or -1.
long	ConvertLearningInfo(Handle from, Handle to, long how);	// ROM 0x00105ca0 ConvertLearningInfo__FPPcT1l

/*------------------------------------------------------------------------------
	T h e   R A M   r e p l a c e m e n t s
------------------------------------------------------------------------------*/

Ref		GetGlobalParaDataRef(RefArg slot);					// ROM 0x002fdb34 GetGlobalParaDataRef__FRC6RefVar - vars.|RamParaGraphData:PARA|.slot
Ref		SetGlobalParaDataRef(RefArg slot, RefArg value);	// ROM 0x002fd944 SetGlobalParaDataRef__FRC6RefVarT1
// One of the three tables' RAM replacement locked: 1, 2 and 3 are the
// DTEMain, PPDMain and trigram binaries kept in `RamParaGraphData`;
// anything else is an engine handle.  ==> its data, or nil.
Ptr		LockRamParaData(ULong which);						// ROM 0x002fe3b0 LockRamParaData__FUl
long	UnlockRamParaData(ULong which);						// ROM 0x002fe530 UnlockRamParaData__FUl

Ref		FSetRamParaData(RefArg rcvr, RefArg data, RefArg which);	// ROM 0x002fdde4 FSetRamParaData
Ref		FGetRamParaData(RefArg rcvr, RefArg which);			// ROM 0x002fe16c FGetRamParaData

/*------------------------------------------------------------------------------
	T h e   R O M ' s   r e s o u r c e s
------------------------------------------------------------------------------*/

Ref		FGetDTEHeader(RefArg rcvr);							// ROM 0x00168284 FGetDTEHeader__FRC6RefVar
Ref		FGetDTEMain(RefArg rcvr);							// ROM 0x001682a8 FGetDTEMain__FRC6RefVar
Ref		FGetPPDMain(RefArg rcvr);							// ROM 0x001682cc FGetPPDMain__FRC6RefVar
Ref		FGetDTETrigram(RefArg rcvr);						// ROM 0x001682f0 FGetDTETrigram__FRC6RefVar
Ref		FGetLetterImages(RefArg rcvr);						// ROM 0x0016831c FGetLetterImages__FRC6RefVar

void	RegisterParaGraphNatives(void);

#endif	/* __PARAGRAPH_H */
