// Comm tool test (comms/CommTools.h, comms/host/HostTCPTool.h) - milestone
// M0 of docs/comms/README.md: the host's TCP tool started as its own task
// (StartCommTool: the task, and its port found through the name server),
// then driven the way an endpoint drives a tool, with the requests of the
// DDK's CommTool.h sent to its port - open, connect (the NIE's 'itrs
// option naming a TCP echo server this test runs on 127.0.0.1), put
// "hello", get it back, a non-blocking get of whatever is there, disconnect
// and close.  The requests' buffers are CBufferLists in this task's memory
// (the ROM's single address space), and once as an outside shared-memory
// object read through the tool's CShadowBufferSegment.
//
// Also: a connect to a port nobody listens on fails; the tool refuses a get
// before it is connected (kCommErrNotConnected); an option it does not
// know is marked not processed.

#include "CommTools.h"
#include "CommManagerInterface.h"
#include "HostTCPTool.h"
#include "BufferList.h"
#include "HostSockets.h"
#include "UserSharedMem.h"
#include "SharedTypes.h"
#include "NewtErrors.h"
#include "CommErrors.h"
#include "Boot.h"
#include "UserBoot.h"
#include "host/TaskRuntime.h"

#include <stdio.h>
#include <string.h>
#include <atomic>
#include <thread>
#include <chrono>

static int failures = 0;
static bool sScenarioDone = false;		// the scenario ran to its end
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


// ---------------------------------------------------------------------------
//	A TCP echo server on 127.0.0.1, in a host thread of its own (it touches
//	nothing of the Newton's)
// ---------------------------------------------------------------------------

static std::atomic<bool>	sServerStop(false);
static uint16_t				sServerPort = 0;
static int					sServerListener = -1;

static void
EchoServer()
{
	int client = -1;
	while (!sServerStop)
	{
		if (client < 0)
		{
			if (HostTCPAccept(sServerListener, &client, nil, nil) != kHostSocketOK)
				client = -1;
		}
		else
		{
			char buffer[256];
			size_t got;
			int result = HostSocketReceive(client, buffer, sizeof(buffer), &got);
			if (result == kHostSocketOK)
			{
				size_t done = 0;
				while (done < got)
				{
					size_t sent;
					if (HostSocketSend(client, buffer + done, got - done, &sent) == kHostSocketOK)
						done += sent;
					else
						std::this_thread::sleep_for(std::chrono::milliseconds(1));
				}
			}
			else if (result == kHostSocketClosed || result < 0)
			{
				HostSocketClose(client);
				client = -1;
			}
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	if (client >= 0)
		HostSocketClose(client);
}


// ---------------------------------------------------------------------------
//	Talking to the tool
// ---------------------------------------------------------------------------

// the NIE's remote socket option: four address bytes and a port
struct TInetRemoteSocket : public TOption
{
	TInetRemoteSocket(uint32_t address, uint16_t port) : TOption(kOptionType)
	{
		SetAsOption(kInetRemoteSocketOption);
		SetLength(6);
		fData[0] = address >> 24;
		fData[1] = address >> 16;
		fData[2] = address >> 8;
		fData[3] = address;
		fData[4] = port >> 8;
		fData[5] = port;
		fData[6] = fData[7] = 0;
	}
	UByte	fData[8];
};

static NewtonErr
Control(TUPort& port, TCommToolControlRequest& request, ULong size, TCommToolReply& reply, ULong replySize)
{
	ULong returnSize;
	NewtonErr err = port.SendRPC(&returnSize, &request, size, &reply, replySize, 0, kCommToolRequestTypeControl);
	return err != noErr ? err : reply.fResult;
}


static void
Scenario(void)
{
	EXPECT(HostSocketsInit() == kHostSocketOK);
	sServerPort = 0;
	EXPECT(HostTCPListen(0x7F000001, &sServerPort, &sServerListener) == kHostSocketOK);
	std::thread server(EchoServer);

	// start the tool
	THostTCPTool* tool = new THostTCPTool('tcp ');
	TServiceInfo info;
	EXPECT(StartCommTool(tool, 'tcp ', &info) == noErr);
	delete tool;							// (the task has its own copy)
	TUPort port(info.GetPortId());
	EXPECT(info.GetPortId() != 0 && info.GetServiceId() == 'tcp ');
	TUPort found;
	EXPECT(ServiceToPort('tcp ', &found, 0) != noErr);	// registered under the tool's task, not nought

	// open
	TCommToolOpenRequest open;
	TCommToolOpenReply openReply;
	EXPECT(Control(port, open, sizeof(open), openReply, sizeof(openReply)) == noErr);
	EXPECT(openReply.fPortId == info.GetPortId());

	// a get before the connect is refused
	{
		UByte bytes[4];
		CBufferSegment* segment = new CBufferSegment;
		segment->Init(bytes, sizeof(bytes));
		CBufferList* list = CBufferList::New();
		list->Init(true);
		list->Insert(segment);
		TCommToolGetRequest get;
		get.fOptions = nil;
		get.fOptionCount = 0;
		get.fData = list;
		TCommToolGetReply getReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &get, sizeof(get), &getReply, sizeof(getReply), 0, kCommToolRequestTypeGet) == noErr);
		EXPECT(getReply.fResult == kCommErrNotConnected);
		list->Delete();
	}

	// a connect to a port nobody listens on fails
	{
		uint16_t deadPort = 0;
		int dead;
		EXPECT(HostTCPListen(0x7F000001, &deadPort, &dead) == kHostSocketOK);
		HostSocketClose(dead);
		TOptionArray options;
		options.Init();
		TInetRemoteSocket remote(0x7F000001, deadPort);
		options.AppendOption(&remote);
		TCommToolConnectRequest connect;
		connect.fOptions = &options;
		connect.fOptionCount = options.GetArrayCount();
		TCommToolConnectReply connectReply;
		EXPECT(Control(port, connect, sizeof(connect), connectReply, sizeof(connectReply)) != noErr);
	}

	// connect to the echo server, with an option nobody knows
	{
		TOptionArray options;
		options.Init();
		TInetRemoteSocket remote(0x7F000001, sServerPort);
		options.AppendOption(&remote);
		TOption unknown;
		unknown.SetAsOption('zzzz');
		options.AppendOption(&unknown);
		TCommToolConnectRequest connect;
		connect.fOptions = &options;
		connect.fOptionCount = options.GetArrayCount();
		TCommToolConnectReply connectReply;
		EXPECT(Control(port, connect, sizeof(connect), connectReply, sizeof(connectReply)) == noErr);
		EXPECT(options.OptionAt(0)->IsProcessed() && options.OptionAt(0)->GetOpCodeResults() == opSuccess);
		EXPECT(!options.OptionAt(1)->IsProcessed() && options.OptionAt(1)->GetOpCodeResults() == (ULong) (SByte) 0xFC);
	}

	// put "hello"
	{
		static UByte hello[] = "hello";
		CBufferSegment* segment = new CBufferSegment;
		segment->Init(hello, 5);
		CBufferList* list = CBufferList::New();
		list->Init(true);
		list->Insert(segment);
		TCommToolPutRequest put;
		put.fOptions = nil;				// (the constructor leaves the 2.0 fields alone, as the ROM's does)
		put.fOptionCount = 0;
		put.fData = list;
		put.fValidCount = -1;
		TCommToolPutReply putReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &put, sizeof(put), &putReply, sizeof(putReply), 0, kCommToolRequestTypePut) == noErr);
		EXPECT(putReply.fResult == noErr && putReply.fPutBytesCount == 5);
		list->Delete();
	}

	// get it back, into a buffer of five
	{
		UByte bytes[5];
		memset(bytes, 0, sizeof(bytes));
		CBufferSegment* segment = new CBufferSegment;
		segment->Init(bytes, sizeof(bytes));
		CBufferList* list = CBufferList::New();
		list->Init(true);
		list->Insert(segment);
		TCommToolGetRequest get;
		get.fOptions = nil;
		get.fOptionCount = 0;
		get.fData = list;
		TCommToolGetReply getReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &get, sizeof(get), &getReply, sizeof(getReply), 0, kCommToolRequestTypeGet) == noErr);
		EXPECT(getReply.fResult == noErr && getReply.fGetBytesCount == 5);
		EXPECT(memcmp(bytes, "hello", 5) == 0);
		list->Delete();
	}

	// an outside put and a non-blocking get: the data in shared-memory
	// objects, as another task's endpoint sends it
	{
		UByte out[] = { 'w', 'o', 'r', 'l', 'd' };
		TUSharedMem outMem;
		EXPECT(outMem.Init() == noErr && outMem.SetBuffer(out, sizeof(out), kSMemReadOnly) == noErr);
		TCommToolPutRequest put;
		put.fOptions = nil;				// (the constructor leaves the 2.0 fields alone, as the ROM's does)
		put.fOptionCount = 0;
		put.fData = (CBufferList*) (ULong) outMem.fId;
		put.fValidCount = sizeof(out);
		put.fOutside = true;
		TCommToolPutReply putReply;
		ULong returnSize;
		EXPECT(port.SendRPC(&returnSize, &put, sizeof(put), &putReply, sizeof(putReply), 0, kCommToolRequestTypePut) == noErr);
		EXPECT(putReply.fResult == noErr && putReply.fPutBytesCount == 5);

		UByte in[16];
		memset(in, 0, sizeof(in));
		TUSharedMem inMem;
		EXPECT(inMem.Init() == noErr && inMem.SetBuffer(in, sizeof(in), kSMemReadWrite | kSMemNoSizeChangeOnCopyTo) == noErr);
		TCommToolGetRequest get;
		get.fOptions = nil;
		get.fOptionCount = 0;
		get.fData = (CBufferList*) (ULong) inMem.fId;
		get.fOutside = true;
		get.fNonBlocking = true;
		get.fThreshold = 5;
		TCommToolGetReply getReply;
		EXPECT(port.SendRPC(&returnSize, &get, sizeof(get), &getReply, sizeof(getReply), 0, kCommToolRequestTypeGet) == noErr);
		EXPECT(getReply.fResult == noErr && getReply.fGetBytesCount == 5);
		EXPECT(memcmp(in, "world", 5) == 0);
	}

	// disconnect and close
	TCommToolDisconnectRequest disconnect;
	TCommToolReply reply;
	EXPECT(Control(port, disconnect, sizeof(disconnect), reply, sizeof(reply)) == noErr);
	TCommToolControlRequest close;
	close.fOpCode = kCommToolClose;
	EXPECT(Control(port, close, sizeof(close), reply, sizeof(reply)) == noErr);

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
		printf("test_CommTool: all passed\n");
	else
		printf("test_CommTool: %d failures\n", failures);
	return failures != 0;
}
