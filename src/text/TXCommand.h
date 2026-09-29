/*
	File:		text/TXCommand.h

	Contains:	The edit commands: an edit made undoable.

				A `TXCommand` is something done to a document that can be
				undone and redone: `Execute` does it the first time, undoes
				it the second and redoes it the third (and so on), going
				round the states nought (to do), one (done), two (undone);
				four is a command that failed or cannot be undone, which
				Execute leaves alone.

				`TXEditCommand` is how the text engine's own edits undo:
				before the edit, the stretch it will change is exported
				into a *local container* on a temporary stream (the undo
				container - TXContainer.h), with the rulers of the
				paragraph at its end in a second one; undoing puts the
				container back over what the edit made, and saving the
				edit's result the same way first (the redo container) is
				what makes redoing it possible.  Which values are kept
				depends on the edit (`GetUndoParams`): the runs alone for a
				restyle, the rulers alone for a change of paragraph,
				everything otherwise.  The kinds: 1 typing (TXKeyCommand),
				2 a restyle (UpdateRangeRuns), 3 a paragraph change
				(UpdateRangeRulers), 4 a move (TXMoveTextCommand) and 5 a
				replacement (TXReplaceTextCommand).

				`TXKeyCommand` is typing: one command for as long as the
				keys follow on from one another (`NewKey` answers 3 when
				this one will not take the next key, which then starts a
				command of its own), done as it goes - so Execute's first
				call undoes it.  A delete key widens what is kept to the
				start of the line before, since backspacing may reach it.

				`TXMoveTextCommand` moves (or copies) a stretch to another
				place through a container of its own; undoing a move is
				moving it back.  `TXReplaceTextCommand` is ReplaceRange
				undoably.

				The ROM makes them with operator new and a separate `I...`
				initialiser, which answers an error and, through `failed`,
				whether the undo container could not be made (the command
				is then done but cannot be undone).

	Reconstructed from the MP2x00 US ROM (0x00232d04-0x00233f04); each
	function cites its origin.  The SaveUndoContainer, RedoHilite, AddKey
	and TXMoveTextCommand::DoIt bodies were read from the assembly.
*/

#ifndef __TXCOMMAND_H
#define __TXCOMMAND_H

#ifndef __TEXTENSION_H
#include "Textension.h"
#endif

class TXStream;

// TXCommand::fState
enum
{
	kTXCommandToDo		= 0,
	kTXCommandDone		= 1,
	kTXCommandUndone	= 2,
	kTXCommandDead		= 4
};

// TXCommand::fKind
enum
{
	kTXKeyCommand		= 1,
	kTXRunsCommand		= 2,
	kTXRulersCommand	= 3,
	kTXMoveCommand		= 4,
	kTXReplaceCommand	= 5
};


// The ROM's object is 0x10 bytes.
class TXCommand
{
public:
					TXCommand();									// ROM 0x00232d04 __ct__9TXCommandFv
	virtual			~TXCommand();									// ROM 0x00232d38 __dt__9TXCommandFv
	// `action`: what the display did (TXDisplay::fLastEditAction).
	virtual NewtonErr DoIt(int* action) = 0;						// (pure: +0x04)
	virtual NewtonErr UndoIt(int* action) = 0;						// (pure: +0x08)
	virtual NewtonErr RedoIt(int* action) = 0;						// (pure: +0x0c)

	void			ITXCommand(Textension* text, int kind);			// ROM 0x00233474 ITXCommand__9TXCommandFP10Textensioni
	// Done, undone or redone, by the state.
	NewtonErr		Execute(int* action);							// ROM 0x0023380c Execute__9TXCommandFPi

	Textension*		fText;			// +0x04
	int				fKind;			// +0x08
	Boolean			fCanUndo;		// +0x0c
	unsigned char	fState;			// +0x0d
};


// The ROM's object is 0x68 bytes.
class TXEditCommand : public TXCommand
{
public:
					TXEditCommand();								// ROM 0x00233c44 __ct__13TXEditCommandFv
	virtual			~TXEditCommand();								// ROM 0x00233dec __dt__13TXEditCommandFv
	virtual NewtonErr DoIt(int* action);							// ROM 0x0023300c DoIt__13TXEditCommandFPi
	virtual NewtonErr UndoIt(int* action);							// ROM 0x002330fc UndoIt__13TXEditCommandFPi
	virtual NewtonErr RedoIt(int* action);							// ROM 0x002332d8 RedoIt__13TXEditCommandFPi
	virtual NewtonErr DoMainAction(void);							// ROM 0x00232fb4 DoMainAction__13TXEditCommandFv
	// Which values the undo container keeps (kTXImport...; 0: none).
	virtual void	GetUndoParams(unsigned char* types);			// ROM 0x00232dcc GetUndoParams__13TXEditCommandFPUc
	virtual NewtonErr SaveUndoContainer(unsigned char types);		// ROM 0x00232df0 SaveUndoContainer__13TXEditCommandFUc
	virtual void	UndoHilite(Boolean show);						// ROM 0x0023308c UndoHilite__13TXEditCommandFUc
	virtual NewtonErr SaveRedoContainer(void);						// ROM 0x00232f0c SaveRedoContainer__13TXEditCommandFv
	virtual void	RedoHilite(Boolean show);						// ROM 0x002330a8 RedoHilite__13TXEditCommandFUc
	virtual TXStream* CreateUndoRedoStream(const TXOffsetRange& range, unsigned char types);	// ROM 0x00233e9c CreateUndoRedoStream__13TXEditCommandFRC13TXOffsetRangeUc
	// The range's values exported onto a new stream (nil: it could not be).
	virtual TXStream* GetContainerStream(TXOffsetRange* range, unsigned char types);	// ROM 0x00232d50 GetContainerStream__13TXEditCommandFP13TXOffsetRangeUc
	// The objects a stream's container holds given back, and the stream.
	virtual void	FreeContainerStream(TXStream* stream);			// ROM 0x00233d88 FreeContainerStream__13TXEditCommandFP8TXStream

	NewtonErr		ITXEditCommand(Textension* text, int kind, unsigned char* failed);	// ROM 0x00233c9c ITXEditCommand__13TXEditCommandFP10TextensioniPUc
	// A restyle or a paragraph change: `values` (the command's to delete)
	// applied to `range` as UpdateRangeRuns/UpdateRangeRulers do.
	NewtonErr		ITXEditCommand(Textension* text, int kind, TXAttrValues* values, long how, const TXOffsetRange& range, unsigned char* failed);	// ROM 0x00233d4c ITXEditCommand__13TXEditCommandFP10TextensioniP12TXAttrValueslRC13TXOffsetRangePUc

	TXAttrValues*	fValues;		// +0x10
	long			fHow;			// +0x14
	TXOffsetRange	fRange;			// +0x18  what the edit changes
	TXOffsetRange	fUndoRange;		// +0x28  where the undo rulers go back
	TXOffsetRange	fRedoRange;		// +0x38  where the redo rulers go back
	TXOffsetPos		fEnd;			// +0x48  the end of what the edit made
	unsigned char	fUndoTypes;		// +0x50
	TXStream*		fUndoStream;	// +0x54
	Boolean			fNothingSaved;	// +0x58  an empty range: undoing is only taking away
	TXStream*		fRedoStream;	// +0x5c
	TXStream*		fUndoRulers;	// +0x60
	TXStream*		fRedoRulers;	// +0x64
};


// The ROM's object is 0x74 bytes.
class TXKeyCommand : public TXEditCommand
{
public:
					TXKeyCommand();									// ROM 0x0023348c __ct__12TXKeyCommandFv
	virtual NewtonErr DoIt(int* action);							// ROM 0x0023372c DoIt__12TXKeyCommandFPi
	virtual NewtonErr UndoIt(int* action);							// ROM 0x00233738 UndoIt__12TXKeyCommandFPi
	virtual NewtonErr SaveUndoContainer(unsigned char types);		// ROM 0x002336b0 SaveUndoContainer__12TXKeyCommandFUc
	virtual void	UndoHilite(Boolean show);						// ROM 0x00233744 UndoHilite__12TXKeyCommandFUc
	virtual void	RedoHilite(Boolean show);						// ROM 0x002337a8 RedoHilite__12TXKeyCommandFUc
	// Whether the key follows on from the ones typed so far.
	virtual Boolean	AcceptKey(unsigned int keyFlags);				// ROM 0x00233528 AcceptKey__12TXKeyCommandFUi
	// The key typed.  ==> 1: it took back everything that was typed.
	virtual long	AddKey(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags, void* data);	// ROM 0x002335b8 AddKey__12TXKeyCommandFPCUslT2UiPv

	// The command made before the first key (which is then given to NewKey).
	void			ITXKeyCommand(Textension* text, const UniChar* chars, long count, unsigned int keyFlags, unsigned char* failed);	// ROM 0x002334cc ITXKeyCommand__12TXKeyCommandFP10TextensionPCUslUiPUc
	// ==> 3: not this command's key; otherwise AddKey's answer.
	long			NewKey(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags, void* data);	// ROM 0x0023363c NewKey__12TXKeyCommandFPCUslT2UiPv

	Boolean			fFirstKey;		// +0x68
	Boolean			fUndone;		// +0x69
	TXOffsetPos		fDeleteStart;	// +0x6c  how far delete keys reached (-1: none were typed)
};


// The ROM's object is 0x84 bytes.
class TXMoveTextCommand : public TXEditCommand
{
public:
	virtual NewtonErr DoIt(int* action);							// ROM 0x00233920 DoIt__17TXMoveTextCommandFPi
	virtual NewtonErr UndoIt(int* action);							// ROM 0x00233b00 UndoIt__17TXMoveTextCommandFPi
	virtual NewtonErr RedoIt(int* action);							// ROM 0x00233b98 RedoIt__17TXMoveTextCommandFPi
	virtual void	GetUndoParams(unsigned char* types);			// ROM 0x00233900 GetUndoParams__17TXMoveTextCommandFPUc

	void			ITXMoveTextCommand(Textension* text, const TXOffsetRange& from, TXOffsetPos to, Boolean copy);	// ROM 0x002338a8 ITXMoveTextCommand__17TXMoveTextCommandFP10TextensionRC13TXOffsetRange8TXOffsetUc

	TXOffsetRange	fFrom;			// +0x68  the text (after the move: where it went)
	TXOffsetPos		fTo;			// +0x78  where it goes (after the move: where it came from)
	Boolean			fCopy;			// +0x80
};


// The ROM's object is 0x7c bytes.
class TXReplaceTextCommand : public TXEditCommand
{
public:
	virtual NewtonErr DoMainAction(void);							// ROM 0x00233c2c DoMainAction__20TXReplaceTextCommandFv
	virtual void	GetUndoParams(unsigned char* types);			// ROM 0x00233bd4 GetUndoParams__20TXReplaceTextCommandFPUc

	NewtonErr		ITXReplaceTextCommand(Textension* text, const TXOffsetRange& range, TXReplaceParams* params, unsigned char* failed);	// ROM 0x00233ba0 ITXReplaceTextCommand__20TXReplaceTextCommandFP10TextensionRC13TXOffsetRangeP15TXReplaceParamsPUc

	TXOffsetRange	fReplaceRange;	// +0x68
	TXReplaceParams* fParams;		// +0x78  the caller's
};

#endif	/* __TXCOMMAND_H */
