/*
	File:		comms/host/HostDNSTool.cpp

	Contains:	THostDNSTool (HostDNSTool.h).

	Host code for the NIE's DNS tool (no ROM counterpart).
*/

#include "HostDNSTool.h"
#include "Options.h"
#include "CommErrors.h"
#include "NewtonMemory.h"
#include "HostSockets.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


// NEWTON_TRACE_DNS (host only): each name the host's resolver is asked
// for and what it answered, on stderr
static int
TracingDNS(void)
{
	static int tracing = -1;
	if (tracing < 0)
		tracing = getenv("NEWTON_TRACE_DNS") != nil;
	return tracing;
}


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
{
	fDefaultDomain[0] = 0;
}


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


// The link id and the servers are taken (the host's resolver has its
// own), the default domain kept (a C string; the domain manager sends "."
// for none); a query is answered at once.
ULong
THostDNSTool::ProcessOptionStart(TOption* theOption, ULong label, ULong opcode)
{
	switch (label)
	{
	case kDNSDefaultDomainOption:
		if (opcode == opSetRequired || opcode == opSetNegotiate)
		{
			const char* domain = (const char*) (theOption + 1);
			Size length = theOption->Length();
			Size n = 0;
			while (n < length && n < (Size) sizeof(fDefaultDomain) - 1 && domain[n] != 0)
				n++;
			memcpy(fDefaultDomain, domain, n);
			fDefaultDomain[n] = 0;
			// (the root, or a domain of dots, is none)
			const char* p = fDefaultDomain;
			while (*p == '.')
				p++;
			if (*p == 0)
				fDefaultDomain[0] = 0;
			if (TracingDNS())
				fprintf(stderr, "[dns] default domain \"%s\"\n", fDefaultDomain);
			return opSuccess;
		}
		if (opcode == opGetCurrent)
			return opSuccess;
		return opFailure;

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
		if (Resolve(name, addresses, &n) == kHostSocketOK)
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
	// the records that follow the query in the request, by index: an
	// answered one replaced by a record whose data is the whole answer (as
	// the NIE's tool does, part 10 +0x15c4: TOptionArray::RemoveOptionAt,
	// then InsertVarOptionAt with the record's header and the data - the
	// request's array is the client's own, so it grows), one there is no
	// answer for marked processed with -2 (+0x173c)
	TOptionArray* array = fOptionsInfo.fOptions;
	TOptionIterator* iter = fOptionsInfo.fOptionsIterator;
	int answer = 0;
	if (array != nil && iter != nil)
	{
		ArrayIndex queryIndex = iter->CurrentIndex() - 1;
		for (ArrayIndex index = queryIndex + 1; index < array->GetArrayCount(); index++)
		{
			TOption* record = array->OptionAt(index);
			if (record == nil || record->Label() != kDNSResourceRecordOption)
				break;
			if (answer < count)
			{
				const char* targetName = answers[answer].targetName;
				const char* resultName = answers[answer].resultName;
				Size n0 = strlen(targetName);
				Size n1 = strlen(resultName);
				Size at = (20 + n0 + 1 + 3) & ~3;
				Size size = (at + n1 + 1 + 3) & ~3;
				UByte* r = (UByte*) NewPtrClear(size);
				if (r == nil)
					break;
				r[0] = type >> 8;
				r[1] = type;
				PutLong(r + 4, answers[answer].target);
				PutLong(r + 8, answers[answer].result);
				memcpy(r + 20, targetName, n0);
				memcpy(r + at, resultName, n1);
				TOption header = *record;
				header.SetLength(size);
				header.SetOpCodeResult(opSuccess);
				header.SetProcessed();
				array->RemoveOptionAt(index);
				array->InsertVarOptionAt(index, &header, r, size);
				DisposPtr((Ptr) r);
				answer++;
			}
			else
			{
				record->SetOpCodeResult((ULong) -2);
				record->SetProcessed();
			}
		}
		// (the array's block may have moved: the query is found again)
		query = array->OptionAt(queryIndex);
		fOptionsInfo.fCurOptPtr = query;
		data = (UByte*) (query + 1);
	}
	PutLong(data + 4, (uint32_t) err);
	return opSuccess;
}


// A name's addresses from the host's resolver: as it is, and then - a name
// with no dot in it, when there is a default domain - with the domain
// after it.
int
THostDNSTool::Resolve(const char* name, uint32_t* addresses, int* count)
{
	*count = 0;
	int result = HostResolveName(name, addresses, 4, count);
	if (TracingDNS())
		fprintf(stderr, "[dns] %s: %d address(es)\n", name, result == kHostSocketOK ? *count : 0);
	if ((result != kHostSocketOK || *count == 0) && fDefaultDomain[0] != 0
	 && name[0] != 0 && strchr(name, '.') == nil)
	{
		char qualified[512];
		snprintf(qualified, sizeof(qualified), "%s.%s", name, fDefaultDomain);
		*count = 0;
		result = HostResolveName(qualified, addresses, 4, count);
		if (TracingDNS())
			fprintf(stderr, "[dns] %s: %d address(es)\n", qualified, result == kHostSocketOK ? *count : 0);
	}
	return result;
}


void THostDNSTool::PutBytes(CBufferList*)						{ PutComplete(kCommErrNotSupported, 0); }
void THostDNSTool::PutFramedBytes(CBufferList*, Boolean)		{ PutComplete(kCommErrNotSupported, 0); }
void THostDNSTool::KillPut()									{ KillPutComplete(noErr); }
void THostDNSTool::GetBytes(CBufferList*)						{ GetComplete(kCommErrNotSupported); }
void THostDNSTool::GetFramedBytes(CBufferList*)					{ GetComplete(kCommErrNotSupported); }
void THostDNSTool::KillGet()									{ KillGetComplete(noErr); }
