/*
	File:		pcmcia/CardPCMCIA.cpp

	Contains:	What a card's CIS is parsed into (the DDK's CardPCMCIA.h):
				TCardPCMCIA and its devices (TCardDevice), configurations
				(TCardConfiguration), packages (TCardPackage), functions
				(TCardFunction) and long links (TCardLongLink), with
				SetString and SetStringsBlock.

				The ROM lays TCardDevice out as 0x20 bytes where the DDK's
				header, compiled today, packs it into 0x1c: Apple's ARM C++
				gave the three one-bit fields at +0x10 a whole word, so the
				characters after them start at +0x14 (fDeviceType) and run
				to fInterleave at +0x1c.  The field names are the DDK's; only
				the offsets differ (verify-report.txt's TCardDevice).

				A multi-function card has a CIS for each function: the first
				TCardPCMCIA - the global one - keeps the list, fCISs, whose
				entry 0 is itself, and each function's own TCardPCMCIA
				shares the global one's VERS_1 strings (RemoveFields leaves
				them to their owner).

	Reconstructed from the MP2x00 US ROM (0x0004ee04-0x0004feb4); each
	function cites its origin.
*/

#include "CardPCMCIA.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "NewtErrors.h"
#include "host/RomBugs.h"

#include <string.h>

// (the ROM's are pointers in its initialised data to these strings)
const char*	kNewtOSString = "NewtOS";			// ROM 0x0c100974 kNewtOSString
const char*	kArmCPU610String = "Arm610";		// ROM 0x0c100978 kArmCPU610String
const char*	kNullString = "";					// ROM 0x0c10097c kNullString


// ROM 0x0004ee04 SetString__FRPcPCc
// A copy of srcStr in desStr's place (what was there given back).
char*
SetString(char*& desStr, const char* srcStr)
{
	delete[] desStr;
	desStr = nil;
	if (srcStr != nil)
	{
		desStr = new char[strlen(srcStr) + 1];
		strcpy(desStr, srcStr);
	}
	return desStr;
}


// ROM 0x0004ee54 SetStringsBlock__FRPcPCcCUl
// A copy of a block of C strings ending in 0xFF (VERS_1's), no more than
// maxSize bytes of it, always ending "\0\xFF".
// ROM BUG (fixed): a block that runs past maxSize comes out as maxSize + 3
// bytes, the copy reading past the end of the source by as much.  The fix
// cuts the block at maxSize (two bytes at least, for the ending) and
// copies no more of the source than there is - up to its 0xFF, which also
// keeps a block that fits from reading the byte after it; what a block
// that fits comes out as is the ROM's.
char*
SetStringsBlock(char*& desBlock, const char* srcBlock, const ULong maxSize)
{
	delete[] desBlock;
	desBlock = nil;
	if (srcBlock != nil && maxSize != 0)
	{
		ULong byteCount = 0;
		const char* s = srcBlock;
		do
		{
			ULong length = strlen(s);
			ULong through = byteCount + length + 1;
			if (through == maxSize || ((byteCount = maxSize + 2), through < maxSize && ((byteCount = through), *s != (char) 0xFF)))
				byteCount = through + 1;
			s += length + 1;
		} while (byteCount < maxSize && *s != (char) 0xFF);
		ULong copied = byteCount;
		if (RomBugFixed())
		{
			if (byteCount > maxSize)
				byteCount = maxSize;
			if (byteCount < 2)
				byteCount = 2;
			ULong source = 0;		// the source's bytes, its 0xFF included
			while (source < maxSize && srcBlock[source] != (char) 0xFF)
				source++;
			if (source < maxSize)
				source++;
			copied = byteCount < source ? byteCount : source;
		}
		desBlock = new char[byteCount];
		BlockMove(srcBlock, desBlock, copied);
		if (copied < byteCount)
			memset(desBlock + copied, 0, byteCount - copied);
		desBlock[byteCount - 1] = (char) 0xFF;
		desBlock[byteCount - 2] = 0;
	}
	return desBlock;
}


/*------------------------------------------------------------------------------
	T C a r d D e v i c e
------------------------------------------------------------------------------*/

// ROM 0x0004fe60 __ct__11TCardDeviceFv
// ROM QUIRK kept: the geometry (fBusSize on) is left as the allocator found
// it; IdentifyCard and CISTPL_DEVICE_GEO fill it in.
TCardDevice::TCardDevice()
{
	fStartOffset = 0;
	fnsecSpeed = 0;
	fSize = 0;
	fVcc = 0;
	fWPS = 0;
	fMwait = 0;
	fAttributeMemoryDescr = 0;
	fDeviceType = 0;
	fJedecMfr = 0;
	fJedecMfrInfo = 0;
}


// ROM 0x0004ef1c __ct__11TCardDeviceFUlN21UcN34T1
TCardDevice::TCardDevice(TNanoSecond nsecSpeed, ULong Size, TMicroVolt Vcc, Boolean Mwait,
						 Boolean AttributeMemoryDescr, Boolean WPS, UChar DeviceType, ULong startOffset)
{
	fSize = Size;
	fnsecSpeed = nsecSpeed;
	fStartOffset = startOffset;
	fVcc = Vcc;
	fWPS = WPS;
	fMwait = Mwait;
	fAttributeMemoryDescr = AttributeMemoryDescr;
	fDeviceType = DeviceType;
	fJedecMfr = 0;
	fJedecMfrInfo = 0;
	fReadBlockSize = 0;
	fEraseBlockSize = 0;
	fBusSize = 0;
	fInterleave = 0;
	fPart = 0;
	fWriteBlockSize = 0;
}


// ROM 0x0004efd4 __dt__11TCardDeviceFv
TCardDevice::~TCardDevice()
{ }


/*------------------------------------------------------------------------------
	T C a r d P a c k a g e
------------------------------------------------------------------------------*/

// ROM 0x0004efe0 __ct__12TCardPackageFv
TCardPackage::TCardPackage()
{
	fVersion = 0;
	fLength = 0;
	fAddress = 0;
	fReserved1 = 0;
	fReserved0 = 0;
	fAttribute = 0;
	fType = 0;
	fOSType = nil;
	fCPUType = nil;
	fName = nil;
}


// ROM 0x0004f034 __dt__12TCardPackageFv
TCardPackage::~TCardPackage()
{
	delete[] fName;
	delete[] fCPUType;
	delete[] fOSType;
}


char*		TCardPackage::SetName(char* srcStr)			{ return SetString(fName, srcStr); }		// ROM 0x0004f074 SetName__12TCardPackageFPc
const char*	TCardPackage::GetName() const				{ return fName != nil ? fName : kNullString; }		// ROM 0x0004f07c GetName__12TCardPackageCFv
char*		TCardPackage::SetCPUType(char* srcStr)		{ return SetString(fCPUType, srcStr); }		// ROM 0x0004f094 SetCPUType__12TCardPackageFPc
const char*	TCardPackage::GetCPUType() const			{ return fCPUType != nil ? fCPUType : kNullString; }	// ROM 0x0004f09c GetCPUType__12TCardPackageCFv
char*		TCardPackage::SetOSType(char* srcStr)		{ return SetString(fOSType, srcStr); }		// ROM 0x0004f0b4 SetOSType__12TCardPackageFPc
const char*	TCardPackage::GetOSType() const				{ return fOSType != nil ? fOSType : kNullString; }	// ROM 0x0004f0bc GetOSType__12TCardPackageCFv


/*------------------------------------------------------------------------------
	T C a r d L o n g L i n k
------------------------------------------------------------------------------*/

// ROM 0x0004f0d4 __ct__13TCardLongLinkFv
TCardLongLink::TCardLongLink()
{
	Clear();
}


// ROM 0x0004f4b4 __dt__13TCardLongLinkFv
TCardLongLink::~TCardLongLink()
{ }


// ROM 0x0004f914 Clear__13TCardLongLinkFv
// No link yet, and this is the primary chain.
void
TCardLongLink::Clear()
{
	fLinkAddress = nil;
	fNoLongLinkTupleSeen = 0;
	fInAttributeMemory = 0;
	fPrimaryList = -1;
	fLongLinkTupleSeen = 0;
}


/*------------------------------------------------------------------------------
	T C a r d F u n c t i o n
------------------------------------------------------------------------------*/

// ROM 0x0004f108 __ct__13TCardFunctionFv
TCardFunction::TCardFunction()
{
	fFuncId = 0xFF;
	fFuncIdSysInits = 0;
	fConfigEntryNumberStart = 0;
	fConfigEntryNumberEnd = 0;
	fRegisterBaseAddress = 0;
}


// ROM 0x0004f158 __dt__13TCardFunctionFv
TCardFunction::~TCardFunction()
{
	void* ext;
	while ((ext = fFunctExts.At(fFunctExts.Count() - 1)) != nil)
	{
		fFunctExts.RemoveElementsAt(fFunctExts.Count() - 1, 1);
		DisposPtr((Ptr) ext);
	}
}


// ROM 0x0004f1dc GetNumOfFuncExts__13TCardFunctionFv
ULong
TCardFunction::GetNumOfFuncExts()
{
	return fFunctExts.Count();
}


// ROM 0x0004f1e4 GetFuncExt__13TCardFunctionFCUl
TCardFuncExt*
TCardFunction::GetFuncExt(const ULong functExtNumber)
{
	TCardFuncExt* ext = nil;
	if (functExtNumber < (ULong) fFunctExts.Count())
		ext = (TCardFuncExt*) fFunctExts.At(functExtNumber);
	return ext;
}


// ROM 0x0004f210 AddFuncExt__13TCardFunctionFCUlPCUc
// A copy of a CISTPL_FUNCE's data, its size in its first byte.
NewtonErr
TCardFunction::AddFuncExt(const ULong dataSize, const UChar* data)
{
	NewtonErr err = noErr;
	UChar* ext = (UChar*) NewPtr(dataSize + 1);
	if (ext == nil)
		err = kError_No_Memory;
	else
	{
		ext[0] = (UChar) dataSize;
		BlockMove(data, ext + 1, dataSize);
		fFunctExts.InsertAt(fFunctExts.Count(), ext);
	}
	return err;
}


/*------------------------------------------------------------------------------
	T C a r d C o n f i g u r a t i o n
------------------------------------------------------------------------------*/

// ROM 0x0004fa18 __ct__18TCardConfigurationFv
TCardConfiguration::TCardConfiguration()
{
	Clear();
}


// ROM 0x0004fd70 __dt__18TCardConfigurationFv
TCardConfiguration::~TCardConfiguration()
{ }


// ROM 0x0004fd7c Clear__18TCardConfigurationFv
// ROM QUIRK kept: the interface type is set to 'p' (0x70), no interface a
// CISTPL_CFTABLE_ENTRY can name, until an entry's TPCE_IF says what it is.
void
TCardConfiguration::Clear()
{
	fConfigurationNumber = 0;
	fInterfaceType = 'p';
	fFeatureSelection = 0;
	fActiveBits = 0;
	for (UChar i = 0; i < kPowerDescrValues; i++)
	{
		fVpp2[i] = 0;
		fVpp1[i] = 0;
		fVcc[i] = 0;
		fVpp2Attr[i] = 0;
		fVpp1Attr[i] = 0;
		fVccAttr[i] = 0;
	}
	for (UChar i = 0; i < kNumIOBlocks; i++)
	{
		fIoAddresses[i] = 0;
		fIoLengths[i] = 0;
	}
	for (UChar i = 0; i < kNumMemoryBlocks; i++)
	{
		fMemAddresses[i] = 0;
		fMemLengths[i] = 0;
		fHostAddresses[i] = 0;
	}
	fWaitTimeNSecs = 0;
	fRdyBsyTimeNSecs = 0;
	fNumOfIOSpace = 0;
	fIoAddrLines = 0;
	fIo8BitOK = 0;
	fIo16BitOK = 0;
	fInterruptInfo = 0;
	fInterruptShare = 0;
	fNumOfMemMap = 0;
	fMiscBits = 0;
}


// ROM 0x0004fe38 __as__18TCardConfigurationFRC18TCardConfiguration
// (a BlockMove of the whole object in the ROM; it has no pointers)
TCardConfiguration&
TCardConfiguration::operator=(const TCardConfiguration& rhs)
{
	BlockMove(&rhs, this, sizeof(TCardConfiguration));
	return *this;
}


/*------------------------------------------------------------------------------
	T C a r d P C M C I A
------------------------------------------------------------------------------*/

// ROM 0x0004f274 __ct__11TCardPCMCIAFv
TCardPCMCIA::TCardPCMCIA()
{
	ClearFields();
}


// ROM 0x0004f2d8 __dt__11TCardPCMCIAFv
TCardPCMCIA::~TCardPCMCIA()
{
	RemoveFields();
}


// ROM 0x0004f34c Clear__11TCardPCMCIAFv
NewtonErr
TCardPCMCIA::Clear()
{
	RemoveFields();
	ClearFields();
	return noErr;
}


// ROM 0x0004f370 Version__11TCardPCMCIAFv
ULong
TCardPCMCIA::Version()
{
	return 0x20200;
}


// ROM 0x0004f37c ClearFields__11TCardPCMCIAFv
NewtonErr
TCardPCMCIA::ClearFields()
{
	fSocketNumber = 0xFFFFFFFF;
	fProductName = nil;
	fManufacturerName = nil;
	fV1String4 = nil;
	fV1String3 = nil;
	fV2Info = nil;
	fV2Vendor = nil;
	fTotalDeviceSize = 0;
	fNumOfDevice = 0;
	fNumOfConfigEntry = 0;
	fNumOfPackage = 0;
	fNumOfUnknownTuples = 0;
	fRegisterBaseAddress = 0;
	fRegistersPresent = 0;
	fConfigurationLastEntryNumber = 0;
	fFirstDataByteAddress = 0;
	fVendorSpecificV2Bytes[1] = 0;
	fVendorSpecificV2Bytes[0] = 0;
	fManufactureIdInfo = 0;
	fManufactureId = 0;
	fNumOfFuncExt = 0;
	fFunctionSysInit = 0;
	fFunctionId = 0;
	// (the ROM clears the ten flags in one: the word's top ten bits)
	fNoAttrMem = 0;
	fBadCIS = 0;
	fAttrMemWrable = 0;
	fTooManyUnknownTuples = 0;
	fFunctionIdAvail = 0;
	f16BitOnlyCard = 0;
	f8BitOnlyCard = 0;
	fNoCIS = 0;
	fGlobalCIS = 0;
	fFuncSpecificCIS = 0;
	for (ULong i = 0; i < kNumFuncExtTuples; i++)
		for (ULong j = 0; j < kFuncExtSize; j++)
			fFuncExt[i][j] = 0;
	fV1Major = 0;
	fV1Minor = 0;
	fCISs = nil;
	fCurrentCISNumber = 0;
	return noErr;
}


// Every TCardDevice of a list given back, last first.
static void
DeleteDevices(CList* list)
{
	TCardDevice* device;
	while ((device = (TCardDevice*) list->At(list->Count() - 1)) != nil)
	{
		list->RemoveElementsAt(list->Count() - 1, 1);
		delete device;
	}
}


// ROM 0x0004f4c0 RemoveFields__11TCardPCMCIAFv
// Everything the parse made given back: the strings it owns, the devices,
// the other-condition device lists, the configurations, the packages and
// the functions - and, for the global CIS, the function-specific ones.
NewtonErr
TCardPCMCIA::RemoveFields()
{
	delete[] fV2Vendor;
	delete[] fV2Info;
	DeleteDevices(&fDevices);
	CList* list;
	while ((list = (CList*) fDeviceOCs.At(fDeviceOCs.Count() - 1)) != nil)
	{
		DeleteDevices(list);
		fDeviceOCs.RemoveElementsAt(fDeviceOCs.Count() - 1, 1);
		delete list;
	}
	while ((list = (CList*) fDeviceOAs.At(fDeviceOAs.Count() - 1)) != nil)
	{
		DeleteDevices(list);
		fDeviceOAs.RemoveElementsAt(fDeviceOAs.Count() - 1, 1);
		delete list;
	}
	TCardConfiguration* configuration;
	while ((configuration = (TCardConfiguration*) fConfigurations.At(fConfigurations.Count() - 1)) != nil)
	{
		fConfigurations.RemoveElementsAt(fConfigurations.Count() - 1, 1);
		delete configuration;
	}
	TCardPackage* package;
	while ((package = (TCardPackage*) fPackages.At(fPackages.Count() - 1)) != nil)
	{
		fPackages.RemoveElementsAt(fPackages.Count() - 1, 1);
		delete package;
	}
	TCardFunction* function;
	while ((function = (TCardFunction*) fCardFunctions.At(fCardFunctions.Count() - 1)) != nil)
	{
		fCardFunctions.RemoveElementsAt(fCardFunctions.Count() - 1, 1);
		delete function;
	}
	// the VERS_1 strings are the global CIS's, which a function's shares
	TCardPCMCIA* global = GetCardCIS(0);
	if (this == global || global->fManufacturerName != fManufacturerName)
		delete[] fManufacturerName;
	if (this == global || global->fProductName != fProductName)
		delete[] fProductName;
	if (this == global || global->fV1String3 != fV1String3)
		delete[] fV1String3;
	if (this == global || global->fV1String4 != fV1String4)
		delete[] fV1String4;
	if (this == global && fCISs != nil)
	{
		TCardPCMCIA* cis;
		while ((cis = (TCardPCMCIA*) fCISs->At(fCISs->Count() - 1)) != nil)
		{
			fCISs->RemoveElementsAt(fCISs->Count() - 1, 1);
			if (cis != global)
				delete cis;
		}
		delete fCISs;
		fCISs = nil;
	}
	return noErr;
}


char*		TCardPCMCIA::SetCardManufacturer(char* srcStr)	{ return SetString(fManufacturerName, srcStr); }	// ROM 0x0004f88c SetCardManufacturer__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardManufacturer() const		{ return fManufacturerName != nil ? fManufacturerName : kNullString; }	// ROM 0x0004f894 GetCardManufacturer__11TCardPCMCIACFv
char*		TCardPCMCIA::SetCardProduct(char* srcStr)		{ return SetString(fProductName, srcStr); }		// ROM 0x0004f8ac SetCardProduct__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardProduct() const				{ return fProductName != nil ? fProductName : kNullString; }	// ROM 0x0004f8b4 GetCardProduct__11TCardPCMCIACFv
char*		TCardPCMCIA::SetCardV1String3(char* srcStr)		{ return SetString(fV1String3, srcStr); }		// ROM 0x0004f8cc SetCardV1String3__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardV1String3() const			{ return fV1String3 != nil ? fV1String3 : kNullString; }	// ROM 0x0004f8d4 GetCardV1String3__11TCardPCMCIACFv
char*		TCardPCMCIA::SetCardV1String4(char* srcStr)		{ return SetString(fV1String4, srcStr); }		// ROM 0x0004f8ec SetCardV1String4__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardV1String4() const			{ return fV1String4 != nil ? fV1String4 : kNullString; }	// ROM 0x0004f8f4 GetCardV1String4__11TCardPCMCIACFv
char*		TCardPCMCIA::SetCardV2Vendor(char* srcStr)		{ return SetString(fV2Vendor, srcStr); }		// ROM 0x0004f90c SetCardV2Vendor__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardV2Vendor() const			{ return fV2Vendor != nil ? fV2Vendor : kNullString; }	// ROM 0x0004f930 GetCardV2Vendor__11TCardPCMCIACFv
char*		TCardPCMCIA::SetCardV2Info(char* srcStr)		{ return SetString(fV2Info, srcStr); }			// ROM 0x0004f948 SetCardV2Info__11TCardPCMCIAFPc
const char*	TCardPCMCIA::GetCardV2Info() const				{ return fV2Info != nil ? fV2Info : kNullString; }		// ROM 0x0004f950 GetCardV2Info__11TCardPCMCIACFv


// ROM 0x0004f968 AddCardDevice__11TCardPCMCIAFP11TCardDevice
TCardDevice*
TCardPCMCIA::AddCardDevice(TCardDevice* device)
{
	fDevices.InsertAt(fDevices.Count(), device);
	return device;
}


// ROM 0x0004f98c GetCardDevice__11TCardPCMCIAFCUl
TCardDevice*
TCardPCMCIA::GetCardDevice(const ULong deviceNumber)
{
	TCardDevice* device = nil;
	if (deviceNumber < (ULong) fDevices.Count())
		device = (TCardDevice*) fDevices.At(deviceNumber);
	return device;
}


// ROM 0x0004f9b8 GetNumOfCISs__11TCardPCMCIAFv
ULong
TCardPCMCIA::GetNumOfCISs()
{
	return fCISs == nil ? 1 : fCISs->Count();
}


// ROM 0x0004f9cc GetCardCIS__11TCardPCMCIAFCUl
// The CIS of a function (0 the global one, which is this when there is
// only one), made the current one.
TCardPCMCIA*
TCardPCMCIA::GetCardCIS(const ULong cisNumber)
{
	if (fCISs == nil)
		return this;
	if (cisNumber < (ULong) fCISs->Count())
	{
		TCardPCMCIA* cis = (TCardPCMCIA*) fCISs->At(cisNumber);
		if (cis != nil)
			fCurrentCISNumber = (UChar) cisNumber;
		return cis;
	}
	return nil;
}


// ROM 0x0004fa4c AddCardOtherCondDeviceList__11TCardPCMCIAFCUl
// ==> the new list's number, or kError_No_Memory.
Long
TCardPCMCIA::AddCardOtherCondDeviceList(const ULong forAttrMemory)
{
	Long result = kError_No_Memory;
	CList* list = new CList;
	if (list != nil)
	{
		CList* lists = forAttrMemory == 0 ? &fDeviceOCs : &fDeviceOAs;
		lists->InsertAt(lists->Count(), list);
		result = lists->Count() - 1;
	}
	return result;
}


// ROM 0x0004faa4 AddCardOtherCondDevice__11TCardPCMCIAFCUlT1P11TCardDevice
NewtonErr
TCardPCMCIA::AddCardOtherCondDevice(const ULong forAttrMemory, const ULong listNumber, TCardDevice* device)
{
	NewtonErr err = noErr;
	CList* lists = forAttrMemory == 0 ? &fDeviceOCs : &fDeviceOAs;
	if (listNumber < (ULong) lists->Count())
	{
		CList* list = (CList*) lists->At(listNumber);
		list->InsertAt(list->Count(), device);
	}
	else
		err = kError_Bad_Parameters;
	return err;
}


// ROM 0x0004faf8 AddCardPackage__11TCardPCMCIAFP12TCardPackage
TCardPackage*
TCardPCMCIA::AddCardPackage(TCardPackage* package)
{
	fPackages.InsertAt(fPackages.Count(), package);
	return package;
}


// ROM 0x0004fb1c GetCardPackage__11TCardPCMCIAFCUl
TCardPackage*
TCardPCMCIA::GetCardPackage(const ULong packageNumber)
{
	TCardPackage* package = nil;
	if (packageNumber < (ULong) fPackages.Count())
		package = (TCardPackage*) fPackages.At(packageNumber);
	return package;
}


// ROM 0x0004fb48 AddCardConfiguration__11TCardPCMCIAFP18TCardConfiguration
TCardConfiguration*
TCardPCMCIA::AddCardConfiguration(TCardConfiguration* configuration)
{
	fConfigurations.InsertAt(fConfigurations.Count(), configuration);
	return configuration;
}


// ROM 0x0004fb6c GetCardConfiguration__11TCardPCMCIAFCUl
TCardConfiguration*
TCardPCMCIA::GetCardConfiguration(const ULong configNumber)
{
	TCardConfiguration* configuration = nil;
	if (configNumber < (ULong) fConfigurations.Count())
		configuration = (TCardConfiguration*) fConfigurations.At(configNumber);
	return configuration;
}


// ROM 0x0004fb98 GetNumOfCardFunctions__11TCardPCMCIAFv
ULong
TCardPCMCIA::GetNumOfCardFunctions()
{
	return fCardFunctions.Count();
}


// ROM 0x0004fba0 GetCardFunction__11TCardPCMCIAFCUl
TCardFunction*
TCardPCMCIA::GetCardFunction(const ULong funcNumber)
{
	TCardFunction* function = nil;
	if (funcNumber < (ULong) fCardFunctions.Count())
		function = (TCardFunction*) fCardFunctions.At(funcNumber);
	return function;
}


// ROM 0x0004fbcc AddCardFunction__11TCardPCMCIAFP13TCardFunction
TCardFunction*
TCardPCMCIA::AddCardFunction(TCardFunction* cardFunction)
{
	fCardFunctions.InsertAt(fCardFunctions.Count(), cardFunction);
	return cardFunction;
}


// ROM 0x0004fbf0 AddFuncSpecificCIS__11TCardPCMCIAFv
// A new TCardPCMCIA for the next function's CIS, added to the global one's
// list (made, with the global one as its first entry, the first time): it
// takes the global CIS's card-wide flags, socket, manufacturer id and
// VERS_1 (the strings shared, not copied).
NewtonErr
TCardPCMCIA::AddFuncSpecificCIS()
{
	NewtonErr err = kError_No_Memory;
	if (fCISs == nil)
	{
		fCISs = new CList;
		if (fCISs == nil)
			return kError_No_Memory;
		fCISs->InsertAt(fCISs->Count(), this);
	}
	TCardPCMCIA* cis = new TCardPCMCIA;
	if (cis != nil)
	{
		fCISs->InsertAt(fCISs->Count(), cis);
		err = noErr;
		cis->fNoAttrMem = fNoAttrMem;
		cis->fBadCIS = fBadCIS;
		cis->fAttrMemWrable = fAttrMemWrable;
		cis->f16BitOnlyCard = f16BitOnlyCard;
		cis->f8BitOnlyCard = f8BitOnlyCard;
		cis->fNoCIS = fNoCIS;
		cis->fSocketNumber = fSocketNumber;
		cis->fManufactureId = fManufactureId;
		cis->fManufactureIdInfo = fManufactureIdInfo;
		cis->fManufacturerName = fManufacturerName;
		cis->fProductName = fProductName;
		cis->fV1String3 = fV1String3;
		cis->fV1String4 = fV1String4;
		cis->fV1Major = fV1Major;
		cis->fV1Minor = fV1Minor;
		cis->fFuncSpecificCIS = -1;
		cis->fCISs = fCISs;
		cis->fCurrentCISNumber = (UChar) (fCISs->Count() - 1);
	}
	return err;
}
