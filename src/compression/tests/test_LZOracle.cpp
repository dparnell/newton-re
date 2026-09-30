// The LZ coder (compression/LZCompression.h) against the ROM itself: the
// ROM's own TLZCompressor and TLZDecompressor are run on the ARM
// interpreter (armcpu/ARMCPU.h) over the ROM image, and the reconstruction
// must compress to the same bytes and decompress to the same bytes *and
// the same length* - including where that length is wrong.
//
// It is wrong in two ways, and both are the ROM's (docs/host-lp64.md and
// the comments at the two sites):
//
//   * a stored last block of 1021 to 1023 bytes comes back as a whole
//     0x400 bytes, because the size it is given back by counts the
//     block's own four-byte header (DecompressBlock, 0x000ffa60);
//   * a coded last block can come back a few bytes long, because the
//     decoder reads codewords until the input runs out and the bits that
//     pad the last byte can make one more.
//
// Neither is reachable through the only thing that uses the coder: the
// store compander hands it fixed 0x400-byte blocks
// (stores/StoreCompander.h), where the last block is full and the padding
// has nothing after it to decode.  The test therefore checks the whole
// round trip at the sizes the machine uses, and against the ROM
// everywhere else.
//
// How the ROM's code is run: the image is at 0, the patchable jump table
// is aliased at 0x01A00000 (nearly every cross-module call goes through
// it - see tools/newton-rom/newtonrom/jumptable.py), and the two
// allocators the compressor's New calls are host traps over a bump
// allocator.  Nothing else of the OS is needed: TLZDecompressor::New is
// `MOV pc, lr` and both Inits answer noErr.

#include "Compression.h"
#include "LZCompression.h"
#include "ARMCPU.h"
#include "Boot.h"
#include "KernelGlobals.h"
#include "UserBoot.h"
#include "NewtonMemory.h"
#include "OSErrors.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <vector>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the ROM's functions (build/MP2x00US/symbols.txt)
static const uint32_t kROMCompressorNew	= 0x000fea60;	// New__13TLZCompressorFv
static const uint32_t kROMCompress		= 0x000ff5e8;	// Compress__13TLZCompressorFPUlPvUlT2T3
static const uint32_t kROMDecompress	= 0x000ffd30;	// Decompress__15TLZDecompressorFPUlPvUlT2T3
static const uint32_t kROMCompressorSize	= 0x438;	// Sizeof__13TLZCompressorSFv
static const uint32_t kROMDecompressorSize	= 0x3c;		// Sizeof__15TLZDecompressorSFv

// the jump table slots the compressor's New calls out through
static const uint32_t kSlotMalloc		= 0x01bd6b68;	// malloc
static const uint32_t kSlotOperatorNew	= 0x01bce738;	// operator new(unsigned int)

// the jump table's virtual window (newtonrom/jumptable.py)
static const uint32_t kJumpVirtual		= 0x01A00000;
static const uint32_t kJumpROM			= 0x00002000;
static const uint32_t kJumpPages		= 0x800;		// 16919 slots, 32 to a virtual page

static const uint32_t kRAM = 0x0F000000, kRAMSize = 0x40000;
static const uint32_t kStop = 0xDEAD0000;


/*------------------------------------------------------------------------------
	The ROM image at 0, the jump table aliased where the MMU puts it, and
	RAM with a bump allocator for the two calls out.
------------------------------------------------------------------------------*/

class ROMMemory : public ARMMemory
{
public:
	std::vector<uint8_t>	rom;
	std::vector<uint8_t>	ram;
	uint32_t				brk;		// the bump allocator
	ROMMemory() : ram(kRAMSize, 0), brk(kRAM + 0x1000) { }

	uint32_t	Allocate(uint32_t size)
	{
		uint32_t at = (brk + 7) & ~7u;
		brk = at + size;
		return brk <= kRAM + kRAMSize ? at : 0;
	}

	uint8_t*	At(uint32_t a, uint32_t n, bool write)
	{
		if (a >= kRAM && a + n <= kRAM + kRAMSize)
			return &ram[a - kRAM];
		if (write)
			return nullptr;						// the ROM and its jump table are read-only
		if (a + n <= rom.size())
			return &rom[a];
		// the jump table: virtual page p is a plain alias of the ROM page
		// 0x2000 + (p / 32) * 0x1000, so 32 virtual pages show the same
		// 4 KB and each owns a different 128-byte slice of it.  The branch
		// offsets are relative to the virtual address, so executing a slot
		// where it is aliased is what makes them come out right.
		if (a >= kJumpVirtual && a < kJumpVirtual + kJumpPages * 0x1000)
		{
			uint32_t page = (a - kJumpVirtual) / 0x1000;
			uint32_t off = (a - kJumpVirtual) % 0x1000;
			uint32_t at = kJumpROM + (page / 32) * 0x1000 + off;
			if (at + n <= rom.size())
				return &rom[at];
		}
		return nullptr;
	}

	bool Read32(uint32_t a, uint32_t* v) override
	{
		uint8_t* p = At(a, 4, false);
		if (!p) return false;
		*v = ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
		return true;
	}
	bool Read16(uint32_t a, uint16_t* v) override
	{
		uint8_t* p = At(a, 2, false);
		if (!p) return false;
		*v = (uint16_t) ((p[0] << 8) | p[1]);
		return true;
	}
	bool Read8(uint32_t a, uint8_t* v) override			{ uint8_t* p = At(a, 1, false); if (!p) return false; *v = *p; return true; }
	bool Write32(uint32_t a, uint32_t v) override
	{
		uint8_t* p = At(a, 4, true);
		if (!p) return false;
		p[0] = (uint8_t) (v >> 24); p[1] = (uint8_t) (v >> 16); p[2] = (uint8_t) (v >> 8); p[3] = (uint8_t) v;
		return true;
	}
	bool Write16(uint32_t a, uint16_t v) override		{ uint8_t* p = At(a, 2, true); if (!p) return false; p[0] = (uint8_t) (v >> 8); p[1] = (uint8_t) v; return true; }
	bool Write8(uint32_t a, uint8_t v) override			{ uint8_t* p = At(a, 1, true); if (!p) return false; *p = v; return true; }

	// malloc and operator new: the compressor's New asks for its node
	// table and its Pushpopper, and nothing gives them back here
	bool IsTrap(uint32_t pc) override					{ return pc == kSlotMalloc || pc == kSlotOperatorNew; }
	bool Trap(TARMCPU* cpu, uint32_t pc) override
	{
		cpu->r[0] = Allocate(cpu->r[0]);
		cpu->r[15] = cpu->r[14];
		return cpu->r[0] != 0;
	}
};

static ROMMemory* gROM = nullptr;


static bool
LoadROM(void)
{
	FILE* f = fopen(NEWTON_ROM_BIN, "rb");
	if (f == nullptr)
		return false;
	gROM = new ROMMemory;
	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	gROM->rom.resize(size);
	bool ok = fread(gROM->rom.data(), 1, size, f) == (size_t) size;
	fclose(f);
	return ok;
}


// A ROM method of five arguments (the shape both Compress and Decompress
// have): this in r0, the first three in r1-r3 and the last two on the
// stack, as APCS passes them.
static bool
CallROM(uint32_t function, uint32_t self, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5)
{
	TARMCPU cpu(gROM);
	cpu.Reset();
	uint32_t sp = kRAM + kRAMSize - 64;
	gROM->Write32(sp, a4);
	gROM->Write32(sp + 4, a5);
	cpu.r[0] = self;
	cpu.r[1] = a1;
	cpu.r[2] = a2;
	cpu.r[3] = a3;
	cpu.r[13] = sp;
	ARMStop stop = cpu.Call(function, kStop, 200000000);
	if (stop != kARMReturned)
	{
		printf("FAIL: the ROM's %08x stopped (%d) at %08x\n", (unsigned) function, (int) stop, (unsigned) cpu.faultPC);
		failures++;
		return false;
	}
	return true;
}


/*------------------------------------------------------------------------------
	The data: the same shapes the round-trip test uses (runs, text, an
	earlier stretch again, noise), from a generator of our own so that the
	ROM and the reconstruction are given the same bytes on every host - the
	C library's rand() is a different sequence on each.
------------------------------------------------------------------------------*/

static const char* kText =
	"The Newton MessagePad is a series of personal digital assistant devices developed by Apple. "
	"The MessagePad was the first series of PDAs; it introduced handwriting recognition. ";

static uint32_t gSeed = 1;
static int Rand(void) { gSeed = gSeed * 1103515245u + 12345u; return (int) ((gSeed >> 16) & 0x7fff); }

static void
Fill(UByte* p, ULong size, unsigned seed)
{
	gSeed = seed;
	ULong i = 0, textLength = (ULong) strlen(kText);
	while (i < size)
	{
		int kind = Rand() % 4;
		ULong n = 1 + Rand() % 200;
		if (i + n > size)
			n = size - i;
		if (kind == 0)
			memset(p + i, Rand() & 0xff, n);
		else if (kind == 1)
		{
			ULong at = Rand() % textLength;
			for (ULong j = 0; j < n; j++)
				p[i + j] = kText[(at + j) % textLength];
		}
		else if (kind == 2 && i > 300)
		{
			ULong back = 1 + Rand() % 300;
			for (ULong j = 0; j < n; j++)
				p[i + j] = p[i + j - back];
		}
		else
			for (ULong j = 0; j < n; j++)
				p[i + j] = (UByte) Rand();
		i += n;
	}
}


/*------------------------------------------------------------------------------
	One size through both, compared.
------------------------------------------------------------------------------*/

static const ULong	kMaxData = 0x2800;
static const ULong	kBuffer = 0x4000;

static UByte	gData[kMaxData];
static UByte	gHostCompressed[kBuffer];
static UByte	gHostRestored[kBuffer];
static UByte	gROMCompressed[kBuffer];
static UByte	gROMRestored[kBuffer];

// where things live in the ARM's RAM
static uint32_t	gSrcAt, gDstAt, gOutSizeAt, gCompressorAt, gDecompressorAt;

static void
SetupARM(void)
{
	gROM->brk = kRAM + 0x1000;
	gSrcAt = gROM->Allocate(kBuffer);
	gDstAt = gROM->Allocate(kBuffer);
	gOutSizeAt = gROM->Allocate(16);
	gCompressorAt = gROM->Allocate(kROMCompressorSize);
	gDecompressorAt = gROM->Allocate(kROMDecompressorSize);
	for (uint32_t i = 0; i < kROMCompressorSize; i += 4)
		gROM->Write32(gCompressorAt + i, 0);
	for (uint32_t i = 0; i < kROMDecompressorSize; i += 4)
		gROM->Write32(gDecompressorAt + i, 0);
	// TLZCompressor::New takes its node table and its Pushpopper; the
	// decompressor's is `MOV pc, lr` and needs nothing
	TARMCPU cpu(gROM);
	cpu.Reset();
	cpu.r[0] = gCompressorAt;
	cpu.r[13] = kRAM + kRAMSize - 64;
	if (cpu.Call(kROMCompressorNew, kStop, 1000000) != kARMReturned)
	{
		printf("FAIL: the ROM's TLZCompressor::New stopped at %08x\n", (unsigned) cpu.faultPC);
		failures++;
	}
}


static void	WriteARM(uint32_t at, const UByte* p, ULong n)	{ for (ULong i = 0; i < n; i++) gROM->Write8(at + i, p[i]); }
static void	ReadARM(uint32_t at, UByte* p, ULong n)			{ for (ULong i = 0; i < n; i++) gROM->Read8(at + i, &p[i]); }
static ULong	ReadWordARM(uint32_t at)					{ uint32_t v = 0; gROM->Read32(at, &v); return v; }


// ==> whether the ROM and the reconstruction agree, and (through `exact`)
// whether the round trip gave the length back
static Boolean
CheckSize(TCompressor* compressor, TDecompressor* decompressor, ULong size, Boolean* exact)
{
	// the reconstruction
	ULong hostCompressed = 0, hostRestored = 0;
	memset(gHostCompressed, 0, sizeof(gHostCompressed));
	memset(gHostRestored, 0xee, sizeof(gHostRestored));
	if (compressor->Compress(&hostCompressed, gHostCompressed, sizeof(gHostCompressed), gData, size) != noErr)
	{
		printf("FAIL: the reconstruction would not compress %lu bytes\n", (unsigned long) size);
		failures++;
		return false;
	}
	decompressor->Decompress(&hostRestored, gHostRestored, sizeof(gHostRestored), gHostCompressed, hostCompressed);

	// the ROM
	SetupARM();
	WriteARM(gSrcAt, gData, size);
	gROM->Write32(gOutSizeAt, 0);
	if (!CallROM(kROMCompress, gCompressorAt, gOutSizeAt, gDstAt, kBuffer, gSrcAt, size))
		return false;
	ULong romCompressed = ReadWordARM(gOutSizeAt);
	if (romCompressed > kBuffer)
	{
		printf("FAIL: the ROM compressed %lu bytes to %lu\n", (unsigned long) size, (unsigned long) romCompressed);
		failures++;
		return false;
	}
	ReadARM(gDstAt, gROMCompressed, romCompressed);
	// The compressed bytes back in, and decompressed where the source was.
	// The buffer is cleared first because a stored last block of 1021 to
	// 1023 bytes is read past its end (see the top of this file), and the
	// bytes that come back from there have to be the same on both sides
	// for the comparison to mean anything - the reconstruction's own
	// buffer is cleared before it compresses into it.
	for (ULong i = 0; i < kBuffer; i++)
		gROM->Write8(gSrcAt + i, 0);
	WriteARM(gSrcAt, gROMCompressed, romCompressed);
	gROM->Write32(gOutSizeAt, 0);
	for (ULong i = 0; i < kBuffer; i++)
		gROM->Write8(gDstAt + i, 0xee);
	if (!CallROM(kROMDecompress, gDecompressorAt, gOutSizeAt, gDstAt, kBuffer, gSrcAt, romCompressed))
		return false;
	ULong romRestored = ReadWordARM(gOutSizeAt);
	if (romRestored > kBuffer)
	{
		printf("FAIL: the ROM restored %lu bytes of %lu\n", (unsigned long) romRestored, (unsigned long) size);
		failures++;
		return false;
	}
	ReadARM(gDstAt, gROMRestored, romRestored);

	Boolean same = true;
	if (hostCompressed != romCompressed || memcmp(gHostCompressed, gROMCompressed, romCompressed) != 0)
	{
		printf("FAIL: %lu bytes compressed: the ROM %lu bytes, the reconstruction %lu%s\n",
			(unsigned long) size, (unsigned long) romCompressed, (unsigned long) hostCompressed,
			hostCompressed == romCompressed ? " (same length, different bytes)" : "");
		failures++;
		same = false;
	}
	// The restored length, and the bytes as far as the source went.  Where
	// the ROM gives back more than it was given (the two faults at the top
	// of this file) the bytes past that came from the decoder reading past
	// the end of its input - what is there is the caller's buffer, not the
	// coder's output, so there is nothing to compare.  The length itself is
	// the thing to hold the two to, and it is compared exactly.
	if (hostRestored != romRestored)
	{
		printf("FAIL: %lu bytes restored: the ROM %lu bytes, the reconstruction %lu\n",
			(unsigned long) size, (unsigned long) romRestored, (unsigned long) hostRestored);
		failures++;
		same = false;
	}
	else
	{
		ULong comparable = romRestored < size ? romRestored : size;
		if (memcmp(gHostRestored, gROMRestored, comparable) != 0)
		{
			ULong at = 0;
			while (at < comparable && gHostRestored[at] == gROMRestored[at])
				at++;
			printf("FAIL: %lu bytes restored: the ROM and the reconstruction differ at %lu (rom %02x, host %02x)\n",
				(unsigned long) size, (unsigned long) at, gROMRestored[at], gHostRestored[at]);
			failures++;
			same = false;
		}
	}
	*exact = romRestored == size && memcmp(gROMRestored, gData, size) == 0;
	return same;
}


static void
LZOracleScenario(void)
{
	InitializeCompression();
	TCompressor* compressor = TCompressor::New("TLZCompressor");
	TDecompressor* decompressor = TDecompressor::New("TLZDecompressor");
	EXPECT(compressor != nil && decompressor != nil);
	if (compressor == nil || decompressor == nil)
	{
		HostStopTasks();
		return;
	}
	EXPECT(compressor->Init(nil) == noErr && decompressor->Init(nil) == noErr);

	Fill(gData, sizeof(gData), 42);

	// Every size the round-trip test uses, the boundaries around a block
	// and a chunk, and the three the stored path gets wrong.
	static const ULong kSizes[] = {
		0, 1, 2, 3, 4, 5, 63, 64, 65, 77, 127, 128,
		0x3fc, 0x3fd, 0x3fe, 0x3ff, 0x400, 0x401, 0x402,
		0x7fe, 0x7ff, 0x800, 0x801, 0x802,
		0x1000, 0x1001, 0x2800
	};
	for (ULong i = 0; i < sizeof(kSizes) / sizeof(kSizes[0]); i++)
	{
		Boolean exact = false;
		CheckSize(compressor, decompressor, kSizes[i], &exact);
	}

	// A whole block is what the store compander gives the coder
	// (stores/StoreCompander.h), and every multiple of one round-trips
	// exactly - which is why neither fault was ever met on the machine.
	for (ULong size = 0x400; size <= 0x2800; size += 0x400)
	{
		Boolean exact = false;
		CheckSize(compressor, decompressor, size, &exact);
		if (!exact)
		{
			printf("FAIL: %lu bytes - a whole number of blocks - did not round-trip\n", (unsigned long) size);
			failures++;
		}
	}

	// Every size up to two chunks and a bit: the reconstruction must agree
	// with the ROM at all of them, and a good few do not give the length
	// back (a coded last block whose padding bits make one more codeword).
	long exactCount = 0, inexact = 0;
	for (ULong size = 0; size <= 0x900; size++)
	{
		Boolean exact = false;
		CheckSize(compressor, decompressor, size, &exact);
		if (exact)
			exactCount++;
		else
			inexact++;
	}
	printf("test_LZOracle: of %ld sizes, %ld round-tripped exactly and %ld did not - and the ROM did the same at every one\n",
		exactCount + inexact, exactCount, inexact);
	EXPECT(inexact > 0);			// (if this ever became 0 the two faults would be gone and the test wants rewriting)

	// The stored path's own boundary, with data the coder cannot shrink:
	// a last block of 1021 to 1023 bytes comes back as a whole 0x400.
	for (ULong i = 0; i < sizeof(gData); i++)
		gData[i] = (UByte) (Rand() >> 3);				// incompressible
	for (ULong size = 1018; size <= 1028; size++)
	{
		Boolean exact = false;
		CheckSize(compressor, decompressor, size, &exact);
		EXPECT(exact == (size < 1021 || size >= 1024));	// the ROM's own answer
	}

	HostStopTasks();
}


int main()
{
	if (!LoadROM())
	{
		printf("test_LZOracle: no ROM image at %s\n", NEWTON_ROM_BIN);
		return 0;					// (as the other ROM tests do: not a failure)
	}
	gHostKernelServicesTask = LZOracleScenario;
	OsBoot();
	if (failures == 0)
		printf("test_LZOracle: all passed\n");
	return failures != 0;
}
