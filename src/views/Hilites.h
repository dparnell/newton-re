/*
	File:		views/Hilites.h

	Contains:	The data hilites - what a view remembers of what is selected
				inside it, as against TView::Hilite, which inverts a whole
				view because it is being pressed.

				A hilite is a C++ object whose address is kept in the view's
				`hilites` array as a pointer Ref (AddressToRef, the same
				"magic" the views themselves are held by), so the array a
				script sees is opaque to it.  THilite is the base - a
				rectangle in the view's own coordinates, and the questions
				asked of it: the region it covers, whether it overlaps a
				rectangle, whether it encloses a point.  TParagraphHilite is
				the paragraph's, a range of characters whose region the
				paragraph works out and caches (TParagraphView::SetupArea).

				HiliteLoop walks a view's hilites, handing out each one's Ref
				and the object behind it.  The loop reads the array's length
				once, so a caller that removes hilites as it goes - as
				TView::RemoveAllHilites does - steps its index and count
				back itself.

	Not in the DDK; reconstructed from the MP2100 D ROM (0x00260bdc-
	0x00261060 and 0x00182e68-0x00183120), each function citing its origin.
	DEVIATION: the ROM's Area answers a TRegion by value, a class the
	reconstruction does not have; here it fills a region the caller owns.
	NOT YET: the copy of the selected text a TParagraphHilite carries (for
	the undo of a replacement) and the CopyFrom that moves it.
*/

#ifndef __HILITES_H
#define __HILITES_H

#ifndef __VIEW_H
#include "View.h"
#endif

#ifndef __REGIONS_H
#include "Regions.h"
#endif


// The virtuals are declared in the ROM's vtable order (analysis/vtable.py
// on THilite's); CopyFrom is not one of them - Clone calls it knowing the
// concrete type.
class THilite
{
public:
					THilite();								// ROM 0x00260bdc __ct__7THiliteFv
	virtual			~THilite();								// ROM 0x00260c10 __dt__7THiliteFv

	virtual THilite* Clone(void);							// ROM 0x00260c70 Clone__7THiliteFv
	virtual void	UpdateBounds(void);						// ROM 0x00260cb0 UpdateBounds__7THiliteFv - the base has none to update
	virtual Boolean	Overlaps(const Rect& r);				// ROM 0x00260cb4 Overlaps__7THiliteFRC5TRect
	virtual Boolean	Encloses(const Point& pt);				// ROM 0x00260cbc Encloses__7THiliteFRC6TPoint
	virtual void	Area(RgnHandle rgn);					// ROM 0x00260c28 Area__7THiliteFv - the bounds, as a region
	void			CopyFrom(THilite* other);				// ROM 0x00260c9c CopyFrom__7THiliteFP7THilite

	Rect			fBounds;			// +0x04  in the view's own coordinates
};


class TParagraphView;

// A range of a paragraph's characters.  The region it covers is the
// paragraph's to work out - the characters are laid out in lines - so it is
// computed once by TParagraphView::SetupArea and kept here; fBounds is that
// region's bounding box.
class TParagraphHilite : public THilite
{
public:
					TParagraphHilite(long start, long end);	// ROM 0x00182e68 __ct__16TParagraphHiliteFl (which takes the text length)
	virtual			~TParagraphHilite();					// ROM 0x00182ef8 __dt__16TParagraphHiliteFv

	virtual THilite* Clone(void);							// ROM 0x00182f50 Clone__16TParagraphHiliteFv
	virtual Boolean	Overlaps(const Rect& r);				// ROM 0x00182fec Overlaps__16TParagraphHiliteFRC5TRect
	virtual Boolean	Encloses(const Point& pt);				// ROM 0x00183070 Encloses__16TParagraphHiliteFRC6TPoint
	virtual void	Area(RgnHandle rgn);					// ROM 0x001830c4 Area__16TParagraphHiliteFv
	void			CopyFrom(THilite* other);				// ROM 0x00182fac CopyFrom__16TParagraphHiliteFP7THilite

	Boolean			HasArea(void) const		{ return fArea != nil && !EmptyRgn(fArea); }
	void			SetArea(RgnHandle rgn);					// host: the region the paragraph worked out, and its bounding box

	long			fStart;				// +0x0c  the first character selected
	long			fEnd;				// +0x10  one past the last
	// +0x14 the selected text, NOT YET
	RgnHandle		fArea;				// +0x18  the region it covers, once the paragraph has said
};


// The hilites of a view, in turn.
class HiliteLoop
{
public:
					HiliteLoop(TView* view);				// ROM 0x00260f58 __ct__10HiliteLoopFP5TView
					~HiliteLoop();							// ROM 0x00260ff8 __dt__10HiliteLoopFv

	Boolean			Next(void);								// ROM 0x00261030 Next__10HiliteLoopFv

	RefVar			fHilites;			// +0x00  the view's hilites array
	long			fIndex;				// +0x04  the next one to hand out
	long			fCount;				// +0x08  how many there were when the loop started
	RefVar			fHilite;			// +0x0c  the current one's Ref
	THilite*		fCurrent;			// +0x10  and the object behind it
};

#endif	/* __HILITES_H */
