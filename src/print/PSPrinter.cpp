/*
	File:		print/PSPrinter.cpp

	Contains:	TPSPrinter, the ROM's TPrinter for PostScript printers: the
				job, the document's structure and the errors
				(print/PSPrinter.h).  The drawing is PSBottlenecks.cpp.

				A job: Open makes the driver connect (waiting while the
				printer says it is busy), opens the printer port at 72 dots
				an inch the size of the paper and installs the bottlenecks.
				The first OpenPage sends the header - the DSC comments
				(title, creator, date, the user), the prolog and each
				PostScript font's Mac-encoded version - and every OpenPage
				then sends %%Page, the landscape turn when there is one and
				the page setup.  The view draws the page once; ClosePage
				sends showpage.  Close sends the trailer with the page count
				and closes the driver.

				An error from the driver is sorted (HandleError): -44000 ..
				-44099 are fatal (the job stops), -44100 .. -44199 problems
				put to the user (CallHandleProblem: the printer is out of
				paper, ...), and anything else is passed over.  A problem the
				user cancels stops the job too (fProblemFatal).

	Reconstructed from the MP2x00 US ROM (0x0021ac14-0x0021bc00); each
	function cites its origin.
*/

#include "print/PSPrinter.h"
#include "Ports.h"
#include "Rects.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NewtonMemory.h"
#include "utility/Unicode.h"
#include "host/RomBugs.h"
#include <string.h>
#include <stdio.h>
#include <new>

extern Boolean	gSCPDevicePackageBusy;		// comms/CommManager.cpp: the serial port has a package on it
Ref				GetPreference(RefArg slot);
Ref				FTime(RefArg rcvr);							// intl/Dates.cpp
Ref				FDateNTime(RefArg rcvr, RefArg minutes);	// intl/Dates.cpp

// how long a busy printer is waited for before it is asked again, and how
// long a problem slip waits before it asks the printer again (the ROM's
// 0x2328000 and 0x1194000)
static const TTimeout	kPSBusyRetry = (TTimeout) 0x2328000;
static const TTimeout	kPSProblemRetry = (TTimeout) 0x1194000;

// DEVIATION: the ROM converts the title and the user's name into 100-byte
// buffers with no limit, and doubles them in place for MakeTextPSFriendly;
// a title of 100 characters or more runs off the end.  The host's buffers
// are twice that and a name is cut at 99 characters, so that the escaped
// text always fits.
enum { kPSNameLength = 99, kPSNameBuffer = 2 * (kPSNameLength + 1) };


PROTOCOL_IMPL_SOURCE_MACRO(TPSPrinter)		// ROM 0x0021ac14 Sizeof__10TPSPrinterSFv
PROTOCOL_CLASSINFO(TPSPrinter, "TPrinter", "", 0x20000, 0, nil)	// ROM 0x00388144 ClassInfo__10TPSPrinterSFv


// ROM 0x0021b9b0 Constructor__10TPSPrinterFPc
// The driver made by name, and its connection record made for it.
NewtonErr
TPSPrinter::Constructor(char* driverName)
{
	fError = kPR_ERR_NotFound;
	fCancelled = false;
	fCancelHandled = false;
	fLevel = 1;
	fDriver = nil;
	// (the ROM makes fFont's RefStruct here - its class info's MakeAt
	// constructs nothing; the host's MakeAt has constructed it already)
	fDriver = (TPSPrinterDriver*) NewByName("TPSPrinterDriver", driverName);
	if (fDriver != nil)
	{
		fError = noErr;
		fProblemFatal = false;
		// (the ROM: operator new of the 0x18-byte record, its two
		// RefStructs made nil)
		PrintConnect* connect = new PrintConnect;
		fDriver->fConnect = connect;
		fDriver->fPrinter = this;
	}
	return fError;
}


// ROM 0x0021bb10 Delete__10TPSPrinterFv
// The connection record and the driver given back.  (fFont's handle is
// not: ROM BUG (fixed) - each job leaves one RefHandle behind.  The fix
// gives it back too: the instance's memory goes without its destructor
// being run, so fFont's is run here.)
void
TPSPrinter::Delete()
{
	if (fDriver != nil)
	{
		PrintConnect* connect = fDriver->fConnect;
		if (connect != nil)
			delete connect;
		fDriver->Delete();
	}
	gSCPDevicePackageBusy = false;
	if (RomBugFixed())
		fFont.~RefStruct();
}


// ROM 0x0021af40 Open__10TPSPrinterFRC6RefVar
// The driver connected - asked again every so often while the printer is
// busy - and the printer port opened, its drawing procs the PostScript
// bottlenecks and its kind 0x200, so that the text procs' questions go to
// StdText rather than to PrStdText (qd/TextObject.cpp's TextProc).
NewtonErr
TPSPrinter::Open(RefArg connectInfo)
{
	if (CheckUserAbort())
		return fError;
	SetupConnect(fDriver->fConnect, connectInfo);
	Boolean opened;
	do
	{
		gSCPDevicePackageBusy = true;
		fError = fDriver->Open();
		opened = (fError == noErr);
		if (fError == kPR_ERR_Busy)
		{
			PrReleaseControl(kPSBusyRetry, this);
			CheckUserAbort();
		}
	} while (fError == kPR_ERR_Busy);
	if (fError == noErr)
	{
		PrPageInfo info;
		GetPageInfo(&info);
		OpenPort(info);
		GrafPort* port = GetPrinterPort();
		port->portBits.pixMapFlags |= 0x200;
		if (!SetupPSBottlenecks(port))
		{
			ClosePort();
			fError = kPR_ERR_NewtonError;
		}
	}
	if (fError != noErr && opened)
		fDriver->Close(true);
	fPageCount = 0;
	SetEmptyRect(&fClipBox);
	SetEmptyRect(&fVisBox);
	return fError;
}


// ROM 0x0021b07c Close__10TPSPrinterFv
// The trailer sent and the driver closed - again for as long as closing
// answers a problem the user puts right.
NewtonErr
TPSPrinter::Close()
{
	NewtonErr err = fError;
	if (ContinueIO())
		SendPSTrailer();
	for ( ; ; )
	{
		err = fDriver->Close(ErrorIsFatal(err));
		if (err == noErr)
			break;
		PrProblemResolution resolution = HandleError(err);
		if (resolution == kPrProblemNotFixed || err == noErr)
			break;
	}
	TearDownPSBottlenecks(GetPort());
	ClosePort();
	return fError;
}


// ROM 0x0021b0fc OpenPage__10TPSPrinterFv
// A page begun: the drawing state forgotten (font, gray, pen, the clip
// path), the header sent before the first, then %%Page, the turn of a
// landscape page and the page setup.  A landscape page's port is turned
// too, when it is still the paper's portrait shape - only the boxes of
// its clip and visible regions, which are rectangles.
NewtonErr
TPSPrinter::OpenPage()
{
	if (!CheckUserAbort() && ContinueIO())
	{
		fFont = NILREF;
		fFontSize = 0;
		fFontFace = 0;
		fGray = 0;
		fPenSize.v = 0;
		fPenSize.h = 0;
		fFillPen.v = 0;
		fFillPen.h = 0;
		fDoPatternFill = false;
		fPageCount++;
		SetEmptyRect(&fClipBox);
		SetEmptyRect(&fVisBox);
		if (fPageCount == 1)
			SendPSHeader(fDriver->fConnect->fConnectInfo);
		sprintf(fBuffer, "\r\r%%%%Page: %d %d\r\r", (int) fPageCount, (int) fPageCount);
		SendPSText(fBuffer, false);
		if (!fDriver->fConnect->fPortrait)
		{
			GrafPort* port = GetPrinterPort();
			Rect* vis = &(*port->visRgn)->rgnBBox;
			if (vis->bottom > vis->right)
			{
				Rect* clip = &(*port->clipRgn)->rgnBBox;
				SetRect(clip, clip->top, clip->left, clip->bottom, clip->right);
				vis = &(*port->visRgn)->rgnBBox;
				SetRect(vis, vis->top, vis->left, vis->bottom, vis->right);
			}
			SendPSText((char*) gPSLandscape, false);
		}
		SendPSText((char*) gOpenPageHeader, false);
		if (fError == noErr)
			fError = fDriver->OpenPage();
		if (fError != noErr)
			HandleError(fError);
	}
	return fError;
}


// ROM 0x0021b930 ClosePage__10TPSPrinterFv
NewtonErr
TPSPrinter::ClosePage()
{
	if (!CheckUserAbort() && ContinueIO())
	{
		SendPSText((char*) gClosePageHeader, false);
		NewtonErr err = fDriver->ClosePage();
		if (err != noErr)
			HandleError(err);
	}
	CheckUserAbort();
	return fError;
}


// ROM 0x0021b2ec RepeatPage__10TPSPrinterFv
// A page is drawn once: PostScript has no bands.
Boolean
TPSPrinter::RepeatPage()
{
	return false;
}


// ROM 0x0021ac1c CancelJob__10TPSPrinterFUc
void
TPSPrinter::CancelJob(Boolean asyncCancel)
{
	fDriver->CancelJob(asyncCancel);
	if (!asyncCancel)
		fError = kPR_ERR_UserCancel;
}


// ROM 0x0021aecc IsProblemResolved__10TPSPrinterFv
PrProblemResolution
TPSPrinter::IsProblemResolved()
{
	return fDriver->IsProblemResolved();
}


// ROM 0x0021b998 SetPortraitOrientation__10TPSPrinterFUc
void
TPSPrinter::SetPortraitOrientation(Boolean portrait)
{
	fDriver->fConnect->fPortrait = portrait;
}


// ROM 0x0021b9a8 FaxEndPage__10TPSPrinterFl
NewtonErr
TPSPrinter::FaxEndPage(long /*pageCount*/)
{
	return noErr;
}


/*------------------------------------------------------------------------------
	T h e   d o c u m e n t
------------------------------------------------------------------------------*/

// ROM 0x0021ad38 GetPageInfo__10TPSPrinterFP10PrPageInfo
// The page at 300 dots an inch: letter 2400 x 3233 dots, A4 2331 x 3323.
void
TPSPrinter::GetPageInfo(PrPageInfo* info)
{
	if (!EQRef(fDriver->fConnect->fPaperSize, RSSYMa4))
	{
		fPageSize.h = 0x0960;
		fPageSize.v = 0x0ca1;
	}
	else
	{
		fPageSize.h = 0x091b;
		fPageSize.v = 0x0cfb;
	}
	info->printerPageSize = fPageSize;
	info->printerDPI.y = ToFixed(300);
	info->printerDPI.x = ToFixed(300);
}


// ROM 0x0021acc0 GetDocTitle__10TPSPrinterFRC6RefVarPc
// The connection frame's title, in Mac Roman.
void
TPSPrinter::GetDocTitle(RefArg connectInfo, char* title)
{
	RefVar str(GetFrameSlotRef(connectInfo, RSSYMtitle));
	if (ISNIL(str))
		*title = 0;
	else
		ConvertFromUnicode(GetCString(str), title, kMacRomanEncoding, kPSNameLength);
}


// ROM 0x0021adf0 GetUserName__10TPSPrinterFRC6RefVarPc
// The owner's name, from the user configuration.
void
TPSPrinter::GetUserName(RefArg /*connectInfo*/, char* name)
{
	RefVar str(GetPreference(RefVar(RSSYMname)));
	if (ISNIL(str))
		*name = 0;
	else
		ConvertFromUnicode(GetCString(str), name, kMacRomanEncoding, kPSNameLength);
}


// ROM 0x0021aed4 MakeTextPSFriendly__10TPSPrinterFPc
// The text made fit for a PostScript string: (, ) and \ escaped.
void
TPSPrinter::MakeTextPSFriendly(char* text)
{
	char friendly[kPSNameBuffer];		// (the ROM's: 100 bytes)
	long n = 0;
	for (char* p = text; *p != 0; p++)
	{
		char c = *p;
		if (c == '(' || c == ')' || c == '\\')
			friendly[n++] = '\\';
		friendly[n++] = c;
	}
	friendly[n] = 0;
	strcpy(text, friendly);
}


// ROM 0x0021b2f4 SetupFontMapping__10TPSPrinterFRC6RefVarT1
// A PostScript font made over again in the Mac encoding, as "<name>-Mac":
// the name is the family's slot for the face.
void
TPSPrinter::SetupFontMapping(RefArg family, RefArg face)
{
	RefVar name(GetFrameSlotRef(family, face));
	if (NOTNIL(name))
	{
		char psName[128];
		ConvertFromUnicode(GetCString(name), psName, kMacRomanEncoding, 0x7FFFFFFF);
		sprintf(fBuffer, "/%s-Mac /%s EncodeFont\r", psName, psName);
		SendPSText(fBuffer, false);
	}
}


// ROM 0x0021b398 SendPSHeader__10TPSPrinterFRC6RefVar
// The document's header: the DSC comments, the prolog, the Mac-encoded
// fonts - every face of every PostScript font family in vars.psFonts but
// a Shift-JIS one (prencoding 6) - and the job's name.
void
TPSPrinter::SendPSHeader(RefArg connectInfo)
{
	char title[kPSNameBuffer];			// (the ROM's: 100 bytes each)
	char user[kPSNameBuffer];
	char date[100];
	SendPSText((char*) "%!PS-Adobe-3.0\r", false);
	GetDocTitle(connectInfo, title);
	sprintf(fBuffer, "%%%%Title: %s\r", title);
	SendPSText(fBuffer, false);
	SendPSText((char*) "%%Creator: Out Box\r", false);
	RefVar now(FTime(RefVar(NILREF)));
	RefVar when(FDateNTime(RefVar(NILREF), now));
	ConvertFromUnicode(GetCString(when), date, kMacRomanEncoding, sizeof(date) - 1);
	sprintf(fBuffer, "%%%%CreationDate: %s\r", date);
	SendPSText(fBuffer, false);
	SendPSText((char*) "%%Pages: (atend)\r", false);
	GetUserName(connectInfo, user);
	sprintf(fBuffer, "%%%%For: %s\r", user);
	SendPSText(fBuffer, false);
	SendPSText((char*) gPostscriptHeader, false);
	SendPSText((char*) gPostscriptHeader2, false);
	RefVar psFonts(GetFrameSlotRef(RefVar(gVarFrame), RSSYMpsfonts));
	RefVar encoding(NILREF);
	if (NOTNIL(psFonts))
	{
		TObjectIterator iter(psFonts, true);
		for ( ; !iter.Done(); iter.Next())
		{
			if (EQRef(iter.fTag, RSSYM_proto))
				continue;
			encoding = GetFrameSlotRef(iter.fValue, RSSYMprencoding);
			if (NOTNIL(encoding) && RINT(encoding) == 6)
				continue;
			SetupFontMapping(iter.fValue, RefVar(RSSYMnormal));
			SetupFontMapping(iter.fValue, RefVar(RSSYMbold));
			SetupFontMapping(iter.fValue, RefVar(RSSYMitalic));
			SetupFontMapping(iter.fValue, RefVar(RSSYMbolditalic));
		}
	}
	SendPSText((char*) "%%EndProlog\r", false);
	SendPSText((char*) "%%BeginSetup\r", false);
	MakeTextPSFriendly(user);
	MakeTextPSFriendly(title);
	sprintf(fBuffer, "(%s; document: %s) jn\r", user, title);
	SendPSText(fBuffer, false);
	SendPSText((char*) "%%EndSetup\r", false);
}


// ROM 0x0021b7b8 SendPSTrailer__10TPSPrinterFv
void
TPSPrinter::SendPSTrailer()
{
	SendPSText((char*) "%%Trailer\r", false);
	sprintf(fBuffer, "%%%%Pages: %d\r", (int) fPageCount);
	SendPSText(fBuffer, false);
	SendPSText((char*) "%%EOF\r", false);
}


// ROM 0x0021b830 SendPSText__10TPSPrinterFPcUc
// A text sent - what is left of it again after a problem the user puts
// right - unless the job has an error already.
void
TPSPrinter::SendPSText(char* text, Boolean eoj)
{
	if (fError != noErr)
		return;
	for ( ; ; )
	{
		ULong sent;
		NewtonErr err = fDriver->SendPSText(text, sent, eoj);
		if (err == noErr)
			return;
		if (HandleError(err) != kPrProblemFixed)
			break;
		text += sent;
	}
}


// ROM 0x0021b89c SendPSBinary__10TPSPrinterFPcUl
// Bytes sent, as SendPSText sends a text.  (After a problem the rest is
// sent from where it stopped, but the size is not made smaller: ROM BUG
// (fixed) - the bytes after the end go too.  The fix takes what was sent
// off the size.)
void
TPSPrinter::SendPSBinary(char* data, ULong size)
{
	if (fError != noErr)
		return;
	for ( ; ; )
	{
		ULong sent;
		NewtonErr err = fDriver->SendPSBinary(data, size, sent);
		if (err == noErr)
			return;
		if (HandleError(err) != kPrProblemFixed)
			break;
		data += sent;
		if (RomBugFixed())
			size = (sent < size) ? size - sent : 0;
	}
}


/*------------------------------------------------------------------------------
	E r r o r s   a n d   p r o b l e m s
------------------------------------------------------------------------------*/

// ROM 0x0021add0 GetStatus__10TPSPrinterFv
NewtonErr
TPSPrinter::GetStatus()
{
	return fError = fDriver->GetStatus();
}


// ROM 0x0021bc00 ErrorIsFatal__10TPSPrinterFl
// An error of the printing system's own (-44099 .. -44000).
Boolean
TPSPrinter::ErrorIsFatal(long error)
{
	return error >= kPR_ERR_MINERROR && error <= kPR_ERR_MAXERROR;
}


// ROM 0x0021ac98 ErrorIsProblem__10TPSPrinterFl
// A problem the user can put right (-44199 .. -44100).
Boolean
TPSPrinter::ErrorIsProblem(long error)
{
	return error >= kPR_ERR_MINPROBLEM && error <= kPR_ERR_MAXPROBLEM;
}


// ROM 0x0021ac50 ErrorIsPrintingError__10TPSPrinterFl
Boolean
TPSPrinter::ErrorIsPrintingError(long error)
{
	return ErrorIsFatal(error) || ErrorIsProblem(error);
}


// ROM 0x0021b2e4 ProblemIsFatal__10TPSPrinterFv
Boolean
TPSPrinter::ProblemIsFatal()
{
	return fProblemFatal;
}


// ROM 0x0021b908 SetSoftError__10TPSPrinterFl
// The job's error, unless it has a fatal one already.
void
TPSPrinter::SetSoftError(long error)
{
	if (!ErrorIsFatal(fError))
		fError = error;
}


// ROM 0x0021ae54 HandleError__10TPSPrinterFl
// An error from the driver: kept as the job's, and a problem put to the
// user.  ==> kPrProblemNotFixed when the job cannot go on.
PrProblemResolution
TPSPrinter::HandleError(long error)
{
	if (ErrorIsPrintingError(error))
		SetSoftError(error);
	if (ErrorIsFatal(error) || ProblemIsFatal())
		return kPrProblemNotFixed;
	if (ErrorIsProblem(error))
		return DoHandleProblem(error);
	return kPrProblemFixed;
}


// ROM 0x0021bb6c DoHandleProblem__10TPSPrinterFl
// The problem slip shown until the printer's status is no longer a problem
// or the user cancels it.
PrProblemResolution
TPSPrinter::DoHandleProblem(long problem)
{
	PrProblemResolution resolution;
	for ( ; ; )
	{
		fProblem = problem;
		resolution = CallHandleProblem(fDriver->fConnect, this, problem, kPSProblemRetry, false);
		if (resolution == kPrProblemNotFixed)
			fProblemFatal = true;
		else
		{
			if (!ErrorIsProblem(GetStatus()))
				break;
			problem = fError;
		}
		if (problem == noErr || fProblemFatal)
			break;
	}
	return resolution;
}


// ROM 0x0021ba80 ContinueIO__10TPSPrinterFv
// Whether there is any point talking to the printer.
Boolean
TPSPrinter::ContinueIO()
{
	return !(fError == kPR_ERR_PrinterError || ProblemIsFatal());
}


// ROM 0x0021bab8 ContinueRendering__10TPSPrinterFv
// Whether there is any point drawing.
Boolean
TPSPrinter::ContinueRendering()
{
	if (fError == kPR_ERR_NewtonError || fError == kPR_ERR_UserCancel)
		return false;
	if (!ContinueIO())
		return false;
	return !CheckUserAbort();
}


/*------------------------------------------------------------------------------
	T P S P r i n t e r D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x003881bc Delete__16TPSPrinterDriverFv
void
TPSPrinterDriver::Delete()
{
	GlueDelete();
}


/*------------------------------------------------------------------------------
	T P S P A P D r i v e r
	(only what is not AppleTalk's; the PAP calls' host stand-ins are
	print/host/HostPAPDriver.cpp)
------------------------------------------------------------------------------*/

PROTOCOL_IMPL_SOURCE_MACRO(TPSPAPDriver)		// ROM 0x0021a348 Sizeof__12TPSPAPDriverSFv
PROTOCOL_CLASSINFO(TPSPAPDriver, "TPSPrinterDriver", "", 0x20000, 0, nil)	// ROM 0x00388520 ClassInfo__12TPSPAPDriverSFv

// ROM 0x0021a408 CancelJob__12TPSPAPDriverFUc
void
TPSPAPDriver::CancelJob(Boolean asyncCancel)
{
	if (!asyncCancel)
		fError = kPR_ERR_UserCancel;
	else
		fCancelled = true;
}


// ROM 0x0021a674 RepeatPSPage__12TPSPAPDriverFv
NewtonErr
TPSPAPDriver::RepeatPSPage()
{
	return noErr;
}


// ROM 0x0021a654 OpenPage__12TPSPAPDriverFv
// The printer's status taken; a page always opens.
NewtonErr
TPSPAPDriver::OpenPage()
{
	fError = GetStatus();
	return noErr;
}


// ROM 0x0021a958 ClosePage__12TPSPAPDriverFv
NewtonErr
TPSPAPDriver::ClosePage()
{
	if (fError == noErr)
		fError = GetStatus();
	return noErr;
}


// ROM 0x0021abf0 IsProblemResolved__12TPSPAPDriverFv
// ==> nought (kPrProblemFixed) when the printer's status is all well.
PrProblemResolution
TPSPAPDriver::IsProblemResolved()
{
	fError = GetStatus();
	return (PrProblemResolution) (fError != noErr);
}


// ROM 0x0021aa84 InterpretPAPStatusString__12TPSPAPDriverFP10TString255Uc
// A status that came as a Pascal string.
NewtonErr
TPSPAPDriver::InterpretPAPStatusString(unsigned char* status, Boolean idleIsFine)
{
	char text[256];
	memcpy(text, status + 1, status[0]);
	text[status[0]] = 0;
	return InterpretPAPString(text, idleIsFine);
}


// ROM 0x0021aabc InterpretPAPString__12TPSPAPDriverFPcUc
// A PostScript printer's status message read: what follows its last
// "status:" - and in that, its last "PrinterError:", or with no "status:"
// at all its last "Error:" - looked for each of gPSStatusStrings' words.
// ==> the first word's problem; with no word known, kPR_ERR_PrinterError;
// a word that means all is well (nought) the driver's own error; and
// "busy", "waiting" or "printing" (kPR_ERR_Busy) nought when idleIsFine.
NewtonErr
TPSPAPDriver::InterpretPAPString(char* status, Boolean idleIsFine)
{
	NewtonErr result = kPR_ERR_PrinterError;
	size_t len = strlen("status:");
	char* tail = status;
	for (char* p = strstr(status, "status:"); p != nil; p = strstr(tail, "status:"))
		tail = p + len;
	const char* error = (status == tail) ? "Error:" : "PrinterError:";
	len = strlen(error);
	for (char* p = strstr(tail, error); p != nil; p = strstr(tail, error))
		tail = p + len;
	for (const IntCString* s = gPSStatusStrings; s < gPSStatusStrings + sizeof(gPSStatusStrings) / sizeof(gPSStatusStrings[0]); s++)
	{
		if (strstr(tail, s->fText) != nil)
		{
			result = s->fCode;
			break;
		}
	}
	if (result == kPR_ERR_Busy)
	{
		if (idleIsFine)
			result = noErr;
	}
	else if (result == noErr)
		result = fError;
	return result;
}
