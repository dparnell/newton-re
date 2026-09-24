/*
	File:		text/TXObjectRange.h

	Contains:	Which run of the text points at which attribute object.

				A `TXObjectRange` is a `TXRanges` (text/TXArray.h) whose
				element is a range end *and* a `TXAttrObject*`: range i
				covers the characters from element i-1's end to element
				i's, and points at the object that says how they are
				shown.  The styles of a document are one of these, its
				rulers are another, and the same code serves both.

				Everything in it turns on two ideas.  A neighbouring
				range that points at an *equal* object is run together
				with this one rather than kept apart, so a document that
				has been edited back and forth does not end up with a
				hundred ranges all saying the same thing; and an object
				may say (through `GetObjFlags`) that it stands for a
				thing in the text rather than for a way of showing it -
				an embedded picture - in which case it is never merged
				and never shared.

				`TXObjectIterator` walks the ranges from an offset.
				`TXRegisteredObjects` is the small pool of objects a
				document shares (`Textension::RegisterRuler` and
				`RegisterRun` put them there).

	Reconstructed from the MP2x00 US ROM (0x0024055c-0x002413e0,
	0x002359e4-0x00235ab0); each function cites its origin.
*/

#ifndef __TXOBJECTRANGE_H
#define __TXOBJECTRANGE_H

#ifndef __TXATTRIBUTES_H
#include "TXAttributes.h"
#endif


// What GetObjFlags may answer.  Bit 4 is the one this file reads: the
// object stands for a thing in the text - a picture - rather than for a
// way of showing the characters, so two ranges holding it are never run
// together and no other range is ever pointed at the same one.  (Bits 1
// and 2 are set by the graphics runs as well and read higher up.)
const unsigned long	kTXObjIndivisible	= 4;


// One range: where it ends, and what it points at.  The ROM's element
// is eight bytes.
struct TXObjectRangeEntry
{
	TXOffset		fEnd;			// +0x00
	TXAttrObject*	fObject;		// +0x04
};


class TXObjectRange : public TXRanges
{
public:
					TXObjectRange(int chunk);						// ROM 0x0024055c __ct__13TXObjectRangeFi
	virtual			~TXObjectRange();								// ROM 0x002405b8 __dt__13TXObjectRangeFv

	virtual long	Remove(long at, long count);					// ROM 0x002408e8 Remove__13TXObjectRangeFlT1
	virtual NewtonErr FreeData(Boolean compact);					// ROM 0x002410fc FreeData__13TXObjectRangeFUc
	// What the character at `offset` points at.  It must be a
	// character there is: past the last range TXRanges answers an
	// index one past the last element, so the callers guard with
	// GetLastRangeEnd as GetNextObjectRange does.
	virtual TXAttrObject* OffsetToObject(TXOffset offset, Boolean atStart);	// ROM 0x0024112c OffsetToObject__13TXObjectRangeF8TXOffset
	// A new range opened before range `index`, ending at `end`.
	virtual TXAttrObject* InsertObjectRange(long index, TXOffset end, TXAttrObject* object, Boolean reference);	// ROM 0x00240688 InsertObjectRange__13TXObjectRangeFlT1P12TXAttrObjectUc
	// Every object a stretch points at changed by the attribute list;
	// ==> the attribute flags of everything that actually changed.
	virtual unsigned long UpdateRangeObjects(TXOffset at, long length, const TXAttrValues* values, long how);	// ROM 0x00240e58 UpdateRangeObjects__13TXObjectRangeFlT1PC12TXAttrValuesT1
	// Range `index` made to end at `end` and point at `object`.
	virtual TXAttrObject* SetObjectRange(long index, TXOffset end, TXAttrObject* object, Boolean reference);	// ROM 0x00240614 SetObjectRange__13TXObjectRangeFlT1P12TXAttrObjectUc

	// Where a stretch of text falls among the ranges, with their bounds
	// moved so that it can be given an object of its own: `firstIndex`
	// and `lastIndex` come back as the ranges it now takes up.  ==>
	// whether a neighbour already holds that object, in which case
	// there is nothing more to do than take the covered ranges out.
	Boolean			UpdateRangesBounds(TXOffset start, TXOffset end, TXAttrObject* object, long* firstIndex, long* lastIndex);	// ROM 0x00240714 UpdateRangesBounds__13TXObjectRangeFlT1P12TXAttrObjectPlT4
	// A stretch of text pointed at `object`.
	void			ReplaceRangeObj(TXOffset at, long length, TXAttrObject* object, Boolean reference);	// ROM 0x00240924 ReplaceRangeObj__13TXObjectRangeFlT1P12TXAttrObjectUc
	// A stretch of text taken out; a neighbour is stretched over the
	// hole, or the ranges it covered are removed.
	void			ClearRange(TXOffset at, long length);			// ROM 0x002409f4 ClearRange__13TXObjectRangeFlT1
	// `oldLen` characters at `at` replaced by `newLen` pointing at
	// `object` (nil: at whatever was already there).
	NewtonErr		ReplaceRange(TXOffset at, long oldLen, long newLen, TXAttrObject* object, Boolean reference);	// ROM 0x00240b20 ReplaceRange__13TXObjectRangeFlN21P12TXAttrObjectUc
	// The same, with a whole run of ranges coming from another one.
	NewtonErr		ReplaceRange(TXOffset at, long oldLen, TXObjectRange* source, Boolean reference);	// ROM 0x00240cf0 ReplaceRange__13TXObjectRangeFlT1P13TXObjectRangeUc

	// The objects of ranges `from` to `to` (inclusive; -1: to the end)
	// each given a reference back.
	void			FreeObjects(long from, long to);				// ROM 0x00240fac FreeObjects__13TXObjectRangeFlT1
	// The object at `offset` and, through `length`, how much of its
	// range is left from there.
	TXAttrObject*	GetNextObjectRange(TXOffset offset, long* length) const;	// ROM 0x00241160 GetNextObjectRange__13TXObjectRangeCFlPl
	long			CountRangeObjects(TXOffset at, long length);	// ROM 0x002411c4 CountRangeObjects__13TXObjectRangeFlT1
	TXAttrObject*	RangeIndexToObject(long index) const;			// ROM 0x00241258 RangeIndexToObject__13TXObjectRangeCFl
	// The first object already here that is equal to this one.
	TXAttrObject*	SearchObject(const TXAttrObject* object);		// ROM 0x00241270 SearchObject__13TXObjectRangeFPC12TXAttrObject
	// The object to actually point a range at: one already here that is
	// equal to it, or the object itself, or a copy of it.  `found`
	// comes back true when an equal one was already here.
	TXAttrObject*	MapObject(TXAttrObject* object, Boolean reference, Boolean* found);	// ROM 0x002412f0 MapObject__13TXObjectRangeFP12TXAttrObjectUcPUc

	TXAttrObject*	fLastObject;	// +0x18  the last object mapped, so a long run of one style is not searched for twice
	Boolean			fOwnsObjects;	// +0x1c  the ranges give their objects back when they go
};


// A walk over the ranges from an offset.  The ROM's object is 0x18
// bytes; `Next` past the last range leaves `fObject` nil.
class TXObjectIterator
{
public:
					TXObjectIterator(const TXObjectRange* range, TXOffset offset);	// ROM 0x00240f60 __ct__16TXObjectIteratorFPC13TXObjectRangel

	void			SetOffset(TXOffset offset);						// ROM 0x00241024 SetOffset__16TXObjectIteratorFl
	void			Next(void);										// ROM 0x0024109c Next__16TXObjectIteratorFv

	const TXObjectRange*	fRange;		// +0x00
	TXOffset				fOffset;	// +0x04  where this range starts from the iterator's point of view
	long					fLength;	// +0x08  how much of it is left
	TXAttrObject*			fObject;	// +0x0c
	long					fIndex;		// +0x10
	long					fCount;		// +0x14  the ranges there were when the walk began
};


// The handful of attribute objects a document shares.  The ROM's object
// is 0x20 bytes, so there is room for exactly six of them, and `Add`
// does not look at whether there is room.  (Its vtable has one pure
// entry at slot 1 that nothing in the ROM overrides, so the class is
// abstract there and instantiated anyway; nothing calls that slot.)
const long	kTXRegisteredObjectsMax	= 6;

class TXRegisteredObjects
{
public:
					TXRegisteredObjects();							// ROM 0x002359e4 __ct__19TXRegisteredObjectsFv
	virtual			~TXRegisteredObjects();							// ROM 0x00235a20 __dt__19TXRegisteredObjectsFv

	void			Add(TXAttrObject* object);						// ROM 0x00235a84 Add__19TXRegisteredObjectsFP12TXAttrObject
	TXAttrObject*	GetIndObject(int index) const;					// ROM 0x00235aa4 GetIndObject__19TXRegisteredObjectsCFi
	long			GetCount(void) const	{ return fCount; }

	long			fCount;			// +0x04
	TXAttrObject*	fObjects[kTXRegisteredObjectsMax];	// +0x08
};

#endif	/* __TXOBJECTRANGE_H */
