/*
	File:		pcmcia/CardCISIterator.h

	Contains:	TCardCISIterator, which walks the tuples of a card's CIS
				where they lie in the card's memory - following the long
				links from attribute memory to common memory and back, and
				the multi-function card's separate CISs - and the byte
				accessors for the attribute memory the CIS code reads it
				with.

				A tuple in attribute memory is one byte in two (the card's
				8-bit CIS on the even addresses of its 16-bit bus); the ROM
				reads each at (address ^ 3), the byte lane the Voyager puts
				it in, and the host's card lays its attribute window out to
				match (hal/host/HostCard.h).

				Not in the DDK; the layout is the ROM's (0x48 bytes, from
				its accesses).  Reconstructed from the MP2x00 US ROM
				(0x0004b30c-0x0004bd40, 0x0004ecbc-0x0004ed10).
*/

#ifndef __CARDCISITERATOR_H
#define __CARDCISITERATOR_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

class TCardSocket;
class TCardPackage;

UChar	CardAttrMemReadByte(void* addr);				// ROM 0x0004ecec CardAttrMemReadByte__FPv
void	CardAttrMemWriteByte(void* addr, UChar data);	// ROM 0x0004ed08 CardAttrMemWriteByte__FPvUc
void	CardAttrMemReadDelay(void);						// ROM 0x0004ecbc CardAttrMemReadDelay__Fv
void	CardAttrMemWriteDelay(void);					// ROM 0x0004ece0 CardAttrMemWriteDelay__Fv

// The iterator's status word (GetStatus)
enum
{
	kCISStatusError			= 0x0003,		// the last read failed (both bits set)
	kCISStatusMirrored		= 0x0004,		// the card has no attribute memory: its CIS is in common memory, read as words
	kCISStatusInAttrMemory	= 0x0010,		// the current CIS chain is in attribute memory (one byte in two)
	kCISStatusLinkToAttr	= 0x0020,		// the pending long link is to attribute memory
	kCISStatusMultiCIS		= 0x0040,		// a CISTPL_LONGLINK_MFC was seen: the card has a CIS per function
	kCISStatusFunctionCIS	= 0x0080,		// iterating one function's CIS
	kCISStatusAtStart		= 0x8000		// ResetCIS has put it at the start of a chain
};

const ULong	kMaxCISs = 8;


class TCardCISIterator
{
public:
					TCardCISIterator();												// ROM 0x0004b30c __ct__16TCardCISIteratorFv
					~TCardCISIterator();											// ROM 0x0004b31c __dt__16TCardCISIteratorFv

	NewtonErr		Init(TCardSocket* socket);										// ROM 0x0004b6b0 Init__16TCardCISIteratorFP11TCardSocket
	ULong			Version(void);													// ROM 0x0004b598 Version__16TCardCISIteratorFv
	ULong			GetStatus(void);												// ROM 0x0004b87c GetStatus__16TCardCISIteratorFv
	NewtonErr		SelectCIS(ULong cisNumber);										// ROM 0x0004b884 SelectCIS__16TCardCISIteratorFUl
	NewtonErr		GetTuple(UChar fromStart);										// ROM 0x0004ba0c GetTuple__16TCardCISIteratorFUc
	NewtonErr		GetTupleData(UChar* buffer, ULong size);						// ROM 0x0004b328 GetTupleData__16TCardCISIteratorFPUcUl
	NewtonErr		GetPackage(TCardPackage* package, UChar fromStart);				// ROM 0x0004b36c GetPackage__16TCardCISIteratorFP12TCardPackageUc

	NewtonErr		ReadCIS(UChar* from, UChar* buffer, ULong count, UChar inAttrMemory);	// ROM 0x0004b8b4 ReadCIS__16TCardCISIteratorFPUcT1UlUc
	NewtonErr		VerifyLinkTargetTuple(UChar* at, UChar* buffer, UChar inAttrMemory);	// ROM 0x0004b4ac VerifyLinkTargetTuple__16TCardCISIteratorFPUcT1Uc
	ULong			SwapLittleEndianShort(UChar* p);								// ROM 0x0004b560 SwapLittleEndianShort__16TCardCISIteratorFPUc
	ULong			SwapLittleEndianLong(UChar* p);									// ROM 0x0004b578 SwapLittleEndianLong__16TCardCISIteratorFPUc
	void			ResetFields(void);												// ROM 0x0004b5a4 ResetFields__16TCardCISIteratorFv
	void			ResetCIS(void);													// ROM 0x0004b610 ResetCIS__16TCardCISIteratorFv

	UChar			fSearchCode;		// +00 the tuple GetTuple looks for (0xFF: any)
	UChar			fTupleCode;			// +01 the current tuple's code
	UChar			fTupleLink;			// +02 and link
	UChar			fField03;			// +03
	ULong			fField04;			// +04
	ULong			fField08;			// +08
	UChar*			fTupleAddress;		// +0C the current tuple
	UChar			fNumOfCISs;			// +10 1, or the functions' CISs a CISTPL_LONGLINK_MFC names plus one
	UChar			fCurrentCIS;		// +11
	UChar			fStopAtEvery;		// +12 bit 0: GetTuple answers every tuple, links included
	UChar			fField13;			// +13
	UChar*			fNextTuple;			// +14
	UChar*			fLongLink;			// +18 where the chain goes on at its end (nil: nowhere)
	TCardSocket*	fSocket;			// +1C
	ULong			fStatus;			// +20
	UChar			fCISInAttrMemory;	// +24 a bit per CIS: its chain starts in attribute memory
	UChar*			fCISAddress[kMaxCISs];	// +28 where each CIS starts
};

#endif	/* __CARDCISITERATOR_H */
