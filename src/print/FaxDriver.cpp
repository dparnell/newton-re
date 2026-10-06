/*
	File:		print/FaxDriver.cpp

	Contains:	TFaxDriver and TFaxDriverData: the fax as a printer
				(print/FaxDriver.h).

	Reconstructed from the MP2x00 US ROM (0x0020f044-0x0020fae4); each
	function cites its origin.
*/

#include "print/FaxDriver.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "ModemOptions.h"
#include "utility/AppWorld.h"
#include "utility/PseudoSyncState.h"
#include "utility/Unicode.h"
#include "UserTasks.h"
#include "host/RomBugs.h"
#include <string.h>
#include <stdint.h>

extern const unsigned int	kFaxFineLetter[3];		// print/FaxDriverTables.cpp: the page, as a PrPageInfo
extern const unsigned int	kFaxStandardLetter[3];
extern const unsigned int	kFaxFineA4[3];
extern const unsigned int	kFaxStandardA4[3];

// what a waiting PrReleaseControl is given: until PrRegainControl
static const TTimeout	kUntilRegained = (TTimeout) 0xFFFFFFFFu;

PROTOCOL_IMPL_SOURCE_MACRO(TFaxDriver)		// ROM 0x0020f044 Sizeof__10TFaxDriverSFv
PROTOCOL_CLASSINFO(TFaxDriver, "TDotPrinterDriver", "", 0x20000, 0, nil)	// ROM 0x00388298 ClassInfo__10TFaxDriverSFv


/*------------------------------------------------------------------------------
	T F a x D r i v e r D a t a
------------------------------------------------------------------------------*/

// ROM 0x0038b9a0 (unnamed) - TFaxDriverData::~TFaxDriverData
TFaxDriverData::~TFaxDriverData()
{ }


// the print job waiting in PrReleaseControl let go on (PrRegainControl's
// code, which the ROM repeats here)
static void
LetPrinterGoOn(TPrinter* printer)
{
	if (printer->fBlocked != nil)
		printer->fBlocked->Unblock();
}


// ROM 0x0020f6d4 OpenSessionComplete__14TFaxDriverDataFlUlN32
// The session's open came back: kept, and the job let go on.
void
TFaxDriverData::OpenSessionComplete(NewtonErr err, ULong connected, ULong reserved, ULong hRes, ULong vRes)
{
	fWaiting = false;
	fSessionOpen = true;
	fOpenErr = err;
	fConnected = connected;
	fReserved = reserved;
	fHRes = hRes;
	fVRes = vRes;
	LetPrinterGoOn(fPrinter);
}


// ROM 0x0020f70c CloseSessionComplete__14TFaxDriverDataFl
void
TFaxDriverData::CloseSessionComplete(NewtonErr err)
{
	fCloseErr = err;
}


// ROM 0x0020f714 BeginPageComplete__14TFaxDriverDataFl
void
TFaxDriverData::BeginPageComplete(NewtonErr err)
{
	fBeginPageErr = err;
}


// ROM 0x0020f71c EndPageComplete__14TFaxDriverDataFl
void
TFaxDriverData::EndPageComplete(NewtonErr err)
{
	fEndPageErr = err;
}


// ROM 0x0020f724 PrintBandComplete__14TFaxDriverDataFl
// A band sent: kept, and the job let go on.
void
TFaxDriverData::PrintBandComplete(NewtonErr err)
{
	fBandPending = false;
	fBandErr = err;
	LetPrinterGoOn(fPrinter);
}


/*------------------------------------------------------------------------------
	T F a x D r i v e r
------------------------------------------------------------------------------*/

// ROM 0x0020f04c Open__10TFaxDriverFv
// The fax tool started over the modem tool (the fax service, a fax
// connection, sending) and the session's options made: the page set up
// at the connection frame's faxResolution (1 normal, 2 anything else),
// the user's dialling preferences (manual dialling when the frame asks,
// without waiting for a dial tone or busy), our identity (localId) and
// the other end's asked for.  The number dialled is the frame's
// phoneNumber (a space when it has none); the job waits for the session
// and, once it is open, the other end's identity goes back in the
// frame's remoteId.  ROM QUIRK: the session's own error is not looked at
// here - Open answers what the driver's error was (a cancel, while it
// waited), and the session's failure shows on the first page.
NewtonErr
TFaxDriver::Open()
{
	RefVar connectInfo(fConnect->fConnectInfo);
	fData = nil;
	fConfig = nil;
	fSessionOptions = nil;
	fPageOpen = false;
	fLinesSent = -1;
	fError = noErr;
	if ((fConfig = new TOptionArray) == nil)
		return kPR_ERR_NewtonError;
	fConfig->Init();
	if ((fSessionOptions = new TOptionArray) == nil)
		return kPR_ERR_NewtonError;
	fSessionOptions->Init();
	fData = new TFaxDriverData('faxs', 'mods');
	if (fData == nil)
		return kPR_ERR_NewtonError;
	fData->fPrinter = fPrinter;
	fData->fWaiting = false;
	fData->fSessionOpen = false;
	fData->fBandPending = false;
	if (fError == noErr)
	{
		fData->SetDefaultConfig(fConfig, 0);
		NewtonErr err = fData->Init(fConfig, 'faxs', kNewtEventClass);
		if (fError == noErr)
			fError = err;
		if (fError == noErr)
		{
			fData->SetDefaultOptions(fSessionOptions);
			TCMOModemDialing dialing;
			SetDialingOptionsFromPrefs(&dialing);
			char number[256];
			RefVar phoneNumber(GetFrameSlot(connectInfo, RSSYMphonenumber));
			if (ISNIL(phoneNumber))
			{
				number[0] = ' ';
				number[1] = 0;
			}
			else
				ConvertFromUnicode(GetCString(phoneNumber), number, kMacRomanEncoding, 0x7fffffff);
			RefVar manual(GetFrameSlot(connectInfo, RSSYMmanualdialing));
			dialing.fManualDial = NOTNIL(manual);
			if (dialing.fManualDial)
			{
				dialing.fDetectDialTone = 0;
				dialing.fDetectBusy = 0;
			}
			fSessionOptions->InsertOptionAt(fSessionOptions->GetArrayCount(), &dialing);
			RefVar localId(GetFrameSlot(connectInfo, RSSYMlocalid));
			if (NOTNIL(localId))
			{
				TCMOFaxLocalId id;
				id.SetOpCode(opSetRequired);
				ConvertFromUnicode(GetCString(localId), id.fId, kMacRomanEncoding, 0x14);
				fSessionOptions->InsertOptionAt(fSessionOptions->GetArrayCount(), &id);
			}
			RefVar resolution(GetFrameSlot(connectInfo, RSSYMfaxresolution));
			if (NOTNIL(resolution))
			{
				ULong value = EQ(resolution, RSSYMnormal) ? 1 : 2;
				TOptionIterator iter(fSessionOptions);
				TCMOFaxPageSetUp* setUp = (TCMOFaxPageSetUp*) iter.FindOption(kCMOFaxPageSetUp);
				if (setUp != nil)
					setUp->fResolution = value;
			}
			TCMOFaxRemoteId remoteId;
			remoteId.SetOpCode(opGetCurrent);
			fSessionOptions->InsertOptionAt(fSessionOptions->GetArrayCount(), &remoteId);
			if (fError != kPR_ERR_UserCancel)
			{
				fData->OpenSession(fSessionOptions, (UChar*) number, strlen(number), true);
				fData->fWaiting = true;
				PrReleaseControl(kUntilRegained, fPrinter);
				fData->fWaiting = false;
				if (fError != kPR_ERR_UserCancel && fError == noErr)
				{
					TOptionIterator iter(fSessionOptions);
					TCMOFaxRemoteId* remote = (TCMOFaxRemoteId*) iter.FindOption(kCMOFaxRemoteId);
					if (remote != nil)
					{
						UniChar name[0x18];
						ConvertToUnicode(remote->fId, name, kMacRomanEncoding, 0x14);
						RefVar str(MakeString(name));
						SetFrameSlot(connectInfo, RSSYMremoteid, str);
					}
				}
			}
		}
	}
	return fError;
}


// ROM 0x0020f4e8 GetPageInfo__10TFaxDriverFP10PrPageInfo
// The page as the session agreed it: 200 dots an inch across, and 200 or
// 100 down, letter or A4 as the paper is.  ROM BUG (fixed): asked before
// the session's open has come back it waits for it by spinning on the
// flag, which nothing can set while this task spins - it never returns.
// (TDotPrinter only asks after Open, which has waited properly.)  The fix
// waits as Open does - in PrReleaseControl, with fWaiting set so a cancel
// can let it go - and gives up waiting once the job is cancelled (the
// page then the standard one, as for a resolution it does not know).
void
TFaxDriver::GetPageInfo(PrPageInfo* info)
{
	Boolean a4 = EQ(fConnect->fPaperSize, RSSYMa4);
	if (RomBugFixed())
	{
		while (!fData->fSessionOpen && fError != kPR_ERR_UserCancel)
		{
			fData->fWaiting = true;
			PrReleaseControl(kUntilRegained, fPrinter);
			fData->fWaiting = false;
		}
	}
	else
		while (!fData->fSessionOpen)
			;
	const unsigned int* page;
	if (fData->fVRes == 0xc4)
		page = a4 ? kFaxFineA4 : kFaxFineLetter;
	else if (fData->fVRes == 0x62)
		page = a4 ? kFaxStandardA4 : kFaxStandardLetter;
	else
		page = a4 ? kFaxStandardA4 : kFaxStandardLetter;
	fPageLines = (long) (int32_t) page[2] >> 16;
	info->printerDPI.x = (Fixed) page[0];
	info->printerDPI.y = (Fixed) page[1];
	info->printerPageSize.v = (short) (page[2] >> 16);
	info->printerPageSize.h = (short) page[2];
}


// ROM 0x0020f5e4 GetBandPrefs__10TFaxDriverFP15DotPrinterPrefs
// Bands of 25 lines, sent asynchronously, the black's bounds wanted.
void
TFaxDriver::GetBandPrefs(DotPrinterPrefs* prefs)
{
	prefs->minBand = 25;
	prefs->optimumBand = 25;
	prefs->asyncBanding = true;
	prefs->wantMinBounds = true;
}


// ROM 0x0020f604 PrintBlankLines__10TFaxDriverFl
// So many white lines sent (a band of no bytes), once the band before
// has gone.
void
TFaxDriver::PrintBlankLines(long count)
{
	if (!ContinueIO())
		return;
	if (fData->fBandPending)
		PrReleaseControl(kUntilRegained, fPrinter);
	fData->fBandPending = true;
	fData->PrintBand(nil, count, 0, 0, true);
	fLinesSent += count;
}


// ROM 0x0020f684 Delete__10TFaxDriverFv
void
TFaxDriver::Delete()
{
	if (fData != nil)
		delete fData;
	if (fConfig != nil)
		delete fConfig;
	if (fSessionOptions != nil)
		delete fSessionOptions;
}


// ROM 0x0020f738 Close__10TFaxDriverFv
// The last page ended (if one is open), the last band waited for, and the
// session closed.
NewtonErr
TFaxDriver::Close()
{
	if (ContinueIO() && fPageOpen)
	{
		fData->EndPage(false, true);
		fError = fData->fEndPageErr;
	}
	if (ContinueIO() && fData->fBandPending)
		PrReleaseControl(kUntilRegained, fPrinter);
	fData->CloseSession(false);
	if (fError == noErr)
		fError = fData->fCloseErr;
	return fError;
}


// ROM 0x0020f7d4 OpenPage__10TFaxDriverFv
// A page begun, and a quarter inch of white at its top.
NewtonErr
TFaxDriver::OpenPage()
{
	if (ContinueIO())
	{
		fData->BeginPage(false);
		fError = fData->fBeginPageErr;
		fPageOpen = true;
	}
	fLinesSent = 0;
	PrintBlankLines(fData->fVRes >> 2);
	return fError;
}


// ROM 0x0020f83c FaxEndPage__10TFaxDriverFl
// A page ended with another to follow (TDotPrinter says so between pages).
NewtonErr
TFaxDriver::FaxEndPage(long pageCount)
{
	if (pageCount != 0 && ContinueIO() && fPageOpen)
	{
		fData->EndPage(false, false);
		if (fError == noErr)
			fError = fData->fEndPageErr;
	}
	return fError;
}


// ROM 0x0020f8a0 ClosePage__10TFaxDriverFv
// A quarter inch of white at the page's foot.
NewtonErr
TFaxDriver::ClosePage()
{
	PrintBlankLines(fData->fVRes >> 2);
	fLinesSent = -1;
	return fError;
}


// ROM 0x0020f8d4 ImageBand__10TFaxDriverFP8PixelMapPC4Rect
// A band sent: the rows above the black's bounds as white lines, the
// rows it covers (whole rows, the left and right ignored) as a band, and
// the rows below as white lines again; a band with no black is all white
// lines.  ROM QUIRK: when the band before failed, it answers 1 rather
// than the error.
NewtonErr
TFaxDriver::ImageBand(PixelMap* band, const Rect* minRect)
{
	ULong leftOffset = fData->fHRes >> 3;
	long rows = band->bounds.bottom - band->bounds.top;
	long blackRows = minRect->bottom - minRect->top;
	if (!ContinueIO())
		return fError;
	if (blackRows <= 0)
	{
		PrintBlankLines(rows);
		return fError;
	}
	UChar* data = (UChar*) band->baseAddr;
	long above = minRect->top - band->bounds.top;
	if (above != 0)
	{
		PrintBlankLines(above);
		data += above * band->rowBytes;
	}
	if (ContinueIO() && fData->fBandPending)
		PrReleaseControl(kUntilRegained, fPrinter);
	if (fData->fBandErr != noErr)
		return 1;
	if (ContinueIO())
	{
		fData->fBandPending = true;
		fData->PrintBand(data, blackRows, band->rowBytes, leftOffset, true);
		fLinesSent += blackRows;
	}
	long below = band->bounds.bottom - minRect->bottom;
	if (below != 0)
		PrintBlankLines(below);
	return fError;
}


// ROM 0x0020fa1c CancelJob__10TFaxDriverFUc
// Cancelled: in the middle of a page (not asynchronously) the rest of it
// sent white, no more than an inch of it; asynchronously while Open waits
// for the session, the session closed and the job let go on.
void
TFaxDriver::CancelJob(Boolean asyncCancel)
{
	if (fData->fSessionOpen && !asyncCancel)
	{
		if (fLinesSent > 0)
		{
			ULong rest = fPageLines - fLinesSent;
			if (fData->fVRes < rest)
				rest = fData->fVRes;
			PrintBlankLines(rest);
		}
	}
	else if (asyncCancel && fData->fWaiting)
	{
		fData->CloseSession(false);
		fError = kPR_ERR_UserCancel;
		PrRegainControl(fPrinter);
		return;
	}
	fError = kPR_ERR_UserCancel;
}


// ROM 0x0020fab4 IsProblemResolved__10TFaxDriverFv
PrProblemResolution
TFaxDriver::IsProblemResolved()
{
	return kPrProblemNotFixed;
}


// ROM 0x0020fac0 ContinueIO__10TFaxDriverFv
// Whether to go on sending: no error, or a cancel (the page is finished
// in white), or -22005.
Boolean
TFaxDriver::ContinueIO()
{
	return fError == noErr || fError == kPR_ERR_UserCancel || fError == -22005;
}
