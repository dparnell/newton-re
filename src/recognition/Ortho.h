/*
	File:		Ortho.h

	Contains:	The cursive reader's orthographic learning: the letters of
				a word read, tied to the stretches of the pen's trace they
				were written with, and a database of letter shapes the
				writer's own letters are learnt into.

	Written by:	ParaGraph; reconstructed from the MP2x00 US ROM.

	Two halves.  When a field asks for it (rc +0xb2 bit 6, which the
	domain parameter 0x20037 sets along with rc +0xb8 bit 3) the reader
	records, for every word it reads, which xrs each letter of its word
	graph was read from and so which stretches of the trace made it
	(`ORCreateLearnInfo` over `OrtoCreate`/`OrtoEntries`): a
	*learn array* (`_LEARN_ARRAY_tag`) that goes into the word's training
	data as its 'ORTL' entry.  When the writer settles on a reading
	(`XRWDoLearning`) the array is walked for the run of letters that
	spells it (`ORTraining` -> `OrtoTraining`), and each letter's own
	points are copied out (`LearnPartsCopy`) and trained into the
	database (`TrainTrajectory`).

	The database (`ORGetDBSize` = 0x6000 bytes, `InitDataBase`) is a
	table of *classes* - a letter written with a given number of strokes -
	each with up to 32 *samples*.  A sample (`_NWTSAMPLE`, 20 bytes) is the
	letter's shape reduced to fourteen bytes: its trace normalised to its
	box (`TraceToOdata`), resampled at sixteen points spaced evenly along
	its length (`ResetParam`, `Repar`) and put through a sixteen-point DCT
	each way (`FDCT16`), the first seven coefficients after the constant
	of x and of y kept, normalised to unit length (`NormCdata`) and
	brought into a byte each (`FillNwtSample`).  Training looks the sample
	up (`SearchInDataBase`: every sample within a box of the new one,
	widened until something is found, then again within the square root
	of the best distance, gathered per letter into an answer list), and
	*Occam* decides whether it is worth keeping: it is added
	(`AddToDataBase`) when the nearest letter is another one, or when it
	is this one but only just nearer than the next.

	Every block that is kept - the learn array (in the training data)
	and the database (in the recogniser's RAM data) - keeps its words
	big-endian as the ROM lays them out, through `toolbox/ByteOrder.h`.

	DEVIATION: the learn array's +0x14 is a cached pointer to its parts
	(+0x04 bytes on); a host pointer does not fit in its four bytes, so
	the host leaves it nought and works the parts out from +0x04 every
	time - as the ROM itself does, recomputing both at the start of
	OrtoTraining.

	Reconstructed from the MP2x00 US ROM (0x00147548-0x001480c0,
	0x0012d178-0x0012e5e8, 0x001528b0-0x00152d10, 0x00079ff8-0x0007a72c);
	each function cites its origin.
*/

#ifndef __ORTHO_H
#define __ORTHO_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

struct xrdata_type;
struct RWG_type;
struct rec_w_type;
struct PS_point_type;
struct Part_of_letter;

/*------------------------------------------------------------------------------
	T h e   l e a r n   a r r a y

	(ROM _LEARN_ARRAY_tag; the offsets of its big-endian fields)
------------------------------------------------------------------------------*/

enum
{
	kOrtoSize		= 0x00,		// u32  the whole block
	kOrtoPartsOff	= 0x04,		// u32  where the parts start (0x18 + four bytes an entry)
	kOrtoMaxEntries	= 0x08,		// u16
	kOrtoMaxParts	= 0x0a,		// u16
	kOrtoGroups		= 0x0c,		// u16  how many runs of symbols the graph had
	kOrtoEntries	= 0x0e,		// u16
	kOrtoParts		= 0x10,		// u16
	kOrtoPartsPtr	= 0x14,		// the parts, cached (the host leaves it nought)
	kOrtoEntry		= 0x18		// the entries: first part, last part, the graph symbol, its character
};

long	OrtoCalcSize(short entries, short parts);					// ROM 0x00147548 OrtoCalcSize__FsT1
Ptr		OrtoGetmem(short entries, short parts);						// ROM 0x0014757c OrtoGetmem__FsT1
Ptr		OrtoResize(Ptr block);										// ROM 0x00147610 OrtoResize__FP16_LEARN_ARRAY_tag - the block trimmed to what it holds (the old one freed)
long	OrtoFasten(Ptr block, UByte symbol, UByte first, UByte last);	// ROM 0x001476d4 OrtoFasten__FP16_LEARN_ARRAY_tagUcN22 - ==> 0 when full
long	OrtoEntries(short flags, Ptr block, RWG_type* rwg, xrdata_type* xr);	// ROM 0x00147734 OrtoEntries__FsP16_LEARN_ARRAY_tagP8RWG_typeP11xrdata_type
long	LearnPartsCopy(const PS_point_type* trace, PS_point_type* out, const Part_of_letter* parts, short count);	// ROM 0x00147a50 LearnPartsCopy__FP13PS_point_typeT1P14Part_of_letters - ==> the points written
void	RemovePointAndSort(xrdata_type* xr, short from, short to, Part_of_letter* parts, short* count);	// ROM 0x00147b88 RemovePointAndSort__FP11xrdata_typesT2P14Part_of_letterPs
void	ORTraining(void* db, const PS_point_type* trace, const rec_w_type* word, void* ortl);	// ROM 0x00147d70 ORTraining__FPvP13PS_point_typeP10rec_w_typeT1
void	ORLArrayDelete(void** ortl);								// ROM 0x00147da4 ORLArrayDelete__FPPv
Ptr		OrtoCreate(short flags, RWG_type* rwg, xrdata_type* xr);	// ROM 0x00147dd4 OrtoCreate__FsP8RWG_typeP11xrdata_type
Ptr		OrtoDelete(Ptr block);										// ROM 0x00147e58 OrtoDelete__FP16_LEARN_ARRAY_tag - ==> nil
long	OrtoTraining(Ptr block, void* db, const char* word, const PS_point_type* trace);	// ROM 0x00147e74 OrtoTraining__FP16_LEARN_ARRAY_tagPvPcP13PS_point_type
ULong	OrtoSize(Ptr block);										// ROM 0x00148054 OrtoSize__FP16_LEARN_ARRAY_tag
void	ORCreateLearnInfo(xrdata_type* xr, RWG_type* rwg, void** ortl, ULong* size);	// ROM 0x00148078 ORCreateLearnInfo__FP11xrdata_typeP8RWG_typePPvPUl

/*------------------------------------------------------------------------------
	T h e   l e t t e r - s h a p e   d a t a b a s e
------------------------------------------------------------------------------*/

// A point of a letter's trace in the approximation's own units (ROM
// _ODATA, 0x18 bytes): where, the step from the point before, how long
// the step was and how far along the letter it is.
struct ODATA
{
	int32_t		x;
	int32_t		y;
	int32_t		dx;
	int32_t		dy;
	int32_t		len;
	int32_t		arc;
};

// One of the sixteen points the letter is resampled at (ROM _ARDATA,
// 0x18 bytes): the smoothed point, the resampled one, the step's length
// and how far along.
struct ARDATA
{
	int32_t		sx;
	int32_t		sy;
	int32_t		x;
	int32_t		y;
	int32_t		len;
	int32_t		arc;
};

// A sample (ROM _NWTSAMPLE, 0x14 bytes, big-endian as the database keeps
// it): the letter (+0), the strokes it was written with (+2; in the
// database, the index of its class), nought (+4), and the fourteen
// coefficient bytes (+6..+0x13, each 0x80 + an eighth).
enum { kNwtSampleSize = 0x14, kNwtCoefficients = 6, kNwtCoefficientCount = 14 };
typedef UByte NWTSAMPLE[kNwtSampleSize];

// What a search found (ROM _ALIST: a halfword room, a halfword count,
// then 16-byte answers), one answer a letter: how many of its samples
// were near, the nearest's distance, where its class and that sample are
// in the database.
struct ALISTEntry
{
	UShort		sym;			// +00
	UShort		count;			// +02
	ULong32		dist;			// +04  squared while searching, then its square root
	ULong32		classOffset;	// +08
	ULong32		sampleOffset;	// +0c
};

struct ALIST
{
	UShort		room;			// +00
	UShort		count;			// +02
	ALISTEntry	e[1];			// +04
};

// the database's header (big-endian): +0 its version (0x71), +6 how many
// classes, +8 its size, +0xc how much is used; then the classes (12
// bytes: the letter, its strokes, nought, how many samples, where they
// start) and then every class's samples, in the order of the classes
enum { kOrtoDBClasses = 0x06, kOrtoDBSize = 0x08, kOrtoDBUsed = 0x0c, kOrtoDBClassTable = 0x10, kOrtoDBClassSize = 0x0c };

long	TraceToOdata(ODATA* odata, const PS_point_type* points, long* strokes);	// ROM 0x0012d178 TraceToOdata__FP6_ODATAP6_POINTPi - ==> the points, 0 for a letter too small or too long
void	NormCdata(int32_t* coef);										// ROM 0x0012d400 NormCdata__FPl
long	TrainTrajectory(const PS_point_type* points, void* db, UShort sym);	// ROM 0x0012d460 TrainTrajectory__FP6_POINTPvUs
void	RjctAppr(long count, ODATA* odata, ARDATA* ardata, int32_t* coef, long iterations);	// ROM 0x0012d508 RjctAppr__FiP6_ODATAP7_ARDATAPlT1
long	FillNwtSample(const PS_point_type* points, UByte* sample);	// ROM 0x0012d728 FillNwtSample__FP6_POINTP10_NWTSAMPLE
ALIST*	CreateAlist(long room);										// ROM 0x0012d7d8 CreateAlist__Fi
void	ClearAlist(ALIST* list);									// ROM 0x0012d820 ClearAlist__FP6_ALIST
void	DestroyAlist(ALIST* list);									// ROM 0x0012d830 DestroyAlist__FP6_ALIST
long	Occam(UShort sym, ALIST* list);								// ROM 0x0012d834 Occam__FUsP6_ALIST - ==> 1 when the sample is worth adding
void	SwapMem(void* a, void* b, ULong count);						// ROM 0x0012d8a0 SwapMem__FPvT1Ui
void	InitDataBase(void* db, ULong size);							// ROM 0x0012d8cc InitDataBase__FPvUi
long	AddToDataBase(void* db, const UByte* sample, UShort sym);	// ROM 0x0012d918 AddToDataBase__FPvP10_NWTSAMPLEUs
void	SortAnswerList(ALIST* list);								// ROM 0x0012dbb4 SortAnswerList__FP6_ALIST
long	isSymInCharSet(UShort sym, const UByte* charset);			// ROM 0x0012dc24 isSymInCharSet__FUsPUc
long	FirstSearch(UByte** found, void* db, const UByte* sample, const UByte* charset, long window);	// ROM 0x0012dc64 FirstSearch__FPPvPvP10_NWTSAMPLEPUcl - ==> how many, 0xffff when no class is in the character set
long	SecondSearch(UByte** found, void* db, const UByte* sample, const UByte* charset, long window, long hole);	// ROM 0x0012deec SecondSearch__FPPvPvP10_NWTSAMPLEPUclT5
long	SearchInDataBase(ALIST* list, const UByte* sample, void* db, const UByte* charset);	// ROM 0x0012e2a4 SearchInDataBase__FP6_ALISTP10_NWTSAMPLEPvPUc
void	ResetParam(long count, ARDATA* ardata, long length);		// ROM 0x001528b0 ResetParam__FiP7_ARDATAl
void	Repar(long count, const ODATA* odata, long points, ARDATA* ardata);	// ROM 0x00152924 Repar__FiP6_ODATAT1P7_ARDATA
long	SQRT32_ORTO(ULong n);										// ROM 0x00152bd0 SQRT32_ORTO__FUl
void	Tracing(long count, ARDATA* ardata);						// ROM 0x00152d10 Tracing__FiP7_ARDATA

void	FDCT4(int32_t* a);												// ROM 0x00079ff8 FDCT4__FPl
void	IDCT4(int32_t* a);												// ROM 0x0007a0bc IDCT4__FPl
void	FDCT8(int32_t* a);												// ROM 0x0007a17c FDCT8__FPl
void	IDCT8(int32_t* a);												// ROM 0x0007a2b4 IDCT8__FPl
void	FDCT16(int32_t* a);											// ROM 0x0007a3e8 FDCT16__FPl
void	IDCT16(int32_t* a);											// ROM 0x0007a628 IDCT16__FPl

#endif	/* __ORTHO_H */
