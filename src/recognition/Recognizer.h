/*
	File:		recognition/Recognizer.h

	Contains:	TRecognizer, what the application side knows of a recogniser:
				the domain that makes its units, the unit type (its id), the
				command the view system dispatches for a unit of it (aeClick
				for 'CLIK', aeStroke for 'STRK', aeTap/aeDoubleTap for the
				click events, aeGesture, aeWord...), a priority, its flags
				(8: a unit's bounds are its stroke's; 2: arbitrated with the
				others), the arbitration time, and the services it can and
				does provide as viewFlags recognition bits (HandleUnit hands
				a unit to the view: ==> the command to send, 0 for none).
				TRecognizerList is the list of them, found by id.

				TRecognitionManager (gRecognition) is the recognition
				system's front: the level it was started at (0: none, 1:
				clicks and strokes, 2: shapes and words too), the stroke
				world, the controller, the arbiter, the areas, the
				recognisers, the clicks to ignore until a time, the modal
				bounds, and the views clicked last.  The ROM's TRecognizer is
				0x20 bytes, TRecognitionManager 0x40.

				NOT YET RECONSTRUCTED: TRecognitionManager::Init's stroke
				world, controller, arbiter and areas (StrokeCentral,
				TController, TArbiter, InitAreas), the gesture, stroke, shape
				and word recognisers (their domains), the recognisers'
				EnableArea (TRecArea::AddAType) and the click recogniser's
				HandleUnit (the area cache: OtherViewInUse, ClicksOnlyArea);
				the host installs the click and click-event recognisers and
				the root domain.

	Reconstructed from the MP2100 D ROM (0x00145308-0x001460a0,
	0x001a0140-0x001a0260, 0x0019f6f4-0x0019f960, 0x001a03e0-0x001a0680);
	each function cites its origin.
*/

#ifndef __RECOGNIZER_H
#define __RECOGNIZER_H

#include "Unit.h"
#include "Domain.h"

class TView;
class TUnitPublic;
class TRecognizerList;
class TController;
class TArbiter;
class StrokeCentral;

// the recogniser flags
enum
{
	kRecognizerStrokeBounds	= 8,		// a unit's bounds are its stroke's
	kRecognizerArbitrated	= 2			// competes in arbitration
};

class TRecognizer
{
public:
						TRecognizer();							// ROM 0x00145308 __ct__11TRecognizerFv
	virtual void		Init(TDomain* domain, ULong id, ULong command, UChar flags, ULong arbitrateTime);	// ROM 0x00145488 Init__11TRecognizerFP7TDomainUlT2UcT2 (+0x00)
	virtual void		InitServices(ULong possible, ULong enabled);	// ROM 0x0014535c InitServices__11TRecognizerFUlT1 (+0x04)
	virtual TDomain*	Domain(void);							// ROM 0x00145938 Domain__11TRecognizerFv (+0x08)
	virtual ULong		ID(void);								// ROM 0x00145f40 ID__11TRecognizerFv (+0x0c)
	virtual ULong		Command(void);							// ROM 0x0014604c Command__11TRecognizerFv (+0x10)
	virtual ULong		Flags(void);							// ROM 0x00146054 Flags__11TRecognizerFv (+0x14)
	virtual Boolean		TestFlags(UChar flags);					// ROM 0x0014605c TestFlags__11TRecognizerFUc (+0x18)
	virtual ULong		ServicesPossible(void);					// ROM 0x0014607c ServicesPossible__11TRecognizerFv (+0x1c)
	virtual ULong		ServicesEnabled(void);					// ROM 0x0014533c ServicesEnabled__11TRecognizerFv (+0x20)
	virtual long		UnitConfidence(TUnitPublic* unit);		// ROM 0x00145344 UnitConfidence__11TRecognizerFP11TUnitPublic (+0x24: 0)
	virtual void		Sleep(void);							// ROM 0x0014534c Sleep__11TRecognizerFv (+0x28)
	virtual void		WakeUp(void);							// ROM 0x00145350 WakeUp__11TRecognizerFv (+0x2c)
	virtual ULong		ArbitrateTime(void);					// ROM 0x00145354 ArbitrateTime__11TRecognizerFv (+0x30)
	virtual void		BuildConfig(RefArg config, TView* view, ULong flags);	// ROM 0x00145368 BuildConfig__11TRecognizerFRC6RefVarP5TViewUl (+0x34: nothing)
	virtual long		EnableArea(TRecArea* area, RefArg config);	// ROM 0x0014536c EnableArea__11TRecognizerFP8TRecAreaRC6RefVar (+0x38: NOT YET - the type added to the area when the config's inputMask enables it)
	virtual long		ConfigureArea(TRecArea* area, RefArg config);	// ROM 0x00145418 ConfigureArea__11TRecognizerFP8TRecAreaRC6RefVar (+0x3c: 0)
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00146074 HandleUnit__11TRecognizerFP11TUnitPublic (+0x40: ==> the command)
	virtual Ref			GetLearningData(TUnitPublic* unit);		// ROM 0x001454b8 GetLearningData__11TRecognizerFP11TUnitPublic (+0x44: nil)
	virtual void		DoLearning(RefArg data, long arg);		// ROM 0x001454c0 DoLearning__11TRecognizerFRC6RefVarl (+0x48: nothing)

	TDomain*			fDomain;			// +0x04
	ULong				fID;				// +0x08  the unit type
	ULong				fCommand;			// +0x0c
	UChar				fFlags;				// +0x10
	ULong				fArbitrateTime;		// +0x14
	ULong				fServicesPossible;	// +0x18
	ULong				fServicesEnabled;	// +0x1c
};

class TRecognizerList : public TArray
{
public:
	static TRecognizerList*	Make(void);							// ROM 0x001a0140 Make__15TRecognizerListSFv
	long				IRecognizerList(void);					// ROM 0x001a01a8 IRecognizerList__15TRecognizerListFv

	void				AddRecognizer(TRecognizer* recognizer);	// ROM 0x001a01b4 AddRecognizer__15TRecognizerListFP11TRecognizer
	TRecognizer*		GetRecognizer(ULong index);				// ROM 0x001a01d8 GetRecognizer__15TRecognizerListFUl
	TRecognizer*		FindRecognizer(ULong id);				// ROM 0x001a01f8 FindRecognizer__15TRecognizerListFUl - by unit type; nil for none
};

// the click recogniser: ==> 0 when another view's area is in use
class TClickRecognizer : public TRecognizer
{
public:
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x0014578c HandleUnit__16TClickRecognizerFP11TUnitPublic
};

// the click-event recogniser: taps, double taps, hilite clicks
class TEventRecognizer : public TRecognizer
{
public:
	virtual ULong		ID(void);								// ROM 0x00145668 ID__16TEventRecognizerFv ('CEVT')
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00145674 HandleUnit__16TEventRecognizerFP11TUnitPublic
};

class TRecognitionManager
{
public:
	long				Init(UChar level);						// ROM 0x001a03e0 Init__19TRecognitionManagerFUc
	long				InitRecognizers(void);					// ROM 0x0019f6f4 InitRecognizers__19TRecognitionManagerFv
	void				EnableModalRecognition(Rect& bounds);	// ROM 0x0019f7a4 EnableModalRecognition__19TRecognitionManagerFR5TRect
	void				DisableModalRecognition(void);			// ROM 0x0019f7fc DisableModalRecognition__19TRecognitionManagerFv
	Boolean				ModalRecognitionOK(Rect& bounds);		// ROM 0x0019f824 ModalRecognitionOK__19TRecognitionManagerFR5TRect - whether the rect's centre lies in the modal bounds (the popup closed when not)
	void				IgnoreClicks(ULong ticks);				// ROM 0x0019f8ec IgnoreClicks__19TRecognitionManagerFUl
	void				SetNextClick(ULong time);				// ROM 0x0019f910 SetNextClick__19TRecognitionManagerFUl - the ignoring dropped unless the time is within a second of its end
	void				SaveClickView(TView* view);				// ROM 0x0019f934 SaveClickView__19TRecognitionManagerFP5TView
	void				RemoveClickView(TView* view);			// ROM 0x0019f944 RemoveClickView__19TRecognitionManagerFP5TView
	long				Idle(void);								// ROM 0x001a0618 Idle__19TRecognitionManagerFv (NOT YET: the stroke world and controller idled)

	UChar				fLevel;				// +0x00
	StrokeCentral*		fStrokeWorld;		// +0x04
	TController*		fController;		// +0x08
	TArbiter*			fArbiter;			// +0x0c
	TRecObject*			fAreas;				// +0x10
	TRecognizerList*	fRecognizers;		// +0x14
	ULong				fIgnoreClicksUntil;	// +0x18
	UChar				fFlag1c;			// +0x1c  1
	Rect*				fModalBounds;		// +0x28
	TView*				fPrevClickView;		// +0x2c
	TView*				fClickView;			// +0x30
	UChar				fFlag38;			// +0x38
	ULong				fUnused3c;			// +0x3c
};

extern TRecognitionManager	gRecognition;					// ROM 0x0c103f50 gRecognition

void	InstallClickRecognizer(TRecognitionManager* manager);	// ROM 0x00145830 InstallClickRecognizer__FP19TRecognitionManager
void	InstallEventRecognizer(TRecognitionManager* manager);	// ROM 0x00145704 InstallEventRecognizer__FP19TRecognitionManager
Boolean	OnlyStrokeWritten(TStrokeUnit* unit);				// ROM 0x00209828 OnlyStrokeWritten__FP11TStrokeUnit (NOT YET: true)

#endif	/* __RECOGNIZER_H */
