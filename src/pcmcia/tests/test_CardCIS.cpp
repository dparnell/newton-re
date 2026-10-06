// A card's CIS (pcmcia/CardCISIterator.h, PCMCIA20Parser.h, the DDK's
// CardPCMCIA.h) read from the host's card (hal/host/HostCard.h): the blank
// flash card HostCardCreate makes parses into one 2 MB flash device of the
// Intel Series 2 kind; a CIS of our own with a long link into common
// memory, a configuration, an entry with power, timing, I/O, interrupt and
// memory, a function id and extension, a checksum and an Apple package
// tuple parses into all of that; and the tuples the iterator walks are the
// ones the card holds.  Run as the kernel services task (the parser catches
// aborts, and exceptions want a task).

#include "CardSocket.h"
#include "HostCard.h"
#include "CardCISIterator.h"
#include "PCMCIA20Parser.h"
#include "OSErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"
#include "host/RomBugs.h"

#include <stdio.h>
#include <string.h>
#include <initializer_list>
#include <stdlib.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char*	kCardFile = "test_CardCIS.card";

// A growable byte array (<vector> is not for Newton code: it reaches
// <locale.h>, which intl/Locale.h shadows)
struct Bytes
{
	unsigned char	fData[0x800];
	size_t			fSize;
	Bytes(size_t n = 0, unsigned char fill = 0) : fSize(n)	{ memset(fData, fill, sizeof(fData)); }
	size_t	size() const					{ return fSize; }
	void	push_back(unsigned char b)		{ fData[fSize++] = b; }
	unsigned char&	operator[](size_t i)	{ return fData[i]; }
	unsigned char	operator[](size_t i) const	{ return fData[i]; }
};

static void
PutWord(unsigned char* v, size_t at, unsigned long w)
{
	v[at] = (unsigned char) (w >> 24);
	v[at + 1] = (unsigned char) (w >> 16);
	v[at + 2] = (unsigned char) (w >> 8);
	v[at + 3] = (unsigned char) w;
}


// A card image in Einstein's container: the common memory (as given, the
// rest 0xFF) and the CIS.
static void
WriteCard(const Bytes& common, const Bytes& cis)
{
	size_t total = 0x200000 + cis.size() + 1 + kHostCardImageInfoSize;
	unsigned char* file = (unsigned char*) malloc(total);
	memset(file, 0xFF, 0x200000);
	memcpy(file, common.fData, common.size());
	size_t cisStart = 0x200000;
	memcpy(file + cisStart, cis.fData, cis.size());
	size_t nameStart = cisStart + cis.size();
	file[nameStart] = 0;
	size_t info = nameStart + 1;
	memset(file + info, 0, kHostCardImageInfoSize);
	PutWord(file, info + 0, 1);
	PutWord(file, info + 4, nameStart);
	PutWord(file, info + 12, nameStart);
	PutWord(file, info + 16, cis.size());
	PutWord(file, info + 20, cisStart);
	PutWord(file, info + 24, 0x200000);
	PutWord(file, info + 28, 0);
	PutWord(file, info + 32, 5);
	PutWord(file, info + 36, 1);
	memcpy(&file[info + 40], "TLinearCard", 12);
	FILE* f = fopen(kCardFile, "wb");
	fwrite(file, 1, total, f);
	fclose(f);
	free(file);
}


static void
TestBlankCard(TCardSocket* socket)
{
	EXPECT(HostCardCreate(kCardFile, 4, "Blank") == noErr);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);

	TCardCISIterator iterator;
	EXPECT(iterator.Init(socket) == noErr);
	EXPECT(iterator.Version() == 0x20200);
	UChar codes[8];
	int n = 0;
	iterator.fSearchCode = 0xFF;
	UChar first = true;
	while (n < 8 && iterator.GetTuple(first) == noErr)
	{
		first = false;
		codes[n++] = iterator.fTupleCode;
	}
	// DEVICE, JEDEC_C, DEVICE_GEO, VERS_1; the END goes on to common memory,
	// which has no CIS (kError_Card_No_CIS ends it)
	EXPECT(n == 4 && codes[0] == 0x01 && codes[1] == 0x18 && codes[2] == 0x1E && codes[3] == 0x15);

	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	EXPECT(parser.Version() == 0x20200);
	EXPECT(parser.ParsePCCardCIS(&card, socket) == noErr);
	EXPECT(!card.fBadCIS && !card.fNoAttrMem && !card.f16BitOnlyCard);
	EXPECT(card.fNumOfDevice == 1 && card.fTotalDeviceSize == 0x400000);
	TCardDevice* device = card.GetCardDevice(0);
	EXPECT(device != nil);
	if (device != nil)
	{
		EXPECT(device->fDeviceType == 5 && device->fSize == 0x400000 && device->fStartOffset == 0);
		EXPECT(device->fnsecSpeed == 150 && device->fVcc == 5000000 && !device->fAttributeMemoryDescr);
		EXPECT(device->fJedecMfr == 0x89 && device->fJedecMfrInfo == 0xA0);
		EXPECT(device->fBusSize == 2 && device->fEraseBlockSize == 0x11 && device->fInterleave == 1);
	}
	EXPECT(card.fV1Major == 4 && card.fV1Minor == 1);
	EXPECT(strcmp(card.GetCardManufacturer(), "Newton host") == 0);
	EXPECT(strcmp(card.GetCardProduct(), "Blank") == 0);
	EXPECT(strcmp(card.GetCardV1String3(), "") == 0);
	EXPECT(card.GetNumOfCISs() == 1 && card.GetCardCIS(0) == &card);
	HostCardRemove(0);
}


// Tuples as they go into the CIS
static void
Tuple(Bytes& cis, unsigned char code, std::initializer_list<unsigned char> data)
{
	cis.push_back(code);
	cis.push_back((unsigned char) data.size());
	for (unsigned char b : data)
		cis.push_back(b);
}


static void
TestRichCIS(TCardSocket* socket)
{
	Bytes cis;
	Tuple(cis, 0x01, { 0x53, 0x0E, 0xFF });							// DEVICE: flash, 150 ns, 4 MB
	cis.push_back(0x00);											// a NULL
	Tuple(cis, 0x1E, { 0x02, 0x11, 0x01, 0x01, 0x01, 0x01 });		// DEVICE_GEO
	Tuple(cis, 0x20, { 0x4D, 0x00, 0x34, 0x12 });					// MANFID 0x004D, 0x1234
	Tuple(cis, 0x21, { 0x02, 0x01 });								// FUNCID: serial, POST
	Tuple(cis, 0x22, { 0x00, 0x02, 0x0F, 0x5C });					// FUNCE
	Tuple(cis, 0x1A, { 0x01, 0x03, 0x00, 0x02, 0x03 });				// CONFIG: base 0x200, registers 0x303, last entry 3
	Tuple(cis, 0x1B, { 0xE1, 0x01,									// CFTABLE_ENTRY 0x21, default, interface: I/O
					   0x19,										//   power (1), I/O, IRQ, no memory
					   0x01, 0x55,									//   Vcc nominal: 5 V
					   0xE0, 0x60, 0xF8, 0x03, 0x00,				//   I/O: 8/16 bit; one range, 0x3F8, 1 long
					   0x30, 0xFF, 0xFF });							//   IRQ: level, mask 0xFFFF
	Tuple(cis, 0x8E, { 0xC8, 0x00, 0x00, 0x20,						// Apple's package tuple
					   0x01, 0x02, 0x00, 0x10, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00,
					   'P', 'k', 'g', 0, 'A', 'r', 'm', '6', '1', '0', 0, 'N', 'e', 'w', 't', 'O', 'S', 0 });
	Tuple(cis, 0x12, { 0x00, 0x00, 0x00, 0x00 });					// LONGLINK_C to common 0
	cis.push_back(0xFF);											// END

	// common memory: LINKTARGET, VERS_1, a CHECKSUM over the VERS_1, END
	Bytes chain;
	Tuple(chain, 0x13, { 'C', 'I', 'S' });
	Tuple(chain, 0x15, { 0x04, 0x01, 'A', 'c', 'm', 'e', 0, 'W', 'i', 'd', 'g', 'e', 't', 0, 0xFF });
	size_t checksumAt = chain.size();
	long relative = 5 - (long) checksumAt;
	unsigned length = (unsigned) (checksumAt - 5);
	Tuple(chain, 0x10, { (unsigned char) relative, (unsigned char) (relative >> 8), (unsigned char) length, 0x00, 0x00 });
	chain.push_back(0xFF);
	// the tuples are read a byte at (address ^ 3), as the machine's bus
	// has them; the checksum sums the bytes where they lie
	Bytes common(chain.size() + 8, 0xFF);
	for (size_t i = 0; i < chain.size(); i++)
		common[i ^ 3] = chain[i];
	unsigned sum = 0;
	for (size_t i = 5; i < checksumAt; i++)
		sum += common[i];
	common[(checksumAt + 6) ^ 3] = (unsigned char) sum;
	WriteCard(common, cis);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);

	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	NewtonErr err = parser.ParsePCCardCIS(&card, socket);
	EXPECT(err == noErr);
	if (err != noErr)
		fprintf(stderr, "  parse: %ld (parser %ld)\n", (long) err, (long) parser.fError);
	EXPECT(!card.fBadCIS && card.fNumOfUnknownTuples == 0);
	EXPECT(card.fNumOfDevice == 1 && card.fTotalDeviceSize == 0x400000);
	TCardDevice* device = card.GetCardDevice(0);
	if (device != nil)
		EXPECT(device->fBusSize == 2 && device->fEraseBlockSize == 0x11 && device->fInterleave == 1);
	EXPECT(card.fManufactureId == 0x004D && card.fManufactureIdInfo == 0x1234);
	EXPECT(card.fFunctionIdAvail && card.fFunctionId == 2 && card.fFunctionSysInit == 1);
	EXPECT(card.fNumOfFuncExt == 1 && card.fFuncExt[0][0] == 0x00 && card.fFuncExt[0][1] == 0x02);
	EXPECT(card.fRegisterBaseAddress == 0x200 && card.fRegistersPresent == 0x03 && card.fConfigurationLastEntryNumber == 3);
	EXPECT(card.fNumOfConfigEntry == 1);
	TCardConfiguration* config = card.GetCardConfiguration(0);
	EXPECT(config != nil);
	if (config != nil)
	{
		EXPECT(config->fConfigurationNumber == 0x21 && config->fInterfaceType == 1);
		EXPECT(config->fVcc[0] == 5000000);
		EXPECT(config->fNumOfIOSpace == 1 && config->fIoAddresses[0] == 0x3F8 && config->fIoLengths[0] == 1);
		EXPECT(config->fIoAddrLines == 0 && config->fIo8BitOK && config->fIo16BitOK);
		EXPECT(config->fInterruptInfo == 0x30 && config->fNumOfMemMap == 0);
	}
	EXPECT(card.GetNumOfCardFunctions() == 1);
	TCardFunction* function = card.GetCardFunction(0);
	if (function != nil)
	{
		EXPECT(function->fFuncId == 2 && function->fRegisterBaseAddress == 0x200);
		EXPECT(function->fConfigEntryNumberEnd == 0 && function->GetNumOfFuncExts() == 1);
	}
	EXPECT(card.fNumOfPackage == 1);
	TCardPackage* package = card.GetCardPackage(0);
	if (package != nil)
		EXPECT(strcmp(package->GetName(), "Pkg") == 0 && package->fAddress == 0x1000 && package->fLength == 0x2000 && package->fVersion == 3);
	// VERS_1 from common memory, past the long link, with its checksum
	EXPECT(strcmp(card.GetCardManufacturer(), "Acme") == 0 && strcmp(card.GetCardProduct(), "Widget") == 0);

	// a spoiled checksum
	common[(chain.size() - 2) ^ 3] ^= 0x55;
	WriteCard(common, cis);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	EXPECT(parser.ParsePCCardCIS(&card, socket) == kError_Card_Checksum);

	// a blank attribute memory
	WriteCard(common, Bytes(16, 0));
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	EXPECT(parser.ParsePCCardCIS(&card, socket) == kError_Card_Blank_CIS && card.fBadCIS);
	HostCardRemove(0);
}


// ROM BUGS (fixed): a DEVICE_GEO naming more devices than the DEVICE has
// wrote through a nil one, and an entry's I/O ranges past the eighth over
// what follows them; fixed, the extra geometry is passed over and only
// eight ranges kept.  (The ROM's code would write through nil here, so
// only the fix is run.)
static void
TestCISBugs(TCardSocket* socket)
{
	Bytes cis;
	Tuple(cis, 0x01, { 0x53, 0x0E, 0xFF });							// DEVICE: one, flash, 4 MB
	Tuple(cis, 0x1E, { 0x02, 0x11, 0x01, 0x01, 0x01, 0x01,			// DEVICE_GEO: two
					   0x04, 0x22, 0x02, 0x02, 0x02, 0x02 });
	Tuple(cis, 0x1A, { 0x01, 0x03, 0x00, 0x02, 0x03 });				// CONFIG
	cis.push_back(0x1B);											// CFTABLE_ENTRY 0x21, default
	cis.push_back(35);
	cis.push_back(0xE1);
	cis.push_back(0x01);											//   interface: I/O
	cis.push_back(0x08);											//   I/O only
	cis.push_back(0xE0);											//   8/16 bit, ranges
	cis.push_back(0x69);											//   ten, 2-byte addresses, 1-byte lengths
	for (int i = 0; i < 10; i++)
	{
		cis.push_back((unsigned char) (i * 0x10));
		cis.push_back(0x03);
		cis.push_back((unsigned char) i);
	}
	cis.push_back(0xFF);											// END
	WriteCard(Bytes(16, 0xFF), cis);
	EXPECT(HostCardInsert(0, kCardFile) == noErr);
	TCardPCMCIA card;
	TPCMCIA20Parser parser;
	SetRomBugFixed(true);
	parser.ParsePCCardCIS(&card, socket);
	EXPECT(card.fNumOfDevice == 1);
	TCardDevice* device = card.GetCardDevice(0);
	if (device != nil)
		EXPECT(device->fBusSize == 2 && device->fEraseBlockSize == 0x11);
	TCardConfiguration* config = card.GetCardConfiguration(0);
	EXPECT(config != nil);
	if (config != nil)
	{
		EXPECT(config->fNumOfIOSpace == kNumIOBlocks);
		EXPECT(config->fIoAddresses[7] == 0x370 && config->fIoLengths[7] == 8);
		EXPECT(config->fIoAddresses[0] == 0x300 && config->fIoLengths[0] == 1);
	}
	HostCardRemove(0);
}


// ROM BUG (fixed): SetStringsBlock's block cut at maxSize came out longer
// than maxSize, read from past the source's end
static void
TestSetStringsBlock(void)
{
	static const char kLong[] = "abcdefgh\0\xFF\0\0\0\0";
	static const char kShort[] = "abc\0def\0\xFF\0\0\0";
	char* block = nil;
	SetRomBugFixed(false);
	SetStringsBlock(block, kLong, 5);
	EXPECT(block != nil && block[4] == 'e' && block[5] == 0 && (UChar) block[6] == 0xFF);
	SetStringsBlock(block, kShort, 64);
	EXPECT(block != nil && block[7] == 0 && block[8] == 0 && (UChar) block[9] == 0xFF);
	SetRomBugFixed(true);
	SetStringsBlock(block, kLong, 5);
	EXPECT(block != nil && memcmp(block, "abc\0\xFF", 5) == 0);
	// one that fits comes out as the ROM's
	SetStringsBlock(block, kShort, 64);
	EXPECT(block != nil && memcmp(block, "abc\0def\0\0\xFF", 10) == 0);
	delete[] block;
}


static void
CISScenario(void)
{
	TCardSocket socket(0);
	EXPECT(socket.Init() == noErr);
	TestBlankCard(&socket);
	TestRichCIS(&socket);
	TestCISBugs(&socket);
	TestSetStringsBlock();
	remove(kCardFile);
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = CISScenario;
	OsBoot();
	if (failures == 0)
		printf("test_CardCIS: all passed\n");
	else
		printf("test_CardCIS: %d failures\n", failures);
	return failures != 0;
}
