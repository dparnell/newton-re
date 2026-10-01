/*
	File:		print/DriverCallbacks.cpp

	Contains:	The callbacks a print driver has (the DDK's DriverCallbacks.h)
				for letting the rest of the Newton run while it waits: a job
				prints in the print view's world, which forked a new task to
				take its event loop over (TPrintView::RealDoCommand), and the
				two share the world's mutex.  PrReleaseControl lets go of it
				for a while - a time, or until PrRegainControl says the
				thing waited for has come (the fax driver waiting for its
				fax tool's replies) - and takes it back.

	Reconstructed from the MP2x00 US ROM (0x0019417c-0x001944b0); each
	function cites its origin.  (CallHandleProblem is with the problem
	slips in PrintView.cpp.)
*/

#include "print/Printer.h"
#include "print/FaxDriver.h"
#include "print/PSPrinter.h"
#include "print/HPPCL.h"
#include "utility/AppWorld.h"
#include "utility/PseudoSyncState.h"
#include "UserTasks.h"
#include <stdint.h>

// ROM 0x0019417c CoolSleep__FUl
// A sleep with the world's mutex let go, so the other tasks of the family
// run meanwhile.
static long
CoolSleep(TTimeout howLong)
{
	long err = ((TForkWorld*) GetGlobals())->ReleaseMutex();
	if (err != noErr)
		return err;
	Sleep(howLong);
	return ((TForkWorld*) GetGlobals())->AcquireMutex();
}


// ROM 0x001941b0 PrintPatchpoint__Fv
// A place the ROM left for patches; it does nothing.
void
PrintPatchpoint(void)
{ }


// ROM 0x00194448 PrReleaseControl__FUlP8TPrinter
// The main task let run: for the time asked, or - a time of all ones, the
// ROM's `cmn r0,#1` - until PrRegainControl.  DEVIATION: the ROM leaves the printer's pointer at the
// waiting state after the wait, a state on the stack that has gone by then
// (a PrRegainControl after it sends to a port that no longer exists, which
// the device shrugs off); the host clears it, as calling into a dead
// object is not something it can do harmlessly.
void
PrReleaseControl(TTimeout howLong, TPrinter* printer)
{
	if ((uint32_t) howLong != 0xFFFFFFFFu)
	{
		CoolSleep(howLong);
		return;
	}
	TPseudoSyncState state;
	if (state.Init() == noErr)
	{
		printer->fBlocked = &state;
		state.Block(0);
		printer->fBlocked = nil;
	}
}


// ROM 0x001944a4 PrRegainControl__FP8TPrinter
// The waiting PrReleaseControl let go on.
void
PrRegainControl(TPrinter* printer)
{
	if (printer->fBlocked != nil)
		printer->fBlocked->Unblock();
}


// ROM 0x00192f08 InitPrintDrivers__Fv
// The imaging engines and the drivers built into the ROM registered, and
// the 'prnt part handler (TPrDriverPart) made for drivers in packages.
// NOT YET RECONSTRUCTED: TPSPAPDriver (PostScript over AppleTalk),
// TLaserWriterLSDriver, TSWGroupDriver and the part handler - the owner's
// decision: the PostScript and HP PCL printers only.
void
InitPrintDrivers(void)
{
	TDotPrinter::ClassInfo()->Register();
	TPSPrinter::ClassInfo()->Register();
	TFaxDriver::ClassInfo()->Register();
	ThpPCL::ClassInfo()->Register();
}
