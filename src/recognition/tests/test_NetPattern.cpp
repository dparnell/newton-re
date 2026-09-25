// The patternizers (recognition/NetPattern.h): what turns a piece of
// writing into the classifier's 384 inputs.
#include "NetPattern.h"
#include "RosEngine.h"
#include "Render.h"
#include "FixedMath.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }


// A stroke list of one stroke, the box it wants.
static RosStrokeList*
BoxOfWriting(Fixed left, Fixed top, Fixed right, Fixed bottom, short strokes)
{
	RosStroke* made[16];
	FPoint p[2];
	for (short i = 0; i < strokes; i++)
	{
		// every stroke the same box, so the list's bounds are it
		p[0].x = left;	p[0].y = top;
		p[1].x = right;	p[1].y = bottom;
		made[i] = StrokeCreate(2, p);
	}
	return SLCreate(strokes, made);
}


int
main()
{
	InitHostStandaloneHeap();
	CharInitialize(0);
	BPNet* net = BPNetCreateNumOut(134);
	BPNetLoad(net, nil);

	// ---- the seven kinds, by name ----
	{
		EXPECT(NetPatternLookup("AspectNorm") == 0);
		EXPECT(NetPatternLookup("StrokeCount") == 4);
		EXPECT(NetPatternLookup("ImageSplatLimited") == 5);
		EXPECT(NetPatternLookup("StrokePUD") == 6);
		EXPECT(strcmp(kNetPatternTypes[5].fName, "ImageSplatLimited") == 0);
		EXPECT(kNetPatternTypes[5].fFlag == 1);		// the only one with it set
		Boolean threw = false;
		newton_try
		{
			NetPatternLookup("Handwriting");
		}
		newton_catch_all
		{
			threw = true;
		}
		end_try;
		EXPECT(threw);
	}

	// ---- what the ROM's own net is shown ----
	{
		// four input groups, and they come to exactly its 384 inputs
		long groups = net->fGroupCount;
		EXPECT(groups == 4);
		static const char* const kExpected[4] = {
			"ImageSplatLimited", "StrokePUD", "AspectNorm", "StrokeCount"
		};
		long total = 0;
		for (long i = 0; i < groups; i++)
		{
			EXPECT(strcmp(net->fInputType[i], kExpected[i]) == 0);
			long across = (long) (short) (net->fNGS[i * 2] >> 16);
			// the second measure is the halfword two bytes in, which is
			// where the ROM's unaligned load takes it from
			long down = (long) (short) (net->fNGS[i * 2] & 0xffff);
			if (down == 0)
				down = 1;
			total += across * down;
		}
		EXPECT(total == net->fInputCount);		// 384
	}

	// ---- the whole set, made from the net ----
	NetPatternizer* all = NetPatternizerCreateFromBP(net);
	EXPECT(all != nil);
	EXPECT(all->fType == &NetPatternMultiT);
	NetMultiPatternizer* multi = (NetMultiPatternizer*) all;
	EXPECT(multi->fCount == 4);
	EXPECT(multi->fChildren[2]->fType == &NetPatternAspectNormT);
	EXPECT(multi->fChildren[3]->fType == &NetPatternCountT);
	// ... and each scalar knows where in the net its cells are
	NetScalarPatternizer* aspect = (NetScalarPatternizer*) multi->fChildren[2];
	NetScalarPatternizer* count = (NetScalarPatternizer*) multi->fChildren[3];
	EXPECT(aspect->fCount == 1 && aspect->fInputs == net->fUnits + 376);
	EXPECT(count->fCount == 7 && count->fInputs == net->fUnits + 377);

	// ---- the measurements ----
	{
		// a scalar's value starts at one
		NetPattern* p = NetPatternCreate(multi->fChildren[2]);
		EXPECT(p != nil && p->fPatternizer == multi->fChildren[2]);
		EXPECT(((NetScalarPattern*) p)->fValue == F(1));
		EXPECT(multi->fChildren[2]->fRefCount == 2);	// the Multi's and this one

		const NetPatternSLToPatProc aspectOf = aspect->fType->fSLToPat;

		// twice as wide as tall: 2/1.5 is over one, so it clamps
		RosStrokeList* wide = BoxOfWriting(F(0), F(0), F(19), F(9), 1);
		aspectOf(net, wide, p, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		EXPECT(((NetScalarPattern*) p)->fValue == F(1));

		// square: 1/1.5 is two thirds
		RosStrokeList* square = BoxOfWriting(F(0), F(0), F(9), F(9), 1);
		aspectOf(net, square, p, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		EXPECT(((NetScalarPattern*) p)->fValue == FixedDivide(F(1), F(1) + F(1) / 2));

		// tall and thin: half as wide as tall, over one and a half
		RosStrokeList* tall = BoxOfWriting(F(0), F(0), F(4), F(9), 1);
		aspectOf(net, tall, p, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		Fixed thin = ((NetScalarPattern*) p)->fValue;
		EXPECT(thin > 0 && thin < FixedDivide(F(1), F(1) + F(1) / 2));

		SLDestroy(wide, 1);
		SLDestroy(square, 1);
		SLDestroy(tall, 1);
		NetPatternDestroy(p);
		EXPECT(multi->fChildren[2]->fRefCount == 1);
	}

	// ---- how many strokes, and how it reaches the net ----
	{
		NetPattern* p = NetPatternCreate(multi->fChildren[3]);
		const NetPatternSLToPatProc countOf = count->fType->fSLToPat;

		RosStrokeList* three = BoxOfWriting(F(0), F(0), F(9), F(9), 3);
		countOf(net, three, p, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		// three strokes out of the seven the net can be told about
		EXPECT(((NetScalarPattern*) p)->fValue == FixedDivide(F(3), F(7)));

		// ... written into the net as a one-hot over the seven cells
		for (long i = 0; i < 7; i++)
			count->fInputs[i] = 0xaa;
		NetPatternSetInput(p);
		long lit = -1, on = 0;
		for (long i = 0; i < 7; i++)
			if (count->fInputs[i] == count->fOn)
			{
				lit = i;
				on++;
			}
		EXPECT(on == 1);
		EXPECT(lit == 3);				// 3/7 of 7.99 is 3
		for (long i = 0; i < 7; i++)
			EXPECT(count->fInputs[i] == (i == lit ? count->fOn : count->fOff));

		// more strokes than the net knows about is the most it knows
		RosStrokeList* many = BoxOfWriting(F(0), F(0), F(9), F(9), 12);
		countOf(net, many, p, 0, 0, 0, 0, 0, 0, 0, 0, 0);
		EXPECT(((NetScalarPattern*) p)->fValue == F(1));
		// ROM QUIRK: and a value of exactly one lights no cell at all,
		// because the cell is chosen by multiplying by seven and
		// ninety-nine hundredths
		NetPatternSetInput(p);
		on = 0;
		for (long i = 0; i < 7; i++)
			if (count->fInputs[i] == count->fOn)
				on++;
		EXPECT(on == 0);

		SLDestroy(three, 1);
		SLDestroy(many, 1);
		NetPatternDestroy(p);
	}

	// ---- the one-hot itself ----
	{
		UByte cells[5];
		NetPatternSetNth(cells, 5, 2, 0xff, 0x11);
		EXPECT(cells[0] == 0x11 && cells[1] == 0x11 && cells[2] == 0xff
			&& cells[3] == 0x11 && cells[4] == 0x11);
		// out of range lights nothing, but still clears
		NetPatternSetNth(cells, 5, 9, 0xff, 0x22);
		for (long i = 0; i < 5; i++)
			EXPECT(cells[i] == 0x22);
		NetPatternSetNth(cells, 5, -1, 0xff, 0x33);
		for (long i = 0; i < 5; i++)
			EXPECT(cells[i] == 0x33);
	}

	// ---- the picture the classifier is shown ----
	{
		NetImagePatternizer* image = (NetImagePatternizer*) multi->fChildren[0];
		EXPECT(image->fType == &NetPatternImageT);
		EXPECT(image->fWidth == 14 && image->fHeight == 14);
		EXPECT(image->fLimited == 1);
		EXPECT(image->fInputs == net->fUnits);			// the first 196 inputs
		// drawn at four times the size, with a pen four sub-pixels
		// across, so the pen is one cell of the grid
		EXPECT(image->fAA->fScale == 4);
		EXPECT(image->fAA->fRec->fWidth == 56 && image->fAA->fRec->fHeight == 56);
		EXPECT(image->fAA->fRec->fDotSize == 4);
		EXPECT(image->fAA->fRec->fRowBytes == 7);

		NetPattern* p = NetPatternCreate((NetPatternizer*) image);
		// a stroke down the left of a tall thin box: the writing is
		// scaled to fill the grid and centred in it
		FPoint line[2];
		line[0].x = F(10);	line[0].y = F(0);
		line[1].x = F(10);	line[1].y = F(30);
		RosStroke* stroke = StrokeCreate(2, line);
		RosStroke* one[1];
		one[0] = stroke;
		RosStrokeList* writing = SLCreate(1, one);

		image->fType->fSLToPat(net, writing, p, 0, F(30), F(30), 0, F(30), F(30), 0, 0, 0);
		const UByte* grid = ((NetImagePattern*) p)->fPixels;
		long ink = 0, most = 0, lit = 0;
		for (long i = 0; i < 14 * 14; i++)
		{
			ink += grid[i];
			if (grid[i] > most)
				most = grid[i];
			if (grid[i] != 0)
				lit++;
		}
		// something was drawn, and a full cell is sixteen sub-pixels
		// at fifteen apiece
		EXPECT(ink > 0);
		EXPECT(most <= 16 * kRenderSubPixelWeight);
		EXPECT(most > 0);
		// a vertical line fills about one column of the fourteen
		EXPECT(lit >= 14 && lit <= 14 * 3);
		// ... and it is drawn down the middle, because the writing is
		// centred
		long leftInk = 0, rightInk = 0;
		for (long r = 0; r < 14; r++)
			for (long c = 0; c < 14; c++)
				(c < 7 ? leftInk : rightInk) += grid[r * 14 + c];
		EXPECT(leftInk > 0 && rightInk > 0);

		// ... and it reaches the net's first 196 inputs
		for (long i = 0; i < 196; i++)
			net->fUnits[i] = 0x5a;
		NetPatternSetInput(p);
		Boolean same = true;
		for (long i = 0; i < 196; i++)
			if (net->fUnits[i] != grid[i])
				same = false;
		EXPECT(same);		// a lit cell is 255 and an unlit one nought, so it copies

		SLDestroy(writing, 1);
		NetPatternDestroy(p);
	}

	// ---- the arctangent, in cycles ----
	{
		// a whole turn is 0x10000; the arguments are (y, x)
		EXPECT(ApproxFixATan2Cycles(0, F(1)) == 0);			// due "east"
		EXPECT(ApproxFixATan2Cycles(0, F(-1)) == -0x8000);	// ... and back
		Fixed quarter = ApproxFixATan2Cycles(F(1), 0);
		EXPECT(quarter > 0x3f00 && quarter < 0x4100);		// a quarter turn
		Fixed eighth = ApproxFixATan2Cycles(F(1), F(1));
		EXPECT(eighth > 0x1f00 && eighth < 0x2100);			// an eighth
		EXPECT(ApproxFixATan2Cycles(F(-1), F(1)) == -eighth);
		// and it stays inside a turn whatever it is given
		for (long i = 0; i < 16; i++)
		{
			Fixed a = ApproxFixATan2Cycles(F(i - 8), F(i * 3 - 20));
			EXPECT(a >= -0x8000 && a <= 0x8000);
		}
	}

	// ---- where the pen went ----
	{
		NetStrokePatternizer* pud = (NetStrokePatternizer*) multi->fChildren[1];
		EXPECT(pud->fType == &NetPatternStrokePUDT);
		EXPECT(pud->fWidth == 20 && pud->fHeight == 9);
		EXPECT(pud->fInputs == net->fUnits + 196);

		NetPattern* p = NetPatternCreate((NetPatternizer*) pud);
		// two strokes: one straight down, then one straight across,
		// with a jump between them
		FPoint down[2], across[2];
		down[0].x = F(0);	down[0].y = F(0);
		down[1].x = F(0);	down[1].y = F(40);
		across[0].x = F(20);	across[0].y = F(0);
		across[1].x = F(60);	across[1].y = F(0);
		RosStroke* two[2];
		two[0] = StrokeCreate(2, down);
		two[1] = StrokeCreate(2, across);
		RosStrokeList* writing = SLCreate(2, two);

		pud->fType->fSLToPat(net, writing, p, 0, F(40), F(60), 0, F(40), F(60), 0, 0, 0);
		const UByte* grid = ((NetStrokePattern*) p)->fCells;

		// every column says which way it was going, spread over two of
		// the eight buckets, and how much of the step the pen was up
		for (long c = 0; c < 20; c++)
		{
			long lit = 0, sum = 0;
			for (long r = 1; r < 9; r++)
				if (grid[r * 20 + c] != 0)
				{
					lit++;
					sum += grid[r * 20 + c];
				}
			EXPECT(lit >= 1 && lit <= 2);
			EXPECT(sum == 0xff);				// the two share 255 between them
		}

		// the jump between the strokes is a run of steps with the pen
		// up, in the middle: the downstroke is forty long, the jump
		// about forty-five and the stroke across forty, so a fifth of
		// the way each
		long first = -1, last = -1, up = 0;
		for (long c = 0; c < 20; c++)
			if (grid[c] > 0x80)
			{
				if (first < 0)
					first = c;
				last = c;
				up++;
			}
		EXPECT(up >= 4 && up <= 9);
		EXPECT(last - first + 1 == up);		// one run, not scattered
		EXPECT(first > 3 && last < 17);		// ... and in the middle

		// ... and the first and last steps are drawn on the paper
		EXPECT(grid[0] < 0x80);
		EXPECT(grid[19] < 0x80);

		// it reaches the net's inputs 196 onwards
		for (long i = 0; i < 180; i++)
			net->fUnits[196 + i] = 0x5a;
		NetPatternSetInput(p);
		Boolean same = true;
		for (long i = 0; i < 180; i++)
			if (net->fUnits[196 + i] != grid[i])
				same = false;
		EXPECT(same);

		SLDestroy(writing, 1);
		NetPatternDestroy(p);
	}

	// ---- and the whole set measures at once ----
	{
		NetPattern* p = NetPatternCreate(all);
		EXPECT(p != nil);
		NetMultiPattern* mp = (NetMultiPattern*) p;
		EXPECT(mp->fCount == 4);
		for (ULong i = 0; i < mp->fCount; i++)
			EXPECT(mp->fChildren[i] != nil);

		// every one of the net's 384 inputs written, from one piece of
		// writing, in one call
		FPoint p2[2];
		p2[0].x = F(0);	p2[0].y = F(0);	p2[1].x = F(20);	p2[1].y = F(30);
		RosStroke* one[1];
		one[0] = StrokeCreate(2, p2);
		RosStrokeList* writing = SLCreate(1, one);
		for (long i = 0; i < net->fInputCount; i++)
			net->fUnits[i] = 0x5a;
		all->fType->fSLToPat(net, writing, p, 0, F(30), F(30), 0, F(30), F(30), 0, 0, F(20));
		NetPatternSetInput(p);
		long touched = 0;
		for (long i = 0; i < net->fInputCount; i++)
			if (net->fUnits[i] != 0x5a)
				touched++;
		EXPECT(touched > 300);			// nearly all of them

		// ... and the classifier answers
		BPNetEvaluate(net);
		long best = 0;
		for (long i = 1; i < net->fOutputCount; i++)
			if (net->fOutputs[i] > net->fOutputs[best])
				best = i;
		EXPECT(net->fOutputs[best] > 0);

		SLDestroy(writing, 1);
		NetPatternDestroy(p);
	}

	NetPatternizerDestroy(all);
	BPNetDestroy(net);
	RSfRcl();

	if (failures == 0)
		printf("test_NetPattern: all passed\n");
	else
		printf("test_NetPattern: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
