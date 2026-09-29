// The comms tests' TCP echo server (on 127.0.0.1, in a host thread of its
// own - it touches nothing of the Newton's), the NIE's remote socket
// option, and a control request sent to a tool.

#include "HostSockets.h"
#include "HostTCPTool.h"
#include <atomic>
#include <thread>
#include <chrono>


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


