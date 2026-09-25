/*
	File:		recognition/CharBox.h

	Contains:	The boxed-character recogniser: one letter, written in
				a box, read.

				This is the shortest way through the engine.  A
				`CharBox` is made over a rectangle, strokes are put
				into it, and it answers the character codes it thinks
				were written, best first.  Inside, the strokes go
				through the patternizers into the classifier's 384
				inputs, the net is run, and its 134 outputs are turned
				into a probability for each of the 256 character codes.

				That last step is `CharBoxNetEvaluate`, and it is where
				the common info's tables earn their keep.  A character
				code the area does not allow scores nothing.  A code
				that stands for a **single** shape takes the net output
				of the node it maps to.  A code that is really *two*
				characters - the compound tables say which two - takes
				the **product** of its two parts' outputs, so that a
				net which never learnt the pair still has an opinion
				about it.

				`CharBoxEvaluate` turns those probabilities into the
				engine's own scores and leans on them with the geometry
				of the box - the height model (`CharModifyProbs`) and
				`GeoContextPenalty`, with the box standing in for the
				letter after - and `CharBoxGetChars` sorts them.

	Reconstructed from the MP2x00 US ROM (0x000561c8-0x00056b7c); each
	function cites its origin.
*/

#ifndef __CHARBOX_H
#define __CHARBOX_H

#ifndef __NETPATTERN_H
#include "NetPattern.h"
#endif

struct RosSegment;

// How many character codes there are, and how many strokes one box
// will take.
const long	kCharBoxCodeCount	= 256;
const long	kCharBoxMaxStrokes	= 6;
// What a code scores when the engine will not have it.
const short	kCharBoxNever		= 0x7ffe;

// One box being written in.  The ROM's object is 0x234 bytes.
struct CharBox
{
	long			fField00;		// +0x000
	long			fField04;		// +0x004
	FRect			fBox;			// +0x008
	NetPatternizer*	fPatternizer;	// +0x018
	NetPattern*		fPattern;		// +0x01c
	RosSegment*		fSegment;		// +0x020
	RosSegment*		fSpare;			// +0x024
	long			fStrokeCount;	// +0x028
	RosStroke**		fStrokes;		// +0x02c  kCharBoxMaxStrokes of them
	BPNet*			fNet;			// +0x030
	short			fScores[kCharBoxCodeCount];	// +0x034 .. +0x233
};

// What `CharBoxGetChars` answers: a score and the code it belongs to.
struct CharBoxChoice
{
	short			fScore;
	UByte			fCode;
	UByte			fPad;
};


// A recogniser over that box.  (The ROM spells it `Intialize`.)
void	CharBoxIntialize(CharBox** out, long field00, const FRect* box,
					long field04, BPNet* net);				// ROM 0x000561c8 CharBoxIntialize
CharBox*	CharBoxStateNew(void);							// ROM 0x00056354 CharBoxStateNew
void	CharBoxDestroy(CharBox* self);						// ROM 0x00056684 CharBoxDestroy

// A stroke put in: it is tidied first, with the four numbers the net's
// own parameters give, and refused when the box is full.  Answers
// nought when it was taken.
long	CharBoxAddStroke(CharBox* self, RosStroke* stroke);	// ROM 0x00056708 CharBoxAddStroke
// Whether a stroke's middle falls inside the box.
Boolean	CharBoxStrokeInBox(const CharBox* self, const RosStroke* stroke);	// ROM 0x000567c0 CharBoxStrokeInBox

// The net run over a piece of writing, and a probability out of 0x10000
// left in `out` for each of the 256 character codes.
void	CharBoxNetEvaluate(CharBox* self, RosStrokeList* strokes,
					Fixed a3, Fixed a4, Fixed a5, Fixed a6, Fixed a7, Fixed a8, Fixed a9,
					Fixed* out);							// ROM 0x00056a44 CharBoxNetEvaluate
// ... which goes through the patternizers.
void	CharBoxNetSetInputs(NetPattern* pattern, BPNet* net, RosStrokeList* strokes,
					Fixed a4, Fixed a5, Fixed a6, Fixed a7, Fixed a8, Fixed a9,
					Fixed a10);								// ROM 0x000563c4 CharBoxNetSetInputs

// The scores turned into the engine's own and leaned on by the box's
// geometry.
void	CharBoxEvaluate(CharBox* self);						// ROM 0x00056878 CharBoxEvaluate
// The best codes, sorted, stopping at the first that scores `never`.
void	CharBoxGetChars(CharBox* self, CharBoxChoice* out, short* count);	// ROM 0x0005682c CharBoxGetChars

#endif	/* __CHARBOX_H */
