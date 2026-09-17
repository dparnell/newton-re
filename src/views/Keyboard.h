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

	Reconstructed from the MP2100 D ROM (0x002e5610-0x002eb538, the
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

Ref			GetKeyTransMapping(void);								// ROM 0x002ea13c GetKeyTransMapping__Fv
UniChar		TranslateKey(ULong keyCode, Boolean isDown, ULong modifiers, ULong* deadState);	// ROM 0x002e5610 TranslateKey__FUlUcT1PUl
ULong		Modifiers(Boolean hard);								// ROM 0x002e69bc Modifiers__FUc
Boolean		IsModifierKeyCode(ULong keyCode);						// ROM 0x002e69e8 IsModifierKeyCode__FUl
UniChar		KeyLabel(ULong keyCode, Boolean hard);					// ROM 0x002e6a04 KeyLabel__FUlUc
Boolean		KeyDown(ULong keyCode, Boolean hard);					// ROM 0x002e6090 KeyDown__FUlUc
UniChar		KeyIn(ULong keyCode, Boolean isDown, TView* keyboard);	// ROM 0x002e6a64 KeyIn__FUlUcP5TView ((TView*) -1: the hardware keyboard)
void		ClearHardKeymap(void);									// ROM 0x002e8e00 ClearHardKeymap__Fv
Boolean		IsCommandKeyDown(void);									// ROM 0x002e5af8 IsCommandKeyDown__Fv
Boolean		IsCommandKeyCode(ULong keyCode);						// ROM 0x002eaf0c IsCommandKeyCode__FUl
Boolean		IsCommandKeystroke(UniChar ch, ULong parameter);		// ROM 0x002eaf64 IsCommandKeystroke__FUsUl
Boolean		KeyIsPrintable(UniChar ch, TView* view);				// ROM 0x002eb01c KeyIsPrintable__FUsP5TView
Boolean		KeyCanBeHandled(UniChar ch);							// ROM 0x002eb0e4 KeyCanBeHandled__FUs

Ref			FindKeyCommand(TView* view, UniChar ch, ULong modifiers);	// ROM 0x002e9f9c FindKeyCommand__FP5TViewUsUl (modifiers: the parameter's bits)
Ref			SendKeyMessage(TView* view, RefArg message);			// ROM 0x002ea238 SendKeyMessage__FP5TViewRC6RefVar

TView*		GetPostingView(Boolean commandKey);						// ROM 0x002eb114 GetPostingView__FUc
void		DoKeyEvent(TResponder* receiver, ULong id, ULong keyCode);	// ROM 0x002e5918 DoKeyEvent__FP10TResponderUlT2
void		HandleKeyEvent(KeyboardEvent* event);					// ROM 0x002e5e0c HandleKeyEvent__FP13KeyboardEvent
ULong		GetKeyEventNo(RefArg keys, ULong index);				// ROM 0x002e5b0c GetKeyEventNo__FRC6RefVarUl
void		HandleKeyEvents(RefArg keys, ULong count);				// ROM 0x002e5b80 HandleKeyEvents__FRC6RefVarUl
void		PostKeyString(TView* view, RefArg str);					// ROM 0x002eb1ec PostKeyString__FP5TViewRC6RefVar

void		RegisterKeyboardNatives(void);

#endif	/* __KEYBOARD_H */
