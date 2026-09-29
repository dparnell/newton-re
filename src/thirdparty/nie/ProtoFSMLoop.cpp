/*
	File:		thirdparty/nie/ProtoFSMLoop.cpp

	Contains:	The NIE's protoFSM engine: DoEvent_Loop, which the engine
				view's idle script runs - it takes the pending event off the
				queue, finds what the current state (and its ancestors) says
				to do with it, does that, moves to the next state, and says
				how long to idle before the next event.  Re-expressed from
				the native code (18980 bytes of it, read with
				tools/newton-rom/analysis/ntknative.py); in NewtonScript:

		func()
		begin
			local newStateFrame, dispose, result, ctx, gotEvent;
		  again:
			ctx := self:?DoEvent_Check('|protoFSM:DoEvent_Loop|);
			if not ctx then return nil;
			gotEvent := nil;
			if ctx.pendingState then begin
				if newStateFrame then begin
					ctx.stateCache := newStateFrame;
					newStateFrame := nil;
				end else if ctx.isNewPendingState then
					ctx.stateCache := :MCollectAncestorStates(ctx.ancestors, ctx.pendingState);
				if ctx.stateCache then begin
					if ctx.eventCache := :MCollectAncestorEvents(ctx.ancestors,
							ctx.pendingState, ctx.pendingEventQueue:Peek()) then
						gotEvent := true
					else if ctx.pendingEventQueue:Peek() <> '|fsm_private_event:Release| then
						:?DebugFSM('UnknownEvent, ctx.pendingState,
							ctx.pendingEventQueue:Peek(), ctx.pendingParamsQueue:Peek());
				end else
					:?DebugFSM('UnknownState, <the same>);
			end else
				:?DebugFSM('NilState, <the same>);

			if not gotEvent then begin
				currentStateFrame := nil;
				currentEventFrame := nil;
				ctx.isNewPendingState := true;
				ctx.pendingEventQueue:DeQueue();
				ctx.pendingParamsQueue:DeQueue();
			end else begin
				ctx.currentState := ctx.pendingState;
				ctx.currentEvent := ctx.pendingEventQueue:DeQueue();
				ctx.currentParams := ctx.pendingParamsQueue:DeQueue();
				if ctx.isNewPendingState then begin
					ctx.isNewPendingState := nil;
					currentStateFrame := {_proto: ctx.stateCache, _parent: self};
					local children := ctx.engineView:?ChildViewFrames();
					if IsArray(children) and Length(children) > 0 then begin
						local scoped := [];
						foreach child in children do
							if child.scope = 'State then AddArraySlot(scoped, child);
						foreach child in scoped do
							RemoveStepView(ctx.engineView, child);
						scoped := nil;
					end;
				end;
				currentEventFrame := {_proto: ctx.eventCache, _parent: currentStateFrame};
				if currentEventFrame.action then begin
					:?TraceFSM('PreAction, ctx.currentState, ctx.currentEvent, ctx.currentParams);
					ctx.level := ctx.level + 1;
					try
						Perform(currentEventFrame, 'action, ctx.currentParams)
					onexception |evt.ex| do
						try :?ExceptionHandler(CurrentException())
						onexception |evt.ex| do nil;
					ctx.level := ctx.level - 1;
					:?TraceFSM('PostAction, ctx.currentState, ctx.currentEvent, ctx.currentParams);
				end;
				if currentEventFrame.nextState then begin
					ctx.pendingState := currentEventFrame.nextState;
					ctx.isNewPendingState := ctx.currentState <> ctx.pendingState;
				end;
				:?TraceFSM('nextState, ctx.pendingState,
					ctx.pendingEventQueue:Peek(), ctx.pendingParamsQueue:Peek());
			end;

			if (ctx.waitView or ctx.Release) and ctx.pendingState
			and newStateFrame := :MCollectAncestorStates(ctx.ancestors, ctx.pendingState) then begin
				dispose := ctx.Release and newStateFrame.terminal;
				if ctx.waitView and newStateFrame.terminal then begin
					ctx.pendingEventQueue:Reset();
					ctx.pendingParamsQueue:Reset();
					AddDelayedCall(func(ctx) if ctx.waitView then ctx.waitView:Close(), [ctx], 1);
				end;
			end;

			ctx.busy := not ctx.pendingEventQueue:IsEmpty();
			if ctx.busy and currentEventFrame and currentEventFrame.nextNoIdle then
				goto again;
			if ctx.busy then ctx.turtle
			else begin
				if dispose then :Dispose();
				nil
			end
		end

				currentStateFrame and currentEventFrame are not locals: they
				are found (and set) as variables of the function's
				environment, the machine's own slots.  newStateFrame and
				dispose live on round the loop: a new state's frame worked
				out at the end of one event becomes the state cache at the
				start of the next.
*/

#include "NIERuntime.h"
#include "NIENatives.h"
#include "Interpreter.h"
#include "ObjectHeap.h"
#include "Frames.h"
#include "NewtonExceptions.h"

Ref FNewIterator(RefArg rcvr, RefArg obj, RefArg deeply);		// frames/Builtins.cpp


enum
{
	kLitName, kLitDoEventCheck, kLitPendingState, kLitStateCache, kLitIsNewPendingState,
	kLitAncestors, kLitMCollectAncestorStates, kLitEventCache, kLitPendingEventQueue, kLitPeek,
	kLitMCollectAncestorEvents, kLitReleaseEvent, kLitUnknownEvent, kLitPendingParamsQueue, kLitDebugFSM,
	kLitUnknownState, kLitNilState, kLitCurrentStateFrame, kLitCurrentEventFrame, kLitDeQueue,
	kLitCurrentState, kLitCurrentEvent, kLitCurrentParams, kLitFrameMap, kLitEngineView,
	kLitChildViewFrames, kLitIsArray, kLitArray, kLitScope, kLitState,
	kLitRemoveStepView, kLitFrameMap2, kLitAction, kLitPreAction, kLitTraceFSM,
	kLitLevel, kLitEvtEx, kLitPerform, kLitCurrentException, kLitExceptionHandler,
	kLitPostAction, kLitNextState, kLitWaitView, kLitRelease, kLitTerminal,
	kLitReset, kLitCloseWaitView, kLitAddDelayedCall, kLitBusy, kLitIsEmpty,
	kLitNextNoIdle, kLitTurtle, kLitDispose
};

struct LoopState
{
	const RefVar*	closure;
	const RefVar*	env;
	const RefVar*	self;
	const RefVar*	ctx;
};

#define LIT(i)		RefVar(NIELiteral(*st->closure, (i)))


static Ref
Lit(const RefVar& closure, long i)
{
	return NIELiteral(closure, i);
}


// ctx.<queue>:<message>()
static Ref
QueueSend(RefArg closure, RefArg ctx, long queue, long message)
{
	return NSSend(RefVar(GetFramePath(ctx, RefVar(Lit(closure, queue)))), RefVar(Lit(closure, message)));
}


// self:?<message>(a1, a2, a3, a4): the arguments are worked out only when
// the method is there (SetupSend's answer is looked at first)
static void
SendIfDefined4(RefArg closure, RefArg self, long message, RefArg a1, RefArg a2, RefArg a3, RefArg a4)
{
	RefVar args(MakeArray(4));
	SetArraySlot(args, 0, a1);
	SetArraySlot(args, 1, a2);
	SetArraySlot(args, 2, a3);
	SetArraySlot(args, 3, a4);
	NSSendWithArgArray(self, RefVar(Lit(closure, message)), args);
}


// :?DebugFSM(why, ctx.pendingState, ctx.pendingEventQueue:Peek(), ctx.pendingParamsQueue:Peek())
static void
DebugFSM(RefArg closure, RefArg self, RefArg ctx, long why)
{
	if (ISNIL(FindImplementor(self, RefVar(Lit(closure, kLitDebugFSM)))))
		return;
	RefVar reason(Lit(closure, why));
	RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
	RefVar event(QueueSend(closure, ctx, kLitPendingEventQueue, kLitPeek));
	RefVar params(QueueSend(closure, ctx, kLitPendingParamsQueue, kLitPeek));
	SendIfDefined4(closure, self, kLitDebugFSM, reason, state, event, params);
}


// :?TraceFSM(what, ctx.currentState, ctx.currentEvent, ctx.currentParams)
static void
TraceAction(RefArg closure, RefArg self, RefArg ctx, long what)
{
	if (ISNIL(FindImplementor(self, RefVar(Lit(closure, kLitTraceFSM)))))
		return;
	RefVar reason(Lit(closure, what));
	RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitCurrentState))));
	RefVar event(GetFramePath(ctx, RefVar(Lit(closure, kLitCurrentEvent))));
	RefVar params(GetFramePath(ctx, RefVar(Lit(closure, kLitCurrentParams))));
	SendIfDefined4(closure, self, kLitTraceFSM, reason, state, event, params);
}


static void
PerformAction(void* data)
{
	LoopState* st = (LoopState*) data;
	RefVar perform(NIEGlobalFunction(LIT(kLitPerform)));
	RefVar eventFrame(NIEFindVariable(*st->env, LIT(kLitCurrentEventFrame)));
	RefVar params(GetFramePath(*st->ctx, LIT(kLitCurrentParams)));
	NSCall(perform, eventFrame, LIT(kLitAction), params);
}

struct HandlerState
{
	LoopState*	loop;
	Exception*	exception;
};

static void
CallExceptionHandler(void* data)
{
	HandlerState* hs = (HandlerState*) data;
	LoopState* st = hs->loop;
	if (ISNIL(FindImplementor(*st->self, LIT(kLitExceptionHandler))))
		return;
	RefVar exception(GetGInterpreter()->TranslateException(hs->exception));
	NSSend(*st->self, LIT(kLitExceptionHandler), exception);
}

static void
ActionFailed(void* data, Exception* exception)
{
	HandlerState hs;
	hs.loop = (LoopState*) data;
	hs.exception = exception;
	NIETryEvtEx(CallExceptionHandler, &hs);
}


// NIE inetenbl.pkg part 1 +0x2ff8 _proto.DoEvent_Loop
Ref
NIEDoEventLoop(RefArg rcvr, RefArg closure)
{
	RefVar env(NIEEnvironment(closure));
	RefVar self(NIESelf(closure));
	RefVar implementor(GetGInterpreter()->IsSend() ? GetGInterpreter()->GetImplementor() : GetArraySlotRef(closure, 2));
	RefVar newStateFrame;			// (these two live on round the loop)
	RefVar dispose;
	RefVar result;
	RefVar ctx;
	LoopState st;
	st.closure = &closure;
	st.env = &env;
	st.self = &self;
	st.ctx = &ctx;

	for (;;)
	{
		ctx = NSSendIfDefined(self, RefVar(Lit(closure, kLitDoEventCheck)), RefVar(Lit(closure, kLitName)));
		if (ISNIL(ctx))
			return NILREF;
		RefVar gotEvent;
		if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState)))))
		{
			if (NOTNIL(newStateFrame))
			{
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitStateCache)), newStateFrame);
				newStateFrame = NILREF;
			}
			else if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitIsNewPendingState)))))
			{
				RefVar ancestors(GetFramePath(ctx, RefVar(Lit(closure, kLitAncestors))));
				RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
				RefVar states(NSSend(self, RefVar(Lit(closure, kLitMCollectAncestorStates)), ancestors, state));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitStateCache)), states);
			}
			if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitStateCache)))))
			{
				RefVar ancestors(GetFramePath(ctx, RefVar(Lit(closure, kLitAncestors))));
				RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
				RefVar event(QueueSend(closure, ctx, kLitPendingEventQueue, kLitPeek));
				RefVar events(NSSend(self, RefVar(Lit(closure, kLitMCollectAncestorEvents)), ancestors, state, event));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitEventCache)), events);
				if (NOTNIL(events))
					gotEvent = TRUEREF;
				else if (NIENotEqual(RefVar(QueueSend(closure, ctx, kLitPendingEventQueue, kLitPeek)), RefVar(Lit(closure, kLitReleaseEvent))))
					DebugFSM(closure, self, ctx, kLitUnknownEvent);
			}
			else
				DebugFSM(closure, self, ctx, kLitUnknownState);
		}
		else
			DebugFSM(closure, self, ctx, kLitNilState);

		if (ISNIL(gotEvent))
		{
			NIESetVariable(env, RefVar(Lit(closure, kLitCurrentStateFrame)), RefVar());
			NIESetVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)), RefVar());
			SetFrameSlot(ctx, RefVar(Lit(closure, kLitIsNewPendingState)), RefVar(TRUEREF));
			QueueSend(closure, ctx, kLitPendingEventQueue, kLitDeQueue);
			QueueSend(closure, ctx, kLitPendingParamsQueue, kLitDeQueue);
		}
		else
		{
			SetFrameSlot(ctx, RefVar(Lit(closure, kLitCurrentState)), RefVar(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState)))));
			SetFrameSlot(ctx, RefVar(Lit(closure, kLitCurrentEvent)), RefVar(QueueSend(closure, ctx, kLitPendingEventQueue, kLitDeQueue)));
			SetFrameSlot(ctx, RefVar(Lit(closure, kLitCurrentParams)), RefVar(QueueSend(closure, ctx, kLitPendingParamsQueue, kLitDeQueue)));
			if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitIsNewPendingState)))))
			{
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitIsNewPendingState)), RefVar());
				RefVar stateFrame(AllocateFrameWithMap(RefVar(Lit(closure, kLitFrameMap))));
				SetArraySlotRef(stateFrame, 0, GetFramePath(ctx, RefVar(Lit(closure, kLitStateCache))));
				SetArraySlotRef(stateFrame, 1, self);
				NIESetVariable(env, RefVar(Lit(closure, kLitCurrentStateFrame)), stateFrame);

				RefVar isArray(NIEGlobalFunction(RefVar(Lit(closure, kLitIsArray))));
				RefVar children(NSSendIfDefined(RefVar(GetFramePath(ctx, RefVar(Lit(closure, kLitEngineView)))), RefVar(Lit(closure, kLitChildViewFrames))));
				if (NOTNIL(NSCall(isArray, children)) && Length(children) > 0)
				{
					RefVar scoped(AllocateArray(RefVar(Lit(closure, kLitArray)), 0));
					RefVar iter(FNewIterator(RefVar(), children, RefVar()));
					while (!ForEachLoopDone(iter))
					{
						RefVar child(GetArraySlotRef(iter, 1));
						if (NIEEqual(RefVar(GetFramePath(child, RefVar(Lit(closure, kLitScope)))), RefVar(Lit(closure, kLitState))))
							AddArraySlot(scoped, child);
						ForEachLoopNext(iter);
					}
					iter = FNewIterator(RefVar(), scoped, RefVar());
					while (!ForEachLoopDone(iter))
					{
						RefVar child(GetArraySlotRef(iter, 1));
						RefVar removeStepView(NIEGlobalFunction(RefVar(Lit(closure, kLitRemoveStepView))));
						RefVar engineView(GetFramePath(ctx, RefVar(Lit(closure, kLitEngineView))));
						NSCall(removeStepView, engineView, child);
						ForEachLoopNext(iter);
					}
					scoped = NILREF;
				}
			}

			RefVar eventFrame(AllocateFrameWithMap(RefVar(Lit(closure, kLitFrameMap))));
			SetArraySlotRef(eventFrame, 0, GetFramePath(ctx, RefVar(Lit(closure, kLitEventCache))));
			SetArraySlotRef(eventFrame, 1, NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentStateFrame))));
			NIESetVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)), eventFrame);

			if (NOTNIL(GetFramePath(RefVar(NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)))), RefVar(Lit(closure, kLitAction)))))
			{
				TraceAction(closure, self, ctx, kLitPreAction);
				RefVar level(NIEAdd(RefVar(GetFramePath(ctx, RefVar(Lit(closure, kLitLevel)))), RefVar(MAKEINT(1))));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitLevel)), level);
				NIETryEvtEx(PerformAction, ActionFailed, &st);
				// NIE BUG (kept): an action that disposes of its own machine
				// (the link manager's CleanUp, when its last link goes) has
				// emptied the context by now, and level - 1 of its nil level
				// throws -48404 "not a number" out of DoEvent_Loop; the NIE's
				// own ARM does the same (the CPU oracle: NEWTON_NIE_ON_CPU
				// with demo/inetfsm.ns)
				level = NIESubtract(RefVar(GetFramePath(ctx, RefVar(Lit(closure, kLitLevel)))), RefVar(MAKEINT(1)));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitLevel)), level);
				TraceAction(closure, self, ctx, kLitPostAction);
			}

			if (NOTNIL(GetFramePath(RefVar(NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)))), RefVar(Lit(closure, kLitNextState)))))
			{
				RefVar next(GetFramePath(RefVar(NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)))), RefVar(Lit(closure, kLitNextState))));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitPendingState)), next);
				RefVar current(GetFramePath(ctx, RefVar(Lit(closure, kLitCurrentState))));
				RefVar pending(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
				SetFrameSlot(ctx, RefVar(Lit(closure, kLitIsNewPendingState)), RefVar(NIENotEqual(current, pending) ? TRUEREF : NILREF));
			}

			if (NOTNIL(FindImplementor(self, RefVar(Lit(closure, kLitTraceFSM)))))
			{
				RefVar reason(Lit(closure, kLitNextState));
				RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
				RefVar event(QueueSend(closure, ctx, kLitPendingEventQueue, kLitPeek));
				RefVar params(QueueSend(closure, ctx, kLitPendingParamsQueue, kLitPeek));
				SendIfDefined4(closure, self, kLitTraceFSM, reason, state, event, params);
			}
		}

		if ((NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitWaitView))))
		  || NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitRelease)))))
		 && NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState)))))
		{
			RefVar ancestors(GetFramePath(ctx, RefVar(Lit(closure, kLitAncestors))));
			RefVar state(GetFramePath(ctx, RefVar(Lit(closure, kLitPendingState))));
			newStateFrame = NSSend(self, RefVar(Lit(closure, kLitMCollectAncestorStates)), ancestors, state);
			if (NOTNIL(newStateFrame))
			{
				if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitRelease))))
				 && NOTNIL(GetFramePath(newStateFrame, RefVar(Lit(closure, kLitTerminal)))))
					dispose = TRUEREF;
				else
					dispose = NILREF;
				if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitWaitView))))
				 && NOTNIL(GetFramePath(newStateFrame, RefVar(Lit(closure, kLitTerminal)))))
				{
					QueueSend(closure, ctx, kLitPendingEventQueue, kLitReset);
					QueueSend(closure, ctx, kLitPendingParamsQueue, kLitReset);
					RefVar addDelayedCall(NIEGlobalFunction(RefVar(Lit(closure, kLitAddDelayedCall))));
					RefVar closeWaitView(SetLexScope(RefVar(Lit(closure, kLitCloseWaitView)), env, self, implementor));
					RefVar args(AllocateArray(RefVar(Lit(closure, kLitArray)), 1));
					SetArraySlot(args, 0, ctx);
					NSCall(addDelayedCall, closeWaitView, args, RefVar(MAKEINT(1)));
				}
			}
		}

		RefVar empty(QueueSend(closure, ctx, kLitPendingEventQueue, kLitIsEmpty));
		SetFrameSlot(ctx, RefVar(Lit(closure, kLitBusy)), RefVar(ISNIL(empty) ? TRUEREF : NILREF));
		if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitBusy))))
		 && NOTNIL(NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame))))
		 && NOTNIL(GetFramePath(RefVar(NIEFindVariable(env, RefVar(Lit(closure, kLitCurrentEventFrame)))), RefVar(Lit(closure, kLitNextNoIdle)))))
			continue;
		break;
	}

	if (NOTNIL(GetFramePath(ctx, RefVar(Lit(closure, kLitBusy)))))
		result = GetFramePath(ctx, RefVar(Lit(closure, kLitTurtle)));
	else
	{
		if (NOTNIL(dispose))
			NSSend(self, RefVar(Lit(closure, kLitDispose)));
		result = NILREF;
	}
	return result;
}
