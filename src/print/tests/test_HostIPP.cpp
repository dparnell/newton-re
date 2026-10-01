// Host unit test for the IPP protocol the host's IPP printers speak
// (print/host/HostIPP.h): printer URIs taken apart, a document's format
// told from its first bytes, the Print-Job request's bytes (RFC 8010's
// encoding: the version, the operation, the request id, the operation
// attributes and the end tag), the HTTP head, and printers' answers read -
// with a Content-Length, in chunks, and one cut short.

#include "print/host/HostIPP.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)


static void
TestURIs(void)
{
	HostIPPURI uri;
	EXPECT(HostIPPParseURI("ipp://printer.local:8631/ipp/print", &uri));
	EXPECT(strcmp(uri.fHost, "printer.local") == 0 && uri.fPort == 8631 && strcmp(uri.fPath, "/ipp/print") == 0);
	EXPECT(HostIPPParseURI("ipp://10.0.0.5/printers/laser", &uri));
	EXPECT(strcmp(uri.fHost, "10.0.0.5") == 0 && uri.fPort == 631 && strcmp(uri.fPath, "/printers/laser") == 0);
	EXPECT(HostIPPParseURI("http://host", &uri));
	EXPECT(uri.fPort == 80 && strcmp(uri.fPath, "/") == 0);
	EXPECT(HostIPPParseURI("ipps://host/ipp/print", &uri) && uri.fTLS && uri.fPort == 631);
	EXPECT(HostIPPParseURI("https://host/", &uri) && uri.fTLS && uri.fPort == 443);
	EXPECT(HostIPPParseURI("ipp://host/", &uri) && !uri.fTLS);
	EXPECT(!HostIPPParseURI("lpd://host/queue", &uri));
	EXPECT(!HostIPPParseURI("ipp://:631/x", &uri));
	EXPECT(!HostIPPParseURI("ipp://host:99999/x", &uri));
}


static void
TestFormats(void)
{
	EXPECT(strcmp(HostIPPDocumentFormat((const unsigned char*) "%!PS-Adobe-3.0\r", 15), "application/postscript") == 0);
	EXPECT(strcmp(HostIPPDocumentFormat((const unsigned char*) "\033%-12345X", 9), "application/vnd.hp-pcl") == 0);
	EXPECT(strcmp(HostIPPDocumentFormat((const unsigned char*) "\033%-12345X%!PS", 13), "application/postscript") == 0);
	EXPECT(strcmp(HostIPPDocumentFormat((const unsigned char*) "plain", 5), "application/octet-stream") == 0);
}


static void
TestRequest(void)
{
	unsigned char request[512];
	size_t n = HostIPPPrintJob("ipp://h/p", "newton", "job", "application/postscript", 0x01020304, request, sizeof(request));
	static const unsigned char expected[] =
		"\x01\x01" "\x00\x02" "\x01\x02\x03\x04"
		"\x01"
		"\x47\x00\x12" "attributes-charset" "\x00\x05" "utf-8"
		"\x48\x00\x1b" "attributes-natural-language" "\x00\x02" "en"
		"\x45\x00\x0b" "printer-uri" "\x00\x09" "ipp://h/p"
		"\x42\x00\x14" "requesting-user-name" "\x00\x06" "newton"
		"\x42\x00\x08" "job-name" "\x00\x03" "job"
		"\x49\x00\x0f" "document-format" "\x00\x16" "application/postscript"
		"\x03";
	EXPECT(n == sizeof(expected) - 1);
	EXPECT(memcmp(request, expected, sizeof(expected) - 1) == 0);
	EXPECT(HostIPPPrintJob("ipp://h/p", "newton", "job", "application/postscript", 1, request, 40) == 0);	// no room

	HostIPPURI uri;
	HostIPPParseURI("ipp://printer:631/ipp/print", &uri);
	char head[512];
	EXPECT(HostIPPHTTPHeader(&uri, head, sizeof(head)) > 0);
	EXPECT(strncmp(head, "POST /ipp/print HTTP/1.1\r\nHost: printer:631\r\n", 45) == 0);
	EXPECT(strstr(head, "Content-Type: application/ipp\r\n") != NULL);
	EXPECT(strstr(head, "Transfer-Encoding: chunked\r\n") != NULL);
	EXPECT(strcmp(head + strlen(head) - 4, "\r\n\r\n") == 0);
}


// an IPP answer: successful-ok (or the status given), and a job-id
static size_t
Answer(unsigned char* p, int status, int jobId)
{
	static const unsigned char head[] = "\x01\x01\x00\x00\x00\x00\x00\x01\x01\x47\x00\x12" "attributes-charset" "\x00\x05" "utf-8" "\x02\x21\x00\x06" "job-id" "\x00\x04";
	size_t n = sizeof(head) - 1;
	memcpy(p, head, n);
	p[2] = (unsigned char) (status >> 8);
	p[3] = (unsigned char) status;
	p[n++] = 0;
	p[n++] = 0;
	p[n++] = (unsigned char) (jobId >> 8);
	p[n++] = (unsigned char) jobId;
	p[n++] = 0x03;
	return n;
}


static void
TestResponses(void)
{
	unsigned char body[128];
	size_t bodySize = Answer(body, 0, 0x1234);
	unsigned char data[1024];
	HostIPPResponse response;

	// with a Content-Length: not yet, then whole
	size_t n = (size_t) sprintf((char*) data, "HTTP/1.1 200 OK\r\nContent-Type: application/ipp\r\nContent-Length: %u\r\n\r\n", (unsigned) bodySize);
	memcpy(data + n, body, bodySize);
	EXPECT(HostIPPParseResponse(data, n + bodySize - 3, false, &response) == 0);
	EXPECT(HostIPPParseResponse(data, n + bodySize, false, &response) == 1);
	EXPECT(response.fHTTPStatus == 200 && response.fIPPStatus == 0 && response.fJobId == 0x1234);

	// in chunks, the head lower-case
	bodySize = Answer(body, 0x040a, 7);
	n = (size_t) sprintf((char*) data, "HTTP/1.1 200 OK\r\ntransfer-encoding: chunked\r\n\r\n%x\r\n", 10);
	memcpy(data + n, body, 10);
	n += 10;
	n += (size_t) sprintf((char*) data + n, "\r\n%x\r\n", (unsigned) (bodySize - 10));
	memcpy(data + n, body + 10, bodySize - 10);
	n += bodySize - 10;
	EXPECT(HostIPPParseResponse(data, n, false, &response) == 0);
	n += (size_t) sprintf((char*) data + n, "\r\n0\r\n\r\n");
	EXPECT(HostIPPParseResponse(data, n, false, &response) == 1);
	EXPECT(response.fIPPStatus == 0x040a && response.fJobId == 7);
	EXPECT(strcmp(HostIPPStatusName(response.fIPPStatus), "client-error-document-format-not-supported") == 0);

	// neither: the end of the connection ends it
	n = (size_t) sprintf((char*) data, "HTTP/1.0 200 OK\r\n\r\n");
	memcpy(data + n, body, bodySize);
	EXPECT(HostIPPParseResponse(data, n + bodySize, false, &response) == 0);
	EXPECT(HostIPPParseResponse(data, n + bodySize, true, &response) == 1 && response.fJobId == 7);

	// an HTTP error, and not HTTP at all
	n = (size_t) sprintf((char*) data, "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n");
	EXPECT(HostIPPParseResponse(data, n, false, &response) == 1 && response.fHTTPStatus == 404 && response.fIPPStatus == -1);
	EXPECT(HostIPPParseResponse((const unsigned char*) "SSH-2.0", 7, true, &response) == -1);
}


// whether bytes hold a text (memmem is not everywhere)
static bool
Holds(const unsigned char* data, size_t size, const char* text)
{
	size_t n = strlen(text);
	for (size_t i = 0; i + n <= size; i++)
		if (memcmp(data + i, text, n) == 0)
			return true;
	return false;
}


// a printer's answer to Get-Printer-Attributes: its state and reasons
static size_t
StateAnswer(unsigned char* p, int state, const char* const* reasons)
{
	unsigned char* start = p;
	*p++ = 1; *p++ = 1; *p++ = 0; *p++ = 0;		// successful-ok
	*p++ = 0; *p++ = 0; *p++ = 0; *p++ = 2;
	*p++ = 0x04;								// printer-attributes-tag
	*p++ = 0x23; *p++ = 0; *p++ = 13; memcpy(p, "printer-state", 13); p += 13;
	*p++ = 0; *p++ = 4; *p++ = 0; *p++ = 0; *p++ = 0; *p++ = (unsigned char) state;
	for (int i = 0; reasons[i] != NULL; i++)
	{
		size_t length = strlen(reasons[i]);
		*p++ = 0x44;
		if (i == 0)
		{
			*p++ = 0; *p++ = 21; memcpy(p, "printer-state-reasons", 21); p += 21;
		}
		else
		{
			*p++ = 0; *p++ = 0;
		}
		*p++ = 0; *p++ = (unsigned char) length; memcpy(p, reasons[i], length); p += length;
	}
	*p++ = 0x44; *p++ = 0; *p++ = 4; memcpy(p, "more", 4); p += 4;		// another attribute's
	*p++ = 0; *p++ = 5; memcpy(p, "thing", 5); p += 5;
	*p++ = 0x03;
	return (size_t) (p - start);
}


static HostIPPCondition
ConditionOf(int state, const char* const* reasons, HostIPPResponse* response)
{
	unsigned char body[512], data[1024];
	size_t bodySize = StateAnswer(body, state, reasons);
	size_t n = (size_t) sprintf((char*) data, "HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n", (unsigned) bodySize);
	memcpy(data + n, body, bodySize);
	EXPECT(HostIPPParseResponse(data, n + bodySize, false, response) == 1);
	return HostIPPPrinterCondition(response);
}


static void
TestPrinterState(void)
{
	// the request: Get-Printer-Attributes asking for the two attributes
	unsigned char request[512];
	size_t size = HostIPPGetPrinterState("ipp://h/ipp/print", 9, request, sizeof(request));
	EXPECT(size > 9 && request[2] == 0x00 && request[3] == 0x0b && request[7] == 9 && request[size - 1] == 0x03);
	EXPECT(Holds(request, size, "requested-attributes") && Holds(request, size, "printer-state-reasons"));
	HostIPPURI uri;
	HostIPPParseURI("ipp://h:631/ipp/print", &uri);
	char head[256];
	EXPECT(HostIPPHTTPRequestHeader(&uri, size, head, sizeof(head)) > 0 && strstr(head, "Content-Length: ") != NULL);
	EXPECT(strstr(head, "chunked") == NULL);

	// the answers, and the problems they come to
	HostIPPResponse response;
	const char* none[] = { "none", NULL };
	const char* paper[] = { "media-empty-error", NULL };
	const char* jam[] = { "toner-low-warning", "media-jam", NULL };
	const char* door[] = { "cover-open", NULL };
	const char* ink[] = { "marker-supply-empty-error", NULL };
	const char* warning[] = { "media-empty-warning", NULL };
	EXPECT(ConditionOf(3, none, &response) == kHostIPPReady);
	EXPECT(response.fPrinterState == 3 && strcmp(response.fStateReasons, "none") == 0);
	EXPECT(ConditionOf(5, paper, &response) == kHostIPPNoPaper);
	EXPECT(ConditionOf(5, jam, &response) == kHostIPPJammed);
	EXPECT(strcmp(response.fStateReasons, "toner-low-warning,media-jam") == 0);
	EXPECT(ConditionOf(5, door, &response) == kHostIPPDoorOpen);
	EXPECT(ConditionOf(4, ink, &response) == kHostIPPNoInk);
	EXPECT(ConditionOf(4, warning, &response) == kHostIPPReady);
	EXPECT(ConditionOf(5, none, &response) == kHostIPPOffLine);

	// what became of a job
	response.fHTTPStatus = 200; response.fIPPStatus = 0;
	EXPECT(HostIPPJobResult(1, &response) == kHostIPPJobPrinted);
	response.fIPPStatus = 0x0507;
	EXPECT(HostIPPJobResult(1, &response) == kHostIPPJobBusy);
	response.fIPPStatus = 0x040a;
	EXPECT(HostIPPJobResult(1, &response) == kHostIPPJobRefused);
	response.fHTTPStatus = 503; response.fIPPStatus = -1;
	EXPECT(HostIPPJobResult(1, &response) == kHostIPPJobBusy);
	response.fHTTPStatus = 404;
	EXPECT(HostIPPJobResult(1, &response) == kHostIPPJobRefused);
	EXPECT(HostIPPJobResult(-1, &response) == kHostIPPJobNoAnswer);
}


int
main()
{
	TestURIs();
	TestFormats();
	TestRequest();
	TestResponses();
	TestPrinterState();
	if (failures == 0)
		printf("test_HostIPP: all passed\n");
	else
		printf("test_HostIPP: %d failures\n", failures);
	return failures != 0;
}
