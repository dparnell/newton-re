/*
	File:		print/tests/test_HostDNSSD.cpp

	Contains:	Tests of the host's DNS-SD layer (print/host/dnssd/HostDNSSD.h):
				an injected list taken as found (names, URIs, formats, a
				lookup by name, a lookup started again), and a real browse
				of the local network, which must finish within its time
				whatever it finds (ctest print.HostDNSSD).
*/

#include "HostDNSSD.h"
#include <stdio.h>
#include <string.h>
#include <chrono>
#include <thread>

static int gFailures = 0;

static void
Check(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "ok" : "FAILED", what);
	if (!ok)
		gFailures++;
}


static void
TestInjected(void)
{
	HostDNSSDInject("Office Laser|ipp://10.0.0.5:631/ipp/print|ps,pcl;Hall Inkjet|ipp://10.0.0.6:8631/printers/hall|pcl;Plotter|ipp://10.0.0.7:631/ipp/print|");
	HostDNSSDStart();
	Check(!HostDNSSDBusy(), "an injected list is found at once");
	Check(HostDNSSDCount() == 3, "three printers found");
	HostDNSSDPrinter p;
	Check(HostDNSSDGet(0, &p) && strcmp(p.fName, "Office Laser") == 0
		&& strcmp(p.fURI, "ipp://10.0.0.5:631/ipp/print") == 0 && p.fPostScript && p.fPCL, "the first: PostScript and PCL");
	Check(HostDNSSDGet(1, &p) && strcmp(p.fName, "Hall Inkjet") == 0 && !p.fPostScript && p.fPCL, "the second: PCL only");
	Check(HostDNSSDGet(2, &p) && !p.fPostScript && !p.fPCL, "the third: neither");
	Check(!HostDNSSDGet(3, &p), "nothing beyond them");
	Check(HostDNSSDFind("Hall Inkjet", &p) && strcmp(p.fURI, "ipp://10.0.0.6:8631/printers/hall") == 0, "found by name");
	Check(!HostDNSSDFind("Office", &p), "a name must be whole");
	Check(HostDNSSDResolve("Office Laser", &p, 100), "resolved by name");
	Check(!HostDNSSDResolve("Nobody", &p, 100), "an unknown name is not resolved");
	HostDNSSDInject("Other|ipp://10.0.0.9:631/ipp/print|ps");
	HostDNSSDStart();
	Check(HostDNSSDCount() == 1 && HostDNSSDGet(0, &p) && strcmp(p.fName, "Other") == 0, "a lookup started again forgets the last");
}


// the host's own browse: whatever the network has, it finishes
static void
TestBrowse(void)
{
	HostDNSSDInject(NULL);
	HostDNSSDStart();
	int waited = 0;
	while (HostDNSSDBusy() && waited < 15000)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
		waited += 50;
	}
	Check(!HostDNSSDBusy(), "a real browse finishes");
	printf("the local network has %d IPP printer(s)\n", HostDNSSDCount());
	HostDNSSDPrinter p;
	for (int i = 0; HostDNSSDGet(i, &p); i++)
		printf("  %s at %s%s%s\n", p.fName, p.fURI, p.fPostScript ? " (PostScript)" : "", p.fPCL ? " (PCL)" : "");
}


int
main()
{
	TestInjected();
	TestBrowse();
	if (gFailures == 0)
		printf("test_HostDNSSD: all passed\n");
	else
		printf("test_HostDNSSD: %d FAILED\n", gFailures);
	return gFailures != 0;
}
