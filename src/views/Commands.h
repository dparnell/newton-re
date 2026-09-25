/*
	File:		views/Commands.h

	Contains:	Commands: the frames the view system passes around to say
				what happened - a clone of the ROM's protoCommand {id,
				result, parameter, receiver, frameParameter, params, undo}
				(MakeCommand and the accessors).  The receiver is a view's
				context (or 'application for the application); the
				parameter an integer (a pointer to a recognition unit, a
				view id, a delta - kNoParameter, 0x8000000, for none), the
				frameParameter any object, params an array of integers or
				objects by index.  A command is dispatched by the
				application (TApplication::DispatchCommand: the receiver's
				DoCommand) and answered in the result; one marked undo is
				on the undo stack.

				The command ids are the ROM's (TView::RealDoCommand
				0x00268d38 switches on them; the NTK's ae... constants):
				the names below are ours, from what each does - NOT the
				NTK's where the two are not known to agree.

	Reconstructed from the MP2x00 US ROM (0x00070424-0x000709e0,
	0x0003450c-0x00034694, 0x00268c24); each function cites its origin.
*/

#ifndef __COMMANDS_H
#define __COMMANDS_H

#ifndef __VIEW_H
#include "View.h"
#endif

// the command ids
enum
{
	aeClick					= 0x0b,		// viewClickScript(unit); the result 0 when it answered 'skip
	aeStroke				= 0x0c,		// viewStrokeScript(unit)
	aeScrub					= 0x0d,		// viewGestureScript(unit, kind) for these
	aeCaret					= 0x0f,
	aeLine					= 0x10,
	aeShape					= 0x11,		// a shape the recogniser made (TEditView::HandleShape)
	aeWord					= 0x12,		// viewWordScript(unit)
	aeGetContextUnits		= 0x14,		// the shapes on a page as shape units, for a new one to snap to (HandleGetContextUnits); the index parameter 0: only near the unit
	aeRawInk				= 0x15,		// viewRawInkScript(strokes)
	aeInkWord				= 0x18,		// viewInkWordScript(strokes)
	aeKeyUp					= 0x1f,		// the key events (TView::HandleKeyEvent; Keyboard.h): the parameter (modifiers << 25) | (key code << 16) | character
	aeKeyDown				= 0x20,
	aeKeyboardConnected		= 0x21,		// to the root: the parameter says whether a keyboard is connected
	aeKeyString				= 0x22,		// the frame parameter: the string typed
	aeKeyRepeat				= 0x23,
	aeAddChild				= 0x29,		// the frameParameter's view added under the receiver and shown
	aeDropChild				= 0x2a,		// the parameter (a view) hidden and removed
	aeHide					= 0x2b,
	aeShow					= 0x2c,		// (the parameter kNoModalCheck skips the modal check)
	aeScrollUp				= 0x2d,		// viewScrollUpScript
	aeScrollDown			= 0x2e,		// viewScrollDownScript
	aeGesture2f				= 0x2f,		// more viewGestureScript kinds
	aeRemoveAllHilites		= 0x30,
	aeTap					= 0x31,		// the click events (TEventRecognizer): viewGestureScript(unit, kind)
	aeDoubleTap				= 0x32,
	aeHiliteClick			= 0x34,		// the other click events (kHiliteClick, event 5): not the views' gesture script (NOT YET: who takes them)
	aeTapDrag				= 0x37,
	aeOverview				= 0x33,		// viewOverviewScript
	aePickItem				= 0x36,		// a picker's item picked (TPickView: the PickStuff as a binary frame parameter)
	aeAddData				= 0x3d,		// the frameParameter added to the receiver's soup (the undo of aeRemoveData)
	aeRemoveData			= 0x3f,		// the child of the parameter's id removed from the soup
	aeMoveData				= 0x40,		// the receiver moved by params[0], params[1] (undone by the reverse)
	aeScaleData				= 0x42,		// the receiver scaled from params[0..3] (undone by the reverse)
	aeReplaceText			= 0x46,		// a paragraph's text replaced (TParagraphView::HandleReplaceText): index parameters [offset, removed, inserted, styleOffset, postUndo, caretAfter, typed], the frame parameter the styles ({styles, tabs, correctInfo}), the text slot the string
	aeAddHilite				= 0x47,		// the frameParameter (a hilite, or a frame with one) appended to hilites
	aeRemoveHilite			= 0x48,
	aeToChildren			= 0x49,		// the command sent to every child
	aeToHilitedChildren		= 0x4b,		// ... to the hilited children
	aeMoveChild				= 0x4c,		// aeMoveData to the child of the parameter's id
	aeAppIdle				= 0x70,		// the application's
	aeRunScript				= 0x71,
	aeUndo					= 0x72
};

const Long kNoParameter = 0x8000000;		// (0x8000000: no parameter)
const Long kNoModalCheck = 0x420000;		// aeShow's parameter to skip the modal check

Ref		MakeCommand(ULong id, TResponder* receiver, Long parameter);			// ROM 0x00070424 MakeCommand__FUlP10TResponderl (Long: the ROM's word - a pointer fits)
TResponder*	CommandReceiver(RefArg cmd);										// ROM 0x000704e8 CommandReceiver__FRC6RefVar
long	CommandID(RefArg cmd);													// ROM 0x000707dc CommandID__FRC6RefVar
void	CommandSetID(RefArg cmd, ULong id);										// ROM 0x00070818 CommandSetID__FRC6RefVarUl
Long	CommandResult(RefArg cmd);												// ROM 0x00070858 CommandResult__FRC6RefVar (Long: a pointer fits, as for the parameter)
void	CommandSetResult(RefArg cmd, Long result);								// ROM 0x00070894 CommandSetResult__FRC6RefVarl
Long	CommandParameter(RefArg cmd);											// ROM 0x000708d4 CommandParameter__FRC6RefVar
void	CommandSetParameter(RefArg cmd, Long parameter);						// ROM 0x00070910 CommandSetParameter__FRC6RefVarl
Ref		CommandFrameParameter(RefArg cmd);										// ROM 0x00070950 CommandFrameParameter__FRC6RefVar
Ref		CommandText(RefArg cmd);												// ROM 0x0017a26c CommandText__FRC6RefVar (the text slot)
void	CommandSetText(RefArg cmd, RefArg text);								// ROM 0x0017a25c CommandSetText__FRC6RefVarT1
void	CommandSetFrameParameter(RefArg cmd, RefArg parameter);					// ROM 0x0007096c CommandSetFrameParameter__FRC6RefVarT1
Long	CommandIndexParameter(RefArg cmd, long index);							// ROM 0x00070624 CommandIndexParameter__FRC6RefVarl
void	CommandSetIndexParameter(RefArg cmd, long index, Long parameter);		// ROM 0x00070568 CommandSetIndexParameter__FRC6RefVarlT2
void	CommandSetIndexFrame(RefArg cmd, long index, RefArg parameter);			// ROM 0x000706b8 CommandSetIndexFrame__FRC6RefVarlT1
void	MarkUndoCommand(RefArg cmd);											// ROM 0x00070764 MarkUndoCommand__FRC6RefVar
Boolean	IsUndoCommand(RefArg cmd);												// ROM 0x000707a4 IsUndoCommand__FRC6RefVar
Ref		MakeRunScriptCommand(RefArg context, RefArg script, RefArg args);		// ROM 0x0003450c MakeRunScriptCommand__FRC6RefVarN21
Ref		MakeUndoCommand(RefArg receiver, RefArg message, RefArg args);			// ROM 0x000345cc MakeUndoCommand__FRC6RefVarN21
Ref		GetStrokeBundleFromCommand(RefArg cmd);									// ROM 0x00268c24 GetStrokeBundleFromCommand__FRC6RefVar

TResponder*	GetResponder(RefArg context, RefArg name);							// ROM 0x000af5b0 GetResponder__FRC6RefVarT1
TResponder*	FailGetResponder(RefArg context, RefArg name);						// ROM 0x000af604 FailGetResponder__FRC6RefVarT1

#endif	/* __COMMANDS_H */
