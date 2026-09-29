/*
	File:		thirdparty/nie/ProtoFSMPeriodic.cpp

	Contains:	The NIE's protoFSM periodic events: a PeriodicTemplate view,
				a child of the machine's engine view, posts its event every
				`delay` ticks until it has done so `occurrences` times, and
				KillPeriodicEvent takes such a view away.  Re-expressed from
				the native code; in NewtonScript:

					PeriodicTemplate.viewIdleScript: func() begin
						try
							if unique = 'unique then fsm:DoUniqueEvent(event, params)
							else fsm:DoEvent(event, params)
						onexception |evt.ex| do nil;
						try
							if (occurrences := occurrences - 1) > 0 then delay
						onexception |evt.ex| do nil
					end
					KillPeriodicEvent: func(event)
						if ctx := self:?DoEvent_Check('|protoFSM:KillPeriodicEvent|) then begin
							local children := ctx.engineView:?ChildViewFrames();
							if IsArray(children) then begin
								local view := LFetch(children, event, 0, '|=|, 'event);
								if view then RemoveStepView(ctx.engineView, view)
							end
						end

				(an idle script answering nil is not called again, so the
				view goes quiet after its last occurrence.)
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"


// NIE inetenbl.pkg part 1 +0xd5e4 PeriodicTemplate.viewIdleScript

struct PeriodicIdle
{
	const RefVar*	env;
	const RefVar*	closure;
	RefVar		result;
};

static void
PostPeriodicEvent(void* data)
{
	PeriodicIdle* p = (PeriodicIdle*) data;
	RefArg env = *p->env;
	RefArg closure = *p->closure;
	RefVar unique(NIEFindVariable(env, RefVar(NIELiteral(closure, 1))));
	if (NIEEqual(unique, RefVar(NIELiteral(closure, 1))))
	{
		RefVar fsm(NIEFindVariable(env, RefVar(NIELiteral(closure, 4))));
		RefVar event(NIEFindVariable(env, RefVar(NIELiteral(closure, 2))));
		RefVar params(NIEFindVariable(env, RefVar(NIELiteral(closure, 3))));
		NSSend(fsm, RefVar(NIELiteral(closure, 5)), event, params);
	}
	else
	{
		RefVar fsm(NIEFindVariable(env, RefVar(NIELiteral(closure, 4))));
		RefVar event(NIEFindVariable(env, RefVar(NIELiteral(closure, 2))));
		RefVar params(NIEFindVariable(env, RefVar(NIELiteral(closure, 3))));
		NSSend(fsm, RefVar(NIELiteral(closure, 6)), event, params);
	}
}

static void
CountOccurrence(void* data)
{
	PeriodicIdle* p = (PeriodicIdle*) data;
	RefArg env = *p->env;
	RefArg closure = *p->closure;
	RefVar occurrences(NIELiteral(closure, 7));
	RefVar left(NIESubtract(RefVar(NIEFindVariable(env, occurrences)), RefVar(MAKEINT(1))));
	NIESetVariable(env, occurrences, left);
	if (NIEGreaterThan(left, RefVar(MAKEINT(0))))
		p->result = NIEFindVariable(env, RefVar(NIELiteral(closure, 8)));
	else
		p->result = NILREF;
}

Ref
NIEPeriodicViewIdleScript(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	PeriodicIdle p;
	p.env = &env;
	p.closure = &closure;
	NIETryEvtEx(PostPeriodicEvent, &p);
	// (a caught exception leaves the idle script answering nil)
	if (NIETryEvtEx(CountOccurrence, &p))
		p.result = NILREF;
	return p.result;
}


// NIE inetenbl.pkg part 1 +0xde38 _proto.KillPeriodicEvent
Ref
NIEKillPeriodicEvent(RefArg rcvr, RefArg event, RefArg closure)
{
	RefVar self(NIESelf(closure));
	RefVar ctx(NSSendIfDefined(self, RefVar(NIELiteral(closure, 1)), RefVar(NIELiteral(closure, 0))));
	if (ISNIL(ctx))
		return NILREF;
	RefVar isArray(NIEGlobalFunction(RefVar(NIELiteral(closure, 4))));
	RefVar children(NSSendIfDefined(RefVar(GetFramePath(ctx, RefVar(NIELiteral(closure, 2)))), RefVar(NIELiteral(closure, 3))));
	if (ISNIL(NSCall(isArray, children)))
		return NILREF;
	RefVar lfetch(NIEGlobalFunction(RefVar(NIELiteral(closure, 7))));
	RefVar view(NSCall(lfetch, children, event, RefVar(MAKEINT(0)), RefVar(NIELiteral(closure, 5)), RefVar(NIELiteral(closure, 6))));
	if (ISNIL(view))
		return NILREF;
	RefVar removeStepView(NIEGlobalFunction(RefVar(NIELiteral(closure, 8))));
	return NSCall(removeStepView, RefVar(GetFramePath(ctx, RefVar(NIELiteral(closure, 2)))), view);
}
