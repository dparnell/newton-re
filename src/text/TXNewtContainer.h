/*
	File:		text/TXNewtContainer.h

	Contains:	TXNewtContainer: a container (TXContainer.h) whose values
				are the slots of a NewtonScript frame - which is how a
				script hands text to a protoTXView and gets it back
				(Replace, GetRangeData, Externalize, Internalize: TXView.h).

				The frame's slots:
				  text     the characters, a string ('TEXT');
				  styles   the runs, an array of (length, font spec) pairs
				           ('txrn') - with no styles, `viewFont` is the one
				           style of the whole text;
				  rulers   the rulers, (length, ruler frame) pairs ('txrl').
				A frame whose class is 'graphics is a picture's run on its
				own ('shap': TXNewtGraphicsRun's public type).

				Reading an object makes a new one (a text run, a ruler or a
				graphics run) from the slot and hands it over to the reader
				(`owned`); writing one keeps only its frame, and asks the
				writer to give the object back.  Big text (2K characters or
				more) is written into a compressed large binary on the
				first store.

	Reconstructed from the MP2x00 US ROM (0x0023e290-0x0023e5cc,
	0x0023f1bc-0x0023f648); each function cites its origin.
*/

#ifndef __TXNEWTCONTAINER_H
#define __TXNEWTCONTAINER_H

#ifndef __TXCONTAINER_H
#include "TXContainer.h"
#endif

const unsigned long	kTXValueGraphics	= 0x73686170;	// 'shap'

// The ROM's object is 0x10 bytes.
class TXNewtContainer : public TXContainer
{
public:
					TXNewtContainer(RefArg frame);					// ROM 0x0023f1bc __ct__15TXNewtContainerFRC6RefVar
	virtual			~TXNewtContainer();								// ROM 0x0023f228 __dt__15TXNewtContainerFv

	virtual NewtonErr AppendNewValue(unsigned long type, long count);	// ROM 0x0023f264 AppendNewValue__15TXNewtContainerFUll
	virtual NewtonErr WriteText(TXTextDescriptor* text);			// ROM 0x0023f55c WriteText__15TXNewtContainerFP16TXTextDescriptor
	virtual NewtonErr WriteObject(long index, TXAttrObject* object, long length, unsigned char* reference);	// ROM 0x0023f440 WriteObject__15TXNewtContainerFlP12TXAttrObjectT1PUc
	virtual NewtonErr FocusOnValue(unsigned long type);				// ROM 0x0023e290 FocusOnValue__15TXNewtContainerFUl
	virtual NewtonErr GetCountObjects(long* count);					// ROM 0x0023e3d8 GetCountObjects__15TXNewtContainerFPl
	virtual NewtonErr GetValueSize(long* size);						// ROM 0x0023e4f4 GetValueSize__15TXNewtContainerFPl
	virtual void	AcquireTextDescriptor(TXTextDescriptor* text);	// ROM 0x0023e518 AcquireTextDescriptor__15TXNewtContainerFP16TXTextDescriptor
	virtual void	ReleaseTextDescriptor(TXTextDescriptor* text);	// ROM 0x0023e588 ReleaseTextDescriptor__15TXNewtContainerFP16TXTextDescriptor
	virtual NewtonErr ReadObject(long index, TXAttrObject** object, long* length, unsigned char* owned);	// ROM 0x0023e5cc ReadObject__15TXNewtContainerFlPP12TXAttrObjectPlPUc

	long			GetCountTextChars(void);						// ROM 0x0023e4a4 GetCountTextChars__15TXNewtContainerFv

	RefStruct		fFrame;			// +0x0c
};

#endif	/* __TXNEWTCONTAINER_H */
