/*
	File:		views/DrawShape.h

	Contains:	NewtonScript shapes: the objects MakeRect, MakeOval,
				MakeRoundRect, MakeLine, MakeWedge, MakePolygon, MakeRegion,
				MakeText and MakeTextBox build, and DrawShape, which draws
				a shape or an array of shapes (nested arrays and style
				frames among them) with a style frame - fillPattern,
				penPattern, penSize, transferMode, font, justification,
				textPattern, clipping, selection - into the current port
				from an origin.  A style in an array applies to the shapes
				after it, to the end of its array.

				The shape objects: 'rectangle, 'oval and 'line are 8-byte
				binaries {top, left, bottom, right} (a line's two ends);
				'roundRectangle is 12 bytes {rect, diameter, pad}, 'wedge
				{rect, startAngle, arcAngle}; 'polygon {data: a 'polygonData
				Polygon}, 'region {data: a 'regionData Region}, 'text
				{bounds: a 'boundsRect binary, data: a 'textData string},
				'TextBox {bounds, data: a 'textBox string, wrapped}, 'bitmap
				{bounds, data: 'bits, colorData, mask}, 'picture {bounds,
				data: 'pictureData}, 'ink {bounds, data}.  The bounds
				binaries hold the host's Rect (DEVIATION: the ROM's are
				big-endian shorts, as everything of its; frames/HostOrder.h
				turns them wherever they meet a MessagePad's bytes - a soup,
				NSOF).

				TStyleSave holds the style in force while a shape list is
				drawn (the ROM's is 0x70 bytes: the patterns, the mode, the
				font, the clip levels and its transform (qd/Transform.h's
				TQDScaler).  Ink shapes, 'picture shapes, MakeShape,
				MakePict, ScaleShape, the hit testing (HitShape,
				PointInShape; FindShape and GetShapeInfo in ShapeVerbs.cpp)
				and WedgeBox are here too.

	Reconstructed from the MP2x00 US ROM (0x000dc840-0x000e3d48,
	0x00198178-0x00198df0); each function cites its origin.
*/

#ifndef __DRAWSHAPE_H
#define __DRAWSHAPE_H

#ifndef __VIEW_H
#include "View.h"
#endif

// a pattern from a style slot, disposed when the style goes (0x08 bytes)
struct TPattern
{
				TPattern()	{ fPattern = nil; fOwned = false; fRestoreFg = false; }
				~TPattern();															// ROM 0x001981bc __dt__8TPatternFv
	Boolean		GetFillPattern(RefArg spec, Boolean isPen);							// ROM 0x00198178 GetFillPattern__8TPatternFRC6RefVarUc

	PatternHandle	fPattern;		// +0x00
	Boolean			fOwned;			// +0x04  disposed by the destructor
	Boolean			fRestoreFg;		// +0x05  the port's pen pattern put back to black when it is this one
};

// one level of a shape list: what a style set that is undone when the
// list ends (0x10 bytes)
struct SaveLevel
{
	void		Init(SaveLevel* previous);											// ROM 0x00198dd8 Init__9SaveLevelFP9SaveLevel

	SaveLevel*	fPrevious;		// +0x00
	RgnHandle	fClip;			// +0x04  the clip before the level's clipping
	long		fTransformLevel;// +0x08
	ULong		fFlags;			// +0x0c  1 scaling, 2 clipping
};

class TStyleSave
{
public:
				TStyleSave();														// ROM 0x00198220 __ct__10TStyleSaveFv
				~TStyleSave();														// ROM 0x0019836c __dt__10TStyleSaveFv
	Boolean		SetStyle(RefArg style, const Point& origin, long flags);			// ROM 0x0019846c SetStyle__10TStyleSaveFRC6RefVarRC6TPointl
	void		BeginLevel(SaveLevel* level);										// ROM 0x00198d38 BeginLevel__10TStyleSaveFP9SaveLevel
	void		EndLevel(void);														// ROM 0x00198d60 EndLevel__10TStyleSaveFv

	// Which slots a style frame has: a bit for each, cleared the first time
	// the slot is found nil so it is not asked for again (kStyleHas...)
	struct StyleCacheEntry
	{
		RefStruct	fStyle;		// the frame (a RefStruct for the collector)
		long		fSlots;		// the kStyleHas... bits still worth asking
	};
	StyleCacheEntry*	LookupCache(void);									// ROM 0x001983e4 LookupCache__10TStyleSaveFv

	Boolean		fPen;				// +0x00  the outline is drawn (penPattern not nil)
	Boolean		fFill;				// +0x01  the inside is filled (fillPattern)
	Boolean		fTextPatternSet;	// +0x02
	long		fSelection;			// +0x04
	long		fTransferMode;		// +0x08  the transferMode slot (1 srcOr when none)
	TPattern	fFillPattern;		// +0x0c
	TPattern	fPenPattern;		// +0x14
	TPattern	fTextPattern;		// +0x1c
	Fixed		fJustification;		// +0x24  the TextOptions' (0: none)
	Fixed		fAlignment;			// +0x28  'center 0.5, 'right 1.0
	RefStruct	fFont;				// +0x2c  the font slot, else the userFont preference
	SaveLevel*	fLevel;				// +0x30
	SaveLevel	fBaseLevel;			// +0x34
	long		fClipDepth;			// +0x44  clipping in force
	long		fTransformDepth;	// +0x48  scaling in force (TQDScaler)
	GrafPort*	fPort;				// +0x4c
	RefStruct	fStyle;				// +0x50  the style frame in force
	StyleCacheEntry	fCache[3];		// +0x54  the last three frames' slots
	long		fCacheNext;			// +0x6c  the entry a new frame takes
};

// the slots of a style frame, as the cache's bits
enum
{
	kStyleHasTransferMode	= 0x001,
	kStyleHasFillPattern	= 0x002,
	kStyleHasPenPattern		= 0x004,
	kStyleHasTextPattern	= 0x008,
	kStyleHasPenSize		= 0x010,
	kStyleHasJustification	= 0x020,
	kStyleHasFont			= 0x040,
	kStyleHasClipping		= 0x080,
	kStyleHasTransform		= 0x100,
	kStyleHasSelection		= 0x200
};

// shapes
Boolean	IsStyleFrame(RefArg obj);													// ROM 0x000dd7e4 IsStyleFrame__FRC6RefVar
Boolean	IsPrimShape(RefArg obj);													// ROM 0x000dd828 IsPrimShape__FRC6RefVar
Ref		MakeRectShape(RefArg cls, RefArg left, RefArg top, RefArg right, RefArg bottom);	// ROM 0x000e1360 MakeRectShape__FRC6RefVarN41
class TView;
Ref		CommonMakePict(TView* view, Rect& bounds, RefArg shape, RefArg style);		// ROM 0x000dc8c0 CommonMakePict__FP5TViewR5TRectRC6RefVarT3 - the view, or the shape in the style, recorded into a picture shape
void	ShapeBounds(RefArg shape, Rect* bounds);									// ROM 0x000e0f20 ShapeBounds__FRC6RefVarP5TRect - a shape's or a list's bounds
void	GetBoundsRect(RefArg shape, Rect* bounds, const Point& origin, TStyleSave* style);	// ROM 0x000dfc60 GetBoundsRect__FRC6RefVarP5TRectRC6TPointP10TStyleSave
void	WedgeBox(Rect* box, short startAngle, short arcAngle);						// ROM 0x000e148c WedgeBox__FP5TRectsT2
void	DrawShape(RefArg shape, RefArg style, const Point& origin);					// ROM 0x000df7c8 DrawShape__FRC6RefVarT1RC6TPoint
void	DrawShapeList(RefArg shape, const Point& origin, TStyleSave* style);		// ROM 0x000dfabc DrawShapeList__FRC6RefVarRC6TPointP10TStyleSave
void	DrawShapeScaled(RefArg shape, RefArg style, const Point& origin, Point resolution);	// ROM 0x000df8a8 DrawShapeScaled__FRC6RefVarT1RC6TPoint5Point - drawn at a resolution (v, h) other than 72 dpi
void	DrawOneShape(RefArg shape, const Point& origin, TStyleSave* style);			// ROM 0x000dfd00 DrawOneShape__FRC6RefVarRC6TPointP10TStyleSave

// the NewtonScript functions: DrawShape, MakeRect, MakeOval, MakeRoundRect,
// MakeLine, MakeWedge, MakePolygon, MakeRegion, MakeText, MakeTextBox,
// ShapeBounds, OffsetShape, IsPrimShape
void	RegisterShapeNatives(void);
// the questions asked of shapes (views/ShapeVerbs.cpp): FindShape,
// GetShapeInfo, MakeInk, StrokeInPicture - registered by RegisterShapeNatives
void	RegisterShapeVerbNatives(void);
void	RegisterPictureShapeNatives(void);		// PictToShape (views/PictureShapes.cpp)
Ref		FOffsetShape(RefArg rcvr, RefArg shape, RefArg dx, RefArg dy);				// ROM 0x000dda60 FOffsetShape
Ref		FMakeRect(RefArg rcvr, RefArg left, RefArg top, RefArg right, RefArg bottom);	// ROM 0x000dc894 FMakeRect
Ref		FMakeOval(RefArg rcvr, RefArg left, RefArg top, RefArg right, RefArg bottom);	// ROM 0x000dda10 FMakeOval
Ref		FMakeRoundRect(RefArg rcvr, RefArg left, RefArg top, RefArg right, RefArg bottom, RefArg diameter);	// ROM 0x000e0dd0 FMakeRoundRect
Ref		FMakeLine(RefArg rcvr, RefArg x1, RefArg y1, RefArg x2, RefArg y2);				// ROM 0x000de860 FMakeLine
Ref		FMakeWedge(RefArg rcvr, RefArg left, RefArg top, RefArg right, RefArg bottom, RefArg startAngle, RefArg arcAngle);	// ROM 0x000e2b60 FMakeWedge
Ref		FMakePolygon(RefArg rcvr, RefArg points);										// ROM 0x000e3a78 FMakePolygon
Ref		FMakeTextBox(RefArg rcvr, RefArg str, RefArg left, RefArg top, RefArg right, RefArg bottom);	// ROM 0x000dd094 FMakeTextBox
Ref		FPictToShape(RefArg rcvr, RefArg picture, RefArg bounds);						// ROM 0x000dd6dc FPictToShape (views/PictureShapes.cpp)
Ref		FDrawIntoBitmap(RefArg rcvr, RefArg shape, RefArg styles, RefArg bitmap);	// ROM 0x0003eee0 FDrawIntoBitmap
// turning and flipping a shape about (cx, cy) (views/ShapeVerbs.cpp)
void	RotatePointR(Point* pt, short cx, short cy);		// ROM 0x000de6d8 RotatePointR__FP5PointsT2
void	RotatePointL(Point* pt, short cx, short cy);		// ROM 0x000de72c RotatePointL__FP5PointsT2
void	FlipHPoint(Point* pt, short cx, short cy);			// ROM 0x000de780 FlipHPoint__FP5PointsT2
void	FlipVPoint(Point* pt, short cx, short cy);			// ROM 0x000de7a4 FlipVPoint__FP5PointsT2
void	RotateRectR(Rect* r, short cx, short cy);			// ROM 0x000de7c8 RotateRectR__FP4RectsT2
void	RotateRectL(Rect* r, short cx, short cy);			// ROM 0x000de98c RotateRectL__FP4RectsT2
void	FlipRectV(Rect* r, short cx, short cy);				// ROM 0x000dea24 FlipRectV__FP4RectsT2
void	FlipRectH(Rect* r, short cx, short cy);				// ROM 0x000dea60 FlipRectH__FP4RectsT2
Ref		DoMungeShape(RefArg shape, RefArg operation, RefArg style, short cx, short cy);	// ROM 0x000dea9c DoMungeShape__FRC6RefVarN21sT4
Ref		FMungeShape(RefArg rcvr, RefArg shape, RefArg operation, RefArg style);			// ROM 0x000df718 FMungeShape
Boolean	PointInShape(RefArg shape, const Point& pt, TStyleSave* style);				// ROM 0x000e15b8 PointInShape__FRC6RefVarRC6TPointP10TStyleSave
Boolean	DoFindShape(RefArg shape, const Point& pt, RefVar& path, TStyleSave* style);	// ROM 0x000e1be0 DoFindShape__FRC6RefVarRC6TPointR6RefVarP10TStyleSave
// Whether the point is in the shape, and - for a list of shapes - which
// of them.  `path` comes back with the index of the shape that was hit at
// each level of the list, outermost last.
Boolean	HitShape(RefArg shape, const Point& pt, RefArg path);		// ROM 0x000e17bc HitShape__FRC6RefVarRC6TPointT1

Ref		FDrawShape(RefArg rcvr, RefArg shape, RefArg style);						// ROM 0x000dc844 FDrawShape - a view's DrawShape method
Ref		FViewIntoBitmap(RefArg rcvr, RefArg src, RefArg dst, RefArg bitmap);	// ROM 0x0003f074 FViewIntoBitmap - likewise a view's method

// ROM 0x00191600 MakePolygonForm__FP6TPointlT2RC5TRectT2
// A shape frame for a polygon, or - for verb 14 - for ink, which carries
// no points of its own and is drawn from the `ink` slot the caller adds.
// A pen size other than the two a shape has by default goes into the
// frame's viewFormat.
Ref		MakePolygonForm(const Point* points, long count, long verb, const Rect& box, long pen);

// The verb that says a shape is ink rather than a polygon.
const long kInkVerb = 14;

#endif	/* __DRAWSHAPE_H */
