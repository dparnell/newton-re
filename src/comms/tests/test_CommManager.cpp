// Comm manager test (comms/CommManager.h, comms/host/HostServices.h) -
// milestone M1 of docs/comms/README.md: the 'cmgr task started
// (InitializeCommManager), the host's 'inet service registered, and
// CMStartService given an endpoint's options - the NIE's 'inet service
// option and its 'itrs remote socket - answers the port of a TCP tool the
// service started and opened with them (the open's reply coming back to the
// comm manager, AECompletionProc, DoneStarting).  The tool then connects to
// the test's echo server, and bytes go there and back.
//
// Also: options naming no service answer kCMErr_NoServiceSpecified (-26004),
// a service nobody provides kCMErr_ServiceNotFound (-26005); the last
// device is kept (CMSetLastDevice, CMGetLastDevice); the service's version
// is not recorded (CMGetServiceVersion).

#include "CommManager.h"
#include "CommTools.h"
#include "HostServices.h"
#include "BufferList.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include "EchoServer.h"

static int failures = 0;
static bool sScenarioDone = false;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
Scenario(void)
{
	EXPECT(HostSocketsInit() == kHostSocketOK);
	EXPECT(HostTCPListen(0x7F000001, &sServerPort, &sServerListener) == kHostSocketOK);
	std::thread server(EchoServer);

	EXPECT(InitializeCommManager() == noErr);
	EXPECT(InitializeCommManager() == -26000);		// running already
	RegisterHostCommServices();

	// no service, and a service nobody provides
	{
		TOptionArray options;
		options.Init();
		TInetRemoteSocket remote(0x7F000001, sServerPort);
		options.AppendOption(&remote);
		TServiceInfo info;
		EXPECT(CMStartService(&options, &info) == -26004);
		TOption service;
		service.SetAsService('none');
		options.AppendOption(&service);
		EXPECT(CMStartService(&options, &info) == -26005);
	}

	// the 'inet service: a tool started and opened
	TOptionArray options;
	options.Init();
	TOption service;
	service.SetAsService('inet');
	options.AppendOption(&service);
	TInetRemoteSocket remote(0x7F000001, sServerPort);
	options.AppendOption(&remote);
	TServiceInfo info;
	EXPECT(CMStartService(&options, &info) == noErr);
	EXPECT(info.GetPortId() != 0 && info.GetServiceId() == 'inet');
	TUPort port(info.GetPortId());

	// connect (the remote socket came with the open) and echo
	TCommToolConnectRequest connect;
	TCommToolConnectReply connectReply;
	EXPECT(Control(port, connect, sizeof(connect), connectReply, sizeof(connectReply)) == noErr);
	{
		static UByte hello[] = "hello";
		CBufferSegment* segment = new CBufferSegment;
		segment->Init(hello, 5);
		CBufferList* list = CBufferList::New();
		list->Init(true);
		list->Insert(segment);
		TCommToolPutRequest put;
		put.fData = list;
		put.fValidCount = -1;
		put.fOptions = nil;
		put.fOptionCount = 0;
		TCommToolPutReply putReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &put, sizeof(put), &putReply, sizeof(putReply), 0, kCommToolRequestTypePut) == noErr);
		EXPECT(putReply.fResult == noErr && putReply.fPutBytesCount == 5);
		list->Delete();
	}
	{
		UByte bytes[5];
		memset(bytes, 0, sizeof(bytes));
		CBufferSegment* segment = new CBufferSegment;
		segment->Init(bytes, sizeof(bytes));
		CBufferList* list = CBufferList::New();
		list->Init(true);
		list->Insert(segment);
		TCommToolGetRequest get;
		get.fData = list;
		get.fOptions = nil;
		get.fOptionCount = 0;
		TCommToolGetReply getReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &get, sizeof(get), &getReply, sizeof(getReply), 0, kCommToolRequestTypeGet) == noErr);
		EXPECT(getReply.fResult == noErr && getReply.fGetBytesCount == 5 && memcmp(bytes, "hello", 5) == 0);
		list->Delete();
	}
	TCommToolDisconnectRequest disconnect;
	TCommToolReply reply;
	EXPECT(Control(port, disconnect, sizeof(disconnect), reply, sizeof(reply)) == noErr);
	TCommToolControlRequest close;
	close.fOpCode = kCommToolClose;
	EXPECT(Control(port, close, sizeof(close), reply, sizeof(reply)) == noErr);

	// the last device
	TConnectedDevice device, last;
	memset(&device, 0, sizeof(device));
	device.fDeviceType = 'dock';
	device.fManufacturer = 'appl';
	EXPECT(CMGetLastDevice(&last) == -26008);
	EXPECT(CMSetLastDevice(&device) == noErr);
	EXPECT(CMGetLastDevice(&last) == noErr && last.fDeviceType == 'dock' && last.fManufacturer == 'appl');

	ULong version = 0;
	EXPECT(CMGetServiceVersion('inet', &version) == -26030);	// the host's service has no 'vern

	sServerStop = true;
	server.join();
	HostSocketClose(sServerListener);
	sScenarioDone = true;
	HostStopTasks();
}


int
main()
{
	gHostKernelServicesTask = Scenario;
	OsBoot();
	EXPECT(sScenarioDone);
	if (failures == 0)
		printf("test_CommManager: all passed\n");
	else
		printf("test_CommManager: %d failures\n", failures);
	return failures != 0;
}
