/*
	File:		recognition/Rosetta.h

	Contains:	The seam between the Newton's recognition system and the
				handwriting engine it was shipped with.

				The MP2x00's engine is ParaGraph's Calligrapher, which
				Apple licensed and shipped as **Rosetta** - the print and
				cursive recogniser of NewtonOS 2.x.  It is a subsystem in
				its own right: about two hundred kilobytes of code in
				some three hundred and fifty functions, with trained
				tables beside it.  Its layers, from the top:

				    TRosRecognizer     the TWRecognizer implementation
				                       (recognition/RosRecognizer.h)
				  > Rosetta*           this file: the fifteen calls the
				                       recogniser makes into the engine
				    WordRecog*         the word recogniser
				    CharBox*           the boxed-character recogniser
				    SL*                the stroke lists they work on
				    low_type/EXTR/...  the feature extraction and the
				                       classifier nets

				Everything above this file is the Newton's; everything
				below it is ParaGraph's, and the names below are theirs
				(`neibour_susp_extr`, `is_umlyut`, `Errorprov`: the code
				was written in Moscow).  That is why the seam is worth
				drawing here explicitly - it is where a modern
				recogniser would be put in instead.

	NOT YET RECONSTRUCTED: the engine below this line.  Every call here
	answers the ROM's own "could not" value, so a TRosRecognizer built
	on it makes no words; `TInkOnlyRecognizer` remains the engine the
	host installs.  See `docs/recognition/README.md`.

	Reconstructed from the MP2x00 US ROM (0x001b7120-0x001b8500); the
	entry points cite their origin, the bodies are not there yet.
*/

#ifndef __ROSETTA_H
#define __ROSETTA_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#ifndef __NEWTONTYPES_H
#include "NewtonTypes.h"
#endif

// What the engine is given a word by: the recogniser hands it this and
// the engine calls back with what it read.  `strokes` is how many
// strokes of the group the words cover, `count` how many readings there
// are, `words` the readings themselves and `scores` their confidences.
typedef void (*RosettaCheckWordsProc)(char** words, ULong strokes, ULong count, UniChar* scores);

// The area the engine is writing in: 0x68 bytes the recogniser fills in
// from a recognition configuration and hands over with RosettaSetArea.
// Both sides read it at fixed byte offsets, so the words are 32 bits
// whatever the host's `long` is.  Only what TRosRecognizer itself
// touches is named; the rest is the engine's, and is kept here so the
// layout is exactly the ROM's.
struct RosettaAreaInfo
{
	UByte			fField00;			// +0x00
	UByte			fField01;			// +0x01
	UByte			fPad02[2];
	unsigned int	fField04;			// +0x04
	unsigned int	fFlags;				// +0x08  0x2000: measure the baseline and read nothing
	int				fLexicons[8];		// +0x0c  which word lists to read against (-1: every one)
	UByte			fField2c[6];		// +0x2c
	UByte			fPad32[6];
	unsigned int	fField38;			// +0x38
	unsigned int	fField3c;			// +0x3c
	unsigned int	fField40;			// +0x40
	unsigned int	fField44;			// +0x44
	unsigned int	fField48;			// +0x48
	UByte			fShapes[5][4];		// +0x4c  the stroke shapes it may expect (0xff: any)
	UByte			fLabel;				// +0x60  what words read here are labelled with
	UByte			fField61;			// +0x61
	UByte			fField62;			// +0x62
	UByte			fField63;			// +0x63
	UByte			fField64;			// +0x64
	UByte			fStrokesExpected;	// +0x65  how many strokes are still to come
	UByte			fPad66[2];
};

const long	kRosettaAreaInfoSize	= 0x68;

// `RosettaDontClassify`'s argument: what the engine is to stop doing.
const ULong	kRosettaClassifyNormally	= 0;
const ULong	kRosettaNoClassify			= 1;	// group the strokes but read nothing
const ULong	kRosettaBaselineOnly		= 2;	// only work the baseline out

// the engine started, with the tablet's resolution and the callback it
// answers words through
NewtonErr	RosettaInitialize(long xScale, long yScale, RosettaCheckWordsProc proc);	// ROM 0x001b810c RosettaInitialize
// a stroke offered; nought strokes ends the word and makes the engine
// read what it has
NewtonErr	RosettaClassify(ULong count, FPoint* points, ULong startTime, ULong endTime);	// ROM 0x001b7ff4 RosettaClassify
// the engine's working values put back as they start
NewtonErr	RosettaInitializeValues(void);							// ROM 0x001b83f4 RosettaInitializeValues
// which of its work the engine is to leave out until told otherwise
void		RosettaDontClassify(ULong what);						// ROM 0x001b8478 RosettaDontClassify
// the area block handed over
NewtonErr	RosettaSetArea(RosettaAreaInfo* areaInfo);				// ROM 0x001b7254 RosettaSetArea
// the baseline and the x-height of what was read, as four Points
NewtonErr	RosettaGetBaseLine(Point* out);							// ROM 0x001b84c4 RosettaGetBaseLine
// whether the engine knows every character of a word
Boolean		RosettaVerifyWordSymbols(char* word);					// ROM 0x001b8134 RosettaVerifyWordSymbols
// what it has gathered about the sentence so far thrown away
void		RosettaClearSentence(void);								// ROM 0x001b8184 RosettaClearSentence
NewtonErr	RosettaAwaken(void);									// ROM 0x001b819c RosettaAwaken
NewtonErr	RosettaQuiesce(void);									// ROM 0x001b8304 RosettaQuiesce
NewtonErr	RosettaSleep(void);										// ROM 0x001b8388 RosettaSleep
// the three the engine's own classify pass is made of
NewtonErr	RosettaClassifySetup(void);								// ROM 0x001b78d0 RosettaClassifySetup
NewtonErr	RosettaClassifyAnalyze(void);							// ROM 0x001b7cc4 RosettaClassifyAnalyze
NewtonErr	RosettaClassifyCleanup(void);							// ROM 0x001b7b10 RosettaClassifyCleanup
// the words the engine read handed to the callback
void		RosettaCheckWords(void);								// ROM 0x001b7120 RosettaCheckWords

// Whether the engine below this file is there.  Nothing but the tests
// and the documentation should need to ask: the recogniser throws
// `evt.ex.abt` when an engine call fails, which is what the ROM does
// when its own engine fails, and the recognition system puts a
// recogniser that throws to sleep.
Boolean		RosettaEngineIsReconstructed(void);

#endif	/* __ROSETTA_H */
