/*
	File:		thirdparty/nie/NIERuntime.h

	Contains:	What the Newton Internet Enabler's native-compiled
				NewtonScript does in the runtime routines at the front of its
				code binary (inetenbl.pkg, the binary at file offset 0x2f88,
				61080 bytes, FNV-1a 0x16d859a2), for the host re-expressions of
				its functions (thirdparty/nie/*.cpp, registered through
				frames/PackageNatives.h).

				NTK compiles a native function so that it:
				  - takes its literals from its closure's fourth slot (the
				    closure is an argument frame: _nextArgFrame, _parent,
				    _implementor, _literals, then the function's locals);
				  - makes its environment a clone of the closure, with the
				    interpreter's receiver and implementor put in slots 1 and
				    2 when it was called by a message send (IsSend);
				  - finds a variable (a symbol that is not a local) in that
				    environment - the locals chain, then _parent/_proto - and
				    then as a global, throwing "undefined variable" when it is
				    neither (the routine at +0x171c);
				  - does aref, <, +, ... inline for integers and through the
				    ROM's FAref, FLessThan, FAdd, ... otherwise (+0x1a2c on).

				Here each is one host function doing what the routine does on
				a 2.x ROM (the routines' 1.x paths are not taken there).

	The citations are `NIE inetenbl.pkg part 1 +0x<offset in the code
	binary> name`: coverage.py checks only ROM citations.
*/

#ifndef __NIERUNTIME_H
#define __NIERUNTIME_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

// The NIE's native code binary, as frames/PackageNatives.h keys it.
const ULong kNIECodeLength = 61080;
const ULong kNIECodeHash = 0x16d859a2;

// A native function's literal i (its closure's literals array).
Ref		NIELiteral(RefArg closure, long i);

// A native function's environment: its closure cloned, the receiver and
// implementor put in when it was sent a message.
Ref		NIEEnvironment(RefArg closure);

// `self` in a native function: the receiver when it was sent a message,
// else its closure's _parent.
Ref		NIESelf(RefArg closure);

// A variable found in the environment, then among the globals (else
// "undefined variable", kNSErrUndefinedVariable with the symbol).
Ref		NIEFindVariable(RefArg env, RefArg symbol);

// A global function (else "undefined global function").
Ref		NIEGlobalFunction(RefArg symbol);

// aref, as the native code does it (FAref).
Ref		NIEAref(RefArg obj, RefArg index);

// a - b, as the native code does it: integers inline, else FSubtract.
Ref		NIESubtract(RefArg a, RefArg b);

// a = b and a > b as the native code tests them: integers inline, else
// FEqual/FGreaterThan (true unless they answer nil).
bool		NIEEqual(RefArg a, RefArg b);
bool		NIEGreaterThan(RefArg a, RefArg b);

// An assignment to a variable that is not a local (SetVariableOrGlobal).
void		NIESetVariable(RefArg env, RefArg symbol, RefArg value);

// `try body onexception |evt.ex| do ...`: the body run with the
// interpreter's stacks saved, put back if anything is thrown; an evt.ex
// exception (or one under it) is caught - answering true - and anything else
// goes on to the next handler.
bool		NIETryEvtEx(void (*body)(void*), void* data);

#endif
