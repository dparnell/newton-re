// The boxed-character recogniser (recognition/CharBox.h): strokes in a
// box, and what the classifier thinks each of the 256 character codes
// is worth.  This is the first end of the engine that answers a
// question about a piece of writing in characters rather than in
// numbers.
#include "CharBox.h"
#include "RosEngine.h"
#include "Segment.h"
#include "BPNet.h"
#include "FixedMath.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// A stroke from one corner to another.
static RosStroke*
Line(Fixed x0, Fixed y0, Fixed x1, Fixed y1)
{
	FPoint p[2];
	p[0].x = x0;	p[0].y = y0;
	p[1].x = x1;	p[1].y = y1;
	return StrokeCreate(2, p);
}


int
main()
{
	InitHostStandaloneHeap();
	CharInitialize(0);
	BPNet* net = BPNetCreateNumOut(134);
	BPNetLoad(net, nil);

	FRect box;
	box.left = F(10);	box.top = F(20);
	box.right = F(40);	box.bottom = F(60);

	// ---- a recogniser over one box ----
	CharBox* cb = nil;
	CharBoxIntialize(&cb, 7, &box, 9, net);
	EXPECT(cb != nil);
	EXPECT(cb->fField00 == 7 && cb->fField04 == 9);
	EXPECT(cb->fNet == net);
	EXPECT(cb->fBox.left == F(10) && cb->fBox.top == F(20));
	EXPECT(cb->fBox.right == F(40) && cb->fBox.bottom == F(60));
	// the whole set of patternizers, and a pattern made from it
	EXPECT(cb->fPatternizer != nil && cb->fPatternizer->fType == &NetPatternMultiT);
	EXPECT(cb->fPattern != nil && cb->fPattern->fPatternizer == cb->fPatternizer);
	// no strokes, and every character code saying never
	EXPECT(cb->fStrokeCount == 0);
	for (long i = 0; i < kCharBoxMaxStrokes; i++)
		EXPECT(cb->fStrokes[i] == nil);
	long never = 0;
	for (long i = 0; i < kCharBoxCodeCount; i++)
		if (cb->fScores[i] == kCharBoxNever)
			never++;
	EXPECT(never == kCharBoxCodeCount);
	// two segments, made before anything else
	EXPECT(cb->fSegment != nil && cb->fSpare != nil);
	EXPECT(cb->fSegment != cb->fSpare);
	EXPECT(cb->fSegment->fStrokes == nil && cb->fSegment->fCount == 0);

	// ---- what falls inside it ----
	{
		RosStroke* middle = Line(F(20), F(30), F(30), F(50));
		EXPECT(CharBoxStrokeInBox(cb, middle));
		RosStroke* left = Line(F(0), F(30), F(4), F(50));
		EXPECT(!CharBoxStrokeInBox(cb, left));
		RosStroke* below = Line(F(20), F(70), F(30), F(90));
		EXPECT(!CharBoxStrokeInBox(cb, below));
		// exactly on a corner is outside: every comparison is strict
		RosStroke* corner = Line(F(10), F(20), F(10), F(20));
		EXPECT(!CharBoxStrokeInBox(cb, corner));
		StrokeDestroy(middle);
		StrokeDestroy(left);
		StrokeDestroy(below);
		StrokeDestroy(corner);
	}

	// ---- strokes put in ----
	{
		RosStroke* down = Line(F(20), F(25), F(20), F(55));
		RosStroke* across = Line(F(15), F(40), F(35), F(40));
		CharBoxAddStroke(cb, down);
		EXPECT(cb->fStrokeCount == 1);
		EXPECT(cb->fStrokes[0] != nil);
		// what the box holds is a tidied *copy*: the caller's stroke is
		// still its own
		EXPECT(cb->fStrokes[0] != down);
		CharBoxAddStroke(cb, across);
		EXPECT(cb->fStrokeCount == 2);

		// six is all it will take, and the strokes of a refused list go
		// back rather than being kept
		for (long i = 0; i < 8; i++)
			CharBoxAddStroke(cb, down);
		EXPECT(cb->fStrokeCount == 6);

		StrokeDestroy(down);
		StrokeDestroy(across);
	}

	// ---- the classifier, and the 256 character codes ----
	{
		// a stroke down the middle of the box and one across it: an
		// upright cross, written large
		RosStroke* made[2];
		made[0] = Line(F(25), F(22), F(25), F(58));
		made[1] = Line(F(12), F(40), F(38), F(40));
		RosStrokeList* writing = SLCreate(2, made);

		Fixed probs[kCharBoxCodeCount];
		for (long i = 0; i < kCharBoxCodeCount; i++)
			probs[i] = -1;
		CharBoxNetEvaluate(cb, writing, F(58), F(36), F(36), F(58), F(36), 0, F(36),
						probs);

		const UByte* outputs = net->fOutputs;
		long legal = 0, compound = 0, sure = 0;
		for (long code = 0; code < kCharBoxCodeCount; code++)
		{
			Boolean allowed = (RosCI->fLegalUse[code >> 5] & (1UL << (code & 31))) != 0;
			if (!allowed)
			{
				// a code the area will not have scores nothing at all
				EXPECT(probs[code] == 0);
				continue;
			}
			legal++;
			// nothing is ever quite certain: the best a byte of 0xff
			// comes to is 0xff00, not 0x10000
			EXPECT(probs[code] >= 0 && probs[code] <= 0xff00);
			UByte part1 = RosCI->fCompoundPart1[code];
			if (part1 == 0)
			{
				// one shape: straight off the node it maps to
				UByte node = RosCI->fCharToNetNode[code];
				EXPECT(probs[code] == (Fixed) ((ULong) outputs[node] << 8));
			}
			else
			{
				compound++;
				UByte n1 = RosCI->fCharToNetNode[part1];
				UByte n2 = RosCI->fCharToNetNode[RosCI->fCompoundPart2[code]];
				Fixed p1 = (Fixed) ((ULong) outputs[n1] << 8);
				Fixed p2 = (Fixed) ((ULong) outputs[n2] << 8);
				// two characters: the product of their two parts, unless
				// they are the same node
				EXPECT(probs[code] == ((n1 == n2) ? p1 : FixedMultiply(p1, p2)));
				// ... and a product of two numbers under one is under
				// either of them
				if (n1 != n2)
					EXPECT(probs[code] <= p1 && probs[code] <= p2);
			}
			if (probs[code] > 0)
				sure++;
		}
		// the US ROM's tables: 166 of the 256 codes are ones the engine
		// may answer, and 54 of those are really two characters
		EXPECT(legal == 166);
		EXPECT(compound == 54);

		// **And this is the engine reading.**  An upright stroke crossed
		// by a level one is a plus sign or a lower-case t, and those are
		// the only two the trained net will have at all; a capital T
		// comes a distant third because the cross-stroke is halfway down
		// rather than at the top.  Everything else scores nothing.
		EXPECT(sure == 3);
		EXPECT(probs['+'] == 0xf100);		// 0xf1 out of 0xff: near enough certain
		EXPECT(probs['t'] == 0xe500);
		EXPECT(probs['T'] == 0x0100);
		EXPECT(probs['+'] > probs['t'] && probs['t'] > probs['T']);
		EXPECT(probs['x'] == 0 && probs['o'] == 0 && probs['1'] == 0);

		// the inputs really were written: the picture patternizer's
		// cells are no longer all the background
		NetMultiPatternizer* multi = (NetMultiPatternizer*) cb->fPatternizer;
		NetImagePatternizer* image = (NetImagePatternizer*) multi->fChildren[0];
		long lit = 0;
		for (long i = 0; i < image->fWidth * image->fHeight; i++)
			if (image->fInputs[i] != image->fOff)
				lit++;
		EXPECT(lit == 52);			// of the 196 the picture has

		SLDestroy(writing, 1);
	}

	// ---- the characters it answers ----
	{
		// a cross written in a box of its own: the classifier's answer
		// made scores, leaned on by the height model and charged for how
		// it sits in the box, and the best of them handed back sorted
		CharBox* cross = nil;
		CharBoxIntialize(&cross, 0, &box, 0, net);
		RosStroke* down = Line(F(25), F(22), F(25), F(58));
		RosStroke* across = Line(F(12), F(40), F(38), F(40));
		CharBoxAddStroke(cross, down);
		CharBoxAddStroke(cross, across);
		StrokeDestroy(down);
		StrokeDestroy(across);

		CharBoxChoice out[8];
		for (long i = 0; i < 8; i++)
		{
			out[i].fScore = 0x1234;
			out[i].fCode = 0x55;
		}
		short count = 8;
		CharBoxGetChars(cross, out, &count);
		for (long i = 0; i < count; i++)
			printf("  '%c' %d\n", out[i].fCode, out[i].fScore);
		// the three the classifier believed in and nothing else - but
		// in a new order.  The net liked the plus sign best; in a box
		// the geometry decides, and a stroke that runs the whole height
		// of the box is a t's or a T's, not a plus sign's, which sits
		// on the middle of the line.  (Scores: lower is better.)
		EXPECT(count == 3);
		EXPECT(out[0].fCode == 't' && out[1].fCode == 'T' && out[2].fCode == '+');
		for (long i = 1; i < count; i++)
			EXPECT(out[i - 1].fScore <= out[i].fScore);
		// what it did not answer is cleared
		for (long i = count; i < 8; i++)
			EXPECT(out[i].fScore == 0 && out[i].fCode == 0);
		// every code the area will not have still says never
		EXPECT(cross->fScores[0] == kCharBoxNever);
		CharBoxDestroy(cross);

		// a box with nothing in it answers nothing and touches nothing
		CharBox* empty = nil;
		CharBoxIntialize(&empty, 0, &box, 0, net);
		out[0].fScore = 0x1234;
		count = 8;
		CharBoxGetChars(empty, out, &count);
		EXPECT(count == 0);
		EXPECT(out[0].fScore == 0x1234);		// not even cleared
		CharBoxDestroy(empty);
	}

	CharBoxDestroy(cb);
	// a nil one is no trouble
	CharBoxDestroy(nil);

	if (failures == 0)
		printf("test_CharBox: all checks passed\n");
	return failures == 0 ? 0 : 1;
}
