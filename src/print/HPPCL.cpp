/*
	File:		print/HPPCL.cpp

	Contains:	ThpPCL, the ROM's driver for HP's PCL printers
				(print/HPPCL.h).

	Reconstructed from the MP2x00 US ROM (0x002e56c0-0x002e6158); each
	function cites its origin.
*/

#include "print/HPPCL.h"
#include "Endpoint.h"
#include "OptionArray.h"
#include "SerialOptions.h"
#include "CommOptions.h"
#include "CommErrors.h"
#include "OSErrors.h"
#include "Frames.h"
#include "RSSymbols.h"
#include "NewtonMemory.h"
#include "Draw.h"
#include <string.h>
#include <stdio.h>

void	PackBits(char** src, char** dst, long count);		// qd: ROM 0x002aeed0 PackBits__FPPcT1l

PrinterServiceHook	gPrinterServiceHook = nil;

// how long a write to the printer may take (the ROM's 0x34bc000), how long a
// problem slip waits before the printer is asked again (0x708000), and how
// long the job waits for the last bytes to drain before it closes the
// connection (0x1194000)
static const TTimeout	kPCLSendTimeout = (TTimeout) 0x34bc000;
static const TTimeout	kPCLProblemRetry = (TTimeout) 0x708000;
static const TTimeout	kPCLDrainTime = (TTimeout) 0x1194000;



PROTOCOL_IMPL_SOURCE_MACRO(ThpPCL)		// ROM 0x002e56c0 Sizeof__6ThpPCLSFv
PROTOCOL_CLASSINFO(ThpPCL, "TDotPrinterDriver", "", 0x20000, 0, nil)	// ROM 0x00388484 ClassInfo__6ThpPCLSFv


// ROM 0x002e56c8 Delete__6ThpPCLFv
void
ThpPCL::Delete()
{
	if (fEndpoint != nil)
		fEndpoint->Delete();
}


// ROM 0x002e56d8 GetBandPrefs__6ThpPCLFP15DotPrinterPrefs
// Bands of 50 rows (25 at the least), sent as they are made, with the box
// round their black.
void
ThpPCL::GetBandPrefs(DotPrinterPrefs* prefs)
{
	prefs->minBand = 25;
	prefs->optimumBand = 50;
	prefs->asyncBanding = false;
	prefs->wantMinBounds = true;
}


// ROM 0x002e56fc ContinueIO__6ThpPCLFv
Boolean
ThpPCL::ContinueIO()
{
	return !(fError == kPR_ERR_PrinterError || fError == kPR_ERR_LostContact || fProblemNotFixed);
}


// ROM 0x002e5730 DoHandleProblem__6ThpPCLFv
// The problem slip shown until the printer can print again or the user
// cancels it; out of paper and an open door offer a button to say it is
// fixed.  (Once offered the button stays for every slip after.)
PrProblemResolution
ThpPCL::DoHandleProblem()
{
	Boolean fixedButton = false;
	PrProblemResolution resolution;
	do
	{
		NewtonErr problem = fError;
		if (problem == kPR_PROB_NoPaper || problem == kPR_PROB_DoorOpen)
			fixedButton = true;
		resolution = CallHandleProblem(fConnect, fPrinter, problem, kPCLProblemRetry, fixedButton);
		if (resolution == kPrProblemNotFixed)
			fProblemNotFixed = true;
		else
			GetStatus();
	} while (fError != noErr && !fProblemNotFixed);
	return resolution;
}


// ROM 0x002e57c8 ErrorIsProblem__6ThpPCLFv
Boolean
ThpPCL::ErrorIsProblem()
{
	return fError >= kPR_ERR_MINPROBLEM && fError <= kPR_ERR_MAXPROBLEM;
}


// ROM 0x002e57f4 GetStatus__6ThpPCLFv
// The printer has nothing to say (the connection is one-way): while the
// job can go on, all is well.
void
ThpPCL::GetStatus()
{
	if (ContinueIO())
		fError = noErr;
}


// ROM 0x002e5818 InitializeConnection__6ThpPCLFv
// The endpoint to the printer made and opened: the serial port at 57600
// bps with hardware flow control on the way out, or IrDA to an IrLPT
// printer.  ==> whether it opened.
// HOST EXTENSION: a printer model gPrinterServiceHook knows is reached
// through its service instead (print/host/HostIPP.h).
Boolean
ThpPCL::InitializeConnection()
{
	TOptionArray options;
	Boolean opened = false;
	fError = options.Init();
	if (fError == noErr)
	{
		ULong hostService = (gPrinterServiceHook != nil) ? gPrinterServiceHook(fPrModel) : 0;
		if (hostService != 0)
		{
			TOption service(kOptionType);
			service.SetAsService(hostService);
			fError = options.InsertOptionAt(options.GetArrayCount(), &service);
			if (fError != noErr)
				return false;
		}
		else if (fPrModel == 0)
		{
			TOption service(kOptionType);
			service.SetAsService('aser');
			fError = options.InsertOptionAt(options.GetArrayCount(), &service);
			if (fError != noErr)
				return false;
			TCMOSerialIOParms ioParms;
			ioParms.fSpeed = 57600;
			fError = options.InsertOptionAt(options.GetArrayCount(), &ioParms);
			if (fError == noErr)
			{
				TCMOOutputFlowControlParms flowControl;
				flowControl.useHardFlowControl = true;
				flowControl.SetOpCode(opSetRequired);
				fError = options.InsertOptionAt(options.GetArrayCount(), &flowControl);
			}
		}
		else
		{
			TOption service(kOptionType);
			service.SetAsService('irda');
			fError = options.InsertOptionAt(options.GetArrayCount(), &service);
			if (fError != noErr)
				return false;
			TCMOIrDAConnectionInfo info;
			info.fPeerNameLength = 5;
			memcpy(info.fClassNames + 4, "IrLPT", 6);
			fError = options.InsertOptionAt(options.GetArrayCount(), &info);
		}
		if (fError == noErr)
			fError = CMGetEndpoint(&options, &fEndpoint, false);
		if (fError == noErr && fEndpoint != nil)
		{
			fError = fEndpoint->EasyOpen(0);
			opened = (fError == noErr);
		}
	}
	return opened;
}


// ROM 0x002e5a0c InitializeFields__6ThpPCLFv
// The job's state cleared, the paper looked at and the printer frame's
// prModel read.
void
ThpPCL::InitializeFields()
{
	fEndpoint = nil;
	fError = noErr;
	fProblemNotFixed = false;
	fCancelled = false;
	fPackBuffer = nil;
	fA4 = EQRef(fConnect->fPaperSize, RSSYMa4);
	RefVar connectInfo(fConnect->fConnectInfo);
	RefVar printer(GetFrameSlotRef(connectInfo, RefVar(Intern("printer"))));
	RefVar model(GetFrameSlotRef(printer, RefVar(Intern("prmodel"))));
	fPrModel = RINT(model);
}


// ROM 0x002e5afc PrinterCanPrint__6ThpPCLFv
// Whether the job can go on - after the user has put a problem right.
Boolean
ThpPCL::PrinterCanPrint()
{
	if (!ContinueIO())
		return false;
	GetStatus();
	if (fError == noErr)
		return true;
	if (ErrorIsProblem() && DoHandleProblem() == kPrProblemFixed)
		return true;
	return false;
}


// ROM 0x002e5b64 ReleaseConnection__6ThpPCLFv
NewtonErr
ThpPCL::ReleaseConnection()
{
	NewtonErr err = noErr;
	if (fEndpoint != nil)
		err = fEndpoint->EasyClose();
	return err;
}


// ROM 0x002e5b90 Open__6ThpPCLFv
// The connection opened and the row buffer made: a row of the page is
// 2400 dots (letter) or 2336 (A4).
NewtonErr
ThpPCL::Open()
{
	InitializeFields();
	if (!InitializeConnection())
		fError = kPR_ERR_NewtonError;
	else
	{
		if (fError == noErr)
		{
			fRowBytes = fA4 ? 0x124 : 300;
			fPackBuffer = (char*) NewPtr(fRowBytes + 0x10);
		}
		if (fError != noErr)
		{
			if (fPackBuffer != nil)
				DisposPtr(fPackBuffer);
			ReleaseConnection();
		}
	}
	return fError;
}


// ROM 0x002e5c1c SendCommand__6ThpPCLFPc
void
ThpPCL::SendCommand(char* command)
{
	SendData(command, strlen(command));
}


// ROM 0x002e5c4c SendData__6ThpPCLFPcl
// Bytes sent to the printer: a write aborted is the user's cancel, one
// that timed out the printer lost, anything else a Newton error.
void
ThpPCL::SendData(char* data, long size)
{
	Size count = size;
	NewtonErr err = fEndpoint->Snd((UByte*) data, count, 0, kPCLSendTimeout);
	if (err == kError_Call_Aborted)
		fError = kPR_ERR_UserCancel;
	else if (err == kError_Message_Timed_Out)
		fError = kPR_ERR_LostContact;
	else if (err != noErr)
		fError = kPR_ERR_NewtonError;
}


// ROM 0x002e5cc4 FaxEndPage__6ThpPCLFl
NewtonErr
ThpPCL::FaxEndPage(long /*pageCount*/)
{
	return noErr;
}


// ROM 0x002e5ccc Close__6ThpPCLFv
// The connection closed - after a while for the last bytes to go - and
// the row buffer given back.
NewtonErr
ThpPCL::Close()
{
	if (ContinueIO())
		PrReleaseControl(kPCLDrainTime, fPrinter);
	NewtonErr err = ReleaseConnection();
	if (fPackBuffer != nil)
		DisposPtr(fPackBuffer);
	if (fError == noErr)
		fError = err;
	return fError;
}


// ROM 0x002e5d20 OpenPage__6ThpPCLFv
// A page begun: the printer reset, 300 dots an inch, the raster's height
// and width and raster graphics started at the left margin.
NewtonErr
ThpPCL::OpenPage()
{
	PrinterCanPrint();
	if (fError == noErr)
	{
		SendCommand((char*) "\033%-12345X");
		SendCommand((char*) "\033E");
		SendCommand((char*) "\033*t300R");
		long width;
		if (!fA4)
		{
			sprintf(fCommand, "\033*r%dT", 0xc4e);
			SendCommand(fCommand);
			width = 0x960;
		}
		else
		{
			sprintf(fCommand, "\033*r%dT", 0xcfc);
			SendCommand(fCommand);
			width = 0x91b;
		}
		sprintf(fCommand, "\033*r%dS", (int) width);
		SendCommand(fCommand);
		SendCommand((char*) "\033*r0A");
		fBlankRows = 0;
		if (fPackBuffer == nil)
			sprintf(fRowCommand, "\033*b%dW", (int) fRowBytes);
	}
	return fError;
}


// ROM 0x002e5e50 ClosePage__6ThpPCLFv
// Raster graphics ended and the printer reset, which ejects the page.
NewtonErr
ThpPCL::ClosePage()
{
	SendCommand((char*) "\033*rC");
	SendCommand((char*) "\033E");
	SendCommand((char*) "\033%-12345X");
	return fError;
}


// ROM 0x002e5ea0 ImageBand__6ThpPCLFP8PixelMapPC4Rect
// A band: its white rows counted to be skipped, the rows with black sent
// (packed, when there is a buffer to pack them in).  A band with no black
// at all (minRect's bottom nought) is all white rows.  After a cancel the
// rest of the page is skipped.
NewtonErr
ThpPCL::ImageBand(PixelMap* band, const Rect* minRect)
{
	if (!ContinueIO())
		return fError;
	long bandRows = band->bounds.bottom - band->bounds.top;
	if (fCancelled)
	{
		sprintf(fCommand, "\033*b%dY", (int) (fBlankRows + bandRows));
		SendCommand(fCommand);
		fBlankRows = 0;
		return fError;
	}
	if (minRect->bottom == 0)
	{
		fBlankRows += bandRows;
		return fError;
	}
	long above = minRect->top - band->bounds.top;
	long skip = fBlankRows + above;
	if (skip != 0)
	{
		sprintf(fCommand, "\033*b%dY", (int) skip);
		SendCommand(fCommand);
		fBlankRows = 0;
	}
	long rows = minRect->bottom - minRect->top;
	char* row = band->baseAddr + band->rowBytes * above;
	while (rows-- != 0 && fError == noErr)
	{
		if (fPackBuffer != nil)
		{
			char* packed = fPackBuffer;
			// (PackBits moves row on by fRowBytes)
			PackBits(&row, &packed, fRowBytes);
			long size = packed - fPackBuffer;
			sprintf(fRowCommand, "\033*b2m%dW", (int) size);
			SendCommand(fRowCommand);
			SendData(fPackBuffer, size);
		}
		else
		{
			SendCommand(fRowCommand);
			SendData(row, band->rowBytes);
			row += band->rowBytes;
		}
	}
	if (minRect->bottom != 0)
	{
		long below = band->bounds.bottom - minRect->bottom;
		if (below != 0)
			fBlankRows += below;
	}
	PrinterCanPrint();
	return fError;
}


// ROM 0x002e609c CancelJob__6ThpPCLFUc
void
ThpPCL::CancelJob(Boolean asyncCancel)
{
	if (asyncCancel)
		fCancelled = true;
	else
		fError = kPR_ERR_UserCancel;
}


// ROM 0x002e60b8 IsProblemResolved__6ThpPCLFv
// Out of paper or the door open is put right when the user says so;
// anything else when the error has changed.
PrProblemResolution
ThpPCL::IsProblemResolved()
{
	NewtonErr was = fError;
	if (was == kPR_PROB_NoPaper || was == kPR_PROB_DoorOpen)
		return (PrProblemResolution) 1;
	GetStatus();
	if (fError != was)
		return (PrProblemResolution) 0;
	return (PrProblemResolution) 1;
}


// ROM 0x002e6104 GetPageInfo__6ThpPCLFP10PrPageInfo
// 300 dots an inch: letter 2400 x 3150 dots, A4 2331 x 3324.
void
ThpPCL::GetPageInfo(PrPageInfo* info)
{
	info->printerDPI.y = ToFixed(300);
	info->printerDPI.x = ToFixed(300);
	if (fA4)
	{
		info->printerPageSize.h = 0x091b;
		info->printerPageSize.v = 0x0cfc;
	}
	else
	{
		info->printerPageSize.h = 0x0960;
		info->printerPageSize.v = 0x0c4e;
	}
}
