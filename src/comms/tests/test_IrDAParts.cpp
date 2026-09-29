// The IrDA stack's self-contained parts (comms/irda/): the quality of
// service (TIrQOS: the parameters, their SNRM/UA form both ways, the
// negotiation and the data/window cut-down), the discovery information
// (TIrDscInfo) and the IAS database (TIASService and its classes,
// attributes and elements, and an attribute's form on the air).

#include "IrQOS.h"
#include "IrDscInfo.h"
#include "IrIASService.h"
#include "BufferSegment.h"
#include "CommErrors.h"
#include "NewtonTime.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestQOS(void)
{
	TIrQOS qos;
	EXPECT(qos.GetBaudRate() == 115200);
	EXPECT(qos.GetDataSize() == 512);
	EXPECT(qos.GetWindowSize() == 1);
	EXPECT(qos.GetMaxTurnAroundTime() == 500 * kMilliseconds);
	EXPECT(qos.GetExtraBOFs() == 2);
	EXPECT(qos.GetLinkDiscThresholdTime() == 40 * kSeconds);

	// the parameters as a SNRM's: seven of three bytes each
	UByte info[32];
	EXPECT(qos.AddInfoToBuffer(info, sizeof(info)) == 0x15);
	const UByte ids[] = { 0x01, 0x82, 0x83, 0x84, 0x85, 0x86, 0x08 };
	for (int i = 0; i < 7; i++)
		EXPECT(info[i * 3] == ids[i] && info[i * 3 + 1] == 1);
	EXPECT(info[2] == 0x3e && info[8] == 0xf && info[20] == 0xff);

	// ... and back, with a longer parameter skipped and one left out
	UByte other[] = { 0x01, 0x01, 0x06, 0x99, 0x02, 0xaa, 0xbb, 0x83, 0x01, 0x03 };
	CBufferSegment segment;
	EXPECT(segment.Init(other, sizeof(other)) == noErr);
	TIrQOS peer;
	EXPECT(peer.ExtractInfoFromBuffer(&segment) == noErr);
	EXPECT(peer.fBaudRate == 0x06 && peer.fDataSize == 0x03);
	EXPECT(peer.fWindowSize == 1 && peer.fLinkDiscThreshold == 0xff);

	// negotiated: the rates both have (9600, 19200: 19200 is used)
	EXPECT(qos.NegotiateWith(&peer) == noErr);
	EXPECT(qos.fBaudRate == 0x06 && qos.GetBaudRate() == 19200);
	TIrQOS none;
	none.fBaudRate = 0x20;
	peer.fBaudRate = 0x02;
	EXPECT(none.NegotiateWith(&peer) == kIrDAErrProtocolError);

	// the setters
	TIrQOS set;
	EXPECT(set.SetBaudRate(38400) == noErr && set.fBaudRate == 0x0e);
	EXPECT(set.SetBaudRate(12345) == kCommErrBadParameter);
	EXPECT(set.SetDataSize(256) == noErr && set.fDataSize == 0x07);
	EXPECT(set.SetWindowSize(3) == noErr && set.fWindowSize == 0x07);
	EXPECT(set.SetWindowSize(8) == kCommErrBadParameter);
	EXPECT(set.SetLinkDiscThresholdTime(12 * kSeconds) == noErr && set.fLinkDiscThreshold == 0x07);

	// a window of seven 2K frames does not fit 9600 bps: cut down
	TIrQOS big;
	big.SetBaudRate(9600);
	big.fDataSize = 0x3f;
	big.fWindowSize = 0x7f;
	EXPECT(big.NormalizeInfo() == noErr);
	EXPECT(big.GetWindowSize() == 1);
	EXPECT((big.GetDataSize() + big.GetExtraBOFs() + 6) * big.GetWindowSize() < IrMaxLineCapacityTable1[0]);
	printf("9600 bps normalised to %lu bytes, window %lu\n", (unsigned long) big.GetDataSize(), (unsigned long) big.GetWindowSize());
}


static void
TestDscInfo(void)
{
	TIrDscInfo mine;
	EXPECT(strcmp(mine.fNickname, "Newton") == 0 && mine.fHints == 2);
	UByte info[32];
	ULong length = mine.AddDevInfoToBuffer(info, sizeof(info));
	EXPECT(length == 1 + 1 + 6 && info[0] == 2 && info[1] == 0 && memcmp(info + 2, "Newton", 6) == 0);
	EXPECT(mine.SetNickname("a name far too long for it") == kCommErrBadParameter);

	CBufferSegment segment;
	EXPECT(segment.Init(info, length) == noErr);
	TIrDscInfo theirs;
	EXPECT(theirs.ExtractDevInfoFromBuffer(&segment) == noErr);
	EXPECT(theirs.fHints == 2 && theirs.fCharSet == 0 && strcmp(theirs.fNickname, "Newton") == 0);
}


static void
TestIAS(void)
{
	TIASService* ias = new TIASService;
	EXPECT(ias->AddStringEntry(kIASDeviceClassStr, kIASDeviceNameAttrStr, "Newton") == noErr);
	EXPECT(ias->AddNBytesEntry(kIASDeviceClassStr, kIASLMPSupportAttrStr, 0x01000000, 3) == noErr);
	EXPECT(ias->AddIntegerEntry("BMW", "IrDA:IrLMP:LsapSel", 5) == noErr);
	EXPECT(ias->GetArraySize() == 2);

	TIASClass* device = ias->FindClass("Device");
	EXPECT(device != nil && device->GetArraySize() == 2);
	EXPECT(ias->FindClass("Nobody") == nil);
	TIASAttribute* support = device != nil ? device->FindAttribute("IrLMPSupport") : nil;
	EXPECT(support != nil);

	// an attribute on the air: a count, then object id 0, the type, the value
	UByte out[32];
	CBufferSegment segment;
	EXPECT(segment.Init(out, sizeof(out)) == noErr);
	if (support != nil)
		support->AddInfoToBuffer(&segment);
	const UByte supportBytes[] = { 0, 1, 0, 0, 2, 0, 3, 1, 0, 0 };
	EXPECT(segment.Position() == (Size) sizeof(supportBytes) && memcmp(out, supportBytes, sizeof(supportBytes)) == 0);

	TIASAttribute* lsap = ias->FindClass("BMW")->FindAttribute("IrDA:IrLMP:LsapSel");
	EXPECT(segment.Init(out, sizeof(out)) == noErr);
	lsap->AddInfoToBuffer(&segment);
	const UByte lsapBytes[] = { 0, 1, 0, 0, 1, 0, 0, 0, 5 };
	Size written = segment.Position();
	EXPECT(written == (Size) sizeof(lsapBytes) && memcmp(out, lsapBytes, sizeof(lsapBytes)) == 0);

	// read back as a client reads an answer
	CBufferSegment in;
	EXPECT(in.Init(out, written) == noErr);
	TIASAttribute answer;
	EXPECT(answer.ExtractInfoFromBuffer(&in) == noErr);
	ULong value = 0;
	EXPECT(answer.GetArraySize() == 1 && ((TIASElement*) answer.At(0))->GetInteger(&value) == noErr && value == 5);
	// (strings are not read back)
	TIASAttribute* name = device->FindAttribute("DeviceName");
	EXPECT(segment.Init(out, sizeof(out)) == noErr);
	name->AddInfoToBuffer(&segment);
	EXPECT(in.Init(out, segment.Position()) == noErr);
	TIASAttribute nameAnswer;
	EXPECT(nameAnswer.ExtractInfoFromBuffer(&in) == kCommErrNotSupported);

	// taken out again: the attribute alone, then the class with it
	EXPECT(ias->RemoveAttribute("Device", "IrLMPSupport", 2) == noErr);
	EXPECT(device->GetArraySize() == 1);
	EXPECT(ias->RemoveAttribute("BMW", "IrDA:IrLMP:LsapSel", 3) == noErr);
	EXPECT(ias->FindClass("BMW") == nil && ias->GetArraySize() == 1);
	delete ias;
}


int
main()
{
	InitHostStandaloneHeap();
	TestQOS();
	TestDscInfo();
	TestIAS();
	printf("test_IrDAParts: %s\n", failures == 0 ? "all passed" : "FAILED");
	return failures != 0;
}
