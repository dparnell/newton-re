/*
	File:		views/TextView.cpp

	Contains:	TTextView: a view showing its text.

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "TextView.h"
#include "Text.h"
#include "RichString.h"
#include "ObjectHeap.h"


// ROM 0x002527bc ClassID__9TTextViewCFv
long
TTextView::ClassID(void) const
{
	return clTextView;
}


// ROM 0x002527c4 DerivedFrom__9TTextViewCFl
Boolean
TTextView::DerivedFrom(long id) const
{
	return id == clTextView || TView::DerivedFrom(id);
}


// ROM 0x002527f8 Constructor__9TTextViewFRC6RefVarP5TView
// The transfer mode from viewTransferMode, srcOr when there is none.
void
TTextView::Constructor(RefArg context, TView* parent)
{
	TView::Constructor(context, parent);
	RefVar mode(GetProto(RSSYMviewtransfermode));
	fTransferMode = ISNIL(mode) ? srcOr : RINT(mode);
}


// ROM 0x00252854 RealDraw__9TTextViewFR5TRect
// The text slot (nothing without one) in the viewFont: a single line
// (oneLineOnly) laid out across the bounds' width by the horizontal text
// bits, its baseline the font's ascent below the top - or viewLineSpacing
// below it, centred, or at the bottom by the vertical bits (one pixel
// lower than the room leaves); otherwise wrapped into the bounds by
// TextBox.
void
TTextView::RealDraw(Rect& /*bounds*/)
{
	RefVar text(GetVar(RSSYMtext));
	if (ISNIL(text))
		return;
	TRichString rich(text);
	ULong justify = fViewJustify;
	RefVar font(GetVar(RSSYMviewfont));
	if ((justify & vjOneLineOnly) == 0)
	{
		TextBox(rich, font, viewBounds, justify & vjHMask, justify & vjVMask, fTransferMode);
		return;
	}
	TextOptions options;
	memset(&options, 0, sizeof(options));
	StyleRecord style;
	CreateTextStyleRecord(font, &style);
	options.fAlignment = ConvertToQDFlush(justify & vjJustifyMask, &options.fJustification);
	options.fWidth = (Fixed) (viewBounds.right - viewBounds.left) << 16;
	options.fTransferMode = fTransferMode;
	FontInfo fontInfo;
	GetStyleFontInfo(&style, &fontInfo);
	FPoint where;
	where.x = (Fixed) viewBounds.left << 16;
	where.y = (Fixed) (viewBounds.top + fontInfo.ascent - 1) << 16;
	Fixed extra = (Fixed) (((viewBounds.bottom - viewBounds.top) - (fontInfo.descent + fontInfo.ascent)) * 0x10000);
	switch (justify & vjVMask)
	{
	case vjTopV:
		{
			RefVar spacing(GetProto(RSSYMviewlinespacing));
			if (NOTNIL(spacing))
				where.y = (Fixed) (viewBounds.top + RINT(spacing)) << 16;
		}
		break;
	case vjCenterV:
		where.y += extra / 2 + 0x10000;
		break;
	case vjBottomV:
		where.y += extra + 0x10000;
		break;
	default:
		break;
	}
	DrawRichString(rich, 0, rich.Length(), &style, where, &options, nil);
	DisposeStyleRecord(&style);
}
