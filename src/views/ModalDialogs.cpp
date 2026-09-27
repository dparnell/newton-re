/*
	File:		views/ModalDialogs.cpp

	Contains:	The view side of the modal dialogs (RootView.h):
				SetModalView marks a view modal and confines recognition
				to it, RealExitModalDialog undoes it when the dialog is
				closed or its view goes.

				A dialog is opened by one of two natives
				(newt/ModalDialogNatives.cpp): :FilterDialog() leaves its
				opener running and puts TRUE in the context's modalState;
				:ModalDialog() stops its opener on a TPseudoSyncState whose
				address is the modalState until the dialog is exited.
				gModalCount says how many are up; while any is, a view
				asked to show waits for them (ModalSafeShow) and a command
				not marked kNoModalCheck is refused.

	Reconstructed from the MP2x00 US ROM (0x0030de2c, 0x0030e14c); each
	function cites its origin.
*/

#include "RootView.h"
#include "View.h"
#include "ViewFlags.h"
#include "Recognizer.h"
#include "PseudoSyncState.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "RSSymbols.h"


// ROM 0x0030de2c SetModalView__FP5TView
// The view marked modal (its viewJustify's private bit) and recognition
// confined to its bounds - moved there when it already was modal.
void
SetModalView(TView* view)
{
	if (view->fViewJustify & vjIsModal)
		gRecognition.DisableModalRecognition();
	view->fViewJustify |= vjIsModal;
	Rect bounds;
	view->OuterBounds(&bounds);
	gRecognition.EnableModalRecognition(bounds);
}


// ROM 0x0030e14c RealExitModalDialog__FP5TView
// A modal view's dialog exited: from a view inside the dialog the mark is
// taken off and the root's child that holds it is the dialog.  When its
// context has a modalState, the mark comes off, recognition is let go -
// or confined to the dialog now frontmost, when there is another - the
// views that waited are shown once none is left, a ModalDialog's opener
// is unblocked and the dialog is sent UnstuffModalCommandKeys.
void
RealExitModalDialog(TView* view)
{
	if (view == nil || (view->fViewJustify & vjIsModal) == 0)
		return;
	if (view->fParent != gRootView)
	{
		view->fViewJustify &= ~vjIsModal;
		do
			view = view->fParent;
		while (view->fParent != gRootView);
	}
	RefVar state(GetProtoVariable(RefVar(view->fContext), RSSYMmodalstate, nil));
	if (ISNIL(state))
		return;
	view->fViewJustify &= ~vjIsModal;
	gRecognition.DisableModalRecognition();
	gModalCount--;
	if (gModalCount < 1)
		ModalSafeShowRelease();
	else
	{
		TView* front = gRootView->GetFrontmostModalView();
		if (front != nil)
		{
			Rect bounds = front->viewBounds;
			gRecognition.EnableModalRecognition(bounds);
		}
	}
	// TRUE for a FilterDialog; a ModalDialog's is its opener's state
	Ref ref = state;
	if (!((ref & 3) == 2 && ((ref >> 2) & 3) == 2))
		((TPseudoSyncState*) RefToAddress(ref))->Unblock();
	NSSend(RefVar(view->fContext), RSSYMunstuffmodalcommandkeys);
}
