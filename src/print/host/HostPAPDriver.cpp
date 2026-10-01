/*
	File:		print/host/HostPAPDriver.cpp

	Contains:	The host's stand-ins for TPSPAPDriver's PAP calls
				(print/PSPrinter.h): the network PostScript printer the
				Print slip's "Choose Network LaserWriter" picked, reached by
				IPP.

				DEVIATION (the owner's decision): PAP over AppleTalk replaced
				by IPP over the host's network.  The printer frame's
				printerName ("Office Laser:LaserWriter@Local network", as the
				ROM's networkChooserDone makes it) names one of the printers
				the host's DNS-SD browse finds (print/host/dnssd/HostDNSSD.h);
				the job goes to its URI through THostIPPConnection.  Not
				found yet, a browse is started and waited for (up to four
				seconds).

	Host only.
*/

#include "print/PSPrinter.h"
#include "print/host/HostIPPTool.h"
#include "print/host/dnssd/HostDNSSD.h"
#include "Frames.h"
#include "utility/Unicode.h"
#include <string.h>
#include <stdio.h>

// the object name of an NBP address, "name:type@zone"
static void
NBPObjectName(const char* address, char* name, size_t size)
{
	size_t n = 0;
	while (address[n] != 0 && address[n] != ':' && address[n] != '@' && n + 1 < size)
	{
		name[n] = address[n];
		n++;
	}
	name[n] = 0;
}


TPSPAPDriver*
TPSPAPDriver::New()
{
	fPAP = nil;
	fError = noErr;
	fSent = 0;
	fClosedAppleTalk = false;
	fCancelled = false;
	fHostConnection = nil;
	return this;
}


void
TPSPAPDriver::Delete()
{
	delete (THostIPPConnection*) fHostConnection;
	fHostConnection = nil;
}


// The printer the chooser picked found and connected to.  A printer frame
// with no printerName is not found (kPR_ERR_NotFound), as the ROM's Open
// has it.
NewtonErr
TPSPAPDriver::Open()
{
	fError = noErr;
	fSent = 0;
	fClosedAppleTalk = false;
	fCancelled = false;
	RefVar connectInfo(fConnect->fConnectInfo);
	RefVar printer(IsFrame(connectInfo) ? GetFrameSlot(connectInfo, RefVar(Intern((char*) "printer"))) : NILREF);
	RefVar address(IsFrame(printer) ? GetFrameSlot(printer, RefVar(Intern((char*) "printerName"))) : NILREF);
	if (!IsString(address))
		return fError = kPR_ERR_NotFound;
	char text[256], name[64];
	ConvertFromUnicode(GetCString(address), text, kMacRomanEncoding, sizeof(text) - 1);
	NBPObjectName(text, name, sizeof(name));
	HostDNSSDPrinter found;
	if (!HostDNSSDResolve(name, &found, kHostDNSSDWait))
	{
		printf("[host] IPP: no printer called %s on the network\n", name);
		fflush(stdout);
		return fError = kPR_ERR_NotFound;
	}
	THostIPPConnection* connection = (THostIPPConnection*) fHostConnection;
	if (connection == nil)
		fHostConnection = connection = new THostIPPConnection;
	fError = connection->Open(found.fURI);
	return fError;
}


NewtonErr
TPSPAPDriver::Close(Boolean /*abort*/)
{
	THostIPPConnection* connection = (THostIPPConnection*) fHostConnection;
	fClosedAppleTalk = true;
	return (connection != nil) ? connection->Close() : noErr;
}


// The printer's state: PAP's status messages (GetData's, and GetPAPStatus's
// when the driver has an error already) are IPP's Get-Printer-Attributes
// here (HostIPPPrinterStatus), a problem the printer reports the problem
// the ROM's InterpretPAPString makes of its PostScript status message.
NewtonErr
TPSPAPDriver::GetStatus()
{
	THostIPPConnection* connection = (THostIPPConnection*) fHostConnection;
	return (connection != nil) ? connection->Status(fPrinter) : noErr;
}


NewtonErr
TPSPAPDriver::SendPSText(char* text, ULong& sent, Boolean eoj)
{
	return SendPSBinary(text, strlen(text), sent);
}


NewtonErr
TPSPAPDriver::SendPSBinary(char* data, ULong size, ULong& sent)
{
	sent = 0;
	if (fCancelled)
		return kPR_ERR_UserCancel;
	THostIPPConnection* connection = (THostIPPConnection*) fHostConnection;
	if (connection == nil)
		return kPR_ERR_NewtonError;
	// as the ROM's: PAP's PutData refused while the printer has a problem
	// (-12716) is the printer asked and its problem the error; every eighth
	// write the status is looked at
	if (fError >= kPR_ERR_MINPROBLEM && fError <= kPR_ERR_MAXPROBLEM)
	{
		fError = GetStatus();
		if (fError != noErr)
			return fError;
	}
	NewtonErr err = connection->Send(data, size, sent);
	if (err == noErr && (++fSent & 7) == 7)
		fError = GetStatus();
	return err;
}


// (nothing comes back from an IPP printer while the job is sent)
NewtonErr
TPSPAPDriver::RecvPSText(char* /*text*/, ULong& size)
{
	size = 0;
	return kPR_ERR_NewtonError;
}
