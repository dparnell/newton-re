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
				0x00266e00 switches on them; the NTK's ae... constants):
				the names below are ours, from what each does - NOT the
				NTK's where the two are not known to agree.

	Reconstructed from the MP2100 D ROM (0x00070dc4-0x00071380,
	0x000345bc-0x00034744, 0x00266cec); each function cites its origin.
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
	aeWord					= 0x12,		// viewWordScript(unit)
	aeRawInk				= 0x15,		// viewRawInkScript(strokes)
	aeInkWord				= 0x18,		// viewInkWordScript(strokes)
	aeKeyDown				= 0x1f,		// the key events (HandleKeyEvent, NOT YET)
	aeKeyUp					= 0x20,
	aeKeyRepeat				= 0x22,
	aeKeyString				= 0x23,
	aeAddChild				= 0x29,		// the frameParameter's view added under the receiver and shown
	aeDropChild				= 0x2a,		// the parameter (a view) hidden and removed
	aeHide					= 0x2b,
	aeShow					= 0x2c,		// (the parameter kNoModalCheck skips the modal check)
	aeScrollUp				= 0x2d,		// viewScrollUpScript
	aeScrollDown			= 0x2e,		// viewScrollDownScript
	aeGesture2f				= 0x2f,		// more viewGestureScript kinds
	aeRemoveAllHilites		= 0x30,
	aeGesture31				= 0x31,
	aeGesture32				= 0x32,
	aeOverview				= 0x33,		// viewOverviewScript
	aeAddData				= 0x3d,		// the frameParameter added to the receiver's soup (the undo of aeRemoveData)
	aeRemoveData			= 0x3f,		// the child of the parameter's id removed from the soup
	aeMoveData				= 0x40,		// the receiver moved by params[0], params[1] (undone by the reverse)
	aeScaleData				= 0x42,		// the receiver scaled from params[0..3] (undone by the reverse)
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

Ref		MakeCommand(ULong id, TResponder* receiver, Long parameter);			// ROM 0x00070dc4 MakeCommand__FUlP10TResponderl (Long: the ROM's word - a pointer fits)
TResponder*	CommandReceiver(RefArg cmd);										// ROM 0x00070e88 CommandReceiver__FRC6RefVar
long	CommandID(RefArg cmd);													// ROM 0x0007117c CommandID__FRC6RefVar
void	CommandSetID(RefArg cmd, ULong id);										// ROM 0x000711b8 CommandSetID__FRC6RefVarUl
long	CommandResult(RefArg cmd);												// ROM 0x000711f8 CommandResult__FRC6RefVar
void	CommandSetResult(RefArg cmd, long result);								// ROM 0x00071234 CommandSetResult__FRC6RefVarl
Long	CommandParameter(RefArg cmd);											// ROM 0x00071274 CommandParameter__FRC6RefVar
void	CommandSetParameter(RefArg cmd, Long parameter);						// ROM 0x000712b0 CommandSetParameter__FRC6RefVarl
Ref		CommandFrameParameter(RefArg cmd);										// ROM 0x000712f0 CommandFrameParameter__FRC6RefVar
void	CommandSetFrameParameter(RefArg cmd, RefArg parameter);					// ROM 0x0007130c CommandSetFrameParameter__FRC6RefVarT1
Long	CommandIndexParameter(RefArg cmd, long index);							// ROM 0x00070fc4 CommandIndexParameter__FRC6RefVarl
void	CommandSetIndexParameter(RefArg cmd, long index, Long parameter);		// ROM 0x00070f08 CommandSetIndexParameter__FRC6RefVarlT2
void	CommandSetIndexFrame(RefArg cmd, long index, RefArg parameter);			// ROM 0x00071058 CommandSetIndexFrame__FRC6RefVarlT1
void	MarkUndoCommand(RefArg cmd);											// ROM 0x00071104 MarkUndoCommand__FRC6RefVar
Boolean	IsUndoCommand(RefArg cmd);												// ROM 0x00071144 IsUndoCommand__FRC6RefVar
Ref		MakeRunScriptCommand(RefArg context, RefArg script, RefArg args);		// ROM 0x000345bc MakeRunScriptCommand__FRC6RefVarN21
Ref		MakeUndoCommand(RefArg receiver, RefArg message, RefArg args);			// ROM 0x0003467c MakeUndoCommand__FRC6RefVarN21
Ref		GetStrokeBundleFromCommand(RefArg cmd);									// ROM 0x00266cec GetStrokeBundleFromCommand__FRC6RefVar

TResponder*	GetResponder(RefArg context, RefArg name);							// ROM 0x000b07a8 GetResponder__FRC6RefVarT1
TResponder*	FailGetResponder(RefArg context, RefArg name);						// ROM 0x000b07fc FailGetResponder__FRC6RefVarT1

#endif	/* __COMMANDS_H */
