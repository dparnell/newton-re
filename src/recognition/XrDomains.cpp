/*
	File:		recognition/XrDomains.cpp

	Contains:	The cursive recogniser's domains and their parameter
				blocks.  See XrDomains.h.
*/

#include "XrDomains.h"
#include "Controller.h"
#include "Areas.h"
#include "Dictionaries.h"		// TDictChain
#include "Recognizer.h"			// gLetterSetSelection, gRecognitionTimeout
#include "StrokeQueue.h"			// gTabScale
#include "InkGroups.h"
#include "WordDescriptors.h"
#include "CursiveReader.h"
#include "NewtonMemory.h"
#include "FixedMath.h"
#include <string.h>

extern const unsigned char	lpunct_charset[8];
extern const unsigned char	epunct_charset[12];
extern const unsigned char	other_charset[16];
extern const unsigned char	num_charset[12];
extern const unsigned char	math_charset[12];
extern const unsigned char	bd_xrws_v[12];
extern const unsigned char	ts_xrws_v[12];
extern const unsigned char	bd_xrlws[12];
extern const unsigned char	ts_xrlws[12];
extern const unsigned char	bd_xrspl[12];
extern const unsigned char	ts_xrspl[12];
extern const unsigned char	bd_xrws_nv[12];
extern const unsigned char	ts_xrws_nv[12];


/*------------------------------------------------------------------------------
	T h e   b l o c k ' s   f i e l d s
------------------------------------------------------------------------------*/

// (host: the ROM's block is one run of bytes; the host's keeps its
//  numbers in arrays at the ROM's offsets and its pointers apart)
UByte*
RCByte(rc_type* rc, ULong offset)
{
	if (offset < 0x30)
		return rc->fB00 + offset;
	if (offset >= 0x90 && offset < 0xbc)
		return rc->fB90 + (offset - 0x90);
	if (offset >= 0xc0 && offset < 0xf8)
		return rc->fBC0 + (offset - 0xc0);
	if (offset >= 0xfc && offset < 0x108)
		return rc->fBFC + (offset - 0xfc);
	return nil;
}


UByte*
XRWByte(XRWORDPARAM* param, ULong offset)
{
	if (offset < 0x10c)
		return RCByte(param, offset);
	if (offset < 0x118)
		return param->fB10C + (offset - 0x10c);
	if (offset >= 0x124 && offset < 0x128)
		return param->fB124 + (offset - 0x124);
	if (offset >= 0x134 && offset < 0x160)
		return param->fB134 + (offset - 0x134);
	return nil;
}


// ROM 0x000d69dc GCLockDTEAndLearningData__FPvP13RcHandlesType
long
GCLockDTEAndLearningData(rc_type* rc, RcHandlesType* saved)
{
	long failures = 0;
	if (rc == nil || saved == nil)
		return 0;
	saved->fDTI = rc->fDTI;
	saved->fOrtho = rc->fOrtho;
	if (rc->fDTI != nil)
	{
		DTIHeader* dti = (DTIHeader*) HWRMemoryLockHandle((Handle) rc->fDTI);
		rc->fDTI = dti;
		if (dti == nil || dti_lock(dti) != 0)
			failures = 1;
	}
	if (rc->fOrtho != nil)
	{
		Ptr db = HWRMemoryLockHandle((Handle) rc->fOrtho);
		rc->fOrtho = db;
		if (db == nil)
			failures++;
	}
	if (failures == 0)
		return 1;
	GCUnlockDTEAndLearningData(rc, saved);
	return 0;
}


// ROM 0x000d6a80 GCUnlockDTEAndLearningData__FPvP13RcHandlesType
void
GCUnlockDTEAndLearningData(rc_type* rc, RcHandlesType* saved)
{
	if (rc == nil || saved == nil)
		return;
	if (rc->fDTI != nil)
		dti_unlock((DTIHeader*) rc->fDTI);
	rc->fDTI = saved->fDTI;
	if (rc->fDTI != nil)
		HWRMemoryUnlockHandle((Handle) rc->fDTI);
	rc->fOrtho = saved->fOrtho;
	if (rc->fOrtho != nil)
		HUnlock((Handle) rc->fOrtho);
}


// ROM 0x000d7030 GCSetUpRecTableAndCharset__FP7rc_typeUi
void
GCSetUpRecTableAndCharset(rc_type* rc, ULong setAlpha)
{
	SetOsRecTableAndCharSet((short) RCGetH(rc, 0x06) != 1);
	if (setAlpha != 0)
		rc->fAlphaCharset = alpha_charset;
}


// ROM 0x0024e394 SetXrWordFieldType__FUlP7rc_type
long
SetXrWordFieldType(ULong type, rc_type* rc)
{
	ULong upper = type & 4;
	ULong numbers = type & 2;
	ULong phone = type & 0x10;
	ULong x40 = type & 0x40;
	if ((type & 0x3f) == 0 && x40 == 0)
		return -1;

	RCSetH(rc, 0x08, RCGetH(rc, 0x08) | 2);
	RCSetH(rc, 0x0c, 0);
	if (type == 0x20)
	{
		RCSetH(rc, 0x0e, 1);
		RCSetH(rc, 0x08, 2);
		RCSetH(rc, 0x02, 0x3b);
	}
	else
		RCSetH(rc, 0x0e, 0);
	UShort h2 = RCGetH(rc, 0x02);
	h2 = upper ? (h2 | 1) : (h2 & ~1);
	h2 = (numbers || phone) ? (h2 | 2) : (h2 & ~2);
	h2 = numbers ? (h2 | 0x20) : (h2 & ~0x20);
	h2 = phone ? (h2 | 4) : (h2 & ~4);
	h2 = (type & 8) ? (h2 | 0x18) : (h2 & ~0x18);
	RCSetH(rc, 0x02, h2);
	UShort h8 = RCGetH(rc, 0x08);
	h8 = (type & 1) ? (h8 | 1) : (h8 & ~1);
	h8 = (x40 || upper) ? (h8 | 4) : (h8 & ~4);
	RCSetH(rc, 0x08, h8);
	if (x40)
		RCSetH(rc, 0x02, RCGetH(rc, 0x02) | 1 | 0x20 | 2);
	if ((short) RCGetH(rc, 0x04) == 1)
	{
		RCSetH(rc, 0x0a, RCGetH(rc, 0x0a) | 3);
		RCSetH(rc, 0x08, RCGetH(rc, 0x08) | 0xc0);
		RCSetH(rc, 0x1e, 0x3f);
	}
	else
	{
		RCSetH(rc, 0x0a, RCGetH(rc, 0x0a) & ~3);
		RCSetH(rc, 0x08, RCGetH(rc, 0x08) & ~0xc0);
		RCSetH(rc, 0x1e, 0x37);
	}
	short kind = RCGetH(rc, 0x00);
	if (kind == 5 || kind == 4 || kind == 2)
		RCSetH(rc, 0x1e, 0x3f);
	if (kind == 5)
		RCSetH(rc, 0x0a, RCGetH(rc, 0x0a) | 2);
	RCSetH(rc, 0x90, RCGetH(rc, 0x90) & 0x0800);
	PrintFieldType(type);
	return 0;
}


// ROM 0x0024e288 SetXrWordFieldSpeed__FUlP7rc_typeT1
long
SetXrWordFieldSpeed(ULong speed, rc_type* rc, ULong type)
{
	if (speed > 9)
		return -1;
	ULong index = 9 - speed;
	const UByte* ts;
	short kind = RCGetH(rc, 0x00);
	if (kind == 1)
	{
		if ((type & 1) != 0)
		{
			RCSetH(rc, 0x14, bd_xrws_v[index]);
			ts = ts_xrws_v;
		}
		else if (type == 0x10)
		{
			RCSetH(rc, 0x14, 10);
			RCSetH(rc, 0x10, 3);
			return 0;
		}
		else
		{
			RCSetH(rc, 0x14, bd_xrws_nv[index]);
			ts = ts_xrws_nv;
		}
	}
	else if (kind == 2)
	{
		RCSetH(rc, 0x14, bd_xrspl[index]);
		ts = ts_xrspl;
	}
	else
	{
		RCSetH(rc, 0x14, bd_xrlws[index]);
		ts = ts_xrlws;
	}
	RCSetH(rc, 0x10, ts[index]);
	return 0;
}


// ROM 0x0024e64c PrintFieldType__FUl
ULong
PrintFieldType(ULong type)
{
	return type;
}


// ROM 0x0006555c LongOperator__FUcPll
void
LongOperator(UByte op, long* value, long operand)
{
	switch (op)
	{
	case 0:	*value = operand; return;
	case 1:	*value = *value | operand; return;
	case 2:	*value = *value & operand; return;
	case 3:	*value = *value ^ operand; return;
	case 4:	*value = (int32_t) (*value + operand); return;
	case 5:	*value = (int32_t) (*value - operand); return;
	case 6:	*value = (int32_t) (operand - *value); return;
	case 7:	*value = (int32_t) (operand * *value); return;
	case 8:	*value = *value / operand; return;
	case 9:	*value = operand / *value; return;
	default: return;
	}
}


// ROM 0x0006561c RCShortOperator__FUcPss
void
RCShortOperator(UByte op, UByte* field, short operand)
{
	long value = (short) ((field[0] << 8) | field[1]);
	LongOperator(op, &value, operand);
	field[1] = value;
	field[0] = value >> 8;
}


// ROM 0x00065664 RCUShortOperator__FUcPUsUs
void
RCUShortOperator(UByte op, UByte* field, UShort operand)
{
	long value = (field[0] << 8) | field[1];
	LongOperator(op, &value, operand);
	field[1] = value;
	field[0] = value >> 8;
}


// ROM 0x000656ac RCCharOperator__FUcPcc
void
RCCharOperator(UByte op, UByte* field, char operand)
{
	long value = field[0];
	LongOperator(op, &value, (UByte) operand);
	field[0] = value;
}


// ROM 0x000656e4 RCUCharOperator__FUcPUcT1
void
RCUCharOperator(UByte op, UByte* field, UByte operand)
{
	long value = field[0];
	LongOperator(op, &value, operand);
	field[0] = value;
}


// ROM 0x0006571c RCBooleanOperator__FUcPUcT1
void
RCBooleanOperator(UByte op, UByte* field, UByte operand)
{
	long value = field[0];
	LongOperator(op, &value, operand);
	field[0] = value;
}


// ROM 0x00065dd4 SetXrWordRC__FUlP11XRWORDPARAM
// DEVIATION: a byte offset that lands in one of the block's pointers
// changes nothing on the host (see XrDomains.h).
void
SetXrWordRC(ULong command, XRWORDPARAM* param)
{
	UByte op = (command >> 25) & 0x1f;
	ULong which = (command >> 16) & 0xff;
	ULong low = command & 0xffff;
	if (((command >> 24) & 1) == 1 && low < 0x160)
	{
		UByte* field = XRWByte(param, low);
		if (field != nil)
			RCUCharOperator(op, field, which);
		return;
	}
	short sOperand = (short) low;
	UByte hi = (low >> 8) & 0xff;
	UByte lo = command & 0xff;
	if (which <= 0x16)
	{
		RCShortOperator(op, XRWByte(param, which * 2), sOperand);
		return;
	}
	switch (which)
	{
	case 0x17:	RCUShortOperator(op, XRWByte(param, 0x90), low); break;
	case 0x18:	RCUShortOperator(op, XRWByte(param, 0x92), low); break;
	case 0x19:	RCUShortOperator(op, XRWByte(param, 0x94), low); break;
	case 0x1a:	RCShortOperator(op, XRWByte(param, 0x96), sOperand); break;
	case 0x21:	RCShortOperator(op, XRWByte(param, 0xea), sOperand); break;
	case 0x22:	RCShortOperator(op, XRWByte(param, 0xec), sOperand); break;
	case 0x23:	RCShortOperator(op, XRWByte(param, 0xee), sOperand); break;
	case 0x24:	RCShortOperator(op, XRWByte(param, 0xf0), sOperand); break;
	case 0x25:	RCShortOperator(op, XRWByte(param, 0xac), sOperand); break;
	case 0x26:	RCUCharOperator(op, XRWByte(param, 0xae), lo); break;
	case 0x27:	RCUCharOperator(op, XRWByte(param, 0xaf), lo); break;
	case 0x28:	RCUCharOperator(op, XRWByte(param, 0xb0), lo); break;
	case 0x29:	RCUShortOperator(op, XRWByte(param, 0xb2), low); break;
	case 0x2a:	RCUShortOperator(op, XRWByte(param, 0xb4), low); break;
	case 0x2b:	RCUCharOperator(op, XRWByte(param, 0xb6), lo); break;
	case 0x2c:	RCUCharOperator(op, XRWByte(param, 0xb7), lo); break;
	case 0x2f:	RCCharOperator(op, XRWByte(param, 0x10c), lo); break;
	case 0x30:	RCBooleanOperator(op, XRWByte(param, 0x15e), lo); break;
	case 0x31:	RCBooleanOperator(op, XRWByte(param, 0x15f), lo); break;
	case 0x33:	RCShortOperator(op, XRWByte(param, 0xb8), sOperand); break;
	case 0x34: case 0x35: case 0x36: case 0x37: case 0x38: case 0x39:
	case 0x3a: case 0x3b: case 0x3c: case 0x3d: case 0x3e: case 0x3f:
		{
			ULong at = 0xc0 + (which - 0x34) * 2;
			RCCharOperator(op, XRWByte(param, at), hi);
			RCCharOperator(op, XRWByte(param, at + 1), lo);
		}
		break;
	default:
		break;
	}
}


/*------------------------------------------------------------------------------
	L e a r n i n g
------------------------------------------------------------------------------*/

// ROM 0x001059b4 LHAddEntry__FPUlUlN22PvT2
// An entry added to the training data (made when there is none): a new
// block with room for one more entry header and the data after the
// others', the old one copied into it (each offset moved along by the
// header) and given back.  ==> 0; -2 for no data, -4 for an entry with
// those ids already there, -1 for no memory.
long
LHAddEntry(Handle* h, ULong id1, ULong id2, ULong id3, void* data, ULong size)
{
	ULong32* old = nil;
	Handle made = nil;
	ULong32* last = nil;
	long result;
	if (data == nil || size == 0)
		return -2;
	ULong total;
	ULong at;
	if (*h == nil)
	{
		total = size + 0x18;
		at = 0x18;
	}
	else
	{
		old = (ULong32*) LHLock(*h);
		if (old == nil)
			return -1;
		result = LHFindEntry(old, id1, id2, id3, nil, nil);
		if (result != -3)
		{
			if (result == 0)
				result = -4;
			goto fail;
		}
		last = old + old[0] * 5 - 4;
		total = last[3] + last[4] + size + 0x14;
		at = total - size;
	}
	made = HWRMemoryAllocHandle(total);
	ULong32* words;
	if (made == nil || (words = (ULong32*) LHLock(made)) == nil)
	{
		result = -1;
		goto fail;
	}
	words[0] = 0;
	if (old != nil)
	{
		ULong count = old[0];
		memcpy((UByte*) words + count * 0x14 + 0x18, (UByte*) old + count * 0x14 + 4, last[3] + last[4] - count * 0x14 - 4);
		words[0] = old[0];
		for (ULong i = 0; i < old[0]; i++)
		{
			ULong32* from = old + 1 + i * 5;
			ULong32* to = words + 1 + i * 5;
			memcpy(to, from, 5 * sizeof(ULong32));
			to[3] = from[3] + 0x14;
		}
		LHUnLock(*h);
		HWRMemoryFreeHandle(*h);
	}
	{
		words[0]++;
		ULong32* entry = words + 1 + (words[0] - 1) * 5;
		entry[0] = (ULong32) id1;
		entry[1] = (ULong32) id2;
		entry[2] = (ULong32) id3;
		entry[3] = (ULong32) at;
		entry[4] = (ULong32) size;
		memcpy((UByte*) words + at, data, size);
		LHUnLock(made);
		*h = made;
	}
	return 0;
fail:
	if (old != nil && *h != nil)
		LHUnLock(*h);
	if (made != nil)
		HWRMemoryFreeHandle(made);
	return result;
}


// ROM 0x00105bb8 LHLock__FUl
Ptr
LHLock(Handle h)
{
	if (h == nil)
		return nil;
	return HWRMemoryLockHandle(h);
}


// ROM 0x00105bc8 LHUnLock__FUl
long
LHUnLock(Handle h)
{
	if (h == nil)
		return -2;
	HWRMemoryUnlockHandle(h);
	return 0;
}


// ROM 0x00105bec LHFindEntry__FPvUlN22PPvPUl
// The data is a count and then that many entries of five words: the
// three ids, the entry's offset from the start of the data and its size.
long
LHFindEntry(void* data, ULong id1, ULong id2, ULong id3, void** entry, ULong* size)
{
	if (data == nil)
		return -2;
	const ULong kAny = '****';
	ULong32* words = (ULong32*) data;
	ULong count = words[0];
	ULong i;
	ULong32* e = nil;
	for (i = 0; i < count; i++)
	{
		e = words + 1 + i * 5;
		if ((id1 == kAny || e[0] == id1) && (id2 == kAny || e[1] == id2) && (id3 == kAny || e[2] == id3))
			break;
	}
	void* found = nil;
	ULong foundSize = 0;
	long result;
	if (i < count)
	{
		found = (UByte*) data + e[3];
		foundSize = e[4];
		result = 0;
	}
	else
		result = -3;
	if (entry != nil)
		*entry = found;
	if (size != nil)
		*size = foundSize;
	return result;
}


// ROM 0x00087280 FlyLearn__FP7rc_typePC10rec_w_type
long
FlyLearn(rc_type* rc, const rec_w_type* word)
{
	DTIHeader* dti = (DTIHeader*) rc->fDTI;
	for (long i = 0; i < 0x18; i++)
	{
		if (word->fWord[i] == 0)
			break;
		if (word->fX30[i] == 0)
			return 1;
	}
	UByte* table = nil;
	if (dti != nil && word != nil)
		table = dti->fDTEMain;
	if (dti != nil && word != nil && table != nil
	&&  word->fWeight >= 0x3c
	&&  FLUpdateCounters(rc, word) == 0
	&&  FLUpdateStates(rc, word) == 0)
		return 0;
	return 1;
}


// ROM 0x00087410 FLUpdateCounters__FP7rc_typePC10rec_w_type
// The variant each letter was read as has its counter put back to
// nought; every other variant of the letter is counted up (to 31).
long
FLUpdateCounters(rc_type* rc, const rec_w_type* word)
{
	DTIHeader* dti = (DTIHeader*) rc->fDTI;
	UByte* info = dti->fLearnInfoPtr;
	for (ULong i = 0; ; )
	{
		ULong c = word->fWord[i];
		if (c == 0)
			return 0;
		if ((word->fVariants[i] & 0x80) != 0)
		{
			if (IsLower(c))
				c = ToUpper(c);
			else if (IsUpper(c))
				c = ToLower(c);
		}
		UByte rec = OSToRec(c);
		if (rec == 0)
			return 1;
		UByte read = word->fVariants[i];
		long count = GetNumVarsOfChar(c, dti);
		for (long v = 0; v < count; v++)
		{
			UByte* p = info + rec * 0x10 + v - 0x200;
			UByte b = *p;
			UByte n = b >> 3;
			if (v == (read & 0x0f))
			{
				if (n != 0)
					n = 0;
			}
			else if (n < 0x1f)
				n++;
			*p = (b & 7) | (n << 3);
		}
		i++;
		if (i > 0x17)
			return 0;
	}
}


// ROM 0x0008751c FLUpdateStates__FP7rc_typePC10rec_w_type
// Each variant's vex moved by its counter: a variant used lately (a
// counter of nought) keeps the descriptor's default, one not used for
// long enough (31) drops to 7, and none may be more than three worse than
// the best of its group.
long
FLUpdateStates(rc_type* rc, const rec_w_type* word)
{
	DTIHeader* dti = (DTIHeader*) rc->fDTI;
	UByte* info = dti->fLearnInfoPtr;
	for (ULong i = 0; i < 0x18; i++)
	{
		ULong c = word->fWord[i];
		if (c == 0)
			return 0;
		long rec = OSToRec(c);
		if (rec == 0 || (c >= 0x80 && rec <= 0x7f))
			continue;
		long count = GetNumVarsOfChar(c, dti);
		for (ULong v = 0; (long) v < count; v++)
		{
			UByte* p = info + rec * 0x10 + v - 0x200;
			UByte b = *p;
			UByte n = b >> 3;
			dte_sym_header_type* descriptor;
			long index = GetSymDescriptor(OSToRec(c), v, &descriptor, dti);
			if (index < 0)
				return 1;
			long deflt = descriptor[index + 0x14] & 7;
			long vex = deflt;
			if (n != 0)
			{
				vex = b & 7;
				if (n == 0x1f)
					vex = 7;
			}
			long least = GetMinGroupVex(c, v, rc);
			if (least >= 0 && least + 3 < vex)
				vex = least + 3;
			if (vex < deflt)
				vex = deflt;
			*p &= 0xf8;
			*p |= vex;
		}
	}
	return 0;
}


// ROM 0x0008767c GetMinGroupVex__FUcT1P7rc_type
long
GetMinGroupVex(UByte c, UByte variant, rc_type* rc)
{
	long least = 7;
	DTIHeader* dti = (DTIHeader*) rc->fDTI;
	long group = GetVarGroup(c, variant, dti);
	if (group < 0)
		return -1;
	long count = GetNumVarsOfChar(c, dti);
	for (long v = 0; v < count; v++)
	{
		long vex;
		if (GetVarGroup(c, v, dti) == group && (vex = GetVarVex(c, v, dti)) < least)
			least = vex;
	}
	return least;
}


// ROM 0x0024e8a8 XRWDoLearning__FUlP11XRWORDPARAM
// What the writer settled on learnt: the word the unit read it as ('RWRD'
// in its training data) moves the learning info on the fly, and with
// orthographic learning on the pen's trace trains the letter shapes
// ('ORTL').
// NOT YET RECONSTRUCTED: ORTraining (0x00147d70), which trains the
// reading engine's trajectories.
long
XRWDoLearning(ULong recordAddr, XRWORDPARAM* param)
{
	XrLearningRecord* record = (XrLearningRecord*) recordAddr;
	if (record == nil || param == nil || record->fData == nil)
		return -1;
	void* data = LHLock(record->fData);
	if (data == nil)
		return -1;
	long result = -1;
	void* words;
	ULong size;
	RcHandlesType saved;
	if (LHFindEntry(data, 'RWRD', '0001', 0, &words, &size) == 0 && words != nil
	&&  size >= (ULong) (record->fIndex * 0x50 + 0x50)
	&&  GCLockDTEAndLearningData(param, &saved) != 0)
	{
		const rec_w_type* word = (const rec_w_type*) ((UByte*) words + record->fIndex * 0x50);
		if ((short) RCGetH(param, 0x22) != 0 && GetLearnInfoPtr((DTIHeader*) param->fDTI) != nil)
		{
			long learnt = FlyLearn(param, word);
			if (TracingCursive())
				fprintf(stderr, "[cursive] learning: \"%s\" (%ld points of trace) learnt on the fly: %s\n",
						(const char*) word->fWord, record->fCount, learnt == 0 ? "the letter counters moved" : "nothing to learn");
		}
		else if (TracingCursive())
			fprintf(stderr, "[cursive] learning: \"%s\" not learnt (learning %s, %s learning info)\n", (const char*) word->fWord,
					(short) RCGetH(param, 0x22) != 0 ? "on" : "off", GetLearnInfoPtr((DTIHeader*) param->fDTI) != nil ? "with" : "no");
		if ((RCGetH(param, 0xb8) & 8) != 0)
		{
			void* ortl;
			if (LHFindEntry(data, 'ORTL', '0001', 0, &ortl, nil) == 0
			&&  ortl != nil && record->fTrace != nil && record->fCount > 2)
			{
				// NOT YET: ORTraining(param->fOrtho, record->fTrace, word, ortl)
			}
		}
		result = 0;
		GCUnlockDTEAndLearningData(param, &saved);
	}
	LHUnLock(record->fData);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   d o m a i n s
------------------------------------------------------------------------------*/

// ROM 0x0021fc7c Make__12TStrXrDomainSFP11TController
TStrXrDomain*
TStrXrDomain::Make(TController* controller)
{
	TStrXrDomain* domain = new TStrXrDomain;
	domain->IStrXrDomain(controller);
	return domain;
}


// ROM 0x0021fcc4 IStrXrDomain__12TStrXrDomainFP11TController
void
TStrXrDomain::IStrXrDomain(TController* controller)
{
	IDomain(controller, kStrXrDomainType, (char*) "Strokes to Xrs Domain");
	fDelay = gRecognitionTimeout;
	AddPieceType('STRK');
	controller->RegisterDomain(this);
	memset(fGeom, 0, sizeof(fGeom));
	memset(fPrevBase, 0, sizeof(fPrevBase));
	fGrid[0] = fGrid[1] = fGrid[2] = 0;			// (the ROM clears 0x0c bytes: not the fourth)
}


// ROM 0x0024dff4 Make__13TXrWordDomainSFP11TController
TXrWordDomain*
TXrWordDomain::Make(TController* controller)
{
	TXrWordDomain* domain = new TXrWordDomain;
	domain->IXrWordDomain(controller);
	return domain;
}


// ROM 0x0024e03c IXrWordDomain__13TXrWordDomainFP11TController
void
TXrWordDomain::IXrWordDomain(TController* controller)
{
	IDomain(controller, kXrWordDomainType, (char*) "Xrs to Word Domain");
	fDelay = 0;
	AddPieceType(kStrXrDomainType);
	controller->RegisterDomain(this);
}


// ROM 0x0024ea2c Dispose__13TXrWordDomainFv
void
TXrWordDomain::Dispose(void)
{}


// ROM 0x0024eb28 Group__13TXrWordDomainFP5TUnitP8dInfoRec
// An STXR unit (a word the cursive reader has read, 0x8000000 while it
// has not been) made the sub of a new word unit; one that is no use makes
// a word unit that is none either.  ==> 1.
long
TXrWordDomain::Group(TUnit* unit, dInfoRec* /*info*/)
{
	if (!unit->TestFlags(0x08000000))
	{
		TArray* areas = unit->GetAreas();
		TXrWordUnit* word = TXrWordUnit::Make(this, unit->fKind + 1, areas);
		if (areas != nil)
			areas->Dispose();
		if (word == nil)
			fController->SignalMemoryError();
		else
		{
			word->AddSub(unit);
			if (unit->TestFlags(kInvalidUnit))
			{
				word->SetFlags(kInvalidUnit);
				word->EndSubs();
			}
			fController->NewGroup(word);
		}
	}
	return 1;
}


// ROM 0x0024ea30 Classify__13TXrWordDomainFP5TUnit
// The word unit given the readings of its STXR unit (a unit with none
// is marked no use and closed), and handed to the controller as a piece
// for the recognisers above - the ROM has NewClassification inlined here.
void
TXrWordDomain::Classify(TUnit* unit)
{
	if (!unit->TestFlags(kInvalidUnit))
	{
		SetUpChains(this, unit);
		ClassifyXrWord((TXrWordUnit*) unit);
		if (unit->InterpretationCount() == 0)
		{
			unit->SetFlags(kInvalidUnit);
			((TSIUnit*) unit)->EndSubs();
		}
	}
	fController->NewClassification(unit);
}


// ROM 0x0024eab4 Reclassify__13TXrWordDomainFP5TUnit
// The readings thrown away, last first, with the training data, and
// taken again from the STXR unit (which has read the ink again).
void
TXrWordDomain::Reclassify(TUnit* unit)
{
	SetUpChains(this, unit);
	for (long i = unit->InterpretationCount() - 1; i >= 0; i--)
		((TSIUnit*) unit)->DeleteInterpretation((ULong) i);
	TXrWordUnit* word = (TXrWordUnit*) unit;
	if (word->fLearning != nil)
	{
		HWRMemoryFreeHandle(word->fLearning);
		word->fLearning = nil;
	}
	TakeReadings(word);
}


// ROM 0x0024e09c ClassifyXrWord__13TXrWordDomainFP11TXrWordUnit
void
TXrWordDomain::ClassifyXrWord(TXrWordUnit* unit)
{
	TakeReadings(unit);
}


// ClassifyXrWord's body, which the ROM writes out a second time in
// Reclassify: each reading of the STXR unit (while it has a score) an
// interpretation - its word, a score of ten times how far below 100 it
// is (so nought is best, as the arbiter wants), and its dictionary
// attribute as the label (-4 for none) - the ink's box and base line
// taken over, and the training data (the readings, and by the domain's
// flags the trace too).
void
TXrWordDomain::TakeReadings(TXrWordUnit* unit)
{
	PS_point_type* trace = nil;
	short points = 0;
	TStrXrUnit* xr = (TStrXrUnit*) unit->GetSub(0);
	if (xr == nil || xr->fActive != 0 || xr->fWords == nil)
		return;
	rec_w_type* word = xr->fWords;
	long count = xr->fWordCount;
	for (long i = 0; i < count; i++, word++)
	{
		long weight = word->fWord[0] != 0 ? word->fWeight : 0;
		if (word->fWord[0] == 0 || weight == 0 || unit->AddWordInterpretation() == -1)
			break;
		// (the index AddWordInterpretation answered is not used: the
		// readings are taken in order, so it is i)
		unit->SetCharWordString(i, (char*) word->fWord);
		long score = 100 - word->fWeight;
		if (score < 0)
			score = -score;
		unit->SetScore(i, score * 10);
		long label = (short) ((word->fX4A[0] << 8) | word->fX4A[1]);
		if (label < 0)
			label = -4;
		unit->SetLabel(i, label);
	}
	unit->fLeft = xr->fLeft;
	unit->fRight = xr->fRight;
	unit->fBase = xr->fBase;
	unit->fBase2 = xr->fBase2;
	unit->fHeight = xr->fHeight;
	unit->fHeight2 = xr->fHeight2;
	unit->fSlant = xr->fField58;
	unit->fField58 = xr->fField5C;
	unit->fField5C = xr->fField60;
	unit->fLearning = xr->fLearning;
	xr->fLearning = nil;
	UShort flags = RCGetH(&fRC, 0xb2);
	if ((flags & 2) != 0)
	{
		GetTraceFromStrXrUnit(xr, &trace, &points);
		if (xr->fMerged != 0 && trace != nil)
			GCMergeLinesAndRemoveDash(trace, &points, xr->fJoinX, xr->fJoinY, xr->fMerged, 0);
	}
	GCFillLearningHandle(&unit->fLearning, flags, nil, trace, points, nil, nil, xr->fWords, xr->fWordCount, nil, 0);
	if (trace != nil)
		HWRMemoryFree((Ptr) trace);
}


// ROM 0x000651cc GetTraceFromStrXrUnit__FP10TStrXrUnitPP13PS_point_typePs
// The unit's strokes (its subs' strokes) put together into one trace.
void
GetTraceFromStrXrUnit(TStrXrUnit* unit, PS_point_type** trace, short* nPoints)
{
	TStroke* local[20];
	*trace = nil;
	ULong count = unit->SubCount();
	TStroke** strokes = local;
	// DEVIATION: the list is of host pointers, so a long one is sized by
	// them
	if (0x13 < count)
		strokes = (TStroke**) HWRMemoryAlloc((count + 1) * sizeof(TStroke*));
	if (strokes == nil)
		return;
	ULong i;
	for (i = 0; i < count; i++)
		strokes[i] = ((TStrokeUnit*) unit->GetSub(i))->fStroke;
	strokes[i] = nil;
	short nStrokes;
	NewGetTraceFromStrokes(strokes, trace, &nStrokes, nPoints);
	if (0x13 < count)
		HWRMemoryFree((Ptr) strokes);
}


#pragma mark - the word unit

// ROM 0x0024fb04 Make__11TXrWordUnitSFP7TDomainUlP6TArray
TXrWordUnit*
TXrWordUnit::Make(TDomain* domain, ULong kind, TArray* areas)
{
	TXrWordUnit* unit = new TXrWordUnit;
	if (unit == nil)
		return nil;
	if (unit->IXrWordUnit(domain, kind, areas) != 0)
	{
		unit->Dispose();
		return nil;
	}
	return unit;
}


// ROM 0x0024fb7c IXrWordUnit__11TXrWordUnitFP7TDomainUlP6TArray
// A word unit of sixteen-byte interpretations, with no training data.
long
TXrWordUnit::IXrWordUnit(TDomain* domain, ULong kind, TArray* areas)
{
	// DEVIATION: the ROM's interpretations are 0x10 bytes; the host's hold
	// pointers, so they are sized by sizeof
	long err = IStdWordUnit(domain, kind, areas, sizeof(UnitInterpretation));
	fLearning = nil;
	return err;
}


// ROM 0x0024fbb0 IDispose__11TXrWordUnitFv
void
TXrWordUnit::IDispose(void)
{
	if (fLearning != nil)
	{
		HWRMemoryFreeHandle(fLearning);
		fLearning = nil;
	}
	TStdWordUnit::IDispose();
}


static inline Fixed	TabletToFixed(long n)	{ return (Fixed) (int32_t) ((uint32_t) n << 16); }

// ROM 0x0024fbe4 GetWordBase__11TXrWordUnitFP6FPointT1Ul
void
TXrWordUnit::GetWordBase(FPoint* left, FPoint* right, ULong /*index*/)
{
	left->y = FixedDivide(TabletToFixed(fBase), gTabScale.y);
	left->x = FixedDivide(TabletToFixed(fLeft), gTabScale.x);
	right->y = FixedDivide(TabletToFixed(fBase2), gTabScale.y);
	right->x = FixedDivide(TabletToFixed(fRight), gTabScale.x);
}


// ROM 0x0024fc58 GetWordSlant__11TXrWordUnitFUl
long
TXrWordUnit::GetWordSlant(ULong /*index*/)
{
	return TabletToFixed(fSlant);
}


// ROM 0x0024fc64 GetWordSize__11TXrWordUnitFUl
long
TXrWordUnit::GetWordSize(ULong /*index*/)
{
	Fixed base = FixedDivide(TabletToFixed(fBase), gTabScale.y);
	Fixed base2 = FixedDivide(TabletToFixed(fBase2), gTabScale.y);
	Fixed height = FixedDivide(TabletToFixed(fHeight), gTabScale.y);
	Fixed height2 = FixedDivide(TabletToFixed(fHeight2), gTabScale.y);
	return ((base - height) + (base2 - height2)) >> 1;
}


// ROM 0x0024fcdc GetTrainingData__11TXrWordUnitFl
Handle
TXrWordUnit::GetTrainingData(long /*index*/)
{
	if (fLearning == nil)
		return nil;
	Size size = GetHandleSize(fLearning);
	Handle copy = NewHandle(size);
	if (copy != nil)
	{
		// (the ROM's LockHandle/UnlockHandle, which are HLock/HUnlock)
		HLock(fLearning);
		HLock(copy);
		BlockMove(*fLearning, *copy, size);
		HUnlock(fLearning);
		HUnlock(copy);
	}
	return copy;
}


// ROM 0x0024fd48 DisposeTrainingData__11TXrWordUnitFPPc
void
TXrWordUnit::DisposeTrainingData(Handle data)
{
	if (data != nil)
		DisposeHandle(data);
}


// ROM 0x0024f84c InitializeParamStruct__13TXrWordDomainFP11XRWORDPARAM
// The engine's defaults, the letter table and vocabularies loaded, and
// the current letter set's learning info.  ==> 0, or -1 (everything that
// had been loaded let go - bar the trigram header, which the ROM leaves
// behind).
long
TXrWordDomain::InitializeParamStruct(XRWORDPARAM* param)
{
	rc_type rc;
	memset(&rc, 0, sizeof(rc));
	RCSetH(&rc, 0x06, 1);
	switch (gLetterSetSelection)
	{
	case 1:	RCSetH(&rc, 0x04, 8); break;
	case 2:	RCSetH(&rc, 0x04, 4); break;
	case 3:
		RCSetH(&rc, 0x04, 4);
		// ROM BUG: the block's own field is set, not the defaults' - and
		// the defaults are copied over the block below, so this is lost.
		RCSetH(param, 0x90, 0x0800);
		break;
	case 4:	RCSetH(&rc, 0x04, 1); break;
	default: RCSetH(&rc, 0x04, 2); break;
	}
	RCSetH(&rc, 0x10, 0x14);
	RCSetH(&rc, 0x14, 0x3c);
	RCSetH(&rc, 0x18, 0x55);
	RCSetH(&rc, 0x16, 0x0f);
	RCSetH(&rc, 0x1a, 0x1e);
	RCSetH(&rc, 0x1c, 0x0c);
	RCSetH(&rc, 0x0c, 0);
	RCSetH(&rc, 0x0e, 0);
	RCSetH(&rc, 0x22, 1);
	RCSetH(&rc, 0x24, 1);
	RCSetH(&rc, 0x1e, 0x37);
	RCSetH(&rc, 0x20, 1);
	RCSetH(&rc, 0x26, 0x5c);
	RCSetH(&rc, 0x28, 0x5c);
	RCSetH(&rc, 0x2a, 2);
	RCSetH(&rc, 0x2c, 5);
	rc.fNumCharset = (const char*) num_charset;
	rc.fMathCharset = (const char*) math_charset;
	rc.fLPunctCharset = (const char*) lpunct_charset;
	rc.fEPunctCharset = (const char*) epunct_charset;
	rc.fOtherCharset = (const char*) other_charset;
	RCSetH(&rc, 0x00, 2);
	RCSetH(&rc, 0x02, 0x3a);
	RCSetH(&rc, 0x08, 3);
	RCSetH(&rc, 0xb4, 0);
	RCSetH(&rc, 0xb2, 0x20);
	RCSetH(&rc, 0xb8, 4);
	RCSetH(&rc, 0x100, 0x3c);
	RCSetH(&rc, 0x102, 10);
	GCSetUpRecTableAndCharset(&rc, 1);
	if (LoadVocAndData("NEWTON0.LST", "NEWTON1.LST", "avp.dte", rc.fVocs, (Handle*) &rc.fDTI, &rc.fTrigrams,
					   &param->fChain0, &param->fChain1) == 0
	&&  AllocLearnInfo((Handle*) &rc.fDTI, gLetterSetSelection) == 0)
	{
		if (GetDTELearnInfoHandle((Handle) rc.fDTI) == nil)
			RCSetH(&rc, 0x24, 0);
		XRWByte(param, 0x10c)[0] = 1;
		*(rc_type*) param = rc;
		return 0;
	}
	for (long i = 0; i < 15; i++)
		if (rc.fVocs[i] != nil)
			UnloadVoc(&rc.fVocs[i]);
	if (rc.fDTI != nil)
		UnloadData((Handle*) &rc.fDTI);
	return -1;
}


// ROM 0x0024ebfc DomainParameter__13TXrWordDomainFUlN21
// DEVIATION: selector 0 answers the host's block size, which is larger
// than the ROM's 0x160 because of its pointers; and the learning info's
// size (0x20021) and the orthographic database's (0x20039) count the
// host's handle word rather than four bytes.
long
TXrWordDomain::DomainParameter(ULong selector, ULong result, ULong info)
{
	XRWORDPARAM* param = nil;
	if (info != 0)
	{
		HLock((Handle) info);
		param = *(XRWORDPARAM**) info;
	}
	// (the ROM leaves r8 as it was for the selectors that set nothing -
	//  0x2000b-0x2000f, 0x20013, 0x20025-0x20028, 0x20042 and anything
	//  it does not know - so what it answers for them is whatever the
	//  caller had in that register; no caller looks)
	long err = 0;
	RcHandlesType saved;
	switch (selector)
	{
	case 0:
		*(ULong*) result = sizeof(XRWORDPARAM);
		break;
	case 1:
		err = InitializeParamStruct(param);
		XRWSetW(param, 0x114, 0);
		param->fRemovedDict = nil;
		XRWByte(param, 0x15e)[0] = 0;
		XRWByte(param, 0x15f)[0] = 1;
		XRWSetW(param, 0x110, 1);
		SetXrWordFieldType(1, param);
		SetXrWordFieldSpeed(XRWGetW(param, 0x114), param, XRWGetW(param, 0x110));
		break;
	case 2:
		switch (result)
		{
		case 0: case 1: case 2: case 3:
		case 0x20001: case 0x20002: case 0x20003: case 0x20004: case 0x20005: case 0x20006: case 0x20007:
		case 0x20008: case 0x20009: case 0x2000a: case 0x20010: case 0x20011: case 0x20012: case 0x20014:
		case 0x20015: case 0x20016: case 0x20017: case 0x20019: case 0x20020:
		case 0x20021: case 0x20022: case 0x20023: case 0x20024: case 0x20029:
		case 0x20030: case 0x20031: case 0x20032: case 0x20033: case 0x20034: case 0x20035: case 0x20036:
		case 0x20037: case 0x20038: case 0x20039: case 0x20040: case 0x20041:
		case 0x20043: case 0x20044: case 0x20045: case 0x20046: case 0x20047:
			err = 0;
			break;
		default:
			// (0x20018 among them, though the domain answers it)
			err = -1;
			break;
		}
		break;
	case 3:
		for (long i = 0; i < 15; i++)
			if (param->fVocs[i] != nil)
				UnloadVoc(&param->fVocs[i]);
		if (param->fDTI != nil)
			UnloadData((Handle*) &param->fDTI);
		if (param->fTrigrams != nil)
			UnloadTrigram(&param->fTrigrams);
		break;
	case 0x20001:
		((void**) result)[0] = param->fChain0;
		((void**) result)[1] = param->fChain1;
		break;
	case 0x20002:
		param->fChain0 = ((void**) result)[0];
		param->fChain1 = ((void**) result)[1];
		SetUpVocAdders(param->fChain0, param->fChain1, &param->fVocs[0], "Ernie's voc", 0);
		if (param->fChain0 == nil && param->fChain1 == nil)
			RCSetH(param, 0x08, RCGetH(param, 0x08) & ~1);
		break;
	case 0x20003:
	case 0x20004:
		break;
	case 0x20005:
		*(ULong*) result = XRWGetW(param, 0x110);
		PrintFieldType(XRWGetW(param, 0x110));
		break;
	case 0x20006:
		err = SetXrWordFieldType(result, param);
		if (err == 0)
		{
			XRWSetW(param, 0x110, result);
			err = SetXrWordFieldSpeed(XRWGetW(param, 0x114), param, result);
		}
		break;
	case 0x20007:
		*(ULong*) result = XRWGetW(param, 0x114);
		break;
	case 0x20008:
		err = SetXrWordFieldSpeed(result, param, XRWGetW(param, 0x110));
		if (err == 0)
			XRWSetW(param, 0x114, result);
		break;
	case 0x20009:
		if (param->fRemovedDict != nil)
		{
			((TDictChain*) param->fChain0)->AddDictToChain(param->fRemovedDict);
			param->fRemovedDict = nil;
		}
		break;
	case 0x2000a:
		if (param->fRemovedDict == nil)
		{
			UShort position = result & 0xffff;
			param->fRemovedDict = ((TDictChain*) param->fChain0)->PositionToHandle(position);
			XRWByte(param, 0x125)[0] = position;
			XRWByte(param, 0x124)[0] = position >> 8;
			((TDictChain*) param->fChain0)->RemoveDictFromChain(param->fRemovedDict);
		}
		break;
	case 0x20010:
		err = XRWDoLearning(result, param);
		break;
	case 0x20011:
		*(XrChainInfo*) result = param->fChainInfo;
		break;
	case 0x20012:
		param->fChainInfo = *(XrChainInfo*) result;
		param->fChain = param->fChainInfo.fChain;
		if (param->fChain != nil)
			RCSetH(param, 0x08, RCGetH(param, 0x08) | 8);
		else
			RCSetH(param, 0x08, RCGetH(param, 0x08) & ~8);
		break;
	case 0x20014:
		if (HWRStrLen((const char*) result) == 0)
			err = -1;
		break;
	case 0x20015:
		if (GCLockDTEAndLearningData(param, &saved) == 0)
		{
			err = -1;
			break;
		}
		if (GetLearnInfoPtr((DTIHeader*) param->fDTI) != nil && SetDefaultsWeights((DTIHeader*) param->fDTI) == -1)
			err = -1;
		ORInitDB(param->fOrtho, ORGetDBSize());
		GCUnlockDTEAndLearningData(param, &saved);
		break;
	case 0x20016:
		{
			UByte* request = (UByte*) result;
			int vex;
			switch (request[2])
			{
			case 0:	vex = 0; break;
			case 1:	vex = 3; break;
			case 2:	vex = 7; break;
			default: err = -1; goto done;
			}
			if (GCLockDTEAndLearningData(param, &saved) == 0)
			{
				err = -1;
				break;
			}
			if (GetLearnInfoPtr((DTIHeader*) param->fDTI) == nil
			||  SetVariantState(request[0], request[1], vex, RCByte(param, 0x05)[0], (DTIHeader*) param->fDTI) == -1)
				err = -1;
			GCUnlockDTEAndLearningData(param, &saved);
		}
		break;
	case 0x20017:
		{
			UByte* request = (UByte*) result;
			if (GCLockDTEAndLearningData(param, &saved) == 0)
			{
				err = -1;
				break;
			}
			long state;
			if (GetLearnInfoPtr((DTIHeader*) param->fDTI) == nil
			||  (state = GetVariantState(request[0], request[1], RCByte(param, 0x05)[0], (DTIHeader*) param->fDTI)) == -1)
				err = -1;
			else
			{
				if (state >= 0 && state < 3)
					request[2] = 0;
				if (state >= 3 && state < 7)
					request[2] = 1;
				if (state >= 7)
					request[2] = 2;
			}
			GCUnlockDTEAndLearningData(param, &saved);
		}
		break;
	case 0x20018:
		{
			UByte* s = (UByte*) result;
			if (GCLockDTEAndLearningData(param, &saved) == 0)
			{
				err = -1;
				break;
			}
			for (long i = 0; s[i] != 0; i++)
			{
				if (GetNumVarsOfChar(s[i], (DTIHeader*) param->fDTI) == 0)
				{
					err = -1;
					break;
				}
			}
			GCUnlockDTEAndLearningData(param, &saved);
		}
		break;
	case 0x20019:
		XRWByte(param, 0x10c)[0] = 1;
		break;
	case 0x20020:
		XRWByte(param, 0x10c)[0] = 0;
		break;
	case 0x20021:
		{
			long size;
			if (GetDTELearnInfoHandle((Handle) param->fDTI) != nil
			&&  (size = GetDTELearnInfoSize(param->fDTI)) > 0)
				*(ULong*) result = size + kHWRHeader;
			else
			{
				err = -1;
				*(ULong*) result = 0;
			}
		}
		break;
	case 0x20022:
		{
			Handle h = GetDTELearnInfoHandle((Handle) param->fDTI);
			*(Handle*) result = h;
			if (h == nil)
				err = -1;
		}
		break;
	case 0x20023:
		RCSetH(param, 0x24, 0);
		break;
	case 0x20024:
		if (GetDTELearnInfoHandle((Handle) param->fDTI) != nil)
			RCSetH(param, 0x24, 1);
		else
			err = -1;
		break;
	case 0x20029:
		{
			char** sets = (char**) result;
			if (sets[0] == nil || sets[1] == nil
			||  HWRStrLen(sets[0]) > 0x14 || HWRStrLen(sets[1]) > 0x14)
			{
				err = -1;
				break;
			}
			char* lpunct = (char*) XRWByte(param, 0x134);
			char* epunct = (char*) XRWByte(param, 0x149);
			HWRStrCpy(lpunct, sets[0]);
			HWRStrCpy(epunct, sets[1]);
			param->fLPunctCharset = lpunct;
			param->fEPunctCharset = epunct;
		}
		break;
	case 0x20030:
		{
			char** sets = (char**) result;
			if (sets[0] == nil || sets[1] == nil)
			{
				err = -1;
				break;
			}
			HWRStrCpy(sets[0], param->fLPunctCharset);
			HWRStrCpy(sets[1], param->fEPunctCharset);
		}
		break;
	case 0x20031:
		param->fLPunctCharset = (const char*) lpunct_charset;
		param->fEPunctCharset = (const char*) epunct_charset;
		break;
	case 0x20032:
		SetXrWordRC(result, param);
		break;
	case 0x20033:
		RCSetH(param, 0x22, 1);
		break;
	case 0x20034:
		RCSetH(param, 0x22, 0);
		break;
	case 0x20035:
		param->fOrtho = AllocOrtographLearnInfo();
		if (param->fOrtho != nil)
			RCSetH(param, 0xb8, RCGetH(param, 0xb8) | 3);
		else
			err = -1;
		break;
	case 0x20036:
		RCSetH(param, 0xb8, RCGetH(param, 0xb8) & ~3);
		break;
	case 0x20037:
		param->fOrtho = AllocOrtographLearnInfo();
		if (param->fOrtho == nil)
		{
			err = -1;
			break;
		}
		RCSetH(param, 0xb8, RCGetH(param, 0xb8) | 8);
		RCSetH(param, 0xb2, RCGetH(param, 0xb2) | 0x40);
		break;
	case 0x20038:
		RCSetH(param, 0xb8, RCGetH(param, 0xb8) & ~8);
		RCSetH(param, 0xb2, RCGetH(param, 0xb2) & ~0x40);
		break;
	case 0x20039:
		if (param->fOrtho == nil)
		{
			*(ULong*) result = 0;
			err = -1;
		}
		else
			*(ULong*) result = ORGetDBSize() + kHWRHeader;
		break;
	case 0x20040:
		err = (param->fOrtho != nil) ? 0 : -1;
		*(void**) result = param->fOrtho;
		break;
	case 0x20041:
		if (AllocLearnInfo((Handle*) &param->fDTI, result) != 0)
		{
			err = -1;
			break;
		}
		RCSetH(param, 0x90, 0);
		switch (result)
		{
		case 1:	RCSetH(param, 0x04, 8); break;
		case 2:	RCSetH(param, 0x04, 4); break;
		case 3:
			RCSetH(param, 0x04, 4);
			RCSetH(param, 0x90, 0x0800);
			break;
		case 4:	RCSetH(param, 0x04, 1); break;
		default: RCSetH(param, 0x04, 2); break;
		}
		err = SetXrWordFieldType(XRWGetW(param, 0x110), param);
		if (err == 0)
			err = SetXrWordFieldSpeed(XRWGetW(param, 0x114), param, XRWGetW(param, 0x110));
		if (GetDTELearnInfoHandle((Handle) param->fDTI) == nil)
			RCSetH(param, 0x24, 0);
		break;
	case 0x20043:
	case 0x20044:
	case 0x20045:
		break;
	case 0x20046:
		RCSetH(param, 0x06, 0);
		if ((result & 1) != 0)
			RCSetH(param, 0x06, RCGetH(param, 0x06) | 1);
		if ((result & 8) != 0)
			RCSetH(param, 0x06, RCGetH(param, 0x06) | 8);
		GCSetUpRecTableAndCharset(param, 1);
		err = SetXrWordFieldType(XRWGetW(param, 0x110), param);
		if (err == 0)
			err = SetXrWordFieldSpeed(XRWGetW(param, 0x114), param, XRWGetW(param, 0x110));
		if (GetDTELearnInfoHandle((Handle) param->fDTI) == nil)
			RCSetH(param, 0x24, 0);
		break;
	case 0x20047:
		*(ULong*) result = 0;
		if ((RCGetH(param, 0x06) & 1) != 0)
			*(ULong*) result = 1;
		if ((RCGetH(param, 0x06) & 8) != 0)
			*(ULong*) result |= 8;
		break;
	default:
		break;
	}
done:
	if (info != 0)
		HUnlock((Handle) info);
	return err;
}


// ROM 0x0024f6f4 ConfigureSubDomain__13TXrWordDomainFP8TRecArea
// The strokes-to-xrs domain told the field type the word domain has for
// the area.
void
TXrWordDomain::ConfigureSubDomain(TRecArea* area)
{
	TDomain* strXr = fController->GetTypedDomain(kStrXrDomainType);
	Handle wordInfo = area->GetInfoFor(kXrWordDomainType, true);
	Handle strXrInfo = area->GetInfoFor(kStrXrDomainType, true);
	ULong type;
	DomainParameter(0x20005, (ULong) &type, (ULong) wordInfo);
	strXr->DomainParameter(0x20006, type, (ULong) strXrInfo);
	area->ParamsAllSet(kStrXrDomainType);
}


// ROM 0x0024f79c SetParameters__13TXrWordDomainFPPc
// The area's block copied into the domain for reading with.  (Unlike the
// base, it does not remember the handle, and answers that nothing
// changed.)
Boolean
TXrWordDomain::SetParameters(Handle params)
{
	XRWORDPARAM* p = (XRWORDPARAM*) *params;
	fRC = *(rc_type*) p;
	f134 = XRWByte(p, 0x10c)[0];
	fFieldType = XRWGetW(p, 0x110);
	fSpeed = XRWGetW(p, 0x114);
	fChain0 = p->fChain0;
	fChain1 = p->fChain1;
	fRemovedDict = p->fRemovedDict;
	fRemovedPosition = (XRWByte(p, 0x124)[0] << 8) | XRWByte(p, 0x124)[1];
	fChainInfo = p->fChainInfo;
	f15C = XRWByte(p, 0x15e)[0];
	f15D = XRWByte(p, 0x15f)[0];
	GCSetUpRecTableAndCharset(&fRC, 0);
	return false;
}
