// The handwriting engine's life (recognition/Rosetta.h): waking,
// quietening and sleeping, and the small calls that go with them.
// This is level 2, the join between the Newton's recogniser and
// ParaGraph's engine, over the bigram grammar and the common info the
// ROM brings with it.
#include "Rosetta.h"
#include "CharBox.h"
#include "RosEngine.h"
#include "FixedGeometry.h"
#include "FixedMath.h"
#include "ROMDictionaryData.h"
#include "Segment.h"
#include "RosStrokes.h"
#include "WordRecog.h"
#include "BPNet.h"
#include "NewtErrors.h"
#include "memory/host/KernelHeap.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
#define EXPECT(cond) do { if (!(cond)) { failures++; fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)

static Fixed	F(long n)		{ return (Fixed) (int) ((unsigned int) n << 16); }

static long		gWordsHandedBack;

static void
TestCheckWords(char** /*words*/, UniChar* /*scores*/, ULong /*strokes*/, ULong /*count*/)
{
	gWordsHandedBack++;
}


int
main()
{
	InitHostStandaloneHeap();

	// ---- the grammar the ROM brings ----
	{
		EXPECT(ROMGrammar.fCount == 8);
		static const char* const kNames[8] = {
			"General", "Date", "Numbers&Money", "Numbers",
			"Phone", "Time", "Money", "PostalCode"
		};
		for (long i = 0; i < 8; i++)
			EXPECT(strcmp(ROMGrammar.fContexts[i]->fName, kNames[i]) == 0);

		// a grammar is a list of *kinds of word*, each a lexicon out of
		// gROMDictionaryData with a score of its own
		const BiGrammar* phone = ROMGrammar.fContexts[4];
		EXPECT(phone->fCount == 5 && phone->fCapacity == 5);
		EXPECT(strcmp(phone->fSlices[0]->fName, "PhoneR_US_C") == 0);
		EXPECT(phone->fSlices[0]->fDictionary == 15);	// gLex8phone
		EXPECT(strcmp(phone->fSlices[1]->fName, "hyphen") == 0);
		EXPECT(phone->fSlices[1]->fDictionary == 113);	// gLex8hyphen
		// ... and a score for each kind that may follow it: a telephone
		// number may be followed by a hyphen and by nothing else
		EXPECT(phone->fSlices[0]->fNextCount == 1);
		EXPECT(phone->fSlices[0]->fNext[0] == phone->fSlices[1]);
		EXPECT(phone->fSlices[0]->fWeights[0] == 458);
		// and after the hyphen, three things, of which going back to
		// the number costs nothing at all
		EXPECT(phone->fSlices[1]->fNextCount == 3);
		EXPECT(phone->fSlices[1]->fNext[0] == phone->fSlices[0]);
		EXPECT(phone->fSlices[1]->fWeights[0] == 0);

		// a kind of word that can never start one has the worst score
		// there is
		const BiGrammar* general = ROMGrammar.fContexts[0];
		EXPECT(general->fCount == 25);
		EXPECT(strcmp(general->fSlices[1]->fName, "endpunct") == 0);
		EXPECT(general->fSlices[1]->fScore == 0x7ffe);
		// the six an area fills in with dictionaries of its own
		EXPECT(strcmp(general->fSlices[7]->fName, "~user") == 0);
		EXPECT(strcmp(general->fSlices[12]->fName, "~null5") == 0);
	}

	// ---- the engine wakes ----
	{
		EXPECT(RosettaInitialize(F(72), F(72), TestCheckWords) == noErr);
		EXPECT(gWordRecog != nil);
		EXPECT(gRosResX == F(72) && gRosResY == F(72));
		EXPECT(gRosCallBack == TestCheckWords);
		// the common info out of the ROM
		EXPECT(RosCI != nil && RosCI->fMinStrokeSize == F(4) + F(1) / 2);
		// ten readings, the ROM's grammar, and its first context found
		// by the name "General"
		EXPECT(gWordRecog->fWordCount == 10);
		EXPECT(gWordRecog->fGrammars == &ROMGrammar);
		EXPECT(gWordRecog->fContextIndex == 0);
		EXPECT(strcmp(gWordRecog->fContext->fName, "General") == 0);
		// the tablet's resolution, in whole dots to the inch
		EXPECT(gWordRecog->fResX == 72 && gWordRecog->fResY == 72);
		EXPECT(gWordRecog->fClassifyMode == kRosettaClassifyNormally);
		EXPECT(gWordRecog->fOwnsStrokes == 1);
		// waking again is nothing
		EXPECT(RosettaAwaken() == noErr);

		// a field asks for a grammar by name
		EXPECT(WordRecogSetContext(gWordRecog, "Phone") == kWordRecogOk);
		EXPECT(gWordRecog->fContextIndex == 4);
		EXPECT(WordRecogSetContext(gWordRecog, "Esperanto") == 1);
		EXPECT(gWordRecog->fContextIndex == 4);
	}

	// ---- the classifier the engine reads with ----
	{
		// it is made by the recogniser's own Initialize, over the ROM's
		// template
		BPNet* net = gWordRecog->fNet;
		EXPECT(net != nil);
		EXPECT(net->fInputCount == 384);
		EXPECT(net->fHiddenCount == 484);
		EXPECT(net->fOutputCount == 134);		// one per character class
		EXPECT(net->fUnitCount == 1002);
		EXPECT(net->fComputedCount == 618);
		EXPECT(net->fUnitCount == net->fInputCount + net->fComputedCount);
		EXPECT(net->fComputedCount == net->fHiddenCount + net->fOutputCount);
		// the trained tables, straight out of the ROM
		EXPECT(net->fWeights == bpWeight);
		EXPECT(net->fConnects == newtConnects);
		EXPECT(net->fWeightSize == 91124);
		EXPECT(net->fLearning == 0);			// RosettaAwaken turns it off
		// the unit array, with the outputs at the end of it
		EXPECT(net->fUnits != nil && net->fUnits == net->fBlock);
		EXPECT(net->fOutputs == net->fUnits + 868);

		// the connection program: 618 units, each ended by a word whose
		// count is nought, and one more to say that was the last
		long boundaries = 0, last = -1;
		for (long i = 0; i < 2392; i++)
			if ((newtConnects[i] >> kBPNetCountShift) == 0)
			{
				boundaries++;
				if (newtConnects[i] & kBPNetNextRegister)
					last = i;
			}
		EXPECT(boundaries == net->fComputedCount + 1);
		EXPECT(last == 2391);					// and it is the very last word

		// the sigmoid: 128 for a sum of nought, never falling, and 255
		// by the time the sum is clamped
		EXPECT(QSigLu[0] == 128);
		EXPECT(QSigLu[kBPNetSigmoidLimit >> kBPNetSigmoidShift] == 255);
		for (long i = 1; i < 360; i++)
			if (QSigLu[i] < QSigLu[i - 1])
			{
				EXPECT(false);
				break;
			}

		// ---- and the net run ----
		// The connection program walked here, the same way
		// `BPNetEvaluate` walks it but counting rather than adding, so
		// that the three numbers the ROM itself writes down can be
		// checked: how many units it works out, how many connections
		// they come to, and that the weights run out exactly at the end
		// of the table.
		{
			const ULong* c = newtConnects;
			long units = 0, connections = 0, cursor = -4, lastWeight = -1;
			long at = net->fInputCount;
			ULong word = *c++;
			for (;;)
			{
				long count;
				for (;;)
				{
					word = *c++;
					count = (long) (((int) word) >> kBPNetCountShift);
					if (count >= 0)
						break;
					if ((word & (kBPNetNextWeights | kBPNetNextRegister)) != 0)
						cursor += 4;
					long first = (at - (long) (word & 0xffff)) & 3;
					long total = 4 - count;
					connections += total;
					if (cursor + first + total - 1 > lastWeight)
						lastWeight = cursor + first + total - 1;
					cursor += 4 * ((first + total - 1) / 4);
				}
				units++;
				at++;
				if ((word & kBPNetNextRegister) != 0)
					break;
			}
			EXPECT(units == net->fComputedCount);				// 618
			EXPECT(connections == (long) net->fConnectionCount);	// 90540
			EXPECT(lastWeight == (long) net->fWeightSize - 1);	// the last byte
		}

		// every input at 128, which is the engine's nought, and the net
		// run over them
		for (long i = 0; i < net->fInputCount; i++)
			net->fUnits[i] = 128;
		BPNetEvaluate(net);
		// something came out, and it is not all one value
		Boolean varies = false;
		for (long i = 1; i < net->fOutputCount; i++)
			if (net->fOutputs[i] != net->fOutputs[0])
				varies = true;
		EXPECT(varies);
		// ... and it is the same every time
		UByte first = net->fOutputs[0];
		long sum = 0;
		for (long i = 0; i < net->fOutputCount; i++)
			sum += net->fOutputs[i];
		BPNetEvaluate(net);
		long again = 0;
		for (long i = 0; i < net->fOutputCount; i++)
			again += net->fOutputs[i];
		EXPECT(net->fOutputs[0] == first && again == sum);
	}

	// ---- the engine's own character set ----
	{
		EXPECT(RosettaVerifyWordSymbols((char*) "hello") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "Hello,") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "$1,000.") == true);
		EXPECT(RosettaVerifyWordSymbols((char*) "") == true);
		// a control character is not one the engine may answer
		char bad[3];
		bad[0] = 'a';	bad[1] = 0x01;	bad[2] = 0;
		EXPECT(RosettaVerifyWordSymbols(bad) == false);
		// ... and neither is a space: the engine reads one word at a
		// time and a space is never a character of one
		EXPECT(RosettaVerifyWordSymbols((char*) "two words") == false);
	}

	// ---- what it is told to stop doing ----
	{
		gWordRecog->fFlags1f4 = 0xffffffff;
		RosettaDontClassify(kRosettaBaselineOnly);
		EXPECT(gWordRecog->fClassifyMode == kRosettaBaselineOnly);
		EXPECT(gWordRecog->fFlags1f4 == 0xffdfd7fe);
	}

	// ---- the baseline, as the two Points the recogniser wants ----
	{
		SetFixedRect(&gWordRecog->fBaseline, F(3), F(5), F(40), F(26));
		Point ends[2];
		EXPECT(RosettaGetBaseLine(ends) == noErr);
		EXPECT(ends[0].h == 3 && ends[0].v == 5);
		EXPECT(ends[1].h == 40 && ends[1].v == 26);
	}

	// ---- the working values put back ----
	{
		gWordRecog->fCharBox = CharBoxStateNew();
		gWordRecog->fField202 = 1;
		gWordRecog->fField203 = 1;
		gWordRecog->fField1e0 = 0;
		EXPECT(RosettaInitializeValues() == noErr);
		EXPECT(gWordRecog->fClassifyMode == kRosettaClassifyNormally);
		EXPECT(gWordRecog->fCharBox == nil);
		EXPECT(gWordRecog->fField202 == 0 && gWordRecog->fField203 == 0);
		EXPECT(gWordRecog->fField1e0 == -1);

		// ... and a word read well enough puts the run back as it was
		// saved, where one read badly leaves it as it has drifted
		gRosLastConfidence = 1000;
		gWordRecog->fSavedRun[0] = F(11);
		gWordRecog->fRun[0] = F(99);
		RosettaInitializeValues();
		EXPECT(gWordRecog->fRun[0] == F(11));
		gRosLastConfidence = 500;
		gWordRecog->fRun[0] = F(99);
		RosettaInitializeValues();
		EXPECT(gWordRecog->fRun[0] == F(99));
	}

	// ---- a grammar built for one field ----
	{
		// `RosettaSetArea` is what a field's configuration becomes.  The
		// engine is awake here, holding the ROM's own General grammar.
		const BiGrammar* general = gWordRecog->fGrammars->fContexts[0];

		RosettaAreaInfo area;
		memset(&area, 0, sizeof(area));
		for (long i = 0; i < 8; i++)
			area.fSymbolSet[i] = 0xffffffff;
		for (long i = 0; i < 5; i++)
		{
			area.fMap[i][0] = -1;
			area.fMap[i][1] = -1;
		}
		// the area carries `9 - n` for the slider setting n, so four is
		// the writer of ordinary habits
		area.fLetterSpace = 4;
		area.fFlags = kRosAreaLetters;

		EXPECT(RosettaSetArea(&area) == noErr);
		// a field of ordinary letters takes the General grammar, and
		// what the engine holds is its own copy of it, narrowed
		EXPECT(gWordRecog->fContextIndex == -1);
		EXPECT(gWordRecog->fContext != general);
		// the clone is nameless, because `BiGrammarCreate` drops the
		// name it is handed
		EXPECT(gWordRecog->fContext->fName == nil);
		// ... and it has every kind of word the General grammar has,
		// because the narrowing reprices them rather than removing them
		EXPECT(gWordRecog->fContext->fCount == general->fCount);
		// the spacing was set from the field
		EXPECT(gSegWordSpacing == F(1));

		// every kind's dictionary is now the data itself rather than an
		// index into gROMDictionaryData
		long changed = 0;
		for (long i = 0; i < gWordRecog->fContext->fCount; i++)
		{
			const BiGSlice* slice = gWordRecog->fContext->fSlices[i];
			const BiGSlice* from = general->fSlices[i];
			EXPECT(strcmp(slice->fName, from->fName) == 0);
			if (from->fDictionary < kROMDictionaryCount)
			{
				EXPECT(slice->fDictionary
					== (ULong) gROMDictionaryData[from->fDictionary]);
				changed++;
			}
		}
		EXPECT(changed == general->fCount);

		// the scores were repriced: the likeliest kind of word in the
		// field now costs nothing, and none costs more than never
		long best = 0x7ffe, worst = 0, never = 0;
		for (long i = 0; i < gWordRecog->fContext->fCount; i++)
		{
			long score = gWordRecog->fContext->fSlices[i]->fScore;
			EXPECT(score >= 0 && score <= 0x7ffe);
			if (score == 0x7ffe)
				never++;
			else
			{
				if (score < best)	best = score;
				if (score > worst)	worst = score;
			}
		}
		// the whole arithmetic pinned: decode each score to a
		// probability, share nine tenths of it among the kinds the
		// field wants, encode again, and bring the best down to nought
		EXPECT(gWordRecog->fContext->fCount == 25);
		EXPECT(best == 0);				// the likeliest kind is free
		EXPECT(worst == 2847);			// and the dearest costs 5.7 nats
		EXPECT(never == 3);				// three the field will not have

		// the dictionaries the field named, in the six slots
		EXPECT(gWordRecog->fDicts[0] == area.fMainDict);
		for (long i = 0; i < 5; i++)
			EXPECT(gWordRecog->fDicts[i + 1] == area.fDicts[i]);
	}

	// ---- a field with a grammar of its own ----
	{
		RosettaAreaInfo area;
		memset(&area, 0, sizeof(area));
		for (long i = 0; i < 8; i++)
			area.fSymbolSet[i] = 0xffffffff;
		for (long i = 0; i < 5; i++)
		{
			area.fMap[i][0] = -1;
			area.fMap[i][1] = -1;
		}
		area.fLetterSpace = 5;
		area.fFlags = kRosAreaPhone;

		EXPECT(RosettaSetArea(&area) == noErr);
		// the Phone grammar, index 1, cloned - so the index is recorded
		// as -(1+1) and the engine owns what it holds
		EXPECT(gWordRecog->fContextIndex == -2);
		const BiGrammar* phone = gWordRecog->fGrammars->fContexts[1];
		EXPECT(gWordRecog->fContext != phone);
		EXPECT(gWordRecog->fContext->fCount == phone->fCount);
		// a clone is scored exactly as its original: only the General
		// grammar is repriced
		for (long i = 0; i < phone->fCount; i++)
		{
			EXPECT(gWordRecog->fContext->fSlices[i]->fScore
				== phone->fSlices[i]->fScore);
			EXPECT(gWordRecog->fContext->fSlices[i]->fNextCount
				== phone->fSlices[i]->fNextCount);
		}
		// ... and its transitions point at its own kinds, not the
		// ROM's
		for (long i = 0; i < phone->fCount; i++)
		{
			const BiGSlice* copy = gWordRecog->fContext->fSlices[i];
			for (long j = 0; j < copy->fNextCount; j++)
			{
				EXPECT(copy->fNext[j] != phone->fSlices[i]->fNext[j]);
				EXPECT(copy->fWeights[j] == phone->fSlices[i]->fWeights[j]);
				// the target really is one of the clone's own
				Boolean found = false;
				for (long k = 0; k < copy->fNextCount + phone->fCount; k++)
					if (k < gWordRecog->fContext->fCount
						&& gWordRecog->fContext->fSlices[k] == copy->fNext[j])
						found = true;
				EXPECT(found);
			}
		}
		// a named grammar leaves the six dictionary slots empty
		for (long i = 0; i < 6; i++)
			EXPECT(gWordRecog->fDicts[i] == nil);
	}

	// ---- the characters a field will have ----
	{
		RosettaAreaInfo area;
		memset(&area, 0, sizeof(area));
		for (long i = 0; i < 5; i++)
		{
			area.fMap[i][0] = -1;
			area.fMap[i][1] = -1;
		}
		area.fLetterSpace = 5;
		// only the codes in the bottom word, and only say so
		area.fFlags = kRosAreaLetters | kRosAreaHasSymbolSet;
		area.fSymbolSet[0] = 0x0000ffff;

		const ULong* before = RosCI->fLegalUse;
		EXPECT(before == rosCharLegalUse);		// the ROM's own, to start
		EXPECT(RosettaSetArea(&area) == noErr);
		// a set of its own now, the ROM's narrowed by the field's
		EXPECT(RosCI->fLegalUse != rosCharLegalUse);
		for (long i = 0; i < 8; i++)
			EXPECT(RosCI->fLegalUse[i] == (rosCharLegalUse[i] & area.fSymbolSet[i]));
		EXPECT(RosCI->fLegalUse[1] == 0);		// nothing above code 31

		// and a field that says nothing about it gets the ROM's own
		// back, the one it made given away
		area.fFlags = kRosAreaLetters;
		EXPECT(RosettaSetArea(&area) == noErr);
		EXPECT(RosCI->fLegalUse == rosCharLegalUse);
	}

	// ---- where the field says the writing goes ----
	{
		RosettaAreaInfo area;
		memset(&area, 0, sizeof(area));
		for (long i = 0; i < 8; i++)
			area.fSymbolSet[i] = 0xffffffff;
		for (long i = 0; i < 5; i++)
		{
			area.fMap[i][0] = -1;
			area.fMap[i][1] = -1;
		}
		area.fLetterSpace = 5;
		area.fFlags = kRosAreaLetters;
		area.fBase = 200;
		area.fBoxLeft = 10;	area.fBoxRight = 300;
		area.fBoxTop = 150;	area.fBoxBottom = 210;
		area.fSmallHeight = 9;
		area.fXSpace = 4;	area.fYSpace = 6;

		// without the flag none of it is looked at
		gWordRecog->fBase = 0;
		EXPECT(RosettaSetArea(&area) == noErr);
		EXPECT(gWordRecog->fBase == 0);

		area.fFlags |= kRosAreaHasBaseInfo;
		EXPECT(RosettaSetArea(&area) == noErr);
		EXPECT(gWordRecog->fBase == 200);
		EXPECT(gWordRecog->fBoxLeft == 10 && gWordRecog->fBoxRight == 300);
		EXPECT(gWordRecog->fBoxTop == 150 && gWordRecog->fBoxBottom == 210);
		EXPECT(gWordRecog->fXSpace == 4 && gWordRecog->fYSpace == 6);
		// the small height is never less than halfway between itself
		// and eleven, so a writer of small letters is still allowed
		// something to work with
		EXPECT(gWordRecog->fSmallHeight == 10);		// (9 + 11) / 2
		area.fSmallHeight = 30;
		EXPECT(RosettaSetArea(&area) == noErr);
		EXPECT(gWordRecog->fSmallHeight == 30);		// (30 + 11) / 2 is less
	}

	// ---- the classifier's answer leaned on by geometry ----
	{
		// `CharModifyProbs` is what knows that a letter sitting below
		// the line is more likely to be a `g` than a `q`, and that a
		// full-height mark in a word of small round ones is an `l`.
		// Two of its four adjustments are switched off in the shipped
		// ROM: the weights are nought.
		EXPECT(RosCI->fStrokeCountWeight == 0);		// and its table is nil
		EXPECT(RosCI->fCharStrokeProbs == nil);
		EXPECT(RosCI->fShapeWeight == 0);
		// what is left
		EXPECT(RosCI->fCapCaseWeight == 0x3333);	// a fifth
		EXPECT(RosCI->fHeightSpread == 0x428f);		// 0.26
		EXPECT(RosCI->fFragmentWeight == F(1));		// so, nothing

		// the height model is real trained data: an `l` is written as
		// tall as the word, an `o` about half, a full stop a seventh,
		// and a `g` taller than the word because of its descender
		EXPECT(CharHeight['l' * 2] > F(1) && CharHeight['l' * 2] < F(1) + 0x4000);
		EXPECT(CharHeight['o' * 2] > 0x8000 && CharHeight['o' * 2] < 0x9000);
		EXPECT(CharHeight['g' * 2] > CharHeight['l' * 2]);
		EXPECT(CharHeight['.' * 2] < 0x3000);
		// and each has a spread, without which it is not used at all
		EXPECT(CharHeight['l' * 2 + 1] > 0);

		FRect box;
		Fixed probs[256];
		Fixed scratch[256];

		// a piece of writing as tall as the word: `l` should come out
		// ahead of `o`, although the classifier liked them the same
		SetFixedRect(&box, F(0), F(0), F(8), F(20));	// 21 tall
		for (long i = 0; i < 256; i++)
			probs[i] = 0;
		probs['l'] = 0x8000;
		probs['o'] = 0x8000;
		CharModifyProbs(&box, 1, 0, 0, 0, F(20),
					0, 0, 0, 0, F(20), F(20), scratch,
					F(20), F(21), F(9), probs);
		EXPECT(probs['l'] > probs['o']);
		EXPECT(probs['l'] > 0);
		// the scratch array says how well each one fits on its own
		EXPECT(scratch['l'] > scratch['o']);

		// and half as tall: the other way about
		SetFixedRect(&box, F(0), F(0), F(8), F(10));	// 11 tall
		for (long i = 0; i < 256; i++)
			probs[i] = 0;
		probs['l'] = 0x8000;
		probs['o'] = 0x8000;
		CharModifyProbs(&box, 1, 0, 0, 0, F(20),
					0, 0, 0, 0, F(20), F(20), scratch,
					F(20), F(21), F(9), probs);
		EXPECT(probs['o'] > probs['l']);

		// it really is a Gaussian: a piece exactly at a character's
		// mean height fits perfectly, and one a spread away fits by
		// exp(-1/2)
		SetFixedRect(&box, F(0), F(0), F(8),
					FixedMultiply(CharHeight['o' * 2], F(20)) - F(1));
		for (long i = 0; i < 256; i++)
			probs[i] = 0;
		probs['o'] = 0x8000;
		CharModifyProbs(&box, 1, 0, 0, 0, F(20),
					0, 0, 0, 0, F(20), F(20), scratch,
					F(20), F(21), F(9), probs);
		EXPECT(scratch['o'] > 0xfe00);			// as near one as makes no odds

		// only the ten best survive: everything else is set to nothing
		for (long i = 0; i < 256; i++)
			probs[i] = 0;
		long offered = 0;
		for (long c = 'a'; c <= 'z'; c++)
			if ((RosCI->fLegalUse[c >> 5] & (1UL << (c & 31))) != 0)
			{
				probs[c] = (Fixed) (0x4000 + c * 4);
				offered++;
			}
		EXPECT(offered > 10);
		SetFixedRect(&box, F(0), F(0), F(8), F(10));
		CharModifyProbs(&box, 1, 0, 0, 0, F(20),
					0, 0, 0, 0, F(20), F(20), scratch,
					F(20), F(21), F(9), probs);
		long survivors = 0;
		for (long i = 0; i < 256; i++)
			if (probs[i] != 0)
				survivors++;
		EXPECT(survivors <= 10);
		EXPECT(survivors > 0);
	}

	// ---- quiet, and asleep ----
	{
		// quietened: the arrays given back, the block left standing
		EXPECT(RosettaQuiesce() == noErr);
		EXPECT(gWordRecog != nil && gWordRecog->fSuspended == 1);
		EXPECT(gWordRecog->fStrokes == nil && gWordRecog->fWords == nil);
		// ... and made again by the word recogniser's own resume
		EXPECT(WordRecogResume(gWordRecog) == kWordRecogOk);
		EXPECT(gWordRecog->fStrokes != nil);

		// and taken down, the common info with it
		EXPECT(RosettaSleep() == noErr);
		EXPECT(gWordRecog == nil);
		EXPECT(RosCI == nil);
		// sleeping again is nothing
		EXPECT(RosettaSleep() == noErr);
	}

	EXPECT(gWordsHandedBack == 0);		// nothing was ever read
	// ---- a grammar and its slices, made by hand ----
	{
		// Both keep their arrays behind the struct in the same block,
		// which is why each is one allocation and one DisposPtr.
		BiGrammar* g = BiGrammarNew(4);
		EXPECT(g != nil);
		EXPECT(g->fName == nil);
		EXPECT(g->fCount == 0 && g->fCapacity == 4);
		// the slice pointers start at the byte after the header
		// (the arrays sit behind the struct; on the host that is
		//  `sizeof` rather than the ROM's 0x20, a host pointer being
		//  twice as wide)
		EXPECT((const void*) g->fSlices == (const void*) (g + 1));
		EXPECT(g->fField14 == 0 && g->fField15 == 0);

		// the name argument is accepted and never stored
		BiGrammar* named = BiGrammarCreate("Postcodes", 2);
		EXPECT(named->fName == nil);
		EXPECT(named->fCapacity == 2);

		// a slice with room for three kinds of word after it: the three
		// pointers, then the three scores, then nothing
		BiGSlice* s = BiGSliceNew(3);
		EXPECT(s->fName == nil);
		EXPECT(s->fNextCount == 0);			// nothing follows it yet
		EXPECT(s->fNextCapacity == 3);		// ... but there is room
		EXPECT((const void*) s->fNext == (const void*) (s + 1));
		EXPECT((const void*) s->fWeights == (const void*) (s->fNext + 3));
		EXPECT(s->fField2c == 0xff);

		// a slice nothing may follow has nil for both rather than a
		// pointer past its own end
		BiGSlice* leaf = BiGSliceNew(0);
		EXPECT(leaf->fNext == nil && leaf->fWeights == nil);
		EXPECT(leaf->fNextCapacity == 0);

		// the grammar takes them and gives them back with itself
		const BiGSlice** slices = (const BiGSlice**) g->fSlices;
		slices[0] = s;
		slices[1] = leaf;
		g->fCount = 2;
		BiGrammarDestroy(g);
		BiGrammarDestroy(named);
		BiGrammarDestroy(nil);				// no trouble

		// the ROM's own are laid out the same way, and full: every one
		// of their slices has as many kinds following it as it has room
		// for, which is what tells the two counts apart
		long sliceCount = 0, def = 0, lexical = 0, wordlike = 0;
		for (long i = 0; i < ROMGrammar.fCount; i++)
		{
			const BiGrammar* rom = ROMGrammar.fContexts[i];
			EXPECT(rom->fCount == rom->fCapacity);
			for (long k = 0; k < rom->fCount; k++)
			{
				const BiGSlice* slice = rom->fSlices[k];
				EXPECT(slice->fNextCount == slice->fNextCapacity);
				sliceCount++;
				// `BiGSliceNew` sets 0xff, so the other two values mean
				// something: every kind named `LexicalSymbols` carries
				// nought, and `wordlike` carries one
				if (slice->fField2c == 0xff)
					def++;
				else if (slice->fField2c == 0)
				{
					lexical++;
					EXPECT(strncmp(slice->fName, "LexicalSymbols", 14) == 0);
				}
				else
				{
					wordlike++;
					EXPECT(slice->fField2c == 1);
					EXPECT(strcmp(slice->fName, "wordlike") == 0);
				}
			}
		}
		EXPECT(sliceCount == 46 && def == 36 && lexical == 9 && wordlike == 1);
	}

	if (failures == 0)
		printf("test_Rosetta: all passed\n");
	else
		printf("test_Rosetta: %d failures\n", failures);
	return failures == 0 ? 0 : 1;
}
