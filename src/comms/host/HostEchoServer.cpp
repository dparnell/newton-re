/*
	File:		comms/host/HostEchoServer.cpp

	Contains:	The host's TCP echo server (HostEchoServer.h).
*/

#include "HostEchoServer.h"
#include "HostSockets.h"

#include <atomic>
#include <thread>
#include <chrono>

static std::atomic<bool>	sStop(false);
static int					sListener = -1;
static std::thread*			sThread = nullptr;


static void
EchoLoop(void)
{
	int client = -1;
	while (!sStop)
	{
		if (client < 0)
		{
			if (HostTCPAccept(sListener, &client, nullptr, nullptr) != kHostSocketOK)
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
				while (done < got && !sStop)
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


uint16_t
HostStartEchoServer(uint16_t port)
{
	if (sThread != nullptr)
		return 0;
	if (HostSocketsInit() != kHostSocketOK)
		return 0;
	if (HostTCPListen(0x7F000001, &port, &sListener) != kHostSocketOK)
		return 0;
	sStop = false;
	sThread = new std::thread(EchoLoop);
	return port;
}


void
HostStopEchoServer(void)
{
	if (sThread == nullptr)
		return;
	sStop = true;
	sThread->join();
	delete sThread;
	sThread = nullptr;
	HostSocketClose(sListener);
	sListener = -1;
}
