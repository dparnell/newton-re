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
				big-endian shorts, as everything of its; a shape stored in a
				soup is not converted - NOT YET).

				TStyleSave holds the style in force while a shape list is
				drawn (the ROM's is 0x70 bytes: the patterns, the mode, the
				font, the clip levels; its transform - the TQDScaler's
				scaling - is NOT YET RECONSTRUCTED, as are ink shapes,
				'picture shapes (QuickDraw pictures), MakeShape from the
				recogniser's shapes, MakePict, ScaleShape, MungeShape, the
				hit testing HitShape/FindShape/PointInShape, GetShapeInfo,
				and the wedge's box (WedgeBox: the whole oval's here).

	Reconstructed from the MP2100 D ROM (0x000ddae0-0x000e5000,
	0x0019a7c4-0x0019b43c); each function cites its origin.
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
				~TPattern();															// ROM 0x0019a808 __dt__8TPatternFv
	Boolean		GetFillPattern(RefArg spec, Boolean isPen);							// ROM 0x0019a7c4 GetFillPattern__8TPatternFRC6RefVarUc

	PatternHandle	fPattern;		// +0x00
	Boolean			fOwned;			// +0x04  disposed by the destructor
	Boolean			fRestoreFg;		// +0x05  the port's pen pattern put back to black when it is this one
};

// one level of a shape list: what a style set that is undone when the
// list ends (0x10 bytes)
struct SaveLevel
{
	void		Init(SaveLevel* previous);											// ROM 0x0019b424 Init__9SaveLevelFP9SaveLevel

	SaveLevel*	fPrevious;		// +0x00
	RgnHandle	fClip;			// +0x04  the clip before the level's clipping
	long		fTransformLevel;// +0x08
	ULong		fFlags;			// +0x0c  1 scaling, 2 clipping
};

class TStyleSave
{
public:
				TStyleSave();														// ROM 0x0019a86c __ct__10TStyleSaveFv
				~TStyleSave();														// ROM 0x0019a9b8 __dt__10TStyleSaveFv
	Boolean		SetStyle(RefArg style, const Point& origin, long flags);			// ROM 0x0019aab8 SetStyle__10TStyleSaveFRC6RefVarRC6TPointl
	void		BeginLevel(SaveLevel* level);										// ROM 0x0019b384 BeginLevel__10TStyleSaveFP9SaveLevel
	void		EndLevel(void);														// ROM 0x0019b3ac EndLevel__10TStyleSaveFv

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
	long		fTransformDepth;	// +0x48  scaling in force (NOT YET: always 0)
	GrafPort*	fPort;				// +0x4c
	RefStruct	fStyle;				// +0x50  the style frame in force
};

// shapes
Boolean	IsStyleFrame(RefArg obj);													// ROM 0x000dea84 IsStyleFrame__FRC6RefVar
Boolean	IsPrimShape(RefArg obj);													// ROM 0x000deac8 IsPrimShape__FRC6RefVar
Ref		MakeRectShape(RefArg cls, RefArg left, RefArg top, RefArg right, RefArg bottom);	// ROM 0x000e260c MakeRectShape__FRC6RefVarN41
void	ShapeBounds(RefArg shape, Rect* bounds);									// ROM 0x000e21cc ShapeBounds__FRC6RefVarP5TRect - a shape's or a list's bounds
void	GetBoundsRect(RefArg shape, Rect* bounds, const Point& origin, TStyleSave* style);	// ROM 0x000e0f00 GetBoundsRect__FRC6RefVarP5TRectRC6TPointP10TStyleSave
void	WedgeBox(Rect* box, short startAngle, short arcAngle);						// ROM 0x000e2738 WedgeBox__FP5TRectsT2
void	DrawShape(RefArg shape, RefArg style, const Point& origin);					// ROM 0x000e0a68 DrawShape__FRC6RefVarT1RC6TPoint
void	DrawShapeList(RefArg shape, const Point& origin, TStyleSave* style);		// ROM 0x000e0d5c DrawShapeList__FRC6RefVarRC6TPointP10TStyleSave
void	DrawOneShape(RefArg shape, const Point& origin, TStyleSave* style);			// ROM 0x000e0fa0 DrawOneShape__FRC6RefVarRC6TPointP10TStyleSave

// the NewtonScript functions: DrawShape, MakeRect, MakeOval, MakeRoundRect,
// MakeLine, MakeWedge, MakePolygon, MakeRegion, MakeText, MakeTextBox,
// ShapeBounds, OffsetShape, IsPrimShape
void	RegisterShapeNatives(void);
Ref		FDrawShape(RefArg rcvr, RefArg shape, RefArg style);						// ROM 0x000ddae4 FDrawShape - a view's DrawShape method

#endif	/* __DRAWSHAPE_H */
