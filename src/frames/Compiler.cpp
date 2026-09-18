/*
	File:		frames/Compiler.cpp

	Contains:	The NewtonScript compiler's code generation (Compiler.h):
				TCompiler, TFunctionState and TLoopState, the parse tree
				walks (declarations, closures, code) and the bytecode
				emitters; ParseString, ParseFile and the natives.

	Host: a nested function's TFunctionState is kept in its parse tree
	node (slot 5) as the ROM keeps it - the pointer as a Ref, which the
	collector takes for an integer since pointers are aligned.
*/

#include "Compiler.h"
#include "ParserTables.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "NSErrors.h"
#include "OSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

long	gCompilerCompatibility = 0;		// 0x0c1022e0
Ref		gFreqFuncNames = NILREF;		// 0x0c1022e4
Ref		gConstFuncFrame = NILREF;		// 0x0c1023b8
long	gPrintLiteralsFlag = 0;			// 0x0c1023c0 gPrintLiterals
static Boolean	gCompilerInited = false;	// 0x0c1023bc

extern Ref		gCodeBlockPrototype;		// Interpreter.cpp
extern Ref		gDebugCodeBlockPrototype;
extern const ExceptionName exCompilerWithFrameData;	// "evt.ex.fr.comp;type.ref.frame"

// a nested function's state, in its parse tree node
static inline Ref		StateToRef(TFunctionState* state)	{ return (Ref) (size_t) state; }
static inline TFunctionState*	RefToState(Ref r)		{ return (TFunctionState*) (size_t) r; }
#define ISMAGICPTR(r)	(RTAG(r) == kTagMagicPtr)


/* -------------------------------------------------------------------------------
	The parse tree
------------------------------------------------------------------------------- */

// ROM 0x002bf984 AllocatePT1__FiRC6RefVar
Ref
AllocatePT1(int kind, RefArg a)
{
	RefVar node(AllocateArray(RSSYMarray, 2));
	SetArraySlotRef(node, 0, MAKEINT(kind));
	SetArraySlotRef(node, 1, a);
	return node;
}


// ROM 0x002c010c AllocatePT2__FiRC6RefVarT2
Ref
AllocatePT2(int kind, RefArg a, RefArg b)
{
	RefVar node(AllocateArray(RSSYMarray, 3));
	SetArraySlotRef(node, 0, MAKEINT(kind));
	SetArraySlotRef(node, 1, a);
	SetArraySlotRef(node, 2, b);
	return node;
}


// ROM 0x002c0a5c AllocatePT3__FiRC6RefVarN22
Ref
AllocatePT3(int kind, RefArg a, RefArg b, RefArg c)
{
	RefVar node(AllocateArray(RSSYMarray, 4));
	SetArraySlotRef(node, 0, MAKEINT(kind));
	SetArraySlotRef(node, 1, a);
	SetArraySlotRef(node, 2, b);
	SetArraySlotRef(node, 3, c);
	return node;
}


// ROM 0x002c1d90 AllocatePT5__FiRC6RefVarN42
Ref
AllocatePT5(int kind, RefArg a, RefArg b, RefArg c, RefArg d, RefArg e)
{
	RefVar node(AllocateArray(RSSYMarray, 6));
	SetArraySlotRef(node, 0, MAKEINT(kind));
	SetArraySlotRef(node, 1, a);
	SetArraySlotRef(node, 2, b);
	SetArraySlotRef(node, 3, c);
	SetArraySlotRef(node, 4, d);
	SetArraySlotRef(node, 5, e);
	return node;
}


// a node's kind and its children (nil past the end)
static long
NodeParts(RefArg node, RefVar& a1, RefVar& a2, RefVar& a3, RefVar& a4, RefVar& a5)
{
	long kind = RINT(GetArraySlotRef(node, 0));
	long count = Length(node) - 1;
	a1 = count >= 1 ? GetArraySlotRef(node, 1) : NILREF;
	a2 = count >= 2 ? GetArraySlotRef(node, 2) : NILREF;
	a3 = count >= 3 ? GetArraySlotRef(node, 3) : NILREF;
	a4 = count >= 4 ? GetArraySlotRef(node, 4) : NILREF;
	a5 = count >= 5 ? GetArraySlotRef(node, 5) : NILREF;
	return kind;
}

static inline long
NodeKind(RefArg node)
{
	return RINT(GetArraySlotRef(node, 0));
}


// ROM 0x002c6858 WalkNodes__FRC6RefVarPvPFPvRC6RefVarlN52_ii
// Every node of the tree to the walker, before its children (postOrder
// false; a walker answering false stops the descent) or after them.
void
WalkNodes(RefArg node, void* context, WalkerProc walker, Boolean postOrder)
{
	RefVar a1, a2, a3, a4, a5;
	long kind = NodeParts(node, a1, a2, a3, a4, a5);
	if (!postOrder && !walker(context, node, kind, a1, a2, a3, a4, a5))
		return;
	switch (kind)
	{
	case tokenCALL:
	case tokenINVOKE:
	{
		// the arguments (child 2), then for invoke the function (child 1)
		long count = Length(a2);
		for (long i = 0; i < count; i++)
			WalkNodes(RefVar(GetArraySlotRef(a2, i)), context, walker, postOrder);
		if (kind == tokenINVOKE)
			WalkNodes(a1, context, walker, postOrder);
		break;
	}
	case tokenBEGIN:
	case tokenBUILDARRAY:
	{
		long count = Length(a1);
		for (long i = 0; i < count; i++)
			WalkNodes(RefVar(GetArraySlotRef(a1, i)), context, walker, postOrder);
		break;
	}
	case tokenGLOBAL:
		WalkNodes(a2, context, walker, postOrder);
		break;
	case tokenIF:
		WalkNodes(a1, context, walker, postOrder);
		WalkNodes(a2, context, walker, postOrder);
		if ((Ref) a3 != NILREF)
			WalkNodes(a3, context, walker, postOrder);
		break;
	case tokenTRY:
	{
		WalkNodes(a1, context, walker, postOrder);
		long count = Length(a2);
		for (long i = 0; i < count; i++)
		{
			RefVar handler(GetArraySlotRef(a2, i));
			WalkNodes(RefVar(GetArraySlotRef(handler, 2)), context, walker, postOrder);
		}
		break;
	}
	case tokenBUILDFRAME:
	{
		TObjectIterator iter(a1);
		for (; !iter.Done(); iter.Next())
			WalkNodes(iter.fValue, context, walker, postOrder);
		break;
	}
	case tokenLOCAL:
	{
		// the initialisers (odd elements)
		long count = Length(a1);
		for (long i = 1; i < count; i += 2)
		{
			RefVar init(GetArraySlotRef(a1, i));
			if ((Ref) init != NILREF)
				WalkNodes(init, context, walker, postOrder);
		}
		break;
	}
	case tokenLOOP:
	case tokenRETURN:
	case tokenBREAK:
	case tokenNOT:
	case tokenUMINUS:
		WalkNodes(a1, context, walker, postOrder);
		break;
	case tokenFOR:
		WalkNodes(a2, context, walker, postOrder);
		WalkNodes(a3, context, walker, postOrder);
		WalkNodes(a4, context, walker, postOrder);
		WalkNodes(a5, context, walker, postOrder);
		break;
	case tokenREPEAT:
	{
		long count = Length(a1);
		for (long i = 0; i < count; i++)
			WalkNodes(RefVar(GetArraySlotRef(a1, i)), context, walker, postOrder);
		WalkNodes(a2, context, walker, postOrder);
		break;
	}
	case tokenFOREACH:
		WalkNodes(a2, context, walker, postOrder);
		WalkNodes(a3, context, walker, postOrder);
		break;
	case tokenASSIGN:
	{
		long lvalueKind = NodeKind(a1);
		if (lvalueKind == tokenSYMBOL)
			WalkNodes(a2, context, walker, postOrder);
		else if (lvalueKind == '.' || lvalueKind == '[')
		{
			WalkNodes(RefVar(GetArraySlotRef(a1, 1)), context, walker, postOrder);
			WalkNodes(RefVar(GetArraySlotRef(a1, 2)), context, walker, postOrder);
			WalkNodes(a2, context, walker, postOrder);
		}
		break;
	}
	case tokenEXISTS:
	{
		long exprKind = NodeKind(a1);
		if (exprKind == '.')
		{
			WalkNodes(RefVar(GetArraySlotRef(a1, 1)), context, walker, postOrder);
			WalkNodes(RefVar(GetArraySlotRef(a1, 2)), context, walker, postOrder);
		}
		else if (exprKind == ':')
			WalkNodes(RefVar(GetArraySlotRef(a1, 2)), context, walker, postOrder);
		break;
	}
	case ':':
	case tokenSENDIFDEFINED:
	{
		// the arguments, then the receiver
		long count = Length(a3);
		for (long i = 0; i < count; i++)
			WalkNodes(RefVar(GetArraySlotRef(a3, i)), context, walker, postOrder);
		if ((Ref) a2 != NILREF)
			WalkNodes(a2, context, walker, postOrder);
		break;
	}
	case tokenWHILE:
	case '&': case '*': case '+': case '-': case '.': case '/': case '<': case '>': case '[':
	case tokenAMPERAMPER: case tokenDIV: case tokenMOD: case tokenLSHIFT: case tokenRSHIFT:
	case tokenLEQ: case tokenGEQ: case tokenEQL: case tokenNEQ: case tokenAND: case tokenOR:
		WalkNodes(a1, context, walker, postOrder);
		WalkNodes(a2, context, walker, postOrder);
		break;
	default:
		break;
	}
	if (postOrder)
		walker(context, node, kind, a1, a2, a3, a4, a5);
}


/* -------------------------------------------------------------------------------
	TLoopState
------------------------------------------------------------------------------- */

// ROM 0x002bfbcc __ct__10TLoopStateFP14TFunctionStateP10TLoopState
TLoopState::TLoopState(TFunctionState* function, TLoopState* enclosing)
{
	fFunction = function;
	fExits = AllocateArray(RSSYMarray, 0);
	fEnclosing = enclosing;
}


// ROM 0x002bfc28 __dt__10TLoopStateFv
TLoopState::~TLoopState()
{
	if (fEnclosing != nil)
		delete fEnclosing;
}


// ROM 0x002bfc68 AddExit__10TLoopStateFl
void
TLoopState::AddExit(long pc)
{
	AddArraySlot(fExits, RefVar(MAKEINT(pc)));
}


// ROM 0x002bfca0 PatchExits__10TLoopStateFl
void
TLoopState::PatchExits(long pc)
{
	long count = Length(fExits);
	for (long i = 0; i < count; i++)
		fFunction->Backpatch(RINT(GetArraySlotRef(fExits, i)), kBCBranch, pc);
}


/* -------------------------------------------------------------------------------
	TFunctionState
------------------------------------------------------------------------------- */

// ROM 0x002bfd1c __ct__14TFunctionStateFP9TCompilerRC6RefVarP14TFunctionStatePi
TFunctionState::TFunctionState(TCompiler* compiler, RefArg args, TFunctionState* enclosing, int* funcDepth)
{
	fCompiler = compiler;
	fArgs = args;
	fLocals = NILREF;
	fInstructions = AllocateBinary(RSSYMinstructions, 0x80);
	fLiterals = AllocateArray(RSSYMliterals, 0x10);
	fArgFrame = NILREF;
	fClosedVars = AllocateFrame();
	fVarLocs = NILREF;
	fPC = 0;
	fEnclosing = enclosing;
	fNext = nil;
	fNumLiterals = 0;
	fNumLocals = 0;
	fNumArgs = 0;
	fNumLocalsDeclared = 0;
	fLoop = nil;
	if (funcDepth == nil)
		fFuncDepth = -1;
	else
		fFuncDepth = (*funcDepth)++;
	fHasClosures = false;
	fUsesReceiver = false;
	fUsesImplementor = false;
	fClosesOverOuter = false;
	if (enclosing == nil)
		fConstants = gConstantsFrame;
	else
	{
		fConstants = AllocateFrame();
		SetFrameSlot(fConstants, RSSYM_proto, enclosing->fConstants);
		enclosing->fHasClosures = true;
	}
	// the variable names are kept unless dbgnovarnames is set (dbgkeepvarnames overrides)
	if (FrameHasSlotRef(gVarFrame, Intern((char*) "dbgnovarnames")))
		fKeepVarNames = GetFrameSlotRef(gVarFrame, Intern((char*) "dbgnovarnames")) == NILREF;
	else if (FrameHasSlotRef(gVarFrame, Intern((char*) "dbgkeepvarnames")))
		fKeepVarNames = GetFrameSlotRef(gVarFrame, Intern((char*) "dbgkeepvarnames")) != NILREF;
	else
		fKeepVarNames = true;
}


// ROM 0x002bff94 __dt__14TFunctionStateFv
TFunctionState::~TFunctionState()
{
	if (fLoop != nil)
		delete fLoop;
}


// ROM 0x002c001c CurPC__14TFunctionStateFv
long
TFunctionState::CurPC(void)
{
	return fPC;
}


// ROM 0x002c0024 LitOffset__14TFunctionStateFRC6RefVar
// The index of a literal, added when new (magic pointers by identity).
long
TFunctionState::LitOffset(RefArg literal)
{
	Ref value = literal;
	for (long i = 0; i < fNumLiterals; i++)
	{
		Ref lit = GetArraySlotRef(fLiterals, i);
		if (ISMAGICPTR(lit) || ISMAGICPTR(value))
		{
			if (lit == value)
				return i;
		}
		else if (EQRef(lit, value))
			return i;
	}
	if (Length(fLiterals) <= fNumLiterals)
		SetLength(fLiterals, Length(fLiterals) + 0x10);
	SetArraySlotRef(fLiterals, fNumLiterals, value);
	return fNumLiterals++;
}


// ROM 0x002c019c EmitOne__14TFunctionStateFUc
void
TFunctionState::EmitOne(unsigned char b)
{
	if (Length(fInstructions) < fPC + 1)
		SetLength(fInstructions, Length(fInstructions) + 0x80);
	((unsigned char*) BinaryData(fInstructions))[fPC++] = b;
}


// ROM 0x002c0210 EmitThree__14TFunctionStateFUcl
void
TFunctionState::EmitThree(unsigned char b, long operand)
{
	if (Length(fInstructions) < fPC + 3)
		SetLength(fInstructions, Length(fInstructions) + 0x80);
	unsigned char* code = (unsigned char*) BinaryData(fInstructions) + fPC;
	code[0] = (unsigned char) (b << 3 | 7);
	code[1] = (unsigned char) (operand >> 8);
	code[2] = (unsigned char) operand;
	fPC += 3;
}


// ROM 0x002c0294 Emit__14TFunctionStateF6Opcodel
// An instruction: the short form for operands 0-6, else the 16-bit one.
void
TFunctionState::Emit(int opcode, long operand)
{
	if (operand >= 0 && operand < 7)
		EmitOne((unsigned char) (opcode << 3 | operand));
	else
		EmitThree((unsigned char) opcode, operand);
}


// ROM 0x002c02bc EmitPlaceholder__14TFunctionStateFv
// A branch to be patched; ==> its pc.
long
TFunctionState::EmitPlaceholder(void)
{
	long pc = CurPC();
	EmitThree(kBCBranch, 0);
	return pc;
}


// ROM 0x002c02ec Backpatch__14TFunctionStateFl6OpcodeT1
void
TFunctionState::Backpatch(long pc, int opcode, long operand)
{
	unsigned char* code = (unsigned char*) BinaryData(fInstructions) + pc;
	code[0] = (unsigned char) (opcode << 3 | 7);
	code[1] = (unsigned char) (operand >> 8);
	code[2] = (unsigned char) operand;
}


// ROM 0x002c032c AddLocals__14TFunctionStateFRC6RefVar
// Local variables declared; a constant's name cannot be one.
void
TFunctionState::AddLocals(RefArg names)
{
	if ((Ref) fLocals == NILREF)
	{
		fLocals = names;
		return;
	}
	RefVar name;
	long count = Length(names);
	for (long i = 0; i < count; i++)
	{
		name = GetArraySlotRef(names, i);
		if (IsConstant(name))
			fCompiler->Error(kNSErrLocalIsConstant, name);
		if (ArrayPosition(fLocals, name, 0, RefVar(NILREF)) == -1)
			AddArraySlot(fLocals, name);
	}
}


// ROM 0x002c0438 DeclarationsFinished__14TFunctionStateFv
void
TFunctionState::DeclarationsFinished(void)
{
	fNumArgs = (Ref) fArgs == NILREF ? 0 : Length(fArgs);
	fNumLocalsDeclared = (Ref) fLocals == NILREF ? 0 : Length(fLocals);
}


// ROM 0x002c0488 ComputeInitialVarLocs__14TFunctionStateFv
// Every argument and local starts at location 0 (a reference count).
void
TFunctionState::ComputeInitialVarLocs(void)
{
	fVarLocs = AllocateFrame();
	for (long i = 0; i < fNumArgs; i++)
		SetFrameSlot(fVarLocs, RefVar(GetArraySlotRef(fArgs, i)), RefVar(MAKEINT(0)));
	for (long i = 0; i < fNumLocalsDeclared; i++)
		SetFrameSlot(fVarLocs, RefVar(GetArraySlotRef(fLocals, i)), RefVar(MAKEINT(0)));
}


// ROM 0x002c0594 ComputeArgFrame__14TFunctionStateFv
// The function's argFrame and where each variable lives.  A 1.x code
// block (compatibility 0) keeps every argument and local in the argFrame
// [_nextArgFrame, _parent, _implementor, args..., locals...].  A 2.x
// function keeps arguments and locals on the value stack (fVarLocs:
// name -> stack index, arguments from 3) and only the variables inner
// functions close over in the argFrame ('closed in fVarLocs), and has no
// argFrame at all when nothing is closed over and self, inherited and no
// enclosing variable are used; _parent and _implementor are 0 when self
// and inherited are not used, so the interpreter need not fill them.
void
TFunctionState::ComputeArgFrame(void)
{
	if (gCompilerCompatibility == 0)
	{
		RefVar tags(AllocateArray(RSSYMarray, fNumArgs + fNumLocalsDeclared + 3));
		SetArraySlotRef(tags, 0, RSSYM_nextargframe);
		SetArraySlotRef(tags, 1, RSSYM_parent);
		SetArraySlotRef(tags, 2, RSSYM_implementor);
		if (fNumArgs != 0)
			ArrayMunger(tags, 3, fNumArgs, fArgs, 0, fNumArgs);
		if (fNumLocalsDeclared != 0)
			ArrayMunger(tags, fNumArgs + 3, fNumLocalsDeclared, fLocals, 0, fNumLocalsDeclared);
		RefVar map(AllocateMapWithTags(RefVar(NILREF), tags));
		fArgFrame = AllocateFrameWithMap(map);
		return;
	}

	long stackIndex = 3;
	long frameIndex = 3;
	RefVar tags(AllocateArray(RSSYMarray, fNumArgs + fNumLocalsDeclared + 3));
	RefVar name;
	// the arguments take the stack slots 3 on (nil for now: the loop below skips them)
	for (long i = 0; i < fNumArgs; i++)
	{
		name = GetArraySlotRef(fArgs, i);
		if (ISINT(GetFrameSlotRef(fVarLocs, name)))
			SetFrameSlot(fVarLocs, name, RefVar(NILREF));
		stackIndex++;
	}
	{
		TObjectIterator iter(fVarLocs);
		for (; !iter.Done(); iter.Next())
		{
			if (ISINT(iter.fValue))
				SetFrameSlot(fVarLocs, iter.fTag, RefVar(MAKEINT(stackIndex++)));
			else if ((Ref) iter.fValue != NILREF)
				SetArraySlotRef(tags, frameIndex++, iter.fTag);
		}
	}
	for (long i = 0; i < fNumArgs; i++)
	{
		name = GetArraySlotRef(fArgs, i);
		if (GetFrameSlotRef(fVarLocs, name) == NILREF)
			SetFrameSlot(fVarLocs, name, RefVar(MAKEINT(i + 3)));
	}
	if (frameIndex >= 4 || (!AtTopLevel() && (fUsesReceiver || fUsesImplementor || fClosesOverOuter)))
	{
		SetLength(tags, frameIndex);
		SetArraySlotRef(tags, 0, RSSYM_nextargframe);
		SetArraySlotRef(tags, 1, RSSYM_parent);
		SetArraySlotRef(tags, 2, RSSYM_implementor);
		RefVar map(AllocateMapWithTags(RefVar(NILREF), tags));
		fArgFrame = AllocateFrameWithMap(map);
		if (!fUsesReceiver && !AtTopLevel())
			SetArraySlotRef(fArgFrame, 1, 0);
		if (!fUsesImplementor && !AtTopLevel())
			SetArraySlotRef(fArgFrame, 2, 0);
	}
	fNumLocals = stackIndex - 3 - fNumArgs;
}


// ROM 0x002c0b04 CopyClosedArgs__14TFunctionStateFv
// The arguments inner functions close over are copied from the stack
// into the argFrame at entry.
void
TFunctionState::CopyClosedArgs(void)
{
	long count = Length(fArgs);
	RefVar name;
	for (long i = 0; i < count; i++)
	{
		name = GetArraySlotRef(fArgs, i);
		if (VariableIndex(name) == -1)
		{
			long position = ArrayPosition(fArgs, name, 0, RefVar(NILREF));
			Emit(kBCGetVar, position + 3);
			Emit(kBCFindAndSetVar, LitOffset(name));
		}
	}
}


// ROM 0x002c0bf0 IsLocalVariable__14TFunctionStateFRC6RefVar
Boolean
TFunctionState::IsLocalVariable(RefArg name)
{
	if ((Ref) fLocals != NILREF && ArrayPosition(fLocals, name, 0, RefVar(NILREF)) != -1)
		return true;
	if ((Ref) fArgs != NILREF && ArrayPosition(fArgs, name, 0, RefVar(NILREF)) != -1)
		return true;
	return false;
}


// ROM 0x002c0c8c VariableIndex__14TFunctionStateFRC6RefVar
// The variable's index on the stack (its argFrame slot for a 1.x code
// block); -1 when it lives in the argFrame.
long
TFunctionState::VariableIndex(RefArg name)
{
	if (gCompilerCompatibility < 1)
	{
		Ref argFrame = fArgFrame;
		if (!ISPTR(argFrame))
			ThrowBadTypeWithFrameData(kNSErrNotAFrame, fArgFrame);
		return FindOffset(ObjClass(OBJ(argFrame)), name);
	}
	Ref loc = GetFrameSlotRef(fVarLocs, name);
	return ISINT(loc) ? RVALUE(loc) : -1;
}


// ROM 0x002c0cf8 NoteVarReference__14TFunctionStateFRC6RefVar
// An inner function refers to name: if it is this function's, it is
// closed over; else the enclosing functions are asked, and this one
// closes over an outer variable.  ==> whether any function has it.
Boolean
TFunctionState::NoteVarReference(RefArg name)
{
	if (IsLocalVariable(name))
	{
		SetFrameSlot(fClosedVars, name, RefVar(TRUEREF));
		SetFrameSlot(fVarLocs, name, RefVar(Intern((char*) "closed")));
		return true;
	}
	if (fEnclosing != nil)
	{
		Boolean found = fEnclosing->NoteVarReference(name);
		if (found)
			fClosesOverOuter = true;
		return found;
	}
	return false;
}


// ROM 0x002c0da4 NoteMsgEnvReference__14TFunctionStateFQ214TFunctionState15MsgEnvComponent
// self or inherited is used: this function and all enclosing ones need it.
void
TFunctionState::NoteMsgEnvReference(MsgEnvComponent component)
{
	for (TFunctionState* state = this; state != nil; state = state->fEnclosing)
	{
		if (component == kMsgEnvReceiver)
			state->fUsesReceiver = true;
		else if (component == kMsgEnvImplementor)
			state->fUsesImplementor = true;
	}
}


// ROM 0x002c0dd4 AddConstant__14TFunctionStateFRC6RefVarT1
// (the ROM inlines SetFrameSlot on the constants frame)
void
TFunctionState::AddConstant(RefArg name, RefArg value)
{
	if (IsLocalVariable(name))
		fCompiler->Error(kNSErrLocalIsConstant, name);
	SetFrameSlot(fConstants, name, value);
}


// ROM 0x002c0e20 IsConstant__14TFunctionStateFRC6RefVar
// A constant of this function or an enclosing one, unless a variable of
// a nearer function hides it (_proto is never one).
Boolean
TFunctionState::IsConstant(RefArg name)
{
	if (EQRef(name, RSSYM_proto))
		return false;
	for (TFunctionState* state = this; state != nil; state = state->fEnclosing)
	{
		if (state->IsLocalConstant(name))
			return true;
		if (state->IsLocalVariable(name))
			return false;
	}
	return false;
}


// ROM 0x002c0ea4 IsLocalConstant__14TFunctionStateFRC6RefVar
Boolean
TFunctionState::IsLocalConstant(RefArg name)
{
	return FrameHasSlotRef(fConstants, name);
}


// ROM 0x002c0ecc GetConstantValue__14TFunctionStateFRC6RefVarPl
// (the ROM inlines GetProtoVariable on the constants frame)
Ref
TFunctionState::GetConstantValue(RefArg name, long* exists)
{
	if ((Ref) fConstants == NILREF)
		ThrowExInterpreterWithSymbol(kNSErrNilContext, name);
	long found;
	Ref value = GetProtoVariable(fConstants, name, &found);
	if (exists != nil)
		*exists = found;
	return value;
}


// ROM 0x002c0ed4 AtTopLevel__14TFunctionStateFv
Boolean
TFunctionState::AtTopLevel(void)
{
	return fEnclosing == nil;
}


// ROM 0x002c0eec MakeCodeBlock__14TFunctionStateFv
// The function object: a clone of the code block prototype (the debug
// one when the debugger numbers functions or names are kept) with the
// instructions, literals (nil when none), argFrame and, for a 2.x
// function, class kFuncClass, numArgs | numLocals << 16 and the 'dbg1
// debug information when names are kept: [the count of names from the
// enclosing argFrames, those names..., the stack locals' names by index,
// with a closed-over argument's entry pointing at its name].
Ref
TFunctionState::MakeCodeBlock(void)
{
	SetLength(fLiterals, fNumLiterals);
	SetLength(fInstructions, fPC);
	RefVar codeBlock;
	if (fFuncDepth < 0 && !fKeepVarNames)
		codeBlock = Clone(RefVar(gCodeBlockPrototype));
	else
		codeBlock = Clone(RefVar(gDebugCodeBlockPrototype));
	if (fFuncDepth >= 0)
		SetFrameSlot(codeBlock, RSSYMdebuggerinfo, RefVar(MAKEINT(fFuncDepth)));
	SetArraySlotRef(codeBlock, kFunctionInstructionsSlot, fInstructions);
	SetArraySlotRef(codeBlock, kFunctionLiteralsSlot, fNumLiterals == 0 ? NILREF : (Ref) fLiterals);
	if (gCompilerCompatibility < 1)
		SetArraySlotRef(codeBlock, kFunctionNumArgsSlot, MAKEINT(fNumArgs));
	else
	{
		SetArraySlotRef(codeBlock, kFunctionNumArgsSlot, MAKEINT(fNumArgs | (fNumLocals << 16)));
		SetArraySlotRef(codeBlock, kFunctionClassSlot, kFuncClass);
		if (fFuncDepth < 0 && fKeepVarNames)
		{
			RefVar dbg(AllocateArray(RefVar(Intern((char*) "dbg1")), 1));
			for (TFunctionState* state = this; state != nil; state = state->fEnclosing)
			{
				if ((Ref) state->fArgFrame != NILREF)
				{
					TObjectIterator iter(state->fArgFrame);
					iter.Next();
					iter.Next();
					iter.Next();
					for (; !iter.Done(); iter.Next())
						AddArraySlot(dbg, iter.fTag);
				}
			}
			long frameNames = Length(dbg);
			SetArraySlotRef(dbg, 0, MAKEINT(frameNames - 1));
			SetLength(dbg, Length(dbg) + fNumArgs + fNumLocals);
			{
				TObjectIterator iter(fVarLocs);
				for (; !iter.Done(); iter.Next())
				{
					if (ISINT(iter.fValue))
						SetArraySlotRef(dbg, RVALUE(iter.fValue) + frameNames - 3, iter.fTag);
					else
					{
						long argIndex = ArrayPosition(fArgs, iter.fTag, 0, RefVar(NILREF));
						if (argIndex != -1)
						{
							long namePosition = ArrayPosition(dbg, iter.fTag, 0, RefVar(NILREF));
							SetArraySlotRef(dbg, frameNames + argIndex, MAKEINT(namePosition));
						}
					}
				}
			}
			if (Length(dbg) > 1)
				SetArraySlotRef(codeBlock, 5, dbg);
		}
	}
	SetArraySlotRef(codeBlock, kFunctionArgFrameSlot, fArgFrame);
	if (gPrintLiteralsFlag)
	{
		PrintObject(fLiterals, 0);
		gREPout->Print("\r");
	}
	return codeBlock;
}


// ROM 0x002c131c BeginLoop__14TFunctionStateFv
void
TFunctionState::BeginLoop(void)
{
	TLoopState* loop = new TLoopState(this, fLoop);
	if (loop == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	fLoop = loop;
}


// ROM 0x002c1368 AddLoopExit__14TFunctionStateFv
// A break: a placeholder branch the loop's end patches; ==> its pc.
long
TFunctionState::AddLoopExit(void)
{
	if (fLoop == nil)
		ThrowMsg((char*) "BREAK statement outside a loop");
	fLoop->AddExit(fPC);
	long pc = CurPC();
	EmitThree(kBCBranch, 0);
	return pc;
}


// ROM 0x002c13c0 EndLoop__14TFunctionStateFv
void
TFunctionState::EndLoop(void)
{
	fLoop->PatchExits(fPC);
	TLoopState* loop = fLoop;
	fLoop = loop->fEnclosing;
	loop->fEnclosing = nil;
	delete loop;
}


/* -------------------------------------------------------------------------------
	TCompiler
------------------------------------------------------------------------------- */

// ROM 0x002bf18c __ct__9TCompilerFP12TInputStreami
// The first compiler roots the parser's values, makes the constant
// functions frame (the constantFunctions global) and the frame of the
// frequently called functions' names.
TCompiler::TCompiler(TInputStream* stream, Boolean interactive)
{
	fFunctionState = nil;
	fFunctionStates = nil;
	fStream = stream;
	fStackSize = 0x40;
	fYaccStack = AllocateArray(RSSYMyaccstack, 0x40);
	fYaccStates = new short[0x80];
	fInteractive = interactive;
	fReserved = 0;
	fFuncDepth = nil;
	fHasPushedToken = false;
	fPushedToken = 0;
	fPushedValue = NILREF;
	LockRef(fYaccStack);
	fYaccValues = Slots(fYaccStack);
	if (!gCompilerInited)
	{
		gCompilerInited = true;
		AddGCRoot(yyval);
		AddGCRoot(yylval);
		AddGCRoot(gConstFuncFrame);
		gConstFuncFrame = GetFrameSlotRef(gVarFrame, RSSYMconstantfunctions);
		if (gConstFuncFrame == NILREF)
		{
			gConstFuncFrame = AllocateFrame();
			SetFrameSlot(RefVar(gVarFrame), RefVar(Intern((char*) "constantfunctions")), RefVar(gConstFuncFrame));
		}
		AddGCRoot(gFreqFuncNames);
		gFreqFuncNames = AllocateFrame();
		for (long i = 0; i < gNumFreqFuncs; i++)
			SetFrameSlot(RefVar(gFreqFuncNames), RefVar(Intern((char*) gFreqFuncInfo[i].fName)), RefVar(MAKEINT(i)));
	}
}


// ROM 0x002c39e8 __dt__9TCompilerFv
TCompiler::~TCompiler()
{
	while (fFunctionStates != nil)
	{
		TFunctionState* state = fFunctionStates;
		fFunctionStates = state->fNext;
		delete state;
	}
	UnlockRef(fYaccStack);
	delete[] fYaccStates;
}


// ROM 0x002bf510 Warning__9TCompilerFPc
void
TCompiler::Warning(const char* message)
{
	gREPout->Print("File \"%s\"; Line %d ### Warning: %s\r", fStream->GetFilename(), fStream->fLineNumber, message);
}


// ROM 0x002c2110 Error__9TCompilerFl
// evt.ex.fr.comp;type.ref.frame with {errorCode, filename, linenumber}.
void
TCompiler::Error(long error)
{
	RefVar data(AllocateFrame());
	SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(error)));
	SetFrameSlot(data, RSSYMfilename, RefVar(MakeString(fStream->GetFilename())));
	SetFrameSlot(data, RSSYMlinenumber, RefVar(MAKEINT(fStream->fLineNumber)));
	ThrowRefException(exCompilerWithFrameData, data);
}


// ROM 0x002c21dc Error__9TCompilerFlRC6RefVar
void
TCompiler::Error(long error, RefArg value)
{
	RefVar data(AllocateFrame());
	SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(error)));
	SetFrameSlot(data, RSSYMvalue, value);
	SetFrameSlot(data, RSSYMfilename, RefVar(MakeString(fStream->GetFilename())));
	SetFrameSlot(data, RSSYMlinenumber, RefVar(MAKEINT(fStream->fLineNumber)));
	ThrowRefException(exCompilerWithFrameData, data);
}


// ROM 0x002c2090 ThrowExCompilerWithBadValue__FlRC6RefVar
void
ThrowExCompilerWithBadValue(long error, RefArg value)
{
	RefVar data(AllocateFrame());
	SetFrameSlot(data, RSSYMerrorcode, RefVar(MAKEINT(error)));
	SetFrameSlot(data, RSSYMvalue, value);
	ThrowRefException(exCompilerWithFrameData, data);
}


/* -------------------------------------------------------------------------------
	Emitting
------------------------------------------------------------------------------- */

// ROM 0x002bf574 Emit__9TCompilerF6Opcodel
void
TCompiler::Emit(int opcode, long operand)
{
	fFunctionState->Emit(opcode, operand);
}


// ROM 0x002bf57c EmitPush__9TCompilerFRC6RefVar
// push-constant for an immediate that fits 16 bits (sign extended) or a
// magic pointer under 0x1000, else push of a literal.
void
TCompiler::EmitPush(RefArg value)
{
	Ref ref = value;
	if ((ISMAGICPTR(ref) && (ULong) (ref >> kRefTagBits) <= 0xfff)
	 || (!ISPTR(ref) && ref == (Ref) (short) ref))
		fFunctionState->Emit(kBCPushConstant, (long) ref & 0xffff);
	else
		fFunctionState->Emit(kBCPush, fFunctionState->LitOffset(value));
}


// ROM 0x002bf5ec EmitPop__9TCompilerFv
void
TCompiler::EmitPop(void)
{
	fFunctionState->EmitOne(kBCPop);
}


// ROM 0x002bf5f8 EmitVarSet__9TCompilerFRC6RefVar
// set-var for a variable on the stack, else set-find-var.
void
TCompiler::EmitVarSet(RefArg name)
{
	long index;
	if (fFunctionState->IsLocalVariable(name) && (index = fFunctionState->VariableIndex(name)) != -1)
		fFunctionState->Emit(kBCSetVar, index);
	else
		fFunctionState->Emit(kBCFindAndSetVar, fFunctionState->LitOffset(name));
}


// ROM 0x002bf65c EmitVarGet__9TCompilerFRC6RefVar
void
TCompiler::EmitVarGet(RefArg name)
{
	if (EQRef(name, RSSYM_parent))
		Warning("References to the variable \"_parent\" have undefined behavior");
	long index;
	if (fFunctionState->IsLocalVariable(name) && (index = fFunctionState->VariableIndex(name)) != -1)
		fFunctionState->Emit(kBCGetVar, index);
	else
		fFunctionState->Emit(kBCFindVar, fFunctionState->LitOffset(name));
}


// ROM 0x002bf730 EmitVarIncr__9TCompilerFRC6RefVar
// incr-var of a for loop's index, which must be on the stack.
void
TCompiler::EmitVarIncr(RefArg name)
{
	long index;
	if (fFunctionState->IsLocalVariable(name) && (index = fFunctionState->VariableIndex(name)) != -1)
		fFunctionState->Emit(kBCIncrVar, index);
	else
		SyntaxError("can't close over a for-loop index variable");
}


// ROM 0x002bf824 FreqFuncIndex__FRC6RefVarl
// The index of a frequently called function with that many arguments, -1
// for none.
long
FreqFuncIndex(RefArg name, long numArgs)
{
	Ref index = GetFrameSlotRef(gFreqFuncNames, name);
	if (index != NILREF)
	{
		if (!ISINT(index))
			ThrowBadTypeWithFrameData(kNSErrNotAnInteger, RefVar(index));
		if (gFreqFuncInfo[RVALUE(index)].fNumArgs == numArgs)
			return RVALUE(index);
	}
	return -1;
}


// ROM 0x002bf7b8 EmitFuncall__9TCompilerFRC6RefVarUl
// freq-func for a frequently called function, else push name; call n.
void
TCompiler::EmitFuncall(RefArg name, ULong numArgs)
{
	long index = FreqFuncIndex(name, numArgs);
	if (index == -1)
	{
		EmitPush(name);
		fFunctionState->Emit(kBCCall, numArgs);
	}
	else
		fFunctionState->Emit(kBCFreqFunc, index);
}


// ROM 0x002bf810 EmitBranch__9TCompilerFUl
void
TCompiler::EmitBranch(ULong pc)
{
	fFunctionState->Emit(kBCBranch, pc);
}


// ROM 0x002bf81c EmitPlaceholder__9TCompilerFv
long
TCompiler::EmitPlaceholder(void)
{
	return fFunctionState->EmitPlaceholder();
}


// ROM 0x002bf8a8 EmitReturn__9TCompilerFv
void
TCompiler::EmitReturn(void)
{
	fFunctionState->EmitOne(kBCReturn);
}


// ROM 0x002bf8b4 CurPC__9TCompilerFv
long
TCompiler::CurPC(void)
{
	return fFunctionState->fPC;
}


// ROM 0x002bf8bc Backpatch__9TCompilerFUl6Opcodel
void
TCompiler::Backpatch(ULong pc, int opcode, long operand)
{
	fFunctionState->Backpatch(pc, opcode, operand);
}


// ROM 0x002bf8c4 AddLocals__9TCompilerFRC6RefVar
void
TCompiler::AddLocals(RefArg names)
{
	fFunctionState->AddLocals(names);
}


// ROM 0x002bf8cc NewFunctionState__9TCompilerFRC6RefVarP14TFunctionStatePi
void
TCompiler::NewFunctionState(RefArg args, TFunctionState* enclosing, int* funcDepth)
{
	TFunctionState* state = new TFunctionState(this, args, enclosing, funcDepth);
	if (state == nil)
		Throw(exOutOfMemory, (void*) kError_No_Memory, nil);
	state->fNext = fFunctionStates;
	fFunctionStates = state;
	fFunctionState = state;
}


// ROM 0x002bf930 EndFunction__9TCompilerFv
Ref
TCompiler::EndFunction(void)
{
	EmitReturn();
	RefVar codeBlock(fFunctionState->MakeCodeBlock());
	fFunctionState = fFunctionState->fEnclosing;
	return codeBlock;
}


// ROM 0x002bf96c BeginLoop__9TCompilerFv
void
TCompiler::BeginLoop(void)
{
	fFunctionState->BeginLoop();
}


// ROM 0x002bf974 AddLoopExit__9TCompilerFv
long
TCompiler::AddLoopExit(void)
{
	return fFunctionState->AddLoopExit();
}


// ROM 0x002bf97c EndLoop__9TCompilerFv
void
TCompiler::EndLoop(void)
{
	fFunctionState->EndLoop();
}


/* -------------------------------------------------------------------------------
	Constants
------------------------------------------------------------------------------- */

// ROM 0x002bf9fc IsConstantExpr__9TCompilerFRC6RefVar
// A constant, a constant's name, or the negation of one.
Boolean
TCompiler::IsConstantExpr(RefArg expr)
{
	long kind = NodeKind(expr);
	RefVar value(Length(expr) == 1 ? NILREF : GetArraySlotRef(expr, 1));
	if (kind == tokenCONST)
		return true;
	if (kind == tokenSYMBOL)
		return fFunctionState->IsConstant(value);
	if (kind == tokenUMINUS)
		return IsConstantExpr(value);
	return false;
}


// ROM 0x002bfae0 EvaluateConstantExpr__9TCompilerFRC6RefVar
Ref
TCompiler::EvaluateConstantExpr(RefArg expr)
{
	long kind = NodeKind(expr);
	RefVar value(Length(expr) == 1 ? NILREF : GetArraySlotRef(expr, 1));
	if (kind == tokenCONST)
		return value;
	if (kind == tokenSYMBOL)
		return fFunctionState->GetConstantValue(value, nil);
	if (kind == tokenUMINUS)
		return (Ref) -(long) EvaluateConstantExpr(value);		// (an integer ref negated is the negated integer's)
	return NILREF;
}


// ROM 0x002c3d40 Simplify__9TCompilerFRC6RefVar
// (nothing in this ROM)
void
TCompiler::Simplify(RefArg /*tree*/)
{ }


/* -------------------------------------------------------------------------------
	Declarations
------------------------------------------------------------------------------- */

// ROM 0x002c7210 DeclarationWalker__9TCompilerFRC6RefVarlN51
// Locals, constants, for and foreach loop variables (name|limit,
// name|incr; name|iter and, when collecting, name|index and
// name|result), and nested functions, which get a TFunctionState of
// their own (in the node's slot 5) with their own declarations.
Boolean
TCompiler::DeclarationWalker(RefArg node, long kind, RefArg a1, RefArg a2, RefArg /*a3*/, RefArg a4, RefArg /*a5*/)
{
	if (kind == tokenLOCAL)
	{
		long count = Length(a1);
		RefVar names(AllocateArray(RSSYMarray, count / 2));
		for (long i = 0; i < count; i += 2)
			SetArraySlotRef(names, i / 2, GetArraySlotRef(a1, i));
		AddLocals(names);
	}
	else if (kind == tokenFUNC)
	{
		NewFunctionState(a1, fFunctionState, fFuncDepth);
		WalkForDeclarations(a2);
		SetArraySlotRef(node, 5, StateToRef(fFunctionState));
		fFunctionState = fFunctionState->fEnclosing;
	}
	else if (kind == tokenCONSTANT)
	{
		long count = Length(a1);
		RefVar name, expr;
		for (long i = 0; i < count; i += 2)
		{
			name = GetArraySlotRef(a1, i);
			expr = GetArraySlotRef(a1, i + 1);
			if (fFunctionState->IsLocalConstant(name))
				Error(kNSErrConstantRedefined, name);
			if (fFunctionState->AtTopLevel() && FrameHasSlotRef(gVarFrame, name))
				Error(kNSErrGlobalConstantConflict, name);
			Simplify(expr);
			if (!IsConstantExpr(expr))
				Error(kNSErrNonConstantInitializer);
			else
				fFunctionState->AddConstant(name, RefVar(EvaluateConstantExpr(expr)));
		}
	}
	else if (kind == tokenFOR)
	{
		RefVar names(AllocateArray(RSSYMarray, 3));
		SetArraySlotRef(names, 0, a1);
		const char* base = SymbolName(a1);
		char* text = (char*) malloc(strlen(base) + 7);
		strcpy(text, base);
		strcat(text, "|limit");
		SetArraySlotRef(names, 1, Intern(text));
		strcpy(text + strlen(base), "|incr");
		SetArraySlotRef(names, 2, Intern(text));
		free(text);
		AddLocals(names);
	}
	else if (kind == tokenFOREACH)
	{
		AddLocals(RefVar(Clone(a4)));
		const char* first = SymbolName(GetArraySlotRef(a4, 0));
		const char* second = Length(a4) < 2 ? "" : SymbolName(GetArraySlotRef(a4, 1));
		char* text = (char*) malloc(strlen(first) + strlen(second) + 8);
		strcpy(text, first);
		strcat(text, second);
		strcat(text, "|iter");
		Boolean collecting = EQRef(a1, RSSYMcollect);
		RefVar names(AllocateArray(RSSYMarray, collecting ? 3 : 1));
		SetArraySlotRef(names, 0, Intern(text));
		if (collecting)
		{
			strcpy(text + strlen(first) + strlen(second), "|index");
			SetArraySlotRef(names, 1, Intern(text));
			strcpy(text + strlen(first) + strlen(second), "|result");
			SetArraySlotRef(names, 2, Intern(text));
		}
		AddLocals(names);
		free(text);
	}
	return true;
}


static Boolean
DeclarationWalkerTrampoline(void* context, RefArg node, long kind, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5)
{
	return ((TCompiler*) context)->DeclarationWalker(node, kind, a1, a2, a3, a4, a5);
}


// ROM 0x002c781c WalkForDeclarations__9TCompilerFRC6RefVar
void
TCompiler::WalkForDeclarations(RefArg tree)
{
	WalkNodes(tree, this, DeclarationWalkerTrampoline, false);
	fFunctionState->DeclarationsFinished();
}


/* -------------------------------------------------------------------------------
	Closures
------------------------------------------------------------------------------- */

// ROM 0x002c3a3c ClosureWalker__9TCompilerFRC6RefVarlN51
// Which variables are referenced from where: a variable of this
// function counts its references (fVarLocs), one of an enclosing
// function is noted as closed over, a global as needing the message
// environment; self and a send to self need the receiver, inherited the
// implementor; a nested function is walked in its own state.
Boolean
TCompiler::ClosureWalker(RefArg /*node*/, long kind, RefArg a1, RefArg a2, RefArg /*a3*/, RefArg /*a4*/, RefArg a5)
{
	if (kind == tokenSELF)
	{
		fFunctionState->NoteMsgEnvReference(kMsgEnvReceiver);
		return true;
	}
	if (kind == tokenSYMBOL)
	{
		if (fFunctionState->IsLocalVariable(a1))
		{
			Ref loc = GetFrameSlotRef(fFunctionState->fVarLocs, a1);
			if (loc == NILREF)
				SetFrameSlot(fFunctionState->fVarLocs, a1, RefVar(MAKEINT(1)));
			else if (ISINT(loc))
				SetFrameSlot(fFunctionState->fVarLocs, a1, RefVar(MAKEINT(RVALUE(loc) + 1)));
			return true;
		}
		if (!fFunctionState->NoteVarReference(a1))
			fFunctionState->NoteMsgEnvReference(kMsgEnvReceiver);
		return true;
	}
	if (kind == tokenFUNC)
	{
		fFunctionState = RefToState(a5);
		WalkForClosures(a2);
		fFunctionState = fFunctionState->fEnclosing;
		return true;
	}
	if (kind == tokenASSIGN)
	{
		// the variable assigned is a reference too
		if (NodeKind(a1) == tokenSYMBOL)
		{
			RefVar b1, b2, b3, b4, b5;
			long lvalueKind = NodeParts(a1, b1, b2, b3, b4, b5);
			ClosureWalker(a1, lvalueKind, b1, b2, b3, b4, b5);
		}
		return true;
	}
	if (kind == ':' || kind == tokenSENDIFDEFINED)
	{
		if ((Ref) a2 == NILREF)					// inherited
			fFunctionState->NoteMsgEnvReference(kMsgEnvImplementor);
		return true;
	}
	return true;
}


static Boolean
ClosureWalkerTrampoline(void* context, RefArg node, long kind, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5)
{
	return ((TCompiler*) context)->ClosureWalker(node, kind, a1, a2, a3, a4, a5);
}


// ROM 0x002c3d00 WalkForClosures__9TCompilerFRC6RefVar
// (the ROM inlines ComputeArgFrame after the walk)
void
TCompiler::WalkForClosures(RefArg tree)
{
	fFunctionState->ComputeInitialVarLocs();
	WalkNodes(tree, this, ClosureWalkerTrampoline, false);
	fFunctionState->ComputeArgFrame();
}


/* -------------------------------------------------------------------------------
	Code
------------------------------------------------------------------------------- */

// ROM 0x002c3fd8 WalkAssignment__9TCompilerFRC6RefVarT1Uc
// lvalue := value: a variable (set-var/set-find-var, the value left
// unless it is for effect), a path (set-path) or an element (setAref).
// ==> whether a value is left on the stack.
Boolean
TCompiler::WalkAssignment(RefArg lvalue, RefArg value, Boolean isEffect)
{
	long kind = NodeKind(lvalue);
	if (kind == tokenSYMBOL)
	{
		RefVar name(GetArraySlotRef(lvalue, 1));
		if (fFunctionState->IsConstant(name))
			Error(kNSErrAssignToConstant, name);
		WalkForCode(value, false);
		EmitVarSet(name);
		if (isEffect)
			return false;
		EmitVarGet(name);
	}
	else if (kind == '.')
	{
		long nilForNil = 1;
		RefVar path(WalkForPath(lvalue, nilForNil));
		if ((Ref) path != NILREF)
			EmitPush(path);
		WalkForCode(value, false);
		Emit(kBCSetPath, isEffect ? 0 : 1);
		if (isEffect)
			return false;
	}
	else if (kind == '[')
	{
		WalkForCode(RefVar(GetArraySlotRef(lvalue, 1)), false);
		WalkForCode(RefVar(GetArraySlotRef(lvalue, 2)), false);
		WalkForCode(value, false);
		EmitFuncall(RSSYMsetaref, 3);
	}
	return true;
}


// ROM 0x002c41e4 WalkForPath__9TCompilerFRC6RefVarRl
// The code for a path expression a.b.c: the object, then get-path per
// element until the last, which is answered (a symbol, or a pathExpr of
// the constant tail) for the caller to push and use; nil when the last
// element was computed and pushed.  nilForNil is the get-path operand
// (1: nil for a missing slot).
Ref
TCompiler::WalkForPath(RefArg expr, long& nilForNil)
{
	if (NodeKind(expr) != '.')
	{
		Error(kNSErrBadPathInAssignment, expr);
		return NILREF;
	}
	RefVar object(GetArraySlotRef(expr, 1));
	RefVar element(GetArraySlotRef(expr, 2));
	if (NodeKind(element) == tokenCONST && IsSymbol(GetArraySlotRef(element, 1)))
	{
		if (NodeKind(object) != '.')
		{
			WalkForCode(object, false);
			return GetArraySlotRef(element, 1);
		}
		RefVar tail(WalkForPath(object, nilForNil));
		if ((Ref) tail == NILREF)
		{
			Emit(kBCGetPath, nilForNil);
			nilForNil = 0;
			return GetArraySlotRef(element, 1);
		}
		if (IsSymbol(tail))
		{
			RefVar path(AllocateArray(RSSYMpathexpr, 2));
			SetArraySlotRef(path, 0, tail);
			SetArraySlotRef(path, 1, GetArraySlotRef(element, 1));
			return path;
		}
		AddArraySlot(tail, RefVar(GetArraySlotRef(element, 1)));
		return tail;
	}
	if (NodeKind(object) == '.')
	{
		RefVar tail(WalkForPath(object, nilForNil));
		if ((Ref) tail != NILREF)
			EmitPush(tail);
		Emit(kBCGetPath, nilForNil);
		nilForNil = 0;
		WalkForCode(element, false);
		return NILREF;
	}
	WalkForCode(object, false);
	WalkForCode(element, false);
	return NILREF;
}


// ROM 0x002c4548 WalkForStringer__9TCompilerFRC6RefVar
// The parts of a & / && concatenation pushed (&& with a " " between);
// ==> how many.
long
TCompiler::WalkForStringer(RefArg expr)
{
	long kind = NodeKind(expr);
	if (kind != '&' && kind != tokenAMPERAMPER)
	{
		WalkForCode(expr, false);
		return 1;
	}
	long count = WalkForStringer(RefVar(GetArraySlotRef(expr, 1)));
	if (kind == tokenAMPERAMPER)
		EmitPush(RefVar(MakeString(" ")));
	count += WalkForStringer(RefVar(GetArraySlotRef(expr, 2)));
	return count + (kind == tokenAMPERAMPER ? 1 : 0);
}


// the sub-expressions of a node, walked for their value
#define WALK(expr)			WalkForCode(expr, false)
// a sub-expression walked for effect, its value (if any) popped
#define WALK_EFFECT(expr)	do { if (WalkForCode(expr, true)) EmitPop(); } while (0)


// ROM 0x002c4664 WalkForCode__9TCompilerFRC6RefVarUc
// The code of a node.  isEffect: its value is not wanted (a statement,
// the non-final expressions of a sequence); ==> whether a value was
// left on the stack anyway.  Warns about a statement with no effect.
Boolean
TCompiler::WalkForCode(RefArg node, Boolean isEffect)
{
	RefVar a1, a2, a3, a4, a5;
	long kind = NodeParts(node, a1, a2, a3, a4, a5);
	Boolean leavesValue = true;
	Boolean isPure = false;			// an expression whose value is its only effect

	switch (kind)
	{
	case tokenWHILE:
	{
		long test = EmitPlaceholder();
		long top = CurPC();
		BeginLoop();
		WALK(a2);
		EmitPop();
		Backpatch(test, kBCBranch, CurPC());
		WALK(a1);
		Emit(kBCBranchIfTrue, top);
		EmitPush(RefVar(NILREF));
		EndLoop();
		break;
	}

	case tokenBEGIN:
	{
		long count = Length(a1);
		if (count == 0)
		{
			if (isEffect)
				leavesValue = false;
			else
				EmitPush(RefVar(NILREF));
			break;
		}
		for (long i = 0; i < count; i++)
		{
			RefVar expr(GetArraySlotRef(a1, i));
			if (i < count - 1)
				WALK_EFFECT(expr);
			else
				leavesValue = WalkForCode(expr, isEffect);
		}
		break;
	}

	case tokenFUNC:
		isPure = true;
		if (isEffect)
		{
			leavesValue = false;
			break;
		}
		{
			TFunctionState* state = RefToState(a5);
			fFunctionState = state;
			state->CopyClosedArgs();
			WALK(a2);
			Boolean hasArgFrame = (Ref) fFunctionState->fArgFrame != NILREF;
			RefVar codeBlock(EndFunction());
			EmitPush(codeBlock);
			if (hasArgFrame)
				Emit(0, kBCSetLexScope);
		}
		return leavesValue;

	case tokenGLOBAL:
		if (FrameHasSlotRef(gConstantsFrame, a1))
			Error(kNSErrGlobalConstantConflict, a1);
		WALK(a2);
		EmitPush(a1);
		EmitFuncall(RSSYMsetglobal, 2);
		break;

	case tokenCONSTANT:
		if (isEffect)
			leavesValue = false;
		else
			EmitPush(RefVar(NILREF));
		break;

	case tokenIF:
	{
		if (NodeKind(a1) == tokenAMPERAMPER)
			Warning("&& used in IF statement...did you mean AND?");
		WALK(a1);
		long toElse = EmitPlaceholder();
		if (isEffect)
			WALK_EFFECT(a2);
		else
			WALK(a2);
		if ((Ref) a3 == NILREF)
		{
			if (!isEffect)
			{
				long toEnd = EmitPlaceholder();
				Backpatch(toElse, kBCBranchIfFalse, CurPC());
				EmitPush(RefVar(NILREF));
				Backpatch(toEnd, kBCBranch, CurPC());
			}
			else
				Backpatch(toElse, kBCBranchIfFalse, CurPC());
		}
		else
		{
			long toEnd = EmitPlaceholder();
			Backpatch(toElse, kBCBranchIfFalse, CurPC());
			if (isEffect)
				WALK_EFFECT(a3);
			else
				WALK(a3);
			Backpatch(toEnd, kBCBranch, CurPC());
		}
		if (isEffect)
			leavesValue = false;
		break;
	}

	case tokenTRY:
	{
		// new-handlers with the (symbol, pc) pairs; the body; pop-handlers;
		// then each handler's code, ending with pop-handlers
		long count = Length(a2);
		RefVar pcHolders(AllocateArray(RSSYMarray, count));
		RefVar exits(AllocateArray(RSSYMarray, count - 1));
		RefVar handler;
		for (long i = 0; i < count; i++)
		{
			handler = GetArraySlotRef(a2, i);
			EmitPush(RefVar(GetArraySlotRef(handler, 1)));
			SetArraySlotRef(pcHolders, i, MAKEINT(EmitPlaceholder()));
		}
		Emit(kBCNewHandlers, count);
		if (isEffect)
			WALK_EFFECT(a1);
		else
			WALK(a1);
		Emit(0, kBCPopHandlers);
		long toEnd = EmitPlaceholder();
		for (long i = 0; i < count; i++)
		{
			handler = GetArraySlotRef(a2, i);
			// the handler's pc, as the integer pushed before new-handlers
			long pc = CurPC();
			Ref pcRef = MAKEINT(pc);
			if (pcRef == (Ref) (short) pcRef)
				Backpatch(RINT(GetArraySlotRef(pcHolders, i)), kBCPushConstant, (long) pcRef & 0xffff);
			else
				Backpatch(RINT(GetArraySlotRef(pcHolders, i)), kBCPush, fFunctionState->LitOffset(RefVar(pcRef)));
			if (isEffect)
				WALK_EFFECT(RefVar(GetArraySlotRef(handler, 2)));
			else
				WALK(RefVar(GetArraySlotRef(handler, 2)));
			if (i < count - 1)
				SetArraySlotRef(exits, i, MAKEINT(EmitPlaceholder()));
		}
		for (long i = 0; i < count - 1; i++)
			Backpatch(RINT(GetArraySlotRef(exits, i)), kBCBranch, CurPC());
		Emit(0, kBCPopHandlers);
		Backpatch(toEnd, kBCBranch, CurPC());
		leavesValue = !isEffect;
		break;
	}

	case tokenBUILDARRAY:
	{
		isPure = true;
		long count = Length(a1);
		for (long i = 0; i < count; i++)
			WALK(RefVar(GetArraySlotRef(a1, i)));
		EmitPush(RefVar(ClassOf(a1)));
		Emit(kBCMakeArray, count);
		break;
	}

	case tokenBUILDFRAME:
	{
		isPure = true;
		long count = Length(a1);
		{
			TObjectIterator iter(a1);
			for (; !iter.Done(); iter.Next())
				WALK(iter.fValue);
		}
		EmitPush(RefVar(SharedFrameMap(a1)));
		Emit(kBCMakeFrame, count);
		break;
	}

	case tokenLOCAL:
	{
		long count = Length(a1);
		for (long i = 0; i < count; i += 2)
		{
			if (GetArraySlotRef(a1, i + 1) != NILREF)
			{
				WALK(RefVar(GetArraySlotRef(a1, i + 1)));
				EmitVarSet(RefVar(GetArraySlotRef(a1, i)));
			}
		}
		if (isEffect)
			leavesValue = false;
		else
			EmitPush(RefVar(NILREF));
		break;
	}

	case tokenLOOP:
	{
		long top = CurPC();
		BeginLoop();
		WALK(a1);
		EmitPop();
		EmitBranch(top);
		EndLoop();
		break;
	}

	case tokenFOR:
	{
		// [FOR, index, from, to, by, body]
		const char* base = SymbolName(a1);
		char* text = (char*) malloc(strlen(base) + 7);
		strcpy(text, base);
		strcat(text, "|limit");
		RefVar limit(Intern(text));
		strcpy(text + strlen(base), "|incr");
		RefVar incr(Intern(text));
		free(text);
		WALK(a2);
		EmitVarSet(a1);
		WALK(a3);
		EmitVarSet(limit);
		WALK(a4);
		EmitVarSet(incr);
		EmitVarGet(incr);
		EmitVarGet(a1);
		long toTest = EmitPlaceholder();
		long top = CurPC();
		BeginLoop();
		WALK_EFFECT(a5);
		EmitVarGet(incr);
		EmitVarIncr(a1);
		Backpatch(toTest, kBCBranch, CurPC());
		EmitVarGet(limit);
		Emit(kBCBranchIfLoopNotDone, top);
		EmitPush(RefVar(NILREF));
		EndLoop();
		break;
	}

	case tokenREPEAT:
	{
		long top = CurPC();
		BeginLoop();
		long count = Length(a1);
		for (long i = 0; i < count; i++)
			WALK_EFFECT(RefVar(GetArraySlotRef(a1, i)));
		WALK(a2);
		Emit(kBCBranchIfFalse, top);
		EmitPush(RefVar(NILREF));
		EndLoop();
		break;
	}

	case tokenCALL:
	{
		long count = Length(a2);
		for (long i = 0; i < count; i++)
			WALK(RefVar(GetArraySlotRef(a2, i)));
		EmitFuncall(a1, count);
		break;
	}

	case tokenINVOKE:
	{
		long count = Length(a2);
		for (long i = 0; i < count; i++)
			WALK(RefVar(GetArraySlotRef(a2, i)));
		WALK(a1);
		Emit(kBCInvoke, count);
		break;
	}

	case tokenFOREACH:
	{
		// [FOREACH, verb, collection, body, vars, deeply]
		Boolean collecting = EQRef(a1, RSSYMcollect);
		Boolean twoVars = Length(a4) > 1;
		Boolean deeply = (Ref) a5 != NILREF;
		RefVar slotVar(twoVars ? GetArraySlotRef(a4, 0) : NILREF);
		RefVar valueVar(GetArraySlotRef(a4, twoVars ? 1 : 0));
		const char* first = SymbolName(GetArraySlotRef(a4, 0));
		const char* second = twoVars ? SymbolName(GetArraySlotRef(a4, 1)) : "";
		char* text = (char*) malloc(strlen(first) + strlen(second) + 8);
		strcpy(text, first);
		strcat(text, second);
		strcat(text, "|iter");
		RefVar iter(Intern(text));
		RefVar index, result;
		WALK(a2);
		if (collecting)
		{
			strcpy(text + strlen(first) + strlen(second), "|index");
			index = Intern(text);
			strcpy(text + strlen(first) + strlen(second), "|result");
			result = Intern(text);
		}
		free(text);
		// iter := NewIterator(collection, deeply)
		EmitPush(RefVar(deeply ? TRUEREF : NILREF));
		EmitFuncall(RSSYMnewiterator, 2);
		EmitVarSet(iter);
		if (collecting)
		{
			// result := Array(iter[5 or 3], nil); index := 0
			EmitVarGet(iter);
			EmitPush(RefVar(MAKEINT(deeply ? 3 : 5)));
			EmitFuncall(RSSYMaref, 2);
			EmitPush(RefVar(RSSYMarray));
			Emit(kBCMakeArray, 0xffff);
			EmitVarSet(result);
			EmitPush(RefVar(MAKEINT(0)));
			EmitVarSet(index);
		}
		long toTest = EmitPlaceholder();
		long top = CurPC();
		BeginLoop();
		EmitVarGet(iter);
		EmitPush(RefVar(MAKEINT(1)));
		EmitFuncall(RSSYMaref, 2);
		EmitVarSet(valueVar);
		if (twoVars)
		{
			EmitVarGet(iter);
			EmitPush(RefVar(MAKEINT(0)));
			EmitFuncall(RSSYMaref, 2);
			EmitVarSet(slotVar);
		}
		if (!collecting)
			WALK_EFFECT(a3);
		else
		{
			// result[index] := body; index := index + 1
			EmitVarGet(result);
			EmitVarGet(index);
			WALK(a3);
			EmitFuncall(RSSYMsetaref, 3);
			EmitPop();
			EmitPush(RefVar(MAKEINT(1)));
			EmitVarIncr(index);
			EmitPop();
			EmitPop();
		}
		EmitVarGet(iter);
		Emit(0, kBCIterNext);
		Backpatch(toTest, kBCBranch, CurPC());
		EmitVarGet(iter);
		Emit(0, kBCIterDone);
		Emit(kBCBranchIfFalse, top);
		long toEnd = 0;
		if (!collecting)
			EmitPush(RefVar(NILREF));
		else
			toEnd = EmitPlaceholder();
		EndLoop();
		if (collecting)
		{
			// a break's value is the result; the loop's end leaves the array
			EmitVarSet(result);
			EmitPop();
			EmitPop();
			Backpatch(toEnd, kBCBranch, CurPC());
			EmitVarGet(result);
			EmitPush(RefVar(NILREF));
			EmitVarSet(result);
		}
		EmitPush(RefVar(NILREF));
		EmitVarSet(iter);
		break;
	}

	case tokenSELF:
		Emit(0, kBCPushSelf);
		break;

	case tokenRETURN:
		WALK(a1);
		EmitReturn();
		break;

	case tokenBREAK:
		WALK(a1);
		AddLoopExit();
		break;

	case tokenASSIGN:
		leavesValue = WalkAssignment(a1, a2, isEffect);
		break;

	case tokenAND:
	{
		isPure = true;
		WALK(a1);
		long toFalse = EmitPlaceholder();
		if (!isEffect)
		{
			WALK(a2);
			long toEnd = EmitPlaceholder();
			Backpatch(toFalse, kBCBranchIfFalse, CurPC());
			EmitPush(RefVar(NILREF));
			Backpatch(toEnd, kBCBranch, CurPC());
		}
		else
		{
			WALK_EFFECT(a2);
			Backpatch(toFalse, kBCBranchIfFalse, CurPC());
			leavesValue = false;
		}
		break;
	}

	case tokenOR:
	{
		isPure = true;
		WALK(a1);
		long toTrue = EmitPlaceholder();
		if (!isEffect)
		{
			WALK(a2);
			long toEnd = EmitPlaceholder();
			Backpatch(toTrue, kBCBranchIfTrue, CurPC());
			EmitPush(RefVar(TRUEREF));
			Backpatch(toEnd, kBCBranch, CurPC());
		}
		else
		{
			WALK_EFFECT(a2);
			Backpatch(toTrue, kBCBranchIfTrue, CurPC());
			leavesValue = false;
		}
		break;
	}

	case tokenNOT:
		isPure = true;
		WALK(a1);
		EmitFuncall(RSSYMnot, 1);
		break;

	case tokenEXISTS:
	{
		isPure = true;
		long exprKind = NodeKind(a1);
		if (exprKind == tokenSYMBOL)
		{
			EmitPush(RefVar(GetArraySlotRef(a1, 1)));
			EmitFuncall(RSSYMhasvar, 1);
		}
		else if (exprKind == '.')
		{
			long nilForNil = 0;
			RefVar path(WalkForPath(a1, nilForNil));
			if ((Ref) path != NILREF)
				EmitPush(path);
			EmitFuncall(RSSYMhaspath, 2);
		}
		else if (exprKind == ':')
		{
			WALK(RefVar(GetArraySlotRef(a1, 2)));
			EmitPush(RefVar(GetArraySlotRef(a1, 1)));
			EmitFuncall(RSSYMhasvariable, 2);
		}
		else
			Error(kNSErrBadExistsSubexpr);
		break;
	}

	case '&':
	case tokenAMPERAMPER:
	{
		isPure = true;
		long count = WalkForStringer(node);
		EmitPush(RefVar(RSSYMarray));
		Emit(kBCMakeArray, count);
		EmitFuncall(RefVar(Intern((char*) "Stringer")), 1);
		break;
	}

	case tokenUMINUS:
		isPure = true;
		if (NodeKind(a1) == tokenCONST)
		{
			// -constant folded
			RefVar value(GetArraySlotRef(a1, 1));
			newton_try
			{
				if (ISINT(value))
					EmitPush(RefVar(MAKEINT(-RINT(value))));
				else
					EmitPush(RefVar(MakeReal(-CDouble(value))));
			}
			newton_catch((ExceptionName) "evt.ex.fr.type")
			{
				ThrowBadTypeWithFrameData(kNSErrNotANumber, value);
			}
			end_try;
		}
		else
		{
			WALK(a1);
			EmitFuncall(RSSYMnegate, 1);
		}
		break;

	case ':':
	case tokenSENDIFDEFINED:
	{
		// [kind, message, receiver (nil: inherited), args]
		long count = Length(a3);
		for (long i = 0; i < count; i++)
			WALK(RefVar(GetArraySlotRef(a3, i)));
		if ((Ref) a2 == NILREF)
		{
			EmitPush(a1);
			Emit(kind == ':' ? kBCResend : kBCResendIfDefined, count);
		}
		else
		{
			WALK(a2);
			EmitPush(a1);
			Emit(kind == ':' ? kBCSend : kBCSendIfDefined, count);
		}
		break;
	}

	case tokenCONST:
		if (isEffect)
			return false;
		EmitPush(a1);
		return true;

	case tokenSYMBOL:
		if (fFunctionState->IsConstant(a1))
			EmitPush(RefVar(fFunctionState->GetConstantValue(a1, nil)));
		else
			EmitVarGet(a1);
		break;

	case '.':
	{
		long nilForNil = 1;
		RefVar path(WalkForPath(node, nilForNil));
		if ((Ref) path != NILREF)
			EmitPush(path);
		Emit(kBCGetPath, nilForNil);
		break;
	}

	case '[':
		isPure = true;
		WALK(a1);
		WALK(a2);
		EmitFuncall(RSSYMaref, 2);
		break;

	// the arithmetic and comparison operators
	case '+':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_2B, 2); break;
	case '-':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_2D, 2); break;
	case '*':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_2A, 2); break;
	case '/':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_2F, 2); break;
	case tokenDIV:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYMdiv, 2); break;
	case tokenMOD:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYMmod, 2); break;
	case tokenLSHIFT:		isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3C_3C, 2); break;
	case tokenRSHIFT:		isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3E_3E, 2); break;
	case '<':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3C, 2); break;
	case '>':				isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3E, 2); break;
	case tokenLEQ:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3C_3D, 2); break;
	case tokenGEQ:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3E_3D, 2); break;
	case tokenEQL:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3D, 2); break;
	case tokenNEQ:			isPure = true; WALK(a1); WALK(a2); EmitFuncall(RSSYM_3C_3E, 2); break;

	default:
		break;
	}

	if (isEffect)
	{
		if (kind == tokenEQL)
			Warning("= with no effect...did you mean := ?");
		else if (isPure)
			Warning("Statement has no effect");
	}
	return leavesValue;
}


/* -------------------------------------------------------------------------------
	Compiling
------------------------------------------------------------------------------- */

// ROM 0x002c3d44 Compile__9TCompilerFv
// The input's commands, parsed and compiled into one function of no
// arguments (nil when the parse failed): declarations, closures, the
// closed-over arguments copied, code.
Ref
TCompiler::Compile(void)
{
	fFuncDepth = nil;
	fFunctionState = nil;
	fFunctionStates = nil;
	int status = Parser();
	RefVar tree(AllocatePT1(tokenBEGIN, RefVar(yyval)));
	yyval = NILREF;
	yylval = NILREF;
	if (status != 0)
		return NILREF;
	Ref compatibility = GetFrameSlotRef(gVarFrame, Intern((char*) "compilercompatibility"));
	gCompilerCompatibility = ISINT(compatibility) ? RVALUE(compatibility) : 1;
	NewFunctionState(RefVar(AllocateArray(RSSYMarray, 0)), nil, fFuncDepth);
	RefVar codeBlock;
	newton_try
	{
		WalkForDeclarations(tree);
		Simplify(tree);
		WalkForClosures(tree);
		RefVar commands(GetArraySlotRef(tree, 1));
		long count = Length(commands);
		if (count > 0 && NodeKind(RefVar(GetArraySlotRef(commands, count - 1))) == tokenEQL)
			Warning("= at top level...did you mean := ?");
		fFunctionState->CopyClosedArgs();
		WalkForCode(tree, false);
		codeBlock = EndFunction();
	}
	cleanup
	{
		while (fFunctionStates != nil)
		{
			TFunctionState* state = fFunctionStates;
			fFunctionStates = state->fNext;
			delete state;
		}
	}
	end_try;
	while (fFunctionStates != nil)
	{
		TFunctionState* state = fFunctionStates;
		fFunctionStates = state->fNext;
		delete state;
	}
	return codeBlock;
}


// ROM 0x002c1404 ParseString__FRC6RefVar
// The forms in a string compiled into one function.  (The ROM's stack
// objects are destroyed by hand on an exception; here they are made on
// the heap for the same reason - a Throw runs no destructors.)
Ref
ParseString(RefArg str)
{
	TStringInputStream* stream = new TStringInputStream(str);
	TCompiler* compiler = new TCompiler(stream, false);
	RefVar codeBlock;
	newton_try
	{
		codeBlock = compiler->Compile();
	}
	cleanup
	{
		delete compiler;
		delete stream;
	}
	end_try;
	delete compiler;
	delete stream;
	return codeBlock;
}


// ROM 0x002c14d0 ParseFile__FPc
// Each form of a file compiled and run (showCodeBlocks prints the
// function, showLoadResults the result); a compiler error gets the file
// and line.  ==> the last form's result.
Ref
ParseFile(const char* filename)
{
	RefVar result(NILREF);
	RefVar codeBlock(NILREF);
	FILE* file = fopen(filename, "r");
	if (file == nil)
		ThrowMsg((char*) "couldn't open file");
	TStdioInputStream* stream = new TStdioInputStream(file, filename);
	TCompiler* compiler = new TCompiler(stream, true);
	newton_try
	{
		while (!feof(file))
		{
			codeBlock = compiler->Compile();
			if (GetFrameSlotRef(gVarFrame, Intern((char*) "showCodeBlocks")) != NILREF)
			{
				gREPout->Print("(#%X) ", (long) (Ref) codeBlock);
				PrintObject(codeBlock, 0);
				gREPout->Print("\r");
			}
			if ((Ref) codeBlock == NILREF)
				result = NILREF;
			else
				result = InterpretBlock(codeBlock, RefVar(gVarFrame));
			if (GetFrameSlotRef(gVarFrame, Intern((char*) "showLoadResults")) != NILREF)
			{
				gREPout->Print("[#%-6X] ", (long) (Ref) result);
				PrintObject(result, 0);
				gREPout->Print("\r");
			}
		}
	}
	newton_catch_all
	{
		if (Subexception((ExceptionName) _info.exception.name, (ExceptionName) "type.ref"))
		{
			RefVar data(**(RefStruct**) &_info.exception.data);
			if (IsFrame(data) && GetFrameSlotRef(data, RSSYMfilename) == NILREF)
			{
				SetFrameSlot(data, RSSYMfilename, RefVar(MakeString(stream->GetFilename())));
				SetFrameSlot(data, RSSYMlinenumber, RefVar(MAKEINT(stream->fLineNumber)));
			}
		}
		gREPout->ExceptionNotify(&_info.exception);
	}
	end_try;
	fclose(file);
	delete compiler;
	delete stream;
	return result;
}


/* -------------------------------------------------------------------------------
	Natives
------------------------------------------------------------------------------- */

// ROM 0x002b583c FCompile
Ref
FCompile(RefArg /*rcvr*/, RefArg str)
{
	return ParseString(str);
}


// ROM 0x002b5844 FDisasm
Ref
FDisasm(RefArg /*rcvr*/, RefArg fn)
{
	Disassemble(fn);
	return NILREF;
}


// (FDisasm has no entry in the ROM's built-in functions frame)
void
RegisterCompilerNatives(void)
{
	RegisterNativeFunction("FCompile", (void*) FCompile, 1);
}
