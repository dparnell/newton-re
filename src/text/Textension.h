/*
	File:		text/Textension.h

	Contains:	Textension: a document - the styled text (TXStyledText.h)
				with its rulers, its formatter, its display and its hilite,
				and the operations a view calls on it.

				`ITextension` puts a document together from its handlers
				(`TXHandlers`: frames, a hilite, a display and characters;
				whichever is nil is made): the rulers (TXRulerRange over a
				new default ruler), the formatter, the display, the hilite,
				and the *pending run* - the style typing will use, a copy
				of the default run until the selection moves
				(`UpdatePendingRun`: then the text run at the caret).
				`TextensionStart` makes the engine's globals once (the
				scratch regions and band arrays, the registered runs and
				rulers); the Newton registers its own default run and ruler
				(`RegisterRun`, `RegisterRuler`).

				An edit is `ReplaceRange` (the rulers', the runs', the
				characters' and the line ends' parts, and the display
				bracketed around them so it redraws what changed), which is
				what typing is (`KeyDown`: characters replace the selection
				in the pending run's style, backspace and escape clear it -
				`ClearKeyDown` - and the arrows move it); `UpdateRangeRuns`
				and `UpdateRangeRulers` restyle a selection, and `Format`
				reformats.  `Click` hands the pen to the hilite.

				A stretch moves in and out as a container (TXContainer.h):
				`Export` writes one, and ReplaceRange puts one in.

				The ROM's object is 0x2c bytes.

	Reconstructed from the MP2x00 US ROM (0x00252af4-0x002540c0); each
	function cites its origin.
*/

#ifndef __TEXTENSION_H
#define __TEXTENSION_H

#ifndef __TXSTYLEDTEXT_H
#include "TXStyledText.h"
#endif
#ifndef __TXHILITE_H
#include "TXHilite.h"
#endif
#ifndef __TXFORMATTER_H
#include "TXFormatter.h"
#endif

class TXContainer;

// What a document is made with; whichever is nil is made.
struct TXHandlers
{
					TXHandlers();									// ROM 0x00252af4 __ct__10TXHandlersFv

	TXFrames*		fFrames;		// +0x00
	TXHilite*		fHilite;		// +0x04
	TXDisplay*		fDisplay;		// +0x08
	TXChars*		fChars;			// +0x0c
};

// ReplaceRange's flags
enum
{
	kTXReplaceCaretAtStart	= 1,	// the caret after the text belongs to what follows
	kTXReplaceNewPendingRun	= 2,	// the pending run is to be worked out again
	kTXReplaceReference		= 8		// the run is shared, not copied
};

// What ReplaceRange puts in: characters (the descriptor), or a
// container, in a run.  The ROM's is 0x24 bytes.
struct TXReplaceParams : public TXTextDescriptor
{
					TXReplaceParams();								// ROM 0x00253d34 __ct__15TXReplaceParamsFv
					TXReplaceParams(const TXTextDescriptor& text);	// ROM 0x00253d84 __ct__15TXReplaceParamsFRC16TXTextDescriptor
					TXReplaceParams(const TXTextDescriptor& text, TXRun* run, Boolean reference);	// ROM 0x00253e44 __ct__15TXReplaceParamsFRC16TXTextDescriptorP5TXRunUc
					// the values of `types` the container has
					TXReplaceParams(TXContainer* container, unsigned char types);	// ROM 0x00253de4 __ct__15TXReplaceParamsFP11TXContainerUc

	TXContainer*	fContainer;		// +0x14
	unsigned char	fTypes;			// +0x18  the container's values to take (kTXImport...)
	TXRun*			fRun;			// +0x1c  nil: the text run at the start
	unsigned long	fFlags;			// +0x20
};


class Textension : public TXStyledText
{
public:
					Textension();									// ROM 0x00253ed4 __ct__10TextensionFv
	virtual			~Textension();									// ROM 0x00254020 __dt__10TextensionFv
	// (TXStyledText's SetTextPort is +0x04)
	virtual void	SetHiliteRange(const TXOffsetRange& range, Boolean show, Boolean scroll);	// ROM 0x00253870 SetHiliteRange__10TextensionFRC13TXOffsetRangeUcT2

	static NewtonErr TextensionStart(void);							// ROM 0x00252b30 TextensionStart__10TextensionSFv
	static void		RegisterRun(TXRun* run);						// ROM 0x00253084 RegisterRun__10TextensionSFP5TXRun
	static void		RegisterRuler(TXRuler* ruler);					// ROM 0x002538ac RegisterRuler__10TextensionSFP7TXRuler
	static TXAttrObject* GetNewRunObject(void);						// ROM 0x00253ebc GetNewRunObject__10TextensionSFv
	static TXAttrObject* GetNewRulerObject(void);					// ROM 0x00253ec8 GetNewRulerObject__10TextensionSFv

	NewtonErr		ITextension(GrafPort* port, const TXHandlers& handlers, char kind);	// ROM 0x00253f14 ITextension__10TextensionFP8GrafPortRC10TXHandlersc
	void			SetCharsHandler(TXChars* chars);				// ROM 0x00252bf8 SetCharsHandler__10TextensionFP7TXChars
	// What a change of size or margins (TXDisplayChanges) calls for:
	// reformatting, redrawing, scrolling.  ==> the display actions taken.
	unsigned long	DisplayChanged(const TXDisplayChanges& changes);	// ROM 0x00252c34 DisplayChanged__10TextensionFRC16TXDisplayChanges
	NewtonErr		Format(Boolean noDisplay, TXOffset start, long length);	// ROM 0x00252d6c Format__10TextensionFUclT2 - length -1: to the end
	void			EndEdit(const TXEditInfo& info, long firstLine, long lastLine, TXOffsetPos* caret);	// ROM 0x00252e28 EndEdit__10TextensionFRC10TXEditInfolT2P8TXOffset
	void			PointToWord(Point pt, TXOffsetRange* range, unsigned char* outside, unsigned char* past);	// ROM 0x00252e5c PointToWord__10TextensionF5PointP13TXOffsetRangePUcT3
	long			CharToLine(TXOffset offset, Boolean atStart, TXOffsetRange* line) const;	// ROM 0x00252ecc CharToLine__10TextensionCF8TXOffsetP13TXOffsetRange
	void			GetRangeBounds(const TXOffsetRange& range, TXLongRect* bounds);	// ROM 0x00252f10 GetRangeBounds__10TextensionFRC13TXOffsetRangeP10TXLongRect
	void			GetRangeBounds(const TXOffsetRange& range, Rect* bounds);	// ROM 0x00252fb4 GetRangeBounds__10TextensionFRC13TXOffsetRangeP4Rect
	void			Click(TXPointingDevice* pen, long flags, TXClickCommandInfo* command, TXClickLoopProc proc, void* data);	// ROM 0x00252fec Click__10TextensionFP16TXPointingDevicelP18TXClickCommandInfoPFUcPvT2_vPv
	// The picture a range of one character is (nil: the hilite's range).
	TXRun*			IsRangeGraphicsRun(const TXOffsetRange* range);	// ROM 0x00253098 IsRangeGraphicsRun__10TextensionFPC13TXOffsetRange
	TXObjectIterator* GetHiliteRangeRuns(TXOffsetRange* range);		// ROM 0x00253128 GetHiliteRangeRuns__10TextensionFP13TXOffsetRange - nil for none; the caller deletes it
	TXAttrObject*	UpdatePendingRun(void);							// ROM 0x00253178 UpdatePendingRun__10TextensionFv
	NewtonErr		ReplaceRange(TXOffset start, TXOffset end, TXReplaceParams* params);	// ROM 0x0025321c ReplaceRange__10TextensionFlT1P15TXReplaceParams
	long			ClearKeyDown(UniChar key, TXOffsetRange* range);	// ROM 0x00253530 ClearKeyDown__10TextensionFUsP13TXOffsetRange
	unsigned int	GetKeyDownFlags(UniChar key);					// ROM 0x002535f4 GetKeyDownFlags__10TextensionFUs
	void			KeyDown(const UniChar* chars, long count, long arrowFlags, unsigned int keyFlags);	// ROM 0x002536a0 KeyDown__10TextensionFPCUslT2Ui
	NewtonErr		Compact(void);									// ROM 0x002537fc Compact__10TextensionFv
	NewtonErr		Activate(Boolean active, Boolean show);			// ROM 0x00253838 Activate__10TextensionFUcT1
	void			GetHiliteRangeWithoutSpaces(TXOffsetRange* range);	// ROM 0x002538c0 GetHiliteRangeWithoutSpaces__10TextensionFP13TXOffsetRange
	void			GetContinuousAttrValues(TXAttrValues* values);	// ROM 0x0025394c GetContinuousAttrValues__10TextensionFP12TXAttrValues
	NewtonErr		UpdateFormatter(unsigned long changed, const TXOffsetRange& range, long* first, long* last);	// ROM 0x00253a44 UpdateFormatter__10TextensionFlRC13TXOffsetRangePlT3
	NewtonErr		UpdateRangeRuns(const TXOffsetRange& range, const TXAttrValues* values, long how);	// ROM 0x00253ae8 UpdateRangeRuns__10TextensionFRC13TXOffsetRangePC12TXAttrValuesl
	NewtonErr		UpdateRangeRulers(const TXOffsetRange& range, const TXAttrValues* values, long how);	// ROM 0x00253c68 UpdateRangeRulers__10TextensionFRC13TXOffsetRangePC12TXAttrValuesl
	// The range's values (`types`) written into `container`; rulers alone
	// widen the range to whole paragraphs.
	NewtonErr		Export(TXOffsetRange* range, TXContainer* container, unsigned char types);	// ROM 0x00253be0 Export__10TextensionFP13TXOffsetRangeP11TXContainerUc

	static TXAttrValues*	fDefaultRunAttrValues;					// ROM 0x0c104ed8 fDefaultRunAttrValues__10Textension

	TXDisplay*		fDisplay;		// +0x10
	TXHilite*		fHilite;		// +0x14
	TXFormatter*	fFormatter;		// +0x18
	TXFrameFormatter* fFrameFormatter;	// +0x1c
	TXRulerRange*	fRulers;		// +0x20
	TXAttrObject*	fPendingRun;	// +0x24
	Boolean			fPendingRunInvalid;	// +0x28
};

#endif	/* __TEXTENSION_H */
