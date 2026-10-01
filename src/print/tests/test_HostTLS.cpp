/*
	File:		print/tests/test_HostTLS.cpp

	Contains:	A test of the host's TLS (print/host/tls/HostTLS.h) against
				tools/print/ippprinter.py --tls (ctest print.HostTLS): the
				printer's address from NEWTON_IPP_PRINTER, a TCP connection
				of the test's own, the handshake, a Get-Printer-Attributes
				request written through the session and the printer's answer
				read back; the certificate's fingerprint is printed (the
				ctest checks it is the test certificate's), and the system's
				store must not vouch for a self-signed certificate.
*/

#include "HostTLS.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET Socket;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
typedef int Socket;
#define closesocket close
#endif

static int gFailures = 0;

static void
Check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "ok" : "FAILED", what);
	if (!ok)
		gFailures++;
}


// everything the session has to send, sent
static bool
Flush(HostTLS* tls, Socket s)
{
	unsigned char buffer[8192];
	size_t n;
	while ((n = HostTLSTakeToSend(tls, buffer, sizeof(buffer))) > 0)
		if (send(s, (const char*) buffer, (int) n, 0) != (int) n)
			return false;
	return true;
}


// what the peer sends next, handed in; ==> false when it has closed
static bool
Receive(HostTLS* tls, Socket s)
{
	unsigned char buffer[8192];
	int n = recv(s, (char*) buffer, sizeof(buffer), 0);
	if (n <= 0)
		return false;
	HostTLSPutReceived(tls, buffer, (size_t) n);
	return true;
}


static void
Put(std::string& out, unsigned char tag, const char* name, const char* value)
{
	size_t n = strlen(name), v = strlen(value);
	out += (char) tag;
	out += (char) (n >> 8); out += (char) n; out += name;
	out += (char) (v >> 8); out += (char) v; out += value;
}


int
main()
{
#if defined(_WIN32)
	WSADATA wsa;
	WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	const char* uri = getenv("NEWTON_IPP_PRINTER");
	int port = 0;
	if (uri == NULL || sscanf(uri, "ipps://127.0.0.1:%d", &port) != 1)
	{
		printf("test_HostTLS: no ipps printer (run under tools/print/ippprinter.py --tls)\n");
		return 1;
	}
	Check(HostTLSAvailable() != 0, "this host has TLS");
	Socket s = socket(AF_INET, SOCK_STREAM, 0);
	sockaddr_in address;
	memset(&address, 0, sizeof(address));
	address.sin_family = AF_INET;
	address.sin_port = htons((unsigned short) port);
	address.sin_addr.s_addr = htonl(0x7f000001);
	Check(connect(s, (sockaddr*) &address, sizeof(address)) == 0, "connected");

	HostTLS* tls = HostTLSNew("127.0.0.1");
	int state = 0;
	for (int round = 0; round < 50 && state == 0; round++)
	{
		state = HostTLSHandshake(tls);
		if (!Flush(tls, s))
			break;
		if (state == 0 && !Receive(tls, s))
			break;
	}
	Check(state == 1, "the handshake");
	if (state != 1)
		printf("  %s\n", HostTLSError(tls));
	char fingerprint[96];
	HostTLSFingerprint(tls, fingerprint);
	printf("the certificate's SHA-256 %s\n", fingerprint);
	Check(strlen(fingerprint) == 95, "a fingerprint");
	Check(!HostTLSSystemTrusts(tls), "the system does not vouch for a self-signed certificate");

	// a Get-Printer-Attributes, and the answer
	std::string body("\x01\x01\x00\x0b\x00\x00\x00\x01\x01", 9);
	Put(body, 0x47, "attributes-charset", "utf-8");
	Put(body, 0x48, "attributes-natural-language", "en");
	Put(body, 0x45, "printer-uri", uri);
	body += (char) 0x03;
	char head[256];
	snprintf(head, sizeof(head), "POST /ipp/print HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/ipp\r\n"
		"Content-Length: %u\r\nConnection: close\r\n\r\n", port, (unsigned) body.size());
	std::string request = std::string(head) + body;
	Check(HostTLSWrite(tls, (const unsigned char*) request.data(), request.size()) == 0, "the request written");
	Flush(tls, s);
	std::string answer;
	for (int round = 0; round < 100; round++)
	{
		unsigned char plain[4096];
		long n;
		while ((n = HostTLSRead(tls, plain, sizeof(plain))) > 0)
			answer.append((const char*) plain, (size_t) n);
		if (n < 0 || !Receive(tls, s))
		{
			while ((n = HostTLSRead(tls, plain, sizeof(plain))) > 0)
				answer.append((const char*) plain, (size_t) n);
			break;
		}
	}
	Check(answer.compare(0, 12, "HTTP/1.1 200") == 0, "the printer's answer read back");
	Check(answer.find("printer-state") != std::string::npos, "its attributes");
	HostTLSFree(tls);
	closesocket(s);
	if (gFailures == 0)
		printf("test_HostTLS: all passed\n");
	else
		printf("test_HostTLS: %d FAILED\n", gFailures);
	return gFailures != 0;
}
