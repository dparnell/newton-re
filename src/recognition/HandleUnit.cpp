/*
	File:		recognition/HandleUnit.cpp

	Contains:	The unit handler: the units the controller (or, on the
				host, the stroke world) hands over are given to their
				recognisers and the commands they answer are posted to the
				views under them.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Recognizer.h"
#include "Controller.h"
#include "UnitPublic.h"
#include "StrokeCentral.h"
#include "Commands.h"
#include "RootView.h"
#include "Application.h"
#include "NewtonExceptions.h"
#include "Frames.h"
#include "REPTranslators.h"
#include "Interpreter.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

Boolean	gInhibitPopup = false;					// ROM 0x0c101948 gInhibitPopup
static TUnit*	gUnitBeingHandled = nil;		// (the ROM's word at 0x0c103f8c) the unit HandleUnitList is on (nil once it is done)


// ROM 0x00036a3c SafeExceptionNotify__FP9Exception
// The exception shown to the user (ExceptionNotify) with a handler round
// it, so that a failure in the showing is dropped.
//
// NOT YET RECONSTRUCTED: ExceptionNotify, which puts the notify slip up.
// The host prints it instead, and prints what it is carrying with it: one
// of the object system's exceptions holds either a frame saying what went
// wrong (the names that end in type.ref.frame) or an error code, and
// without either the name alone says almost nothing - every mistake a ROM
// script makes arrives here as evt.ex.fr.intrp.
void
SafeExceptionNotify(Exception* exception)
{
	fprintf(stderr, "exception in a unit handler: %s", exception->name);
	if (Subexception(exception->name, (ExceptionName) "type.ref"))
	{
		fprintf(stderr, "\n");
		if (gREPout != nil && exception->data != nil)
		{
			PrintObject(*(RefStruct*) exception->data, 0);
			fprintf(stderr, "\n");
		}
	}
	else if (Subexception(exception->name, (ExceptionName) "evt.ex.msg") && exception->data != nil)
		fprintf(stderr, ": %s\n", (const char*) exception->data);	// ThrowMsg carries the words
	else
		fprintf(stderr, " (%ld)\n", (long) (Long) exception->data);
	if (getenv("NEWTON_TRACE_EXCEPTIONS") != nil && gREPout != nil)
		StackTrace();
	fflush(stderr);
}


// ROM 0x0019d6a8 HandleUnit__FP6TArray
// HandleUnitList under an exception handler: an exception ends the
// handling and is reported, not thrown on.
long
HandleUnit(TArray* units)
{
	long handled = 0;
	newton_try
	{
		handled = HandleUnitList(units);
	}
	newton_catch_all
	{
		SafeExceptionNotify(CurrentException());
	}
	end_try;
	return handled;
}


// ROM 0x0019d72c HandleUnitList__FP6TArray
// Each unit: its recogniser's command is taken (and dropped, as a click
// on a clicks-only area is noted, when the recogniser is arbitrated and
// the click came within half a second of a stroke that went to the word
// recogniser - a tap after writing); the click ignoring is checked
// against the unit's start; the command is dropped too when the unit's
// bounds are outside the modal bounds, or when a click lands on the
// caret (DoCaretClick handles it).  Unless the unit started before the
// last flush, the recogniser's HandleUnit answers the command to post
// and PostAndDoCommand dispatches it (an exception in the dispatch is
// reported); the application is told a new undo batch begins.  Then,
// when the handling did not replace the unit being handled: a click on a
// clicks-only area whose stroke is still current is noted too; a command
// nobody handled (other than aeTap) with nothing noted leaves the unit
// as it is; otherwise the recogniser's flag 1 becomes the after-writing
// state (when a command was posted), the unit's ink is taken off and
// invalidated and its strokes claimed in the controller (not for a click
// whose stroke is already over), it counts as handled, and a noted click
// forgets the click view and triggers recognition.  ==> whether any unit
// was handled.
long
HandleUnitList(TArray* units)
{
	long anyHandled = 0;
	gRecognition.fClickSwallowed = false;
	ULong count = units->Count();
	for (ULong i = 0; i < count; i++)
	{
		TUnit* unit = *(TUnit**) units->GetEntry(i);
		ULong type = unit->fType;
		gUnitBeingHandled = unit;
		TRecognizer* recognizer = gRecognition.fRecognizers->FindRecognizer(type);
		ULong command = recognizer->Command();
		ULong startTime = unit->fStartTime;
		gRecognition.SetNextClick(startTime);
		Boolean afterWriting = gRecognition.fAfterWriting;
		if (recognizer->TestFlags(kRecognizerArbitrated) && afterWriting)
		{
			TStroke* stroke = unit->GetStroke(0);
			if (stroke->fDownTime < stroke->fPrevUpTime + 30)
			{
				command = 0;
				if (ClicksOnlyArea(unit))
					gRecognition.fClickSwallowed = true;
			}
		}
		TUnitPublic pub(unit, 0);
		ULong mask = pub.RequiredMask();
		long result = 0;
		Rect bounds;
		pub.Bounds(&bounds);
		if (!gRecognition.ModalRecognitionOK(bounds) || (command == aeClick && gRootView->DoCaretClick(&pub)))
		{
			command = 0;
			gRecognition.fClickSwallowed = true;
		}
		ULong posted = 0;
		if (!gStrokeWorld.BeforeLastFlush(startTime))
		{
			if (command != 0)
			{
				posted = recognizer->HandleUnit(&pub);
				if (posted != 0)
				{
					newton_try
					{
						result = PostAndDoCommand(posted, &pub, mask);
					}
					newton_catch_all
					{
						SafeExceptionNotify(CurrentException());
					}
					end_try;
					gApplication->fNewUndoBatch = true;
				}
			}
		}
		if (gUnitBeingHandled == unit)
		{
			Boolean clickDone = posted == aeClick && gStrokeWorld.CurrentStroke() == nil;
			if (posted == aeClick && !clickDone && ClicksOnlyArea(unit))
				gRecognition.fClickSwallowed = true;
			Boolean noteWriting;
			if (result == 0)
			{
				if (posted == aeTap)
					noteWriting = true;
				else if (!gRecognition.fClickSwallowed)
					goto next;
				else
					noteWriting = posted != 0;
			}
			else
				noteWriting = posted != 0;
			if (noteWriting)
			{
				recognizer = gRecognition.fRecognizers->FindRecognizer(type);
				gRecognition.fAfterWriting = recognizer->TestFlags(1);
			}
			if (!clickDone)
			{
				pub.Cleanup();
				pub.Invalidate();
				gController->MarkUnits(unit, kClaimedUnit);
			}
			anyHandled = 1;
			if (gRecognition.fClickSwallowed)
			{
				gRecognition.SaveClickView(nil);
				gController->TriggerRecognition();
			}
		}
next:
		gUnitBeingHandled = nil;
	}
	gInhibitPopup = false;
	return anyHandled;
}


// ROM 0x0019dccc PostAndDoCommand__FUlP11TUnitPublicT1
// The view under the unit found with the mask (none: nothing posted); a
// click outside the popup and its parents closes the popup instead (and
// forgets the click view) - ==> 1.  Otherwise a command to the view
// with the unit as its parameter - the raw ink and ink word commands
// carry the word's strokes as the frame parameter, the ink word's with
// its start and end times - dispatched by the application.  ==> the
// command's result.
long
PostAndDoCommand(ULong command, TUnitPublic* unit, ULong mask)
{
	TView* view = unit->FindView(mask);
	if (view == nil)
	{
		gInhibitPopup = false;
		return 0;
	}
	if (command == aeClick)
	{
		TView* popup = gRootView->fPopup;
		Boolean closed = false;
		if (popup != nil && view != popup)
		{
			TView* parent = view->fParent;
			while (parent != popup && parent != gRootView)
				parent = parent->fParent;
			if (parent == gRootView)
			{
				gRootView->SetPopup(nil, true);
				closed = true;
				gRecognition.SaveClickView(nil);
			}
		}
		if (closed)
		{
			gInhibitPopup = false;
			return 1;
		}
	}
	RefVar cmd(MakeCommand(command, view, (Long) unit));
	if (command == aeRawInk)
		CommandSetFrameParameter(cmd, RefVar(unit->Strokes()));
	else if (command == aeInkWord)
	{
		RefVar strokes(unit->Strokes());
		SetFrameSlot(strokes, RSSYMstarttime, RefVar(MAKEINT(unit->StartTime())));
		SetFrameSlot(strokes, RSSYMendtime, RefVar(MAKEINT(unit->EndTime())));
		CommandSetFrameParameter(cmd, strokes);
	}
	long result = gApplication->DispatchCommand(cmd);
	gInhibitPopup = false;
	return result;
}


// ROM 0x0019dbd8 HandleGetContextUnits__FP5TUnitl
// aeGetContextUnits (0x14) to the view under the unit, with the argument
// as its first index parameter: the routine the shape domain asks for the
// shapes on the page (SetContextUnitRoutine).  ==> the unit list the view
// answers, nil for no view.  An evt.ex exception is swallowed and answers
// nil; anything else goes on up.
TUnitList*
HandleGetContextUnits(TUnit* unit, long arg)
{
	TUnitList* result = nil;
	newton_try
	{
		TUnitPublic pub(unit, 0);
		TView* view = pub.FindView(vAnythingAllowed);
		if (view != nil)
		{
			RefVar cmd(MakeCommand(0x14, view, (Long) &pub));
			CommandSetIndexParameter(cmd, 0, arg);
			result = (TUnitList*) gApplication->DispatchCommand(cmd);
		}
	}
	newton_catch(exRootException)
	{ }
	end_try;
	return result;
}


// ROM 0x0019db84 UpdateStroke__FP5TUnit
// The unit's stroke's ink taken off and the root view updated.
void
UpdateStroke(TUnit* unit)
{
	TUnitPublic pub(unit, 0);
	pub.Stroke()->InkOff(true);
	gRootView->Update(nil);
}


// ROM 0x0019dad0 HandleExpiredStroke__FP5TUnit
// A stroke no recogniser took: the after-writing state set; the stroke
// goes to the stroke world's expired strokes (to be grouped into ink:
// NOT YET RECONSTRUCTED: StrokeCentral::AddExpiredStroke), or, while the
// arbiter is arbitrating for the whole screen (NOT YET: gArbiter), its
// ink is just taken off.
void
HandleExpiredStroke(TUnit* unit)
{
	newton_try
	{
		gRecognition.fAfterWriting = true;
		UpdateStroke(unit);
	}
	newton_catch_all
	{
		SafeExceptionNotify(CurrentException());
	}
	end_try;
}
