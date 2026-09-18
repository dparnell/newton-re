/*
	File:		views/Keyboard.cpp

	Contains:	The keyboard: key translation, the key maps, the key events
				posted as commands, the key commands.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "Keyboard.h"
#include "View.h"
#include "RootView.h"
#include "Commands.h"
#include "Application.h"
#include "ViewFlags.h"
#include "Locale.h"
#include "Frames.h"
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
static ULong
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
// view chain, the popup dismissed - NOT YET); a key up/down/repeat goes
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
		// NOT YET RECONSTRUCTED: FDismissPopup(nil)
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


void
RegisterKeyboardNatives(void)
{
	RegisterNativeFunction("FKeyIn", (void*) FKeyIn, 2);
	RegisterNativeFunction("FTranslateKey", (void*) FTranslateKey, 3);
	RegisterNativeFunction("FIsKeyDown", (void*) FIsKeyDown, 2);
	RegisterNativeFunction("FGetTrueModifiers", (void*) FGetTrueModifiers, 0);
	RegisterNativeFunction("FIsCommandKeystroke", (void*) FIsCommandKeystroke, 2);
	RegisterNativeFunction("FPostKeyString__FRC6RefVarN21", (void*) FPostKeyString, 2);
	RegisterNativeFunction("FHandleKeyEvents", (void*) FHandleKeyEvents, 1);
	RegisterNativeFunction("FSendKeyMessage", (void*) FSendKeyMessage, 2);
	RegisterNativeFunction("FClearHardKeymap", (void*) FClearHardKeymap, 0);
}
