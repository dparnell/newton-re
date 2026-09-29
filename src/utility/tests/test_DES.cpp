// utility/DES.h against the ROM itself: the ROM's own DESEncodeNonce,
// DESDecodeNonce and DESCharToKey are run on the ARM interpreter
// (armcpu/ARMCPU.h) over the ROM image, and the reconstruction must give
// the same blocks and keys - for keys and blocks of every shape, and for
// passwords short, long and empty.  Also: decryption undoes encryption, and
// several blocks go through at once.  (The ROM's DES is not bit-for-bit the
// standard's: it takes a key's bits one place over, so the published
// vectors are not the test - the ROM is.)

#include "DES.h"
#include "ARMCPU.h"

#include <stdio.h>
#include <string.h>
#include <vector>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

// the ROM's functions
static const uint32_t kROMDESEncodeNonce = 0x002d4864;
static const uint32_t kROMDESDecodeNonce = 0x002d4898;
static const uint32_t kROMDESCharToKey = 0x002d45dc;

static const uint32_t kRAM = 0x0F000000, kRAMSize = 0x10000;
static const uint32_t kStop = 0xDEAD0000;


// the ROM image at 0 (read-only) and a little RAM
class ROMMemory : public ARMMemory
{
public:
	std::vector<uint8_t> rom;
	std::vector<uint8_t> ram;
	ROMMemory() : ram(kRAMSize, 0) { }
	uint8_t* At(uint32_t a, uint32_t n, bool write)
	{
		if (!write && a + n <= rom.size())
			return &rom[a];
		if (a >= kRAM && a + n <= kRAM + kRAMSize)
			return &ram[a - kRAM];
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


// a ROM function of two pointer arguments, each to words in RAM (in and
// out)
static bool
CallROM(uint32_t function, const DESWord* a, int aWords, DESWord* b, int bWords)
{
	const uint32_t aAt = kRAM + 0x100, bAt = kRAM + 0x200;
	for (int i = 0; i < aWords; i++)
		gROM->Write32(aAt + i * 4, a[i]);
	for (int i = 0; i < bWords; i++)
		gROM->Write32(bAt + i * 4, b[i]);
	TARMCPU cpu(gROM);
	cpu.Reset();
	cpu.r[0] = aAt;
	cpu.r[1] = bAt;
	cpu.r[13] = kRAM + kRAMSize - 16;
	ARMStop stop = cpu.Call(function, kStop, 10000000);
	if (stop != kARMReturned)
	{
		printf("FAIL: the ROM's function at %08x stopped (%d) at %08x\n", (unsigned) function, (int) stop, (unsigned) cpu.faultPC);
		failures++;
		return false;
	}
	for (int i = 0; i < bWords; i++)
		gROM->Read32(bAt + i * 4, &b[i]);
	return true;
}


static void
CheckBlock(DESWord k0, DESWord k1, DESWord p0, DESWord p1)
{
	DESWord key[2] = { k0, k1 };
	DESWord host[2] = { p0, p1 };
	DESEncodeNonce(key, host);
	DESWord rom[2] = { p0, p1 };
	if (!CallROM(kROMDESEncodeNonce, key, 2, rom, 2))
		return;
	if (host[0] != rom[0] || host[1] != rom[1])
	{
		failures++;
		printf("FAIL: key %08x%08x, %08x%08x: the ROM says %08x%08x, the host %08x%08x\n",
			(unsigned) k0, (unsigned) k1, (unsigned) p0, (unsigned) p1, (unsigned) rom[0], (unsigned) rom[1], (unsigned) host[0], (unsigned) host[1]);
	}
	DESDecodeNonce(key, host);
	EXPECT(host[0] == p0 && host[1] == p1);
	if (CallROM(kROMDESDecodeNonce, key, 2, rom, 2))
		EXPECT(rom[0] == p0 && rom[1] == p1);
}


static void
CheckPassword(const char* text)
{
	UniChar password[64];
	int n = 0;
	for (; text[n] != 0; n++)
		password[n] = (UniChar) (unsigned char) text[n];
	password[n] = 0;
	DESWord host[2];
	DESCharToKey(password, host);
	// the string as the ROM keeps it: big-endian UniChars
	DESWord words[33];
	memset(words, 0, sizeof(words));
	for (int i = 0; i <= n; i++)
		words[i / 2] |= (DESWord) password[i] << ((i & 1) ? 0 : 16);
	DESWord rom[2] = { 0, 0 };
	if (!CallROM(kROMDESCharToKey, words, (n + 2) / 2 + 1, rom, 2))
		return;
	if (host[0] != rom[0] || host[1] != rom[1])
	{
		failures++;
		printf("FAIL: password \"%s\": the ROM says %08x%08x, the host %08x%08x\n",
			text, (unsigned) rom[0], (unsigned) rom[1], (unsigned) host[0], (unsigned) host[1]);
	}
}


int
main()
{
	if (!LoadROM())
	{
		printf("test_DES: no ROM image (%s); skipped\n", NEWTON_ROM_BIN);
		return 0;
	}
	CheckBlock(0x13345779, 0x9BBCDFF1, 0x01234567, 0x89ABCDEF);
	CheckBlock(0x01010101, 0x01010101, 0x80000000, 0x00000000);
	CheckBlock(0x0E329232, 0xEA6D0D73, 0x87878787, 0x87878787);
	CheckBlock(0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF);
	CheckBlock(0, 0, 0, 0);
	CheckBlock(0x80000001, 0x7FFFFFFE, 0x00000001, 0x80000000);
	DESWord seed = 0x12345678;
	for (int i = 0; i < 20; i++)
	{
		DESWord w[4];
		for (int j = 0; j < 4; j++)
		{
			seed = seed * 1103515245 + 12345;
			w[j] = seed ^ (seed << 13);
		}
		CheckBlock(w[0], w[1], w[2], w[3]);
	}

	// several blocks at once, the pointer left past them
	DESWord key[2] = { 0x13345779, 0x9BBCDFF1 };
	DESWord schedule[32];
	DESKeySched(key, schedule);
	DESWord data[4] = { 0x01234567, 0x89ABCDEF, 0x01234567, 0x89ABCDEF };
	DESWord one[2] = { 0x01234567, 0x89ABCDEF };
	DESEncodeNonce(key, one);
	DESWord* p = data;
	DESEncode(schedule, 16, &p);
	EXPECT(p == data + 4);
	EXPECT(data[0] == one[0] && data[1] == one[1] && data[2] == one[0] && data[3] == one[1]);
	p = data;
	DESDecode(schedule, 16, &p);
	EXPECT(p == data + 4 && data[0] == 0x01234567 && data[3] == 0x89ABCDEF);

	CheckPassword("");
	CheckPassword("a");
	CheckPassword("abc");
	CheckPassword("abcd");
	CheckPassword("secret");
	CheckPassword("password");
	CheckPassword("a longer password than eight");

	// the values tools/dock/newtondes.py (the desktop's copy) prints
	{
		static const UniChar kEmpty[1] = { 0 };
		static const UniChar kSecret[7] = { 's', 'e', 'c', 'r', 'e', 't', 0 };
		DESWord k[2];
		DESCharToKey(kEmpty, k);
		EXPECT(k[0] == 0xf207bf4f && k[1] == 0x851b167d);
		DESCharToKey(kSecret, k);
		EXPECT(k[0] == 0xcdfbc154 && k[1] == 0x1567c58d);
		DESWord key[2] = { 0x13345779, 0x9BBCDFF1 }, block[2] = { 0x01234567, 0x89ABCDEF };
		DESEncodeNonce(key, block);
		EXPECT(block[0] == 0xb1a43e32 && block[1] == 0xd37048e8);
	}

	if (failures == 0)
		printf("test_DES: all passed\n");
	else
		printf("test_DES: %d failures\n", failures);
	return failures != 0;
}
