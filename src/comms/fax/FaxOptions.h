/*
	File:		comms/fax/FaxOptions.h

	Contains:	The fax tool's options (comms/fax/FaxTool.h): the page's set-up,
				direction, identities, the session agreed, the minimum scan
				line time, a band's layout, and the start and end of a page.

				The ROM's classes; their declarations are not in the DDK, so
				the names of the fields are ours, their offsets the ROM's.

	Reconstructed from the MP2x00 US ROM (0x000b4a30-0x000b4e00); each
	constructor cites its origin (FaxTool.cpp).
*/

#ifndef __COMMS_FAX_FAXOPTIONS_H
#define __COMMS_FAX_FAXOPTIONS_H

#ifndef __OPTIONARRAY_H
#include "OptionArray.h"
#endif

/*------------------------------------------------------------------------------
	The fax options.
------------------------------------------------------------------------------*/

#define kCMOFaxPageSetUp			'fpsu'
#define kCMOFaxPassThru				'fpt '
#define kCMOFaxEnableProgressEvent	'fepe'
#define kCMOFaxDirection			'fdir'
#define kCMOFaxSessionInfo			'fsif'
#define kCMOFaxRemoteId				'frid'
#define kCMOFaxLocalId				'flid'
#define kCMOFaxMinScanLineTime		'fmsl'
#define kCMOFaxStartPage			'fsgp'
#define kCMOFaxConfigSendBand		'fcsb'
#define kCMOFaxEndMessage			'feom'

// 'fpsu': the page's resolution and size
class TCMOFaxPageSetUp : public TOption
{
public:
					TCMOFaxPageSetUp();

	ULong			fLength;				// +0x0c  T.30's length (0 A4, 1 B4, 2 unlimited)
	ULong			fWidth;					// +0x10  T.30's width (0 1728 pixels, 1 2048, 2 2432)
	ULong			fResolution;			// +0x14  1 standard, 3 fine
};

// 'fpt ': (never read by the tool)
class TCMOFaxPassThru : public TOption
{
public:
					TCMOFaxPassThru();

	Boolean			fPassThru;				// +0x0c
};

// 'fepe': a progress event every so many lines received (0: none)
class TCMOFaxEnableProgressEvent : public TOption
{
public:
					TCMOFaxEnableProgressEvent();

	ULong			fLines;					// +0x0c
};

// 'fdir': sending, receiving or both
class TCMOFaxDirection : public TOption
{
public:
					TCMOFaxDirection();

	Boolean			fSend;					// +0x0c
	Boolean			fReceive;				// +0x0d
};

// 'fsif': the session agreed (what a received page is)
class TCMOFaxSessionInfo : public TOption
{
public:
					TCMOFaxSessionInfo();

	ULong			fHorizontalRes;			// +0x0c  dots per inch (204)
	ULong			fVerticalRes;			// +0x10  lines per inch (98 standard, 196 fine)
	ULong			fLength;				// +0x14  T.30's length
	ULong			fWidth;					// +0x18  pixels
	ULong			fBitRate;				// +0x1c  bits a second
};

// 'frid', 'flid': the other machine's identity (CSI or TSI), and ours
class TCMOFaxRemoteId : public TOption
{
public:
					TCMOFaxRemoteId();

	UChar			fId[0x18];				// +0x0c  a C string (20 characters and a nought)
};

class TCMOFaxLocalId : public TOption
{
public:
					TCMOFaxLocalId();

	UChar			fId[0x18];				// +0x0c
};

// 'fmsl': the page's minimum scan line time
class TCMOFaxMinScanLineTime : public TOption
{
public:
					TCMOFaxMinScanLineTime();

	ULong			fTime;					// +0x0c  (0x1380 to 0x1388, T.30's codes)
};

// 'fsgp': a page starts (answered asynchronously)
class TCMOFaxStartPage : public TOptionExtended
{
public:
					TCMOFaxStartPage();
};

// 'fcsb': how the client's bands of scan lines are laid out
class TCMOFaxConfigSendBand : public TOption
{
public:
					TCMOFaxConfigSendBand();

	ULong			fLines;					// +0x0c  lines a band
	ULong			fBytesPerLine;			// +0x10  bytes a line in the band
	ULong			fLeftOffset;			// +0x14  white pixels before each line
};

// 'feom': a page ends (answered asynchronously)
class TCMOFaxEndMessage : public TOptionExtended
{
public:
					TCMOFaxEndMessage();

	Boolean			fLastPage;				// +0x14  sending: no page follows; receiving: another does
	Boolean			fPageAccepted;			// +0x15  receiving: the client kept the page
};


#endif	/* __COMMS_FAX_FAXOPTIONS_H */
