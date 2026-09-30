/*
	File:		pcmcia/PCMCIA20Parser.h

	Contains:	TPCMCIA20Parser, which reads a card's CIS (PC Card standard
				2.0 and the multi-function additions) into a TCardPCMCIA:
				the devices, their JEDEC codes and geometry, VERS_1 and
				VERS_2, the manufacturer and function ids, the
				configuration tuple and its entries, Apple's package tuples,
				checksums and the long links.

				Each CisTpl_ handler is given a copy of the tuple (code,
				link, data) and answers where it stopped reading; a problem
				is left in fError, which ProcessCIS weighs after each tuple
				(an unknown tuple is counted and passed over, up to sixteen
				of them).

				Not in the DDK; the layout is the ROM's (0x2c bytes, from its
				accesses).  Reconstructed from the MP2x00 US ROM
				(0x0004bd40-0x0004e40c).
*/

#ifndef __PCMCIA20PARSER_H
#define __PCMCIA20PARSER_H

#ifndef __CARDPCMCIA_H
#include "CardPCMCIA.h"
#endif

class TCardSocket;


class TPCMCIA20Parser
{
public:
					TPCMCIA20Parser();								// ROM 0x0004bd40 __ct__15TPCMCIA20ParserFv
					~TPCMCIA20Parser();								// ROM 0x0004bd8c __dt__15TPCMCIA20ParserFv

	NewtonErr		ParsePCCardCIS(TCardPCMCIA* card, TCardSocket* socket);										// ROM 0x0004e23c ParsePCCardCIS__15TPCMCIA20ParserFP11TCardPCMCIAP11TCardSocket
	NewtonErr		ParsePCCardCIS(UChar* attrMem, UChar* commonMem, TCardPCMCIA* card, TCardSocket* socket);	// ROM 0x0004e114 ParsePCCardCIS__15TPCMCIA20ParserFPUcT1P11TCardPCMCIAP11TCardSocket
	ULong			Version(void);									// ROM 0x0004e288 Version__15TPCMCIA20ParserFv

	NewtonErr		Reset(void);									// ROM 0x0004d218 Reset__15TPCMCIA20ParserFv
	NewtonErr		ValidateCIS(UChar* attrMem, UChar* commonMem, TCardPCMCIA* card, TCardSocket* socket);	// ROM 0x0004df88 ValidateCIS__15TPCMCIA20ParserFPUcT1P11TCardPCMCIAP11TCardSocket
	NewtonErr		ProcessCIS(UChar* attrMem, UChar* commonMem);	// ROM 0x0004ddc0 ProcessCIS__15TPCMCIA20ParserFPUcT1
	UChar*			ProcessTuple(UChar* tuple, UChar* base, UChar* where, UChar inAttrMemory);	// ROM 0x0004db38 ProcessTuple__15TPCMCIA20ParserFPUcN21Uc

	UChar*			CisTpl_Null(UChar* tuple);						// ROM 0x0004d6dc CisTpl_Null__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Device(UChar* tuple);					// ROM 0x0004d0f0 CisTpl_Device__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Device_A(UChar* tuple);					// ROM 0x0004d118 CisTpl_Device_A__15TPCMCIA20ParserFPUc
	UChar*			DeviceParser(UChar* tuple, UChar forAttrMemory, UChar otherConditions);	// ROM 0x0004cd6c DeviceParser__15TPCMCIA20ParserFPUcUcT2
	UChar*			CisTpl_Device_GEO(UChar* tuple);				// ROM 0x0004d124 CisTpl_Device_GEO__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Checksum(UChar* tuple, UChar* base, ULong offset, UChar inAttrMemory);	// ROM 0x0004cae8 CisTpl_Checksum__15TPCMCIA20ParserFPUcT1UlUc
	UChar*			CisTpl_LongLink_A(UChar* tuple);				// ROM 0x0004d620 CisTpl_LongLink_A__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_LongLink_C(UChar* tuple);				// ROM 0x0004d670 CisTpl_LongLink_C__15TPCMCIA20ParserFPUc
	UChar*			SetLongLink(UChar* tuple, UChar inAttrMemory);	// ROM 0x0004d5b0 SetLongLink__15TPCMCIA20ParserFPUcUc
	UChar*			CisTpl_LinkTarget(UChar* tuple, UChar* where);	// ROM 0x0004d57c CisTpl_LinkTarget__15TPCMCIA20ParserFPUcT1
	UChar*			CisTpl_No_Link(UChar* tuple);					// ROM 0x0004d69c CisTpl_No_Link__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Vers_1(UChar*& tuple);					// ROM 0x0004d8c8 CisTpl_Vers_1__15TPCMCIA20ParserFRPUc
	UChar*			CisTpl_Vers_2(UChar* tuple);					// ROM 0x0004da24 CisTpl_Vers_2__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Jedec_C(UChar* tuple);					// ROM 0x0004d574 CisTpl_Jedec_C__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Jedec_A(UChar* tuple);					// ROM 0x0004d56c CisTpl_Jedec_A__15TPCMCIA20ParserFPUc
	UChar*			JedecInfoParser(UChar* tuple, UChar forAttrMemory);	// ROM 0x0004d464 JedecInfoParser__15TPCMCIA20ParserFPUcUc
	UChar*			CisTpl_Conf(UChar* tuple);						// ROM 0x0004cbd0 CisTpl_Conf__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_CE(UChar* tuple);						// ROM 0x0004c0dc CisTpl_CE__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Manuf_Id(UChar* tuple);					// ROM 0x0004d288 CisTpl_Manuf_Id__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Func_Id(UChar* tuple);					// ROM 0x0004d2e8 CisTpl_Func_Id__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Func_Ext(UChar* tuple);					// ROM 0x0004d3c0 CisTpl_Func_Ext__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_Vendor_Unique(UChar* tuple);				// ROM 0x0004d6fc CisTpl_Vendor_Unique__15TPCMCIA20ParserFPUc
	UChar*			CisTpl_End(UChar* tuple);						// ROM 0x0004d280 CisTpl_End__15TPCMCIA20ParserFPUc

	UChar*			GetPowerValue(UChar* p, UChar inTenths, void* value, ULong* extension);	// ROM 0x0004bdd0 GetPowerValue__15TPCMCIA20ParserFPUcUcPvPUl
	Boolean			ChecksumOK(ULong checksum, UChar* from, ULong count, ULong stride);	// ROM 0x0004bf0c ChecksumOK__15TPCMCIA20ParserFUlPUcN21
	TNanoSecond		GetExtendedDeviceSpeed(UChar*& p);				// ROM 0x0004c020 GetExtendedDeviceSpeed__15TPCMCIA20ParserFRPUc
	ULong			GetBits(ULong byte, ULong highBit, ULong count);	// ROM 0x0004e0fc GetBits__15TPCMCIA20ParserFUlN21
	ULong			pow(ULong base, ULong exponent);				// ROM 0x0004d64c pow__15TPCMCIA20ParserFUlT1
	Long			GetShort(UChar*& p);							// ROM 0x0004e294 GetShort__15TPCMCIA20ParserFRPUc
	ULong			GetWord(UChar*& p);								// ROM 0x0004e2c8 GetWord__15TPCMCIA20ParserFRPUc
	UChar*			GetTuple(UChar* from, UChar* buffer, UChar inAttrMemory);	// ROM 0x0004e304 GetTuple__15TPCMCIA20ParserFPUcT1Uc
	UChar*			IncrAddr(UChar*& p, ULong count);				// ROM 0x0004e3bc IncrAddr__15TPCMCIA20ParserFRPUcUl
	UChar*			StartTuple(UChar*& p);							// ROM 0x0004e3e4 StartTuple__15TPCMCIA20ParserFRPUc

	NewtonErr			fError;				// +00
	TCardPCMCIA*		fCard;				// +04 the CIS being filled in (a function's, on a multi-function card)
	TCardSocket*		fSocket;			// +08
	TCardLongLink		fLongLink;			// +0C
	TCardConfiguration*	fDefaultConfig;		// +14 the entry the next CISTPL_CFTABLE_ENTRY defaults from
	UChar*				fTupleEnd;			// +18 the last byte of the tuple being read
	UChar				fTupleCount;		// +1C
	UChar				fDeviceTuples;		// +1D CISTPL_DEVICEs seen (only the first is read)
	UChar				fNullTuples;		// +1E
	TCardFunction*		fFunction;			// +20 the function the last CISTPL_FUNCID made
	ULong				fConfigBase;		// +24 the configuration registers' address (CISTPL_CONFIG)
	UChar				fConfigEntryStart;	// +28 the function's first configuration entry
	UChar				fConfigEntryEnd;	// +29 and its last so far
	UChar				fNewFunction;		// +2A a CISTPL_FUNCID since the last entry
	UChar				fIOFunctions;		// +2B how many functions the next entry's I/O ranges are for
};

#endif	/* __PCMCIA20PARSER_H */
