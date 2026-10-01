/*
	File:		print/host/dnssd/HostDNSSD.h

	Contains:	The printers on the local network, as the host's own DNS-SD
				(Bonjour, mDNS) service browsing finds them: IPP printers
				(_ipp._tcp) - their names, the ipp:// URI each answers at
				and whether it takes PostScript and PCL (the TXT record's
				pdl).  The host's IPP printers offer what this finds: the
				ROM's network printer chooser (its NBP lookups answered from
				it, HostNetworkPrinters.cpp) and the Network Printers
				preferences panel.

				The browse is the host's: DnsServiceBrowse and
				DnsServiceResolve on Windows (dnsapi, Windows 10 and later);
				avahi-browse (the Avahi daemon's own client) on Linux and the
				BSDs; nothing elsewhere.  Never a resolver of our own.  It
				runs on a host thread of its own, so a lookup is started and
				then asked how many it has found, as the ROM's NBP lookups
				are.

				Pluggable: HostDNSSDInject (or NEWTON_FOUND_PRINTERS in the
				environment) puts a list in its place - how the ctests find
				tools/print/ippprinter.py's printer.  A list is
				"NAME|URI|FORMATS;NAME|URI|FORMATS...", FORMATS being any of
				ps and pcl separated by commas.

				Built without the Newton include paths (the platforms'
				headers and the DDK's names do not mix): a plain C interface.

	Host only.
*/

#ifndef __HOSTDNSSD_H
#define __HOSTDNSSD_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct HostDNSSDPrinter
{
	char	fName[64];			// the service instance's name ("Office Laser")
	char	fURI[320];			// ipp://address:port/path
	int		fPostScript;		// it takes application/postscript
	int		fPCL;				// ... application/vnd.hp-pcl
} HostDNSSDPrinter;

// A list in place of the browse (nil or "": the browse again).
void	HostDNSSDInject(const char* list);

// A lookup started (again): the printers found so far forgotten and a
// browse begun on a thread of its own, or the injected list taken.
void	HostDNSSDStart(void);

// Whether a browse is still going.
int		HostDNSSDBusy(void);

// The printers found so far, and one of them by number (==> 0 beyond them)
// or by name.
int		HostDNSSDCount(void);
int		HostDNSSDGet(int index, HostDNSSDPrinter* printer);
int		HostDNSSDFind(const char* name, HostDNSSDPrinter* printer);

// A printer found by name, looked for afresh when it has not been found
// yet: a browse started and waited on for up to `milliseconds` (the
// caller's thread blocked meanwhile).  ==> 0: not on the network.
int		HostDNSSDResolve(const char* name, HostDNSSDPrinter* printer, int milliseconds);

#ifdef __cplusplus
}
#endif

#endif	/* __HOSTDNSSD_H */
