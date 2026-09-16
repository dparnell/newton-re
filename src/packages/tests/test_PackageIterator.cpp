// Package iterator test (src/packages/PackageIterator.h): the packages
// built into the MP2100 D ROM extension (build/MP2100D/rom.bin, the
// `pkgl` entry: what tools/newton-rom/analysis/packages.py lists) read
// from memory with TPrivatePackageIterator/TPackageIterator and from a
// pipe with TPackageIterator - the directory fields, the part infos, the
// relocation chunk of a "package1" package, and a bad header refused.
// The image is found by its AIF-less size: the REx starts at ROM$$Size
// (layout.json's rex.start, 0x6f2f1c); the packages are scanned for
// their signatures from there.

#include "PackageIterator.h"
#include "../../utility/tests/TestPipe.h"
#include "OSErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

const long kRExStart = 0x6f2f1c;			// layout.json: rex.start
const long kMaxPackages = 16;

static UByte* gROM = nil;
static long gROMSize = 0;
static UByte* gPackages[kMaxPackages];
static long gNumPackages = 0;


static Boolean
LoadROM()
{
	FILE* f = fopen(NEWTON_ROM_BIN, "rb");
	if (f == nil)
		return false;
	fseek(f, 0, SEEK_END);
	gROMSize = ftell(f);
	fseek(f, 0, SEEK_SET);
	gROM = (UByte*) malloc(gROMSize);
	Boolean ok = fread(gROM, 1, gROMSize, f) == (size_t) gROMSize;
	fclose(f);
	return ok;
}


// the packages of the REx: signatures scanned for from its start
static void
FindPackages()
{
	for (long a = kRExStart; a + kPackageDirectorySize <= gROMSize && gNumPackages < kMaxPackages; )
	{
		if (IsPackageHeader(gROM + a, gROMSize - a))
		{
			gPackages[gNumPackages++] = gROM + a;
			PackageDirectory* dir = (PackageDirectory*) (gROM + a);
			a += (dir->Size() + 3) & ~3;
		}
		else
			a += 4;
	}
}


// a package name (big-endian UniChars in the directory) as ASCII
static void
NameToASCII(const UniChar* name, char* out, long max)
{
	const UByte* bytes = (const UByte*) name;
	long i = 0;
	for (; i < max - 1; i++)
	{
		UniChar c = GetBigEndianHalf(bytes + i * 2);
		if (c == 0)
			break;
		out[i] = (char) c;
	}
	out[i] = 0;
}


static void
TestMemory()
{
	EXPECT(gNumPackages == 10);
	if (gNumPackages < 10)
		return;
	// Cardfile: the first, "package1" with a relocation chunk, one frames part
	TPackageIterator it(gPackages[0]);
	EXPECT(it.Init() == noErr);
	char name[64];
	NameToASCII(it.PackageName(), name, sizeof(name));
	EXPECT(strcmp(name, "Cardfile") == 0);
	EXPECT(it.GetPackageId() == 'xxxx');
	EXPECT(it.PackageFlags() == kNoCompressionFlag);
	EXPECT(it.GetVersion() == 1 && it.NumberOfParts() == 1 && it.PackageSize() == 146800 && it.DirectorySize() == 284);
	EXPECT(!it.CopyProtected() && !it.ForDispatchOnly());
	EXPECT(it.Copyright() != nil);
	NameToASCII(it.Copyright(), name, sizeof(name));
	EXPECT(strstr(name, "Apple") != nil || strlen(name) > 0);
	// "package1" but no kRelocationFlag: no chunk read
	EXPECT(it.fRelocationInfo == nil && it.fPartsOffset == 284);
	PartInfo info;
	memset(&info, 0, sizeof(info));
	it.GetPartInfo(0, &info);
	EXPECT(info.kind == kFrames && info.type == 'auto' && info.size == 146516 && info.sizeInMemory == 146516);
	EXPECT(!info.autoLoad && !info.autoRemove && !info.compressed && info.notify && !info.autoCopy);	// flags 0x81
	EXPECT(info.infoSize == 26 && memcmp(info.info, "Courtesy of Newton Toolkit", 26) == 0);
	EXPECT(info.data == (ULong) (gPackages[0] + 284));
	EXPECT(it.GetPartDataOffset(0) == 284 && it.ProcessorTypeOfPart(0) == 0);
	// the part data begins with the frames part's object heap header
	const UByte* part = (const UByte*) info.data;
	EXPECT(GetBigEndianWord(part) == 0x00001041);				// an NTK frames part: the first object's header word
	// out of range parts
	info.size = 77;
	it.GetPartInfo(5, &info);
	EXPECT(info.size == 77 && it.GetPartDataOffset(5) == 0 && it.ProcessorTypeOfPart(5) == 0);

	// the others: names, kinds and types
	static const char* names[] = { "Cardfile", "Verbindung", "FaxViewer", "Tabellen", "help book", "ListView", "ScreenBuffer", "ScreenDrivers", "Profil", "WorldData" };
	static const ULong types[] = { 'auto', 'form', 'auto', 'form', 'book', 'auto', 0, 0, 'form', 'soup' };
	static const long kinds[] = { kFrames, kFrames, kFrames, kFrames, kFrames, kFrames, kProtocol, kProtocol, kFrames, kRaw };
	for (long i = 0; i < gNumPackages; i++)
	{
		TPackageIterator pkg(gPackages[i]);
		EXPECT(pkg.Init() == noErr);
		NameToASCII(pkg.PackageName(), name, sizeof(name));
		EXPECT(strcmp(name, names[i]) == 0);
		pkg.GetPartInfo(0, &info);
		EXPECT(info.type == types[i] && info.kind == kinds[i]);
		EXPECT(info.size <= pkg.PackageSize() - pkg.DirectorySize());
		EXPECT(pkg.NumberOfParts() == 1);
	}
	// the private iterator alone
	TPrivatePackageIterator priv;
	EXPECT(priv.Init(gPackages[9]) == noErr && priv.NumberOfParts() == 1 && priv.PackageSize() == 282772);
	priv.GetPartInfo(0, &info);
	EXPECT(info.type == 'soup' && info.data == (ULong) (gPackages[9] + priv.fPartsOffset));

	// a bad header
	UByte bad[0x40];
	memcpy(bad, gPackages[0], sizeof(bad));
	bad[7] = '7';
	EXPECT(!IsPackageHeader(bad, sizeof(bad)));
	TPackageIterator badIt(bad);
	EXPECT(badIt.Init() == kError_Bad_Package && badIt.fDirectory == nil);
	EXPECT(!IsPackageHeader(gPackages[0], 0x20));					// too short
}


static void
TestPipe()
{
	if (gNumPackages < 10)
		return;
	// the whole of ScreenDrivers (a small one) through a pipe
	PackageDirectory* dir = (PackageDirectory*) gPackages[7];
	CTestPipe pipe(dir->Size());
	pipe.WriteChunk(gPackages[7], dir->Size(), false);
	pipe.Rewind();
	TPackageIterator it(&pipe);
	EXPECT(it.Init() == noErr);
	char name[64];
	NameToASCII(it.PackageName(), name, sizeof(name));
	EXPECT(strcmp(name, "ScreenDrivers") == 0);
	EXPECT(it.fFromPipe && it.fDirectory != (PackageDirectory*) gPackages[7]);		// its own copy
	EXPECT(it.PackageSize() == 4348 && it.DirectorySize() == 176 && it.GetPackageId() == ' no ');
	PartInfo info;
	it.GetPartInfo(0, &info);
	EXPECT(info.kind == kProtocol && info.type == 0 && info.size == 4172 && info.autoLoad);
	EXPECT(info.data == 176);										// a pipe source: the offset
	EXPECT(pipe.ReadPosition() == 176);								// the directory has been consumed; the parts follow
	// a pipe that runs dry in the directory
	CTestPipe shortPipe(64);
	shortPipe.WriteChunk(gPackages[7], 0x40, false);
	shortPipe.Rewind();
	TPackageIterator shortIt(&shortPipe);
	EXPECT(shortIt.Init() != noErr && shortIt.fDirectory == nil);
}


int
main()
{
	InitHostStandaloneHeap();
	if (!LoadROM())
	{
		printf("test_PackageIterator: cannot read %s\n", NEWTON_ROM_BIN);
		return 1;
	}
	FindPackages();
	TestMemory();
	TestPipe();
	if (failures == 0)
		printf("test_PackageIterator: all passed\n");
	return failures != 0;
}
