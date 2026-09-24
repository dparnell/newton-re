/*
	File:		text/TXAttributes.h

	Contains:	The attributes a run of text carries, and the objects that
				hold them.

				An *attribute* is a four-character tag ('font', 'size',
				'face', ...) and a value of up to twenty bytes.
				`TXAttrValues` is a list of them - a TXArray of 0x20-byte
				elements, each a tag, a flag saying whether the value is
				an object the list owns, the value's length, and the value
				itself.  It is how the engine passes a set of attributes
				about: a style slip hands one down to be applied, and a
				selection hands one back up saying what its runs have in
				common.

				`TXAttrObject` is the base of everything a run can point
				at - a text style, a ruler, a graphics run.  It is
				reference-counted (`Reference`/`Free`), it answers its
				attributes one tag at a time (`GetAttributeValue`,
				`SetAttributeValue`) and in bulk (`GetAttributesValues`),
				and `GetCommonAttrValues` narrows a list down to the
				attributes it shares - which is what makes a style slip
				show a blank where a selection disagrees.

	Reconstructed from the MP2x00 US ROM (0x002310e4-0x002315e8); each
	function cites its origin.
*/

#ifndef __TXATTRIBUTES_H
#define __TXATTRIBUTES_H

#ifndef __TXARRAY_H
#include "TXArray.h"
#endif
#include "objects.h"
#include "NewtErrors.h"

class TXAttrValues;
class TXStream;

typedef unsigned long	TXAttrTag;		// a four-character code

// The tags the text runs carry, as the ROM's `FTXGetContinuousRun`
// reads them out.
enum
{
	kTXAttrFont			= 0x666f6e74,	// 'font' - the family, as a NewtonScript Ref
	kTXAttrSize			= 0x73697a65,	// 'size'
	kTXAttrFace			= 0x66616365,	// 'face'

	// the ruler's (text/TXRuler.h)
	kTXAttrJustification	= 0x6a757374,	// 'just'
	kTXAttrTabs				= 0x74616273,	// 'tabs'
	kTXAttrIndent			= 0x6e646e74,	// 'ndnt' - the first line's left edge
	kTXAttrLeftMargin		= 0x6c4d7267,	// 'lMrg'
	kTXAttrRightMargin		= 0x724d7267,	// 'rMrg'
	kTXAttrLineSpacing		= 0x6c737063	// 'lspc'
};

// One entry of a TXAttrValues list.  The ROM's element is 0x20 bytes,
// twelve of them the tag, the owns flag and the length, so a value may
// be up to twenty.  DEVIATION: the host's pointers are twice as wide,
// and the biggest value the engine puts in one of these is a 'tabs
// update (text/TXRuler.h) - two tabs and a ruler pointer - so the area
// is made large enough for that here.
const long	kTXAttrValueSize	= 32;		// the ROM's is 0x20 - 12 = 20

struct TXAttrValue
{
	TXAttrTag		fTag;			// +0x00
	Boolean			fOwnsValue;		// +0x04  the value is an object this list frees
	long			fLength;		// +0x08
	char			fValue[kTXAttrValueSize];	// +0x0c
};


// A list of attributes.  Its Remove is the array's, with the owned
// values freed first.
class TXAttrValues : public TXArray
{
public:
					TXAttrValues();									// ROM 0x00231340 __ct__12TXAttrValuesFv
	virtual			~TXAttrValues();								// ROM 0x00231388 __dt__12TXAttrValuesFv

	void			Add(TXAttrTag tag, const void* value, int length, Boolean ownsValue);	// ROM 0x00231408 Add__12TXAttrValuesFUlPCviUc
	virtual long	Remove(long at, long count);					// ROM 0x0023145c Remove__12TXAttrValuesFlT1
	void			GetIndAttrData(long index, TXAttrTag* tag, void* value, int* length) const;	// ROM 0x002314cc GetIndAttrData__12TXAttrValuesCFlPUlPvPi
	void			SetIndAttrData(long index, TXAttrTag tag, const void* value, int length);	// ROM 0x0023150c SetIndAttrData__12TXAttrValuesFlUlPCvi
	// The value of that tag copied out; ==> whether the list had one.
	Boolean			GetValue(TXAttrTag tag, void* value) const;		// ROM 0x00231544 GetValue__12TXAttrValuesCFUlPv
};


// The base of everything a run of text can point at.  It is
// reference-counted: it starts with one reference, Reference() adds
// another, and Free() gives one back and deletes the object when the
// last one goes.  The ROM's object is 8 bytes.
//
// Everything about attributes is virtual and does nothing here; the
// subclasses - a text style, a ruler, a graphics run - are what answer.
class TXAttrObject : public TXVirtualObject
{
public:
					TXAttrObject();									// ROM 0x002310e4 __ct__12TXAttrObjectFv
	virtual			~TXAttrObject();								// ROM 0x00231120 __dt__12TXAttrObjectFv

	virtual void	Free(void);										// ROM 0x002312fc Free__12TXAttrObjectFv - one reference given back
	virtual void	FreeData(void);									// ROM 0x002315b0 FreeData__12TXAttrObjectFv
	virtual TXAttrObject* CreateNew(void) const = 0;				// (pure: vtable +0x0c)
	// ==> the object to use: this one, with one more reference - or,
	// for a subclass that cannot be shared, a copy of it.
	virtual TXAttrObject* Reference(void);							// ROM 0x002315bc Reference__12TXAttrObjectFv
	virtual long	GetCountReferences(void);						// ROM 0x002315b4 GetCountReferences__12TXAttrObjectFv
	virtual long	GetClassId(void) const = 0;						// (pure: vtable +0x18)
	virtual unsigned long GetObjFlags(void) const;					// ROM 0x002315cc GetObjFlags__12TXAttrObjectCFv
	// Every attribute this object has, added to the list.
	virtual void	GetAttributesValues(TXAttrValues* values);		// ROM 0x00231294 GetAttributesValues__12TXAttrObjectFP12TXAttrValues
	virtual Boolean	IsEqual(const TXAttrObject* other) const;		// ROM 0x00231298 IsEqual__12TXAttrObjectCFPC12TXAttrObject
	virtual void	Assign(const TXAttrObject* other) = 0;			// (pure: vtable +0x28)
	virtual Boolean	GetAttributeValue(TXAttrTag tag, void* value) const;	// ROM 0x002315dc GetAttributeValue__12TXAttrObjectCFUlPv
	virtual void	SetAttributeValue(TXAttrTag tag, const void* value);	// ROM 0x002315e4 SetAttributeValue__12TXAttrObjectFUlPCv
	virtual Ref		GetNSObject(void) const = 0;					// (pure: vtable +0x34)
	virtual void	SetNSObject(RefArg obj) = 0;					// (pure: vtable +0x38)
	virtual long	GetPublicType(void) const;						// ROM 0x002315d4 GetPublicType__12TXAttrObjectCFv
	virtual NewtonErr WritePublicData(TXStream* stream, long* written);	// ROM 0x002312ec WritePublicData__12TXAttrObjectFP8TXStreamPl
	virtual NewtonErr ReadPublicData(TXStream* stream, long length);	// ROM 0x002312f4 ReadPublicData__12TXAttrObjectFP8TXStreaml
	// Whether the object's value for that tag is the one given; the
	// list is narrowed down to the attributes it agrees about.
	virtual Boolean	GetCommonAttrValue(TXAttrTag tag, void* value) const;	// ROM 0x002311dc GetCommonAttrValue__12TXAttrObjectCFUlPv
	virtual unsigned long GetAttributeFlags(TXAttrTag tag) const;	// ROM 0x00231138 GetAttributeFlags__12TXAttrObjectCFUl
	virtual void	UpdateAttribute(TXAttrTag tag, const void* value, long how);	// ROM 0x00231140 UpdateAttribute__12TXAttrObjectFUlPCvl

	// Every value of the list applied; ==> the attribute flags of the
	// ones that were.
	unsigned long	Update(const TXAttrValues* values, long how);	// ROM 0x00231148 Update__12TXAttrObjectFPC12TXAttrValuesl
	// The list narrowed to what this object agrees with.
	void			GetCommonAttrValues(TXAttrValues* values);		// ROM 0x002311e4 GetCommonAttrValues__12TXAttrObjectFP12TXAttrValues

	long			fCountReferences;	// +0x04  one to begin with
};

#endif	/* __TXATTRIBUTES_H */
