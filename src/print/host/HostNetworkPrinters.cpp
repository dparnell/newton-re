/*
	File:		print/host/HostNetworkPrinters.cpp

	Contains:	Finding the printers on the network, two ways (HostIPP.h).

				The ROM's own: the Print slip's "Choose Network LaserWriter"
				opens the ROM's network chooser, which asks AppleTalk for its
				zones (HaveZones, GetMyZone, GetZoneList) and looks the
				printers up by NBP (NBPStart "=:LaserWriter@zone", then
				NBPGetCount and NBPGetNames while it idles, NBPStop), and the
				printer picked becomes a printer frame with TPSPAPDriver as
				its driver.  DEVIATION (the owner's decision): AppleTalk is
				not reconstructed; these natives - the ROM's are AppleTalk's
				own, FHaveZones 0x00066c14, FNBPStart 0x0006664c and their
				kin - are answered from the host's DNS-SD browse
				(print/host/dnssd/HostDNSSD.h): one zone, "Local network", in
				which the LaserWriters are the IPP printers that take
				PostScript; TPSPAPDriver's PAP is IPP in its turn
				(HostPAPDriver.cpp).

				The host's own: the Network Printers preferences panel
				(HostPrinters.ns), over HostLookForPrinters(),
				HostLookingForPrinters() and HostFoundPrinters().

	Host only.
*/

#include "print/host/HostIPP.h"
#include "print/host/dnssd/HostDNSSD.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Compiler.h"
#include "RSSymbols.h"
#include "NewtonExceptions.h"
#include "utility/Unicode.h"
#include <stdio.h>
#include <string.h>

extern const char gHostPrintersSource[];		// HostPrinters.ns (HostPrintersSource.cpp, generated)

static const char kZone[] = "Local network";


static Ref
MakeHostString(const char* text)
{
	return MakeString(text);
}


/*------------------------------------------------------------------------------
	A p p l e T a l k ' s   z o n e s   a n d   N B P ,   f r o m   D N S - S D
------------------------------------------------------------------------------*/

// OpenAppleTalk(), CloseAppleTalk(): nothing to open (0: done)
static Ref
FOpenAppleTalk(RefArg /*rcvr*/)
{
	return MAKEINT(0);
}

static Ref
FCloseAppleTalk(RefArg /*rcvr*/)
{
	return MAKEINT(0);
}

static Ref
FAppleTalkOpenCount(RefArg /*rcvr*/)
{
	return MAKEINT(0);
}

// HaveZones(): the network has one zone
static Ref
FHaveZones(RefArg /*rcvr*/)
{
	return TRUEREF;
}

static Ref
FGetMyZone(RefArg /*rcvr*/)
{
	return MakeHostString(kZone);
}

static Ref
FGetZoneList(RefArg /*rcvr*/)
{
	RefVar zones(MakeArray(1));
	SetArraySlot(zones, 0, RefVar(MakeHostString(kZone)));
	return zones;
}

// the lookup a cookie stands for: 1 the LaserWriters (PostScript), 2 a type
// no IPP printer is
static long
LookupKind(RefArg cookie)
{
	return ISINT(cookie) ? RINT(cookie) : 0;
}

// NBPStart("=:TYPE@ZONE"): a lookup begun
static Ref
FNBPStart(RefArg /*rcvr*/, RefArg nbpName)
{
	char text[256] = "";
	if (IsString(nbpName))
		ConvertFromUnicode(GetCString(nbpName), text, kMacRomanEncoding, sizeof(text) - 1);
	const char* type = strchr(text, ':');
	char kind[64] = "";
	if (type != nil)
	{
		size_t n = 0;
		type++;
		while (type[n] != 0 && type[n] != '@' && n + 1 < sizeof(kind))
		{
			kind[n] = type[n];
			n++;
		}
		kind[n] = 0;
	}
	HostDNSSDStart();
	return MAKEINT(strcmp(kind, "LaserWriter") == 0 ? 1 : 2);
}

// the printers a lookup answers, by number
static bool
LookupMatch(long kind, int index, HostDNSSDPrinter* printer)
{
	if (!HostDNSSDGet(index, printer))
		return false;
	return kind == 1 && printer->fPostScript;
}

static Ref
FNBPGetCount(RefArg /*rcvr*/, RefArg cookie)
{
	long kind = LookupKind(cookie);
	long count = 0;
	HostDNSSDPrinter printer;
	for (int i = 0; i < HostDNSSDCount(); i++)
		if (LookupMatch(kind, i, &printer))
			count++;
	return MAKEINT(count);
}

// NBPGetNames(cookie): the names found, as the ROM's answers them (the
// object names alone)
static Ref
FNBPGetNames(RefArg /*rcvr*/, RefArg cookie)
{
	long kind = LookupKind(cookie);
	RefVar names(MakeArray(0));
	HostDNSSDPrinter printer;
	for (int i = 0; i < HostDNSSDCount(); i++)
		if (LookupMatch(kind, i, &printer))
			AddArraySlot(names, RefVar(MakeHostString(printer.fName)));
	return names;
}

// NBPStop(cookie): the lookup over.  The chooser stops its lookup when it
// closes, after the printer picked has been made userConfiguration's
// currentPrinter (the Print slip's networkChooserDone and saveNew, which
// assign it without flushing the user configuration).  HOST ADDITION: the
// user configuration flushed here (FlushUserConfig, the ROM's own: its
// entry in the System soup written), so a printer picked on the network is
// still the current printer after a restart.  The ROM keeps the choice
// only when something later flushes the configuration.
static Ref
FNBPStop(RefArg /*rcvr*/, RefArg /*cookie*/)
{
	newton_try
	{
		NSCallGlobalFn(RefVar(Intern((char*) "FlushUserConfig")));
	}
	newton_catch_all
	{
	}
	end_try;
	return MAKEINT(0);
}


/*------------------------------------------------------------------------------
	T h e   N e t w o r k   P r i n t e r s   p a n e l ' s
------------------------------------------------------------------------------*/

// HostLookForPrinters(): a browse begun (again)
static Ref
FHostLookForPrinters(RefArg /*rcvr*/)
{
	HostDNSSDStart();
	return TRUEREF;
}

static Ref
FHostLookingForPrinters(RefArg /*rcvr*/)
{
	return HostDNSSDBusy() ? TRUEREF : NILREF;
}

// HostFoundPrinters(): [{name, uri, postScript, pcl}, ...]
static Ref
FHostFoundPrinters(RefArg /*rcvr*/)
{
	RefVar list(MakeArray(0));
	HostDNSSDPrinter printer;
	for (int i = 0; HostDNSSDGet(i, &printer); i++)
	{
		RefVar found(AllocateFrame());
		SetFrameSlot(found, RSSYMname, RefVar(MakeHostString(printer.fName)));
		SetFrameSlot(found, RefVar(Intern((char*) "uri")), RefVar(MakeHostString(printer.fURI)));
		SetFrameSlot(found, RefVar(Intern((char*) "postScript")), printer.fPostScript ? TRUEREF : NILREF);
		SetFrameSlot(found, RefVar(Intern((char*) "pcl")), printer.fPCL ? TRUEREF : NILREF);
		AddArraySlot(list, found);
	}
	return list;
}


static void
DefineGlobalFunction(const char* name, void* fn, long numArgs)
{
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) name)), RefVar(MakeCFunction(fn, numArgs, nil)));
}


void
HostInstallNetworkPrinters(void)
{
	DefineGlobalFunction("OpenAppleTalk", (void*) FOpenAppleTalk, 0);
	DefineGlobalFunction("CloseAppleTalk", (void*) FCloseAppleTalk, 0);
	DefineGlobalFunction("AppleTalkOpenCount", (void*) FAppleTalkOpenCount, 0);
	DefineGlobalFunction("HaveZones", (void*) FHaveZones, 0);
	DefineGlobalFunction("GetMyZone", (void*) FGetMyZone, 0);
	DefineGlobalFunction("GetZoneList", (void*) FGetZoneList, 0);
	DefineGlobalFunction("NBPStart", (void*) FNBPStart, 1);
	DefineGlobalFunction("NBPGetCount", (void*) FNBPGetCount, 1);
	DefineGlobalFunction("NBPGetNames", (void*) FNBPGetNames, 1);
	DefineGlobalFunction("NBPStop", (void*) FNBPStop, 1);
	DefineGlobalFunction("HostLookForPrinters", (void*) FHostLookForPrinters, 0);
	DefineGlobalFunction("HostLookingForPrinters", (void*) FHostLookingForPrinters, 0);
	DefineGlobalFunction("HostFoundPrinters", (void*) FHostFoundPrinters, 0);
	// the panel, and the printers added before
	newton_try
	{
		RefVar fn(ParseString(RefVar(MakeString(gHostPrintersSource))));
		RefVar printers(InterpretBlock(fn, RefVar()));
		SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "NetworkPrinters:host")), printers);
		NSSend(printers, RefVar(Intern((char*) "Start")));
	}
	newton_catch_all
	{
		fprintf(stderr, "[host] the Network Printers panel did not start: %s\n", CurrentException()->name);
	}
	end_try;
}
