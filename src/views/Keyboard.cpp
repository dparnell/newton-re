/*
	File:		views/Keyboard.cpp

	Contains:	The keyboard: key translation, the key maps, the key events
				posted as commands, the key commands.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Keyboard.h"
#include "PickView.h"		// FDismissPopup
#include "View.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "ViewFlags.h"
#include "Locale.h"
#include "Frames.h"
#include "RichString.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "NativeFunctions.h"
#include "ROMConstants.h"
#include "Unicode.h"
#include "NewtonExceptions.h"
#include <string.h>

unsigned char	gSoftKeyMap[32];				// ROM 0x0c1025b0
ULong			gSoftKeyDeadState = 0;			// ROM 0x0c1025d0
Boolean			gSoftCapsLock = false;			// ROM 0x0c1025d4
unsigned char	gHardKeyMap[32];				// ROM 0x0c1025d8
ULong			gHardKeyDeadState = 0;			// ROM 0x0c1025f8
Boolean			gHardCapsLock = false;			// ROM 0x0c1025fc
ULong			gTrueModifiers = 0;				// ROM 0x0c102600
Boolean			gKeyboardConnected = false;		// ROM 0x0c101a24
Boolean			gKeyHelpOpen = false;			// ROM 0x0c10261c
Boolean			gInRepeatedKeyCommand = false;	// ROM 0x0c104f70 gInRepeatedKeyCommand (the Newt globals + 0x18)


/*------------------------------------------------------------------------------
	T h e   e v e n t s
------------------------------------------------------------------------------*/

TNewtEvent::TNewtEvent(ULong type)
{
	fAEventClass = 'newt';
	fAEventID = 'idle';
	fType = type;
}


// as TKeyboardTool::SendKeyEvent (ROM 0x000fbc54) fills one in
KeyboardEvent::KeyboardEvent(ULong id, ULong keyCode)
	: TNewtEvent('keyb')
{
	fEventId = id;
	fFromKeyboard = 1;
	fKeyCode = keyCode;
	fLength = 0;
	memset(fKeys, 0, sizeof(fKeys));
}


/*------------------------------------------------------------------------------
	T r a n s l a t i o n
------------------------------------------------------------------------------*/

// ROM 0x0030f450 GetKeyTransMapping__Fv
// The 'kchr binary: vars.international.keyboard.mapping (the locale's
// keycodeMapping, put there when the locale is set).
Ref
GetKeyTransMapping(void)
{
	RefVar intl(IntlResources());
	RefVar keyboard(GetProtoVariable(intl, RSSYMkeyboard, nil));
	return GetFrameSlotRef(keyboard, RSSYMmapping);
}


// the private-use characters the function keys' 0x10 stands for
static UniChar
FunctionKeyChar(ULong keyCode)
{
	switch (keyCode)
	{
	case 0x60: return 0xf725;	// F5
	case 0x61: return 0xf726;	// F6
	case 0x62: return 0xf727;	// F7
	case 0x63: return 0xf723;	// F3
	case 0x64: return 0xf728;	// F8
	case 0x65: return 0xf729;	// F9
	case 0x67: return 0xf72b;	// F11
	case 0x69: return 0xf72d;	// F13
	case 0x6b: return 0xf72e;	// F14
	case 0x6d: return 0xf72a;	// F10
	case 0x6f: return 0xf72c;	// F12
	case 0x71: return 0xf72f;	// F15
	case 0x76: return 0xf724;	// F4
	case 0x78: return 0xf722;	// F2
	case 0x7a: return 0xf721;	// F1
	default:   return 0x10;
	}
}


// ROM 0x0030a948 TranslateKey__FUlUcT1PUl
// The character a key code gives under the modifiers, from the mapping
// (the KCHR: +2 the modifiers' table index, +0x102 the table count,
// +0x104 the 128-byte tables, then the dead keys: a count and records
// {table, key code, completions count, (completion, result) pairs, (0,
// the accent alone)}).  A pending dead key (*deadState: its record's
// offset) completes the character (the accent itself when its key comes
// again); a key that gives nothing may be a dead key, whose accent is
// answered and whose record is remembered (on a key down).  The Mac
// Roman character is converted to Unicode; the function keys' 0x10 to
// their private-use characters.
UniChar
TranslateKey(ULong keyCode, Boolean isDown, ULong modifiers, ULong* deadState)
{
	RefVar mapping(GetKeyTransMapping());
	LockRefArg(mapping);			// (the ROM's TObjectPtr: the heap compacts)
	const unsigned char* base = (const unsigned char*) BinaryData(mapping);
	long table = (signed char) base[2 + modifiers];
	unsigned char ch = base[0x104 + table * 0x80 + keyCode];
	long tableCount = (short) ((base[0x102] << 8) | base[0x103]);
	const unsigned char* deadArea = base + tableCount * 0x80;
	if (*deadState != 0)
	{
		const unsigned char* record = base + *deadState;
		const unsigned char* p = record + 4;
		Boolean matched = false;
		while (*p != 0)
		{
			if (ch == *p)
			{
				matched = true;
				break;
			}
			p += 2;
		}
		if (matched || ch != 0 || record[1] == keyCode)
		{
			if (matched || ch == 0)
				ch = p[1];
			if (isDown)
				*deadState = 0;
		}
	}
	if (ch == 0)
	{
		long count = (short) ((deadArea[0x104] << 8) | deadArea[0x105]);
		const unsigned char* record = deadArea + 0x106;
		for (; count > 0; count--)
		{
			if (record[1] == keyCode && (signed char) record[0] == table)
			{
				if (isDown)
					*deadState = record - base;
				ch = record[5];
				break;
			}
			record += ((short) ((record[2] << 8) | record[3])) * 2 + 6;
		}
	}
	UnlockRefArg(mapping);
	UniChar unicode;
	ConvertToUnicode(&ch, &unicode, kMacRomanEncoding, 1);
	if (unicode == 0x10 && keyCode > 0x5f && keyCode < 0x7c)
		unicode = FunctionKeyChar(keyCode);
	return unicode;
}


// ROM 0x0030bcd0 Modifiers__FUc
// The modifier keys down, from the hard or soft key map: bit 0 command
// (key 0x37), 1 shift, 2 caps lock, 3 option, 4 control.
ULong
Modifiers(Boolean hard)
{
	const unsigned char* map = hard ? gHardKeyMap : gSoftKeyMap;
	return ((map[7] & 0x0f) << 1) | (map[6] >> 7);
}


// ROM 0x0030bcfc IsModifierKeyCode__FUl
Boolean
IsModifierKeyCode(ULong keyCode)
{
	return keyCode > 0x36 && keyCode < 0x3c;
}


// ROM 0x0030bd18 KeyLabel__FUlUc
// The character the key would give now (the dead state is not changed).
UniChar
KeyLabel(ULong keyCode, Boolean hard)
{
	ULong deadState = hard ? gHardKeyDeadState : gSoftKeyDeadState;
	return TranslateKey(keyCode, true, Modifiers(hard), &deadState);
}


// ROM 0x0030b3a4 KeyDown__FUlUc
Boolean
KeyDown(ULong keyCode, Boolean hard)
{
	const unsigned char* map = hard ? gHardKeyMap : gSoftKeyMap;
	return (map[keyCode >> 3] & (1 << (keyCode & 7))) != 0;
}


// ROM 0x0030e114 ClearHardKeymap__Fv
void
ClearHardKeymap(void)
{
	memset(gHardKeyMap, 0, sizeof(gHardKeyMap));
	gHardKeyDeadState = 0;
	gHardCapsLock = false;
	gTrueModifiers = 0;
}


// ROM 0x0030bd78 KeyIn__FUlUcP5TView
// A key gone down or up on the hardware keyboard (keyboard == -1) or an
// on-screen one (its view): the right shift and option keys stand for
// the left ones (the hardware's gTrueModifiers keeps which are really
// down, so one going up while the other is held changes nothing); caps
// lock toggles on each press (its release is key 0); the on-screen
// keyboards are told of the modifiers (TRootView::HandleKeyIn); the key
// map's bit is set or cleared; ==> the character (0 while a dead key is
// pending).
UniChar
KeyIn(ULong keyCode, Boolean isDown, TView* keyboard)
{
	Boolean hard = keyboard == (TView*) -1;
	Boolean mapIt = hard;
	if (hard)
	{
		switch (keyCode)
		{
		case kShiftKey:
			gTrueModifiers = isDown ? (gTrueModifiers | 1) : (gTrueModifiers & ~1);
			if (!isDown && (gTrueModifiers & 2))
				return 0;
			break;
		case kOptionKey:
			gTrueModifiers = isDown ? (gTrueModifiers | 4) : (gTrueModifiers & ~4);
			if (!isDown && (gTrueModifiers & 8))
				return 0;
			break;
		case kRightShiftKey:
			gTrueModifiers = isDown ? (gTrueModifiers | 2) : (gTrueModifiers & ~2);
			if (!isDown && (gTrueModifiers & 1))
				return 0;
			keyCode = kShiftKey;
			break;
		case kRightOptionKey:
			gTrueModifiers = isDown ? (gTrueModifiers | 8) : (gTrueModifiers & ~8);
			if (!isDown && (gTrueModifiers & 4))
				return 0;
			keyCode = kOptionKey;
			break;
		default:
			mapIt = false;
			break;
		}
	}
	else if (keyCode == kRightShiftKey)
		keyCode = kShiftKey;
	else if (keyCode == kRightOptionKey)
		keyCode = kOptionKey;
	else
		mapIt = false;
	if (!mapIt)
	{
		if (keyCode == kCapsLockKey)
		{
			Boolean* capsLock = hard ? &gHardCapsLock : &gSoftCapsLock;
			if (!isDown)
				keyCode = 0;
			else
			{
				isDown = !*capsLock;
				*capsLock = isDown;
			}
		}
		if (!hard)
			gRootView->HandleKeyIn(keyCode, isDown, keyboard);
	}
	unsigned char* map = hard ? gHardKeyMap : gSoftKeyMap;
	unsigned char bit = 1 << (keyCode & 7);
	if (isDown)
		map[keyCode >> 3] |= bit;
	else
		map[keyCode >> 3] &= ~bit;
	UniChar ch;
	if (hard)
	{
		ch = TranslateKey(keyCode, isDown, Modifiers(true), &gHardKeyDeadState);
		if (gHardKeyDeadState != 0)
			return 0;
	}
	else
		ch = TranslateKey(keyCode, isDown, Modifiers(false), &gSoftKeyDeadState);
	if (gSoftKeyDeadState != 0)
		return 0;
	return ch;
}


// ROM 0x0030ae0c IsCommandKeyDown__Fv
Boolean
IsCommandKeyDown(void)
{
	return (gHardKeyMap[6] & 0x80) != 0;
}


// ROM 0x00310220 IsCommandKeyCode__FUl
// Escape (0x35) and the function keys are command keys of their own.
Boolean
IsCommandKeyCode(ULong keyCode)
{
	if (keyCode == 0x35)
		return true;
	if (keyCode < 0x60 || keyCode > 0x7a)
		return false;
	if (keyCode <= 0x65)
		return true;
	return keyCode == 0x67 || keyCode == 0x69 || keyCode == 0x6b || keyCode == 0x6d
		|| keyCode == 0x6f || keyCode == 0x71 || keyCode == 0x76 || keyCode == 0x7a;
}


// ROM 0x00310278 IsCommandKeystroke__FUsUl
// The command key held, a function key or escape.
Boolean
IsCommandKeystroke(UniChar ch, ULong parameter)
{
	if (parameter & (kCommandModifier << 25))
		return true;
	if (ch >= kFunctionKeyCharFirst && ch <= kFunctionKeyCharLast)
		return true;
	return ch == kEscapeChar;
}


// ROM 0x00310330 KeyIsPrintable__FUsP5TView
// Whether the character goes into the view's text: not the function
// keys or escape; return and tab only in a paragraph; else any from
// space up.
Boolean
KeyIsPrintable(UniChar ch, TView* view)
{
	if (view == nil)
		return false;
	if (ch >= kFunctionKeyCharFirst && ch <= kFunctionKeyCharLast)
		return false;
	if (ch == kEscapeChar)
		return false;
	if (ch == 0x0d || ch == 0x09)
		return view->DerivedFrom(clParagraphView);
	return ch >= 0x20;
}


// ROM 0x003103f8 KeyCanBeHandled__FUs
Boolean
KeyCanBeHandled(UniChar ch)
{
	return ch >= 0x1c || ch == 9 || ch == 8 || ch == 0x0d || ch == 3;
}


/*------------------------------------------------------------------------------
	T h e   k e y   c o m m a n d s
------------------------------------------------------------------------------*/

// ROM 0x0030f0b4 CountOnes__FUl
static ULong
CountOnes(ULong n)
{
	ULong x = (n >> 1) & 0xdb6db6db;
	x = (n - x) - ((x >> 1) & 0xdb6db6db);
	x = (x + (x >> 3)) & 0xc71c71c7;
	x = x + (x >> 6);
	x = x + (x >> 12);
	return (x + (x >> 24)) & 0x3f;
}


// ROM 0x0030f0f4 KeyCommandModifiers__FRC6RefVar
// The modifiers a key command frame asks for, in the parameter's bits.
ULong
KeyCommandModifiers(RefArg command)
{
	Ref modifiers = GetFrameSlotRef(command, RSSYMmodifiers);
	if (ISNIL(modifiers))
		return 0;
	return RINT(modifiers) & 0x3e000000;
}


// ROM 0x0030f158 FindKeyCommandInArray__FRC6RefVarUsUlPlPUc
// The key command in the array for the character and modifiers: an
// exact match of the modifiers (*exact) wins at once, else the one
// asking for the most of the modifiers held (*matched: how many);
// ==> its index, -1 for none.
long
FindKeyCommandInArray(RefArg commands, UniChar ch, ULong modifiers, long* matched, Boolean* exact)
{
	long found = -1;
	long best = -1;
	if (exact != nil)
		*exact = false;
	long count = Length(commands);
	for (long i = 0; i < count; i++)
	{
		RefVar command(GetArraySlotRef(commands, i));
		if (ISNIL(command))
			continue;
		if ((RCHAR(GetFrameSlotRef(command, RSSYMchar)) & 0xffff) != ch)
			continue;
		ULong wanted = KeyCommandModifiers(command);
		if (wanted == modifiers)
		{
			best = CountOnes(modifiers);
			found = i;
			if (exact != nil)
				*exact = true;
			break;
		}
		if ((wanted & modifiers) == wanted)
		{
			long n = CountOnes(wanted & modifiers);
			if (n > best)
			{
				best = n;
				found = i;
			}
		}
	}
	if (matched != nil)
		*matched = best;
	return found;
}


// ROM 0x0030f2b0 FindKeyCommand__FP5TViewUsUl
// The key command for the keystroke, looked for in the _keyCommands
// arrays up the key view chain (the _nextKeyView proto variable when
// there is one - 'none ends the search - else the parent) to the root:
// an exact match ends it, else the best partial match on the way.
Ref
FindKeyCommand(TView* view, UniChar ch, ULong modifiers)
{
	RefVar found;
	long best = -1;
	Boolean done = false;
	do
	{
		RefVar commands(view->GetProto(RSSYM_keycommands));
		if (IsArray(commands))
		{
			long matched;
			Boolean exact;
			long slot = FindKeyCommandInArray(commands, ch, modifiers, &matched, &exact);
			if (exact)
			{
				found = GetArraySlotRef(commands, slot);
				best = matched;
				done = true;
			}
			else if (slot != -1 && matched > best)
			{
				found = GetArraySlotRef(commands, slot);
				best = matched;
			}
		}
		if (view == gRootView)
			done = true;
		else if (!done)
		{
			RefVar next(view->GetProto(RSSYM_nextkeyview));
			if (ISNIL(next))
				view = view->fParent;
			else if (EQRef(next, RSSYMnone))
				done = true;
			else
				view = GetView(next);
		}
	} while (!done);
	return found;
}


// ROM 0x0030f54c SendKeyMessage__FP5TViewRC6RefVar
// The message run (with the view's context as its argument) by the
// first view up the key view chain that has it; ==> its result.
Ref
SendKeyMessage(TView* view, RefArg message)
{
	RefVar result;
	RefVar args(AllocateArray(RSSYMarray, 1));
	SetArraySlotRef(args, 0, view->fContext);
	Boolean done = false;
	do
	{
		if (NOTNIL(view->GetProto(message)))
		{
			result = view->RunScript(message, args, false, nil);
			break;
		}
		if (view == gRootView)
			break;
		RefVar next(view->GetProto(RSSYM_nextkeyview));
		if (ISNIL(next))
			view = view->fParent;
		else if (EQRef(next, RSSYMnone))
			done = true;
		else
			view = GetView(next);
	} while (!done);
	return result;
}


/*------------------------------------------------------------------------------
	T h e   e v e n t s   t o   t h e   v i e w s
------------------------------------------------------------------------------*/

// ROM 0x00310428 GetPostingView__FUc
// The view key events go to: with a hardware keyboard, the root's
// popup while it is visible and does not let keys through
// (allowKeysThrough); else the key view ('viewFrontKey), or for a
// command keystroke the command-key view ('viewFrontCommandKey).
TView*
GetPostingView(Boolean commandKey)
{
	if (gRootView->CommandKeyboardConnected())
	{
		TView* popup = gRootView->fPopup;
		if (popup != nil && (popup->fFlags & vVisible) && ISNIL(popup->GetProto(RSSYMallowkeysthrough)))
			return popup;
	}
	return GetView(RefVar(NILREF), commandKey ? RSSYMviewfrontcommandkey : RSSYMviewfrontkey);
}


// ROM 0x0030ac50 DoKeyEvent__FP10TResponderUlT2
// A key event from the keyboard tool to the receiver: the command key
// held down (a repeat) opens the key help (_keyHelpOpenScript up the key
// view chain, the popup dismissed first); a key up/down/repeat goes
// through KeyIn (the character), tells the root the keyboard is
// connected when it was not (aeKeyboardConnected), and is dispatched as
// its command with (modifiers, key code, character) as the parameter;
// aeKeyboardConnected goes to the root with the event's key code.  The
// root view is then updated (the ROM inlines TRootView::Update with the
// caret's care).
void
DoKeyEvent(TResponder* receiver, ULong id, ULong keyCode)
{
	if (id == aeKeyRepeat && keyCode == kCommandKey)
	{
		FDismissPopup(RefVar());
		TView* view = (TView*) receiver;
		if (view == nil || GetView(view->fContext) == nil)
			view = gRootView;
		SendKeyMessage(view, RSSYM_keyhelpopenscript);
		gKeyHelpOpen = true;
		return;
	}
	if (gKeyHelpOpen)
	{
		SendKeyMessage(receiver != nil ? (TView*) receiver : gRootView, RSSYM_keyhelpclosescript);
		gKeyHelpOpen = false;
	}
	RefVar cmd;
	if (id == aeKeyUp || id == aeKeyDown || id == aeKeyRepeat)
	{
		UniChar ch = KeyIn(keyCode, id == aeKeyDown || id == aeKeyRepeat, (TView*) -1);
		if (!gRootView->KeyboardConnected())
		{
			cmd = MakeCommand(aeKeyboardConnected, gRootView, 1);
			gApplication->DispatchCommand(cmd);
		}
		if (receiver == nil)
			return;
		cmd = MakeCommand(id, receiver, (Long) MakeKeyEventParameter(Modifiers(true), keyCode, ch));
		gApplication->DispatchCommand(cmd);
	}
	else if (id == aeKeyboardConnected)
	{
		cmd = MakeCommand(aeKeyboardConnected, gRootView, keyCode);
		gApplication->DispatchCommand(cmd);
	}
	else
		return;
	if (gRootView->NeedsUpdate())
		gRootView->Update(nil);		// NOT YET RECONSTRUCTED: the caret hidden and redrawn, UpdateDefaultButtonAndCaretSlip
}


// ROM 0x0030b120 HandleKeyEvent__FP13KeyboardEvent
// A keyboard event from the keyboard tool: a key string (several key
// events at once) through HandleKeyEvents; else DoKeyEvent to the
// posting view (the command-key view for the command key, a command
// key held or a command key code).  NOT YET RECONSTRUCTED: gTickleTime
// (the user is active).
void
HandleKeyEvent(KeyboardEvent* event)
{
	ULong id = event->fEventId;
	if (id == aeKeyString)
	{
		HandleKeyEvents(RefVar(AddressToRef(event->fKeys)), event->fLength);
		return;
	}
	ULong keyCode = event->fKeyCode;
	Boolean commandKey = keyCode == kCommandKey || IsCommandKeyDown() || IsCommandKeyCode(keyCode);
	DoKeyEvent(GetPostingView(commandKey), id, keyCode);
}


// ROM 0x0030ae20 GetKeyEventNo__FRC6RefVarUl
// The index'th key event of a set: a byte of the event's own data (an
// address ref), an int of an array, or a byte of a string's data - the
// key code with 0x80 for a key down.
ULong
GetKeyEventNo(RefArg keys, ULong index)
{
	if (ISINT(keys))
		return ((const unsigned char*) RefToAddress(keys))[index];
	if (IsArray(keys))
		return RINT(GetArraySlotRef(keys, index)) & 0xff;
	return ((const unsigned char*) GetCString(keys))[index];
}


// ROM 0x0030ae94 HandleKeyEvents__FRC6RefVarUl
// A run of key events: a key view that takes its keys one by one
// (TextFlags 0x400) gets each as DoKeyEvent; else the characters of the
// keys down are gathered (KeyIn) into a string posted with
// PostKeyString - up to a command keystroke, from which on every event
// goes to the command-key view as DoKeyEvent.
void
HandleKeyEvents(RefArg keys, ULong count)
{
	TView* view = GetPostingView(false);
	if (view->TextFlags() & 0x400)
	{
		for (ULong i = 0; i < count; i++)
		{
			ULong b = GetKeyEventNo(keys, i);
			ULong keyCode = b & 0x7f;
			ULong id = (b & 0x80) ? aeKeyDown : aeKeyUp;
			Boolean commandKey = (id == aeKeyDown && keyCode == kCommandKey) || IsCommandKeyDown() || IsCommandKeyCode(keyCode);
			DoKeyEvent(GetPostingView(commandKey), id, keyCode);
		}
		return;
	}
	RefVar str(AllocateBinary(RSSYMstring, 2 + count * 2));
	long n = 0;
	Boolean commandMode = false;
	for (ULong i = 0; i < count; i++)
	{
		ULong b = GetKeyEventNo(keys, i);
		ULong keyCode = b & 0x7f;
		ULong id = (b & 0x80) ? aeKeyDown : aeKeyUp;
		Boolean asCommand;
		if (id == aeKeyDown && !commandMode)
		{
			asCommand = keyCode == kCommandKey || IsCommandKeyDown() || IsCommandKeyCode(keyCode);
			if (asCommand)
				commandMode = true;
		}
		else
			asCommand = commandMode;
		if (asCommand)
		{
			if (n != 0)
			{
				UniChar* text = (UniChar*) BinaryData(str);
				text[n] = 0;
				SetLength(str, 2 + n * 2);
				PostKeyString(GetPostingView(false), str);
				if (count - 1 > i)
					SetLength(str, 2 + count * 2);
				n = 0;
			}
			DoKeyEvent(GetPostingView(true), id, keyCode);
		}
		else
		{
			UniChar ch = KeyIn(keyCode, id == aeKeyDown, (TView*) -1);
			if (ch != 0 && id == aeKeyDown)
			{
				UniChar* text = (UniChar*) BinaryData(str);
				text[n++] = ch;
			}
		}
	}
	UniChar* text = (UniChar*) BinaryData(str);
	text[n] = 0;
	if (n != 0)
	{
		SetLength(str, 2 + n * 2);
		PostKeyString(GetPostingView(false), str);
	}
}


// a key down and key up for the character, to the posting view
static void
PostKeyDownUp(UniChar ch, TView* downView)
{
	RefVar cmd;
	if (downView != nil)
	{
		cmd = MakeCommand(aeKeyDown, downView, ch);
		gApplication->DispatchCommand(cmd);
	}
	TView* upView = GetPostingView(false);
	if (upView != nil)
	{
		cmd = MakeCommand(aeKeyUp, upView, ch);
		gApplication->DispatchCommand(cmd);
	}
}


// ROM 0x00310500 PostKeyString__FP5TViewRC6RefVar
// A string typed at the view: when every character is printable for
// it, one aeKeyString command with the string; a view that wants its
// keys one by one (TextFlags 0x400) gets a key down and key up per
// character instead; otherwise the printable runs go as aeKeyString to
// the posting view and each other character as a key down and up.
void
PostKeyString(TView* view, RefArg str)
{
	if (view == nil)
		return;
	LockRefArg(str);				// (the ROM's TObjectPtr)
	const UniChar* text = (const UniChar*) BinaryData(str);
	long length = Ustrlen(text);
	Boolean wantsKeys = (view->TextFlags() & 0x400) != 0;
	RefVar cmd;
	if (!wantsKeys)
	{
		Boolean allPrintable = true;
		for (long i = 0; i < length; i++)
			if (!KeyIsPrintable(text[i], view))
			{
				allPrintable = false;
				break;
			}
		if (allPrintable)
		{
			UnlockRefArg(str);
			cmd = MakeCommand(aeKeyString, view, kNoParameter);
			CommandSetFrameParameter(cmd, str);
			gApplication->DispatchCommand(cmd);
			return;
		}
		// the printable runs as strings, each other character (and the
		// terminator after a trailing run: the ROM reads text[length]) as
		// a key down and up
		long start = 0;
		for (long i = 0; i < length; )
		{
			TView* posting = GetPostingView(false);
			while (i < length && KeyIsPrintable(text[i], posting))
				i++;
			if (i > start)
			{
				cmd = MakeCommand(aeKeyString, posting, kNoParameter);
				CommandSetFrameParameter(cmd, RefVar(Substring(str, start, i - start)));
				gApplication->DispatchCommand(cmd);
				posting = GetPostingView(false);
			}
			PostKeyDownUp(text[i], posting);
			i++;
			start = i;
		}
		UnlockRefArg(str);
		return;
	}
	for (long i = 0; i < length; i++)
		PostKeyDownUp(text[i], GetPostingView(false));
	UnlockRefArg(str);
}


/*------------------------------------------------------------------------------
	T h e   n a t i v e s
------------------------------------------------------------------------------*/

// ROM 0x0030bf88 FKeyIn
// KeyIn(keyCode, isDown): a key on a soft keyboard (no view); ==> the character's code
static Ref
FKeyIn(RefArg /*rcvr*/, RefArg keyCode, RefArg isDown)
{
	return MAKEINT(KeyIn(RINT(keyCode), NOTNIL(isDown), nil));
}


// ROM 0x0030bfdc FTranslateKey
// TranslateKey(keyCode, parameter, deadState): the character's code, or
// the new dead state when a dead key is pending
static Ref
FTranslateKey(RefArg /*rcvr*/, RefArg keyCode, RefArg parameter, RefArg deadState)
{
	ULong dead = RINT(deadState);
	UniChar ch = TranslateKey(RINT(keyCode), true, (RINT(parameter) & 0x3e000000) >> 25, &dead);
	return dead == 0 ? MAKEINT(ch) : MAKEINT(dead);
}


// ROM 0x0030bb20 FIsKeyDown
static Ref
FIsKeyDown(RefArg /*rcvr*/, RefArg keyCode, RefArg hard)
{
	return MAKEBOOLEAN(KeyDown(RINT(keyCode), NOTNIL(hard)));
}


// ROM 0x0030bd64 FGetTrueModifiers
static Ref
FGetTrueModifiers(RefArg /*rcvr*/)
{
	return MAKEINT(gTrueModifiers);
}


// ROM 0x003102b0 FIsCommandKeystroke
static Ref
FIsCommandKeystroke(RefArg /*rcvr*/, RefArg ch, RefArg parameter)
{
	return MAKEBOOLEAN(IsCommandKeystroke((UniChar) RCHAR(ch), RINT(parameter)));
}


// ROM 0x0030ac28 FPostKeyString__FRC6RefVarN21
// PostKeyString(viewName, string) from a context
static Ref
FPostKeyString(RefArg rcvr, RefArg name, RefArg str)
{
	TView* view = GetView(rcvr, name);
	if (view != nil)
		PostKeyString(view, str);
	return NILREF;
}


// ROM 0x0030b0f0 FHandleKeyEvents
static Ref
FHandleKeyEvents(RefArg /*rcvr*/, RefArg keys)
{
	HandleKeyEvents(keys, Length(keys));
	return NILREF;
}


// ROM 0x0030f698 FSendKeyMessage
static Ref
FSendKeyMessage(RefArg /*rcvr*/, RefArg context, RefArg message)
{
	TView* view = GetView(context);
	if (view == nil)
		return NILREF;
	return SendKeyMessage(view, message);
}


// ROM 0x0030ee6c FClearHardKeymap
static Ref
FClearHardKeymap(RefArg /*rcvr*/)
{
	ClearHardKeymap();
	return NILREF;
}


void	ArrayInsert(RefArg array, RefArg element, long index);		// frames/Munger.cpp


// ROM 0x0030b1c4 AddKeyCommand__FRC6RefVarT1
// The command put at the front of the view's `_keyCommands`.  The view
// gets an array of its own the first time, and a clone of the one it
// inherits when that one is read-only - a ROM array is, and the views
// that share it must not be written through.
void
AddKeyCommand(RefArg view, RefArg command)
{
	RefVar commands(GetProtoVariable(view, RSSYM_keycommands, nil));
	if (ISNIL(commands))
	{
		commands = AllocateArray(RefVar(RSSYMarray), 1);
		SetArraySlotRef(commands, 0, command);
		SetFrameSlot(view, RSSYM_keycommands, commands);
		return;
	}
	if ((ObjectFlags(commands) & kObjReadOnly) != 0)
	{
		commands = Clone(commands);
		SetFrameSlot(view, RSSYM_keycommands, commands);
	}
	ArrayInsert(commands, command, 0);
}


// ROM 0x0030b28c FAddKeyCommand
// :AddKeyCommand(command): the command added to the view it is sent to.
// ==> nil.
static Ref
FAddKeyCommand(RefArg rcvr, RefArg command)
{
	AddKeyCommand(rcvr, command);
	return NILREF;
}

/*------------------------------------------------------------------------------
	T h e   k e y   c o m m a n d s   a   s c r i p t   s e e s

	A view's `_keyCommands` is an array of command frames, each with a
	`char`, its `modifiers` and a `keyMessage` to send.  The functions
	below are what a script uses to add to that array, to gather what is
	in force at the caret, and to ask which command a key or a message
	would reach; they all walk the key-view chain the same way
	FindKeyCommand does - `_nextKeyView` when the view names one, the
	parent otherwise, stopping at the root or at `'none`.
------------------------------------------------------------------------------*/

// Munger.cpp
void	ArrayInsert(RefArg array, RefArg element, long index);


// ROM 0x0030f6c8 UserVisibleChar__FUs
// Whether a character is one a menu could show.  The special key
// characters 0xf721 to 0xf72f (the arrows, the function keys and their
// like) are not, nor is anything below a space, nor delete.  The
// characters just below that range, 0xf700 to 0xf720, are.
Boolean
UserVisibleChar(UniChar c)
{
	if (c >= 0xf721 && c <= 0xf72f)
		return false;
	if (c < 0x20)
		return false;
	return c != 0x7f;
}


// ROM 0x0030f700 GetDisplayCmdChar__FRC6RefVar
// The character a key command shows itself by: its `showChar` when it
// has one, else its `char`; 0 when that is not a character anyone could
// read.
UniChar
GetDisplayCmdChar(RefArg command)
{
	RefVar shown(GetFrameSlotRef(command, RSSYMshowchar));
	if (ISNIL(shown))
		shown = GetFrameSlotRef(command, RSSYMchar);
	UniChar c = RCHAR(shown);
	return UserVisibleChar(c) ? c : 0;
}


// ROM 0x0030fa70 AlreadyInCommandArray__FRC6RefVarT1
// Whether the array already holds a command for the same key - the same
// character with the same modifiers.  The one nearest the caret wins, so
// the walk outwards keeps the first it saw.
Boolean
AlreadyInCommandArray(RefArg commands, RefArg command)
{
	long count = Length(commands);
	UniChar wanted = RCHAR(RefVar(GetFrameSlotRef(command, RSSYMchar)));
	ULong modifiers = KeyCommandModifiers(command);
	for (long i = 0; i < count; i++)
	{
		RefVar one(GetArraySlotRef(commands, i));
		ULong theirs = KeyCommandModifiers(one);
		UniChar c = RCHAR(RefVar(GetFrameSlotRef(one, RSSYMchar)));
		if (modifiers == theirs && wanted == c)
			return true;
	}
	return false;
}


// the next view up the key-view chain; nil at the end of it
static TView*
NextKeyCommandView(TView* view)
{
	if (view == gRootView)
		return nil;
	RefVar next(view->GetProto(RSSYM_nextkeyview));
	if (ISNIL(next))
		return view->fParent;
	if (EQRef(next, RSSYMnone))
		return nil;
	return GetView(next);
}


// ROM 0x0030fbac GatherKeyCommands__FP5TView
// Every key command in force at the view, from it outwards: the array
// its `_keyCommands` holds, then its next key view's, and so on to the
// root.  A key that is already spoken for nearer the caret is not added
// again, so what comes back is what would actually happen.
Ref
GatherKeyCommands(TView* view)
{
	RefVar gathered(AllocateArray(RSSYMarray, 0));
	while (view != nil)
	{
		RefVar commands(view->GetProto(RSSYM_keycommands));
		if (IsArray(commands))
		{
			long count = Length(commands);
			for (long i = 0; i < count; i++)
			{
				RefVar one(GetArraySlotRef(commands, i));
				if (!AlreadyInCommandArray(gathered, one))
					AddArraySlot(gathered, one);
			}
		}
		view = NextKeyCommandView(view);
	}
	return gathered;
}


// ROM 0x0030f7e0 MatchKeyMessage__FP5TViewRC6RefVarUl
// The key commands along the chain whose `keyMessage` is the one given.
// `what` says which: 0 the first there is, 1 the first that could be
// shown in a menu (GetDisplayCmdChar answers something), 2 all of them
// as an array.
Ref
MatchKeyMessage(TView* view, RefArg message, ULong what)
{
	RefVar found;
	if (what == 2)
		found = AllocateArray(RSSYMarray, 0);
	Boolean done = false;
	while (view != nil && !done)
	{
		RefVar commands(view->GetProto(RSSYM_keycommands));
		if (IsArray(commands))
		{
			long count = Length(commands);
			for (long i = 0; i < count; i++)
			{
				RefVar one(GetArraySlotRef(commands, i));
				if (!EQRef(GetFrameSlotRef(one, RSSYMkeymessage), message))
					continue;
				UniChar shown = GetDisplayCmdChar(one);
				if (what == 0 || (what == 1 && shown != 0))
				{
					found = one;
					done = true;
					break;
				}
				if (what == 2)
					AddArraySlot(found, one);
			}
		}
		if (!done)
			view = NextKeyCommandView(view);
	}
	return found;
}


// ROM 0x0030b2a4 AddKeyCommands__FRC6RefVarT1
// Commands added to a view's own `_keyCommands`.  A view that has none
// simply takes the array given; one that has takes a copy - of its
// `viewChildren`'s array when it has one and its own is read-only (the
// ROM's own templates live in the read-only ROM) - and the new commands
// are munged onto the front of it.
void
AddKeyCommands(RefArg context, RefArg commands)
{
	RefVar mine(GetProtoVariable(context, RSSYM_keycommands, nil));
	if (ISNIL(mine))
	{
		SetFrameSlot(context, RSSYM_keycommands, commands);
		return;
	}
	RefVar children(GetFrameSlotRef(context, RSSYMviewchildren));
	RefVar source(mine);
	if (NOTNIL(children) && (ObjectFlags(mine) & kObjReadOnly) != 0)
		source = children;
	RefVar copy(Clone(source));
	ArrayMunger(copy, 0, 0, commands, 0, Length(commands));
	SetFrameSlot(context, RSSYM_keycommands, copy);
}


// ROM 0x0030b3ec BlockKeyCommand__FP5TViewRC6RefVar
// A key message stopped here: every command along the chain that sends
// it is answered with a key command of its own on this view - the same
// character and modifiers, and no message - which the search finds
// first and so does nothing.
void
BlockKeyCommand(TView* view, RefArg message)
{
	RefVar matched(MatchKeyMessage(view, message, 2));
	long count = Length(matched);
	if (count == 0)
		return;
	RefVar mine(view->GetProto(RSSYM_keycommands));
	if (ISNIL(mine))
		mine = AllocateArray(RSSYMarray, 0);
	else if ((ObjectFlags(mine) & kObjReadOnly) != 0)
		mine = Clone(mine);
	SetFrameSlot(RefVar(view->fContext), RSSYM_keycommands, mine);
	for (long i = 0; i < count; i++)
	{
		RefVar one(GetArraySlotRef(matched, i));
		RefVar c(GetFrameSlotRef(one, RSSYMchar));
		RefVar modifiers(GetFrameSlotRef(one, RSSYMmodifiers));
		RefVar blocker(Clone(RefVar(Rcanonicalkeycommand)));
		SetFrameSlot(blocker, RSSYMchar, c);
		SetFrameSlot(blocker, RSSYMmodifiers, modifiers);
		ArrayInsert(mine, blocker, 0);
	}
}

// ROM 0x0030fda0 StringsSame__FRC6RefVarT1
// Whether two strings read the same.  The same object is the same string
// without looking; otherwise they are compared right through, cases and
// diacriticals and all (frames/RichString.h).
Boolean
StringsSame(RefArg a, RefArg b)
{
	if (EQRef(a, b))
		return true;
	TRichString first(a);
	TRichString second(b);
	return first.CompareSubStringCommon(second, 0, -1, false) == 0;
}


// ROM 0x0030fe38 CategorizeKeyCommands__FRC6RefVar
// A gathered array of key commands sorted into the groups a keyboard
// help slip shows: an array of `canonicalKeyCommandCategory` frames,
// each with the category's name and the `keyCommands` in it.
//
// A command with no `category` is given the "other" one first, and the
// array is sorted by category so that the ones alike fall together; each
// run of them becomes a group.  Only commands with a `name` and a
// character anyone could read are shown at all.  The groups are sorted
// by name in their turn, and the "other" group is moved to the end - it
// is the one nobody named.
//
// The input array is changed where a command had no category: that
// command is replaced, in place, by a copy carrying one.
Ref
CategorizeKeyCommands(RefArg commands)
{
	long count = Length(commands);
	for (long i = 0; i < count; i++)
	{
		RefVar one(GetArraySlotRef(commands, i));
		if (ISNIL(GetFrameSlotRef(one, RSSYMcategory)))
		{
			one = Clone(one);
			SetFrameSlot(one, RSSYMcategory, RefVar(Rothercategoryname));
			SetArraySlotRef(commands, i, one);
		}
	}
	SortArray(commands, RefVar(RSSYMstr_3C), RefVar(RSSYMcategory));

	RefVar groups(AllocateArray(RSSYMarray, 0));
	RefVar lastName;
	RefVar group;
	RefVar members;
	for (long i = 0; i < count; i++)
	{
		RefVar one(GetArraySlotRef(commands, i));
		UniChar shown = GetDisplayCmdChar(one);
		if (ISNIL(GetFrameSlotRef(one, RSSYMname)) || !UserVisibleChar(shown))
			continue;
		RefVar name(GetFrameSlotRef(one, RSSYMcategory));
		if (ISNIL(name))
			name = Rothercategoryname;
		if (ISNIL(lastName) || !StringsSame(name, lastName))
		{
			group = Clone(RefVar(Rcanonicalkeycommandcategory));
			lastName = name;
			SetFrameSlot(group, RSSYMcategory, lastName);
			members = AllocateArray(RSSYMarray, 1);
			SetArraySlotRef(members, 0, one);
			SetFrameSlot(group, RSSYMkeycommands, members);
			AddArraySlot(groups, group);
		}
		else
			AddArraySlot(members, one);
	}

	SortArray(groups, RefVar(RSSYMstr_3C), RefVar(RSSYMcategory));
	// the groups' own commands sorted by name, and "other" moved to the
	// end (once - `moved` is what stops it being moved for ever)
	long groupCount = Length(groups);
	Boolean moved = false;
	for (long i = 0; i < groupCount; )
	{
		RefVar one(GetArraySlotRef(groups, i));
		RefVar its(GetFrameSlotRef(one, RSSYMkeycommands));
		RefVar name(GetFrameSlotRef(one, RSSYMcategory));
		if (moved || i == groupCount - 1 || !StringsSame(name, RefVar(Rothercategoryname)))
		{
			SortArray(its, RefVar(RSSYMstr_3C), RefVar(RSSYMname));
			i++;
		}
		else
		{
			RefVar kept(one);
			ArrayRemoveCount(groups, i, 1);
			AddArraySlot(groups, kept);
			moved = true;
		}
	}
	return groups;
}


// ROM 0x00310218 FCategorizeKeyCommands
static Ref
FCategorizeKeyCommands(RefArg /*rcvr*/, RefArg commands)
{
	return CategorizeKeyCommands(commands);
}

// ROM 0x0030f4bc FFindKeyCommand
// FindKeyCommand(view, char, modifiers): the command that key would
// reach from that view, or nil.
static Ref
FFindKeyCommand(RefArg /*rcvr*/, RefArg view, RefArg c, RefArg modifiers)
{
	TView* theView = GetView(view);
	if (theView == nil)
		return NILREF;
	ULong bits = (ULong) RINT(modifiers);
	UniChar key = RCHAR(c);
	return FindKeyCommand(theView, key, bits);
}


// ROM 0x0030fd48 FGatherKeyCommands
static Ref
FGatherKeyCommands(RefArg /*rcvr*/, RefArg view)
{
	return GatherKeyCommands(GetView(view));
}


// ROM 0x0030fa3c FMatchKeyMessage
// MatchKeyMessage(view, message, what): see MatchKeyMessage above.
static Ref
FMatchKeyMessage(RefArg /*rcvr*/, RefArg view, RefArg message)
{
	TView* theView = GetView(view);
	if (theView == nil)
		return NILREF;
	return MatchKeyMessage(theView, message, 1);
}


// ROM 0x0030b3d4 FAddKeyCommands
// view:AddKeyCommands(commands)
Ref
FAddKeyCommands(RefArg rcvr, RefArg commands)
{
	AddKeyCommands(rcvr, commands);
	return NILREF;
}


// ROM 0x0030b58c FBlockKeyCommand
// view:BlockKeyCommand(message)
Ref
FBlockKeyCommand(RefArg rcvr, RefArg message)
{
	TView* view = GetView(rcvr);
	if (view != nil)
		BlockKeyCommand(view, message);
	return NILREF;
}


// ROM 0x00269c1c FInRepeatedKeyCommand
// InRepeatedKeyCommand(): whether the key command being run now is one
// the key repeat sent, rather than a key the writer has just pressed -
// which is how a command that should not repeat says so.
static Ref
FInRepeatedKeyCommand(RefArg /*rcvr*/)
{
	return MAKEBOOLEAN(gInRepeatedKeyCommand);
}


void
RegisterKeyboardNatives(void)
{
	RegisterNativeFunction("FInRepeatedKeyCommand", (void*) FInRepeatedKeyCommand, 0);
	RegisterNativeFunction("FFindKeyCommand", (void*) FFindKeyCommand, 3);
	RegisterNativeFunction("FCategorizeKeyCommands", (void*) FCategorizeKeyCommands, 1);
	RegisterNativeFunction("FGatherKeyCommands", (void*) FGatherKeyCommands, 1);
	RegisterNativeFunction("FMatchKeyMessage", (void*) FMatchKeyMessage, 2);
	RegisterNativeFunction("FAddKeyCommands", (void*) FAddKeyCommands, 1);
	RegisterNativeFunction("FBlockKeyCommand", (void*) FBlockKeyCommand, 1);
	RegisterNativeFunction("FKeyIn", (void*) FKeyIn, 2);
	RegisterNativeFunction("FAddKeyCommand", (void*) FAddKeyCommand, 1);
	RegisterNativeFunction("FTranslateKey", (void*) FTranslateKey, 3);
	RegisterNativeFunction("FIsKeyDown", (void*) FIsKeyDown, 2);
	RegisterNativeFunction("FGetTrueModifiers", (void*) FGetTrueModifiers, 0);
	RegisterNativeFunction("FIsCommandKeystroke", (void*) FIsCommandKeystroke, 2);
	RegisterNativeFunction("FPostKeyString__FRC6RefVarN21", (void*) FPostKeyString, 2);
	RegisterNativeFunction("FHandleKeyEvents", (void*) FHandleKeyEvents, 1);
	RegisterNativeFunction("FSendKeyMessage", (void*) FSendKeyMessage, 2);
	RegisterNativeFunction("FClearHardKeymap", (void*) FClearHardKeymap, 0);
}
