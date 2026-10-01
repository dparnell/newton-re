/*
	File:		print/host/HostIPP.h

	Contains:	Printing to a printer on the network by IPP, the Internet
				Printing Protocol (RFC 8011, encoded as RFC 8010 has it):
				the host's own way out for the ROM's PostScript and HP PCL
				printers.

				Not in the ROM.  The ROM's printers reach the printer
				through a comm endpoint - the HP driver ThpPCL over the
				serial port or IrDA (print/HPPCL.h), the PostScript one over
				AppleTalk's PAP.  The host gives them one more service to
				make an endpoint of: 'ippc, THostIPPTool, a comm tool whose
				connection is an IPP Print-Job request to the configured
				printer over a TCP connection of the host's
				(hal/host/HostSockets.h - the host's network stack, never one
				of our own).  The bytes the driver sends are the document:
				the tool sends the HTTP POST and the job's attributes when
				the first bytes come (the document-format named from them -
				"%!" PostScript, ESC PCL), each write as an HTTP chunk, and
				on the disconnect the last chunk, reading the printer's
				answer before it closes.

				The PostScript printer (TPSPrinter) is given a driver of the
				host's, THostIPPPSDriver (HostPSDriver.h), which sends its
				text through such an endpoint; the HP driver takes the
				service through gPrinterServiceHook (print/HPPCL.h) for the
				printer model kHostIPPPrinterModel.

				How a user reaches it: newton --ipp-printer
				ipp://host:631/ipp/print (or NEWTON_IPP_PRINTER in the
				environment, or HostSetIPPPrinter(uri) from a script) adds
				two printers to AvailablePrinters - "IPP printer
				(PostScript)" and "IPP printer (HP PCL)" - which the Print
				slip's Choose Other Printer lists, as it lists the host's PNG
				printer.

	Host only.
*/

#ifndef __HOSTIPP_H
#define __HOSTIPP_H

#include <stddef.h>
#include <stdint.h>

// the comm service the IPP tool answers, and the HP printer model that is
// reached through it
#define kHostIPPService			'ippc'
enum { kHostIPPPrinterModel = 100 };

// The printers' names in the Print slip
#define kHostIPPPSPrinterName	"IPP printer (PostScript)"
#define kHostIPPPCLPrinterName	"IPP printer (HP PCL)"
#define kHostIPPPSDriverName	"THostIPPPSDriver"

/*------------------------------------------------------------------------------
	The protocol (plain C++, no Newton types: tests/test_HostIPP.cpp)
------------------------------------------------------------------------------*/

// An ipp://, ipps://, http:// or https:// printer URI taken apart; the
// port is 631 (ipp, ipps), 80 (http) or 443 (https) when it names none,
// the path "/" when it has none; ipps and https are over TLS
// (HostIPPSocket.h).  ==> false when it is not one.
struct HostIPPURI
{
	char		fHost[256];
	uint16_t	fPort;
	char		fPath[512];
	bool		fTLS;
};
bool		HostIPPParseURI(const char* uri, HostIPPURI* parts);

// The document format a job's first bytes say it is: application/postscript
// ("%!" or a PJL universal exit followed by PostScript), application/
// vnd.hp-pcl (ESC), else application/octet-stream.
const char*	HostIPPDocumentFormat(const unsigned char* data, size_t size);

// A Print-Job request's IPP message (version 1.1): the operation
// attributes - charset utf-8, natural language en, printer-uri,
// requesting-user-name, job-name, document-format - and the end tag, the
// document to follow.  ==> its length (nought if it does not fit size).
size_t		HostIPPPrintJob(const char* printerURI, const char* user, const char* jobName, const char* format, uint32_t requestId, unsigned char* buffer, size_t size);

// The HTTP request line and headers of a POST of application/ipp in
// chunks.  ==> its length (nought if it does not fit).
size_t		HostIPPHTTPHeader(const HostIPPURI* uri, char* buffer, size_t size);

// A Get-Printer-Attributes request's IPP message asking for printer-state
// and printer-state-reasons, and the HTTP head of a POST of it (a
// Content-Length, not chunks).  ==> their lengths (nought if they do not
// fit).
size_t		HostIPPGetPrinterState(const char* printerURI, uint32_t requestId, unsigned char* buffer, size_t size);
size_t		HostIPPHTTPRequestHeader(const HostIPPURI* uri, size_t contentLength, char* buffer, size_t size);

// An HTTP response read so far: complete (the head and as much of the body
// as its Content-Length or chunks say), the HTTP status, and from the IPP
// message in the body its status-code and (when it has one) the job-id.
// ==> 1 complete, 0 not yet, -1 not HTTP.  At the end of the connection
// what has come is taken as complete.
struct HostIPPResponse
{
	int			fHTTPStatus;
	int			fIPPStatus;		// -1: none
	int32_t		fJobId;			// -1: none
	int			fPrinterState;	// -1: none; 3 idle, 4 processing, 5 stopped
	char		fStateReasons[256];	// printer-state-reasons, joined by commas
};
int			HostIPPParseResponse(const unsigned char* data, size_t size, bool connectionClosed, HostIPPResponse* response);

// a status-code's name (RFC 8011 13.1), for the log
const char*	HostIPPStatusName(int status);

// What a printer's state says of it, as the Newton's printer problems put
// it (PrintErrors.h's kPR_PROB_...): a printer-state-reason that stops it
// printing (RFC 8011 5.4.12; bare or with -error - a -warning or -report
// does not stop it), or a printer stopped for another reason.
enum HostIPPCondition
{
	kHostIPPReady,
	kHostIPPNoPaper,		// media-empty, media-needed
	kHostIPPNoInk,			// marker-supply-empty, toner-empty
	kHostIPPJammed,			// media-jam
	kHostIPPDoorOpen,		// door-open, cover-open
	kHostIPPOffLine			// paused, shutdown, offline, or stopped (printer-state 5)
};
HostIPPCondition	HostIPPPrinterCondition(const HostIPPResponse* response);

// What became of a job, by the printer's answer to its Print-Job
// (`complete` as HostIPPParseResponse answered it: 1 an answer).
enum HostIPPJobOutcome
{
	kHostIPPJobPrinted,		// HTTP 200 and successful-*
	kHostIPPJobBusy,		// server-error-busy or HTTP 503: try again later
	kHostIPPJobRefused,		// any other answer
	kHostIPPJobNoAnswer		// the connection ended with no answer
};
HostIPPJobOutcome	HostIPPJobResult(int complete, const HostIPPResponse* response);


/*------------------------------------------------------------------------------
	The host's IPP printer
------------------------------------------------------------------------------*/

// The printer jobs go to (nil or "": none).
void		HostSetIPPPrinter(const char* uri);
const char*	HostIPPPrinter(void);

// What the jobs came to, for a script waiting on one: how many the printer
// has answered, and the last HTTP and IPP status and job id.
long		HostIPPJobsDone(void);
void		HostIPPLastJob(HostIPPResponse* response);

// The tool's service and the PostScript driver registered, the HP driver's
// hook set, the script functions defined (HostSetIPPPrinter(uri),
// HostIPPJobs() - the jobs answered - and HostIPPLastStatus() - the last
// job's IPP status, -1 for none), and - when a printer is configured, here
// or by NEWTON_IPP_PRINTER - the two printers added to AvailablePrinters.
// Run with the NewtonScript globals there (the newt world's PreMain, from
// HostInstallPrinter).
void		HostInstallIPPPrinters(void);

// The printers on the network: the AppleTalk natives the ROM's network
// chooser asks (HaveZones, GetMyZone, GetZoneList, NBPStart, NBPGetCount,
// NBPGetNames, NBPStop, OpenAppleTalk, CloseAppleTalk) answered from the
// host's DNS-SD browse (dnssd/HostDNSSD.h) - DEVIATION, the owner's
// decision -, the Network Printers panel's functions (HostLookForPrinters,
// HostLookingForPrinters, HostFoundPrinters) defined, and HostPrinters.ns
// started: the panel registered in the Prefs and the printers added
// before offered again.  (HostNetworkPrinters.cpp; HostInstallIPPPrinters
// calls it.)
void		HostInstallNetworkPrinters(void);

#endif	/* __HOSTIPP_H */
