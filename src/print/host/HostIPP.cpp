/*
	File:		print/host/HostIPP.cpp

	Contains:	The IPP protocol as the host's IPP printer speaks it
				(HostIPP.h): printer URIs, the Print-Job request, the HTTP
				around it and the printer's answer.  Plain C++.

	Host only (RFC 8010, RFC 8011, RFC 7230's chunked transfer coding).
*/

#include "HostIPP.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>


bool
HostIPPParseURI(const char* uri, HostIPPURI* parts)
{
	if (uri == NULL)
		return false;
	uint16_t defaultPort;
	const char* rest;
	if (strncmp(uri, "ipp://", 6) == 0)
	{
		defaultPort = 631;
		rest = uri + 6;
	}
	else if (strncmp(uri, "http://", 7) == 0)
	{
		defaultPort = 80;
		rest = uri + 7;
	}
	else
		return false;			// (ipps:// and https:// want TLS, which the host's sockets do not do)
	const char* hostEnd = rest;
	while (*hostEnd != 0 && *hostEnd != ':' && *hostEnd != '/')
		hostEnd++;
	size_t hostLength = (size_t) (hostEnd - rest);
	if (hostLength == 0 || hostLength >= sizeof(parts->fHost))
		return false;
	memcpy(parts->fHost, rest, hostLength);
	parts->fHost[hostLength] = 0;
	parts->fPort = defaultPort;
	const char* p = hostEnd;
	if (*p == ':')
	{
		char* end;
		long port = strtol(p + 1, &end, 10);
		if (end == p + 1 || port <= 0 || port > 65535)
			return false;
		parts->fPort = (uint16_t) port;
		p = end;
	}
	if (*p == 0)
		strcpy(parts->fPath, "/");
	else if (*p == '/')
	{
		if (strlen(p) >= sizeof(parts->fPath))
			return false;
		strcpy(parts->fPath, p);
	}
	else
		return false;
	return true;
}


const char*
HostIPPDocumentFormat(const unsigned char* data, size_t size)
{
	// a PJL job ("ESC %-12345X@PJL ...") may go on in PostScript
	static const char kUEL[] = "\033%-12345X";
	if (size >= 2 && data[0] == '%' && data[1] == '!')
		return "application/postscript";
	if (size >= 11 && memcmp(data, kUEL, 9) == 0 && data[9] == '%' && data[10] == '!')
		return "application/postscript";
	if (size >= 1 && data[0] == 0x1b)
		return "application/vnd.hp-pcl";
	return "application/octet-stream";
}


// One attribute: its value tag, name (empty for another value of the same
// attribute) and value.
static size_t
PutAttribute(unsigned char* p, size_t room, unsigned char tag, const char* name, const char* value)
{
	size_t nameLength = strlen(name);
	size_t valueLength = strlen(value);
	size_t need = 1 + 2 + nameLength + 2 + valueLength;
	if (need > room || nameLength > 0xffff || valueLength > 0xffff)
		return 0;
	*p++ = tag;
	*p++ = (unsigned char) (nameLength >> 8);
	*p++ = (unsigned char) nameLength;
	memcpy(p, name, nameLength);
	p += nameLength;
	*p++ = (unsigned char) (valueLength >> 8);
	*p++ = (unsigned char) valueLength;
	memcpy(p, value, valueLength);
	return need;
}


size_t
HostIPPPrintJob(const char* printerURI, const char* user, const char* jobName, const char* format, uint32_t requestId, unsigned char* buffer, size_t size)
{
	if (size < 9)
		return 0;
	unsigned char* p = buffer;
	*p++ = 1;					// version 1.1
	*p++ = 1;
	*p++ = 0x00;				// Print-Job
	*p++ = 0x02;
	*p++ = (unsigned char) (requestId >> 24);
	*p++ = (unsigned char) (requestId >> 16);
	*p++ = (unsigned char) (requestId >> 8);
	*p++ = (unsigned char) requestId;
	*p++ = 0x01;				// operation-attributes-tag
	struct { unsigned char tag; const char* name; const char* value; } attrs[] =
	{
		{ 0x47, "attributes-charset", "utf-8" },
		{ 0x48, "attributes-natural-language", "en" },
		{ 0x45, "printer-uri", printerURI },
		{ 0x42, "requesting-user-name", user },
		{ 0x42, "job-name", jobName },
		{ 0x49, "document-format", format }
	};
	for (size_t i = 0; i < sizeof(attrs) / sizeof(attrs[0]); i++)
	{
		size_t n = PutAttribute(p, size - (size_t) (p - buffer), attrs[i].tag, attrs[i].name, attrs[i].value);
		if (n == 0)
			return 0;
		p += n;
	}
	if ((size_t) (p - buffer) >= size)
		return 0;
	*p++ = 0x03;				// end-of-attributes-tag
	return (size_t) (p - buffer);
}


size_t
HostIPPHTTPHeader(const HostIPPURI* uri, char* buffer, size_t size)
{
	int n = snprintf(buffer, size,
		"POST %s HTTP/1.1\r\n"
		"Host: %s:%u\r\n"
		"Content-Type: application/ipp\r\n"
		"Transfer-Encoding: chunked\r\n"
		"User-Agent: Newton MessagePad (host)\r\n"
		"Connection: close\r\n"
		"\r\n",
		uri->fPath, uri->fHost, (unsigned) uri->fPort);
	if (n < 0 || (size_t) n >= size)
		return 0;
	return (size_t) n;
}


// the IPP message in a body: its status-code, and the first integer
// attribute named job-id
static void
ParseIPPMessage(const unsigned char* body, size_t size, HostIPPResponse* response)
{
	if (size < 8)
		return;
	response->fIPPStatus = (body[2] << 8) | body[3];
	size_t i = 8;
	while (i < size)
	{
		unsigned char tag = body[i++];
		if (tag == 0x03)
			break;
		if (tag < 0x10)
			continue;			// a delimiter: the next group
		if (i + 2 > size)
			break;
		size_t nameLength = (body[i] << 8) | body[i + 1];
		i += 2;
		if (i + nameLength + 2 > size)
			break;
		const unsigned char* name = body + i;
		i += nameLength;
		size_t valueLength = (body[i] << 8) | body[i + 1];
		i += 2;
		if (i + valueLength > size)
			break;
		if (tag == 0x21 && valueLength == 4 && nameLength == 6 && memcmp(name, "job-id", 6) == 0 && response->fJobId < 0)
			response->fJobId = (int32_t) (((uint32_t) body[i] << 24) | (body[i + 1] << 16) | (body[i + 2] << 8) | body[i + 3]);
		i += valueLength;
	}
}


int
HostIPPParseResponse(const unsigned char* data, size_t size, bool connectionClosed, HostIPPResponse* response)
{
	response->fHTTPStatus = 0;
	response->fIPPStatus = -1;
	response->fJobId = -1;
	if (size >= 5 && memcmp(data, "HTTP/", 5) != 0)
		return -1;
	// the head
	const unsigned char* headEnd = NULL;
	for (size_t i = 0; i + 3 < size; i++)
		if (data[i] == '\r' && data[i + 1] == '\n' && data[i + 2] == '\r' && data[i + 3] == '\n')
		{
			headEnd = data + i + 4;
			break;
		}
	if (headEnd == NULL)
		return connectionClosed ? -1 : 0;
	const char* sp = (const char*) memchr(data, ' ', (size_t) (headEnd - data));
	if (sp == NULL)
		return -1;
	response->fHTTPStatus = atoi(sp + 1);
	// its Content-Length or chunks
	long contentLength = -1;
	bool chunked = false;
	const unsigned char* line = data;
	while (line < headEnd)
	{
		const unsigned char* eol = line;
		while (eol < headEnd && *eol != '\n')
			eol++;
		size_t n = (size_t) (eol - line);
		char header[256];
		if (n >= sizeof(header))
			n = sizeof(header) - 1;
		for (size_t i = 0; i < n; i++)
			header[i] = (char) tolower(line[i]);
		header[n] = 0;
		if (strncmp(header, "content-length:", 15) == 0)
			contentLength = atol(header + 15);
		else if (strncmp(header, "transfer-encoding:", 18) == 0 && strstr(header, "chunked") != NULL)
			chunked = true;
		line = eol + 1;
	}
	size_t bodySize = size - (size_t) (headEnd - data);
	if (chunked)
	{
		// the chunks put together (an IPP answer is small)
		static unsigned char body[65536];
		size_t have = 0;
		const unsigned char* p = headEnd;
		const unsigned char* end = data + size;
		bool done = false;
		while (p < end)
		{
			const unsigned char* eol = p;
			while (eol + 1 < end && !(eol[0] == '\r' && eol[1] == '\n'))
				eol++;
			if (eol + 1 >= end)
				break;
			unsigned long length = strtoul((const char*) p, NULL, 16);
			p = eol + 2;
			if (length == 0)
			{
				done = true;
				break;
			}
			if ((size_t) (end - p) < length + 2)
				break;
			if (have + length <= sizeof(body))
			{
				memcpy(body + have, p, length);
				have += length;
			}
			p += length + 2;
		}
		if (!done && !connectionClosed)
			return 0;
		ParseIPPMessage(body, have, response);
		return 1;
	}
	if (contentLength >= 0)
	{
		if (bodySize < (size_t) contentLength && !connectionClosed)
			return 0;
		ParseIPPMessage(headEnd, bodySize < (size_t) contentLength ? bodySize : (size_t) contentLength, response);
		return 1;
	}
	if (!connectionClosed)
		return 0;
	ParseIPPMessage(headEnd, bodySize, response);
	return 1;
}


const char*
HostIPPStatusName(int status)
{
	switch (status)
	{
	case 0x0000:	return "successful-ok";
	case 0x0001:	return "successful-ok-ignored-or-substituted-attributes";
	case 0x0002:	return "successful-ok-conflicting-attributes";
	case 0x0400:	return "client-error-bad-request";
	case 0x0401:	return "client-error-forbidden";
	case 0x0402:	return "client-error-not-authenticated";
	case 0x0403:	return "client-error-not-authorized";
	case 0x0404:	return "client-error-not-possible";
	case 0x0405:	return "client-error-timeout";
	case 0x0406:	return "client-error-not-found";
	case 0x040a:	return "client-error-document-format-not-supported";
	case 0x0500:	return "server-error-internal-error";
	case 0x0501:	return "server-error-operation-not-supported";
	case 0x0503:	return "server-error-version-not-supported";
	case 0x0507:	return "server-error-busy";
	}
	return "(unknown)";
}
