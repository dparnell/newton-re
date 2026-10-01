/*
	File:		armcpu/ARMCardHandler.cpp

	Contains:	The TCardHandler proxy and the card side's host objects as
				the ARM code sees them (ARMCardHandler.h).

	Written by:	the reconstruction (DEVIATION: the ROM runs a card
				handler part's code where it lies).  The glue cites the ROM
				functions it answers for; the mirrors are in the ROM's
				layout of the DDK's classes (CardPCMCIA.h, as clang lays
				them out for the ARM - tools/newton-rom/parse_headers.py).
*/

#include "ARMCardHandler.h"
#include "ARMProtocols.h"
#include "ARMWorld.h"
#include "CardHandler.h"
#include "CardSocket.h"
#include "CardPCMCIA.h"
#include "CardATALoader.h"
#include "HostCard.h"
#include "OSErrors.h"

#include <stdio.h>
#include <string.h>

// the mirrors' kinds
enum
{
	kMirrorSocket		= 'csok',
	kMirrorCard			= 'cpcm',
	kMirrorFunction		= 'cfun',
	kMirrorConfig		= 'ccfg',
	kMirrorPartition	= 'atap'
};

static void		PutByte(uint32_t a, uint32_t v)		{ ARMWrite8(a, (uint8_t) v); }
static void		PutHalf(uint32_t a, uint32_t v)		{ ARMWrite8(a, (uint8_t) (v >> 8)); ARMWrite8(a + 1, (uint8_t) v); }
static void		PutWord(uint32_t a, uint32_t v)		{ ARMWrite32(a, v); }


/*------------------------------------------------------------------------------
	T h e   m i r r o r s
------------------------------------------------------------------------------*/

// a socket: a handle (the glue answers its methods)
static uint32_t
SocketMirror(TCardSocket* socket)
{
	return ARMMirrorFor(socket, 8, kMirrorSocket);
}

// a TCardPCMCIA as the ROM lays it out, as far as its fields go before its
// strings (+0x30; the strings are its methods')
static uint32_t
CardMirror(TCardPCMCIA* card)
{
	if (card == nil)
		return 0;
	uint32_t m = ARMMirrorFor(card, 0x100, kMirrorCard);
	uint32_t flags = 0;
	if (card->fNoAttrMem)				flags |= 0x80000000;
	if (card->fBadCIS)					flags |= 0x40000000;
	if (card->fAttrMemWrable)			flags |= 0x20000000;
	if (card->fTooManyUnknownTuples)	flags |= 0x10000000;
	if (card->fFunctionIdAvail)			flags |= 0x08000000;
	if (card->f16BitOnlyCard)			flags |= 0x04000000;
	if (card->f8BitOnlyCard)			flags |= 0x02000000;
	if (card->fNoCIS)					flags |= 0x01000000;
	if (card->fGlobalCIS)				flags |= 0x00800000;
	if (card->fFuncSpecificCIS)			flags |= 0x00400000;
	PutWord(m + 0x00, flags);
	PutWord(m + 0x04, (uint32_t) card->fSocketNumber);
	PutWord(m + 0x08, (uint32_t) card->fTotalDeviceSize);
	PutWord(m + 0x0c, (uint32_t) card->fFirstDataByteAddress);
	PutWord(m + 0x10, (uint32_t) card->fRegisterBaseAddress);
	PutWord(m + 0x14, (uint32_t) card->fRegistersPresent);
	PutHalf(m + 0x18, card->fManufactureId);
	PutHalf(m + 0x1a, card->fManufactureIdInfo);
	PutByte(m + 0x1c, card->fFunctionId);
	PutByte(m + 0x1d, card->fFunctionSysInit);
	for (ULong i = 0; i < kNumFuncExtTuples; i++)
		for (ULong j = 0; j < kFuncExtSize; j++)
			PutByte(m + 0x1e + i * kFuncExtSize + j, card->fFuncExt[i][j]);
	PutByte(m + 0x26, card->fNumOfFuncExt);
	PutByte(m + 0x27, card->fNumOfDevice);
	PutByte(m + 0x28, card->fNumOfConfigEntry);
	PutByte(m + 0x29, card->fNumOfPackage);
	PutByte(m + 0x2a, card->fNumOfUnknownTuples);
	PutByte(m + 0x2b, card->fConfigurationLastEntryNumber);
	PutByte(m + 0x2c, card->fVendorSpecificV2Bytes[0]);
	PutByte(m + 0x2d, card->fVendorSpecificV2Bytes[1]);
	return m;
}

static uint32_t
FunctionMirror(TCardFunction* function)
{
	if (function == nil)
		return 0;
	uint32_t m = ARMMirrorFor(function, 0x20, kMirrorFunction);
	PutByte(m + 0, function->fFuncId);
	PutByte(m + 1, function->fFuncIdSysInits);
	PutByte(m + 2, function->fConfigEntryNumberStart);
	PutByte(m + 3, function->fConfigEntryNumberEnd);
	PutWord(m + 4, (uint32_t) function->fRegisterBaseAddress);
	return m;
}

static uint32_t
ConfigMirror(TCardConfiguration* c)
{
	if (c == nil)
		return 0;
	uint32_t m = ARMMirrorFor(c, 0x15c, kMirrorConfig);
	for (ULong i = 0; i < kPowerDescrValues; i++)
	{
		PutWord(m + 0x00 + i * 4, (uint32_t) c->fVcc[i]);
		PutWord(m + 0x1c + i * 4, (uint32_t) c->fVpp1[i]);
		PutWord(m + 0x38 + i * 4, (uint32_t) c->fVpp2[i]);
		PutWord(m + 0x54 + i * 4, (uint32_t) c->fVccAttr[i]);
		PutWord(m + 0x70 + i * 4, (uint32_t) c->fVpp1Attr[i]);
		PutWord(m + 0x8c + i * 4, (uint32_t) c->fVpp2Attr[i]);
	}
	for (ULong i = 0; i < kNumIOBlocks; i++)
	{
		PutWord(m + 0xa8 + i * 4, (uint32_t) c->fIoAddresses[i]);
		PutWord(m + 0xc8 + i * 4, (uint32_t) c->fIoLengths[i]);
	}
	for (ULong i = 0; i < kNumMemoryBlocks; i++)
	{
		PutWord(m + 0xe8 + i * 4, (uint32_t) c->fMemAddresses[i]);
		PutWord(m + 0x108 + i * 4, (uint32_t) c->fMemLengths[i]);
		PutWord(m + 0x128 + i * 4, (uint32_t) c->fHostAddresses[i]);
	}
	PutWord(m + 0x148, (uint32_t) c->fWaitTimeNSecs);
	PutWord(m + 0x14c, (uint32_t) c->fRdyBsyTimeNSecs);
	PutByte(m + 0x150, c->fFeatureSelection);
	PutByte(m + 0x151, c->fNumOfIOSpace);
	PutByte(m + 0x152, c->fIoAddrLines);
	PutByte(m + 0x153, c->fIo8BitOK);
	PutByte(m + 0x154, c->fIo16BitOK);
	PutByte(m + 0x155, c->fConfigurationNumber);
	PutByte(m + 0x156, c->fInterfaceType);
	PutByte(m + 0x157, c->fActiveBits);
	PutByte(m + 0x158, c->fInterruptInfo);
	PutByte(m + 0x159, c->fInterruptShare);
	PutByte(m + 0x15a, c->fNumOfMemMap);
	PutByte(m + 0x15b, c->fMiscBits);
	return m;
}

// an ATA card's partition info: the ROM's 0x20 bytes, its two entries
// copied into the ARM heap (0x200 bytes each)
static uint32_t
PartitionMirror(TATAPartitionInfo* info)
{
	if (info == nil)
		return 0;
	uint32_t m = ARMMirrorFor(info, 0x20 + 0x400, kMirrorPartition);
	PutWord(m + 0x00, (uint32_t) info->fPCPartition);
	PutWord(m + 0x04, (uint32_t) info->fMapBlock);
	PutWord(m + 0x08, (uint32_t) info->fDriverBlock);
	PutWord(m + 0x10, (uint32_t) info->fNewtonBlock);
	PutWord(m + 0x18, (uint32_t) info->fField18);
	PutWord(m + 0x1c, (uint32_t) info->fField1C);
	uint8_t* driver = ARMHostAddress(m + 0x20, 0x200);
	uint8_t* newton = ARMHostAddress(m + 0x220, 0x200);
	if (info->fDriverEntry != nil && driver != nil)
		memcpy(driver, info->fDriverEntry, 0x200);
	if (info->fNewtonEntry != nil && newton != nil)
		memcpy(newton, info->fNewtonEntry, 0x200);
	PutWord(m + 0x0c, info->fDriverEntry != nil ? m + 0x20 : 0);
	PutWord(m + 0x14, info->fNewtonEntry != nil ? m + 0x220 : 0);
	return m;
}

// a socket's window as a card-bus region (made once for each place and size)
struct WindowRegion { ULong fHost; ULong fSize; uint32_t fARM; };
static WindowRegion	gWindows[8];
static int			gWindowCount = 0;

static uint32_t
WindowFor(ULong host, ULong size)
{
	if (host == 0 || size == 0)
		return 0;
	for (int i = 0; i < gWindowCount; i++)
		if (gWindows[i].fHost == host && gWindows[i].fSize == size)
			return gWindows[i].fARM;
	uint32_t a = ARMMapRegion((void*) host, (uint32_t) size, kARMRegionCardBus);
	if (gWindowCount < 8)
	{
		WindowRegion w = { host, size, a };
		gWindows[gWindowCount++] = w;
	}
	return a;
}


/*------------------------------------------------------------------------------
	T h e   g l u e
------------------------------------------------------------------------------*/

static TCardSocket*
SocketArg(ARMTrapContext& c)
{
	TCardSocket* socket = (TCardSocket*) ARMHostOf(c.Arg(0), kMirrorSocket);
	if (socket == nil)
		ThrowMsg("armcpu: not a card socket the host handed over");
	return socket;
}

static TCardPCMCIA*
CardArg(ARMTrapContext& c)
{
	TCardPCMCIA* card = (TCardPCMCIA*) ARMHostOf(c.Arg(0), kMirrorCard);
	if (card == nil)
		ThrowMsg("armcpu: not a card the host handed over");
	return card;
}

// ROM 0x00055468 SocketNumber__11TCardSocketFv
static bool
Glue_SocketNumber(void*, ARMTrapContext& c)
{
	c.Return((uint32_t) SocketArg(c)->SocketNumber());
	return true;
}
// ROM 0x00055484 AttributeMemBaseAddr__11TCardSocketFv
static bool
Glue_AttributeMemBaseAddr(void*, ARMTrapContext& c)
{
	c.Return(WindowFor(SocketArg(c)->AttributeMemBaseAddr(), kHostCardAttrSize));
	return true;
}
// ROM 0x00055498 CommonMemBaseAddr__11TCardSocketFv
static bool
Glue_CommonMemBaseAddr(void*, ARMTrapContext& c)
{
	TCardSocket* socket = SocketArg(c);
	c.Return(WindowFor(socket->CommonMemBaseAddr(), HostCardCommonSize(socket->SocketNumber())));
	return true;
}
// ROM 0x000554b0 IOBaseAddr__11TCardSocketFv
// (the host's sockets have no I/O space: nought, as theirs answers)
static bool
Glue_IOBaseAddr(void*, ARMTrapContext& c)
{
	SocketArg(c);
	c.Return(0);
	return true;
}

// a card's strings: a copy in the ARM heap, the last one of each kind
// given back when the next is asked for
static uint32_t	gStrings[6];
static uint32_t
StringOf(int which, const char* s)
{
	ARMFree(gStrings[which]);
	gStrings[which] = s != nil ? ARMCString(s) : 0;
	return gStrings[which];
}
// ROM 0x0004f894 GetCardManufacturer__11TCardPCMCIACFv
static bool	Glue_GetCardManufacturer(void*, ARMTrapContext& c)	{ c.Return(StringOf(0, CardArg(c)->GetCardManufacturer())); return true; }
// ROM 0x0004f8b4 GetCardProduct__11TCardPCMCIACFv
static bool	Glue_GetCardProduct(void*, ARMTrapContext& c)		{ c.Return(StringOf(1, CardArg(c)->GetCardProduct())); return true; }
// ROM 0x0004f8d4 GetCardV1String3__11TCardPCMCIACFv
static bool	Glue_GetCardV1String3(void*, ARMTrapContext& c)		{ c.Return(StringOf(2, CardArg(c)->GetCardV1String3())); return true; }
// ROM 0x0004f8f4 GetCardV1String4__11TCardPCMCIACFv
static bool	Glue_GetCardV1String4(void*, ARMTrapContext& c)		{ c.Return(StringOf(3, CardArg(c)->GetCardV1String4())); return true; }
// ROM 0x0004f930 GetCardV2Vendor__11TCardPCMCIACFv
static bool	Glue_GetCardV2Vendor(void*, ARMTrapContext& c)		{ c.Return(StringOf(4, CardArg(c)->GetCardV2Vendor())); return true; }
// ROM 0x0004f950 GetCardV2Info__11TCardPCMCIACFv
static bool	Glue_GetCardV2Info(void*, ARMTrapContext& c)		{ c.Return(StringOf(5, CardArg(c)->GetCardV2Info())); return true; }
// ROM 0x0004f370 Version__11TCardPCMCIAFv
static bool	Glue_CardVersion(void*, ARMTrapContext& c)			{ c.Return((uint32_t) CardArg(c)->Version()); return true; }
// ROM 0x0004fb98 GetNumOfCardFunctions__11TCardPCMCIAFv
static bool	Glue_GetNumOfCardFunctions(void*, ARMTrapContext& c)	{ c.Return((uint32_t) CardArg(c)->GetNumOfCardFunctions()); return true; }
// ROM 0x0004fba0 GetCardFunction__11TCardPCMCIAFCUl
static bool	Glue_GetCardFunction(void*, ARMTrapContext& c)		{ c.Return(FunctionMirror(CardArg(c)->GetCardFunction(c.Arg(1)))); return true; }
// ROM 0x0004fb6c GetCardConfiguration__11TCardPCMCIAFCUl
static bool	Glue_GetCardConfiguration(void*, ARMTrapContext& c)	{ c.Return(ConfigMirror(CardArg(c)->GetCardConfiguration(c.Arg(1)))); return true; }


/*------------------------------------------------------------------------------
	T h e   p r o x y
	A card handler part's instance, as the card server sees it: each method
	the ARM instance's dispatch table slot (4 RecognizeCard ... 19
	CardSpecific, as CardHandler.h declares them).
------------------------------------------------------------------------------*/

class TCardHandlerARM : public TCardHandler
{
public:
	NewtonErr	Call(int slot, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0)
				{
					uint32_t args[3] = { a, b, c };
					return (NewtonErr) (int32_t) ARMCallSlot(this, slot, args, 3);
				}
	NewtonErr	RecognizeCard(TCardSocket* socket, TCardPCMCIA* card)				{ return Call(4, SocketMirror(socket), CardMirror(card)); }
	NewtonErr	ParseUnrecognizedCard(TCardSocket* socket, TCardPCMCIA* card)		{ return Call(5, SocketMirror(socket), CardMirror(card)); }
	NewtonErr	InstallServices(TCardSocket* socket, TCardPCMCIA* card, ULong config)	{ return Call(6, SocketMirror(socket), CardMirror(card), (uint32_t) config); }
	NewtonErr	RemoveServices(void)												{ return Call(7); }
	NewtonErr	SuspendServices(void)												{ return Call(8); }
	NewtonErr	ResumeServices(TCardSocket* socket, TCardPCMCIA* card, ULong config)	{ return Call(9, SocketMirror(socket), CardMirror(card), (uint32_t) config); }
	NewtonErr	EmergencyShutdown(void)												{ return Call(10); }
	NewtonErr	FormatCIS(TCardSocket* socket, TCardPCMCIA* card)					{ return Call(11, SocketMirror(socket), CardMirror(card)); }
	char*		CardIdString(TCardPCMCIA* card)
				{
					uint32_t s = (uint32_t) Call(12, CardMirror(card));
					if (s == 0 || !ARMReadCString(s, fIdString, sizeof(fIdString)))
						return nil;
					return fIdString;
				}
	ULong		CardStatus(void)													{ return (ULong) (uint32_t) Call(13); }
	ULong		GetNumberOfDevice(void)												{ return (ULong) (uint32_t) Call(14); }
	void		GetDeviceInfo(ULong device, ULong* type, TObjectId* phys, void** driverInfo, ULong* offset, ULong* size)
				{
					uint32_t out = ARMAlloc(20, true);
					uint32_t args[6] = { (uint32_t) device, out, out + 4, out + 8, out + 12, out + 16 };
					ARMCallSlot(this, 15, args, 6);
					uint32_t v[5];
					for (int i = 0; i < 5; i++)
						ARMRead32(out + i * 4, &v[i]);
					ARMFree(out);
					if (type != nil)		*type = v[0];
					if (phys != nil)		*phys = (TObjectId) v[1];
					// (an ARM address: the device's driver, as the ARM code knows it)
					if (driverInfo != nil)	*driverInfo = (void*) (uintptr_t) v[2];
					if (offset != nil)		*offset = v[3];
					if (size != nil)		*size = v[4];
				}
	void		SetCardServerPort(TObjectId port)									{ Call(16, (uint32_t) port); }
	void		SetRemovableHandler(Boolean removable)								{ Call(17, (uint32_t) removable); }
	Boolean		GetRemovableHandler(void)											{ return (Boolean) (Call(18) & 0xff); }
	long		CardSpecific(ULong selector, void* ptr, ULong something)
				{
					uint32_t p = 0;
					if (selector == kCardSpecificATASetPartitionInfo)
						p = PartitionMirror((TATAPartitionInfo*) ptr);
					else if (ptr != nil)
						fprintf(stderr, "[armcpu] CardSpecific %lu with a host pointer: NOT YET\n", (unsigned long) selector);
					return (long) Call(19, (uint32_t) selector, p, (uint32_t) something);
				}

	char		fIdString[64];
};

static TProtocol*	MakeCardHandlerARM(void* at)	{ return new (at) TCardHandlerARM; }
static size_t		SizeCardHandlerARM(void)		{ return sizeof(TCardHandlerARM); }


void
InstallARMCardHandlers(void)
{
	RegisterARMProxyKind("TCardHandler", MakeCardHandlerARM, SizeCardHandlerARM);
	ARMRegisterGlue("SocketNumber__11TCardSocketFv", Glue_SocketNumber);
	ARMRegisterGlue("AttributeMemBaseAddr__11TCardSocketFv", Glue_AttributeMemBaseAddr);
	ARMRegisterGlue("CommonMemBaseAddr__11TCardSocketFv", Glue_CommonMemBaseAddr);
	ARMRegisterGlue("IOBaseAddr__11TCardSocketFv", Glue_IOBaseAddr);
	ARMRegisterGlue("GetCardManufacturer__11TCardPCMCIACFv", Glue_GetCardManufacturer);
	ARMRegisterGlue("GetCardProduct__11TCardPCMCIACFv", Glue_GetCardProduct);
	ARMRegisterGlue("GetCardV1String3__11TCardPCMCIACFv", Glue_GetCardV1String3);
	ARMRegisterGlue("GetCardV1String4__11TCardPCMCIACFv", Glue_GetCardV1String4);
	ARMRegisterGlue("GetCardV2Vendor__11TCardPCMCIACFv", Glue_GetCardV2Vendor);
	ARMRegisterGlue("GetCardV2Info__11TCardPCMCIACFv", Glue_GetCardV2Info);
	ARMRegisterGlue("Version__11TCardPCMCIAFv", Glue_CardVersion);
	ARMRegisterGlue("GetNumOfCardFunctions__11TCardPCMCIAFv", Glue_GetNumOfCardFunctions);
	ARMRegisterGlue("GetCardFunction__11TCardPCMCIAFCUl", Glue_GetCardFunction);
	ARMRegisterGlue("GetCardConfiguration__11TCardPCMCIAFCUl", Glue_GetCardConfiguration);
}
