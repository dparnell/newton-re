/*
	File:		views/Keyboard.h

	Contains:	The keyboard: key codes translated to characters through
				the locale's 'kchr mapping (TranslateKey: the Macintosh KCHR
				format - a modifier-to-table map, 128-entry tables, dead
				keys), the keys held down (KeyIn, the hard and soft key
				maps, the modifiers, caps lock, dead-key state), and the
				key events posted to the views as commands (DoKeyEvent /
				HandleKeyEvent from a KeyboardEvent, HandleKeyEvents from a
				string of key codes, PostKeyString) - the view that gets
				them is the key view (GetPostingView), and TView::
				HandleKeyEvent runs its viewKeyDownScript & co.  The
				NewtonScript functions KeyIn, TranslateKey, IsKeyDown,
				GetTrueModifiers, IsCommandKeystroke, PostKeyString,
				HandleKeyEvents.

				Key codes are the Macintosh virtual key codes: 0x37 command,
				0x38 shift, 0x39 caps lock, 0x3a option, 0x3b control, 0x3c
				right shift, 0x3d right option; 0x60-0x7a the function keys
				(mapped to the private-use characters U+F721-U+F72F).  A key
				event's command parameter is (modifiers << 25) | (key code
				<< 16) | character.

				The key commands: a view's _keyCommands array of frames
				{char, modifiers, keyMessage, ...}, looked for up the key
				view chain (_nextKeyView) by FindKeyCommand, the message
				sent by SendKeyMessage.

				NOT YET RECONSTRUCTED: the key help (MatchKeyMessage,
				GatherKeyCommands), the caret's key view and its chain
				(SetKeyView, NextKeyView), the keyboard tool (TKeyboardTool)
				that sends the events, the on-screen keyboards' registry.

	Reconstructed from the MP2x00 US ROM (0x0030a948-0x0031084c, the
	TRootView parts in RootView.cpp); each function cites its origin.
*/

#ifndef __KEYBOARD_H
#define __KEYBOARD_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "AEvents.h"
#include "objects.h"

class TView;
class TResponder;

// the key codes
enum
{
	kCommandKey			= 0x37,
	kShiftKey			= 0x38,
	kCapsLockKey		= 0x39,
	kOptionKey			= 0x3a,
	kControlKey			= 0x3b,
	kRightShiftKey		= 0x3c,
	kRightOptionKey		= 0x3d
};

// the modifier bits (Modifiers: from the key map)
enum
{
	kCommandModifier	= 0x01,
	kShiftModifier		= 0x02,
	kCapsLockModifier	= 0x04,
	kOptionModifier		= 0x08,
	kControlModifier	= 0x10
};

// the parts of a key event's command parameter
inline ULong	KeyEventChar(ULong parameter)		{ return parameter & 0xffff; }
inline ULong	KeyEventKeyCode(ULong parameter)	{ return (parameter >> 16) & 0xff; }
inline ULong	KeyEventModifiers(ULong parameter)	{ return parameter >> 25; }
inline ULong	MakeKeyEventParameter(ULong modifiers, ULong keyCode, ULong ch)	{ return (modifiers << 25) | (keyCode << 16) | ch; }

const ULong kFunctionKeyCharFirst	= 0xf721;	// the private-use characters of the function keys
const ULong kFunctionKeyCharLast	= 0xf72f;
const ULong kEscapeChar				= 0x1b;

// the newt task's events: 'newt/'idle with a type at +8
class TNewtEvent : public TAEvent
{
public:
				TNewtEvent(ULong type);
	ULong		fType;			// +0x08  'keyb for a keyboard event
};

// what the keyboard tool sends (TKeyboardTool::SendKeyEvent 0x000fbc54): 0x2c bytes
class KeyboardEvent : public TNewtEvent
{
public:
				KeyboardEvent(ULong id, ULong keyCode);
	ULong		fEventId;		// +0x0c  aeKeyUp/aeKeyDown/aeKeyRepeat/aeKeyString (several characters)/aeKeyboardConnected
	ULong		fFromKeyboard;	// +0x10  1
	ULong		fKeyCode;		// +0x14  the key code (aeKeyboardConnected: whether connected)
	ULong		fLength;		// +0x18  the key events' count (aeKeyString)
	unsigned char	fKeys[16];	// +0x1c  the key events: a key code, | 0x80 for a key down
};

// the key maps: a bit per key code, the hardware keyboard's and the on-screen (soft) keyboards'
extern unsigned char	gSoftKeyMap[32];		// 0x0c1025b0
extern ULong			gSoftKeyDeadState;		// 0x0c1025d0  the offset of the pending dead key's record
extern Boolean			gSoftCapsLock;			// 0x0c1025d4
extern unsigned char	gHardKeyMap[32];		// 0x0c1025d8
extern ULong			gHardKeyDeadState;		// 0x0c1025f8
extern Boolean			gHardCapsLock;			// 0x0c1025fc
extern ULong			gTrueModifiers;			// 0x0c102600  the left/right shift and option keys actually down: bits 0 shift, 1 right shift, 2 option, 3 right option
extern Boolean			gKeyboardConnected;		// 0x0c101a24
extern Boolean			gKeyHelpOpen;			// 0x0c10261c  the key help (command held) is showing
extern Boolean			gInRepeatedKeyCommand;	// 0x0c102064  a key command is being sent for a repeat

Ref			GetKeyTransMapping(void);								// ROM 0x0030f450 GetKeyTransMapping__Fv
UniChar		TranslateKey(ULong keyCode, Boolean isDown, ULong modifiers, ULong* deadState);	// ROM 0x0030a948 TranslateKey__FUlUcT1PUl
ULong		Modifiers(Boolean hard);								// ROM 0x0030bcd0 Modifiers__FUc
Boolean		IsModifierKeyCode(ULong keyCode);						// ROM 0x0030bcfc IsModifierKeyCode__FUl
UniChar		KeyLabel(ULong keyCode, Boolean hard);					// ROM 0x0030bd18 KeyLabel__FUlUc
Boolean		KeyDown(ULong keyCode, Boolean hard);					// ROM 0x0030b3a4 KeyDown__FUlUc
UniChar		KeyIn(ULong keyCode, Boolean isDown, TView* keyboard);	// ROM 0x0030bd78 KeyIn__FUlUcP5TView ((TView*) -1: the hardware keyboard)
void		ClearHardKeymap(void);									// ROM 0x0030e114 ClearHardKeymap__Fv
Boolean		IsCommandKeyDown(void);									// ROM 0x0030ae0c IsCommandKeyDown__Fv
Boolean		IsCommandKeyCode(ULong keyCode);						// ROM 0x00310220 IsCommandKeyCode__FUl
Boolean		IsCommandKeystroke(UniChar ch, ULong parameter);		// ROM 0x00310278 IsCommandKeystroke__FUsUl
Boolean		KeyIsPrintable(UniChar ch, TView* view);				// ROM 0x00310330 KeyIsPrintable__FUsP5TView
Boolean		KeyCanBeHandled(UniChar ch);							// ROM 0x003103f8 KeyCanBeHandled__FUs

long		FindKeyCommandInArray(RefArg commands, UniChar ch, ULong modifiers, long* matched, Boolean* exact);	// ROM 0x0030f158 FindKeyCommandInArray__FRC6RefVarUsUlPlPUc - the index of the best match, -1 for none
Ref			FindKeyCommand(TView* view, UniChar ch, ULong modifiers);	// ROM 0x0030f2b0 FindKeyCommand__FP5TViewUsUl (modifiers: the parameter's bits)
Ref			SendKeyMessage(TView* view, RefArg message);			// ROM 0x0030f54c SendKeyMessage__FP5TViewRC6RefVar
// a command's letter and its modifier keys as the key help and a picker
// draw them (views/KeyHelpSlip.cpp)
struct StyleRecord;
long		GetModifiersWidth(RefArg command);					// ROM 0x00183a74 GetModifiersWidth__FRC6RefVar - the modifier icons' room, and 10 more
long		GetCommandCharWidth(RefArg command, StyleRecord* style);	// ROM 0x001839f8 GetCommandCharWidth__FRC6RefVarP11StyleRecord - the letter's width, in capitals
void		DrawModifierIcons(ULong modifiers, long x, long y);		// ROM 0x00183ad0 DrawModifierIcons__FUlN21 - right to left, ending at x, on the baseline y
Boolean		UserVisibleChar(UniChar c);								// ROM 0x0030f6c8 UserVisibleChar__FUs - one a menu could show
ULong		KeyCommandModifiers(RefArg command);					// ROM 0x0030f0f4 KeyCommandModifiers__FRC6RefVar - the command's modifier bits (& 0x3e000000)
UniChar		GetDisplayCmdChar(RefArg command);						// ROM 0x0030f700 GetDisplayCmdChar__FRC6RefVar - showChar, else char; 0 for one nobody could read
Boolean		AlreadyInCommandArray(RefArg commands, RefArg command);	// ROM 0x0030fa70 AlreadyInCommandArray__FRC6RefVarT1
Ref			GatherKeyCommands(TView* view);							// ROM 0x0030fbac GatherKeyCommands__FP5TView - every command in force at the view
Ref			MatchKeyMessage(TView* view, RefArg message, ULong what);	// ROM 0x0030f7e0 MatchKeyMessage__FP5TViewRC6RefVarUl (0 the first, 1 the first that shows, 2 all)
void		AddKeyCommands(RefArg context, RefArg commands);			// ROM 0x0030b2a4 AddKeyCommands__FRC6RefVarT1
void		BlockKeyCommand(TView* view, RefArg message);			// ROM 0x0030b3ec BlockKeyCommand__FP5TViewRC6RefVar
Boolean		StringsSame(RefArg a, RefArg b);						// ROM 0x0030fda0 StringsSame__FRC6RefVarT1
Ref			CategorizeKeyCommands(RefArg commands);					// ROM 0x0030fe38 CategorizeKeyCommands__FRC6RefVar - the groups a keyboard help slip shows

// the two of those a view answers as methods, so MakeViewMethods has
// them for a host that does not use the ROM's root template
Ref			FAddKeyCommands(RefArg rcvr, RefArg commands);			// ROM 0x0030b3d4 FAddKeyCommands
Ref			FBlockKeyCommand(RefArg rcvr, RefArg message);			// ROM 0x0030b58c FBlockKeyCommand

TView*		GetPostingView(Boolean commandKey);						// ROM 0x00310428 GetPostingView__FUc
void		DoKeyEvent(TResponder* receiver, ULong id, ULong keyCode);	// ROM 0x0030ac50 DoKeyEvent__FP10TResponderUlT2
void		HandleKeyEvent(KeyboardEvent* event);					// ROM 0x0030b120 HandleKeyEvent__FP13KeyboardEvent
ULong		GetKeyEventNo(RefArg keys, ULong index);				// ROM 0x0030ae20 GetKeyEventNo__FRC6RefVarUl
void		HandleKeyEvents(RefArg keys, ULong count);				// ROM 0x0030ae94 HandleKeyEvents__FRC6RefVarUl
void		PostKeyString(TView* view, RefArg str);					// ROM 0x00310500 PostKeyString__FP5TViewRC6RefVar

// A key command added to the view's own _keyCommands array - what a
// script does to give a view a key of its own (FindKeyCommand looks
// through them when a key reaches the view).
void		AddKeyCommand(RefArg view, RefArg command);			// ROM 0x0030b1c4 AddKeyCommand__FRC6RefVarT1

void		RegisterKeyboardNatives(void);

#endif	/* __KEYBOARD_H */
