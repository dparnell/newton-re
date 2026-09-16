/*
	File:		frames/Parser.cpp

	Contains:	The NewtonScript parser: TCompiler::Parser, a Berkeley yacc
				(byacc) parser over the ROM's tables (ParserTables.cpp,
				generated from the ROM by tools/newton-rom/analysis/
				nsgrammar.py; docs/frames/grammar.md lists the rules), with
				the grammar's actions building the parse tree of arrays
				[MAKEINT(kind), children...] that Compiler.cpp walks.

	The skeleton is byacc's (the tables yylhs, yylen, yydefred, yydgoto,
	yysindex, yyrindex, yygindex, yytable, yycheck as byacc lays them
	out): the state stack holds shorts, the value stack Refs (an array
	object, locked, so the tree is safe from the collector; ParserStack-
	Overflow grows both by a quarter).  The value stack's top (yyval,
	yylval) are GC roots.  In interactive mode the parser returns after
	each ';'-terminated command (the mid-rule action of command_plus), so
	the REP can run one at a time.
*/

#include "Compiler.h"
#include "ParserTables.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "REPTranslators.h"
#include "RSSymbols.h"
#include "NSErrors.h"
#include "NewtonExceptions.h"
#include "NewtonMemory.h"

#include <stdio.h>
#include <string.h>

// the parser's state (0x0c102668-)
long		yydebug = 0;
long		yynerrs = 0;
long		yyerrflag = 0;
int			yychar = -1;
long		yystate = 0;
short*		yyssp = nil;
Ref*		yyvsp = nil;
short*		yyss = nil;
Ref			yyval = NILREF;
Ref			yylval = NILREF;

#define YYABORT		return 1
#define YYACCEPT	return 0

// the children of a rule's right-hand side: $n is yyvsp[n - yylen]
#define YYVAL(n)	(yyvsp[(n) - count])


// ROM 0x00300d68 SyntaxError__9TCompilerFPc
// "message--read <token>, but wanted <the tokens the state accepts>".
void
TCompiler::SyntaxError(const char* message)
{
	if (yychar != -1)
	{
		char value[64];
		const char* tokenName = (yychar > YYMAXTOKEN || yyname[yychar] == nil) ? "illegal symbol" : yyname[yychar];
		if (yychar == tokenSYMBOL)
			snprintf(value, sizeof(value), " \"%s\"", SymbolName(yylval));
		else if (yychar == tokenINTEGER)
			snprintf(value, sizeof(value), " %ld", RINT(yylval));
		else if (yychar == tokenREAL)
			snprintf(value, sizeof(value), " %#g", CDouble(RefVar(yylval)));
		else
			value[0] = 0;
		char text[512];
		long n = snprintf(text, sizeof(text), "%s--read %s%s, but wanted ", message, tokenName, value);
		long wanted = 0;
		for (int token = 0; token <= YYMAXTOKEN; token++)
		{
			long i;
			if (((i = yyrindex[yystate]) != 0 && (i += token) >= 0 && i <= YYTABLESIZE && yycheck[i] == token)
			 || ((i = yysindex[yystate]) != 0 && (i += token) >= 0 && i <= YYTABLESIZE && yycheck[i] == token))
			{
				if (wanted++ != 0 && n < (long) sizeof(text) - 3)
					n += snprintf(text + n, sizeof(text) - n, ", ");
				if (n < (long) sizeof(text) - 1)
					n += snprintf(text + n, sizeof(text) - n, "%s", yyname[token]);
			}
		}
		Error(kNSErrSyntaxError, RefVar(MakeString(text)));
	}
	Error(kNSErrSyntaxError, RefVar(MakeString(message)));
}


// ROM 0x00300c50 ParserStackOverflow__9TCompilerFv
// Both stacks grown by a quarter; ==> true when they cannot be.
Boolean
TCompiler::ParserStackOverflow(void)
{
	Boolean failed = true;
	newton_try
	{
		long depth = yyssp - yyss;
		long size = (fStackSize * 5) / 4;
		short* states = new short[size];
		if (states != nil)
		{
			BlockMove(fYaccStates, states, fStackSize * sizeof(short));
			delete[] fYaccStates;
			fYaccStates = states;
			yyssp = states + depth;
			yyss = states;
			UnlockRef(fYaccStack);
			SetLength(fYaccStack, size);
			LockRef(fYaccStack);
			fYaccValues = Slots(fYaccStack);
			yyvsp = fYaccValues + depth;
			failed = false;
			fStackSize = size;
		}
	}
	newton_catch((ExceptionName) "evt.ex")
	{ }
	end_try;
	return failed;
}


// a symbol's name for a warning about duplicates
static void
DuplicateWarning(TCompiler* compiler, const char* what, Ref sym)
{
	char text[256];
	snprintf(text, sizeof(text), "Duplicate %s name: %s\r", what, SymbolName(sym));
	compiler->Warning(text);
}


// ROM 0x002fd9b0 Parser__9TCompilerFv
// ==> 0 when the input parsed (yyval holds the commands), 1 on an error.
int
TCompiler::Parser(void)
{
	yynerrs = 0;
	yyerrflag = 0;
	yychar = -1;
	yyvsp = fYaccValues;
	yyssp = fYaccStates;
	yyss = fYaccStates;
	yystate = 0;
	*yyssp = 0;

	for (;;)
	{
		long rule;
		if ((rule = yydefred[yystate]) == 0)
		{
			// a token is needed
			if (yychar < 0)
			{
				if ((yychar = GetToken()) < 0)
					yychar = 0;
				if (yydebug)
				{
					const char* name = (yychar <= YYMAXTOKEN && yyname[yychar] != nil) ? yyname[yychar] : "illegal-symbol";
					gREPout->Print("yydebug: state %d, reading %d (%s)\r", yystate, yychar, name);
				}
			}
			long i;
			if ((i = yysindex[yystate]) != 0 && (i += yychar) >= 0 && i <= YYTABLESIZE && yycheck[i] == yychar)
			{
				// shift
				if (yydebug)
					gREPout->Print("yydebug: state %d, shifting to state %d\r", yystate, yytable[i]);
				if (yyssp >= yyss + fStackSize - 1 && ParserStackOverflow())
				{
					SyntaxError("yacc stack overflow");
					YYABORT;
				}
				yystate = yytable[i];
				*++yyssp = (short) yystate;
				*++yyvsp = yylval;
				yychar = -1;
				if (yyerrflag > 0)
					yyerrflag--;
				continue;
			}
			if ((i = yyrindex[yystate]) != 0 && (i += yychar) >= 0 && i <= YYTABLESIZE && yycheck[i] == yychar)
				rule = yytable[i];
			else
			{
				// an error: recover by discarding states until one shifts the error token, then tokens
				if (yyerrflag == 0)
				{
					SyntaxError("syntax error");
					yynerrs++;
				}
				if (yyerrflag < 3)
				{
					yyerrflag = 3;
					for (;;)
					{
						if ((i = yysindex[*yyssp]) != 0 && (i += YYERRCODE) >= 0 && i <= YYTABLESIZE && yycheck[i] == YYERRCODE)
						{
							if (yydebug)
								gREPout->Print("yydebug: state %d, error recovery shifting to state %d\r", *yyssp, yytable[i]);
							if (yyssp >= yyss + fStackSize - 1 && ParserStackOverflow())
							{
								SyntaxError("yacc stack overflow");
								YYABORT;
							}
							yystate = yytable[i];
							*++yyssp = (short) yystate;
							*++yyvsp = yylval;
							break;
						}
						if (yydebug)
							gREPout->Print("yydebug: error recovery discarding state %d\r", *yyssp);
						if (yyssp <= yyss)
							YYABORT;
						yyssp--;
						yyvsp--;
					}
				}
				else
				{
					if (yychar == 0)
						YYABORT;
					if (yydebug)
					{
						const char* name = (yychar <= YYMAXTOKEN && yyname[yychar] != nil) ? yyname[yychar] : "illegal-symbol";
						gREPout->Print("yydebug: state %d, error recovery discards token %d (%s)\r", yystate, yychar, name);
					}
					yychar = -1;
				}
				continue;
			}
		}

		// reduce by rule
		if (yydebug)
			gREPout->Print("yydebug: state %d, reducing by rule %d (%s)\r", yystate, rule, yyrule[rule]);
		long count = yylen[rule];
		yyval = yyvsp[1 - count];
		switch (rule)
		{
		case 1:		// input :
		case 105:	// expr_star :
		case 109:	// expr_seq :
			yyval = AllocateArray(RSSYMarray, 0);
			break;

		case 3:		// command_plus : command
		case 99:	// handle_plus : handle_expr
		case 107:	// expr_plus : expr
		case 110:	// expr_seq : expr
		case 145:	// sexpr_plus : sexpr
			yyval = AllocateArray(RSSYMarray, 1);
			SetArraySlotRef(yyval, 0, YYVAL(1));
			break;

		case 4:		// $$1 : (after command_plus ';')
			yyval = yyvsp[-1];
			if (fInteractive)
				YYACCEPT;			// one command at a time
			break;

		case 5:		// command_plus : command_plus ';' $$1 command
			yyval = YYVAL(1);
			AddArraySlot(RefVar(yyval), RefVar(YYVAL(4)));
			break;

		case 11:	// expr : tokenSELF
			yyval = AllocatePT1(tokenSELF, RefVar(NILREF));
			break;
		case 12:	// expr : tokenBEGIN expr_seq tokenEND
			yyval = AllocatePT1(tokenBEGIN, RefVar(YYVAL(2)));
			break;
		case 13:	// expr : '(' expr ')'
		case 138:	// sexpr : '[' sexpr_star ']'
		case 140:	// sexpr : '{' sexpr_frame_slot_star '}'
			yyval = YYVAL(2);
			break;

		// the binary operators: [op, left, right]
		case 14: yyval = AllocatePT2('+', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 15: yyval = AllocatePT2('-', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 16: yyval = AllocatePT2('*', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 17: yyval = AllocatePT2('/', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 18: yyval = AllocatePT2(tokenDIV, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 19: yyval = AllocatePT2('&', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 20: yyval = AllocatePT2(tokenAMPERAMPER, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 21: yyval = AllocatePT2(tokenMOD, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 22:	// expr : '-' expr
			yyval = AllocatePT1(tokenUMINUS, RefVar(YYVAL(2)));
			break;
		case 23: yyval = AllocatePT2(tokenLSHIFT, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 24: yyval = AllocatePT2(tokenRSHIFT, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 25: yyval = AllocatePT2('<', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 26: yyval = AllocatePT2('>', RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 27: yyval = AllocatePT2(tokenLEQ, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 28: yyval = AllocatePT2(tokenGEQ, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 29: yyval = AllocatePT2(tokenEQL, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 30: yyval = AllocatePT2(tokenNEQ, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 31: yyval = AllocatePT2(tokenAND, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 32: yyval = AllocatePT2(tokenOR, RefVar(YYVAL(1)), RefVar(YYVAL(3))); break;
		case 33:	// expr : tokenNOT expr
			yyval = AllocatePT1(tokenNOT, RefVar(YYVAL(2)));
			break;
		case 34:	// expr : lvalue tokenEXISTS
		case 35:	// expr : send_exists_expr tokenEXISTS
			yyval = AllocatePT1(tokenEXISTS, RefVar(YYVAL(1)));
			break;

		case 47:	// constant : tokenCONST
		case 48:	// constant : tokenINTEGER
		case 49:	// constant : tokenREAL
		case 51:	// constant : tokenREFCONST
			yyval = AllocatePT1(tokenCONST, RefVar(YYVAL(1)));
			break;
		case 50:	// constant : '\'' sexpr
			yyval = AllocatePT1(tokenCONST, RefVar(YYVAL(2)));
			break;
		case 52:	// lvalue : tokenSYMBOL
			yyval = AllocatePT1(tokenSYMBOL, RefVar(YYVAL(1)));
			break;
		case 53:	// lvalue : expr '.' '(' expr ')'
			yyval = AllocatePT2('.', RefVar(YYVAL(1)), RefVar(YYVAL(4)));
			break;
		case 54:	// lvalue : expr '.' tokenSYMBOL
		{
			RefVar slot(AllocatePT1(tokenCONST, RefVar(YYVAL(3))));
			yyval = AllocatePT2('.', RefVar(YYVAL(1)), slot);
			break;
		}
		case 55:	// lvalue : expr '[' expr ']'
			yyval = AllocatePT2('[', RefVar(YYVAL(1)), RefVar(YYVAL(3)));
			break;
		case 56:	// assignment : lvalue tokenASSIGN expr
			yyval = AllocatePT2(tokenASSIGN, RefVar(YYVAL(1)), RefVar(YYVAL(3)));
			break;
		case 57:	// local_decl : tokenLOCAL local_plus  ([names and initialisers, type])
			yyval = AllocatePT2(tokenLOCAL, RefVar(GetArraySlotRef(YYVAL(2), 0)), RefVar(GetArraySlotRef(YYVAL(2), 1)));
			break;
		case 58:	// constant_decl : tokenCONSTANT constant_init_plus
			yyval = AllocatePT1(tokenCONSTANT, RefVar(YYVAL(2)));
			break;
		case 59:	// global_decl : tokenGLOBAL tokenSYMBOL
		{
			RefVar value(AllocatePT1(tokenCONST, RefVar(NILREF)));
			yyval = AllocatePT2(tokenGLOBAL, RefVar(YYVAL(2)), value);
			break;
		}
		case 60:	// global_decl : tokenGLOBAL tokenSYMBOL tokenASSIGN expr
			yyval = AllocatePT2(tokenGLOBAL, RefVar(YYVAL(2)), RefVar(YYVAL(4)));
			break;
		case 61:	// global_decl : tokenGLOBAL tokenSYMBOL '(' formal_args ')' expr
		case 62:	// global_decl : tokenFUNC tokenSYMBOL '(' formal_args ')' expr
		{
			// DefGlobalFn('name, func (args) expr)
			RefVar args(AllocateArray(RSSYMarray, 2));
			SetArraySlotRef(args, 0, AllocatePT1(tokenCONST, RefVar(YYVAL(2))));
			RefVar formals(YYVAL(4));
			RefVar fn(AllocatePT5(tokenFUNC, RefVar(GetArraySlotRef(formals, 0)), RefVar(YYVAL(6)), RefVar(NILREF), RefVar(GetArraySlotRef(formals, 1)), RefVar(NILREF)));
			SetArraySlotRef(args, 1, fn);
			yyval = AllocatePT2(tokenCALL, RefVar(RSSYMdefglobalfn), args);
			break;
		}
		case 63:	// break_expr : tokenBREAK expr
			yyval = AllocatePT1(tokenBREAK, RefVar(YYVAL(2)));
			break;
		case 64:	// break_expr : tokenBREAK
			yyval = AllocatePT1(tokenBREAK, RefVar(AllocatePT1(tokenCONST, RefVar(NILREF))));
			break;
		case 65:	// return_expr : tokenRETURN expr
			yyval = AllocatePT1(tokenRETURN, RefVar(YYVAL(2)));
			break;
		case 66:	// return_expr : tokenRETURN
			yyval = AllocatePT1(tokenRETURN, RefVar(AllocatePT1(tokenCONST, RefVar(NILREF))));
			break;
		case 67:	// funcall_expr : tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT2(tokenCALL, RefVar(YYVAL(1)), RefVar(YYVAL(3)));
			break;
		case 68:	// funcall_expr : tokenCALL expr tokenWITH '(' expr_star ')'
			yyval = AllocatePT2(tokenINVOKE, RefVar(YYVAL(2)), RefVar(YYVAL(5)));
			break;
		case 69:	// send_exists_expr : expr ':' tokenSYMBOL   [':', message, receiver]
			yyval = AllocatePT2(':', RefVar(YYVAL(3)), RefVar(YYVAL(1)));
			break;
		case 70:	// send_exists_expr : ':' tokenSYMBOL
			yyval = AllocatePT2(':', RefVar(YYVAL(2)), RefVar(AllocatePT1(tokenSELF, RefVar(NILREF))));
			break;
		case 71:	// send_expr : expr ':' tokenSYMBOL '(' expr_star ')'   [':', message, receiver, args]
			yyval = AllocatePT3(':', RefVar(YYVAL(3)), RefVar(YYVAL(1)), RefVar(YYVAL(5)));
			break;
		case 72:	// send_expr : tokenINHERITED ':' tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT3(':', RefVar(YYVAL(3)), RefVar(NILREF), RefVar(YYVAL(5)));
			break;
		case 73:	// send_expr : ':' tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT3(':', RefVar(YYVAL(2)), RefVar(AllocatePT1(tokenSELF, RefVar(NILREF))), RefVar(YYVAL(4)));
			break;
		case 74:	// send_expr : expr tokenSENDIFDEFINED tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT3(tokenSENDIFDEFINED, RefVar(YYVAL(3)), RefVar(YYVAL(1)), RefVar(YYVAL(5)));
			break;
		case 75:	// send_expr : tokenINHERITED tokenSENDIFDEFINED tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT3(tokenSENDIFDEFINED, RefVar(YYVAL(3)), RefVar(NILREF), RefVar(YYVAL(5)));
			break;
		case 76:	// send_expr : tokenSENDIFDEFINED tokenSYMBOL '(' expr_star ')'
			yyval = AllocatePT3(tokenSENDIFDEFINED, RefVar(YYVAL(2)), RefVar(AllocatePT1(tokenSELF, RefVar(NILREF))), RefVar(YYVAL(4)));
			break;
		case 77:	// if_expr : tokenIF expr tokenTHEN expr tokenELSE expr
			yyval = AllocatePT3(tokenIF, RefVar(YYVAL(2)), RefVar(YYVAL(4)), RefVar(YYVAL(6)));
			break;
		case 78:	// if_expr : tokenIF expr tokenTHEN expr
			yyval = AllocatePT3(tokenIF, RefVar(YYVAL(2)), RefVar(YYVAL(4)), RefVar(NILREF));
			break;
		case 84:	// infinite_loop : tokenLOOP expr
			yyval = AllocatePT1(tokenLOOP, RefVar(YYVAL(2)));
			break;
		case 85:	// for_loop : tokenFOR tokenSYMBOL tokenASSIGN expr tokenTO expr tokenDO expr
			yyval = AllocatePT5(tokenFOR, RefVar(YYVAL(2)), RefVar(YYVAL(4)), RefVar(YYVAL(6)), RefVar(AllocatePT1(tokenCONST, RefVar(MAKEINT(1)))), RefVar(YYVAL(8)));
			break;
		case 86:	// for_loop : tokenFOR tokenSYMBOL tokenASSIGN expr tokenTO expr tokenBY expr tokenDO expr
			yyval = AllocatePT5(tokenFOR, RefVar(YYVAL(2)), RefVar(YYVAL(4)), RefVar(YYVAL(6)), RefVar(YYVAL(8)), RefVar(YYVAL(10)));
			break;
		case 87:	// with_loop : tokenFOREACH tokenSYMBOL ',' tokenSYMBOL optional_deeply tokenIN expr withverb expr
		{
			// [FOREACH, verb, collection, body, [slot, value], deeply]
			RefVar vars(AllocateArray(RSSYMarray, 2));
			SetArraySlotRef(vars, 0, YYVAL(2));
			SetArraySlotRef(vars, 1, YYVAL(4));
			yyval = AllocatePT5(tokenFOREACH, RefVar(YYVAL(8)), RefVar(YYVAL(7)), RefVar(YYVAL(9)), vars, RefVar(YYVAL(5)));
			break;
		}
		case 88:	// with_loop : tokenFOREACH tokenSYMBOL optional_deeply tokenIN expr withverb expr
		{
			RefVar vars(AllocateArray(RSSYMarray, 1));
			SetArraySlotRef(vars, 0, YYVAL(2));
			yyval = AllocatePT5(tokenFOREACH, RefVar(YYVAL(6)), RefVar(YYVAL(5)), RefVar(YYVAL(7)), vars, RefVar(YYVAL(3)));
			break;
		}
		case 89:	// optional_deeply :
		case 122:	// local_clause :
			yyval = NILREF;
			break;
		case 90:	// optional_deeply : tokenDEEPLY
			yyval = TRUEREF;
			break;
		case 91:	// withverb : tokenDO
			yyval = RSSYMmap;
			break;
		case 92:	// withverb : tokenSYMBOL
			if (!EQRef(YYVAL(1), RSSYMcollect))
				SyntaxError("FOREACH requires DO or COLLECT");
			yyval = RSSYMcollect;
			break;
		case 93:	// while_loop : tokenWHILE expr tokenDO expr
			yyval = AllocatePT2(tokenWHILE, RefVar(YYVAL(2)), RefVar(YYVAL(4)));
			break;
		case 94:	// repeat_loop : tokenREPEAT expr_seq tokenUNTIL expr
			yyval = AllocatePT2(tokenREPEAT, RefVar(YYVAL(2)), RefVar(YYVAL(4)));
			break;
		case 95:	// lambda_expr : tokenFUNC '(' formal_args ')' expr   [FUNC, arg names, body, native, arg types, state]
		{
			RefVar formals(YYVAL(3));
			yyval = AllocatePT5(tokenFUNC, RefVar(GetArraySlotRef(formals, 0)), RefVar(YYVAL(5)), RefVar(NILREF), RefVar(GetArraySlotRef(formals, 1)), RefVar(NILREF));
			break;
		}
		case 96:	// lambda_expr : tokenFUNC tokenNATIVE '(' formal_args ')' expr
		{
			RefVar formals(YYVAL(4));
			yyval = AllocatePT5(tokenFUNC, RefVar(GetArraySlotRef(formals, 0)), RefVar(YYVAL(6)), RefVar(TRUEREF), RefVar(GetArraySlotRef(formals, 1)), RefVar(NILREF));
			break;
		}
		case 97:	// lambda_expr : tokenFUNC '+' '(' formal_args ')' expr
		{
			RefVar formals(YYVAL(4));
			yyval = AllocatePT5(tokenFUNC, RefVar(GetArraySlotRef(formals, 0)), RefVar(YYVAL(6)), RefVar(TRUEREF), RefVar(GetArraySlotRef(formals, 1)), RefVar(NILREF));
			break;
		}
		case 98:	// try_expr : tokenTRY expr_seq handle_plus
		{
			RefVar body(AllocatePT1(tokenBEGIN, RefVar(YYVAL(2))));
			yyval = AllocatePT2(tokenTRY, body, RefVar(YYVAL(3)));
			break;
		}
		case 100:	// handle_plus : handle_plus handle_expr
			yyval = YYVAL(1);
			AddArraySlot(RefVar(yyval), RefVar(YYVAL(2)));
			break;
		case 101:	// handle_expr : tokenONEXCEPTION tokenSYMBOL tokenDO expr
			yyval = AllocatePT2(tokenONEXCEPTION, RefVar(YYVAL(2)), RefVar(YYVAL(4)));
			break;
		case 102:	// constructor : '[' expr_star ']'
			yyval = AllocatePT1(tokenBUILDARRAY, RefVar(YYVAL(2)));
			break;
		case 103:	// constructor : '[' tokenSYMBOL ':' expr_star ']'
			yyval = AllocatePT1(tokenBUILDARRAY, RefVar(YYVAL(4)));
			SetClass(RefVar(YYVAL(4)), RefVar(YYVAL(2)));
			break;
		case 104:	// constructor : '{' frame_slot_star '}'
			yyval = AllocatePT1(tokenBUILDFRAME, RefVar(YYVAL(2)));
			break;
		case 108:	// expr_plus : expr_plus ',' expr
		case 111:	// expr_seq : expr_seq ';' expr
		case 146:	// sexpr_plus : sexpr_plus ',' sexpr
			yyval = YYVAL(1);
			AddArraySlot(RefVar(yyval), RefVar(YYVAL(3)));
			break;

		case 112:	// formal_args :   ([names], [types])
		{
			yyval = AllocateArray(RSSYMarray, 2);
			SetArraySlotRef(yyval, 0, AllocateArray(RSSYMarray, 0));
			SetArraySlotRef(yyval, 1, AllocateArray(RSSYMarray, 0));
			break;
		}
		case 114:	// arg_plus : tokenSYMBOL
		{
			yyval = AllocateArray(RSSYMarray, 2);
			RefVar names(AllocateArray(RSSYMarray, 1));
			RefVar types(AllocateArray(RSSYMarray, 1));
			SetArraySlotRef(yyval, 0, names);
			SetArraySlotRef(yyval, 1, types);
			SetArraySlotRef(names, 0, YYVAL(1));
			break;
		}
		case 115:	// arg_plus : tokenSYMBOL tokenSYMBOL   (type name)
		{
			yyval = AllocateArray(RSSYMarray, 2);
			RefVar names(AllocateArray(RSSYMarray, 1));
			RefVar types(AllocateArray(RSSYMarray, 1));
			SetArraySlotRef(yyval, 0, names);
			SetArraySlotRef(yyval, 1, types);
			SetArraySlotRef(names, 0, YYVAL(2));
			SetArraySlotRef(types, 0, YYVAL(1));
			break;
		}
		case 116:	// arg_plus : arg_plus ',' tokenSYMBOL
		case 117:	// arg_plus : arg_plus ',' tokenSYMBOL tokenSYMBOL
		{
			yyval = YYVAL(1);
			Ref name = rule == 116 ? YYVAL(3) : YYVAL(4);
			Ref type = rule == 116 ? NILREF : YYVAL(3);
			RefVar names(GetArraySlotRef(yyval, 0));
			RefVar types(GetArraySlotRef(yyval, 1));
			{
				TObjectIterator iter(names);
				for (; !iter.Done(); iter.Next())
					if (EQRef(iter.fValue, name))
					{
						DuplicateWarning(this, "argument", name);
						break;
					}
			}
			AddArraySlot(names, RefVar(name));
			AddArraySlot(types, RefVar(type));
			break;
		}
		case 118:	// local_plus : tokenSYMBOL local_clause   ([[name, initialiser, ...], type])
			if (YYVAL(2) != NILREF)
			{
				// a typed local: the clause has the name, $1 is the type
				yyval = YYVAL(2);
				SetArraySlotRef(yyval, 1, YYVAL(1));
			}
			else
			{
				yyval = AllocateArray(RSSYMarray, 2);
				RefVar pairs(AllocateArray(RSSYMarray, 2));
				SetArraySlotRef(yyval, 0, pairs);
				SetArraySlotRef(pairs, 0, YYVAL(1));
			}
			break;
		case 119:	// local_plus : tokenSYMBOL tokenASSIGN expr
		case 124:	// local_clause : tokenSYMBOL tokenASSIGN expr
		{
			yyval = AllocateArray(RSSYMarray, 2);
			RefVar pairs(AllocateArray(RSSYMarray, 2));
			SetArraySlotRef(yyval, 0, pairs);
			SetArraySlotRef(pairs, 0, YYVAL(1));
			SetArraySlotRef(pairs, 1, YYVAL(3));
			break;
		}
		case 120:	// local_plus : local_plus ',' tokenSYMBOL
		case 121:	// local_plus : local_plus ',' tokenSYMBOL tokenASSIGN expr
		{
			yyval = YYVAL(1);
			Ref name = YYVAL(3);
			Ref init = rule == 121 ? YYVAL(5) : NILREF;
			RefVar pairs(GetArraySlotRef(yyval, 0));
			{
				TObjectIterator iter(pairs);
				for (; !iter.Done(); iter.Next())
					if (EQRef(iter.fValue, name))
					{
						DuplicateWarning(this, "variable", name);
						break;
					}
			}
			AddArraySlot(pairs, RefVar(name));
			AddArraySlot(pairs, RefVar(init));
			break;
		}
		case 123:	// local_clause : tokenSYMBOL
		{
			yyval = AllocateArray(RSSYMarray, 2);
			RefVar pairs(AllocateArray(RSSYMarray, 2));
			SetArraySlotRef(yyval, 0, pairs);
			SetArraySlotRef(pairs, 0, YYVAL(1));
			break;
		}
		case 125:	// constant_init_plus : tokenSYMBOL tokenASSIGN expr
			yyval = AllocateArray(RSSYMarray, 2);
			SetArraySlotRef(yyval, 0, YYVAL(1));
			SetArraySlotRef(yyval, 1, YYVAL(3));
			break;
		case 126:	// constant_init_plus : constant_init_plus ',' tokenSYMBOL tokenASSIGN constant
			yyval = YYVAL(1);
			AddArraySlot(RefVar(yyval), RefVar(YYVAL(3)));
			AddArraySlot(RefVar(yyval), RefVar(YYVAL(5)));
			break;
		case 127:	// frame_slot_star :
		case 147:	// sexpr_frame_slot_star :
			yyval = AllocateFrame();
			break;
		case 129:	// frame_slot_plus : tokenSYMBOL ':' expr
		case 149:	// sexpr_frame_slot_plus : tokenSYMBOL ':' sexpr
			yyval = AllocateFrame();
			SetFrameSlot(RefVar(yyval), RefVar(YYVAL(1)), RefVar(YYVAL(3)));
			break;
		case 130:	// frame_slot_plus : frame_slot_plus ',' tokenSYMBOL ':' expr
		case 150:	// sexpr_frame_slot_plus : sexpr_frame_slot_plus ',' tokenSYMBOL ':' sexpr
			yyval = YYVAL(1);
			if (FrameHasSlotRef(yyval, YYVAL(3)))
			{
				char text[256];
				snprintf(text, sizeof(text), "duplicate slot name: %s", SymbolName(YYVAL(3)));
				Warning(text);
			}
			SetFrameSlot(RefVar(yyval), RefVar(YYVAL(3)), RefVar(YYVAL(5)));
			break;
		case 133:	// sexpr : '-' tokenINTEGER
			yyval = MAKEINT(-RINT(YYVAL(2)));
			break;
		case 135:	// sexpr : '-' tokenREAL
			yyval = MakeReal(-CDouble(RefVar(YYVAL(2))));
			break;
		case 139:	// sexpr : '[' tokenSYMBOL ':' sexpr_star ']'
			yyval = YYVAL(4);
			SetClass(RefVar(yyval), RefVar(YYVAL(2)));
			break;
		case 142:	// path_expr : path_expr '.' tokenSYMBOL
			if (EQRef(ClassOf(RefVar(YYVAL(1))), RSSYMpathexpr))
			{
				yyval = YYVAL(1);
				AddArraySlot(RefVar(yyval), RefVar(YYVAL(3)));
			}
			else
			{
				yyval = AllocateArray(RSSYMpathexpr, 2);
				SetArraySlotRef(yyval, 0, YYVAL(1));
				SetArraySlotRef(yyval, 1, YYVAL(3));
			}
			break;
		case 143:	// sexpr_star :
			yyval = AllocateArray(RSSYMarray, 0);
			break;

		default:	// $$ = $1
			break;
		}

		// the goto after the reduction
		yyssp -= count;
		yystate = *yyssp;
		yyvsp -= count;
		long lhs = yylhs[rule];
		if (yystate == 0 && lhs == 0)
		{
			if (yydebug)
				gREPout->Print("yydebug: after reduction, shifting from state 0 to state %d\r", YYFINAL);
			yystate = YYFINAL;
			*++yyssp = YYFINAL;
			*++yyvsp = yyval;
			if (yychar < 0)
			{
				if ((yychar = GetToken()) < 0)
					yychar = 0;
				if (yydebug)
				{
					const char* name = (yychar <= YYMAXTOKEN && yyname[yychar] != nil) ? yyname[yychar] : "illegal-symbol";
					gREPout->Print("yydebug: state %d, reading %d (%s)\r", YYFINAL, yychar, name);
				}
			}
			if (yychar == 0)
				YYACCEPT;
			continue;
		}
		long i;
		if ((i = yygindex[lhs]) != 0 && (i += yystate) >= 0 && i <= YYTABLESIZE && yycheck[i] == yystate)
			yystate = yytable[i];
		else
			yystate = yydgoto[lhs];
		if (yydebug)
			gREPout->Print("yydebug: after reduction, shifting from state %d to state %d\r", *yyssp, yystate);
		if (yyssp >= yyss + fStackSize - 1 && ParserStackOverflow())
		{
			SyntaxError("yacc stack overflow");
			YYABORT;
		}
		*++yyssp = (short) yystate;
		*++yyvsp = yyval;
	}
}
