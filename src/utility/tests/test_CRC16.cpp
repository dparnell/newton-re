// CRC-16 test (CRC16.h): the ROM's two CRC-16 accumulators against the
// standard check values.  TCRC16 is CRC-16/ARC (init 0); TIrCRC16 is the
// IrDA/HDLC FCS (CRC-16/CCITT, init 0xFFFF, one's-complemented).  The
// canonical test string "123456789" has published CRCs for both.

#include "CRC16.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static const char* kCheck = "123456789";		// the standard CRC check string


static void
TestARC()
{
	TCRC16 crc;
	crc.Reset();
	crc.ComputeCRC((UByte*) kCheck, (ULong) strlen(kCheck));
	// CRC-16/ARC("123456789") == 0xBB3D
	EXPECT(crc.Value() == 0xBB3D);

	// byte-at-a-time matches the block form
	TCRC16 crc2;
	crc2.Reset();
	for (const char* p = kCheck; *p; p++)
		crc2.ComputeCRC((UByte) *p);
	EXPECT(crc2.Value() == crc.Value());

	// Get() serialises the value most significant byte first
	crc.Get();
	EXPECT(crc.fResult[0] == 0xBB && crc.fResult[1] == 0x3D);

	// the empty message is the initial value, 0
	TCRC16 empty;
	empty.Reset();
	EXPECT(empty.Value() == 0x0000);
}


static void
TestIrDA()
{
	TIrCRC16 crc;
	crc.Reset();
	for (const char* p = kCheck; *p; p++)
		crc.ComputeCRC((UByte) *p);
	crc.Finalize();
	// CRC-16/X-25 (the IrDA/HDLC FCS)("123456789") == 0x906E
	EXPECT(crc.Value() == 0x906E);

	crc.Get();
	EXPECT(crc.fResult[0] == 0x90 && crc.fResult[1] == 0x6E);

	// the FCS of an empty message: ~0xFFFF == 0x0000
	TIrCRC16 empty;
	empty.Reset();
	empty.Finalize();
	EXPECT(empty.Value() == 0x0000);

	// a single zero byte, then finalize: a known intermediate
	TIrCRC16 one;
	one.Reset();
	one.ComputeCRC(0x00);
	EXPECT(one.Value() == (UShort) (IrCRCLookupTable[0xff] ^ 0x00ff));
}


int main()
{
	TestARC();
	TestIrDA();
	if (failures == 0)
		printf("test_CRC16: all passed\n");
	else
		printf("test_CRC16: %d failures\n", failures);
	return failures != 0;
}
