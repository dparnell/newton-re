/*
	File:		recognition/ParaGraph.cpp

	Contains:	The cursive recogniser's data: its memory, character
				classes, letter table and learning infos.  See ParaGraph.h.
*/

#include "ParaGraph.h"
#include "WRecDomain.h"			// gRecMemErrCount
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NativeFunctions.h"
#include "ByteOrder.h"
#include <string.h>

extern const unsigned char	alpha_charset_eng[28];
extern const unsigned char	alpha_charset_swe[32];
extern const unsigned char	alpha_charset_swe_nodiacr[32];
extern const unsigned char	DefaultVexMapTable[768];
extern const unsigned char	BlockVexMapTable[768];
extern const unsigned char	PalmerVexMapTable[768];
extern const unsigned char	os_rec_ctbl_intl[128];
extern const unsigned char	os_rec_ctbl_eng[128];
extern const unsigned char	_xctype[256];
extern const unsigned char	_xupper[128];
extern const unsigned char	_xlower[128];

// ROM 0x0c100bcc p_os_rec_ctbl / 0x0c100bd0 alpha_charset
// (initialised in the ROM's RW area to the international table and the
// English letters - which SetOsRecTableAndCharSet itself never pairs)
const UByte*	p_os_rec_ctbl = os_rec_ctbl_intl;
const char*		alpha_charset = (const char*) alpha_charset_eng;

// ROM 0x0c105474 (unnamed)
ParaRamData		gParaRamData;


/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   m e m o r y
------------------------------------------------------------------------------*/

// ROM 0x000e63a8 HWRMemoryAllocHandle__FUl
Handle
HWRMemoryAllocHandle(ULong size)
{
	if (size <= 0x493e0)
	{
		Handle h = NewHandle(size + kHWRHeader);
		if (h != nil)
		{
			NameHandle(h, 'para');
			return h;
		}
		gRecMemErrCount++;
	}
	return nil;
}


// ROM 0x000e640c HWRMemoryLockHandle__FUl
Ptr
HWRMemoryLockHandle(Handle h)
{
	HLock(h);
	Ptr block = *h;
	if (block == nil)
		return nil;
	*(Handle*) block = h;
	return block + kHWRHeader;
}


// ROM 0x000e6434 HWRMemoryUnlockHandle__FUl
long
HWRMemoryUnlockHandle(Handle h)
{
	HUnlock(h);
	return 1;
}


// ROM 0x000e644c HWRMemoryFreeHandle__FUl
long
HWRMemoryFreeHandle(Handle h)
{
	DisposHandle(h);
	return 1;
}


// ROM 0x000e6464 HWRMemoryAlloc__FUl
Ptr
HWRMemoryAlloc(ULong size)
{
	if (size <= 0x493e0)
	{
		Handle h = NewHandle(size + kHWRHeader);
		if (h == nil)
			gRecMemErrCount++;
		else
		{
			NameHandle(h, 'para');
			HLock(h);
			Ptr block = *h;
			if (block != nil)
			{
				*(Handle*) block = h;
				return block + kHWRHeader;
			}
			DisposHandle(h);
		}
	}
	return nil;
}


// ROM 0x000e64e0 HWRMemoryFree__FPv
long
HWRMemoryFree(Ptr block)
{
	DisposHandle(*(Handle*) (block - kHWRHeader));
	return 1;
}


// ROM 0x000e6514 HWRStrLen__FPc
long
HWRStrLen(const char* s)
{
	const char* p = s;
	while (*p++ != 0)
		;
	return p - (s + 1);
}


// ROM 0x000e6560 HWRStrCpy__FPcT1
char*
HWRStrCpy(char* dest, const char* src)
{
	char* p = dest;
	while ((*p++ = *src++) != 0)
		;
	return dest;
}


// ROM 0x000e657c HWRStrCat__FPcT1
void
HWRStrCat(char* dest, const char* src)
{
	char* p = dest;
	while (*p != 0)
		p++;
	while ((*p++ = *src++) != 0)
		;
}


// ROM 0x000e65e8 HWRStrrChr__FPci
char*
HWRStrrChr(char* s, int c)
{
	char* found = nil;
	if (*s != 0)
	{
		do
		{
			if ((UByte) *s == (c & 0xff))
				found = s;
			s++;
		} while (*s != 0);
	}
	return found;
}


/*------------------------------------------------------------------------------
	T h e   e n g i n e ' s   c h a r a c t e r   c l a s s e s
------------------------------------------------------------------------------*/

// ROM 0x00283e54 IsUpper
int
IsUpper(int c)
{
	c &= 0xff;
	return (_xctype[c] & 0x01) != 0;
}


// ROM 0x00283e84 IsLower
int
IsLower(int c)
{
	c &= 0xff;
	return (_xctype[c] & 0x02) != 0;
}


// ROM 0x00283f20 IsAlpha
int
IsAlpha(int c)
{
	c &= 0xff;
	return IsUpper(c) || IsLower(c);
}


// ROM 0x00283f58 ToUpper
int
ToUpper(int c)
{
	c &= 0xff;
	if (IsAlpha(c))
	{
		if (c > 0x7f)
			return _xupper[c & 0x7f];
		if (c >= 'a' && c <= 'z')
			return c - 0x20;
	}
	return c;
}


// ROM 0x00283fb0 ToLower
int
ToLower(int c)
{
	c &= 0xff;
	if (IsAlpha(c))
	{
		if (c > 0x7f)
			return _xlower[c & 0x7f];
		if (c >= 'A' && c <= 'Z')
			return c + 0x20;
	}
	return c;
}


// ROM 0x00087c84 SetOsRecTableAndCharSet__Fi
void
SetOsRecTableAndCharSet(int which)
{
	if (which == 0)
	{
		p_os_rec_ctbl = os_rec_ctbl_eng;
		alpha_charset = (const char*) alpha_charset_eng;
		return;
	}
	p_os_rec_ctbl = os_rec_ctbl_intl;
	if (which == 1)
		alpha_charset = (const char*) alpha_charset_swe;
	else if (which == 2)
		alpha_charset = (const char*) alpha_charset_swe_nodiacr;
	else
		alpha_charset = (const char*) alpha_charset_eng;
}


// ROM 0x00087d7c OSToRec__Fi
UByte
OSToRec(int c)
{
	c &= 0xff;
	if (c < 0x7f)
		return c;
	for (long i = 0; i < 0x3f; i++)
		if (p_os_rec_ctbl[i * 2] == c)
			return p_os_rec_ctbl[i * 2 + 1];
	return 0;
}


/*------------------------------------------------------------------------------
	T h e   l e t t e r   t a b l e
------------------------------------------------------------------------------*/

// ROM 0x002fe68c ReadDteResource__FPcsPPvT3Ui
// DEVIATION: the ROM's handle is the resource's size and is used as the
// DTE header where it lies; the host's is a DTIHeader after the handle
// word, with the resource's bytes 4-0x8f copied into it (the first four
// are what the handle word overwrites) and the pointers started at
// nought rather than at the stale words the resource ships.
long
ReadDteResource(const char* /*name*/, short /*which*/, Handle* dti, Handle* trigrams, ULong makeTrigrams)
{
	*trigrams = nil;
	UByte* header = (UByte*) BinaryData(FGetDTEHeader(RefVar()));
	UByte* dteMain = (UByte*) BinaryData(FGetDTEMain(RefVar()));
	UByte* trigramData = (UByte*) BinaryData(FGetDTETrigram(RefVar()));
	Handle h = NewHandle(kHWRHeader + sizeof(DTIHeader));
	if (h != nil)
	{
		NameHandle(h, 'DTEH');
		HLock(h);
		memset(*h, 0, kHWRHeader + sizeof(DTIHeader));
		memcpy(((DTIHeader*) (*h + kHWRHeader))->fHeader, header + 4, sizeof(((DTIHeader*) nil)->fHeader));
		HUnlock(h);
		trigramData += 4;
		DTIHeader* locked = (DTIHeader*) HWRMemoryLockHandle(h);
		UByte* ppdMain = (UByte*) BinaryData(FGetPPDMain(RefVar()));
		if (!SetUpDteAddres(&locked, nil, gParaRamData.fDTEMain, nil, nil, gParaRamData.fPPDMain, nil,
							dteMain + 4, nil, nil, ppdMain + 4, nil))
		{
			if (makeTrigrams != 0)
			{
				if (gParaRamData.fTrigrams != 0)
					trigramData = nil;
				CreateTrigramHeader(trigrams, gParaRamData.fTrigrams, trigramData);
			}
			if (makeTrigrams == 0 || *trigrams != nil)
			{
				HWRMemoryUnlockHandle(h);
				*dti = h;
				return 0;
			}
		}
		HWRMemoryUnlockHandle(h);
		UnloadData(&h);
		*dti = nil;
	}
	if (*trigrams != nil)
		UnloadTrigram(trigrams);
	return 1;
}


// ROM 0x002d4eec SetUpDteAddres__FPPvPPcN52PvN48
Boolean
SetUpDteAddres(DTIHeader** dti, void* ramDTE, ULong ramDTEMain, void* a8, Handle pdf, ULong ramPDF, void* c4,
			   UByte* dteMain, UByte* ramDTEMainPtr, void* ac, UByte* ppdMain, void* c8)
{
	(*dti)->fRAMDTE = ramDTE;
	(*dti)->fDTEMain = dteMain;
	(*dti)->fRAMDTEMain = ramDTEMain;
	(*dti)->fRAMDTEMainPtr = ramDTEMainPtr;
	(*dti)->fA8 = a8;
	(*dti)->fAC = ac;
	if (pdf != nil)
		(*dti)->fPDF = pdf;
	else if (ppdMain != nil)
		(*dti)->fPDF = CreatePDFHeader(ppdMain);
	else
		(*dti)->fPDF = nil;
	(*dti)->fPDFPtr = nil;
	(*dti)->fRAMPDF = ramPDF;
	(*dti)->fC4 = c4;
	(*dti)->fC8 = c8;
	(*dti)->fCC = 0;
	return (*dti)->fPDF == nil;
}


// ROM 0x002d4d3c CreatePDFHeader__FPv
Handle
CreatePDFHeader(UByte* ppd)
{
	if (ppd != nil)
	{
		Handle h = HWRMemoryAllocHandle(sizeof(PDFHeader));
		if (h != nil)
		{
			PDFHeader* header = (PDFHeader*) HWRMemoryLockHandle(h);
			if (header != nil)
			{
				ULong offset = GetBigEndianWord(ppd + 0x1c);
				memcpy(header->fWord3, ppd + 0x0c, 4);
				header->fSection0 = ppd + 0x10;
				header->fSection1 = ppd + offset + 0x10;
				header->fSection2 = ppd + (long) (short) GetBigEndianHalf(header->fWord3) * 2 + offset + 0x10;
				HWRMemoryUnlockHandle(h);
				return h;
			}
			HWRMemoryFreeHandle(h);
		}
	}
	return nil;
}


// ROM 0x002d4cbc CreateTrigramHeader__FPPvPPcPv
long
CreateTrigramHeader(Handle* header, ULong ramTrigram, UByte* trigrams)
{
	*header = HWRMemoryAllocHandle(0x98);
	if (*header == nil)
		return 0;
	TrigramHeader* p = (TrigramHeader*) HWRMemoryLockHandle(*header);
	if (p == nil)
	{
		HWRMemoryFreeHandle(*header);
		*header = nil;
		return 0;
	}
	memset(p, 0, sizeof(TrigramHeader));
	p->fRAMTrigram = ramTrigram;
	p->fTrigram = trigrams;
	HUnlock(*header);
	return 1;
}


// ROM 0x002d4ddc LockRAMPDF__FPUc
PDFHeader*
LockRAMPDF(ULong ramPDF)
{
	if (ramPDF != 0)
	{
		UByte* ppd = (UByte*) LockRamParaData(ramPDF);
		if (ppd != nil)
		{
			PDFHeader* header = (PDFHeader*) HWRMemoryAlloc(sizeof(PDFHeader));
			if (header != nil)
			{
				ULong offset = GetBigEndianWord(ppd + 0x1c);
				memcpy(header->fWord3, ppd + 0x0c, 4);
				header->fSection0 = ppd + 0x10;
				header->fSection1 = ppd + offset + 0x10;
				header->fSection2 = ppd + (long) (short) GetBigEndianHalf(header->fWord3) * 2 + offset + 0x10;
				return header;
			}
			UnlockRamParaData(ramPDF);
		}
	}
	return nil;
}


// ROM 0x002d4e74 UnlockRAMPDF__FPUcT1
long
UnlockRAMPDF(ULong ramPDF, PDFHeader* header)
{
	if (header != nil)
		HWRMemoryFree((Ptr) header);
	return UnlockRamParaData(ramPDF);
}


// ROM 0x002d4e9c ARM_MAC_PDFUnloadFile__FPUlPPUc
long
ARM_MAC_PDFUnloadFile(Handle* pdf, void** locked)
{
	if (*pdf == nil)
		*locked = nil;
	else
	{
		if (*locked != nil)
			HWRMemoryUnlockHandle(*pdf);
		*locked = nil;
		HWRMemoryFreeHandle(*pdf);
		*pdf = nil;
	}
	return 1;
}


// ROM 0x00087900 dti_lock__FPv
long
dti_lock(DTIHeader* dti)
{
	if (dti == nil)
		return 1;
	if (dti->fRAMDTEMainPtr == nil && dti->fRAMDTEMain != 0)
		dti->fRAMDTEMainPtr = (UByte*) LockRamParaData(dti->fRAMDTEMain);
	if (dti->fRAMPDF != 0 && dti->fPDFPtr == nil)
	{
		dti->fPDFPtr = LockRAMPDF(dti->fRAMPDF);
		if (dti->fPDFPtr == nil)
			dti->fRAMPDF = 0;
	}
	if (dti->fPDFPtr == nil && dti->fPDF != nil)
		dti->fPDFPtr = HWRMemoryLockHandle(dti->fPDF);
	if (dti->fLearnInfoPtr == nil && dti->fLearnInfo != nil)
		dti->fLearnInfoPtr = (UByte*) HWRMemoryLockHandle(dti->fLearnInfo);
	return 0;
}


// ROM 0x00087cdc dti_unlock__FPv
long
dti_unlock(DTIHeader* dti)
{
	if (dti == nil)
		return 1;
	if (dti->fRAMDTEMainPtr != nil && dti->fRAMDTEMain != 0)
	{
		UnlockRamParaData(dti->fRAMDTEMain);
		dti->fRAMDTEMainPtr = nil;
	}
	if (dti->fRAMPDF == 0)
	{
		if (dti->fPDFPtr == nil || dti->fPDF == nil)
			goto learnInfo;
		HWRMemoryUnlockHandle(dti->fPDF);
		dti->fPDFPtr = nil;
	}
	if (dti->fRAMPDF != 0 && dti->fPDFPtr != nil)
	{
		UnlockRAMPDF(dti->fRAMPDF, (PDFHeader*) dti->fPDFPtr);
		dti->fPDFPtr = nil;
	}
learnInfo:
	if (dti->fLearnInfoPtr != nil && dti->fLearnInfo != nil)
	{
		HWRMemoryUnlockHandle(dti->fLearnInfo);
		dti->fLearnInfoPtr = nil;
	}
	return 0;
}


// ROM 0x0008788c dti_unload__FPPv
long
dti_unload(Handle* dti)
{
	if (dti != nil)
	{
		DTIHeader* p = (DTIHeader*) HWRMemoryLockHandle(*dti);
		if (p == nil)
			HWRMemoryFreeHandle(*dti);
		else
		{
			dti_unlock(p);
			if (p->fPDF != nil)
				ARM_MAC_PDFUnloadFile(&p->fPDF, &p->fPDFPtr);
			HWRMemoryUnlockHandle(*dti);
			HWRMemoryFreeHandle(*dti);
			*dti = nil;
		}
	}
	return 0;
}


// ROM 0x0021c16c triads_unlock__FPv
long
triads_unlock(TrigramHeader* header)
{
	if (header == nil)
		return 1;
	if (header->fTrigram != nil && header->fRAMTrigram != 0)
	{
		UnlockRamParaData(header->fRAMTrigram);
		header->fTrigram = nil;
	}
	return 0;
}


// ROM 0x0021c0cc triads_unload__FPPv
long
triads_unload(Handle* header)
{
	TrigramHeader* p = (TrigramHeader*) HWRMemoryLockHandle(*header);
	if (p == nil)
		HWRMemoryFreeHandle(*header);
	else
	{
		triads_unlock(p);
		HWRMemoryUnlockHandle(*header);
		HWRMemoryFreeHandle(*header);
		*header = nil;
	}
	return 0;
}


// ROM 0x00169058 voc_unload__FPPv
long
voc_unload(Handle* vocs)
{
	if (*vocs != nil)
	{
		Ptr p = HWRMemoryLockHandle(*vocs);
		Handle h = *vocs;
		if (p != nil)
		{
			HWRMemoryUnlockHandle(h);
			HWRMemoryFreeHandle(*vocs);
			*vocs = nil;
			return 0;
		}
		if (h != nil)
			HWRMemoryFreeHandle(h);
	}
	*vocs = nil;
	return 1;
}


// ROM 0x002ba334 UnloadVoc__FPPv
long
UnloadVoc(Handle* vocs)
{
	return voc_unload(vocs);
}


// ROM 0x002ba338 UnloadData__FPPv
long
UnloadData(Handle* dti)
{
	return dti_unload(dti);
}


// ROM 0x002ba33c UnloadTrigram__FPPv
long
UnloadTrigram(Handle* header)
{
	return triads_unload(header);
}


// ROM 0x00168f48 SetUpVocAdders__FPP15AirusAParmBlockT1PPvPcs
long
SetUpVocAdders(void* main, void* aux, Handle* vocs, const char* /*name*/, short index)
{
	Boolean made = false;
	if (*vocs == nil)
	{
		*vocs = HWRMemoryAllocHandle(sizeof(VocAdders));
		if (*vocs == nil)
			return 0;
		made = true;
	}
	VocAdders* p = (VocAdders*) HWRMemoryLockHandle(*vocs);
	if (p == nil)
	{
		HWRMemoryFreeHandle(*vocs);
		*vocs = nil;
		return 0;
	}
	if (made)
		memset(p, 0, sizeof(VocAdders));
	p->fMain[index] = main;
	p->fAux[index] = aux;
	HUnlock(*vocs);
	return 1;
}


// ROM 0x002fe894 ReadVocResource__FPUcPPv
// (the ".air" file name it makes is never used: the vocabularies are the
//  dictionary chains an area gives the domain, not files)
long
ReadVocResource(const char* name, Handle* vocs)
{
	char fileName[80];
	HWRStrCpy(fileName, name);
	char* dot = HWRStrrChr(fileName, '.');
	if (dot != nil)
		*dot = 0;
	HWRStrCat(fileName, ".air");
	*vocs = nil;
	SetUpVocAdders(nil, nil, vocs, name, 1);
	return 0;
}


// ROM 0x002ba340 LoadVocAndData__FPcN21PPvN24PPPcT7
long
LoadVocAndData(const char* voc, const char* /*voc2*/, const char* dte, Handle* vocs, Handle* dti,
			   Handle* trigrams, void** main, void** aux)
{
	for (short i = 0; i < 15; i++)
		vocs[i] = nil;
	if (ReadDteResource(dte, 1, dti, trigrams, 1) == 0 && ReadVocResource(voc, vocs) == 0)
	{
		*main = nil;
		*aux = nil;
		if (vocs[0] != nil)
		{
			VocAdders* p = (VocAdders*) HWRMemoryLockHandle(vocs[0]);
			if (p != nil)
			{
				*main = p->fMain[0];
				*aux = p->fAux[0];
				HWRMemoryUnlockHandle(vocs[0]);
			}
		}
		return 0;
	}
	return -1;
}


// ROM 0x002d4d2c GetLearnInfoPtr__FPv
Ptr
GetLearnInfoPtr(DTIHeader* dti)
{
	return dti == nil ? nil : (Ptr) dti->fLearnInfoPtr;
}


// ROM 0x002d4fc0 GetDTELearnInfoHandle__FPv
Handle
GetDTELearnInfoHandle(Handle dti)
{
	if (dti != nil)
	{
		DTIHeader* p = (DTIHeader*) HWRMemoryLockHandle(dti);
		if (p != nil)
		{
			Handle info = p->fLearnInfo;
			HWRMemoryUnlockHandle(dti);
			return info;
		}
	}
	return nil;
}


// ROM 0x002d5004 GetDTELearnInfoSize__FPv
long
GetDTELearnInfoSize(void* dti)
{
	return dti == nil ? 0 : kLearnInfoSize;
}


// ROM 0x002d5018 SetLearnInfoAddress__FPvUl
Boolean
SetLearnInfoAddress(DTIHeader* dti, Handle info)
{
	if (dti != nil)
	{
		dti->fLearnInfo = info;
		dti->fLearnInfoPtr = nil;
	}
	return dti != nil;
}


// ROM 0x002fe90c AllocLearnInfo__FPPvUl
long
AllocLearnInfo(Handle* dti, ULong letterSet)
{
	if (letterSet > 4 || (letterSet == 4 && gParaRamData.fLearnInfo[4] != nil))
		return 1;
	DTIHeader* p = (DTIHeader*) HWRMemoryLockHandle(*dti);
	if (p == nil)
		return 1;
	long result;
	if (gParaRamData.fLearnInfo[letterSet] == nil && letterSet != 4)
	{
		result = 1;
		long size = GetDTELearnInfoSize(p);
		gParaRamData.fLearnInfo[letterSet] = HWRMemoryAllocHandle(size);
		Ptr info;
		if (gParaRamData.fLearnInfo[letterSet] != nil
		&&  (info = HWRMemoryLockHandle(gParaRamData.fLearnInfo[letterSet])) != nil)
		{
			memset(info, 0, size);
			HWRMemoryUnlockHandle(gParaRamData.fLearnInfo[letterSet]);
			SetLearnInfoAddress(p, gParaRamData.fLearnInfo[letterSet]);
			if (dti_lock(p) == 0)
			{
				SetDefaultsWeights(p);
				if (dti_unlock(p) == 0)
					result = 0;
			}
		}
	}
	else
	{
		SetLearnInfoAddress(p, gParaRamData.fLearnInfo[letterSet]);
		result = 0;
	}
	HWRMemoryUnlockHandle(*dti);
	return result;
}


// ROM 0x00148064 ORGetDBSize__Fv
ULong
ORGetDBSize(void)
{
	return 0x6000;
}


// ROM 0x0014806c ORInitDB__FPvUl
void
ORInitDB(void* db, ULong size)
{
	if (db == nil)
		return;
	memset(db, 0, size);
	((ULong32*) db)[0] = 0x71;
	((UByte*) db)[7] = 0;
	((UByte*) db)[6] = 0;
	((ULong32*) db)[2] = size;
	((ULong32*) db)[3] = 0x10;
}


// ROM 0x002fea10 AllocOrtographLearnInfo__Fv
Handle
AllocOrtographLearnInfo(void)
{
	if (gParaRamData.fOrtho != nil)
		return gParaRamData.fOrtho;
	gParaRamData.fOrtho = HWRMemoryAllocHandle(ORGetDBSize());
	if (gParaRamData.fOrtho == nil)
		return nil;
	Ptr db = HWRMemoryLockHandle(gParaRamData.fOrtho);
	if (db != nil)
	{
		ORInitDB(db, ORGetDBSize());
		HWRMemoryUnlockHandle(gParaRamData.fOrtho);
		return gParaRamData.fOrtho;
	}
	HWRMemoryFreeHandle(gParaRamData.fOrtho);
	gParaRamData.fOrtho = nil;
	return nil;
}


// ROM 0x000873d8 SetDefaultsWeights__FPv
long
SetDefaultsWeights(DTIHeader* dti)
{
	long result = 0;
	if (SetDefCaps(dti) != 0)
		result = -1;
	if (SetDefVexes(dti) != 0)
		result = -1;
	return result;
}


// ROM 0x000879b0 SetDefCaps__FPv
long
SetDefCaps(DTIHeader* dti)
{
	UByte* info = (dti != nil) ? dti->fLearnInfoPtr : nil;
	if (dti != nil && info != nil)
	{
		memset(info + 0x820, 0, 0x104);
		return 0;
	}
	return 1;
}


// ROM 0x000879e8 SetDefVexes__FPv
// Each character code's bytes set to its descriptors' default vexes:
// all sixteen from the ROM's descriptor, and then the RAM one's over the
// variants after the ROM's.
long
SetDefVexes(DTIHeader* dti)
{
	UByte* info = (dti != nil) ? dti->fLearnInfoPtr : nil;
	if (dti == nil || info == nil)
		return 1;
	for (long c = 0x20; c < 0xa2; c++)
	{
		dte_sym_header_type* descriptor;
		ULong count;
		if (GetSymDescriptor(c, 0, &descriptor, dti) < 0)
			count = 0;
		else
		{
			memcpy(info + c * 0x10 - 0x200, descriptor + 0x14, 0x10);
			count = descriptor[0];
		}
		if (GetSymDescriptor(c, count, &descriptor, dti) >= 0)
			memcpy(info + c * 0x10 + count - 0x200, descriptor + 0x14, 0x10 - count);
	}
	return 0;
}


// ROM 0x00087dc4 GetSymDescriptor__FUcT1PP19dte_sym_header_typePv
// The ROM's table first and then the RAM one's: each descriptor's first
// byte is how many variants it holds, and `variant` is counted across
// both.
long
GetSymDescriptor(UByte sym, UByte variant, dte_sym_header_type** descriptor, DTIHeader* dti)
{
	long before = 0;
	if (dti == nil)
		return -1;
	long total = 0;
	dte_sym_header_type* found = nil;
	for (long table = 0; table < 2; table++)
	{
		found = nil;
		if (table == 0)
		{
			UByte* offsets = dti->fDTEMain;
			ULong offset;
			if (offsets == nil || (offset = GetBigEndianWord(offsets + sym * 4)) == 0)
				continue;
			found = offsets + offset;
		}
		else
		{
			UByte* offsets = dti->fRAMDTEMainPtr;
			if (offsets != nil)
			{
				ULong offset = GetBigEndianWord(offsets + sym * 4);
				if (offset != 0)
					found = offsets + offset;
				before = total;
			}
		}
		ULong count;
		if (found == nil || (count = found[0]) == 0)
			continue;
		total += count;
		if (total > 0x10)
			return -1;
		if (variant < total)
			break;
	}
	if (total > 0 && variant < total)
	{
		*descriptor = found;
		return variant - before;
	}
	return -1;
}


// ROM 0x00087eec GetNumVarsOfChar__FUcPv
long
GetNumVarsOfChar(UByte c, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	if (GetSymDescriptor(OSToRec(c), 0, &descriptor, dti) < 0)
		return 0;
	UByte first = descriptor[0];
	ULong second;
	if (GetSymDescriptor(OSToRec(c), first, &descriptor, dti) < 0)
		second = 0;
	else
		second = descriptor[0];
	return first + second;
}


// ROM 0x00087b90 GetVarGroup__FUcT1Pv
long
GetVarGroup(UByte c, UByte variant, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	long index = GetSymDescriptor(OSToRec(c), variant, &descriptor, dti);
	if (index < 0)
		return -1;
	return (descriptor[index + 0x24] >> 1) & 7;
}


// ROM 0x00087e84 CheckVarActive__FUcN21Pv
long
CheckVarActive(UByte c, UByte variant, UByte style, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	long index = GetSymDescriptor(OSToRec(c), variant, &descriptor, dti);
	if (index < 0 || (descriptor[index + 0x24] & ((style << 4) & 0xff)) == 0)
		return 0;
	return 1;
}


// ROM 0x0008801c GetVarVex__FUcT1Pv
long
GetVarVex(UByte c, UByte variant, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	UByte* info;
	if (GetSymDescriptor(OSToRec(c), variant, &descriptor, dti) < 0 || (info = dti->fLearnInfoPtr) == nil)
		return -1;
	return (short) (info[OSToRec(c) * 0x10 + variant - 0x200] & 7);
}


// ROM 0x00088094 SetVarVex__FUcN21Pv
long
SetVarVex(UByte c, UByte variant, UByte vex, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	UByte* info;
	if (GetSymDescriptor(OSToRec(c), variant, &descriptor, dti) < 0 || (info = dti->fLearnInfoPtr) == nil)
		return 1;
	UByte* p = info + OSToRec(c) * 0x10 + variant - 0x200;
	*p &= 0xf8;
	*p |= vex & 7;
	return 0;
}


// ROM 0x00087b00 SetVarCounter__FUcN21Pv
long
SetVarCounter(UByte c, UByte variant, UByte count, DTIHeader* dti)
{
	dte_sym_header_type* descriptor;
	UByte* info;
	if (GetSymDescriptor(OSToRec(c), variant, &descriptor, dti) < 0 || (info = dti->fLearnInfoPtr) == nil)
		return 1;
	UByte* p = info + OSToRec(c) * 0x10 + variant - 0x200;
	*p &= 7;
	*p |= count << 3;
	return 0;
}


// ROM 0x0008731c GetDteVariantState__FUcN21Pv
long
GetDteVariantState(UByte c, UByte group, UByte style, DTIHeader* dti)
{
	long found = -1;
	long least = 7;
	UByte count = GetNumVarsOfChar(c, dti);
	UByte variant = 0;
	if (count != 0)
	{
		do
		{
			if (GetVarGroup(c, variant, dti) == group && CheckVarActive(c, variant, style, dti) != 0)
			{
				found = 0;
				long vex = GetVarVex(c, variant, dti);
				if (vex < least)
					least = vex;
			}
			variant++;
		} while (variant < count);
		if (found >= 0)
			return least;
	}
	return -1;
}


// ROM 0x00087754 SetDteVariantState__FUcT1iT1Pv
// Every variant of the group the letter set uses given the vex, and a
// use counter to match: 0 for a vex under 3, 15 for 3-6, 31 above.
long
SetDteVariantState(UByte c, UByte group, int vex, UByte style, DTIHeader* dti)
{
	long result = -10;
	UByte count = GetNumVarsOfChar(c, dti);
	UByte variant = 0;
	if (count != 0)
	{
		do
		{
			if (GetVarGroup(c, variant, dti) == group && CheckVarActive(c, variant, style, dti) != 0)
			{
				if (result == -10)
					result = 0;
				if (SetVarVex(c, variant, vex, dti) != 0)
					result = -1;
				long err;
				if (vex < 3 || vex > 6)
					err = SetVarCounter(c, variant, vex < 7 ? 0 : 0x1f, dti);
				else
					err = SetVarCounter(c, variant, 0x0f, dti);
				if (err != 0)
					result = -1;
			}
			variant++;
		} while (variant < count);
		if (result != -10)
			return result;
	}
	return -1;
}


// ROM 0x00087744 GetVariantState__FUcN21Pv
long
GetVariantState(UByte c, UByte group, UByte style, DTIHeader* dti)
{
	return GetDteVariantState(c, group, style, dti);
}


// ROM 0x00087714 SetVariantState__FUcT1iT1Pv
long
SetVariantState(UByte c, UByte group, int vex, UByte style, DTIHeader* dti)
{
	return SetDteVariantState(c, group, vex, style, dti);
}


/*------------------------------------------------------------------------------
	T h e   l e t t e r   s e t s '   l e a r n i n g   i n f o s
------------------------------------------------------------------------------*/

// ROM 0x002fdbfc PGFreeAllLearningData__Fv
void
PGFreeAllLearningData(void)
{
	for (long i = 0; i < 5; i++)
	{
		if (gParaRamData.fLearnInfo[i] != nil)
		{
			HWRMemoryFreeHandle(gParaRamData.fLearnInfo[i]);
			gParaRamData.fLearnInfo[i] = nil;
		}
	}
	if (gParaRamData.fOrtho != nil)
		HWRMemoryFreeHandle(gParaRamData.fOrtho);
	gParaRamData.fOrtho = nil;
}


// ROM 0x002fdc54 PGLetterSetInfoUnchanged__FPPcUl
// A letter set's defaults made afresh - the set's own learning info put
// aside for the moment so that AllocLearnInfo makes a new one - and
// compared with `info` a byte at a time.
Boolean
PGLetterSetInfoUnchanged(Handle info, ULong letterSet)
{
	Boolean unchanged = false;
	Handle dti = nil;
	Handle trigrams;
	Handle saved = gParaRamData.fLearnInfo[letterSet];
	gParaRamData.fLearnInfo[letterSet] = nil;
	if (ReadDteResource("avp.dte", 1, &dti, &trigrams, 0) == 0
	&&  AllocLearnInfo(&dti, letterSet) == 0)
	{
		Handle defaults = GetDTELearnInfoHandle(dti);
		if (defaults != nil)
		{
			long size = GetDTELearnInfoSize(dti);
			UByte* d = (UByte*) HWRMemoryLockHandle(defaults);
			if (d != nil)
			{
				UByte* p = (UByte*) HWRMemoryLockHandle(info);
				if (p != nil)
				{
					long i;
					for (i = 0; i < size && d[i] == p[i]; i++)
						;
					unchanged = (i == size);
				}
				if (d != nil && defaults != nil)
					HWRMemoryUnlockHandle(defaults);
				if (p != nil && info != nil)
					HWRMemoryUnlockHandle(info);
			}
		}
	}
	if (gParaRamData.fLearnInfo[letterSet] != nil)
		HWRMemoryFreeHandle(gParaRamData.fLearnInfo[letterSet]);
	gParaRamData.fLearnInfo[letterSet] = saved;
	if (dti != nil)
		UnloadData(&dti);
	return unchanged;
}


// ROM 0x002fdd94 PGGetLetterSetInfo__FUl
Handle
PGGetLetterSetInfo(ULong letterSet)
{
	Handle info = nil;
	if (letterSet < 5)
	{
		info = gParaRamData.fLearnInfo[letterSet];
		if (info != nil && PGLetterSetInfoUnchanged(info, letterSet))
			info = nil;
	}
	return info;
}


// ROM 0x00105ca0 ConvertLearningInfo__FPPcT1l
long
ConvertLearningInfo(Handle from, Handle to, long how)
{
	long result = -1;
	if (from == nil || to == nil)
		return -1;
	UByte* f = (UByte*) HWRMemoryLockHandle(from);
	UByte* t = (UByte*) HWRMemoryLockHandle(to);
	if (f != nil && t != nil)
	{
		const UByte* map;
		switch (how & 7)
		{
		case 0:	map = DefaultVexMapTable; break;
		case 1:	map = PalmerVexMapTable; break;
		case 2:	map = BlockVexMapTable; break;
		default: goto done;
		}
		for (long row = 0; row < 0x6a && row <= 0x5f; row++)
		{
			for (long i = 0; i < 8; i++)
			{
				UByte to8 = map[row * 8 + i];
				if (to8 != 0xff)
				{
					if (((how >> 16) & 7) == 0)
						t[row * 8 + to8] = f[row * 8 + i];
					else
						f[row * 8 + i] = t[row * 8 + to8];
				}
			}
		}
		result = 0;
	}
done:
	if (f != nil)
		HWRMemoryUnlockHandle(from);
	if (t != nil)
		HWRMemoryUnlockHandle(to);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   R A M   r e p l a c e m e n t s
------------------------------------------------------------------------------*/

static Ref
ParaDataSymbol(ULong which)
{
	switch (which)
	{
	case 1:	return Intern("RamParaGraphDTEM:PARA");
	case 2:	return Intern("RamParaGraphPPDB:PARA");
	default: return Intern("RamParaGraphTRIA:PARA");
	}
}


// ROM 0x002fdb34 GetGlobalParaDataRef__FRC6RefVar
Ref
GetGlobalParaDataRef(RefArg slot)
{
	RefVar frame(GetFrameSlotRef(RefVar(gVarFrame), RefVar(Intern("RamParaGraphData:PARA"))));
	if (!IsFrame(frame))
		return NILREF;
	return GetFrameSlotRef(frame, slot);
}


// ROM 0x002fd944 SetGlobalParaDataRef__FRC6RefVarT1
// nil takes the slot out, and the frame with it when that leaves it
// empty.  ==> the frame.
Ref
SetGlobalParaDataRef(RefArg slot, RefArg value)
{
	RefVar frame(GetFrameSlotRef(RefVar(gVarFrame), RefVar(Intern("RamParaGraphData:PARA"))));
	if (ISNIL(value))
	{
		if (NOTNIL(frame))
		{
			Boolean isFrame = IsFrame(frame);
			if (isFrame)
				RemoveSlot(frame, slot);
			if (!isFrame || Length(frame) == 0)
			{
				RefVar name(Intern("RamParaGraphData:PARA"));
				RemoveSlot(RefVar(gVarFrame), name);
				frame = NILREF;
			}
		}
	}
	else
	{
		if (!IsFrame(frame))
		{
			frame = AllocateFrame();
			RefVar name(Intern("RamParaGraphData:PARA"));
			SetFrameSlot(RefVar(gVarFrame), RefVar(EnsureInternal(name)), frame);
		}
		SetFrameSlot(frame, RefVar(EnsureInternal(slot)), value);
	}
	return frame;
}


// ROM 0x002fe3b0 LockRamParaData__FUl
Ptr
LockRamParaData(ULong which)
{
	if (which != 1 && which != 2 && which != 3)
		return HWRMemoryLockHandle((Handle) which);
	RefVar data(GetGlobalParaDataRef(RefVar(ParaDataSymbol(which))));
	if (ISNIL(data) || !IsBinary(data))
		return nil;
	LockRef(data);
	return BinaryData(data) + 4;
}


// ROM 0x002fe530 UnlockRamParaData__FUl
long
UnlockRamParaData(ULong which)
{
	if (which != 1 && which != 2 && which != 3)
		return HWRMemoryUnlockHandle((Handle) which);
	RefVar data(GetGlobalParaDataRef(RefVar(ParaDataSymbol(which))));
	if (NOTNIL(data) && IsBinary(data))
		UnlockRef(data);
	return 1;
}


// ROM 0x002fdde4 FSetRamParaData
// SetRamParaData(data, which): a replacement for one of the three tables.
// `which` 0-2 copies the binary into a handle of the engine's (bits 4-11
// non-zero: appended to what is there, which is how a table too big for
// one binary arrives in pieces); 3-5 keeps the binary itself in
// `RamParaGraphData` and marks the table 1, 2 or 3.  nil takes the
// replacement away.  ==> 0, or -1 (the ROM answers the word 0xfffffffc,
// which as a Ref is the integer -1).
// DEVIATION: a handle's value is a host pointer, kept in the ULong the
// ROM keeps it in (ULong is pointer-sized on the host).
Ref
FSetRamParaData(RefArg /*rcvr*/, RefArg data, RefArg whichArg)
{
	if (ISNIL(whichArg))
		return MAKEINT(-1);
	ULong which = RINT(whichArg);
	ULong* slot = &gParaRamData.fDTEMain;
	switch (which & 0xf)
	{
	case 0:
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(1)), RefVar());
		break;
	case 1:
		slot = &gParaRamData.fPPDMain;
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(2)), RefVar());
		break;
	case 2:
		slot = &gParaRamData.fTrigrams;
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(3)), RefVar());
		break;
	case 3:
		if (gParaRamData.fDTEMain > 1)
			DisposHandle((Handle) gParaRamData.fDTEMain);
		gParaRamData.fDTEMain = NOTNIL(data) ? 1 : 0;
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(1)), data);
		return MAKEINT(0);
	case 4:
		if (gParaRamData.fPPDMain != 0 && gParaRamData.fPPDMain != 2)
			DisposHandle((Handle) gParaRamData.fPPDMain);
		gParaRamData.fPPDMain = ISNIL(data) ? 0 : 2;
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(2)), data);
		return MAKEINT(0);
	case 5:
		if (gParaRamData.fTrigrams != 0 && gParaRamData.fTrigrams != 3)
			DisposHandle((Handle) gParaRamData.fTrigrams);
		gParaRamData.fTrigrams = ISNIL(data) ? 0 : 3;
		SetGlobalParaDataRef(RefVar(ParaDataSymbol(3)), data);
		return MAKEINT(0);
	default:
		return MAKEINT(-1);
	}
	if ((which & 0xff0) == 0 && *slot != 0)
	{
		if (*slot != 1 && *slot != 2 && *slot != 3)
			DisposHandle((Handle) *slot);
		*slot = 0;
	}
	if (ISNIL(data))
		return MAKEINT(0);
	LockRef(data);
	long size = Length(data);
	Size at;
	if ((which & 0xff0) == 0)
	{
		Handle h = NewHandle(size);
		*slot = (ULong) h;
		if (h == nil)
			goto failed;
		at = 0;
	}
	else
	{
		if (*slot == 0)
			goto failed;
		at = GetHandleSize((Handle) *slot);
		if (SetHandleSize((Handle) *slot, size + at) != noErr)
			goto failed;
	}
	HLock((Handle) *slot);
	BlockMove(BinaryData(data), *(Handle) *slot + at, size);
	HUnlock((Handle) *slot);
	UnlockRef(data);
	return MAKEINT(0);

failed:
	UnlockRef(data);
	return MAKEINT(-1);
}


// ROM 0x002fe16c FGetRamParaData
// GetRamParaData(which): 0-2 answer a piece of a table's replacement as a
// 'raw binary - bits 12-29 the piece's length (nought: the whole) and
// bits 4-11 which piece; 3-5 the binary kept in `RamParaGraphData`.
Ref
FGetRamParaData(RefArg /*rcvr*/, RefArg whichArg)
{
	RefVar result;
	if (ISNIL(whichArg))
		return NILREF;
	ULong which = RINT(whichArg);
	Handle h;
	switch (which & 0xf)
	{
	case 0:	h = (Handle) gParaRamData.fDTEMain; goto piece;
	case 1:	h = (Handle) gParaRamData.fPPDMain; goto piece;
	case 2:	h = (Handle) gParaRamData.fTrigrams;
	piece:
		if ((ULong) h > 3)
		{
			ULong size = GetHandleSize(h);
			ULong length = (which >> 12) & 0x3ffff;
			if (length == 0)
				length = size;
			ULong index = (which >> 4) & 0xff;
			ULong start = index * length;
			if (start <= size)
			{
				if (length * (index + 1) > size)
					length = size - start;
				result = AllocateBinary(RSSYMraw, length);
				if (NOTNIL(result))
				{
					LockRef(result);
					HLock(h);
					BlockMove(*h + start, BinaryData(result), length);
					HUnlock(h);
					UnlockRef(result);
				}
			}
		}
		break;
	case 3:
		if (gParaRamData.fDTEMain == 1)
			result = GetGlobalParaDataRef(RefVar(ParaDataSymbol(1)));
		break;
	case 4:
		if (gParaRamData.fPPDMain == 2)
			result = GetGlobalParaDataRef(RefVar(ParaDataSymbol(2)));
		break;
	case 5:
		if (gParaRamData.fTrigrams == 3)
			result = GetGlobalParaDataRef(RefVar(ParaDataSymbol(3)));
		break;
	}
	return result;
}


/*------------------------------------------------------------------------------
	T h e   R O M ' s   r e s o u r c e s
------------------------------------------------------------------------------*/

// ROM 0x00168284 FGetDTEHeader__FRC6RefVar
Ref
FGetDTEHeader(RefArg /*rcvr*/)
{
	return GetFrameSlotRef(RefVar(Rcharsetinforesources), RSSYMdteheader);
}


// ROM 0x001682a8 FGetDTEMain__FRC6RefVar
Ref
FGetDTEMain(RefArg /*rcvr*/)
{
	return GetFrameSlotRef(RefVar(Rcharsetinforesources), RSSYMdtemain);
}


// ROM 0x001682cc FGetPPDMain__FRC6RefVar
Ref
FGetPPDMain(RefArg /*rcvr*/)
{
	return GetFrameSlotRef(RefVar(Rcharsetinforesources), RSSYMppdmain);
}


// ROM 0x001682f0 FGetDTETrigram__FRC6RefVar
Ref
FGetDTETrigram(RefArg /*rcvr*/)
{
	return GetFrameSlotRef(RefVar(Rcharsetinforesources), RSSYMdtetrigrams);
}


// ROM 0x0016831c FGetLetterImages__FRC6RefVar
Ref
FGetLetterImages(RefArg /*rcvr*/)
{
	return GetFrameSlotRef(RefVar(Rcharsetinforesources), RSSYMletterimages);
}


void
RegisterParaGraphNatives(void)
{
	RegisterNativeFunction("FSetRamParaData", (void*) FSetRamParaData, 2);
	RegisterNativeFunction("FGetRamParaData", (void*) FGetRamParaData, 1);
}
