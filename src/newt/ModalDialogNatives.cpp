/*
	File:		newt/ModalDialogNatives.cpp

	Contains:	The modal dialogs and script forks as a script sees them.

				view:FilterDialog() opens the view modally and returns at
				once: its context's modalState is TRUE, and the pen and the
				keys go only to it until view:ExitModalDialog() (or its
				closing) lets them go again - AsyncConfirm is made of it.

				view:ModalDialog() does the same and then *waits*: the
				script that asked stops on a TPseudoSyncState, which forks
				the newt world so that a new task carries on running the
				event loop - drawing the dialog, taking the taps - while the
				old one is blocked.  The dialog's exit unblocks it; its
				script runs to its end, and when it gets back to its event
				loop the loop ends (the fork has it now) and its task goes.
				ModalConfirm is made of it.  What was being recognised when
				the dialog opened is put aside for the fork and put back
				afterwards (TRecognitionManager::SaveRecognitionState).

				ForkScript(fn, args) is the same trick without a dialog: the
				world forks, the function runs in the old task, and the
				fork keeps the machine responsive meanwhile; YieldToFork
				lets the other run.

				The view side (SetModalView, RealExitModalDialog) is
				views/ModalDialogs.cpp.

	Reconstructed from the MP2x00 US ROM (0x0030de88-0x0030e3ac); each
	function cites its origin.
*/

#include "NewtWorld.h"
#include "RootView.h"
#include "View.h"
#include "Recognizer.h"
#include "PseudoSyncState.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "NewtonExceptions.h"
#include "RSSymbols.h"


// ROM 0x0030de88 FModalDialog
// view:ModalDialog(): the view opened modally - a view inside a dialog
// makes the root's child that holds it the dialog - and the caller held
// until the dialog is exited.  ==> TRUE; NIL when the wait could not be
// set up.
Ref
FModalDialog(RefArg rcvr)
{
	NSSend(rcvr, RSSYMstuffmodalcommandkeys);
	if (ISNIL(RealOpenX(rcvr, true)))
		return TRUEREF;
	gRootView->Update(nil);
	((TNewtWorld*) GetGlobals())->fHandler->SetWakeupTime(0);
	TPseudoSyncState state;
	if (state.Init() != noErr)
		return NILREF;
	RefVar dialog(rcvr);
	Boolean nested = gModalCount != 0;
	gModalCount++;
	if (nested)
		gRecognition.DisableModalRecognition();
	TView* view = GetView(dialog);
	if (view->fParent != gRootView)
	{
		view->fViewJustify |= vjIsModal;
		do
			view = view->fParent;
		while (view->fParent != gRootView);
		dialog = view->fContext;
	}
	SetModalView(view);
	SetFrameSlot(dialog, RSSYMmodalstate, RefVar(AddressToRef(&state)));
	UChar failed = false;
	RecognitionState* saved = gRecognition.SaveRecognitionState(&failed);
	if (!failed)
		state.Block(0);
	gRecognition.RestoreRecognitionState(saved);
	SetFrameSlot(dialog, RSSYMmodalstate, RefVar(NILREF));
	return TRUEREF;
}


// ROM 0x0030e054 FFilterDialog
// view:FilterDialog(): the view opened modally, the caller going on at
// once.  ==> TRUE.
Ref
FFilterDialog(RefArg rcvr)
{
	NSSend(rcvr, RSSYMstuffmodalcommandkeys);
	if (NOTNIL(RealOpenX(rcvr, true)))
	{
		Boolean nested = gModalCount != 0;
		gModalCount++;
		if (nested)
			gRecognition.DisableModalRecognition();
		SetModalView(GetView(rcvr));
		gRootView->Update(nil);
		((TNewtWorld*) GetGlobals())->fHandler->SetWakeupTime(0);
		SetFrameSlot(rcvr, RSSYMmodalstate, RefVar(TRUEREF));
	}
	return TRUEREF;
}


// ROM 0x0030e284 FExitModalDialog
Ref
FExitModalDialog(RefArg rcvr)
{
	RealExitModalDialog(GetView(rcvr));
	return TRUEREF;
}


// ROM 0x0030e2a0 FForkScript
// ForkScript(fn, args): the world forked (an error if it cannot be) and
// the function called in this task, the fork running the event loop
// meanwhile; recognition is put aside for it as for a modal dialog, and
// put back whether or not the function throws.  ==> its result.
Ref
FForkScript(RefArg /*rcvr*/, RefArg fn, RefArg args)
{
	if (((TForkWorld*) GetGlobals())->Fork(nil) != noErr)
		ThrowMsg("couldn't fork it over");
	RefVar result(NILREF);
	UChar failed = false;
	RecognitionState* saved = gRecognition.SaveRecognitionState(&failed);
	newton_try
	{
		if (!failed)
			result = DoBlock(fn, args);
	}
	newton_catch_all
	{
		gRecognition.RestoreRecognitionState(saved);
		rethrow;
	}
	end_try;
	gRecognition.RestoreRecognitionState(saved);
	return result;
}


// ROM 0x0030e390 FYieldToFork
Ref
FYieldToFork(RefArg /*rcvr*/)
{
	((TForkWorld*) GetGlobals())->Yield();
	return NILREF;
}


void
RegisterModalDialogNatives(void)
{
	RegisterNativeFunction("FModalDialog", (void*) FModalDialog, 0);
	RegisterNativeFunction("FFilterDialog", (void*) FFilterDialog, 0);
	RegisterNativeFunction("FExitModalDialog", (void*) FExitModalDialog, 0);
	RegisterNativeFunction("FForkScript", (void*) FForkScript, 2);
	RegisterNativeFunction("FYieldToFork", (void*) FYieldToFork, 0);
}
