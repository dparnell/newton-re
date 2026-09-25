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
typedef void (*RosettaCheckWordsProc)(char** words, UniChar* scores, ULong strokes, ULong count);

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
	Handle			fMainDict;			// +0x04  the lexicon the engine reads against
	unsigned int	fFlags;				// +0x08  what the field expects (below)
	// which characters may be written here: 256 bits, one per code
	unsigned int	fSymbolSet[8];		// +0x0c  (all ones: any of them)
	short			fBase;				// +0x2c  the baseline, when the field says where it is
	short			fBoxLeft;			// +0x2e  and the grid it is written on
	short			fBoxRight;			// +0x30
	short			fBoxTop;			// +0x32
	short			fBoxBottom;			// +0x34
	UByte			fPad36[2];
	Handle			fDicts[5];			// +0x38  the dictionaries named by hand (at most five)
	short			fMap[5][2];			// +0x4c  characters read as other characters (-1: none)
	UByte			fLabel;				// +0x60  what words read here are labelled with
	UByte			fDictCount;			// +0x61  how many of fDicts are in use
	UByte			fSmallHeight;		// +0x62
	UByte			fXSpace;			// +0x63
	UByte			fYSpace;			// +0x64
	UByte			fLetterSpace;		// +0x65  how far apart the letters are written
	UByte			fPad66[2];
};

// What `fFlags` says the field expects.  The low bits are the kinds of
// word the engine may answer; the high ones are how it is written.
const unsigned int	kRosAreaSingleLetters	= 0x00000001;
const unsigned int	kRosAreaNumbers			= 0x00000002;
const unsigned int	kRosAreaPunctuation		= 0x00000004;
const unsigned int	kRosAreaPhone			= 0x00000008;
const unsigned int	kRosAreaDate			= 0x00000010;
const unsigned int	kRosAreaTime			= 0x00000020;
const unsigned int	kRosAreaMoney			= 0x00000040;
const unsigned int	kRosAreaLetters			= 0x00000080;
const unsigned int	kRosAreaUpperCase		= 0x00000100;
const unsigned int	kRosAreaNames			= 0x00000400;
const unsigned int	kRosAreaHasBaseInfo		= 0x00000800;	// fBase and the grid are set
const unsigned int	kRosAreaHasSymbolSet	= 0x00001000;	// fSymbolSet is not simply everything
const unsigned int	kRosAreaCursive			= 0x00002000;
const unsigned int	kRosAreaCapitals		= 0x00004000;
const unsigned int	kRosAreaAddress			= 0x00008000;
const unsigned int	kRosAreaCustom1			= 0x00010000;
const unsigned int	kRosAreaCustom2			= 0x00020000;

// DEVIATION: the ROM's block is 0x68 bytes; ours is bigger, because the
// three Handles in it are twice as wide on a 64-bit host.  Nothing
// outside the engine reads it, so only the two sides agreeing matters -
// and they both say `sizeof`.
const long	kRosettaAreaInfoSize	= (long) sizeof(RosettaAreaInfo);	// the ROM's is 0x68

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
void		RosettaClassifySetup(void);								// ROM 0x001b78d0 RosettaClassifySetup
void		RosettaClassifyAnalyze(struct RosStroke* stroke);		// ROM 0x001b7cc4 RosettaClassifyAnalyze
// The box a boxed character is taken to be written in.
void		RosICBX(struct RosStroke* stroke, FRect* box);			// ROM 0x001b7724 RosICBX
void		RosettaClassifyCleanup(void);							// ROM 0x001b7b10 RosettaClassifyCleanup
// the words the engine read handed to the callback.  This is what the
// word recogniser is given as *its* callback (five arguments, the
// flags array among them); it turns the raw scores into confidences
// out of a thousand and passes four of them up to `gRosCallBack`.
void		RosettaCheckWords(char** words, UniChar* scores, long* flags,
							ULong strokes, ULong count);		// ROM 0x001b7120 RosettaCheckWords

// The one word recogniser (`recognition/WordRecog.h`), made when the
// engine wakes and destroyed when it sleeps; nil while it is asleep.
struct WordRecog;
extern WordRecog*	gWordRecog;						// ROM 0x0c101960 (unnamed)
// How sure the engine was of the last word it read, out of a thousand.
extern short		gRosLastConfidence;				// ROM 0x0c101964 (unnamed)
// The tablet's resolution, and the Newton's own callback.
extern long			gRosResX;						// ROM 0x0c101954 gRosResX
extern long			gRosResY;						// ROM 0x0c101958 gRosResY
extern RosettaCheckWordsProc	gRosCallBack;		// ROM 0x0c10195c gRosCallBack

// Whether the engine below this file is there - it is.  Kept for the
// tests and for a host that wants to know whether it can install
// `TRosRecognizer` rather than an ink-only one.
Boolean		RosettaEngineIsReconstructed(void);

#endif	/* __ROSETTA_H */
