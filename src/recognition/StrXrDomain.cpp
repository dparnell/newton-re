/*
	File:		recognition/StrXrDomain.cpp

	Contains:	The cursive recogniser's strokes-to-xrs domain and its
				unit: the writing grouped into words and each word read
				(XrDomains.h).

				A stroke offered to the domain goes, while the pen is
				still writing, onto the unit collecting the writing
				(PreGroup / Group -> GCPregroupAndGroup ->
				CallGroupAndClassify): listed in the unit's strokes and
				added as a sub.  Once the unit is classified (or a stroke
				is the last one written) the listed strokes are made into
				a trace (GCAllocRecTrace) and given to the word segmenter
				with the unit's word descriptors (GroupAndClassifyStrokes
				-> GCGroupStrokes), the words it settles read
				(GCClassifyStrokes -> GCTryToRecognize), and each word
				read made into a unit of its own (GCReleaseRecResults ->
				WriteRecResults): its strokes as subs, where it lies, its
				readings (GCWriteRW) - handed to the controller as a new
				piece, which the xrs-to-words domain turns into a word.
				A word the reader could not read (0x200: on the host, all
				of them) is a unit flagged invalid (0x400000), whose
				strokes the arbiter then gives up as ink.

	Reconstructed from the MP2x00 US ROM (0x000d39c8-0x000d5240,
	0x00065d48-0x00065dd4, 0x0021fd74-0x00220e10, 0x0024e650-0x0024e8a8);
	each function cites its origin.
*/

#include "XrDomains.h"
#include "WordDescriptors.h"
#include "CursiveReader.h"
#include "InkGroups.h"
#include "ParaGraph.h"
#include "Controller.h"
#include "Areas.h"
#include "Unit.h"
#include "Stroke.h"
#include "StrokeQueue.h"		// gTabScale
#include "Recognizer.h"			// gLetterSetSelection
#include "WordRecognizer.h"		// gUSE_GROUP_AND_CLASSIFY
#include "Dictionaries.h"
#include "FixedMath.h"
#include "NewtonMemory.h"
#include <string.h>
#include <ctype.h>

static inline Boolean	BitIsSet(const UByte* bits, ULong i)	{ return ((bits[(i & 0xff) >> 3] >> (7 - (i & 7))) & 1) != 0; }
static inline void		SetBit(UByte* bits, ULong i)			{ bits[(i & 0xff) >> 3] |= (UByte) (1 << (7 - (i & 7))); }
static inline void		ClearBit(UByte* bits, ULong i)			{ bits[(i & 0xff) >> 3] &= (UByte) ~(1 << (7 - (i & 7))); }
static inline long		RoundFixed16(Fixed f)					{ return (short) ((ULong) (f + 0x8000) >> 16); }

// whether a unit's stroke list lists anything
static inline Boolean
Listed(const UByte* strokes)
{
	return GCGetRealStrokeIndex((UByte*) strokes, 0xff) != 0 || (strokes[0x1f] & 1) != 0;
}


#pragma mark - the unit

// ROM 0x00220bcc Make__10TStrXrUnitSFP7TDomainUlP6TArray
TStrXrUnit*
TStrXrUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TStrXrUnit* unit = new TStrXrUnit;
	if (unit != nil && unit->IStrXrUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		unit = nil;
	}
	return unit;
}


// ROM 0x00220c44 IStrXrUnit__10TStrXrUnitFP7TDomainUlP6TArray
long
TStrXrUnit::IStrXrUnit(TDomain* domain, ULong kind, TArray* areas)
{
	long err = ISIUnit(domain, kStrXrDomainType, kind, areas, 0);
	fLeft = fRight = fBase = fBase2 = fHeight = fHeight2 = fField58 = fField5C = fField60 = 0;
	fLineHeight = 0;
	fBaseLine = 0;
	fNewLine = 0;
	fPrevBase[0] = fPrevBase[1] = fPrevBase[2] = fPrevBase[3] = 0;
	fWordCount = 0;
	fWords = nil;
	fLearning = nil;
	fDescriptors = nil;
	fGRes = nil;
	fNumStrokes = 0;
	fNext = 0;
	memset(fStrokes, 0, sizeof(fStrokes));
	fJoinX = 0;
	fJoinY = 0;
	fMerged = 0;
	fActive = 1;
	fRereading = 0;
	return err;
}


// ROM 0x00220d2c IDispose__10TStrXrUnitFv
// What it holds freed, then as a TSIUnit.
void
TStrXrUnit::IDispose(void)
{
	if (fLearning != nil)
		HWRMemoryFreeHandle(fLearning);
	fLearning = nil;
	if (fDescriptors != nil)
		HWRMemoryFreeHandle(fDescriptors);
	fDescriptors = nil;
	if (fWords != nil)
		HWRMemoryFree((Ptr) fWords);
	fWords = nil;
	fWordCount = 0;
	GCDisposeGResHandle(&fGRes);
	fNumStrokes = 0;
	fNext = 0;
	memset(fStrokes, 0, sizeof(fStrokes));
	fJoinX = 0;
	fJoinY = 0;
	fMerged = 0;
	TSIUnit::IDispose();
}


// ROM 0x00220dc0 Dump__10TStrXrUnitFP4TMsg
void
TStrXrUnit::Dump(TMsg* msg)
{
	TSIUnit::Dump(msg);
}


// ROM 0x00220e08 SizeInBytes__10TStrXrUnitFv
long
TStrXrUnit::SizeInBytes(void)
{
	long size = 0;
	if (fLearning != nil)
		size = GetHandleSize(fLearning);
	if (fDescriptors != nil)
		size += GetHandleSize(fDescriptors);
	if (fWords != nil)
		size += GetHandleSize(*(Handle*) ((Ptr) fWords - kHWRHeader));
	if (fGRes != nil)
		size += GetHandleSize(fGRes);
	return TSIUnit::SizeInBytes() + size;
}


#pragma mark - the domain

// ROM 0x00220210 Dispose__12TStrXrDomainFv
void
TStrXrDomain::Dispose(void)
{
}


// ROM 0x00220214 Classify__12TStrXrDomainFP5TUnit
void
TStrXrDomain::Classify(TUnit* unit)
{
	if (unit->TestFlags(0x8000000))
		return;
	CallGroupAndClassify(this, (TStrXrUnit*) unit, nil, 1, 0, (fControl & 0x10000) != 0);
}


// ROM 0x0022024c Reclassify__12TStrXrDomainFP5TUnit
void
TStrXrDomain::Reclassify(TUnit* unit)
{
	TStrXrUnit* xr = (TStrXrUnit*) unit;
	if (xr->fLearning != nil)
		HWRMemoryFreeHandle(xr->fLearning);
	xr->fLearning = nil;
	if (xr->fDescriptors != nil)
		HWRMemoryFreeHandle(xr->fDescriptors);
	xr->fDescriptors = nil;
	if (xr->fWords != nil)
		HWRMemoryFree((Ptr) xr->fWords);
	xr->fWords = nil;
	xr->fWordCount = 0;
	GCDisposeGResHandle(&xr->fGRes);
	xr->fNumStrokes = 0;
	xr->fNext = 0;
	memset(xr->fStrokes, 0, sizeof(xr->fStrokes));
	xr->fActive = 1;
	xr->fRereading = 1;
	CallGroupAndClassify(this, xr, nil, 1, 0, 1);
}


// ROM 0x0021fd74 ReclassifyStrXr__12TStrXrDomainFP10TStrXrUnit
void
TStrXrDomain::ReclassifyStrXr(TStrXrUnit* unit)
{
	CallGroupAndClassify(this, unit, nil, 1, 0, 1);
}


// ROM 0x00220ba0 ClassifyStrXr__12TStrXrDomainFP10TStrXrUnit
void
TStrXrDomain::ClassifyStrXr(TStrXrUnit* unit)
{
	CallGroupAndClassify(this, unit, nil, 1, 0, (fControl & 0x10000) != 0);
}


// ROM 0x002202e0 Group__12TStrXrDomainFP5TUnitP8dInfoRec
// A stroke grouped: into the box it was written in when the area has
// boxes (and on line as well when it continues the box the last stroke
// was in), otherwise on line.
long
TStrXrDomain::Group(TUnit* unit, dInfoRec* info)
{
	if ((fGrid[0] != 0 && fGrid[2] != 0) || (fGrid[1] != 0 && fGrid[3] != 0))
	{
		if (!GroupBoxedSegmentation(unit))
			return 1;
	}
	GroupOnLineSegmentation(unit);
	return 1;
}


// ROM 0x00220344 PreGroup__12TStrXrDomainFP5TUnit
// A stroke collected while the pen is still writing - unless the area
// reads only at the end.
long
TStrXrDomain::PreGroup(TUnit* unit)
{
	if (unit == nil)
		return 1;
	if ((fControl & 0x10000) == 0)
		return (UByte) GCPregroupAndGroup(this, (TStrokeUnit*) unit, 1);
	return 0;
}


// ROM 0x0021fe10 GroupOnLineSegmentation__12TStrXrDomainFP5TUnit
long
TStrXrDomain::GroupOnLineSegmentation(TUnit* stroke)
{
	return GCPregroupAndGroup(this, (TStrokeUnit*) stroke, 0);
}


// ROM 0x0021fe18 GroupBoxedSegmentation__12TStrXrDomainFP5TUnit
Boolean
TStrXrDomain::GroupBoxedSegmentation(TUnit* stroke)
{
	Boolean same = AddStrokeToBoxedWord((TStrokeUnit*) stroke);
	if (!same)
		StartWord((TStrokeUnit*) stroke);
	if (fParameters != nil)
		((STRXRPARAM*) *fParameters)->fBoxHit = fBoxHit;
	return same;
}


// ROM 0x0021fe60 AddStrokeToBoxedWord__12TStrXrDomainFP11TStrokeUnit
// The units still collecting strokes ended unless the stroke is in the
// box the last one was in.  ROM QUIRK: what is answered is whether it is
// for the last of those units - with none, false.
Boolean
TStrXrDomain::AddStrokeToBoxedWord(TStrokeUnit* stroke)
{
	Boolean same = false;
	ULong hit = BoxHit(stroke);
	TUnitList* units = fController->GetDelayList(this, fType);
	if (units == nil)
		fController->SignalMemoryError();
	else
	{
		for (ULong i = 0; i < (ULong) units->fCount; i++)
		{
			TSIUnit* unit = (TSIUnit*) units->GetUnit(i);
			same = hit == fBoxHit;
			if (!same)
				unit->EndSubs();
		}
		units->Dispose();
	}
	fBoxHit = hit;
	return same;
}


// ROM 0x0021ff30 BoxHit__12TStrXrDomainFP11TStrokeUnit
// Which box the middle of the stroke is in: the column (the low half)
// and the row (the high half), counted from the grid's origin; 0xffff
// for a direction the grid has no size in, nought for a middle before
// the origin.
ULong
TStrXrDomain::BoxHit(TStrokeUnit* stroke)
{
	ULong hit = 0xffffffff;
	FRect box;
	stroke->GetBBox(&box);
	ULong mid = (ULong) (RoundFixed16(box.right) + RoundFixed16(box.left)) >> 1;
	ULong from = (ULong) RoundFixed16(fGrid[0]);
	long size = RoundFixed16(fGrid[2]) - (long) from;
	if (size != 0)
	{
		if (from < mid)
			hit = (hit & 0xffff0000) | (((ULong) (mid - from) / (ULong) size) & 0xffff);
		else
			hit &= 0xffff0000;
	}
	mid = (ULong) (RoundFixed16(box.top) + RoundFixed16(box.bottom)) >> 1;
	from = (ULong) RoundFixed16(fGrid[1]);
	size = RoundFixed16(fGrid[3]) - (long) from;
	if (size != 0)
	{
		if (from < mid)
			hit = (hit & 0xffff) | ((((ULong) (mid - from) / (ULong) size) & 0xffff) << 16);
		else
			hit &= 0xffff;
	}
	return hit;
}


// ROM 0x0021fd94 StartWord__12TStrXrDomainFP11TStrokeUnit
void
TStrXrDomain::StartWord(TStrokeUnit* stroke)
{
	TAreaList* areas = stroke->GetAreas();
	TStrXrUnit* unit = TStrXrUnit::Make(this, stroke->fKind + 1, (TArray*) areas);
	if (areas != nil)
		areas->Dispose();
	if (unit == nil)
	{
		fController->SignalMemoryError();
		return;
	}
	unit->AddSub(stroke);
	fController->NewGroup(unit);
}


// ROM 0x00220a4c SetParameters__12TStrXrDomainFPPc
// The area's parameter block copied in, and the recognition tables for
// its language set up.
Boolean
TStrXrDomain::SetParameters(Handle params)
{
	STRXRPARAM* p = (STRXRPARAM*) *params;
	fFlags = p->fFlags;
	fFlags2 = p->fFlags2;
	fDTI = p->fDTI;
	fFieldType = p->fFieldType;
	fControl = p->fControl;
	for (long i = 0; i < 7; i++)
		fGeom[i] = p->fGeom[i];
	for (long i = 0; i < 4; i++)
		fGrid[i] = p->fGrid[i];
	fBoxHit = p->fBoxHit;
	fLetterStyle = p->fLetterStyle;
	fLanguage = p->fLanguage;
	fField6C = p->fField44;
	fField70 = p->fField48;
	fField74 = p->fField4C;
	for (long i = 0; i < 4; i++)
		fPrevBase[i] = p->fPrevBase[i];
	rc_type rc;
	memset(&rc, 0, sizeof(rc));		// (the ROM's is the stack's; only the language is read)
	RCSetH(&rc, 0x06, p->fLanguage);
	GCSetUpRecTableAndCharset(&rc, 0);
	return true;
}


// ROM 0x00220080 SetStrXrFieldType__FUlP10STRXRPARAM
// The kind of field (the recogniser's flags: 1 words, 2 numbers, 4
// upper-case, 8 punctuation, 0x10 phone, 0x20 letters, 0x40 cursive)
// turned into what the domain reads it as.
// A byte of the block at its ROM offset, changed in place.  The block's
// fields are the host's own (the handle is pointer-sized), so the byte is
// taken out of the field it falls in - numbered big-endian, as the ROM's
// memory is - and put back.
// DEVIATION: a byte of the letter table's handle (+0x04..+0x07) changes
// nothing on the host (as XRWByte's pointers).
static void
StrXrByteOperator(STRXRPARAM* p, ULong at, UByte op, UByte operand)
{
	// kind: 0 a 16-bit field, 1 a long, 2 a ULong (both 32 bits in the ROM)
	struct Field { ULong fAt; int fKind; void* fField; };
	const Field fields[] =
	{
		{ 0x00, 0, &p->fFlags }, { 0x02, 0, &p->fFlags2 },
		{ 0x08, 2, &p->fFieldType }, { 0x0c, 2, &p->fControl },
		{ 0x10, 1, &p->fGeom[0] }, { 0x14, 1, &p->fGeom[1] }, { 0x18, 1, &p->fGeom[2] },
		{ 0x1c, 1, &p->fGeom[3] }, { 0x20, 1, &p->fGeom[4] }, { 0x24, 1, &p->fGeom[5] },
		{ 0x28, 1, &p->fGeom[6] },
		{ 0x2c, 1, &p->fGrid[0] }, { 0x30, 1, &p->fGrid[1] }, { 0x34, 1, &p->fGrid[2] },
		{ 0x38, 1, &p->fGrid[3] }, { 0x3c, 2, &p->fBoxHit },
		{ 0x40, 0, &p->fLetterStyle }, { 0x42, 0, &p->fLanguage },
		{ 0x44, 0, &p->fField44 }, { 0x46, 0, &p->fField46 },
		{ 0x48, 2, &p->fField48 }, { 0x4c, 2, &p->fField4C },
		{ 0x50, 0, &p->fPrevBase[0] }, { 0x52, 0, &p->fPrevBase[1] },
		{ 0x54, 0, &p->fPrevBase[2] }, { 0x56, 0, &p->fPrevBase[3] },
	};
	for (const Field& f : fields)
	{
		ULong size = f.fKind == 0 ? 2 : 4;
		if (at < f.fAt || at >= f.fAt + size)
			continue;
		uint32_t value = f.fKind == 0 ? *(UShort*) f.fField : f.fKind == 1 ? (uint32_t) *(long*) f.fField : (uint32_t) *(ULong*) f.fField;
		UByte bytes[4];
		for (ULong i = 0; i < size; i++)
			bytes[i] = value >> (8 * (size - 1 - i));
		RCUCharOperator(op, &bytes[at - f.fAt], operand);
		value = 0;
		for (ULong i = 0; i < size; i++)
			value = (value << 8) | bytes[i];
		if (f.fKind == 0)
			*(UShort*) f.fField = value;
		else if (f.fKind == 1)
			*(long*) f.fField = (int32_t) value;
		else
			*(ULong*) f.fField = value;
		return;
	}
}


// A long field changed as a short: its low half taken as a signed short,
// the operator applied, and the answer put back sign-extended (the ROM
// stores the half big-endian on the stack, reads the word back and shifts
// it down arithmetically).
static void
StrXrLongAsShort(long* field, UByte op, short operand)
{
	UByte half[2] = { (UByte) (*field >> 8), (UByte) *field };
	RCShortOperator(op, half, operand);
	*field = (short) ((half[0] << 8) | half[1]);
}


// ROM 0x000651e4 SetStrXrRC__FUlP10STRXRPARAM
// One word of a recognition configuration's strxrCommands carried out: the
// operator (bits 25-29: set, or, and, xor, add, subtract, ...) applied to
// the field it names.  With bit 24 set and an offset under 0x58 in the low
// half, the byte at that offset is changed by the command's byte (bits
// 16-23); otherwise bits 16-23 name the field and the low half is the
// operand (signed, but as it stands for the unsigned fields).
void
SetStrXrRC(ULong command, STRXRPARAM* param)
{
	UByte op = (command >> 25) & 0x1f;
	UByte which = (command >> 16) & 0xff;
	ULong low = command & 0xffff;
	if (((command >> 24) & 1) == 1 && low < 0x58)
	{
		StrXrByteOperator(param, low, op, which);
		return;
	}
	short sOperand = (short) low;
	UByte half[2];
	switch (which)
	{
	case 0x02:
	case 0x03:
	case 0x43:
	case 0x44:
	case 0x45:
	case 0x46:
		{
			// ROM QUIRK: 0x45 and 0x46 both change +0x54 (fPrevBase[2]); +0x56
			// has no command of its own
			UShort* field = which == 0x02 ? &param->fLetterStyle : which == 0x03 ? &param->fLanguage
						: which == 0x43 ? (UShort*) &param->fPrevBase[0] : which == 0x44 ? (UShort*) &param->fPrevBase[1]
						: (UShort*) &param->fPrevBase[2];
			half[0] = *field >> 8;
			half[1] = *field;
			RCShortOperator(op, half, sOperand);
			*field = (half[0] << 8) | half[1];
		}
		break;
	case 0x17:
	case 0x18:
	case 0x2a:
		{
			UShort* field = which == 0x17 ? &param->fFlags : which == 0x18 ? &param->fFlags2 : &param->fField44;
			half[0] = *field >> 8;
			half[1] = *field;
			RCUShortOperator(op, half, low);
			*field = (half[0] << 8) | half[1];
		}
		break;
	case 0x1b:	StrXrLongAsShort(&param->fGeom[0], op, sOperand); break;
	case 0x1d:	StrXrLongAsShort(&param->fGeom[1], op, sOperand); break;
	case 0x1f:	StrXrLongAsShort(&param->fGeom[2], op, sOperand); break;
	case 0x20:	StrXrLongAsShort(&param->fGeom[3], op, sOperand); break;
	case 0x1c:	StrXrLongAsShort(&param->fGeom[4], op, sOperand); break;
	case 0x1e:	StrXrLongAsShort(&param->fGeom[5], op, sOperand); break;
	case 0x25:	StrXrLongAsShort(&param->fGeom[6], op, sOperand); break;
	case 0x40:
		// the writer's letter spacing, the low half of the control word
		half[0] = param->fControl >> 8;
		half[1] = param->fControl;
		RCShortOperator(op, half, sOperand);
		param->fControl = (param->fControl & 0xffff0000) | (UShort) ((half[0] << 8) | half[1]);
		break;
	case 0x41:
		{
			// read only at the end (no grouping while writing)
			UByte flag = (param->fControl & 0x10000) != 0;
			RCBooleanOperator(op, &flag, command & 0xff);
			param->fControl = (param->fControl & ~(ULong) 0x10000) | (flag != 0 ? 0x10000 : 0);
		}
		break;
	case 0x42:
		{
			// how many words to wait for, from bit 17
			UShort words = (param->fControl >> 17) & 0xffff;
			half[0] = words >> 8;
			half[1] = words;
			RCShortOperator(op, half, sOperand);
			param->fControl = (param->fControl & 0x1ffff) | ((((ULong) ((half[0] << 8) | half[1])) << 17) & 0xfffe0000);
		}
		break;
	default:
		break;
	}
}


long
SetStrXrFieldType(ULong type, STRXRPARAM* param)
{
	ULong words = type & 1, upper = type & 4, punct = type & 8, cursive = type & 0x40;
	ULong numbers = type & 2, letters = type & 0x20, phone = type & 0x10;
	if (words == 0 && upper == 0 && numbers == 0 && punct == 0 && phone == 0 && cursive == 0 && letters == 0)
		return -1;
	param->fFlags &= 0x0800;
	param->fFlags2 = 0;
	param->fFlags |= letters == 0 ? 2 : 1;
	if (type != 0x20)
	{
		param->fFlags |= (words == 0 && upper == 0) ? 0x20 : 0x30;
		if ((punct != 0 || phone != 0 || numbers != 0) && letters == 0)
			param->fFlags |= 0x8000;
		if (phone != 0)
			param->fFlags2 |= 2;
		if (type == 0x10)
			goto cursiveStyle;
	}
	param->fFlags2 |= 1;
cursiveStyle:
	if (cursive == 0 && param->fLetterStyle != 1)
		param->fFlags &= ~0x400;
	else
		param->fFlags |= 0x400;
	if ((phone == 0 && numbers == 0) || letters != 0 || param->fField4C == 0)
		param->fField48 = 0;
	else
		param->fField48 = 1;
	return 0;
}


// ROM 0x0022037c DomainParameter__12TStrXrDomainFUlN21
// What the area's parameter block (a STRXRPARAM in the handle `info`)
// is asked or told.  ==> 0, -1 for a selector not known or a value not
// accepted.
long
TStrXrDomain::DomainParameter(ULong selector, ULong result, ULong info)
{
	Handle h = (Handle) info;
	STRXRPARAM* p = nil;
	long err = -1;
	if (h != nil)
	{
		HLock(h);
		p = (STRXRPARAM*) *h;
	}
	switch (selector)
	{
	case 0:
		// DEVIATION: the host's block is bigger (a pointer in it); the ROM
		// answers 0x58
		*(ULong*) result = sizeof(STRXRPARAM);
		err = 0;
		break;
	case 1:
		// a new block: the letter set's style, the language, the letter
		// spacing, and the letter table loaded with the set's learning
		switch (gLetterSetSelection)
		{
		case 3:
			p->fLetterStyle = 4;
			p->fFlags = 0x0800;
			break;
		case 4:
			p->fLetterStyle = 1;
			break;
		case 1:
			p->fLetterStyle = 8;
			break;
		case 2:
			p->fLetterStyle = 4;
			break;
		default:
			p->fLetterStyle = 2;
			break;
		}
		p->fLanguage = 1;
		p->fField44 = 0;
		p->fControl = (p->fControl & 0xfffe0000) | 5;
		p->fControl = ((gUSE_GROUP_AND_CLASSIFY != 0 ? 4 : 0) << 17) | 5;
		{
			Handle trigrams;
			if (ReadDteResource("avp.dte", 1, &p->fDTI, &trigrams, 0) == 0
			 && AllocLearnInfo(&p->fDTI, gLetterSetSelection) == 0)
			{
				memset(p->fGeom, 0, sizeof(p->fGeom));
				p->fPrevBase[0] = p->fPrevBase[1] = p->fPrevBase[2] = p->fPrevBase[3] = 0;
				memset(p->fGrid, 0, sizeof(p->fGrid));
				p->fBoxHit = 0xffffffff;
				p->fField4C = 1;
				p->fFieldType = 1;
				err = SetStrXrFieldType(1, p);
			}
		}
		break;
	case 2:
		// whether the selector in `result` is one of these
		switch (result)
		{
		case 0: case 1: case 2: case 3:
		case 0x20005: case 0x20006: case 0x2000b: case 0x2000c: case 0x2000d: case 0x2000e: case 0x20014:
		case 0x20025: case 0x20026: case 0x20027: case 0x20028: case 0x20032: case 0x20041: case 0x20042:
		case 0x20043: case 0x20044: case 0x20045: case 0x20046: case 0x20047:
			err = 0;
			break;
		}
		break;
	case 3:
		if (p->fDTI != nil)
			UnloadData(&p->fDTI);
		err = 0;
		break;
	case 0x20005:
		*(ULong*) result = p->fFieldType;
		err = 0;
		break;
	case 0x20006:
		err = SetStrXrFieldType(result, p);
		if (err == 0)
			p->fFieldType = result;
		break;
	// (0x2000b and 0x2000d answer the geometry and the grid, 0x2000c and
	// 0x2000e set them: the ROM's memcpy takes the destination first -
	// ConfigFromFrame sets both with 0x2000c and 0x2000e)
	case 0x2000b:
		memcpy((void*) result, p->fGeom, sizeof(p->fGeom));
		err = 0;
		break;
	case 0x2000c:
		memcpy(p->fGeom, (void*) result, sizeof(p->fGeom));
		err = 0;
		break;
	case 0x2000d:
		memcpy((void*) result, p->fGrid, sizeof(p->fGrid));
		err = 0;
		break;
	case 0x2000e:
		memcpy(p->fGrid, (void*) result, sizeof(p->fGrid));
		err = 0;
		break;
	case 0x20014:
		{
			// the characters a field may hold, as a string: digits,
			// punctuation, and anything else (letters)
			const unsigned char* s = (const unsigned char*) result;
			Boolean other = false, punct = false, digit = false;
			if (s[0] == 0)
				break;
			for (short i = 0; s[i] != 0; i++)
			{
				// (the ROM's __ctype: 2 punctuation, 0x20 a digit)
				if (ispunct(s[i]))
					punct = true;
				else if (isdigit(s[i]))
					digit = true;
				else
					other = true;
			}
			if (!other && !punct && !digit)
				break;
			p->fFlags = 2;
			p->fFlags |= other ? 0x30 : 0x20;
			if (punct)
				p->fFlags |= 0x8000;
			err = 0;
		}
		break;
	case 0x20025:
		if (result > 9)
			break;
		p->fControl = (p->fControl & 0xffff0000) | result;
		err = 0;
		break;
	case 0x20026:
		*(ULong*) result = p->fControl & 0xffff;
		err = 0;
		break;
	case 0x20027:
		p->fControl |= 0x10000;
		err = 0;
		break;
	case 0x20028:
		p->fControl &= ~0x10000;
		err = 0;
		break;
	case 0x20032:
		SetStrXrRC(result, p);
		err = 0;
		break;
	case 0x20041:
		if (AllocLearnInfo(&p->fDTI, result) != 0)
			break;
		switch (result)
		{
		case 3:
			p->fLetterStyle = 4;
			p->fFlags = 0x0800;
			break;
		case 4:
			p->fLetterStyle = 1;
			break;
		case 1:
			p->fLetterStyle = 8;
			break;
		case 2:
			p->fLetterStyle = 4;
			break;
		default:
			p->fLetterStyle = 2;
			break;
		}
		err = SetStrXrFieldType(p->fFieldType, p);
		break;
	case 0x20042:
		{
			// the base line of the word just read, for the next (a
			// TStrXrUnit's geometry, from its fLeft)
			long* g = (long*) result;
			p->fPrevBase[0] = (short) (g[2] - g[4]);
			p->fPrevBase[1] = (short) g[2];
			p->fPrevBase[2] = (short) g[7];
			p->fPrevBase[3] = (short) g[8];
			err = 0;
		}
		break;
	case 0x20043:
	case 0x20044:
		err = 0;
		break;
	case 0x20045:
		p->fField4C = result != 0 ? 1 : 0;
		SetStrXrFieldType(p->fFieldType, p);
		err = 0;
		break;
	case 0x20046:
		p->fLanguage = 0;
		if ((result & 1) != 0)
			p->fLanguage |= 1;
		if ((result & 8) != 0)
			p->fLanguage |= 8;
		err = SetStrXrFieldType(p->fFieldType, p);
		break;
	case 0x20047:
		*(ULong*) result = 0;
		if ((p->fLanguage & 1) != 0)
			*(ULong*) result = 1;
		if ((p->fLanguage & 8) != 0)
			*(ULong*) result |= 8;
		err = 0;
		break;
	}
	if (h != nil)
		HUnlock(h);
	return err;
}


#pragma mark - the GC layer

// ROM 0x000d39c8 GCPregroupAndGroup__FP12TStrXrDomainP11TStrokeUnitUi
// A stroke given to the units still collecting writing - or, with none
// and not pre-grouping, a new unit started with it.
long
GCPregroupAndGroup(TStrXrDomain* domain, TStrokeUnit* stroke, ULong pregroup)
{
	Boolean failed = false;
	TUnitList* units = domain->fController->GetDelayList(domain, domain->fType);
	if (units != nil)
	{
		long count = units->fCount;
		if (count < 1)
		{
			if (pregroup == 0)
			{
				TAreaList* areas = stroke->GetAreas();
				TStrXrUnit* unit = TStrXrUnit::Make(domain, stroke->fKind + 1, (TArray*) areas);
				if (areas != nil)
					areas->Dispose();
				if (unit == nil)
					failed = true;
				else
				{
					unit->AddSub(stroke);
					domain->fController->NewGroup(unit);
				}
			}
		}
		else
		{
			for (ULong i = 0; (long) i < count; i++)
			{
				TStrXrUnit* unit = (TStrXrUnit*) units->GetUnit(i);
				if (unit != nil && unit->fActive != 0)
					CallGroupAndClassify(domain, unit, stroke, 0, pregroup, (domain->fControl & 0x10000) != 0);
			}
		}
		units->Dispose();
		if (!failed)
			return 0;
	}
	domain->fController->SignalMemoryError();
	return 0;
}


// ROM 0x000d3b0c CallGroupAndClassify__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitUiN24
// The unit's writing, with the new stroke (if any), grouped into words
// and the words that are ready read.  While the pen is still writing a
// stroke is simply listed and added (unless it is the last complete one,
// or the list is nearly full).  More than 250 strokes are done 250 at a
// time.  After reading, the words read are released as units of their
// own; what is left of the unit carries on collecting (its segmenter's
// state kept) - or, when everything it held has been read, the unit is
// ended and marked classified (0x8000000).  ==> whether anything was
// read.
ULong
CallGroupAndClassify(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, ULong classify, ULong pregroup, ULong lineAtATime)
{
	PS_point_type* trace = nil;
	ULong classified = 0;
	ULong released = 0;
	if (unit->fActive == 0)
	{
		domain->fController->NewClassification(unit);
		return 1;
	}
	UByte* unitStrokes = unit->fStrokes;
	if (classify == 0 && pregroup == 0 && stroke != nil && unit->fNumStrokes < 0xf9
	 && !domain->fController->IsLastCompleteStroke(stroke))
	{
		// only listed
		long end = Listed(unitStrokes) ? unit->fNumStrokes : unit->SubCount();
		end = (short) (end + 1);
		for (long s = unit->fNumStrokes; s < end; s++)
			SetBit(unitStrokes, s);
		unit->fNumStrokes = (short) end;
		unit->AddSub(stroke);
		return 0;
	}
	rc_type rc;
	long err = GCFillRecParmStruct(domain, unit, &rc);
	if (err != 0)
	{
		if (err == -3)
			domain->fController->SignalMemoryError();
		return classified;
	}
	long end = (short) unit->SubCount();
	if (stroke != nil)
		end = (short) (end + 1);
	if (end == 0)
		return classified;
	GCGroupParmStruct parm;
	memset(&parm, 0, sizeof(parm));
	if (unit->fRereading != 0)
	{
		parm.fJoinX = unit->fJoinX;
		parm.fJoinY = unit->fJoinY;
		parm.fMerged = unit->fMerged;
	}
	parm.fGRes = unit->fGRes;
	parm.fNumStrokes = unit->fNext;
	memcpy(parm.fStrokes, unitStrokes, sizeof(parm.fStrokes));
	long first = unit->fNumStrokes;
	long base = 0;
	if (Listed(unitStrokes))
	{
		end = first;
		if (stroke != nil)
			end = (short) (first + 1);
	}
	Boolean all;
	long rel = 0;
	do
	{
		parm.fSpacing = (short) (domain->fControl & 0xffff);
		parm.fLineAtATime = (short) ((domain->fControl >> 17) & 0xffff);
		parm.fField02 = (UByte) (RCGetH(&rc, 0x92) >> 8);
		parm.fField03 = (UByte) RCGetH(&rc, 0x92);
		parm.fSureLevel = (short) RCGetH(&rc, 0x26);
		if ((end + parm.fNumStrokes) - first <= 0xfa)
		{
			all = false;
			for ( ; first < end; first++)
				SetBit(parm.fStrokes, first - base);
			first = end;
		}
		else
		{
			all = true;
			for (long s = first - base; s < 0xfa; s++)
				SetBit(parm.fStrokes, s);
		}
		do
		{
			RCSetH(&rc, 0xea, domain->fPrevBase[0]);
			RCSetH(&rc, 0xec, domain->fPrevBase[1]);
			RCSetH(&rc, 0xee, domain->fPrevBase[2]);
			RCSetH(&rc, 0xf0, domain->fPrevBase[3]);
			short nPoints;
			GCAllocRecTrace(unit, stroke, parm.fStrokes, (short) base, &trace, &nPoints);
			if (trace == nil)
				goto done;
			err = GroupAndClassifyStrokes(trace, nPoints, &rc, &parm, &unit->fDescriptors,
					pregroup != 0 && !all, lineAtATime, classify != 0 && !all, &classified);
			HWRMemoryFree((Ptr) trace);
			trace = nil;
			rel = GCReleaseRecResults(domain, unit, stroke, parm.fStrokes, (short) base, classify, &released);
		} while (err == -4);
		if (rel == -3)
			break;
		if (!all && classify == 0 && parm.fNumStrokes != 0xfa)
			break;
		base = (short) (parm.fNext + base);
		if (all || parm.fNumStrokes == 0xfa)
			first = base;
		GCDisposeGResHandle(&parm.fGRes);
		memset(&parm, 0, sizeof(parm));
	} while (all);
done:
	GCClearChains(domain, unit);
	unit->fGRes = parm.fGRes;
	if (stroke != nil && pregroup != 0)
	{
		// the stroke is not the segmenter's yet
		if (parm.fNumStrokes != 0)
			parm.fNumStrokes = (short) (parm.fNumStrokes - 1);
		first = (short) (first - 1);
		end = (short) (end - 1);
		ClearBit(parm.fStrokes, parm.fNumStrokes & 0xff);
	}
	if (unit->fRereading != 0)
	{
		unit->fRereading = 0;
		return classified;
	}
	if (released == 0)
	{
		if (stroke != nil)
		{
			unit->fGRes = parm.fGRes;
			unit->fNext = parm.fNumStrokes;
			unit->fNumStrokes = (short) (first - base);
			memcpy(unitStrokes, parm.fStrokes, sizeof(parm.fStrokes));
			if (pregroup == 0)
				unit->AddSub(stroke);
			return classified;
		}
		if (classify == 0)
			return classified;
		domain->fController->NewClassification(unit);
		unit->SetFlags(0x400000);
		return classified;
	}
	if (Listed(parm.fStrokes) || first != end)
	{
		// what is not read goes on in a new unit
		TAreaList* areas = unit->GetSub(0)->GetAreas();
		TStrXrUnit* rest = TStrXrUnit::Make(domain, unit->GetSub(0)->fKind + 1, (TArray*) areas);
		if (areas != nil)
			areas->Dispose();
		if (rest == nil)
		{
			domain->fController->SignalMemoryError();
			return classified;
		}
		ULong subs = unit->SubCount();
		long span = end - base;
		for (ULong i = 0; (long) i < 0x100; i++)
		{
			if (first == end)
			{
				if (!BitIsSet(parm.fStrokes, i))
					continue;
			}
			else if (span <= (long) i)
				break;
			ULong at = (ULong) GCGetUnitRealStrokeIndex(unit, (short) (i + base));
			if (subs < at)
				break;
			if (at == subs)
			{
				if (stroke != nil && pregroup == 0)
					rest->AddSub(stroke);
			}
			else
				rest->AddSub(unit->GetSub(at));
		}
		rest->fDescriptors = unit->fDescriptors;
		unit->fDescriptors = nil;
		rest->fGRes = parm.fGRes;
		rest->fNext = parm.fNumStrokes;
		if (first == end)
		{
			rest->fNumStrokes = (short) (first - base);
			memcpy(rest->fStrokes, parm.fStrokes, sizeof(rest->fStrokes));
		}
		else
		{
			rest->fNumStrokes = (short) rest->SubCount();
			for (long s = 0; s < rest->fNumStrokes; s++)
				SetBit(rest->fStrokes, s);
		}
		unit->fGRes = nil;
		unit->fNext = 0;
		unit->fNumStrokes = 0;
		memset(unitStrokes, 0, sizeof(unit->fStrokes));
		if (classify == 0)
			domain->fController->NewGroup(rest);
		else
		{
			domain->fController->NewClassification(rest);
			rest->SetFlags(0x400000);
		}
	}
	unit->EndSubs();
	unit->SetFlags(0x8000000);
	return classified;
}


// ROM 0x000d4400 GCReleaseRecResults__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitPUcsUiPUi
// Every word that has been read (or that went wrong) released as a unit
// of its own (WriteRecResults), its strokes taken off the list and its
// descriptor thrown away; the base line of the last one handed to the
// domain for the next word.  ==> 0, -3 for a failure, -5 for nothing
// to release.
long
GCReleaseRecResults(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, UByte* strokes, short base, ULong classify, ULong* released)
{
	TStrXrUnit* last = nil;
	long err;
	if (unit == nil)
		return -5;
	Handle h = unit->fDescriptors;
	if (h == nil)
		return -5;
	GCWordDescrType* words = (GCWordDescrType*) HWRMemoryLockHandle(h);
	if (words == nil)
		err = -3;
	else
	{
		Boolean failed = false;
		GCWordDescrType* word = GCGetFirstWordDescriptor(words);
		while (word != nil)
		{
			GCWordDescrType* next = GCGetNextWordDescriptor(words, word);
			if ((word->fFlags & 0xf00) != 0 || word->fFlags == 0x54)
			{
				if (WriteRecResults(domain, unit, stroke, &last, word, base, classify, released) == -3)
					failed = true;
				if ((word->fFlags & 0x800) != 0)
					failed = true;
				GCWDRemoveStrokesFromList(word, strokes);
				if (word->fLearning != nil)
				{
					HWRMemoryFreeHandle(word->fLearning);
					word->fLearning = nil;
				}
				GCWordDescriptorDispose(words, word);
			}
			word = next;
		}
		err = failed ? -3 : 0;
	}
	if (last != nil)
		WritePrevBaseLineToStrXrDomain(domain, last);
	if (words != nil)
		HWRMemoryUnlockHandle(h);
	return err;
}


// ROM 0x000d4540 WriteRecResults__FP12TStrXrDomainP10TStrXrUnitP11TStrokeUnitPP10TStrXrUnitP15GCWordDescrTypesUiPUi
// A word read made into a unit of its own (the unit itself when it is
// being read again) - or, when the reader split it into several words,
// one unit for each: the word's strokes (the dash left out of a word
// continued after one) as its subs, where it lies, the base line found,
// and its readings.  A word that went wrong, or whose unit could not be
// filled, is flagged invalid (0x400000); otherwise it is the last good
// one.  ==> 0, -6, -7 for no memory.
long
WriteRecResults(TStrXrDomain* domain, TStrXrUnit* unit, TStrokeUnit* stroke, TStrXrUnit** last, GCWordDescrType* word, short base, ULong classify, ULong* released)
{
	GCRecResults* results = nil;
	long err = -7;
	if (last == nil || word == nil || unit == nil)
		return -6;
	ULong kind = unit->GetSub(0)->fKind + 1;
	long parts = 1;
	if (word->fRecResults != nil)
	{
		results = GCLockRecResultsHandle(word->fRecResults);
		if (results == nil)
			return -7;
		if (results->fSplit != nil)
			parts = ((UByte*) results->fSplit)[0xf];
	}
	long part = 0;
	Boolean failed = false;
	if (parts < 1)
	{
		err = 0;
		goto out;
	}
	do
	{
		TStrXrUnit* u;
		if (unit->fRereading == 0)
		{
			TAreaList* areas = unit->GetSub(0)->GetAreas();
			u = TStrXrUnit::Make(domain, kind, (TArray*) areas);
			if (areas != nil)
				areas->Dispose();
			if (u == nil)
				break;
		}
		else
		{
			u = unit;
			if (0 < part)
			{
				err = 0;
				goto out;
			}
		}
		*released = 1;
		u->fActive = 0;
		ULong subs = unit->SubCount();
		Boolean joined = false;
		if (unit->fRereading == 0)
		{
			// the word's strokes, in the order it lists them
			long pos = 0;
			Boolean pastDash = word->fMerged < 2;
			ULong s = word->fFirst;
			long e = 0;
			for (;;)
			{
				ULong strokeNo;
				for (;;)
				{
					if ((long) word->fLast < (long) s && (word->fExtra[e] == 0 || 7 < e))
					{
						joined = !(word->fMerged < 2 || !pastDash);
						goto strokesDone;
					}
					if (!pastDash && (long) (word->fMerged - 1) == pos)
						pastDash = true;		// (the dash: not counted)
					else
						pos++;
					if ((long) word->fLast < (long) s)
						strokeNo = word->fExtra[e++];
					else
						strokeNo = s++;
					if (parts < 2)
						break;
					// only the strokes of this part
					UByte* split = (UByte*) results->fSplit;
					long k = 0;
					for (long j = 0; j < part; j++)
						k += split[0x4c + j];
					long kEnd = k + split[0x4c + part];
					while (k < kEnd && split[0x58 + k] != pos)
						k++;
					if (k < kEnd)
						break;
				}
				ULong at = (ULong) GCGetUnitRealStrokeIndex(unit, (short) (strokeNo + base));
				if (at < subs)
					u->AddSub(unit->GetSub(at));
				if (stroke != nil && at == subs)
					u->AddSub(stroke);
			}
		}
strokesDone:
		if (parts < 2)
		{
			u->fLeft = (short) ((word->fInkBox[0] << 8) | word->fInkBox[1]);
			u->fRight = (short) ((word->fInkBox[4] << 8) | word->fInkBox[5]);
		}
		else
		{
			FRect box, all;
			for (ULong i = 0; i < (ULong) u->SubCount(); i++)
			{
				u->GetSub(i)->GetBBox(&box);
				AddRect(&box, &all, i == 0);
			}
			if (gTabScale.x == 0x80000)
			{
				u->fLeft = (short) ((ULong) (all.left * 8 + 0x8000) >> 16);
				u->fRight = (short) ((ULong) (all.right * 8 + 0x8000) >> 16);
			}
			else
			{
				u->fLeft = (short) ((ULong) (FixedMultiply(all.left, gTabScale.x) + 0x8000) >> 16);
				u->fRight = (short) ((ULong) (FixedMultiply(all.right, gTabScale.x) + 0x8000) >> 16);
			}
		}
		u->fBase = word->fBase[1];
		u->fBase2 = word->fBase[1];
		u->fHeight = word->fBase[1] - word->fBase[0];
		u->fHeight2 = word->fBase[1] - word->fBase[0];
		u->fField5C = word->fBase[2];
		u->fField60 = word->fBase[3];
		u->fField58 = word->fField12;
		u->fLineHeight = word->fLineHeight;
		u->fBaseLine = word->fBaseLine;
		u->fNewLine = word->fNewLine;
		for (long i = 0; i < 4; i++)
			u->fPrevBase[i] = word->fPrevBase[i];
		if (parts == 1)
		{
			u->fLearning = word->fLearning;
			word->fLearning = nil;
		}
		if (joined)
		{
			u->fJoinX = word->fJoinX;
			u->fJoinY = word->fJoinY;
			u->fMerged = word->fMerged;
		}
		if (results != nil && GCWriteRW(u, results, part) == -7)
			failed = true;
		if (unit->fRereading == 0)
		{
			if (classify == 0)
			{
				domain->fController->NewGroup(u);
				u->EndSubs();
			}
			else
				domain->fController->NewClassification(u);
		}
		if ((word->fFlags & 0xf00) == 0 && !failed)
			*last = u;
		else
			u->SetFlags(0x400000);
		part++;
		if (parts <= part)
		{
			err = 0;
			goto out;
		}
	} while (!failed);
	err = -7;
out:
	if (results != nil)
		GCUnlockRecResultsHandle(word->fRecResults);
	return err;
}


// ROM 0x000d4b6c GCWriteRW__FP10TStrXrUnitP12GCRecResultsi
// A word's readings put in its unit - for one part of a word the reader
// split, each reading's characters of that part only (a space after a
// part's last character dropped), its score and the part's own, and the
// readings that come out the same kept once.  ==> 0, -6, -7 for no
// memory.
long
GCWriteRW(TStrXrUnit* unit, GCRecResults* results, long part)
{
	if (unit == nil || results == nil || part < 0)
		return -6;
	UByte* split = (UByte*) results->fSplit;
	ULong parts = split == nil ? 1 : split[0xf];
	if ((long) parts <= part)
		return -6;
	long count = results->fCount;
	if (parts < 2 || count <= 0)
	{
		unit->fWordCount = (short) count;
		if (0 < unit->fWordCount)
		{
			unit->fWords = (rec_w_type*) HWRMemoryAlloc(unit->fWordCount * sizeof(rec_w_type));
			if (unit->fWords == nil)
			{
				unit->fWordCount = 0;
				return -7;
			}
			memcpy(unit->fWords, results->fWords, unit->fWordCount * sizeof(rec_w_type));
		}
		return 0;
	}
	rec_w_type* words = (rec_w_type*) HWRMemoryAlloc(count * sizeof(rec_w_type));
	if (words == nil)
		return -7;
	memset(words, 0, count * sizeof(rec_w_type));
	for (long w = 0; w < count && w < 5 && results->fWords[w].fWord[0] != 0; w++)
	{
		const rec_w_type* from = results->fWords + w;
		long k = 0, n = 0;
		ULong c = 0;
		do
		{
			UByte ch = from->fWord[c];
			if (ch == 0 || part < k)
				break;
			if (part == k)
			{
				words[w].fWord[n] = ch;
				words[w].fVariants[n] = from->fVariants[c];
				words[w].fX30[n] = from->fX30[c];
				n++;
			}
			if ((split[w * 3 + (c >> 3)] & (1 << (c & 7))) != 0)
			{
				if ((long) (c + 1) < 0x18 && from->fWord[c + 1] == ' ')
					c++;
				k++;
			}
			c++;
		} while ((long) c < 0x18);
		words[w].fWeight = from->fWeight;
		SByte partScore = (SByte) split[w * 0xc + k + 0x10];
		words[w].fX4A[0] = (UByte) (partScore >> 7);
		words[w].fX4A[1] = (UByte) partScore;
	}
	long distinct = count;
	for (long a = 0; a < results->fCount; a++)
	{
		for (long b = 0; words[a].fWord[0] != 0 && b < results->fCount; b++)
		{
			if (a != b && HWRStrCmp((char*) words[a].fWord, (char*) words[b].fWord) == 0)
			{
				words[b].fWord[0] = 0;
				distinct--;
			}
		}
	}
	if (results->fCount == distinct)
	{
		unit->fWordCount = results->fCount;
		unit->fWords = words;
	}
	else
	{
		if (0 < distinct)
		{
			unit->fWords = (rec_w_type*) HWRMemoryAlloc(distinct * sizeof(rec_w_type));
			if (unit->fWords == nil)
			{
				HWRMemoryFree((Ptr) words);
				return -7;
			}
			unit->fWordCount = (short) distinct;
			long n = 0;
			for (long a = 0; a < results->fCount; a++)
			{
				if (words[a].fWord[0] != 0 && n < distinct)
				{
					memcpy(unit->fWords + n, words + a, sizeof(rec_w_type));
					n++;
				}
			}
		}
		HWRMemoryFree((Ptr) words);
	}
	return 0;
}


// ROM 0x000d4f00 GCFillRecParmStruct__FP12TStrXrDomainP5TUnitP7rc_type
// The engine's parameters for the unit's area: the word domain's (the
// area's dictionary chains given it first), with what this domain was
// told of the field and its base line.  ==> 0, -5 for no word domain.
long
GCFillRecParmStruct(TStrXrDomain* domain, TUnit* unit, rc_type* rc)
{
	if (domain == nil || unit == nil || rc == nil)
		return -5;
	TXrWordDomain* xrw = (TXrWordDomain*) domain->fController->GetTypedDomain(kXrWordDomainType);
	if (xrw == nil)
		return -5;
	SetUpChains(xrw, unit);
	*rc = xrw->fRC;
	RCSetH(rc, 0x92, domain->fFlags2);
	RCSetH(rc, 0x90, domain->fFlags);
	*RCByte(rc, 0xb6) = (UByte) domain->fField70;
	RCSetH(rc, 0xac, (UShort) domain->fGeom[6]);
	RCSetH(rc, 0xf4, (UShort) (domain->fGeom[2] - domain->fGeom[4]));
	RCSetH(rc, 0xf6, (UShort) domain->fGeom[2]);
	AdjustRecParmStruct(xrw, rc);
	return 0;
}


// ROM 0x000d4ff0 GCClearChains__FP12TStrXrDomainP5TUnit
// (Nothing: ==> 0 when there is a word domain, -5 otherwise.)
long
GCClearChains(TStrXrDomain* domain, TUnit* unit)
{
	if (domain != nil && unit != nil && domain->fController->GetTypedDomain(kXrWordDomainType) != nil)
		return 0;
	return -5;
}


// ROM 0x000d502c GCAllocRecTrace__FP10TStrXrUnitP11TStrokeUnitPUcsPP13PS_point_typePs
// The listed strokes as a trace: the unit's subs, and the new stroke
// for the one past them.  A stroke not in the unit leaves no trace.
void
GCAllocRecTrace(TStrXrUnit* unit, TStrokeUnit* stroke, UByte* strokes, short base, PS_point_type** trace, short* nPoints)
{
	TStroke* local[20];
	TStroke** list = local;
	if (trace == nil || nPoints == nil)
		return;
	*trace = nil;
	*nPoints = 0;
	if (unit == nil || strokes == nil)
		return;
	ULong subs = unit->SubCount();
	ULong count = 0;
	for (ULong s = 0; s < 0x100; s++)
		if (BitIsSet(strokes, s))
			count++;
	if (count == 0)
		return;
	if (0x14 <= count)
	{
		// DEVIATION: host pointers (the ROM's list is count * 4 + 4 bytes)
		list = (TStroke**) HWRMemoryAlloc((count + 1) * sizeof(TStroke*));
		if (list == nil)
			return;
	}
	{
		long k = 0;
		for (ULong s = 0; s < 0x100; s++)
		{
			if (!BitIsSet(strokes, s))
				continue;
			ULong at = (ULong) GCGetUnitRealStrokeIndex(unit, (short) (s + base));
			if (at == subs && stroke != nil)
				list[k] = stroke->fStroke;
			else
			{
				if (subs <= at)
					goto out;
				list[k] = ((TStrokeUnit*) unit->GetSub(at))->fStroke;
			}
			k++;
		}
		list[k] = nil;
		Boolean acquired = stroke != nil && AcquireStroke(stroke->fStroke);
		short nStrokes;
		NewGetTraceFromStrokes(list, trace, &nStrokes, nPoints);
		if (acquired)
			ReleaseStroke();
	}
out:
	if (list != local && list != nil)
		HWRMemoryFree((Ptr) list);
}


// ROM 0x000d51d4 GCGetUnitRealStrokeIndex__FP10TStrXrUnits
// A listed stroke's place among the unit's subs: the listed strokes
// before it (the index itself when nothing is listed; past 250, the
// strokes listed and the rest counted on).
long
GCGetUnitRealStrokeIndex(TStrXrUnit* unit, short index)
{
	if (!Listed(unit->fStrokes))
		return index;
	if (0xf9 < index)
		return (short) (GCGetNumOfStrokesInList(unit->fStrokes) + index - 0xfa);
	return GCGetRealStrokeIndex(unit->fStrokes, index);
}


// ROM 0x000d5240 GroupAndClassifyStrokes__FP13PS_point_typesP7rc_typeP17GCGroupParmStructPUlUiN26PUi
// The trace grouped into words (the descriptors made when there are
// none) and the words ready read.  Reading a line at a time, the
// segmenter is not asked: the listed strokes are one word, read at the
// end (or, continuing after a dash, joined).  ==> 0, -3 no memory, -4
// resumed next time, -6 nothing to do, or GCClassifyStrokes' error.
long
GroupAndClassifyStrokes(PS_point_type* trace, short nPoints, rc_type* rc, GCGroupParmStruct* parm, Handle* descriptors, ULong pregroup, ULong lineAtATime, ULong final, ULong* classified)
{
	GCWordDescrType* words = nil;
	long err;
	Boolean resume = false;
	*classified = 0;
	if (trace == nil || rc == nil || nPoints < 3)
		return -6;
	if (*descriptors == nil)
	{
		*descriptors = GCNewRecSegment();
		if (*descriptors == nil)
			return -3;
	}
	words = (GCWordDescrType*) HWRMemoryLockHandle(*descriptors);
	if (words == nil)
		return -3;
	if (lineAtATime == 0)
		err = GCGroupStrokes(words, trace, nPoints, final, pregroup, parm);
	else
	{
		long s = parm->fNumStrokes;
		while (s < 0x100 && BitIsSet(parm->fStrokes, s))
			s++;
		parm->fNumStrokes = (short) s;
		// ROM QUIRK: without `final`, or when pre-grouping, the error
		// tested next is whatever the caller had in r6 - the stroke
		// unit's address, which is neither -1 nor -4
		err = 0;
		if (final != 0 && pregroup == 0)
		{
			ULong flags = parm->fMerged != 0 ? 0x80 : 4;
			err = GCWordDescWriteGroupResults(words, 0, (UByte) (s - 1), nil, 0, 0, 0, 0, 0, flags, nil);
			if (flags == 0x80 && err == 0)
			{
				GCWordDescrType* word = GCGetWordDescWithFlags(words, 0x80, 1);
				if (word != nil)
				{
					word->fJoinX = parm->fJoinX;
					word->fJoinY = parm->fJoinY;
					word->fMerged = parm->fMerged;
					word->fFlags = 4;
				}
				goto classify;
			}
		}
	}
	if (err == -1)
		goto done;
	resume = err == -4;
classify:
	err = GCClassifyStrokes(words, trace, rc, parm, classified);
	if (err == 0 && resume)
		err = -4;
done:
	if (*descriptors != nil && words != nil)
		HWRMemoryUnlockHandle(*descriptors);
	return err;
}


// ROM 0x000d546c GCClassifyStrokes__FP15GCWordDescrTypeP13PS_point_typeP7rc_typeP17GCGroupParmStructPUi
// The words to be read now (flags exactly 4) read - each told the base
// line of the last word read before it, or the domain's - and, when there
// were none, the settled ones (exactly 2) read ahead.  ==> 0, -5, or -6
// for a word with nothing to read.
long
GCClassifyStrokes(GCWordDescrType* words, PS_point_type* trace, rc_type* rc, GCGroupParmStruct* parm, ULong* classified)
{
	if (words == nil || trace == nil || rc == nil)
		return -5;
	ULong flags = 4;
	for (;;)
	{
		Boolean read = false;
		GCWordDescrType* word = GCGetWordDescWithFlags(words, flags, 1);
		while (word != nil)
		{
			if (flags == 4)
				read = true;
			short b0, b1, b2, b3;
			GCWordDescrType* before;
			for (before = GCGetPrevWordDescriptor(words, word); before != nil; before = GCGetPrevWordDescriptor(words, before))
				if ((before->fFlags & 0x70) != 0)
					break;
			if (before != nil && before != word)
			{
				b0 = before->fBase[0];
				b1 = before->fBase[1];
				b2 = before->fBase[2];
				b3 = before->fBase[3];
			}
			else
			{
				b0 = (short) RCGetH(rc, 0xea);
				b1 = (short) RCGetH(rc, 0xec);
				b2 = (short) RCGetH(rc, 0xee);
				b3 = (short) RCGetH(rc, 0xf0);
			}
			GCWDWritePrevBaseLine(b0, b1, b2, b3, word);
			*classified = 1;
			if (GCTryToRecognize(trace, word, rc, parm) == -6)
				return -6;
			word = GCGetWordDescWithFlags(words, flags, 1);
		}
		if (flags == 2)
			return 0;
		flags = 2;
		if (read)
			return 0;
	}
}


// ROM 0x00065d48 WritePrevBaseLineToStrXrDomain__FP12TStrXrDomainP10TStrXrUnit
// The base line of a word just read kept in the area's parameters for
// the next word.
void
WritePrevBaseLineToStrXrDomain(TStrXrDomain* domain, TStrXrUnit* unit)
{
	TRecArea* area = unit->GetArea();
	if (area == nil)
		return;
	Handle h = area->GetInfoFor(kStrXrDomainType, false);
	if (h == nil)
		return;
	if (domain->DomainParameter(0x20042, (ULong) &unit->fLeft, (ULong) h) != 0)
		return;
	HLock(h);
	domain->SetParameters(h);
	HUnlock(h);
}


// ROM 0x0024e650 SetUpChains__FP13TXrWordDomainP5TUnit
// The area's two dictionary chains (those with a dictionary in them)
// and its third given to the word domain's parameters, which are then
// read in.
void
SetUpChains(TXrWordDomain* domain, TUnit* unit)
{
	TRecArea* area = unit->GetArea();
	Handle h = area->GetInfoFor(kXrWordDomainType, false);
	void* chains[2] = { nil, nil };
	if (area->fDictionaries[0] != nil && area->fDictionaries[0]->GetEntry(0) != nil)
		chains[0] = area->fDictionaries[0];
	if (area->fDictionaries[1] != nil && area->fDictionaries[1]->GetEntry(0) != nil)
		chains[1] = area->fDictionaries[1];
	domain->DomainParameter(0x20002, (ULong) chains, (ULong) h);
	XrChainInfo third;
	memset(&third, 0, sizeof(third));
	if (area->fDictionaries[2] != nil && area->fDictionaries[2]->GetEntry(0) != nil)
		third.fChain = area->fDictionaries[2];
	domain->DomainParameter(0x20012, (ULong) &third, (ULong) h);
	HLock(h);
	domain->SetParameters(h);
	HUnlock(h);
}


// ROM 0x0024e77c AdjustRecParmStruct__FP13TXrWordDomainP7rc_type
// The parameters made to agree with the domain: whether learning is on,
// no vocabulary flag without chains, and for a word field with a lexical
// database (or a number or phone field) the flags that go with it.
void
AdjustRecParmStruct(TXrWordDomain* domain, rc_type* rc)
{
	*RCByte(rc, 0xaf) = domain->f134 != 0 ? 1 : 0;
	if ((RCGetH(rc, 0x08) & 1) != 0 && domain->fChain0 == nil && domain->fChain1 == nil)
		RCSetH(rc, 0x08, RCGetH(rc, 0x08) & ~1);
	if (domain->f15C != 0)
		return;
	if (rc->fChain != nil && RCGetH(rc, 0x00) == 1)
	{
		ULong type = domain->fFieldType;
		Boolean plain = (type & 1) == 0 && (type & 4) == 0 && (type & 0x40) == 0;
		if (plain)
			RCSetH(rc, 0x02, 0);
		if (!plain && domain->fChain1 != nil)
			return;
		RCSetH(rc, 0x02, RCGetH(rc, 0x02) & 0xffd9);
		return;
	}
	if (RCGetH(rc, 0x00) != 5 && RCGetH(rc, 0x00) != 4)
		return;
	if ((domain->fFieldType & 1) != 0)
	{
		RCSetH(rc, 0x02, RCGetH(rc, 0x02) | 1);
		RCSetH(rc, 0x08, RCGetH(rc, 0x08) | 4);
	}
}
