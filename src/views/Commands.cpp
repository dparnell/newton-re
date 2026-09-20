/*
	File:		views/Commands.cpp

	Contains:	Command frames: MakeCommand and the accessors.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Commands.h"
#include "Application.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"


// ROM 0x00070424 MakeCommand__FUlP10TResponderl
// A command frame (a clone of protoCommand) with the id, the receiver's
// DEVIATION: a command's parameter is one 32-bit word, and it is
// sometimes a number (a key code, a view's index) and sometimes a pointer
// (the unit a click carries).  The ROM keeps it as an integer Ref - the
// word shifted up by two - which round-trips a pointer because the
// machine's RAM lives below 0x40000000.  A host pointer does not fit in
// the thirty bits a Newton integer holds, so one that does not is kept
// whole as a pointer Ref instead (AddressToRef, the convention the view
// objects already use), and told apart on the way out by being wider than
// a Newton word.  A parameter that does fit is the integer the ROM would
// have written, which is what a script reading cmd.parameter sees.
static Ref
ParameterRef(Long parameter)
{
	Ref value = MAKEINT(parameter);
	if ((Long) RVALUE(value) == parameter)
		return value;
	return AddressToRef((void*) parameter);
}


static Long
ParameterValue(Ref value)
{
	if (value == (Ref) (int) value)			// a Newton-sized word
		return (Long) RVALUE(value);
	return (Long) RefToAddress(value);
}


// context ('application for the application) and the parameter.
//
// DEVIATION: the ROM does not look at the receiver, it takes the address
// of the context field (receiver + 0x24) and hands that to SetFrameSlot -
// so a nil receiver reads the word at 0x24, which on the Newton is inside
// the exception vectors and gives the command a receiver that is not a
// view.  TView::RealDoCommand's aeAddChild does exactly that whenever the
// child it added was a preallocated slip that is not open, and the
// command then fails in the dispatch and is reported as an action error.
// A host cannot read address 0x24, so a nil receiver leaves the slot nil,
// which fails in the same place for the same reason.
Ref
MakeCommand(ULong id, TResponder* receiver, Long parameter)
{
	RefVar cmd(Clone(RefVar(Rprotocommand)));
	SetFrameSlot(cmd, RSSYMid, RefVar(MAKEINT(id)));
	if (receiver == gApplication)
		SetFrameSlot(cmd, RSSYMreceiver, RSSYMapplication);
	else if (receiver == nil)
		SetFrameSlot(cmd, RSSYMreceiver, RefVar(NILREF));
	else
		SetFrameSlot(cmd, RSSYMreceiver, ((TView*) receiver)->fContext);
	SetFrameSlot(cmd, RSSYMparameter, RefVar(ParameterRef(parameter)));
	return cmd;
}


// ROM 0x000704e8 CommandReceiver__FRC6RefVar
// The receiver: the application for 'application, else the context's view.
TResponder*
CommandReceiver(RefArg cmd)
{
	RefVar receiver(GetFrameSlotRef(cmd, RSSYMreceiver));
	if (EQRef(receiver, RSSYMapplication))
		return gApplication;
	return GetView(receiver);
}


// ROM 0x000707dc CommandID__FRC6RefVar
long
CommandID(RefArg cmd)
{
	RefVar id(GetFrameSlotRef(cmd, RSSYMid));
	if (!ISINT(id))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, id);
	return RVALUE(id);
}


// ROM 0x00070818 CommandSetID__FRC6RefVarUl
void
CommandSetID(RefArg cmd, ULong id)
{
	SetFrameSlot(cmd, RSSYMid, RefVar(MAKEINT(id)));
}


// ROM 0x00070858 CommandResult__FRC6RefVar
long
CommandResult(RefArg cmd)
{
	RefVar result(GetFrameSlotRef(cmd, RSSYMresult));
	if (!ISINT(result))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, result);
	return RVALUE(result);
}


// ROM 0x00070894 CommandSetResult__FRC6RefVarl
void
CommandSetResult(RefArg cmd, long result)
{
	SetFrameSlot(cmd, RSSYMresult, RefVar(MAKEINT(result)));
}


// ROM 0x000708d4 CommandParameter__FRC6RefVar
Long
CommandParameter(RefArg cmd)
{
	RefVar parameter(GetFrameSlotRef(cmd, RSSYMparameter));
	if (!ISINT(parameter))
		ThrowBadTypeWithFrameData(kNSErrNotAnInteger, parameter);
	return ParameterValue(parameter);
}


// ROM 0x00070910 CommandSetParameter__FRC6RefVarl
void
CommandSetParameter(RefArg cmd, Long parameter)
{
	SetFrameSlot(cmd, RSSYMparameter, RefVar(ParameterRef(parameter)));
}


// ROM 0x00070950 CommandFrameParameter__FRC6RefVar
Ref
CommandFrameParameter(RefArg cmd)
{
	return GetFrameSlotRef(cmd, RSSYMframeparameter);
}


// ROM 0x0007096c CommandSetFrameParameter__FRC6RefVarT1
void
CommandSetFrameParameter(RefArg cmd, RefArg parameter)
{
	SetFrameSlot(cmd, RSSYMframeparameter, parameter);
}


// the params array, made (and grown to hold the index) as needed
static Ref
CommandParams(RefArg cmd, long index)
{
	RefVar params(GetFrameSlotRef(cmd, RSSYMparams));
	if (ISNIL(params))
	{
		params = MakeArray(0);
		SetFrameSlot(cmd, RSSYMparams, params);
	}
	if (Length(params) < index + 1)
		SetLength(params, index + 1);
	return params;
}


// ROM 0x00070624 CommandIndexParameter__FRC6RefVarl
// params[index]; 0 beyond the array (or without one).
Long
CommandIndexParameter(RefArg cmd, long index)
{
	RefVar params(GetFrameSlotRef(cmd, RSSYMparams));
	if (ISNIL(params) || Length(params) < index + 1)
		return 0;
	return ParameterValue(GetArraySlotRef(params, index));
}


// ROM 0x00070568 CommandSetIndexParameter__FRC6RefVarlT2
void
CommandSetIndexParameter(RefArg cmd, long index, Long parameter)
{
	RefVar params(CommandParams(cmd, index));
	SetArraySlotRef(params, index, ParameterRef(parameter));
}


// ROM 0x000706b8 CommandSetIndexFrame__FRC6RefVarlT1
void
CommandSetIndexFrame(RefArg cmd, long index, RefArg parameter)
{
	RefVar params(CommandParams(cmd, index));
	SetArraySlotRef(params, index, parameter);
}


// ROM 0x00070764 MarkUndoCommand__FRC6RefVar
void
MarkUndoCommand(RefArg cmd)
{
	SetFrameSlot(cmd, RSSYMundo, RefVar(TRUEREF));
}


// ROM 0x000707a4 IsUndoCommand__FRC6RefVar
Boolean
IsUndoCommand(RefArg cmd)
{
	return NOTNIL(GetFrameSlotRef(cmd, RSSYMundo));
}


// ROM 0x0003450c MakeRunScriptCommand__FRC6RefVarN21
// An aeRunScript command for the application: the frame parameter
// [script, args, context] - the view of the context runs the script.
Ref
MakeRunScriptCommand(RefArg context, RefArg script, RefArg args)
{
	RefVar cmd(MakeCommand(aeRunScript, gApplication, kNoParameter));
	RefVar params(MakeArray(3));
	SetArraySlotRef(params, 2, context);
	SetArraySlotRef(params, 0, script);
	SetArraySlotRef(params, 1, args);
	CommandSetFrameParameter(cmd, params);
	return cmd;
}


// ROM 0x000345cc MakeUndoCommand__FRC6RefVarN21
// The same with an 'undo array: a function is called, a message sent to
// the receiver.
Ref
MakeUndoCommand(RefArg receiver, RefArg message, RefArg args)
{
	RefVar cmd(MakeCommand(aeRunScript, gApplication, kNoParameter));
	RefVar params(AllocateArray(RSSYMundo, 3));
	SetArraySlotRef(params, 2, receiver);
	SetArraySlotRef(params, 0, message);
	SetArraySlotRef(params, 1, args);
	CommandSetFrameParameter(cmd, params);
	return cmd;
}


// ROM 0x00268c24 GetStrokeBundleFromCommand__FRC6RefVar
// The strokes of a recognition command: the frame parameter, or made
// from the unit the parameter points to (NOT YET RECONSTRUCTED: the
// units - TUnitPublic::WordInfo and Strokes; the frame parameter alone).
Ref
GetStrokeBundleFromCommand(RefArg cmd)
{
	return CommandFrameParameter(cmd);
}


// ROM 0x000af5b0 GetResponder__FRC6RefVarT1
// The responder a name means from a context: the application for
// 'application, else the view (GetView).
TResponder*
GetResponder(RefArg context, RefArg name)
{
	if (EQRef(name, RSSYMapplication))
		return gApplication;
	return GetView(context, name);
}


// ROM 0x000af604 FailGetResponder__FRC6RefVarT1
TResponder*
FailGetResponder(RefArg context, RefArg name)
{
	TResponder* responder = GetResponder(context, name);
	if (responder == nil)
		ThrowMsg((char*) "nil responder");
	return responder;
}


// ROM 0x0017a26c CommandText__FRC6RefVar
Ref
CommandText(RefArg cmd)
{
	return GetFrameSlotRef(cmd, RSSYMtext);
}


// ROM 0x0017a25c CommandSetText__FRC6RefVarT1
void
CommandSetText(RefArg cmd, RefArg text)
{
	SetFrameSlot(cmd, RSSYMtext, text);
}
