/*
	File:		thirdparty/nie/ProtoFSMEvents.cpp

	Contains:	The NIE's protoFSM: posting events to a machine, and the
				smaller helpers around it, re-expressed from its native code.
				In NewtonScript:

					DoEvent: func(event, params) begin
						local ctx := self:?DoEvent_Check('|protoFSM:DoEvent|);
						if ctx then begin
							ctx.pendingEventQueue:EnQueue(event);
							ctx.pendingParamsQueue:EnQueue(params);
							if not ctx.busy then begin
								ctx.busy := true;
								AddDelayedSend(ctx.engineView, 'SetupIdle,
									[ctx.turtle], 1);
							end;
						end;
						nil
					end
					DoUniqueEvent: func(event, params)
						if ctx := self:?DoEvent_Check('|protoFSM:DoUniqueEvent|) then
							if LSearch(ctx.<path>, event, 0, '|=|, nil) = nil then
								self:DoEvent(event, params)
					PeriodicTemplate.viewSetupDoneScript: func()
						:SetupIdle(delay)
					(0x14535): func(s) begin
						if StrEndsWith(s, ", ") then
							StrMunger(s, StrLen(s) - 2, nil, nil, 0, nil);
						s
					end

				(the last is ObjectToString's helper that takes the trailing
				separator off what it has built.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"

int	StrEndsWith(RefArg str, RefArg suffix);		// frames/StringNatives.cpp
void	StrMunger(RefArg s1, long s1start, long s1count, RefArg s2, long s2start, long s2count);	// frames/Munger.cpp


// NIE inetenbl.pkg part 1 +0x29ec _proto.DoEvent
Ref
NIEDoEvent(RefArg rcvr, RefArg event, RefArg params, RefArg closure)
{
	RefVar self(NIESelf(closure));
	RefVar ctx(NSSendIfDefined(self, RefVar(NIELiteral(closure, 1)), RefVar(NIELiteral(closure, 0))));
	if (NOTNIL(ctx))
	{
		NSSend(RefVar(GetFramePath(ctx, RefVar(NIELiteral(closure, 2)))), RefVar(NIELiteral(closure, 3)), event);
		NSSend(RefVar(GetFramePath(ctx, RefVar(NIELiteral(closure, 4)))), RefVar(NIELiteral(closure, 3)), params);
		if (ISNIL(GetFramePath(ctx, RefVar(NIELiteral(closure, 5)))))
		{
			SetFrameSlot(ctx, RefVar(NIELiteral(closure, 5)), RefVar(TRUEREF));
			// the global function is found before its arguments are worked out
			RefVar addDelayedSend(NIEGlobalFunction(RefVar(NIELiteral(closure, 10))));
			RefVar view(GetFramePath(ctx, RefVar(NIELiteral(closure, 6))));
			RefVar args(AllocateArray(RefVar(NIELiteral(closure, 9)), 1));
			SetArraySlot(args, 0, RefVar(GetFramePath(ctx, RefVar(NIELiteral(closure, 8)))));
			NSCall(addDelayedSend, view, RefVar(NIELiteral(closure, 7)), args, RefVar(MAKEINT(1)));
		}
	}
	return NILREF;
}


// NIE inetenbl.pkg part 1 +0xe9f0 _proto.DoUniqueEvent
Ref
NIEDoUniqueEvent(RefArg rcvr, RefArg event, RefArg params, RefArg closure)
{
	RefVar self(NIESelf(closure));
	RefVar ctx(NSSendIfDefined(self, RefVar(NIELiteral(closure, 1)), RefVar(NIELiteral(closure, 0))));
	RefVar result;
	if (NOTNIL(ctx))
	{
		RefVar lsearch(NIEGlobalFunction(RefVar(NIELiteral(closure, 4))));
		RefVar pending(GetFramePath(ctx, RefVar(NIELiteral(closure, 2))));
		RefVar found(NSCall(lsearch, pending, event, RefVar(MAKEINT(0)), RefVar(NIELiteral(closure, 3)), RefVar()));
		if (ISNIL(found))
			result = NSSend(self, RefVar(NIELiteral(closure, 5)), event, params);
	}
	return result;
}


// NIE inetenbl.pkg part 1 +0xdbdc PeriodicTemplate.viewSetupDoneScript
Ref
NIEPeriodicViewSetupDoneScript(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar self(NIESelf(closure));
	RefVar delay(NIEFindVariable(env, RefVar(NIELiteral(closure, 0))));
	return NSSend(self, RefVar(NIELiteral(closure, 1)), delay);
}


// NIE inetenbl.pkg part 1 +0x8670 (0x14535, ObjectToString's trim)
// StrEndsWith and StrMunger are the ROM's C functions (through the glue);
// StrLen is called as a global function.
Ref
NIETrimSeparator(RefArg rcvr, RefArg s, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	if (StrEndsWith(s, RefVar(NIELiteral(closure, 0))))
	{
		RefVar strLen(NIEGlobalFunction(RefVar(NIELiteral(closure, 2))));
		RefVar length(NSCall(strLen, s));
		RefVar start(NIESubtract(length, RefVar(MAKEINT(2))));
		StrMunger(s, RINT(start), -1, RefVar(), 0, -1);
	}
	return s;
}
