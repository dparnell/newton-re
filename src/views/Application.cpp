/*
	File:		views/Application.cpp

	Contains:	TApplication: dispatching commands, the undo stacks, the
				delayed actions; the NewtonScript functions over them.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Application.h"
#include "Commands.h"
#include "RootView.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "CompMath.h"

TApplication*	gApplication = nil;		// ROM 0x0c1025a0

const long kNewtErrNoReceiver = -8003;	// ErrorNotify's code for a command nobody took (its documented name is not known)
const long kNotifyKindError = 3;

static TTime kZeroTime;					// the ROM's zeroTime: all zero


// ROM 0x001465a4 ErrorNotify__FlT1
// The root view told of an error: root:Notify(kind, error, nil) (the
// ROM's Rviewroot script puts up the notify icon; the host's root has the
// method only when a template gives it one - DEVIATION: sent if defined).
void
ErrorNotify(long error, long kind)
{
	if (gRootView == nil)
		return;
	RefVar args(MakeArray(3));
	SetArraySlotRef(args, 0, MAKEINT(kind));
	SetArraySlotRef(args, 1, MAKEINT(error));
	long defined;
	DoMessageIfDefined(RefVar(gRootView->fContext), RSSYMnotify, args, &defined);
}


/*------------------------------------------------------------------------------
	T A p p l i c a t i o n
------------------------------------------------------------------------------*/

// ROM 0x00033aa8 ClassID__12TApplicationCFv
long
TApplication::ClassID(void) const
{
	return clApplication;
}


// ROM 0x00033ab0 DerivedFrom__12TApplicationCFl
Boolean
TApplication::DerivedFrom(long id) const
{
	return id == clApplication || TResponder::DerivedFrom(id);
}


// ROM 0x0003448c __dt__12TApplicationFv
TApplication::~TApplication()
{ }


// ROM 0x00034334 Constructor__12TApplicationFv
// The toolbox initialised (the subclass's), empty undo stacks, no batch
// pending, the next idle time 1 (never, as it were).
void
TApplication::Constructor(void)
{
	InitToolbox();
	fUndoStack = MakeArray(0);
	fRedoStack = MakeArray(0);
	fNewUndoBatch = false;
	fRedoNext = false;
	fNextIdleTime.time.hi = 0;
	fNextIdleTime.time.lo = 1;
}


// ROM 0x00034500 InitToolbox__12TApplicationFv
void	TApplication::InitToolbox(void)		{ }
// ROM 0x00034504 Run__12TApplicationFv
void	TApplication::Run(void)				{ }
// ROM 0x00034508 Quit__12TApplicationFv
void	TApplication::Quit(void)			{ }


// ROM 0x00033ae4 Idle__12TApplicationFv
// An idle: the next undo command posted starts a new batch.
void
TApplication::Idle(void)
{
	fNewUndoBatch = true;
}


// ROM 0x00034078 DispatchCommand__12TApplicationFRC6RefVar
// The command sent to its receiver's DoCommand; ==> the result the
// receiver left (0 when there is no receiver: ErrorNotify -8003).
long
TApplication::DispatchCommand(RefArg cmd)
{
	TResponder* receiver = CommandReceiver(cmd);
	if (receiver != nil)
		receiver->DoCommand(cmd);
	else
		ErrorNotify(kNewtErrNoReceiver, kNotifyKindError);
	return CommandResult(cmd);
}


// ROM 0x000343a0 PostUndoCommand__12TApplicationFRC6RefVar
// The command marked undo and pushed on the undo stack; the first one
// after an idle starts a new batch: the stack so far becomes the redo
// stack when the undoRedo preference is off, else is dropped.
void
TApplication::PostUndoCommand(RefArg cmd)
{
	if (fNewUndoBatch)
	{
		fNewUndoBatch = false;
		if (ISNIL(NSCallGlobalFn(RSSYMgetuserconfig, RSSYMundoredo)))
			fRedoStack = (Ref) fUndoStack;
		fUndoStack = MakeArray(0);
		fRedoNext = false;
	}
	MarkUndoCommand(cmd);
	AddArraySlot(fUndoStack, cmd);
}


// ROM 0x00034434 PostUndoCommand__12TApplicationFUlP10TResponderl
// A command made and posted (the application itself the receiver when
// none is given).
void
TApplication::PostUndoCommand(ULong id, TResponder* receiver, Long parameter)
{
	if (receiver == nil)
		receiver = this;
	RefVar cmd(MakeCommand(id, receiver, parameter));
	gApplication->PostUndoCommand(cmd);
}


// ROM 0x000340c8 Undo__12TApplicationFv
// The undo stack's commands dispatched, newest first, while a fresh
// stack collects what they post (the undo of the undo); the old stack
// is emptied.  With the undoRedo preference off the redo stack is what
// the undone commands posted (the next Undo redoes); on, the stacks
// swap.  Nothing undone: ErrorNotify -8003.
void
TApplication::Undo(void)
{
	long undone = 0;
	RefVar stack((Ref) fUndoStack);
	RefVar redo((Ref) fRedoStack);
	Boolean redoPref = NOTNIL(NSCallGlobalFn(RSSYMgetuserconfig, RSSYMundoredo));
	if (!redoPref)
		fRedoStack = MakeArray(0);
	// (the ROM's ArrayIsEmpty 0x0012a834 / ArrayPop 0x0012a7dc)
	fUndoStack = MakeArray(0);
	Boolean redoNext = fRedoNext;
	newton_try
	{
		while (Length(stack) != 0)
		{
			RefVar cmd(GetArraySlotRef(stack, Length(stack) - 1));
			SetLength(stack, Length(stack) - 1);
			if (DispatchCommand(cmd) == 0)
				undone = -1;
		}
	}
	cleanup
	{
		SetLength(stack, 0);
	}
	end_try;
	SetLength(stack, 0);
	if (!redoPref)
		fUndoStack = (Ref) redo;
	fRedoNext = !redoNext;
	if (undone == 0)
		ErrorNotify(kNewtErrNoReceiver, kNotifyKindError);
}


// ROM 0x00034268 ClearUndo__12TApplicationFv
void
TApplication::ClearUndo(void)
{
	fUndoStack = MakeArray(0);
	fRedoStack = MakeArray(0);
	fRedoNext = false;
}


// ROM 0x000342ac GetUndoState__12TApplicationFv
// 'undo, or - with the undoRedo preference - 'undoRedo when the next Undo
// redoes.
Ref
TApplication::GetUndoState(void)
{
	if (ISNIL(NSCallGlobalFn(RSSYMgetuserconfig, RSSYMundoredo)))
		return RSSYMundo;
	return fRedoNext ? RSSYMundoredo : RSSYMundo;
}


// ROM 0x0003430c GetUndoStack__12TApplicationFl
Ref
TApplication::GetUndoStack(long which)
{
	return which == 0 ? (Ref) fUndoStack : (Ref) fRedoStack;
}


// ROM 0x00033af0 AddDelayedAction__12TApplicationFRC6RefVarN31
// A delayed action queued: the receiver, the action (a message symbol,
// or a function - nil receiver: called), its args, and the time it is
// due (a 'time binary of the global time delay milliseconds from now;
// a non-integer delay: at the next idle).  NOT YET RECONSTRUCTED: the
// idle timer re-armed for the earliest action.
void
TApplication::AddDelayedAction(RefArg receiver, RefArg action, RefArg args, RefArg delay)
{
	long index;
	if (ISNIL(fDelayedActions))
	{
		index = 0;
		fDelayedActions = MakeArray(4);
	}
	else
	{
		index = Length(fDelayedActions);
		SetLength(fDelayedActions, index + 4);
	}
	SetArraySlotRef(fDelayedActions, index, receiver);
	SetArraySlotRef(fDelayedActions, index + 1, action);
	SetArraySlotRef(fDelayedActions, index + 2, args);
	if (ISINT(delay))
	{
		RefVar when(AllocateBinary(RSSYMtime, sizeof(TTime)));
		TTime due = TimeFromNow((TTimeout) (RVALUE(delay) * kMilliseconds));
		*(TTime*) BinaryData(when) = due;
		SetArraySlotRef(fDelayedActions, index + 3, when);
	}
	else
		SetArraySlotRef(fDelayedActions, index + 3, NILREF);
}


// ROM 0x00033c98 RunNextDelayedAction__12TApplicationFv
// The first delayed action whose time has come (or that has none) taken
// out of the queue (the queue nil when empty) and done - a function
// called (DoBlock), a message sent (DoMessage), a script run on the
// receiver (DoScript).  ==> whether one was run.
Boolean
TApplication::RunNextDelayedAction(void)
{
	if (ISNIL(fDelayedActions) || Length(fDelayedActions) <= 0)
		return false;
	TTime now = GetGlobalTime();
	for (long index = 0; index < Length(fDelayedActions); index += 4)
	{
		RefVar when(GetArraySlotRef(fDelayedActions, index + 3));
		Boolean due = ISNIL(when);
		if (!due)
		{
			TTime time = *(TTime*) BinaryData(when);
			due = CompCompare(&now.time, &time.time) > 0;
		}
		if (!due)
			continue;
		RefVar receiver(GetArraySlotRef(fDelayedActions, index));
		RefVar action(GetArraySlotRef(fDelayedActions, index + 1));
		RefVar args(GetArraySlotRef(fDelayedActions, index + 2));
		ArrayRemoveCount(fDelayedActions, index, 4);
		if (Length(fDelayedActions) == 0)
			fDelayedActions = NILREF;
		if (ISNIL(receiver))
			DoBlock(action, args);
		else if (IsSymbol(action))
			DoMessage(receiver, action, args);
		else
			DoScript(receiver, action, args);
		return true;
	}
	return false;
}


// ROM 0x00033e9c UpdateNextIdleTime__12TApplicationFRC5TTime
// The next idle time brought forward to the time when it is sooner (a
// zero time means none).
void
TApplication::UpdateNextIdleTime(const TTime& time)
{
	if (CompCompare(&time.time, &kZeroTime.time) == 0)
		return;
	if (CompCompare(&fNextIdleTime.time, &kZeroTime.time) != 0 && CompCompare(&time.time, &fNextIdleTime.time) >= 0)
		return;
	fNextIdleTime = time;
}


// ROM 0x00033f18 NextDelayedActionTime__12TApplicationFRC5TTime
// The earliest time of the queued actions, from the one given (the
// given one when there is none earlier, or no queue, or the system is
// not up).
TTime
TApplication::NextDelayedActionTime(const TTime& from)
{
	TTime earliest = from;
	if (!gNewtIsAliveAndWell || ISNIL(fDelayedActions))
		return earliest;
	for (long index = 3; index < Length(fDelayedActions); index += 4)
	{
		RefVar when(GetArraySlotRef(fDelayedActions, index));
		if (ISNIL(when))
			continue;
		TTime time = *(TTime*) BinaryData(when);
		if (CompCompare(&earliest.time, &kZeroTime.time) == 0 || CompCompare(&time.time, &earliest.time) < 0)
			earliest = time;
	}
	return earliest;
}


// ROM 0x00034694 DoCommand__12TApplicationFRC6RefVar
// The application's own commands: aeAppIdle notes it; aeRunScript's frame
// parameter [script, args, context] runs the script on the context's
// view (no view: the result -8003) - an 'undo array sends the message
// (or calls the function) instead; aeUndo undoes.
Boolean
TApplication::DoCommand(RefArg cmd)
{
	long id = CommandID(cmd);
	if (id == aeAppIdle)
		fFlags |= 0x80000000;
	else if (id == aeRunScript)
	{
		RefVar params(CommandFrameParameter(cmd));
		RefVar context(GetArraySlotRef(params, 2));
		RefVar script(GetArraySlotRef(params, 0));
		RefVar args(GetArraySlotRef(params, 1));
		CommandSetResult(cmd, 0);
		if (EQRef(ClassOf(params), RSSYMundo))
		{
			if (IsFunction(script))
				DoBlock(script, args);
			else
				DoMessage(context, script, args);
		}
		else
		{
			TView* view = GetView(context);
			if (view == nil)
				CommandSetResult(cmd, kNewtErrNoReceiver);
			else
				view->RunScript(script, args, true, nil);
		}
	}
	else if (id == aeUndo)
		Undo();
	return true;
}


/*------------------------------------------------------------------------------
	N a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x000af8fc FPostCommand__FRC6RefVarN21
// PostCommand(receiverName, id): the command (the id an integer, or a
// four-character string) dispatched to the responder of the name.
static Ref
FPostCommand(RefArg rcvr, RefArg name, RefArg id)
{
	TResponder* responder = FailGetResponder(rcvr, name);
	ULong commandId;
	if (IsString(id))
		commandId = *(ULong*) BinaryData(id);
	else if (ISINT(id))
		commandId = (ULong) RVALUE(id);
	else
		return TRUEREF;
	if (commandId != 0)
	{
		RefVar cmd(MakeCommand(commandId, responder, kNoParameter));
		gApplication->DispatchCommand(cmd);
	}
	return NILREF;
}


// ROM 0x000af9c4 FPostCommandParam__FRC6RefVarN31
// PostCommandParam(receiverName, id, parameter): an integer parameter,
// or a frame parameter.
static Ref
FPostCommandParam(RefArg rcvr, RefArg name, RefArg id, RefArg parameter)
{
	TResponder* responder = FailGetResponder(rcvr, name);
	if (!ISINT(id) || RVALUE(id) == 0)
		return NILREF;
	RefVar cmd;
	if (ISINT(parameter))
		cmd = MakeCommand((ULong) RVALUE(id), responder, RVALUE(parameter));
	else
	{
		cmd = MakeCommand((ULong) RVALUE(id), responder, kNoParameter);
		CommandSetFrameParameter(cmd, parameter);
	}
	gApplication->DispatchCommand(cmd);
	return NILREF;
}


// ROM 0x000af8d8 FPostAndDo__FRC6RefVarT1
static Ref
FPostAndDo(RefArg /*rcvr*/, RefArg cmd)
{
	gApplication->DispatchCommand(cmd);
	return NILREF;
}


// ROM 0x000af648 FAddDelayedAction__FRC6RefVarN31
static Ref
FAddDelayedAction(RefArg receiver, RefArg action, RefArg args, RefArg delay)
{
	gApplication->AddDelayedAction(receiver, action, args, delay);
	return NILREF;
}


// ROM 0x000af67c FAddDelayedCall
static Ref
FAddDelayedCall(RefArg /*rcvr*/, RefArg fn, RefArg args, RefArg delay)
{
	gApplication->AddDelayedAction(RefVar(NILREF), fn, args, delay);
	return NILREF;
}


// ROM 0x000af6d0 FAddDelayedSend
static Ref
FAddDelayedSend(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args, RefArg delay)
{
	gApplication->AddDelayedAction(receiver, message, args, delay);
	return NILREF;
}


// ROM 0x000af704 FAddDeferredAction__FRC6RefVarN21
static Ref
FAddDeferredAction(RefArg receiver, RefArg action, RefArg args)
{
	return FAddDelayedAction(receiver, action, args, RefVar(NILREF));
}


// ROM 0x000af754 FAddDeferredCall
static Ref
FAddDeferredCall(RefArg rcvr, RefArg fn, RefArg args)
{
	return FAddDelayedCall(rcvr, fn, args, RefVar(NILREF));
}


// ROM 0x000af7a4 FAddDeferredSend
static Ref
FAddDeferredSend(RefArg rcvr, RefArg receiver, RefArg message, RefArg args)
{
	return FAddDelayedSend(rcvr, receiver, message, args, RefVar(NILREF));
}


// ROM 0x000afc44 FAddUndoAction__FRC6RefVarN21
// AddUndoAction(script, args) on a view (a slot of the ROM's root
// template, and a global): the script run on it by Undo.
Ref
FAddUndoAction(RefArg rcvr, RefArg script, RefArg args)
{
	RefVar cmd(MakeRunScriptCommand(rcvr, script, args));
	gApplication->PostUndoCommand(cmd);
	return NILREF;
}


// ROM 0x000afc84 FAddUndoCall
static Ref
FAddUndoCall(RefArg /*rcvr*/, RefArg fn, RefArg args)
{
	RefVar cmd(MakeUndoCommand(RefVar(NILREF), fn, args));
	gApplication->PostUndoCommand(cmd);
	return NILREF;
}


// ROM 0x000afcec FAddUndoSend
static Ref
FAddUndoSend(RefArg /*rcvr*/, RefArg receiver, RefArg message, RefArg args)
{
	RefVar cmd(MakeUndoCommand(receiver, message, args));
	gApplication->PostUndoCommand(cmd);
	return NILREF;
}


// ROM 0x000afd40 FClearUndoStacks
static Ref
FClearUndoStacks(RefArg /*rcvr*/)
{
	gApplication->ClearUndo();
	return NILREF;
}


// ROM 0x000af638 FGetUndoState
static Ref
FGetUndoState(RefArg /*rcvr*/)
{
	return gApplication->GetUndoState();
}


// host: Undo() - the application's Undo (the ROM's is the undo button's
// aeUndo command through the root view; DEVIATION: a global for the tests)
static Ref
FUndo(RefArg /*rcvr*/)
{
	gApplication->Undo();
	return NILREF;
}


// host: RunDelayedActions() - the due delayed actions run, one after the
// other (the ROM runs them from the event loop: RunDelayedActionProcs)
static Ref
FRunDelayedActions(RefArg /*rcvr*/)
{
	while (gApplication->RunNextDelayedAction())
		;
	return NILREF;
}


void
RegisterApplicationNatives(void)
{
	RegisterNativeFunction("FPostCommand__FRC6RefVarN21", (void*) FPostCommand, 2);
	RegisterNativeFunction("FPostCommandParam__FRC6RefVarN31", (void*) FPostCommandParam, 3);
	RegisterNativeFunction("FPostAndDo__FRC6RefVarT1", (void*) FPostAndDo, 1);
	RegisterNativeFunction("FAddDelayedAction__FRC6RefVarN31", (void*) FAddDelayedAction, 3);
	RegisterNativeFunction("FAddDelayedCall", (void*) FAddDelayedCall, 3);
	RegisterNativeFunction("FAddDelayedSend", (void*) FAddDelayedSend, 4);
	RegisterNativeFunction("FAddDeferredAction__FRC6RefVarN21", (void*) FAddDeferredAction, 2);
	RegisterNativeFunction("FAddDeferredCall", (void*) FAddDeferredCall, 2);
	RegisterNativeFunction("FAddDeferredSend", (void*) FAddDeferredSend, 3);
	RegisterNativeFunction("FAddUndoAction__FRC6RefVarN21", (void*) FAddUndoAction, 2);
	RegisterNativeFunction("FAddUndoCall", (void*) FAddUndoCall, 2);
	RegisterNativeFunction("FAddUndoSend", (void*) FAddUndoSend, 3);
	RegisterNativeFunction("FClearUndoStacks", (void*) FClearUndoStacks, 0);
	RegisterNativeFunction("FGetUndoState", (void*) FGetUndoState, 0);
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "Undo")), RefVar(MakeCFunction((void*) FUndo, 0, nil)));
	SetFrameSlot(RefVar(gFunctionFrame), RefVar(Intern((char*) "RunDelayedActions")), RefVar(MakeCFunction((void*) FRunDelayedActions, 0, nil)));
}
