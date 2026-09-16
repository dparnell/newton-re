/*
	File:		views/ViewFlags.h

	Contains:	The view system's constants: the viewFlags bits (the names
				TView::Dump 0x0025e33c prints for them, and the private ones
				above bit 27 that TView::SetFlags keeps out of the context's
				viewFlags slot), the viewJustify bits as JustifyBounds
				0x00262224 applies them, the viewFormat fields as PreDraw/
				PostDraw 0x00266370/0x002666c0 draw them, the view class
				numbers BuildView 0x0025ca18 switches on, the slot cache
				indices (Rslotcachetable 0x005c94c5), and the view system's
				evt.ex error codes.  The NewtonScript-side names are the
				NTK's (Newton Programmer's Guide, "Views"); where a name is
				recalled rather than read from a header it says so.
*/

#ifndef __VIEWFLAGS_H
#define __VIEWFLAGS_H

// viewFlags (fFlags; the low 28 bits are the context's viewFlags slot)
enum
{
	vVisible					= 0x00000001,
	vReadOnly					= 0x00000002,
	vApplication				= 0x00000004,
	vCalculateBounds			= 0x00000008,
	vNoKeys						= 0x00000010,
	vClipping					= 0x00000020,
	vFloating					= 0x00000040,
	vWriteProtected				= 0x00000080,
	vSingleUnit					= 0x00000100,
	vClickable					= 0x00000200,
	vStrokesAllowed				= 0x00000400,
	vGesturesAllowed			= 0x00000800,
	vCharsAllowed				= 0x00001000,
	vNumbersAllowed				= 0x00002000,
	vLettersAllowed				= 0x00004000,
	vPunctuationAllowed			= 0x00008000,
	vShapesAllowed				= 0x00010000,
	vMathAllowed				= 0x00020000,
	vPhoneField					= 0x00040000,
	vDateField					= 0x00080000,
	vTimeField					= 0x00100000,
	vAddressField				= 0x00200000,
	vNameField					= 0x00400000,
	vCapsRequired				= 0x00800000,
	vCustomDictionaries			= 0x01000000,
	vAnythingAllowed			= 0x01fffe00,
	vRecognitionAllowed			= 0x01ffff00,	// the bits FindClosestView matches a mask against
	vSelected					= 0x02000000,
	vClipboard					= 0x04000000,
	vNoScripts					= 0x08000000,	// the view scripts are not run (RunScript, RunCacheScript)
	vViewFlagsMask				= 0x0fffffff,	// what the viewFlags slot holds
	// private to the C++ side: never written to the viewFlags slot
	vIsInSetupForm				= 0x10000000,	// being built (the Constructor, AddViews): JustifyBounds reads viewJustify from the context
	vHasIdlerHint				= 0x20000000,
	vIsMarked					= 0x40000000,	// AddViews/SyncScroll mark the children they keep; RemoveUnmarked drops the rest
	vIsInSetup2					= 0x80000000,	// with vIsInSetupForm: the view is being deleted (Delete, RemoveAllViews)
	vIsBeingDeleted				= 0x90000000
};

// viewJustify (fViewJustify, the low 30 bits; the ROM keeps two private bits above them)
enum
{
	vjLeftH						= 0x00000000,	// the text's alignment in the view (the text views)
	vjRightH					= 0x00000001,
	vjCenterH					= 0x00000002,
	vjFullH						= 0x00000003,
	vjHMask						= 0x00000003,
	vjTopV						= 0x00000000,
	vjCenterV					= 0x00000004,
	vjBottomV					= 0x00000008,
	vjFullV						= 0x0000000c,
	vjVMask						= 0x0000000c,
	vjParentLeftH				= 0x00000000,	// the view's place against its parent
	vjParentCenterH				= 0x00000010,
	vjParentRightH				= 0x00000020,
	vjParentFullH				= 0x00000030,
	vjParentHMask				= 0x00000030,
	vjParentTopV				= 0x00000000,
	vjParentCenterV				= 0x00000040,
	vjParentBottomV				= 0x00000080,
	vjParentFullV				= 0x000000c0,
	vjParentVMask				= 0x000000c0,
	vjParentClip				= 0x00000100,	// placed against the parent's bounds, not its scrolled contents (viewOriginX/Y); the name as recalled from the NTK
	vjSiblingNoH				= 0x00000000,	// the view's place against the previous sibling
	vjSiblingCenterH			= 0x00000200,
	vjSiblingRightH				= 0x00000400,
	vjSiblingFullH				= 0x00000600,
	vjSiblingLeftH				= 0x00000800,
	vjSiblingHMask				= 0x00000e00,
	vjSiblingNoV				= 0x00000000,
	vjSiblingCenterV			= 0x00001000,
	vjSiblingBottomV			= 0x00002000,
	vjSiblingFullV				= 0x00003000,
	vjSiblingTopV				= 0x00004000,
	vjSiblingVMask				= 0x00007000,
	vjSiblingMask				= 0x00007e00,
	vjChildrenLasso				= 0x00008000,	// the Constructor sizes the view to enclose its children (the name as recalled from the NTK)
	vjReflow					= 0x00010000,	// AddViews stops at the first child hanging below the view (the name as recalled from the NTK)
	vjLeftRatio					= 0x04000000,	// the bounds are percentages of the parent's (or sibling's) size
	vjRightRatio				= 0x08000000,
	vjTopRatio					= 0x10000000,
	vjBottomRatio				= 0x20000000,
	vjRatioMask					= 0x3c000000,
	vjJustifyMask				= 0x3fffffff,
	vjIsModal					= 0x40000000	// private: the view is the modal one (SetModalView); kept with the justification word
};

// viewFormat (fViewFormat)
enum
{
	vfFillMask					= 0x0000000f,	// fill: the pattern index, 0 none
	vfFillWhite					= 0x00000001,
	vfFillLtGray				= 0x00000002,
	vfFillGray					= 0x00000003,
	vfFillDkGray				= 0x00000004,
	vfFillBlack					= 0x00000005,
	vfFillCustom				= 0x0000000e,	// the viewFillPattern slot
	vfFrameMask					= 0x000000f0,	// frame: the pattern index
	vfFrameWhite				= 0x00000010,
	vfFrameLtGray				= 0x00000020,
	vfFrameGray					= 0x00000030,
	vfFrameDkGray				= 0x00000040,
	vfFrameBlack				= 0x00000050,
	vfFrameDragger				= 0x00000060,
	vfFrameMatte				= 0x00000070,
	vfFrameDragShadow			= 0x000000d0,	// PostDraw: the matte-then-black double frame with the picture
	vfFrameCustom				= 0x000000e0,	// the viewFramePattern slot
	vfFrameHilite				= 0x000000f0,	// PostDraw: the double frame
	vfPenMask					= 0x00000f00,	// the frame's pen width
	vfPenShift					= 8,
	vfLinesMask					= 0x0000f000,	// lines (viewLineSpacing apart): the pattern index
	vfLinesShift				= 12,
	vfLinesWhite				= 0x00001000,
	vfLinesLtGray				= 0x00002000,
	vfLinesGray					= 0x00003000,
	vfLinesDkGray				= 0x00004000,
	vfLinesBlack				= 0x00005000,
	vfLinesCustom				= 0x0000e000,	// the viewLinePattern slot
	vfInsetMask					= 0x00030000,	// the frame inset from the bounds
	vfInsetShift				= 16,
	vfShadowMask				= 0x000c0000,	// the drop shadow's width
	vfShadowShift				= 18,
	vfHiliteMask				= 0x00f00000,
	vfRoundMask					= 0x0f000000,	// the corners' radius, halved
	vfRoundShift				= 24,
	vfFormatMask				= 0x3fffffff
};

inline long	vfPen(long n)		{ return n << vfPenShift; }
inline long	vfInset(long n)		{ return n << vfInsetShift; }
inline long	vfShadow(long n)	{ return n << vfShadowShift; }
inline long	vfRound(long n)		{ return n << vfRoundShift; }

// the view classes (viewClass; each class's ClassID answers its number;
// bit 16 marks a data template - the realData of a stationery form)
enum
{
	clView						= 74,	// 0x4a
	clRootView					= 75,
	clPictureView				= 76,
	clEditView					= 77,
	clContainerView				= 78,
	clKeyboardView				= 79,
	clMonthView					= 80,
	clParagraphView				= 81,
	clPolygonView				= 82,
	clDataView					= 83,
	clMathExpView				= 84,
	clMathOpView				= 85,
	clMathLineView				= 86,
	clRemoteView				= 88,
	clPickView					= 91,
	clGaugeView					= 92,
	clPrintView					= 94,
	clMeetingView				= 95,
	clSliderView				= 96,
	clTextView					= 98,
	clListView					= 99,
	clClipboard					= 101,
	clOutline					= 105,
	clHelpOutline				= 107,
	clTXView					= 108,
	clDataTemplate				= 0x10000
};

// the slot cache (Rslotcachetable): the slots a view looks up often, each
// with a bit in the view's masks saying whether the lookup may find it
enum
{
	kIndexViewQuitScript		= 0,
	kIndexStyles				= 1,
	kIndexTabs					= 2,
	kIndexRealData				= 3,
	kIndexText					= 4,
	kIndexViewTransferMode		= 5,
	kIndexViewFont				= 6,
	kIndexViewOriginY			= 7,
	kIndexViewOriginX			= 8,
	kIndexViewJustify			= 9,
	kIndexViewFlags				= 10,
	kIndexViewDrawScript		= 11,
	kIndexViewIdleScript		= 12,
	kIndexViewSetupChildrenScript = 13,
	kIndexViewChildren			= 14,
	kIndexButtonClickScript		= 15,
	kIndexViewClickScript		= 16,
	kIndexViewFormat			= 17,
	kIndexViewBounds			= 18,
	kIndexViewShowScript		= 19,
	kIndexViewStrokeScript		= 20,
	kIndexViewGestureScript		= 21,
	kIndexViewHiliteScript		= 22,
	kIndexAllocateContext		= 23,
	kIndexKeyPressScript		= 24,
	kIndexViewKeyUpScript		= 25,
	kIndexViewKeyDownScript		= 26,
	kIndexViewKeyRepeatScript	= 27,
	kIndexStepAllocateContext	= 28,
	kIndexViewChangedScript		= 29,
	kIndexViewRawInkScript		= 30,
	kIndexViewInkWordScript		= 31,
	kIndexViewKeyStringScript	= 32,
	kIndexViewCaretActivateScript = 33,
	kSlotCacheCount				= 34
};

// the view system's errors (evt.ex)
enum
{
	kViewErrNoKeyView			= -8500,	// NextKeyView: the _tabChildren name no view
	kViewErrCouldNotCreate		= -8501,	// BuildView: an unknown viewClass; the Constructor: the viewSetupFormScript deleted the view
	kViewErrNoViewClass			= -8502,	// BuildContext: neither viewClass nor viewStationery
	kViewErrNoStationery		= -8503,	// BuildContext: the viewStationery is not in vars.stdForms
	kViewErrNoViewFlags			= -8504,	// BuildContext: the template has no viewFlags
	kViewErrNoViewBounds		= -8505		// the Constructor: the template has no viewBounds
};

#endif	/* __VIEWFLAGS_H */
