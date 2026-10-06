/*
	File:		views/Reflow.cpp

	Contains:	ReFlow and ReflowPreflight: a page's children laid out
				again for printing.  ReFlow takes the views of a page (the
				Notepad's paper roll hands it its data), gathers them into
				groups of the ones that overlap one another down the page,
				and makes each group a view of its own, stacked one under
				the other, with a paragraph on its own poured through the
				printer's width and cut into as many groups as the page
				height makes it (ReflowText).  ReflowPreflight gathers the
				fonts a page's children use, which is what the print
				format's font slip offers.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "View.h"
#include "RootView.h"
#include "ParagraphView.h"
#include "Fonts.h"
#include "RichString.h"		// IsInkWord
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"	// GetVariable
#include "NativeFunctions.h"
#include "RSSymbols.h"
#include "ROMConstants.h"
#include "Locale.h"			// GetPreference
#include "Rects.h"
#include "Unicode.h"		// Ustrlen
#include "host/RomBugs.h"
#include <string.h>


const UniChar kCR = 0x0d;


// ROM 0x001a4bb0 MungeStyles__FRC6RefVarT1
// A styles array with every run's font at the size of `font` (an ink
// word left at its own); anything else comes back as it is.  (The
// NewtonScript MungeStyles, FMungeStyles, is the same thing again.)
static Ref
MungeStyles(RefArg styles, RefArg font)
{
	RefVar result;
	if (!IsArray(styles))
		result = styles;
	else
	{
		long count = Length(styles);
		result = Clone(styles);
		long size = GetFontSize(font);
		for (long slot = 1; slot < count; slot += 2)
		{
			if (!IsInkWord(RefVar(GetArraySlot(result, slot))))
				SetArraySlot(result, slot, RefVar(SetFontSize(RefVar(GetArraySlot(result, slot)), size)));
		}
	}
	return result;
}


// ROM 0x001a4ccc MungeAllStyles__FRC6RefVarT1
// A styles array with every run's font replaced by `font` (an ink word
// left as it is); nil for anything that is not an array.
static Ref
MungeAllStyles(RefArg styles, RefArg font)
{
	RefVar result;
	if (IsArray(styles))
	{
		long count = Length(styles);
		result = Clone(styles);
		RefVar one;
		for (long slot = 1; slot < count; slot += 2)
		{
			one = GetArraySlot(result, slot);
			if (!IsInkWord(one))
				SetArraySlot(result, slot, font);
		}
	}
	return result;
}


// ROM 0x001a4d98 MungeInkScale__FRC6RefVarT1
// A styles array with each ink word copied and given the scale the user
// prints ink at (SetFontParms); anything else comes back as it is.
static Ref
MungeInkScale(RefArg styles, RefArg scale)
{
	RefVar result;
	if (!IsArray(styles))
		result = styles;
	else
	{
		long count = Length(styles);
		result = Clone(styles);
		RefVar one;
		for (long slot = 1; slot < count; slot += 2)
		{
			one = GetArraySlot(result, slot);
			if (IsInkWord(one))
			{
				one = Clone(one);
				SetArraySlot(result, slot, one);
				SetFontParms(one, scale);
			}
		}
	}
	return result;
}


// ROM 0x001a4e8c SplitStyles__FRC6RefVarlT2
// The runs of a styles array that cover the characters [start, end): the
// runs before `start` taken out, the one it falls in shortened from the
// front, and the array cut after the run `end` falls in, which is
// shortened to finish there.
//
// ROM BUG (fixed): a part that starts and ends inside one run gets that run
// shortened twice over the same variable - first to what is left after
// `start`, then overwritten with `end` less the run's *start* - so the
// run comes out `start` less the run's start longer than the text.  The
// fix measures the last run from `start` when the part starts inside it.
static Ref
SplitStyles(RefArg styles, long start, long end)
{
	RefVar result;
	if (IsArray(styles))
	{
		result = Clone(styles);
		long count = Length(styles);
		long pos = 0;
		for (long slot = 0; slot < count; )
		{
			Long runLength = RINT(GetArraySlot(result, slot));
			long runEnd = pos + runLength;
			if (start < runEnd)
			{
				if (pos < start)
					SetArraySlot(result, slot, RefVar(MAKEINT(runLength - (start - pos))));
			}
			else
			{
				ArrayMunger(result, slot, 2, RefVar(), 0, 0);
				slot -= 2;
				count -= 2;
			}
			if (end < pos)
			{
				SetLength(result, slot);
				break;
			}
			if (end <= runEnd)
			{
				long from = (RomBugFixed() && pos < start) ? start : pos;
				SetArraySlot(result, slot, RefVar(MAKEINT(end - from)));
				slot += 2;
				SetLength(result, slot);
				break;
			}
			slot += 2;
			pos = runEnd;
		}
	}
	return result;
}


// A string binary of `length` characters from `chars`, terminated.
static Ref
TextPart(const UniChar* chars, long length)
{
	long size = length * sizeof(UniChar) + sizeof(UniChar);
	RefVar string(AllocateBinary(RSSYMstring, size));
	TBinaryDataPtr data(string);
	UniChar* to = (UniChar*) (char*) data;
	memmove(to, chars, size);
	to[length] = 0;
	return string;
}


// ROM 0x001a5014 ReflowText__FRC6RefVarN21lPlT4
// A paragraph poured into the printer's width and cut where the page runs
// out: each piece a copy of the paragraph (horizontal justification only,
// its fonts as the print format says - `reflowFont` in place of all of
// them, or only its size, by `unistyle` - and its text flags without the
// size-to-the-text bit) put in a group of its own on `pages`.  The piece
// is built as a real view under the root to find where its laid-out
// lines stop (OffsetPastVisible): the text is cut there and the rest goes
// round again as the next piece, which starts a page (a first group).
// `*y` is the room left on the page going in and, coming out, what is
// left after the last piece and the gap under it.
//
// The walk over the text looks at each character in turn but only ever
// stops at the end of the text: whatever split the paragraph at its
// returns is not there, so a piece is always the rest of the text (less a
// final return).
//
// ROM BUGS (fixed): the styles given to a piece are those split for the
// whole of the rest of the text, not cut again where the page cuts it;
// the fonts of a 'all format and the ink words' print scale are worked
// out after the styles slot was set, so neither reaches the piece; `*y`
// is set to the whole page height after every piece, so a paragraph that
// fits where it started is charged as if it had started a page; and a
// cut that leaves one character only drops it, the walk finding the end
// of the text before looking at it.  The fix (RomBugFixed()) sets the
// styles once all three munges are done, splits them again where the page
// cuts the piece, starts the next page's room only when a piece was cut,
// and starts the walk after a cut at the cut itself.
static Ref
PieceStyles(RefArg styles, RefArg format, RefArg reflowFont, RefArg inkScale)
{
	RefVar result(styles);
	if (EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMfont))
		result = MungeStyles(result, reflowFont);
	if (NOTNIL(result) && EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMall))
		result = MungeAllStyles(result, reflowFont);
	if (NOTNIL(result))
		result = MungeInkScale(result, inkScale);
	return result;
}

static void
ReflowText(RefArg para, RefArg format, RefArg pages, long width, long* y, long pageHeight)
{
	RefVar text(GetProtoVariable(para, RSSYMtext, nil));
	RefVar part;
	RefVar group;
	RefVar styles;
	RefVar font;
	RefVar inkScale(GetPreference(RSSYMinkprintingscale));
	RefVar reflowFont(GetFrameSlot(format, RSSYMreflowfont));
	if (ISNIL(reflowFont))
		reflowFont = MAKEINT(0x3001);
	Rect bounds;
	FromObject(RefVar(GetVariable(para, RSSYMviewbounds, nil, 0)), bounds);
	TBinaryDataPtr textData(text);
	const UniChar* base = (const UniChar*) (char*) textData;
	long textLength = Ustrlen(base);
	const UniChar* p = base;
	const UniChar* src = base;
	RefVar scratch;
	long parts = 0;
	long lastBottom = 0;
	if (*p != 0)
	{
		do
		{
			if (p[1] == 0)
			{
				const UniChar* end = p + 1;
				long n = (long) (end - src);
				if (n > 0)
				{
					if (n > 1 && end[-1] == kCR)
						n--;
					styles = GetProtoVariable(para, RSSYMstyles, nil);
					part = Clone(para);
					scratch = GetProtoVariable(para, RSSYMviewjustify, nil);
					scratch = ISINT(scratch) ? MAKEINT(RINT(scratch) & 3) : MAKEINT(0);
					SetFrameSlot(part, RSSYMviewjustify, scratch);
					if (n != textLength)
					{
						SetFrameSlot(part, RSSYMtext, RefVar(TextPart(src, n)));
						if (NOTNIL(styles))
						{
							long offset = (long) (src - base);
							styles = SplitStyles(styles, offset, offset + n);
						}
					}
					scratch = GetProtoVariable(para, RSSYMcopyprotection, nil);
					if (ISNIL(scratch))
					{
						font = GetProtoVariable(para, RSSYMviewfont, nil);
						if (EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMall))
							font = reflowFont;
						else if (ISNIL(font))
							font = reflowFont;
						else if (EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMfont))
							font = SetFontSize(font, GetFontSize(reflowFont));
						SetFrameSlot(part, RSSYMviewfont, font);
					}
					if (NOTNIL(styles) && RomBugFixed())
					{
						styles = PieceStyles(styles, format, reflowFont, inkScale);
						if (NOTNIL(styles))
							SetFrameSlot(part, RSSYMstyles, styles);
					}
					else if (NOTNIL(styles))
					{
						if (EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMfont))
							styles = MungeStyles(styles, reflowFont);
						if (NOTNIL(styles))
							SetFrameSlot(part, RSSYMstyles, styles);
						if (EQ(RefVar(GetFrameSlot(format, RSSYMunistyle)), RSSYMall))
							styles = MungeAllStyles(styles, reflowFont);
						styles = MungeInkScale(styles, inkScale);
					}
					// the piece as wide as the printer and as tall as the
					// paragraph - or as the room left, for a paragraph that
					// sizes itself to its text
					bounds.left = 0;
					bounds.right = (short) width;
					OffsetRect(&bounds, 0, -bounds.top);
					scratch = GetProtoVariable(para, RSSYMviewflags, nil);
					if (!ISINT(scratch) || (RINT(scratch) & 8) != 0)
						bounds.bottom = (short) *y;
					SetFrameSlot(part, RSSYMviewbounds, RefVar(ToObject(bounds)));
					scratch = GetProtoVariable(part, RSSYMtextflags, nil);
					SetFrameSlot(part, RSSYMtextflags, RefVar(ISINT(scratch) ? MAKEINT(RINT(scratch) & ~4) : MAKEINT(0)));
					RefVar context(TView::BuildContext(RefVar(Clone(part)), true));
					scratch = GetProtoVariable(context, RSSYMtextflags, nil);
					SetFrameSlot(context, RSSYMtextflags, RefVar(ISINT(scratch) ? MAKEINT(RINT(scratch) | 0x800) : MAKEINT(0x800)));
					SetFrameSlot(context, RSSYMrecconfig, RefVar(Rrcinkortext));
					TParagraphView* view = (TParagraphView*) BuildView(gRootView, context);
					long visible = view->OffsetPastVisible();
					Rect last;
					view->BoundsOfLastLine(&last);
					lastBottom = last.bottom;
					view->RemoveView();
					Boolean cut = false;		// (the fix) the piece was cut where the page ran out
					if (visible > 0)
					{
						cut = visible < n;
						p = src + visible;
						n = visible;
						if (n > 1 && p[-1] == kCR)
							n--;
						SetFrameSlot(part, RSSYMtext, RefVar(TextPart(src, n)));
						RefVar paraStyles(GetProtoVariable(para, RSSYMstyles, nil));
						if (RomBugFixed() && NOTNIL(paraStyles))
						{
							long offset = (long) (src - base);
							styles = PieceStyles(RefVar(SplitStyles(paraStyles, offset, offset + n)),
												 format, reflowFont, inkScale);
							if (NOTNIL(styles))
								SetFrameSlot(part, RSSYMstyles, styles);
						}
					}
					group = Clone(RefVar(parts > 0 || Length(pages) == 0 ? Rcanonicalfirstgroup : Rcanonicalgroup));
					parts++;
					RefVar children(MakeArray(1));
					SetArraySlot(children, 0, part);
					SetFrameSlot(group, RSSYMviewchildren, children);
					AddArraySlot(pages, group);
					if (!RomBugFixed() || cut)
						*y = pageHeight;
					if (RomBugFixed() && cut)
					{
						src = p;
						continue;			// (the walk goes on from the cut itself)
					}
				}
				src = p;
			}
			p++;
		} while (*p != 0);
	}
	*y -= lastBottom + 20;
}


// ROM 0x001a5e68 ReFlow
// ReFlow(items, format, box, localBox): a page's views laid out again for
// the printer, answered as an array of group views to stack down the
// paper (each a canonicalGroup - the first a canonicalFirstGroup - with
// the views as its children, moved to its own top left).  The group is
// grown from the highest view not yet placed by every view that starts
// within its gutter of the group's bottom (textGutter for a paragraph,
// graphicsGutter for anything else) without making it taller than a
// page, until it stops growing.  A group of one paragraph is poured
// through the width of `localBox` instead (ReflowText).  The format's
// `pageBounds` is the page (else `localBox` is), `viewLineSpacing` goes
// on every group, and the running room left on the page starts a new one
// when a group and the 20-pixel gap under it do not fit.  `box` has only
// to be a rectangle.  Nil when there are no items or either box is not a
// rectangle.
//
// ROM QUIRK kept: which gutter a view gets is chosen by a test of its
// text's length against nought that can never be true, so a view with an
// empty text is treated as text rather than as a picture.
static Ref
ReFlow(RefArg /*rcvr*/, RefArg items, RefArg format, RefArg box, RefArg localBox)
{
	RefVar placed;
	Rect targetBox, local;
	if (ISNIL(items) || !FromObject(box, targetBox) || !FromObject(localBox, local))
		return placed;
	RefVar item;
	RefVar clone;
	long count = Length(items);
	placed = MakeArray(count);
	long width = (short) (local.right - local.left);
	long y = (short) (local.bottom - local.top);
	long pageHeight = y;
	if (NOTNIL(GetFrameSlot(format, RSSYMpagebounds)))
	{
		Rect page;
		FromObject(RefVar(GetFrameSlot(format, RSSYMpagebounds)), page);
		pageHeight = (short) (page.bottom - page.top);
	}
	RefVar pages(MakeArray(0));
	RefVar text;
	Long graphicsGutter = RINT(GetFrameSlot(format, RSSYMgraphicsgutter));
	Long textGutter = RINT(GetFrameSlot(format, RSSYMtextgutter));
	Long lineSpacing = RINT(GetFrameSlot(format, RSSYMviewlinespacing));
	RefVar group;
	Rect band, bounds;
	for ( ; ; )
	{
		// the highest view not yet placed starts the group
		long first = -1;
		long firstTop = 0;
		for (long i = 0; i < count; i++)
		{
			if (NOTNIL(GetArraySlot(placed, i)))
				continue;
			item = GetArraySlot(items, i);
			if (FromObject(RefVar(GetVariable(item, RSSYMviewbounds, nil, 0)), bounds)
			 && (first < 0 || bounds.top < firstTop))
			{
				firstTop = bounds.top;
				first = i;
				band = bounds;
			}
		}
		if (first < 0)
			break;
		// the group grown by every view close enough under it, until it
		// stops growing (a view taken is marked with an integer)
		long members = 0;
		long bottom;
		do
		{
			bottom = band.bottom;
			for (long i = 0; i < count; i++)
			{
				if (NOTNIL(GetArraySlot(placed, i)))
					continue;
				item = GetArraySlot(items, i);
				if (!FromObject(RefVar(GetVariable(item, RSSYMviewbounds, nil, 0)), bounds))
					continue;
				text = GetProtoVariable(item, RSSYMtext, nil);
				long reach;
				if (ISNIL(text))
					reach = bounds.top - graphicsGutter;
				else
				{
					// the ROM asks whether the text's length, unsigned, is
					// below nought - never - so a text always gets the text gutter
					TBinaryDataPtr chars(text);
					Boolean never = Ustrlen((const UniChar*) (char*) chars) < 0 && false;
					reach = bounds.top - (never ? graphicsGutter : textGutter);
				}
				if (i == first
				 || (reach < band.bottom && bounds.bottom - band.top < pageHeight))
				{
					// one more down and across, so that a line (which is
					// empty) still counts in the union
					bounds.bottom++;
					bounds.right++;
					Union(&band, &bounds);
					members++;
					SetArraySlot(placed, i, RefVar(MAKEINT(1)));
				}
			}
		} while (bottom != band.bottom);

		// a paragraph on its own is poured through the width instead
		Boolean isText = false;
		if (members == 1)
		{
			long i;
			for (i = 0; i < count; i++)
			{
				if (ISINT(GetArraySlot(placed, i)))
				{
					item = GetArraySlot(items, i);
					text = GetProtoVariable(item, RSSYMtext, nil);
					isText = NOTNIL(text);
					break;
				}
			}
			if (isText)
			{
				SetArraySlot(placed, i, RefVar(TRUEREF));
				ReflowText(item, format, pages, width, &y, pageHeight);
				continue;
			}
		}

		group = Clone(RefVar(Length(pages) != 0 ? Rcanonicalgroup : Rcanonicalfirstgroup));
		SetFrameSlot(group, RSSYMviewjustify, RefVar(MAKEINT(0xA010)));
		RefVar children(MakeArray(members));
		SetFrameSlot(group, RSSYMviewchildren, children);
		SetFrameSlot(group, RSSYMviewlinespacing, RefVar(MAKEINT(lineSpacing)));
		long child = 0;
		for (long i = 0; i < count; i++)
		{
			if (!ISINT(GetArraySlot(placed, i)))
				continue;
			SetArraySlot(placed, i, RefVar(TRUEREF));
			item = GetArraySlot(items, i);
			FromObject(RefVar(GetVariable(item, RSSYMviewbounds, nil, 0)), bounds);
			clone = Clone(item);
			OffsetRect(&bounds, -band.left, -band.top);
			SetFrameSlot(clone, RSSYMviewbounds, RefVar(ToObject(bounds)));
			if (NOTNIL(GetProtoVariable(item, RSSYMtext, nil)))
			{
				RefVar flags(GetProtoVariable(item, RSSYMtextflags, nil));
				SetFrameSlot(clone, RSSYMtextflags, RefVar(ISINT(flags) ? MAKEINT(RINT(flags) & ~4) : MAKEINT(0)));
			}
			SetArraySlot(children, child++, clone);
		}
		AddArraySlot(pages, group);
		long height = (short) (band.bottom - band.top) + 20;
		if (y <= height)
			y = pageHeight;
		y -= height;
	}
	return pages;
}


// ROM 0x001a5a70 SaveStylee__FRC6RefVarT1
// A font's family, size, face and the font itself each added to the
// preflight frame's arrays of them, once.
static void
SaveStylee(RefArg preflight, RefArg style)
{
	RefVar family(GetFontFamilySym(style));
	long size = GetFontSize(style);
	long face = GetFontFace(style);
	FSetAdd(RefVar(), RefVar(GetFrameSlot(preflight, RSSYMfamily)), family, RefVar(TRUEREF));
	FSetAdd(RefVar(), RefVar(GetFrameSlot(preflight, RSSYMsize)), RefVar(MAKEINT(size)), RefVar(TRUEREF));
	FSetAdd(RefVar(), RefVar(GetFrameSlot(preflight, RSSYMface)), RefVar(MAKEINT(face)), RefVar(TRUEREF));
	FSetAdd(RefVar(), RefVar(GetFrameSlot(preflight, RSSYMstyles)), RefVar(MakeCompactFont(family, size, face)), RefVar(TRUEREF));
}


// ROM 0x001a5cd8 ReflowPreflight
// ReflowPreflight(children): the fonts a page's views use - their
// viewFonts and the fonts of their styles runs - as a copy of the ROM's
// stylePreflight frame, {family, size, face, styles}, each an array of
// the different ones.  The paper roll's print layout looks at it to see
// whether the page is all in one family.
static Ref
ReflowPreflight(RefArg /*rcvr*/, RefArg children)
{
	RefVar preflight(DeepClone(RefVar(Rstylepreflight)));
	if (NOTNIL(children))
	{
		RefVar child;
		long count = Length(children);
		RefVar style;
		for (long i = 0; i < count; i++)
		{
			child = GetArraySlot(children, i);
			style = GetProtoVariable(child, RSSYMviewfont, nil);
			if (NOTNIL(style))
				SaveStylee(preflight, style);
			style = GetProtoVariable(child, RSSYMstyles, nil);
			if (NOTNIL(style))
			{
				if (!IsArray(style))
					SaveStylee(preflight, style);
				else
				{
					long runs = Length(style);
					for (long slot = 1; slot < runs; slot += 2)
						SaveStylee(preflight, RefVar(GetArraySlot(style, slot)));
				}
			}
		}
	}
	return preflight;
}


void
RegisterReflowNatives(void)
{
	RegisterNativeFunction("ReFlow", (void*) ReFlow, 4);
	RegisterNativeFunction("ReflowPreflight", (void*) ReflowPreflight, 1);
}
