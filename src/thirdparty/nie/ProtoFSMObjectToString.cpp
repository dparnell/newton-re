/*
	File:		thirdparty/nie/ProtoFSMObjectToString.cpp

	Contains:	The NIE's ObjectToString: an object printed as NewtonScript
				source into a string (what protoFSM's ExceptionHandler and the
				Internet Enabler's InetObjectToString report with), under the
				same global settings as the Inspector's printer.  Re-expressed
				from the native code; in NewtonScript:

					func(obj)
					begin
						local backIndex, backList, maxDepth, maxLength,
							runParent, runProto, f, p, floatFormat;
						backIndex := nil;
						backList := Array(1, nil);
						runProto := GetGlobalVar('printProto) <> nil;
						runParent := GetGlobalVar('printParent) <> nil;
						floatFormat := GetGlobalVar('printReal);
						if not IsString(floatFormat) then floatFormat := "%.16e";
						maxDepth := GetGlobalVar('printDepth);
						if not IsNumber(maxDepth) or maxDepth < 0 then maxDepth := 65535;
						maxLength := GetGlobalVar('printLength);
						if not IsNumber(maxLength) or maxLength < 0 then maxLength := 65535;
						p := func(s) ...;		// the trim, 0x14535
						f := func(x, depth) ...;	// the printer, 0x14649
						try
							p(f(obj, 0))
						onexception |evt.ex.outofmem| do begin
							backList := nil; "<insufficient memory>"
						end
						onexception |evt.ex| do begin
							backList := nil; "<exception occurred>"
						end
					end

				The locals live in the function's environment (its argument
				frame, slots 4 to 12) because p and f, made with that
				environment as their lexical scope, read and write them.
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "NewtonExceptions.h"

void IncrementCurrentStackPos(void);		// frames/ObjectHeap.cpp
void DecrementCurrentStackPos(void);
void ClearRefHandles(void);

enum
{
	kEnvBackIndex = 4, kEnvBackList, kEnvMaxDepth, kEnvMaxLength, kEnvRunParent,
	kEnvRunProto, kEnvF, kEnvP, kEnvFloatFormat
};

enum
{
	kLitBackIndex, kLitArray, kLitBackList, kLitPrintProto, kLitGetGlobalVar,
	kLitRunProto, kLitPrintParent, kLitRunParent, kLitPrintReal, kLitFloatFormat,
	kLitIsString, kLitDefaultFloatFormat, kLitPrintDepth, kLitMaxDepth, kLitIsNumber,
	kLitDefaultMax, kLitPrintLength, kLitMaxLength, kLitP, kLitPName,
	kLitF, kLitFName, kLitOutOfMem, kLitEvtEx, kLitNoMemory,
	kLitExceptionOccurred
};


static Ref
GlobalSetting(RefArg closure, long name)
{
	RefVar getGlobalVar(NIEGlobalFunction(RefVar(NIELiteral(closure, kLitGetGlobalVar))));
	return NSCall(getGlobalVar, RefVar(NIELiteral(closure, name)));
}


// maxDepth, maxLength: a setting that is not a number, or is negative,
// is 65535
static void
LimitSetting(RefArg closure, RefArg env, long slot, long name)
{
	SetArraySlotRef(env, slot, GlobalSetting(closure, name));
	RefVar isNumber(NIEGlobalFunction(RefVar(NIELiteral(closure, kLitIsNumber))));
	RefVar value(GetArraySlotRef(env, slot));
	if (ISNIL(NSCall(isNumber, value))
	 || NIELessThan(RefVar(GetArraySlotRef(env, slot)), RefVar(MAKEINT(0))))
		SetArraySlotRef(env, slot, MAKEINT(65535));
}


struct PrintState
{
	const RefVar*	env;
	const RefVar*	obj;
	RefVar			result;
};

static void
Print(void* data)
{
	PrintState* ps = (PrintState*) data;
	RefArg env = *ps->env;
	RefVar p(GetArraySlotRef(env, kEnvP));
	RefVar f(GetArraySlotRef(env, kEnvF));
	RefVar printed(NSCall(f, *ps->obj, RefVar(MAKEINT(0))));
	ps->result = NSCall(p, printed);
}


// NIE inetenbl.pkg part 1 +0xc498 _proto.ObjectToString
Ref
NIEObjectToString(RefArg rcvr, RefArg obj, RefArg closure)
{
	TInterpreter* interpreter = GetGInterpreter();
	RefVar env(Clone(closure));
	RefVar self, implementor;
	if (interpreter->IsSend())
	{
		self = interpreter->GetReceiver();
		SetArraySlotRef(env, 1, self);
		implementor = interpreter->GetImplementor();
		SetArraySlotRef(env, 2, implementor);
	}
	else
	{
		self = GetArraySlotRef(env, 1);
		implementor = GetArraySlotRef(env, 2);
	}

	SetArraySlotRef(env, kEnvBackIndex, NILREF);
	RefVar backList(AllocateArray(RefVar(NIELiteral(closure, kLitArray)), 1));
	SetArraySlotRef(env, kEnvBackList, backList);
	SetArraySlotRef(backList, 0, NILREF);
	SetArraySlotRef(env, kEnvRunProto, NOTNIL(GlobalSetting(closure, kLitPrintProto)) ? TRUEREF : NILREF);
	SetArraySlotRef(env, kEnvRunParent, NOTNIL(GlobalSetting(closure, kLitPrintParent)) ? TRUEREF : NILREF);
	SetArraySlotRef(env, kEnvFloatFormat, GlobalSetting(closure, kLitPrintReal));
	if (!IsString(RefVar(GetArraySlotRef(env, kEnvFloatFormat))))
		SetArraySlotRef(env, kEnvFloatFormat, NIELiteral(closure, kLitDefaultFloatFormat));
	LimitSetting(closure, env, kEnvMaxDepth, kLitPrintDepth);
	LimitSetting(closure, env, kEnvMaxLength, kLitPrintLength);
	SetArraySlotRef(env, kEnvP, SetLexScope(RefVar(NIELiteral(closure, kLitP)), env, self, implementor));
	SetArraySlotRef(env, kEnvF, SetLexScope(RefVar(NIELiteral(closure, kLitF)), env, self, implementor));

	PrintState ps;
	ps.env = &env;
	ps.obj = &obj;
	StackState* state = GetStackStateBlock();
	IncrementCurrentStackPos();
	newton_try
	{
		Print(&ps);
		DecrementCurrentStackPos();
		DisposeStackStateBlock(state);
	}
	newton_catch_all
	{
		DecrementCurrentStackPos();
		ClearRefHandles();
		ResetStack(*state);
		DisposeStackStateBlock(state);
		if (Subexception(_info.exception.name, "evt.ex.outofmem"))
		{
			SetArraySlotRef(env, kEnvBackList, NILREF);
			ps.result = NIELiteral(closure, kLitNoMemory);
		}
		else if (Subexception(_info.exception.name, "evt.ex"))
		{
			SetArraySlotRef(env, kEnvBackList, NILREF);
			ps.result = NIELiteral(closure, kLitExceptionOccurred);
		}
		else
			rethrow;
	}
	end_try;
	return ps.result;
}
