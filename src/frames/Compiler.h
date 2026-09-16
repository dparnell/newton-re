/*
	File:		frames/Compiler.h

	Contains:	The NewtonScript compiler: source text (TInputStream) to a
				function object.  TCompiler holds the lexer (Lexer.cpp), the
				Berkeley yacc parser (Parser.cpp, its tables generated into
				ParserTables.h/.cpp by tools/newton-rom/analysis/nsgrammar.py)
				and the code generator (Compiler.cpp); TFunctionState is the
				function being compiled, TLoopState the loop being compiled.

	The parser builds a parse tree of arrays [MAKEINT(kind), child...]
	whose kind is the token of the construct ('+', tokenIF, ...; the
	characters are their own tokens), which the code generator walks: once
	for declarations (locals, constants, nested functions - each nested
	function gets a TFunctionState, kept in the tree's slot 5), once for
	closures (which variables inner functions reach, so they go into the
	function's argFrame rather than the stack), then for code.  The
	compilerCompatibility global chooses between 1.x code blocks (0: every
	variable in the argFrame, class 'CodeBlock) and 2.x functions (1, the
	default: arguments and uncaptured locals on the value stack, class
	kFuncClass); dbgkeepvarnames/dbgnovarnames keep the variable names as
	'dbg1 debug information.

	The DDK has no header for the compiler; the layouts are the ROM's.
*/

#ifndef __COMPILER_H
#define __COMPILER_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

#include <stdio.h>

class TCompiler;
class TFunctionState;
class TLoopState;


/* -------------------------------------------------------------------------------
	Input streams: characters for the lexer
------------------------------------------------------------------------------- */

const UniChar kEndOfStream = 0xffff;

class TInputStream				// 0x108 bytes
{
public:
	TInputStream();
	virtual ~TInputStream();

	virtual UniChar	GetChar(void) = 0;				// slot +4: the next character, kEndOfStream at the end
	virtual void	UngetChar(UniChar c) = 0;		// slot +8
	virtual Boolean	End(void) = 0;					// slot +0xc

	const char*		GetFilename(void);
	void			SetFilename(const char* name);

	long			fLineNumber;	// +0x04  counting carriage returns
	char			fFilename[256];	// +0x08
};


class TStringInputStream : public TInputStream	// 0x110
{
public:
	TStringInputStream(RefArg str);

	UniChar			GetChar(void);
	void			UngetChar(UniChar c);
	Boolean			End(void);

	RefStruct		fString;		// +0x108
	long			fPosition;		// +0x10c
};


class TStdioInputStream : public TInputStream	// 0x10c
{
public:
	TStdioInputStream(FILE* file, const char* filename);

	UniChar			GetChar(void);
	void			UngetChar(UniChar c);
	Boolean			End(void);

	FILE*			fFile;			// +0x108
};


/* -------------------------------------------------------------------------------
	TLoopState: the break exits of the loop being compiled
------------------------------------------------------------------------------- */

class TLoopState						// 0xc
{
public:
	TLoopState(TFunctionState* function, TLoopState* enclosing);
	~TLoopState();

	void			AddExit(long pc);			// a branch placeholder at pc to patch
	void			PatchExits(long pc);		// to pc, at the loop's end

	TFunctionState*	fFunction;		// +0
	RefStruct		fExits;			// +4  the placeholders' pcs
	TLoopState*		fEnclosing;		// +8
};


/* -------------------------------------------------------------------------------
	TFunctionState: the function being compiled
------------------------------------------------------------------------------- */

enum MsgEnvComponent { kMsgEnvReceiver = 0, kMsgEnvImplementor = 1 };

class TFunctionState					// 0x5c
{
public:
	TFunctionState(TCompiler* compiler, RefArg args, TFunctionState* enclosing, int* funcDepth);
	~TFunctionState();

	long			CurPC(void);
	long			LitOffset(RefArg literal);
	void			EmitOne(unsigned char b);
	void			EmitThree(unsigned char b, long operand);
	void			Emit(int opcode, long operand);
	long			EmitPlaceholder(void);
	void			Backpatch(long pc, int opcode, long operand);
	void			AddLocals(RefArg names);
	void			DeclarationsFinished(void);
	void			ComputeInitialVarLocs(void);
	void			ComputeArgFrame(void);
	void			CopyClosedArgs(void);
	Boolean			IsLocalVariable(RefArg name);
	long			VariableIndex(RefArg name);
	Boolean			NoteVarReference(RefArg name);
	void			NoteMsgEnvReference(MsgEnvComponent component);
	void			AddConstant(RefArg name, RefArg value);
	Boolean			IsConstant(RefArg name);
	Boolean			IsLocalConstant(RefArg name);
	Ref				GetConstantValue(RefArg name, long* exists);
	Boolean			AtTopLevel(void);
	Ref				MakeCodeBlock(void);
	void			BeginLoop(void);
	long			AddLoopExit(void);
	void			EndLoop(void);

	TCompiler*		fCompiler;			// +0x00
	RefStruct		fArgs;				// +0x04  the argument names
	RefStruct		fLocals;			// +0x08  the locals' names (nil until one is declared)
	RefStruct		fConstants;			// +0x0c  a frame, _proto the enclosing function's (gConstantsFrame at the top)
	RefStruct		fInstructions;		// +0x10  the bytecode, grown by 0x80
	RefStruct		fLiterals;			// +0x14  the literals, grown by 0x10
	RefStruct		fArgFrame;			// +0x18  the argFrame (nil when none is needed)
	RefStruct		fClosedVars;		// +0x1c  a frame: the variables inner functions reach
	RefStruct		fVarLocs;			// +0x20  a frame: variable -> stack index, or 'closed
	long			fNumLocals;			// +0x24  locals on the stack (2.x)
	long			fPC;				// +0x28
	long			fNumLiterals;		// +0x2c
	long			fNumArgs;			// +0x30
	long			fNumLocalsDeclared;	// +0x34
	TLoopState*		fLoop;				// +0x38
	long			fFuncDepth;			// +0x3c  the debugger's numbering, -1 for none
	Boolean			fHasClosures;		// +0x40  an inner function was compiled
	Boolean			fUsesReceiver;		// +0x44  self is referenced
	Boolean			fUsesImplementor;	// +0x48  inherited is referenced
	Boolean			fClosesOverOuter;	// +0x4c  a variable of an enclosing function is referenced
	Boolean			fKeepVarNames;		// +0x50  debug information wanted
	TFunctionState*	fEnclosing;			// +0x54
	TFunctionState*	fNext;				// +0x58  the compiler's list of them
};


/* -------------------------------------------------------------------------------
	TCompiler
------------------------------------------------------------------------------- */

class TCompiler							// 0x34
{
public:
	TCompiler(TInputStream* stream, Boolean interactive);
	~TCompiler();

	Ref				Compile(void);

	// the lexer (Lexer.cpp)
	int				GetToken(void);
	int				yylex0(void);
	int				GetNumber(UniChar c);
	UniChar*		GetCharsUntil(UniChar terminator, Boolean isString, long& length);
	int				ReservedWordToken(const char* name);

	// the parser (Parser.cpp)
	int				Parser(void);
	Boolean			ParserStackOverflow(void);
	void			SyntaxError(const char* message);

	// errors and warnings
	void			Warning(const char* message);
	void			Error(long error);
	void			Error(long error, RefArg value);

	// code generation (Compiler.cpp)
	void			Emit(int opcode, long operand);
	void			EmitPush(RefArg value);
	void			EmitPop(void);
	void			EmitVarSet(RefArg name);
	void			EmitVarGet(RefArg name);
	void			EmitVarIncr(RefArg name);
	void			EmitFuncall(RefArg name, ULong numArgs);
	void			EmitBranch(ULong pc);
	long			EmitPlaceholder(void);
	void			EmitReturn(void);
	long			CurPC(void);
	void			Backpatch(ULong pc, int opcode, long operand);
	void			AddLocals(RefArg names);
	void			NewFunctionState(RefArg args, TFunctionState* enclosing, int* funcDepth);
	Ref				EndFunction(void);
	void			BeginLoop(void);
	long			AddLoopExit(void);
	void			EndLoop(void);
	Boolean			IsConstantExpr(RefArg expr);
	Ref				EvaluateConstantExpr(RefArg expr);
	Boolean			ClosureWalker(RefArg node, long kind, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5);
	void			WalkForClosures(RefArg tree);
	void			Simplify(RefArg tree);
	Boolean			WalkAssignment(RefArg lvalue, RefArg value, Boolean isEffect);
	Ref				WalkForPath(RefArg expr, long& nilForNil);
	long			WalkForStringer(RefArg expr);
	Boolean			WalkForCode(RefArg node, Boolean isEffect);
	Boolean			DeclarationWalker(RefArg node, long kind, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5);
	void			WalkForDeclarations(RefArg tree);

	TFunctionState*	fFunctionState;		// +0x00  the function being compiled
	TFunctionState*	fFunctionStates;	// +0x04  all of them, for deletion
	TInputStream*	fStream;			// +0x08
	long			fStackSize;			// +0x0c  the parser's stacks' depth
	RefStruct		fYaccStack;			// +0x10  the value stack, an array (locked)
	Ref*			fYaccValues;		// +0x14  its slots
	short*			fYaccStates;		// +0x18  the state stack
	Boolean			fInteractive;		// +0x1c  stop after each command
	long			fReserved;			// +0x20
	int*			fFuncDepth;			// +0x24  the debugger's function counter (nil: none)
	Boolean			fHasPushedToken;	// +0x28  GetToken's one-token lookahead
	int				fPushedToken;		// +0x2c
	RefStruct		fPushedValue;		// +0x30
};


// the parse tree
Ref		AllocatePT1(int kind, RefArg a);
Ref		AllocatePT2(int kind, RefArg a, RefArg b);
Ref		AllocatePT3(int kind, RefArg a, RefArg b, RefArg c);
Ref		AllocatePT5(int kind, RefArg a, RefArg b, RefArg c, RefArg d, RefArg e);
typedef Boolean (*WalkerProc)(void* context, RefArg node, long kind, RefArg a1, RefArg a2, RefArg a3, RefArg a4, RefArg a5);
void	WalkNodes(RefArg node, void* context, WalkerProc walker, Boolean postOrder);

// the parser's globals (Parser.cpp)
extern Ref		yyval;
extern Ref		yylval;
extern int		yychar;
extern long		yydebug;

// compiling
extern long		gCompilerCompatibility;			// 0x0c1022e0  0: 1.x code blocks, else 2.x functions
extern Ref		gFreqFuncNames;					// 0x0c1022e4  a frame: name -> index in gFreqFuncInfo
extern Ref		gConstFuncFrame;				// 0x0c1023b8  the constantFunctions global
extern long		gPrintLiteralsFlag;				// 0x0c1023c0  gPrintLiterals: print each function's literals

long	FreqFuncIndex(RefArg name, long numArgs);
Ref		ParseString(RefArg str);				// the string's forms compiled into one function
Ref		ParseFile(const char* filename);		// each form compiled and run; ==> the last result
void	ThrowExCompilerWithBadValue(long error, RefArg value);

// natives
Ref		FCompile(RefArg rcvr, RefArg str);
Ref		FDisasm(RefArg rcvr, RefArg fn);
void	RegisterCompilerNatives(void);

#endif	/* __COMPILER_H */
