/*
	File:		views/KeyHelpSlip.cpp

	Contains:	The key-help slip: the list of command keys in force, shown
				when the command key is held down.  Its template's
				viewSetupFormScript (FKeyHelpSlipSetup) gathers the key
				commands of the front view (GatherKeyCommands), groups them
				(CategorizeKeyCommands), and works out the slip's layout -
				one or two columns of 100 pixels, the width of the widest
				command letter in each column, the height and where on the
				screen it goes; its viewDrawScript (FKeyHelpSlipDraw) draws
				each group's name in bold and under it the commands, each
				as its command letter (right-aligned in the column's letter
				width) with the modifier-key icons before it and its name
				after, the name cut short with an ellipsis when it does not
				fit the column.

	Reconstructed from the MP2x00 US ROM (0x001839f8-0x00184a24); each
	function cites its origin.
*/

#include "View.h"
#include "Keyboard.h"
#include "PickView.h"		// GetAppAreaBounds
#include "Fonts.h"
#include "Text.h"
#include "Pictures.h"		// DrawBitmap
#include "Unicode.h"		// UppercaseText
#include "RichString.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"		// GetVariable
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "SoundSettings.h"	// FClicker


static const long kCommandColumnWidth = 100;		// a column's command letters and names

Ref		FStrLen(RefArg rcvr, RefArg str);			// ROM 0x001fd0a8 FStrLen__FRC6RefVarT1 (frames/StringNatives.cpp)


// ROM 0x001839f8 GetCommandCharWidth__FRC6RefVarP11StyleRecord
// How wide a command's letter is drawn: its `char`, in capitals.
long
GetCommandCharWidth(RefArg command, StyleRecord* style)
{
	UniChar ch = RCHAR(RefVar(GetProtoVariable(command, RSSYMchar, nil)));
	UppercaseText(&ch, 1);
	return MeasureOnce(&ch, 1, style);
}


// ROM 0x00183a74 GetModifiersWidth__FRC6RefVar
// How much room the modifier icons before a command letter take, and 10
// more.
long
GetModifiersWidth(RefArg command)
{
	ULong modifiers = KeyCommandModifiers(command);
	long width = 0;
	if (modifiers & 0x02000000)		// command
		width = 11;
	if (modifiers & 0x04000000)		// shift
		width = (short) (width + 11);
	if (modifiers & 0x10000000)		// option
		width = (short) (width + 12);
	if (modifiers & 0x20000000)		// control
		width = (short) (width + 12);
	return (short) (width + 10);
}


// ROM 0x00183ad0 DrawModifierIcons__FUlN21
// The icons of the modifier keys drawn right to left, ending at x, their
// bottoms on the baseline y, 7 high: command, shift and option 9 wide,
// control 5.
void
DrawModifierIcons(ULong modifiers, long x, long y)
{
	Rect box;
	box.top = (short) (y - 7);
	box.bottom = (short) y;
	x = (short) x;
	if (modifiers & 0x02000000)
	{
		box.right = (short) x;
		box.left = (short) (x - 9);
		DrawBitmap(RefVar(Rcommandkeyicon), &box, 0);
		x = (short) (x - 11);
	}
	if (modifiers & 0x04000000)
	{
		box.right = (short) x;
		box.left = (short) (x - 9);
		DrawBitmap(RefVar(Rshiftkeyicon), &box, 0);
		x = (short) (x - 11);
	}
	if (modifiers & 0x10000000)
	{
		box.right = (short) x;
		box.left = (short) (x - 9);
		DrawBitmap(RefVar(Roptionkeyicon), &box, 0);
		x = (short) (x - 12);
	}
	if (modifiers & 0x20000000)
	{
		box.right = (short) x;
		box.left = (short) (x - 5);
		DrawBitmap(RefVar(Rcontrolkeyicon), &box, 0);
	}
}


// ROM 0x00183c18 GetSlipWidth__FUlN41
// A slip of that many columns: the columns and the gaps between them, a
// margin each side and the room for the scroll arrows.
static ULong
GetSlipWidth(ULong columns, ULong columnWidth, ULong gap, ULong margin, ULong extra)
{
	return gap * (columns - 1) + columnWidth * columns + margin * 2 + extra;
}


// ROM 0x00183c38 FKeyHelpSlipSetup
// The slip's viewSetupFormScript.  Sets the slip's keyCommands (the front
// view's, grouped), numcols (2 when two columns fit the application
// area, else 1), slipHeight, lineHeight, widths (for each group, the
// letter width of each of its columns) and viewBounds: centred across
// the application area, a third of the way down the room it leaves, as
// high as the groups need (each group a line for its name, its rows, and
// 2 pixels, and 10 more) but never more than the area less two lines -
// then with its scroll arrows shown and 9 pixels wider on the left and
// 10 on the right, cut down a line at a time until it fits.  Then the
// pen's click.
//
// ROM QUIRKS kept: the widest letter so far is not reset from one column
// to the next, so a column is never narrower than the one before it; and
// the bounds are the application area's size, not its place (it is at
// the screen's origin on the MP2x00).
static Ref
FKeyHelpSlipSetup(RefArg rcvr)
{
	TView* frontView = GetView(RefVar(NILREF), RSSYMviewfrontcommandkey);
	RefVar commands(GatherKeyCommands(frontView));
	commands = CategorizeKeyCommands(commands);
	SetFrameSlot(rcvr, RSSYMkeycommands, commands);

	Rect area;
	GetAppAreaBounds(&area);
	long columns = 2;
	ULong slipWidth = GetSlipWidth(2, kCommandColumnWidth, 18, 9, 15);
	if ((ULong) ((short) (area.right - area.left) - 4) < slipWidth)
	{
		columns = 1;
		slipWidth = GetSlipWidth(1, kCommandColumnWidth, 18, 9, 15);
	}
	SetFrameSlot(rcvr, RSSYMnumcols, RefVar(MAKEINT(columns)));

	StyleRecord style;
	CreateTextStyleRecord(RefVar(GetVariable(rcvr, RSSYMviewfont, nil, 0)), &style);
	FontInfo info;
	GetStyleFontInfo(&style, &info);
	long lineHeight = info.ascent + info.descent - 1;

	ULong groups = (ULong) Length(commands);
	ULong height = 0;
	RefVar list;
	for (ULong g = 0; g < groups; g++)
	{
		list = GetFrameSlotRef(RefVar(GetArraySlotRef(commands, g)), RSSYMkeycommands);
		ULong count = (ULong) Length(list);
		ULong rows = count / columns + 1;
		if (count % columns != 0)
			rows++;
		height = lineHeight * rows + height + 2;
	}
	ULong slipHeight = height - 2;
	height = slipHeight + 12;

	Rect bounds;
	ULong areaWidth = (ULong) (short) (area.right - area.left);
	bounds.left = areaWidth > slipWidth ? (short) ((areaWidth - slipWidth) >> 1) : 0;
	bounds.right = (short) (bounds.left + slipWidth);
	SetFrameSlot(rcvr, RSSYMslipheight, RefVar(MAKEINT(slipHeight)));
	SetFrameSlot(rcvr, RSSYMlineheight, RefVar(MAKEINT(lineHeight)));

	long areaHeight = (short) (area.bottom - area.top);
	ULong room = (ULong) (areaHeight - lineHeight * 2);
	if (height > room)
	{
		RefVar scrollers(GetProtoVariable(rcvr, RSSYMscrollers, nil));
		SetFrameSlot(scrollers, RSSYMviewflags, RefVar(MAKEINT(3)));
		bounds.left = (short) (bounds.left - 9);
		bounds.right = (short) (bounds.right + 10);
		while (height > room)
			height -= lineHeight;
	}
	bounds.top = (short) ((ULong) (areaHeight - height) / 3);
	bounds.bottom = (short) (bounds.top + height);
	SetFrameSlot(rcvr, RSSYMviewbounds, RefVar(ToObject(bounds)));

	RefVar widths(AllocateArray(RSSYMarray, 0));
	SetFrameSlot(rcvr, RSSYMwidths, widths);
	RefVar command;
	for (ULong g = 0; g < groups; g++)
	{
		RefVar groupWidths(AllocateArray(RSSYMarray, 0));
		AddArraySlot(widths, groupWidths);
		list = GetFrameSlotRef(RefVar(GetArraySlotRef(commands, g)), RSSYMkeycommands);
		ULong count = (ULong) Length(list);
		ULong rows = count / columns;
		ULong remainder = count % columns;
		if (remainder != 0)
			rows++;
		ULong widest = 0, row = 0, column = 0;
		for (ULong i = 0; i < count; i++)
		{
			command = GetArraySlotRef(list, i);
			ULong w = (ULong) GetCommandCharWidth(command, &style);
			if (w > widest)
				widest = w;
			if (row == rows - 1 || i == count - 1
			 || (row == rows - 2 && remainder != 0 && column >= remainder))
			{
				AddArraySlot(groupWidths, RefVar(MAKEINT(widest)));
				row = 0;
				column++;
			}
			else
				row++;
		}
	}
	FClicker(RefVar(NILREF));
	DisposeStyleRecord(&style);
	return NILREF;
}


// ROM 0x00184244 FKeyHelpSlipDraw
// The slip's viewDrawScript: for each group its name in bold at the left
// (9 in), and under it its commands in columns 118 apart from 24 in, a
// column's rows each a line: the command letter in capitals ending at the
// column's 100, the modifier icons before it, the name from the column's
// start - cut short at what fits beside the letter and icons, followed by
// an ellipsis.  A group whose name would start below the slip ends the
// drawing; a command below it is not drawn.
//
// ROM QUIRK kept: a name that does not fit is measured through
// StyledStrTruncate, but only for how many characters to draw: the
// ellipsis the truncation puts on counts among them, so one character
// more of the name than fits is drawn before the ellipsis.
static Ref
FKeyHelpSlipDraw(RefArg rcvr)
{
	StyleRecord style;
	RefVar commands(GetFrameSlotRef(rcvr, RSSYMkeycommands));
	ULong groups = (ULong) Length(commands);
	RefVar widths(GetFrameSlotRef(rcvr, RSSYMwidths));
	CreateTextStyleRecord(RefVar(GetVariable(rcvr, RSSYMviewfont, nil, 0)), &style);
	FontInfo info;
	GetStyleFontInfo(&style, &info);
	long lineHeight = (short) (info.ascent + info.descent - 1);
	ULong columns = (ULong) RINT(RefVar(GetFrameSlotRef(rcvr, RSSYMnumcols)));
	TView* view = GetView(rcvr);
	Rect bounds = view->viewBounds;
	Point origin;
	view->GetChildOrigin(&origin);
	long y = (short) (bounds.top + info.ascent + origin.v + 6);
	long bottom = bounds.bottom;
	StyleRecord* styles = &style;
	ULong plainFace = style.fFontFace;
	RefVar group, name, groupWidths, list, command, commandName;
	for (ULong g = 0; g < groups; g++)
	{
		group = GetArraySlotRef(commands, g);
		name = GetFrameSlotRef(group, RSSYMcategory);
		groupWidths = GetArraySlotRef(widths, g);
		TRichString rich(name);
		FPoint at;
		at.x = (Fixed) ((ULong) (bounds.left + 9) << 16);
		at.y = (Fixed) ((ULong) y << 16);
		if (y > bottom)
			break;
		style.fFontFace = plainFace | 1;			// the group's name in bold
		DrawRichString(rich, 0, rich.Length(), &style, at, nil, nil);
		style.fFontFace = plainFace;
		y = (short) (y + lineHeight);
		long top = y;
		list = GetFrameSlotRef(group, RSSYMkeycommands);
		ULong count = (ULong) Length(list);
		long columnX = bounds.left + 24;
		ULong rows = count / columns;
		ULong remainder = count % columns;
		if (remainder != 0)
			rows++;
		ULong row = 0, column = 0;
		long letterWidth = (short) RINT(RefVar(GetArraySlotRef(groupWidths, 0)));
		for (ULong i = 0; i < count; i++)
		{
			command = GetArraySlotRef(list, i);
			commandName = GetFrameSlotRef(command, RSSYMname);
			UniChar ch = GetDisplayCmdChar(command);
			UppercaseText(&ch, 1);
			long letterRight = columnX + kCommandColumnWidth;
			at.x = (Fixed) ((ULong) (letterRight - letterWidth) << 16);
			at.y = (Fixed) ((ULong) y << 16);
			if (y <= bottom)
			{
				DrawTextOnce(&ch, 1, &styles, nil, at, nil, nil);
				DrawModifierIcons(KeyCommandModifiers(command), letterRight - (letterWidth + 2), y);
			}
			long room = kCommandColumnWidth - (GetModifiersWidth(command) + letterWidth);
			TRichString nameText(commandName);
			long length = nameText.Length();
			TextBoundsInfo measured;
			MeasureRichString(nameText, 0, length, &style, at, nil, &measured);
			Boolean truncated = false;
			if ((ULong) room < (ULong) (long) (short) ((measured.fWidth + 0x8000) >> 16))
			{
				RefVar font(view->GetVar(RSSYMviewfont));
				RefVar cut(StyledStrTruncate(RefVar(Clone(commandName)), room, font));
				length = RINT(RefVar(FStrLen(RefVar(NILREF), cut)));
				truncated = true;
			}
			at.x = (Fixed) ((ULong) columnX << 16);
			if (y <= bottom)
				DrawRichString(nameText, 0, length, &style, at, nil, &measured);
			if (truncated)
			{
				UniChar ellipsis = 0x2026;
				at.x += measured.fWidth;
				if (y <= bottom)
					DrawTextOnce(&ellipsis, 1, &styles, nil, at, nil, nil);
			}
			if (i < count - 1
			 && (row == rows - 1 || (row == rows - 2 && remainder != 0 && column >= remainder)))
			{
				row = 0;
				column++;
				columnX += 118;
				y = top;
				if (column < (ULong) Length(groupWidths))
					letterWidth = (short) RINT(RefVar(GetArraySlotRef(groupWidths, column)));
			}
			else
			{
				y = (short) (y + lineHeight);
				row++;
			}
		}
		y = (short) (lineHeight * rows + 2 + top);
	}
	DisposeStyleRecord(&style);
	return NILREF;
}


void
RegisterKeyHelpSlipNatives(void)
{
	RegisterNativeFunction("FKeyHelpSlipSetup", (void*) FKeyHelpSlipSetup, 0);
	RegisterNativeFunction("FKeyHelpSlipDraw", (void*) FKeyHelpSlipDraw, 0);
}
