/*
	File:		comms/host/HostDNSTool.h

	Contains:	THostDNSTool, the comm tool that stands in for the Newton
				Internet Enabler's TDNSTool (inetenbl.pkg part 10): its
				lookups answered by the host's resolver
				(hal/host/HostSockets.h's HostResolveName and
				HostResolveAddress).

				DEVIATION (owner's decision - docs/comms/README.md): the NIE's
				tool sends DNS queries over UDP through its own TCP/IP stack
				to the servers the link names; the host asks its own
				resolver, which knows its own servers.

				The protocol is the NIE's own, as its NewtonScript domain
				manager speaks it (read with tools/newton-rom/analysis/
				pkgns.py; docs/comms/README.md, "The DNS service"):

				the endpoint is instantiated with a 'sid ' service option
				naming 'dnst and the options
				'ilid  the link id, a long
				'ddom  the default domain, a C string
				'dnic  a DNS server's address, a long (one per server)
				which are taken: the link id and the servers are left alone
				(the host's resolver has its own servers), and the default
				domain - the setup's defaultDomain, "." when it has none -
				is kept for a name with no dot in it that the host's resolver
				does not find as it is: that name is asked for again with
				the domain after it, as a resolver's search list does (the
				NIE's own tool keeps the domain too, part 10 +0x2b88 ->
				+0x1798; the host asks for the name as it is first, where
				its own resolver applies the host's own search domains).
				NEWTON_TRACE_DNS prints each name asked for.  A query is an option
				request (opGetCurrent) of one 'dnsq followed by four 'rrcd:

				'dnsq  'dnst (4 characters), the result (a long: 0 or an
				       error, which the tool writes), the query id, an IP
				       address (4 bytes: a reverse lookup's), the query type
				       (1 an address, 12 a name, 15 a mail exchanger - the
				       tool's 0x1390 switch), the name's length, the name
				'rrcd  a resource record the tool fills in: two shorts, a
				       target and a result address (4 bytes each), two longs,
				       the target and the result name (C strings)

				The domain manager reads a record as (MAddCacheEntry,
				0x38cdd) the target address, the result address, the target
				name and the result name, and the second short as how long
				to cache it (nought: fifteen minutes); a record whose result
				is not success is skipped, and the tool marks each record
				it has no answer for processed with a result of -2 (part 10
				+0x173c).  The host fills the first short with the query
				type and leaves the second nought and the longs nought
				(the host's resolver says nothing of a record's life).  An answered
				record is replaced in the request by one whose data holds the
				whole answer, as the NIE's own tool replaces it (part 10
				+0x15c4: TOptionArray::RemoveOptionAt, then InsertVarOptionAt -
				the calls at +0x463c and +0x4604, read from their arguments;
				the request's option array is the client's own, which grows),
				so the names come back whole though the domain manager asks
				with empty strings.

				A name that is not found answers -60791 in the 'dnsq's
				result (the NIE's "The host name you requested wasn't
				located" - its error strings' table, 0x43e3d).

	Host code for the NIE's DNS tool (no ROM counterpart).
*/

#ifndef __COMMS_HOSTDNSTOOL_H
#define __COMMS_HOSTDNSTOOL_H

#ifndef __COMMS_COMMTOOLS_H
#include "CommTools.h"
#endif

// the NIE's DNS option labels
#define kDNSQueryOption				'dnsq'
#define kDNSResourceRecordOption	'rrcd'
#define kDNSDefaultDomainOption		'ddom'
#define kDNSServerOption			'dnic'
#define kDNSServiceId				'dnst'

// the query types (the DNS's own)
#define kDNSQueryAddress			1		// A: a name's addresses
#define kDNSQueryName				12		// PTR: an address's name
#define kDNSQueryMailExchanger		15		// MX

// the NIE's DNS errors (its error strings' table)
#define kDNSErrNameNotFound			(-60791)

class THostDNSTool : public TCommTool
{
public:
						THostDNSTool(ULong serviceId);
	virtual				~THostDNSTool();

	virtual ULong		GetSizeOf();
	virtual UChar*		GetToolName();
	virtual NewtonErr	OpenStart(TOptionArray* options);
	virtual ULong		ProcessOptionStart(TOption* theOption, ULong label, ULong opcode);

	// (no data flows: a query is an option request)
	virtual void		PutBytes(CBufferList* clientBuffer);
	virtual void		PutFramedBytes(CBufferList* clientBuffer, Boolean endOfFrame);
	virtual void		KillPut();
	virtual void		GetBytes(CBufferList* clientBuffer);
	virtual void		GetFramedBytes(CBufferList* clientBuffer);
	virtual void		KillGet();

protected:
	ULong				Query(TOption* query);
	int					Resolve(const char* name, uint32_t* addresses, int* count);

	char				fDefaultDomain[256];	// 'ddom: "" when there is none
};

#endif
