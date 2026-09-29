/*
	File:		comms/host/HostDNSTool.cpp

	Contains:	THostDNSTool (HostDNSTool.h).

	Host code for the NIE's DNS tool (no ROM counterpart).
*/

#include "HostDNSTool.h"
#include "Options.h"
#include "CommErrors.h"
#include "HostSockets.h"

#include <string.h>


static uint32_t
DataLong(const UByte* p)
{
	return ((uint32_t) p[0] << 24) | ((uint32_t) p[1] << 16) | ((uint32_t) p[2] << 8) | p[3];
}

static void
PutLong(UByte* p, uint32_t value)
{
	p[0] = value >> 24;
	p[1] = value >> 16;
	p[2] = value >> 8;
	p[3] = value;
}


THostDNSTool::THostDNSTool(ULong serviceId)
	: TCommTool(serviceId)
{ }


THostDNSTool::~THostDNSTool()
{ }


ULong
THostDNSTool::GetSizeOf()
{
	return sizeof(THostDNSTool);
}


UChar*
THostDNSTool::GetToolName()
{
	return (UChar*) "Host DNS";
}


NewtonErr
THostDNSTool::OpenStart(TOptionArray* options)
{
	if (HostSocketsInit() != kHostSocketOK)
		return kCommErrResourceNotAvailable;
	return noErr;
}


// The link id, the default domain and the servers are taken (the host's
// resolver has its own); a query is answered at once.
ULong
THostDNSTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	switch (label)
	{
	case kDNSDefaultDomainOption:
	case kDNSServerOption:
	case 'ilid':
		if (opcode == opSetRequired || opcode == opSetNegotiate || opcode == opGetCurrent)
			return opSuccess;
		return opFailure;

	case kDNSQueryOption:
		return Query(theOption);

	case kDNSResourceRecordOption:
		// (a record no query came before: none to fill)
		return (ULong) -2;
	}
	return TCommTool::ProcessOptionStart(theOption, label, opcode);
}


// The query's name (or address) looked up, the result written into the
// query and the answers into the 'rrcd options that follow it in the
// request, each marked processed; those left over processed with -2.
ULong
THostDNSTool::Query(TOption* query)
{
	UByte* data = (UByte*) (query + 1);
	Size length = query->Length();
	if (length < 24)
		return opFailure;
	uint32_t address = DataLong(data + 12);
	uint32_t type = DataLong(data + 16);
	uint32_t nameLength = DataLong(data + 20);
	char name[256];
	Size room = length - 24;
	if (nameLength > room)
		nameLength = room;
	if (nameLength > sizeof(name) - 1)
		nameLength = sizeof(name) - 1;
	memcpy(name, data + 24, nameLength);
	name[nameLength] = 0;

	// the answers: (target address, result address, target name, result name)
	struct Answer { uint32_t target; uint32_t result; const char* targetName; const char* resultName; };
	Answer answers[4];
	int count = 0;
	uint32_t addresses[4];
	char found[256];
	NewtonErr err = noErr;
	if (type == kDNSQueryName)
	{
		if (HostResolveAddress(address, found, sizeof(found)) == kHostSocketOK)
		{
			answers[0].target = address;
			answers[0].result = 0;
			answers[0].targetName = "";
			answers[0].resultName = found;
			count = 1;
		}
	}
	else
	{
		// (a mail exchanger is answered as the name's address: the host's
		// resolver answers addresses only)
		int n = 0;
		if (HostResolveName(name, addresses, 4, &n) == kHostSocketOK)
		{
			count = (n < 4) ? n : 4;
			for (int i = 0; i < count; i++)
			{
				answers[i].target = 0;
				answers[i].result = addresses[i];
				answers[i].targetName = name;
				answers[i].resultName = "";
			}
		}
	}
	if (count == 0)
		err = kDNSErrNameNotFound;
	PutLong(data + 4, (uint32_t) err);

	// the records that follow the query in the request
	TOptionIterator* iter = fOptionsInfo.fOptionsIterator;
	int answer = 0;
	if (iter != nil)
	{
		for (TOption* record = iter->CurrentOption(); record != nil && iter->More(); record = iter->NextOption())
		{
			if (record->Label() != kDNSResourceRecordOption)
				break;
			if (answer < count && record->Length() >= 20)
			{
				UByte* r = (UByte*) (record + 1);
				Size size = record->Length();
				memset(r, 0, size);
				r[0] = type >> 8;
				r[1] = type;
				PutLong(r + 4, answers[answer].target);
				PutLong(r + 8, answers[answer].result);
				// the two names, as far as the record has room: each a C
				// string, the second on the next word boundary after the
				// first, and room always left for the second's terminator
				const char* targetName = answers[answer].targetName;
				const char* resultName = answers[answer].resultName;
				Size n0 = strlen(targetName);
				while (n0 > 0 && ((20 + n0 + 1 + 3) & ~3) + 1 > size)
					n0--;
				if (20 + n0 + 1 <= size)
				{
					memcpy(r + 20, targetName, n0);
					r[20 + n0] = 0;
				}
				Size at = (20 + n0 + 1 + 3) & ~3;
				if (at < size)
				{
					Size n1 = strlen(resultName);
					if (at + n1 + 1 > size)
						n1 = size - at - 1;
					memcpy(r + at, resultName, n1);
					r[at + n1] = 0;
				}
				record->SetOpCodeResult(opSuccess);
				answer++;
			}
			else
				record->SetOpCodeResult((ULong) -2);
			record->SetProcessed();
		}
	}
	return opSuccess;
}


void THostDNSTool::PutBytes(CBufferList*)						{ PutComplete(kCommErrNotSupported, 0); }
void THostDNSTool::PutFramedBytes(CBufferList*, Boolean)		{ PutComplete(kCommErrNotSupported, 0); }
void THostDNSTool::KillPut()									{ KillPutComplete(noErr); }
void THostDNSTool::GetBytes(CBufferList*)						{ GetComplete(kCommErrNotSupported); }
void THostDNSTool::GetFramedBytes(CBufferList*)					{ GetComplete(kCommErrNotSupported); }
void THostDNSTool::KillGet()									{ KillGetComplete(noErr); }
