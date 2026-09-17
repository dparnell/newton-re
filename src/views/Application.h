/*
	File:		views/Application.h

	Contains:	TApplication (class 67): the responder at the top of the
				view system - it dispatches commands to their receivers
				(DispatchCommand), keeps the undo and redo stacks of undo
				commands (PostUndoCommand, Undo, ClearUndo, GetUndoState)
				and the delayed actions (AddDelayedAction: [receiver,
				message or function, args, time] quadruples the idle runs
				when their time comes - RunNextDelayedAction), and answers
				its own commands (DoCommand: aeAppIdle, aeRunScript, aeUndo).
				gApplication is the one application; the ROM's is a
				TNotebook (its Run is the event loop, its InitToolbox makes
				the root view, the inker, the offscreen bitmaps - NOT YET
				RECONSTRUCTED), the host's a TApplication that
				InitViewSystem makes.  The ROM's object is 0x20 bytes.

				NOT YET RECONSTRUCTED: the idle timer the delayed actions
				arm (the event handler's ResetIdle/StopIdle: the host runs
				the due actions when RunNextDelayedAction is called), the
				event loop.

	Reconstructed from the MP2100 D ROM (0x00033b58-0x000345c0,
	0x00034744-0x00034800, 0x000b0800-0x000b0fa0); each function cites
	its origin.
*/

#ifndef __APPLICATION_H
#define __APPLICATION_H

#ifndef __VIEW_H
#include "View.h"
#endif
#ifndef __NEWTONTIME_H
#include "NewtonTime.h"
#endif

const long clApplication = 67;

class TApplication : public TResponder
{
public:
	virtual long	ClassID(void) const;										// ROM 0x00033b58 ClassID__12TApplicationCFv
	virtual Boolean	DerivedFrom(long id) const;									// ROM 0x00033b60 DerivedFrom__12TApplicationCFl
	virtual			~TApplication();											// ROM 0x0003453c __dt__12TApplicationFv
	virtual Boolean	DoCommand(RefArg cmd);										// ROM 0x00034744 DoCommand__12TApplicationFRC6RefVar (vtable +0x10, after TxObject's Key)
	virtual void	Constructor(void);											// ROM 0x000343e4 Constructor__12TApplicationFv (+0x14: InitToolbox, the undo stacks, the idle time)
	virtual void	Run(void);													// ROM 0x000345b4 Run__12TApplicationFv (+0x18)
	virtual void	Idle(void);													// ROM 0x00033b94 Idle__12TApplicationFv (+0x1c)
	virtual void	Quit(void);													// ROM 0x000345b8 Quit__12TApplicationFv (+0x20)
	virtual void	InitToolbox(void);											// ROM 0x000345b0 InitToolbox__12TApplicationFv (+0x24)

	long		DispatchCommand(RefArg cmd);									// ROM 0x00034128 DispatchCommand__12TApplicationFRC6RefVar
	void		PostUndoCommand(RefArg cmd);									// ROM 0x00034450 PostUndoCommand__12TApplicationFRC6RefVar
	void		PostUndoCommand(ULong id, TResponder* receiver, Long parameter);	// ROM 0x000344e4 PostUndoCommand__12TApplicationFUlP10TResponderl
	void		Undo(void);														// ROM 0x00034178 Undo__12TApplicationFv
	void		ClearUndo(void);												// ROM 0x00034318 ClearUndo__12TApplicationFv
	Ref			GetUndoState(void);												// ROM 0x0003435c GetUndoState__12TApplicationFv
	Ref			GetUndoStack(long which);										// ROM 0x000343bc GetUndoStack__12TApplicationFl
	void		AddDelayedAction(RefArg receiver, RefArg action, RefArg args, RefArg delay);	// ROM 0x00033ba0 AddDelayedAction__12TApplicationFRC6RefVarN31
	Boolean		RunNextDelayedAction(void);										// ROM 0x00033d48 RunNextDelayedAction__12TApplicationFv - one due action run; ==> whether one was
	void		UpdateNextIdleTime(const TTime& time);							// ROM 0x00033f4c UpdateNextIdleTime__12TApplicationFRC5TTime
	TTime		NextDelayedActionTime(const TTime& now);						// ROM 0x00033fc8 NextDelayedActionTime__12TApplicationFRC5TTime

	TTime		fNextIdleTime;		// +0x04
	RefStruct	fUndoStack;			// +0x0c  (the ROM: a RefStruct*) the undo commands, newest last
	RefStruct	fRedoStack;			// +0x10  the undone ones, for redo (with the undoRedo preference)
	Boolean		fNewUndoBatch;		// +0x14  the next undo command starts a batch (Idle sets it)
	Boolean		fRedoNext;			// +0x15  the next Undo redoes
	RefStruct	fDelayedActions;	// +0x18  [receiver, action, args, time (nil: at once), ...]
	ULong		fFlags;				// +0x1c  bit 31: aeAppIdle received
};

extern TApplication*	gApplication;		// ROM 0x0c1025a0 gApplication

void	ErrorNotify(long error, long kind);										// ROM 0x001480fc ErrorNotify__FlT1 - root:Notify(kind, error, nil)
Ref		FAddUndoAction(RefArg rcvr, RefArg script, RefArg args);				// ROM 0x000b0e3c FAddUndoAction__FRC6RefVarN21 - a view's AddUndoAction method
void	RegisterApplicationNatives(void);	// PostCommand, PostCommandParam, PostAndDo, AddDelayedAction/Call/Send, AddDeferredAction/Call/Send, AddUndoAction/Call/Send, ClearUndoStacks, GetUndoState

#endif	/* __APPLICATION_H */
