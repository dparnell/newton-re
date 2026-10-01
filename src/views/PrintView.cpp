/*
	File:		views/PrintView.cpp

	Contains:	TPrintView and the print job's C side.  See PrintView.h.

	Reconstructed from the MP2x00 US ROM (0x00192f90-0x001944b0; SetStatus
	0x000e895c); each function cites its origin.
*/

#include "PrintView.h"
#include "RootView.h"
#include "Commands.h"
#include "Rects.h"
#include "Regions.h"
#include "Ports.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "Interpreter.h"
#include "REPTranslators.h"
#include "SortTables.h"
#include "FixedMath.h"
#include "NewtonExceptions.h"
#include "utility/AppWorld.h"
#include "utility/Unicode.h"
#include "UserTasks.h"
#include "NewtonTime.h"
#include "VirtualMemory.h"
#include "CommErrors.h"

extern const ExceptionName	exOutOfMemory;
extern GrafPort		gGrafPort;
Ref		FOpenX(RefArg rcvr);
Ref		FCloseX(RefArg rcvr);

// the fax progress: the page a PAP job (a received fax printed) is on,
// and the transport it goes to (0x0c1017a0 gTransportProgress; the page
// is the word after it, 0x0c1017a4)
RefStruct*	gTransportProgress = nil;
static long	gTransportProgressPage = 0;

enum
{
	kPrintCommand = 56,			// PostCommand(printView, 56): print
	kCancelPrintCommand = 57,	// PostCommand(printView, 57): cancel
	kPrintNothingCommand = 44
};

// what the fax driver's ContinueIO also lets pass: a fax call the tool
// retries (the page is printed again, up to three times)
const NewtonErr	kFaxErrRetry = -22005;


// ROM 0x000e895c SetStatus__FRC6RefVarN21
// The transport's SetStatus(status, args).
void
SetStatus(RefArg transport, RefArg status, RefArg args)
{
	RefVar message(MakeArray(2));
	SetArraySlot(message, 0, status);
	SetArraySlot(message, 1, args);
	DoMessage(transport, RSSYMsetstatus, message);
}


// ROM 0x00193dbc SetPrintProgress__FRC6RefVarlT2
// The transport's SetPrintProgress(page, percent), drawn in the
// Newton's own port.
void
SetPrintProgress(RefArg transport, long page, long percent)
{
	GrafPort* saved = GetCurrentPort();
	SetPort(&gGrafPort);
	RefVar message(MakeArray(2));
	SetArraySlot(message, 0, MAKEINT(page));
	SetArraySlot(message, 1, MAKEINT(percent));
	DoMessage(transport, RSSYMsetprintprogress, message);
	SetPort(saved);
}


// ROM 0x00193e70 SetFaxPrintProgress__FRC6RefVarlT2
// A percentage of nought records the page; anything else is the progress
// on it, told to the transport (if there is one).
void
SetFaxPrintProgress(RefArg transport, long page, long percent)
{
	long savedPage = gTransportProgressPage;
	if (percent == 0)
	{
		gTransportProgressPage = page;
		return;
	}
	if (ISNIL(transport))
		return;
	GrafPort* saved = GetCurrentPort();
	SetPort(&gGrafPort);
	RefVar message(MakeArray(2));
	SetArraySlot(message, 0, MAKEINT(savedPage));
	SetArraySlot(message, 1, MAKEINT(percent));
	DoMessage(transport, RSSYMsetprintprogress, message);
	SetPort(saved);
}


// ROM 0x00193e9c HandleProblem__FRC6RefVarP8TPrinterlUlUc
// A driver's problem (no paper, a door open ...) shown in the root's
// printProblem slip, the printer asked every retry time whether it has
// been put right, for two minutes at most or until the user dismisses
// the slip.  ==> kPrProblemNotFixed if the printer did not say it was
// fixed and the user did not press the slip's Fixed, or the time ran out.
PrProblemResolution
HandleProblem(RefArg connectInfo, TPrinter* printer, NewtonErr problem, TTimeout retryTime, Boolean fixedButton)
{
	GrafPort* saved = GetCurrentPort();
	SetPort(&gGrafPort);
	RefVar slip(gRootView->GetVar(RSSYMprintproblem));
	SetFrameSlot(slip, RSSYMerror, MAKEINT(problem));
	SetFrameSlot(slip, RSSYMretryallowed, fixedButton ? TRUEREF : NILREF);
	TTime deadline = TimeFromNow(0x1a5e0000);
	FOpenX(slip);
	Boolean fixed;
	long late;
	Ref dismissed;
	do
	{
		((TForkWorld*) GetGlobals())->ReleaseMutex();
		Sleep(retryTime);
		((TForkWorld*) GetGlobals())->AcquireMutex();
		fixed = printer->IsProblemResolved() == kPrProblemFixed;
		TTime now = GetGlobalTime();
		late = CompCompare(&now.time, &deadline.time);
		dismissed = GetProtoVariable(slip, RSSYMuserdismissed, nil);
		if (fixed || late > 0)
		{
			if (ISNIL(dismissed))
				FCloseX(slip);
			break;
		}
	} while (ISNIL(dismissed));
	SetPort(saved);
	if (!fixed)
		fixed = NOTNIL(GetProtoVariable(slip, RSSYMfixed, nil));
	return (!fixed || late > 0) ? kPrProblemNotFixed : kPrProblemFixed;
}


// ROM 0x0019441c CallHandleProblem__FP12PrintConnectP8TPrinterlUlUc
// The driver callback (DriverCallbacks.h).
PrProblemResolution
CallHandleProblem(PrintConnect* connect, TPrinter* printer, NewtonErr problem, TTimeout retryTime, Boolean fixedButton)
{
	return HandleProblem(connect->fConnectInfo, printer, problem, retryTime, fixedButton);
}


// ROM 0x00194070 MakePrinter__FRC6RefVar
// The printer a printer frame names: its imaging engine by its
// imagingName, constructed with its driverName (nil, and nothing made,
// when either is missing or the construction fails).
TPrinter*
MakePrinter(RefArg printer)
{
	RefVar driverName(GetProtoVariable(printer, RSSYMdrivername, nil));
	RefVar imagingName(GetProtoVariable(printer, RSSYMimagingname, nil));
	if (ISNIL(driverName) || ISNIL(imagingName))
		return nil;
	char name[256];
	ConvertFromUnicode(GetCString(imagingName), name, kMacRomanEncoding, 0x7fffffff);
	TPrinter* made = (TPrinter*) NewByName("TPrinter", name);
	if (made != nil)
	{
		ConvertFromUnicode(GetCString(driverName), name, kMacRomanEncoding, 0x7fffffff);
		if (made->Constructor(name) != noErr)
		{
			made->Delete();
			made = nil;
		}
	}
	return made;
}


// ROM 0x001941b4 DisposePrinter__FP8TPrinter
void
DisposePrinter(TPrinter* printer)
{
	printer->Delete();
}


/*------------------------------------------------------------------------------
	T P r i n t V i e w
------------------------------------------------------------------------------*/

// ROM 0x00192f90 ClassID__10TPrintViewCFv
long
TPrintView::ClassID(void) const
{
	return clPrintView;
}


// ROM 0x00192f98 DerivedFrom__10TPrintViewCFl
Boolean
TPrintView::DerivedFrom(long id) const
{
	return id == clPrintView || TView::DerivedFrom(id);
}


// ROM 0x00192fcc RealDoCommand__10TPrintViewFRC6RefVar
// The work done with 5K more stack locked down.
Boolean
TPrintView::RealDoCommand(RefArg cmd)
{
	TULockStack lockStack;
	LockStack(&lockStack, 0x1400);
	Boolean handled = false;
	newton_try
	{
		handled = ROMRealDoCommand(cmd);
	}
	cleanup
	{
		UnlockStack(&lockStack);
	}
	end_try;
	UnlockStack(&lockStack);
	return handled;
}


// ROM 0x00193054 ROMRealDoCommand__10TPrintViewFRC6RefVar
// Command 56 prints: the world forked (the fork takes the event loop
// over while the job runs here; when the world cannot fork the command
// is taken and nothing printed), the transport (the root's slot named by
// the fields' category) told what the job is doing, the printer made and
// opened with the fields, the view's viewShowScript run with its port
// current and the pages printed; the job's error, if any, goes in the
// fields' error slot - which the ROM then prints to the debugging port
// (PrintObject; the host's REP output).  Command 57 cancels, 44 is taken
// and ignored.
Boolean
TPrintView::ROMRealDoCommand(RefArg cmd)
{
	if (CommandID(cmd) == kPrintNothingCommand)
		return true;
	if (CommandID(cmd) != kPrintCommand)
	{
		if (CommandID(cmd) != kCancelPrintCommand)
			return false;
		if (fPrinter != nil)
			fPrinter->DoUserAbort();
		return true;
	}
	RefVar result;
	volatile NewtonErr err = ((TForkWorld*) GetGlobals())->Fork(nil);
	if (err != noErr)
		return true;
	RefVar fields(GetVar(RSSYMfields));
	RefVar category(GetProtoVariable(fields, RSSYMcategory, nil));
	RefVar transport(gRootView->GetVar(category));
	if (ISNIL(transport))
		Throw(exRootException, (void*) -8301, nil);
	RefVar pageCount(GetProtoVariable(fields, RSSYMpagecount, nil));
	fPageCount = ISNIL(pageCount) ? 0x7fff : RINT(pageCount);
	SetStatus(transport, RSSYMconnecting, RefVar(NILREF));
	newton_try
	{
		RefVar printer(GetProtoVariable(fields, RSSYMprinter, nil));
		fIsPAP = false;
		RefVar driverName(GetProtoVariable(printer, RSSYMdrivername, nil));
		UniChar papName[32];
		ConvertToUnicode("TPSPAPDriver", papName, kMacRomanEncoding, 0x7fffffff);
		if (CompareStringNoCase(GetCString(driverName), papName) == 0)
			fIsPAP = true;
		fPrinter = MakePrinter(printer);
		if (fPrinter == nil)
			err = kPR_ERR_NotFound;
		else
		{
			RemoveAllViews();
			AddViews(false);
			fUseFullPage = false;
			RefVar theFormat(GetVar(RSSYMtheformat));
			if (NOTNIL(theFormat) && NOTNIL(GetProtoVariable(theFormat, RSSYMusefullpage, nil)))
			{
				RefVar printForm(GetFrameSlot(fContext, RSSYMprintform));
				if (NOTNIL(printForm))
				{
					fFullPage = GetView(printForm)->viewBounds;
					fUseFullPage = true;
				}
			}
			newton_try
			{
				err = fPrinter->Open(fields);
				SetPort(&gGrafPort);
				if (err == noErr)
				{
					newton_try
					{
						SetStatus(transport, RSSYMpreparing, RefVar(NILREF));
						fPort = fPrinter->GetPort();
						SetPort(fPort);
						RunScript(RSSYMviewshowscript, RefVar(NILREF), true, nil);
						err = PrintPages(transport);
					}
					cleanup
					{
						SetPort(&gGrafPort);
						SetStatus(transport, RSSYMdisconnecting, RefVar(NILREF));
					}
					end_try;
					SetPort(&gGrafPort);
					SetStatus(transport, RSSYMdisconnecting, RefVar(NILREF));
				}
			}
			cleanup
			{
				DisposePrinter(fPrinter);
			}
			end_try;
			DisposePrinter(fPrinter);
		}
	}
	newton_catch(exRootException)
	{
		ClearFlags(0x90000000);
		if (Subexception(CurrentException()->name, exOutOfMemory))
			err = (NewtonErr) (intptr_t) CurrentException()->data;
		else
			err = kPR_ERR_NewtonError;
	}
	end_try;
	SetPort(&gGrafPort);
	if (err != noErr)
		result = MAKEINT(err);
	SetStatus(transport, RSSYMidle, RefVar(NILREF));
	if ((Ref) fields != 0)
	{
		SetFrameSlot(fields, RSSYMerror, result);
		// (the ROM's own, 0x001935a8: every print job's item printed to the
		// REP - on a MessagePad the debugger's serial stream, which nobody
		// sees; on the host, newton's stdout)
		PrintObject(fields, 0);
	}
	return true;
}


// ROM 0x00193618 PrintPages__10TPrintViewFRC6RefVar
// The pages, one after another while the print format's
// printNextPageScript says there is another (and pages are left): each
// opened in the page's orientation, drawn once per band - the band's
// slice of the page (the printer port's visible region) drawn, or the
// print form's full-page bounds for a format that asks for the full page
// - with the progress told as the bands go down the page, and closed.  A
// fax call the tool retries (-22005) prints the page again, three times
// at most; closing the printer likewise.  The transport's status
// becoming Canceling ends the page with kPR_ERR_UserCancel.  A PAP job
// printing a received fax (fIsPAP, left set only for one whose print
// type is receivedFax) reports its progress per fax page instead.
// ==> the job's error.
NewtonErr
TPrintView::PrintPages(RefArg transport)
{
	volatile NewtonErr result = noErr;
	volatile long pageNumber = 1;
	volatile long pageRetries = 0;
	volatile long closeRetries = 0;
	volatile Boolean keepGoing = true;
	RefVar statusValue;
	RefVar scratch;
	if (fIsPAP)
	{
		RefVar coverForm(GetVar(RSSYMcoverform));
		scratch = coverForm;
		RefVar printType(GetProtoVariable(scratch, RSSYMprinttype, nil));
		if (ISNIL(printType))
		{
			RefVar mainFormat(GetProtoVariable(scratch, RSSYMmainformat, nil));
			if (ISNIL(mainFormat)
			 || ISNIL(printType = GetProtoVariable(mainFormat, RSSYMprinttype, nil))
			 || !EQ(printType, RSSYMreceivedfax))
				fIsPAP = false;
		}
		else if (!EQ(printType, RSSYMreceivedfax))
			fIsPAP = false;
	}
	while (result == noErr && keepGoing && fPageCount != 0)
	{
		newton_try
		{
			SetPrintProgress(transport, pageNumber, 0);
			Boolean portrait = true;
			RefVar fields(GetVar(RSSYMfields));
			if (NOTNIL(fields))
			{
				RefVar bounds(GetProtoVariable(fields, RSSYMprinterpagebounds, nil));
				if (NOTNIL(bounds))
				{
					RefVar orientation(GetProtoVariable(bounds, RSSYMorientation, nil));
					if (NOTNIL(orientation))
						portrait = !EQ(orientation, RSSYMlandscape);
				}
			}
			fPrinter->SetPortraitOrientation(portrait);
			result = fPrinter->OpenPage();
			if (result == noErr)
			{
				newton_try
				{
					Dirty(nil);
					long height = portrait ? fPort->portRect.bottom - fPort->portRect.top
										   : fPort->portRect.right - fPort->portRect.left;
					long lastProgress = 0;
					Fixed pageHeight = ToFixed(height);
					do
					{
						statusValue = GetProtoVariable(transport, RSSYMstatus, nil);
						if (EQ(statusValue, RSSYMcanceling))
						{
							result = kPR_ERR_UserCancel;
							break;
						}
						Rect band = (*fPort->visRgn)->rgnBBox;
						long done = portrait ? (short) (band.bottom - fPort->portRect.top)
											 : (short) (band.right - fPort->portRect.left);
						Fixed fraction = FixedDivide(ToFixed(done), pageHeight);
						long percent = (short) ((FixedMultiply(ToFixed(100), fraction) + 0x8000) >> 16);
						long step = percent - lastProgress;
						if (step > 25)
						{
							if (fIsPAP)
							{
								RefStruct* progress = new RefStruct(transport);
								gTransportProgress = progress;
								if (progress == nil)
									Throw(exOutOfMemory, (void*) -10007, nil);
								SetFaxPrintProgress(transport, pageNumber, 0);
							}
							else
								SetPrintProgress(transport, pageNumber, lastProgress + step / 2);
						}
						if (fUseFullPage)
						{
							RefVar printForm(GetFrameSlot(fContext, RSSYMprintform));
							if (NOTNIL(printForm))
								fFullPage = GetView(printForm)->viewBounds;
							Draw(fFullPage, true);
						}
						else
						{
							InsetRect(&band, -1, -1);
							Draw(band, true);
						}
						if (step > 25)
						{
							lastProgress = percent;
							SetPrintProgress(transport, pageNumber, percent);
						}
					} while (fPrinter->RepeatPage());
					// (a cancelled page too)
					SetPrintProgress(transport, pageNumber, 100);
				}
				cleanup
				{
					NewtonErr err = fPrinter->ClosePage();
					if (result == noErr)
						result = err;
					pageNumber++;
					fPageCount--;
					err = fPrinter->FaxEndPage(fPageCount);
					if (result == noErr)
						result = err;
				}
				end_try;
				NewtonErr err = fPrinter->ClosePage();
				if (result == noErr)
					result = err;
				pageNumber++;
				fPageCount--;
				err = fPrinter->FaxEndPage(fPageCount);
				if (result == noErr)
					result = err;
			}
			if (result == kFaxErrRetry && ++pageRetries < 3)
			{
				pageNumber--;
				fPageCount++;
				result = noErr;
			}
			else if (fPageCount != 0 && result == noErr)
			{
				pageRetries = 0;
				// (the port's drawing procs out of the way while the script runs)
				QDProcs* procs = fPort->grafProcs;
				fPort->grafProcs = nil;
				keepGoing = NOTNIL(RunScript(RSSYMprintnextpagescript, RefVar(NILREF), false, nil));
				fPort->grafProcs = procs;
			}
		}
		cleanup
		{
			// (the ROM's handler: the printer closed and the progress record
			// dropped before the exception goes on)
			if (!(closeRetries != 0 && result == kCommErrNotConnected))
			{
				NewtonErr err = fPrinter->Close();
				if (result == noErr)
					result = err;
			}
			if (fIsPAP && gTransportProgress != nil)
			{
				delete gTransportProgress;
				gTransportProgress = nil;
			}
		}
		end_try;
		if (keepGoing && fPageCount != 0 && result == noErr)
			continue;
		if (!(closeRetries != 0 && result == kCommErrNotConnected))
		{
			NewtonErr err = fPrinter->Close();
			if (result == noErr)
			{
				if (err == kFaxErrRetry && ++closeRetries < 3)
				{
					pageNumber--;
					fPageCount++;
					result = noErr;
					keepGoing = true;
				}
				else
					result = err;
			}
		}
		if (fIsPAP && gTransportProgress != nil)
		{
			delete gTransportProgress;
			gTransportProgress = nil;
		}
	}
	return result;
}
