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

	Reconstructed from the MP2x00 US ROM (0x001437b4-0x0014454c,
	0x0019de84-0x0019dfa4, 0x0019d438-0x0019d6a4, 0x0019e124-0x0019e3c4);
	each function cites its origin.
*/

#ifndef __RECOGNIZER_H
#define __RECOGNIZER_H

#include "Unit.h"
#include "Domain.h"
#include "Areas.h"
#include "NewtonTime.h"

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
	kRecognizerArbitrated	= 2,		// competes in arbitration
	kRecognizerIsWriting	= 1		// it reads writing (TRecognitionManager::fAfterWriting)
};

class TRecognizer
{
public:
						TRecognizer();							// ROM 0x001437b4 __ct__11TRecognizerFv
	virtual void		Init(TDomain* domain, ULong id, ULong command, UChar flags, ULong arbitrateTime);	// ROM 0x00143934 Init__11TRecognizerFP7TDomainUlT2UcT2 (+0x00)
	virtual void		InitServices(ULong possible, ULong enabled);	// ROM 0x00143808 InitServices__11TRecognizerFUlT1 (+0x04)
	virtual TDomain*	Domain(void);							// ROM 0x00143de4 Domain__11TRecognizerFv (+0x08)
	virtual ULong		ID(void);								// ROM 0x001443ec ID__11TRecognizerFv (+0x0c)
	virtual ULong		Command(void);							// ROM 0x001444f8 Command__11TRecognizerFv (+0x10)
	virtual ULong		Flags(void);							// ROM 0x00144500 Flags__11TRecognizerFv (+0x14)
	virtual Boolean		TestFlags(UChar flags);					// ROM 0x00144508 TestFlags__11TRecognizerFUc (+0x18)
	virtual ULong		ServicesPossible(void);					// ROM 0x00144528 ServicesPossible__11TRecognizerFv (+0x1c)
	virtual ULong		ServicesEnabled(void);					// ROM 0x001437e8 ServicesEnabled__11TRecognizerFv (+0x20)
	virtual long		UnitConfidence(TUnitPublic* unit);		// ROM 0x001437f0 UnitConfidence__11TRecognizerFP11TUnitPublic (+0x24: 0)
	virtual void		Sleep(void);							// ROM 0x001437f8 Sleep__11TRecognizerFv (+0x28)
	virtual void		WakeUp(void);							// ROM 0x001437fc WakeUp__11TRecognizerFv (+0x2c)
	virtual ULong		ArbitrateTime(void);					// ROM 0x00143800 ArbitrateTime__11TRecognizerFv (+0x30)
	virtual void		BuildConfig(RefArg config, TView* view, ULong flags);	// ROM 0x00143814 BuildConfig__11TRecognizerFRC6RefVarP5TViewUl (+0x34: nothing)
	virtual long		EnableArea(TRecArea* area, RefArg config);	// ROM 0x00143818 EnableArea__11TRecognizerFP8TRecAreaRC6RefVar (+0x38: NOT YET - the type added to the area when the config's inputMask enables it)
	virtual long		ConfigureArea(TRecArea* area, RefArg config);	// ROM 0x001438c4 ConfigureArea__11TRecognizerFP8TRecAreaRC6RefVar (+0x3c: 0)
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00144520 HandleUnit__11TRecognizerFP11TUnitPublic (+0x40: ==> the command)
	virtual Ref			GetLearningData(TUnitPublic* unit);		// ROM 0x00143964 GetLearningData__11TRecognizerFP11TUnitPublic (+0x44: nil)
	virtual void		DoLearning(RefArg data, long arg);		// ROM 0x0014396c DoLearning__11TRecognizerFRC6RefVarl (+0x48: nothing)

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
	static TRecognizerList*	Make(void);							// ROM 0x0019de84 Make__15TRecognizerListSFv
	long				IRecognizerList(void);					// ROM 0x0019deec IRecognizerList__15TRecognizerListFv

	void				AddRecognizer(TRecognizer* recognizer);	// ROM 0x0019def8 AddRecognizer__15TRecognizerListFP11TRecognizer
	TRecognizer*		GetRecognizer(ULong index);				// ROM 0x0019df1c GetRecognizer__15TRecognizerListFUl
	TRecognizer*		FindRecognizer(ULong id);				// ROM 0x0019df3c FindRecognizer__15TRecognizerListFUl - by unit type; nil for none
};

// the click recogniser: ==> 0 when another view's area is in use
// A tick of the 60 Hz clock in the units Sleep counts: the Newton's
// timer runs at 3.6864 MHz, so a sixtieth of a second is 61440 of them.
enum { kTicksToTimeUnits = 0xf000 };


// The gesture recogniser, over the edge-list domain (EdgeList.h).  It
// turns the label the domain's shape tests put on a unit into the
// command the view under it answers.
class TScrubRecognizer : public TRecognizer
{
public:
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00143a00 HandleUnit__16TScrubRecognizerFP11TUnitPublic
};


class TClickRecognizer : public TRecognizer
{
public:
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00143c38 HandleUnit__16TClickRecognizerFP11TUnitPublic
};

// the click-event recogniser: taps, double taps, hilite clicks
class TEventRecognizer : public TRecognizer
{
public:
	virtual ULong		ID(void);								// ROM 0x00143b14 ID__16TEventRecognizerFv ('CEVT')
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00143b20 HandleUnit__16TEventRecognizerFP11TUnitPublic
};

class TRecognitionManager;

// The word recogniser, which drives a handwriting engine through the
// TWRecognizer protocol (WRecDomain.h).  It answers every question by
// handing it to its domain; what it adds is HandleUnit, the decision
// about what the view under the writing is told.
class TWRecRecognizer : public TRecognizer
{
public:
	virtual long		UnitConfidence(TUnitPublic* unit);		// ROM 0x00144238 UnitConfidence__15TWRecRecognizerFP11TUnitPublic
	virtual void		Sleep(void);							// ROM 0x00144260 Sleep__15TWRecRecognizerFv
	virtual void		WakeUp(void);							// ROM 0x00144280 WakeUp__15TWRecRecognizerFv
	virtual long		ConfigureArea(TRecArea* area, RefArg config);	// ROM 0x00144178 ConfigureArea__15TWRecRecognizerFP8TRecAreaRC6RefVar
	virtual ULong		HandleUnit(TUnitPublic* unit);			// ROM 0x00144174 HandleUnit__15TWRecRecognizerFP11TUnitPublic
};

// The services a word recogniser can provide: everything a field can
// ask to have read, which is vAnythingAllowed less the strokes and
// clicks it does not deal in.
enum { kWRecServices = 0x017ef000 };

// The writer's recognition preferences, read at boot and whenever the
// Prefs slip changes one of them.
long	GetDefaultedPreference(RefArg slot, long deflt);		// ROM 0x0019cc04 GetDefaultedPreference__FRC6RefVarl - the default written down when there is none
Ref		FReadCursiveOptions(RefArg rcvr);					// ROM 0x0019cfd8 FReadCursiveOptions__FRC6RefVar
Ref		ReadDomainOptions(void);							// ROM 0x0019d1e0 ReadDomainOptions
// What the writer settled on handed to the recogniser of that unit
// type, so that it reads the same writing better next time.
void	DoIndexedLearning(ULong id, RefArg data, ULong which);	// ROM 0x001a0cc4 DoIndexedLearning__FUlRC6RefVarT1

// ROM 0x0c10184c gLetterSetSelection / 0x0c101850 gRecognitionTimeout /
// 0x0c101858 gRecognitionLetterSpacing / 0x0c101868 gUseBigTrainingData
extern long		gLetterSetSelection;		// which letter set the engines read
extern ULong	gRecognitionTimeout;		// ticks the recogniser waits after the pen stops (15 to 60)
extern long		gRecognitionLetterSpacing;	// how close letters may be (nine less what the slip offers)
extern Boolean	gUseBigTrainingData;		// the learning keeps more about each word

void	InstallWRecRecognizer(TRecognitionManager* manager);	// ROM 0x00144094 InstallWRecRecognizer__FP19TRecognitionManager
void	RegisterWRec(void);									// ROM 0x001b5bb4 RegisterWRec__Fv (NOT YET: the ROM's own engine)

// What a word unit that has won its arbitration comes to: a tap, a
// word, or one of the two ink commands.  The Airus word recogniser
// answers through the same function.
// The command the recogniser of a unit type answers with (0 for no
// recogniser).
ULong	GetCommand(ULong type);
TUnitList*	HandleGetContextUnits(TUnit* unit, long whole);	// ROM 0x0019dbd8 HandleGetContextUnits__FP5TUnitl									// ROM 0x0019d338 GetCommand__FUl
ULong	WordRecognizerHandleUnit(TRecognizer* recognizer, TUnitPublic* unit);	// ROM 0x00143f00 WordRecognizerHandleUnit__FP11TRecognizerP11TUnitPublic
ULong	GetInkCommand(RefArg wordInfo);						// ROM 0x00143dec GetInkCommand__FRC6RefVar - aeRawInk or aeInkWord, by what the view wants

// Which word recogniser is in use.
ULong	GetIDFromRef(RefArg spec);							// ROM 0x00144380 GetIDFromRef__FRC6RefVar - a four-character type out of a four-character string
Boolean	SetWordRecognizer(ULong id);						// ROM 0x001442a0 (unnamed) - SetWordRecognizer
Ref		FUseWRec(RefArg rcvr, RefArg name);					// ROM 0x001443f4 FUseWRec

// ROM 0x0c101688 gRecInkNotifyFlags / 0x0c101684 gLastInkWordWarning
// Whether to warn the writer about ink and about the recogniser running
// out of memory, and the day the ink warning was last shown.
extern ULong	gRecInkNotifyFlags;
extern ULong	gLastInkWordWarning;

class TRecognitionManager
{
public:
	long				Init(UChar level);						// ROM 0x0019e124 Init__19TRecognitionManagerFUc
	long				InitRecognizers(void);					// ROM 0x0019d438 InitRecognizers__19TRecognitionManagerFv
	void				EnableModalRecognition(Rect& bounds);	// ROM 0x0019d4e8 EnableModalRecognition__19TRecognitionManagerFR5TRect
	void				DisableModalRecognition(void);			// ROM 0x0019d540 DisableModalRecognition__19TRecognitionManagerFv
	Boolean				ModalRecognitionOK(Rect& bounds);		// ROM 0x0019d568 ModalRecognitionOK__19TRecognitionManagerFR5TRect - whether the rect's centre lies in the modal bounds (the popup closed when not)
	void				IgnoreClicks(ULong ticks);				// ROM 0x0019d630 IgnoreClicks__19TRecognitionManagerFUl
	void				SetNextClick(ULong time);				// ROM 0x0019d654 SetNextClick__19TRecognitionManagerFUl - the ignoring dropped unless the time is within a second of its end
	void				SaveClickView(TView* view);				// ROM 0x0019d678 SaveClickView__19TRecognitionManagerFP5TView
	void				RemoveClickView(TView* view);			// ROM 0x0019d688 RemoveClickView__19TRecognitionManagerFP5TView
	long				Idle(void);								// ROM 0x0019e35c Idle__19TRecognitionManagerFv - the strokes idled, the ink compressed (the controller's idle NOT YET)
	TTime				NextIdle(void);							// ROM 0x0019e394 NextIdle__19TRecognitionManagerFv - when to idle next: the earlier of the stroke world's compress time and the controller's next time (NOT YET); zero for never

	UChar				fLevel;				// +0x00
	StrokeCentral*		fStrokeWorld;		// +0x04
	TController*		fController;		// +0x08
	TArbiter*			fArbiter;			// +0x0c
	TRecObject*			fAreas;				// +0x10
	TRecognizerList*	fRecognizers;		// +0x14
	ULong				fIgnoreClicksUntil;	// +0x18
	Boolean				fAfterWriting;		// +0x1c  the last unit handled went to a recogniser flagged 1 (the words'): a click soon after is swallowed
	Rect*				fModalBounds;		// +0x28
	TView*				fPrevClickView;		// +0x2c
	TView*				fClickView;			// +0x30
	AreaHandler			fUnitHandler;		// +0x34  what an area's winning units are handed to: HandleUnit, or the journal's replay
	Boolean				fClickSwallowed;	// +0x38  a click on a clicks-only area went unhandled: the click view forgotten, recognition triggered
	ULong				fUnused3c;			// +0x3c
};

extern TRecognitionManager	gRecognition;					// ROM 0x0c106e88 gRecognition

// the unit handler (HandleUnit.cpp): the units the controller has
// arbitrated handed to their recognisers and the commands posted to the
// views
long	HandleUnit(TArray* units);						// ROM 0x0019d6a8 HandleUnit__FP6TArray - HandleUnitList under an exception handler (an exception is reported, not thrown)
long	HandleUnitList(TArray* units);					// ROM 0x0019d72c HandleUnitList__FP6TArray - ==> whether any unit was handled
long	PostAndDoCommand(ULong command, TUnitPublic* unit, ULong mask);	// ROM 0x0019dccc PostAndDoCommand__FUlP11TUnitPublicT1 - the command dispatched to the view under the unit; ==> the command's result (1 when a popup closed on the click)
TUnitList*	HandleGetContextUnits(TUnit* unit, long arg);	// ROM 0x0019dbd8 HandleGetContextUnits__FP5TUnitl - aeGetContextUnits to the view under the unit: the shapes on the page as units
void	HandleExpiredStroke(TUnit* unit);				// ROM 0x0019dad0 HandleExpiredStroke__FP5TUnit - a stroke no recogniser took (NOT YET: to the stroke world's expired strokes; the ink taken off while the arbiter is modal)
void	UpdateStroke(TUnit* unit);						// ROM 0x0019db84 UpdateStroke__FP5TUnit - the unit's stroke's ink taken off and the root view updated
void	SafeExceptionNotify(Exception* exception);	// ROM 0x00036a3c SafeExceptionNotify__FP9Exception - an exception out of a handler reported, not thrown
extern Boolean	gInhibitPopup;							// ROM 0x0c101948 gInhibitPopup

void	InstallStrokeRecognizer(TRecognitionManager* manager);	// ROM 0x00143d64 InstallStrokeRecognizer__FP19TRecognitionManager
void	InstallClickRecognizer(TRecognitionManager* manager);	// ROM 0x00143cdc InstallClickRecognizer__FP19TRecognitionManager
void	InstallEventRecognizer(TRecognitionManager* manager);	// ROM 0x00143bb0 InstallEventRecognizer__FP19TRecognitionManager
void	InstallGestureRecognizer(TRecognitionManager* manager);	// ROM 0x00143970 InstallGestureRecognizer__FP19TRecognitionManager
Boolean	OnlyStrokeWritten(TStrokeUnit* unit);				// ROM 0x0020bf58 OnlyStrokeWritten__FP11TStrokeUnit - no other stroke is in hand and none followed this one
Boolean	OtherViewInUse(TView* view);						// ROM 0x00036960 OtherViewInUse__FP5TView - somebody else's writing is still in hand
Boolean	ClicksOnlyArea(TUnit* unit);						// ROM 0x000369e8 ClicksOnlyArea__FP5TUnit (NOT YET: false)

#endif	/* __RECOGNIZER_H */
