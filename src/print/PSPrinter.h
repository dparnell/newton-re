/*
	File:		print/PSPrinter.h

	Contains:	The PostScript printer: TPSPrinter, the ROM's TPrinter for
				PostScript printers, and TPSPrinterDriver, the seam its
				connection to the printer plugs into.

				Where TDotPrinter draws each page into a bitmap a band at a
				time, TPSPrinter has no bitmap at all: the printer port's
				drawing procs are its PostScript bottlenecks (PrStdRect,
				PrStdText and the rest, PSBottlenecks.cpp), which turn each
				QuickDraw verb into PostScript - a rectangle into
				"top left bottom right StdRect fill", a frame into a path
				stroked with a pen of the port's pen size, text into
				"(...) show" in the font's PostScript name, a bitmap into
				hex for the prolog's `bimage` - and send it to the printer
				as it goes.  The page is drawn once (RepeatPage is false);
				the document is DSC: a header naming the title, the user and
				the date, the prolog (gPostscriptHeader, the procedures the
				bottlenecks use), each page between %%Page and showpage, and
				a trailer giving the page count.

				TPSPrinterDriver carries the text there: SendPSText,
				SendPSBinary and RecvPSText, and the job's open/close/page
				brackets.  The ROM's one driver is TPSPAPDriver, PostScript
				over AppleTalk's PAP (NOT YET: AppleTalk is not
				reconstructed); the host's is print/host/HostPSDriver.h,
				PostScript over a comm endpoint to a printer on the
				network.

	Not in the DDK's headers.  TPSPrinterDriver's interface follows the
	ROM's glue at 0x003881bc-0x00388250 and TPSPAPDriver's class info
	(tools/newton-rom/analysis/classinfo.py --name TPSPAPDriver);
	TPSPrinter's fields are named from their uses in 0x00155f58-0x0015a0ac
	and 0x0021ac14-0x0021bc00.  The offsets in the comments are the ROM's.
*/

#ifndef __PRINT_PSPRINTER_H
#define __PRINT_PSPRINTER_H

#ifndef __PRINT_PRINTER_H
#include "print/Printer.h"
#endif

#include "Frames.h"
#include "TextObject.h"

struct curve;
struct path;
struct paths;
struct StyleRecord;


/*------------------------------------------------------------------------------
	T P S P r i n t e r D r i v e r
	A PostScript printer's connection.  Like TDotPrinterDriver it has no
	New: it is made by name and its fields filled in by
	TPSPrinter::Constructor.
------------------------------------------------------------------------------*/

PROTOCOL TPSPrinterDriver : public TProtocol
{
public:
	void				Delete();											// ROM 0x003881bc Delete__16TPSPrinterDriverFv
	VIRTUAL NewtonErr	Open() ENDVIRTUAL;									// ROM 0x003881d8 Open__16TPSPrinterDriverFv
	VIRTUAL NewtonErr	Close(Boolean abort) ENDVIRTUAL;					// ROM 0x003881e4 Close__16TPSPrinterDriverFUc
	VIRTUAL NewtonErr	OpenPage() ENDVIRTUAL;								// ROM 0x003881f0 OpenPage__16TPSPrinterDriverFv
	VIRTUAL NewtonErr	ClosePage() ENDVIRTUAL;								// ROM 0x003881fc ClosePage__16TPSPrinterDriverFv
	VIRTUAL void		CancelJob(Boolean asyncCancel) ENDVIRTUAL;			// ROM 0x00388208 CancelJob__16TPSPrinterDriverFUc
	VIRTUAL PrProblemResolution	IsProblemResolved() ENDVIRTUAL;				// ROM 0x00388214 IsProblemResolved__16TPSPrinterDriverFv
	VIRTUAL NewtonErr	GetStatus() ENDVIRTUAL;								// ROM 0x00388220 GetStatus__16TPSPrinterDriverFv
	// a NUL-terminated text sent (`sent`: how much went, when it answers
	// an error part way); `eoj` marks the end of the job for PAP
	VIRTUAL NewtonErr	SendPSText(char* text, ULong& sent, Boolean eoj) ENDVIRTUAL;	// ROM 0x0038822c SendPSText__16TPSPrinterDriverFPcRUlUc
	VIRTUAL NewtonErr	RepeatPSPage() ENDVIRTUAL;							// ROM 0x00388238 RepeatPSPage__16TPSPrinterDriverFv
	VIRTUAL NewtonErr	SendPSBinary(char* data, ULong size, ULong& sent) ENDVIRTUAL;	// ROM 0x00388244 SendPSBinary__16TPSPrinterDriverFPcUlRUl
	VIRTUAL NewtonErr	RecvPSText(char* text, ULong& size) ENDVIRTUAL;		// ROM 0x00388250 RecvPSText__16TPSPrinterDriverFPcRUl

	PrintConnect*		fConnect;		// +0x10  the job's connection frame, paper and orientation
	TPrinter*			fPrinter;		// +0x14  the imaging engine driving it
};


/*------------------------------------------------------------------------------
	T P S P r i n t e r
	The ROM's TPrinter for PostScript printers.
------------------------------------------------------------------------------*/

enum { kPSPrinterBufferSize = 0x100 };

PROTOCOL TPSPrinter : public TPrinter
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TPSPrinter);

	// (the class info's default New does nothing: Constructor sets it up)
	TPSPrinter*		New() { return this; }

	NewtonErr		Constructor(char* driverName);				// ROM 0x0021b9b0 Constructor__10TPSPrinterFPc
	void			Delete();									// ROM 0x0021bb10 Delete__10TPSPrinterFv
	NewtonErr		Open(RefArg connectInfo);					// ROM 0x0021af40 Open__10TPSPrinterFRC6RefVar
	NewtonErr		Close();									// ROM 0x0021b07c Close__10TPSPrinterFv
	NewtonErr		OpenPage();									// ROM 0x0021b0fc OpenPage__10TPSPrinterFv
	NewtonErr		ClosePage();								// ROM 0x0021b930 ClosePage__10TPSPrinterFv
	Boolean			RepeatPage();								// ROM 0x0021b2ec RepeatPage__10TPSPrinterFv
	void			CancelJob(Boolean asyncCancel);				// ROM 0x0021ac1c CancelJob__10TPSPrinterFUc
	PrProblemResolution	IsProblemResolved();					// ROM 0x0021aecc IsProblemResolved__10TPSPrinterFv
	void			SetPortraitOrientation(Boolean portrait);	// ROM 0x0021b998 SetPortraitOrientation__10TPSPrinterFUc
	NewtonErr		FaxEndPage(long pageCount);					// ROM 0x0021b9a8 FaxEndPage__10TPSPrinterFl

	// the job
	void			GetPageInfo(PrPageInfo* info);				// ROM 0x0021ad38 GetPageInfo__10TPSPrinterFP10PrPageInfo
	void			GetDocTitle(RefArg connectInfo, char* title);	// ROM 0x0021acc0 GetDocTitle__10TPSPrinterFRC6RefVarPc
	void			GetUserName(RefArg connectInfo, char* name);	// ROM 0x0021adf0 GetUserName__10TPSPrinterFRC6RefVarPc
	void			MakeTextPSFriendly(char* text);				// ROM 0x0021aed4 MakeTextPSFriendly__10TPSPrinterFPc
	void			SetupFontMapping(RefArg family, RefArg face);	// ROM 0x0021b2f4 SetupFontMapping__10TPSPrinterFRC6RefVarT1
	void			SendPSHeader(RefArg connectInfo);			// ROM 0x0021b398 SendPSHeader__10TPSPrinterFRC6RefVar
	void			SendPSTrailer();							// ROM 0x0021b7b8 SendPSTrailer__10TPSPrinterFv
	void			SendPSText(char* text, Boolean eoj);		// ROM 0x0021b830 SendPSText__10TPSPrinterFPcUc
	void			SendPSBinary(char* data, ULong size);		// ROM 0x0021b89c SendPSBinary__10TPSPrinterFPcUl

	// the errors and problems
	NewtonErr		GetStatus();								// ROM 0x0021add0 GetStatus__10TPSPrinterFv
	Boolean			ErrorIsFatal(long error);					// ROM 0x0021bc00 ErrorIsFatal__10TPSPrinterFl
	Boolean			ErrorIsProblem(long error);					// ROM 0x0021ac98 ErrorIsProblem__10TPSPrinterFl
	Boolean			ErrorIsPrintingError(long error);			// ROM 0x0021ac50 ErrorIsPrintingError__10TPSPrinterFl
	Boolean			ProblemIsFatal();							// ROM 0x0021b2e4 ProblemIsFatal__10TPSPrinterFv
	void			SetSoftError(long error);					// ROM 0x0021b908 SetSoftError__10TPSPrinterFl
	PrProblemResolution	HandleError(long error);				// ROM 0x0021ae54 HandleError__10TPSPrinterFl
	PrProblemResolution	DoHandleProblem(long problem);			// ROM 0x0021bb6c DoHandleProblem__10TPSPrinterFl
	Boolean			ContinueIO();								// ROM 0x0021ba80 ContinueIO__10TPSPrinterFv
	Boolean			ContinueRendering();						// ROM 0x0021bab8 ContinueRendering__10TPSPrinterFv

	// the drawing (PSBottlenecks.cpp)
	Boolean			SetupPSBottlenecks(GrafPort* port);			// ROM 0x00156f34 SetupPSBottlenecks__10TPSPrinterFP8GrafPort
	void			TearDownPSBottlenecks(GrafPort* port);		// ROM 0x0015706c TearDownPSBottlenecks__10TPSPrinterFP8GrafPort
	char*			FixedToString(Fixed value, char* string);	// ROM 0x001568c0 FixedToString__10TPSPrinterFlPc
	void			OffsetFixedPoint(FPoint& pt, Fixed dh, Fixed dv);	// ROM 0x00156970 OffsetFixedPoint__10TPSPrinterFR6FPointlT2
	void			PositionPen(long h, long v, Point pen);		// ROM 0x00156990 PositionPen__10TPSPrinterFlT15Point
	void			SetupPen(Point pen);						// ROM 0x00156a54 SetupPen__10TPSPrinterF5Point
	void			SetClip(GrafPort* port);					// ROM 0x00156aec SetClip__10TPSPrinterFP8GrafPort
	void			SetGrayLevel(GrafVerb verb, GrafPort* port);	// ROM 0x00156bc8 SetGrayLevel__10TPSPrinterFUcP8GrafPort
	void			DoSetGray(UChar gray);						// ROM 0x00157f68 DoSetGray__10TPSPrinterFUc
	long			CountBitsInPattern(PatternHandle pattern);	// ROM 0x00156778 CountBitsInPattern__10TPSPrinterFPP8PixelMap
	Boolean			GetDoPatternFill();							// ROM 0x00157a60 GetDoPatternFill__10TPSPrinterFv
	long			SetLineWidth(Point from, Point to, Point pen);	// ROM 0x00156e04 SetLineWidth__10TPSPrinterF5PointN21
	void			ResetLineWidth(long width);					// ROM 0x00156a28 ResetLineWidth__10TPSPrinterFl
	void			SetupLineStrings(const FPoint& a, const FPoint& b, Point pen, char* ax, char* ay, char* by, char* bxh, char* byv, char* ayv);	// ROM 0x00156e90 SetupLineStrings__10TPSPrinterFRC6FPointT15PointPcN54
	void			DoPSLine(const FPoint& to);					// ROM 0x00156ffc DoPSLine__10TPSPrinterFRC6FPoint
	void			Draw1QDLine(const FPoint& from, Point pen, const FPoint& to);	// ROM 0x00159b28 Draw1QDLine__10TPSPrinterFRC6FPoint5PointT1
	void			SendRectangle(Rect* r, Point pen);			// ROM 0x00156524 SendRectangle__10TPSPrinterFP4Rect5Point
	void			EmitInsetRect(Rect* r, Point pen);			// ROM 0x0015666c EmitInsetRect__10TPSPrinterFP4Rect5Point
	void			DrawFillRect(Rect* r);						// ROM 0x001562ec DrawFillRect__10TPSPrinterFP4Rect
	void			DrawFrameRect(Rect* r, Point pen);			// ROM 0x001564d8 DrawFrameRect__10TPSPrinterFP4Rect5Point
	void			DrawFillOval(Rect* r);						// ROM 0x00156114 DrawFillOval__10TPSPrinterFP4Rect
	void			DrawFrameOval(Rect* r, Point pen);			// ROM 0x0015639c DrawFrameOval__10TPSPrinterFP4Rect5Point
	void			DrawAnyArc(Rect* r, Point pen, long startAngle, long endAngle, Boolean frame, Boolean partOfPath);	// ROM 0x00159ee0 DrawAnyArc__10TPSPrinterFP4Rect5PointlT3UcT5
	void			DrawFillPoly(PolyHandle poly);				// ROM 0x001561cc DrawFillPoly__10TPSPrinterFPP7Polygon
	void			DrawFramePoly(PolyHandle poly, Point pen);	// ROM 0x001563fc DrawFramePoly__10TPSPrinterFPP7Polygon5Point
	void			Draw1Curve(curve* c, Point pen, Boolean frame);	// ROM 0x001595d0 Draw1Curve__10TPSPrinterFP5curve5PointUc
	void			DrawAnyCurve(curve* c, Point pen, Boolean frame);	// ROM 0x00155f78 DrawAnyCurve__10TPSPrinterFP5curve5PointUc
	Boolean			CheckEmptyPath(path* contour);				// ROM 0x0015976c CheckEmptyPath__10TPSPrinterFP4path
	path*			Draw1Path(path* contour, Point pen, Boolean frame);	// ROM 0x001597c0 Draw1Path__10TPSPrinterFP4path5PointUc
	void			DrawAnyPath(paths** p, Point pen, Boolean frame);	// ROM 0x001560a8 DrawAnyPath__10TPSPrinterFPP5paths5PointUc

	// the text
	UChar			SetupPSTextMode(TextObjectRef text);		// ROM 0x00157090 SetupPSTextMode__10TPSPrinterFl
	void			DoSelectFont(Boolean macEncoding);			// ROM 0x001570e0 DoSelectFont__10TPSPrinterFUc
	long			UnicodeToDestmap(char* chars, long encoding);	// ROM 0x001579fc UnicodeToDestmap__10TPSPrinterFPcl
	void			FlushBuffer(char* chars, long& start, long end, long charSize, StyleRecord* style, Fixed charExtra, Fixed spaceExtra);	// ROM 0x00157414 FlushBuffer__10TPSPrinterFPcRllT3P11StyleRecordN23
	void			HandleCharacters(char* chars, long index, long& start, long charSize, long* widths, StyleRecord* style, Fixed charExtra, Fixed spaceExtra);	// ROM 0x00157628 HandleCharacters__10TPSPrinterFPclRlT2PlP11StyleRecordN22
	void			EmitText(long count, char* chars, long charSize, long* widths, StyleRecord* style, Fixed charExtra, Fixed spaceExtra);	// ROM 0x001577f0 EmitText__10TPSPrinterFlPcT1PlP11StyleRecordN21

	TPSPrinterDriver*	fDriver;		// +0x98
	long			fProblem;			// +0x9c  the problem last put to the user
	Boolean			fProblemFatal;		// +0xa0  the user cancelled a problem
	UChar			fGray;				// +0xa1  the gray last set, 0 (black) .. 64 (white)
	RefStruct		fFont;				// +0xa4  the font family last selected (a screen family frame, or the Symbol font's)
	Fixed			fFontSize;			// +0xa8  ... its size
	long			fFontFace;			// +0xac  ... and face
	long			fUnused[5];			// +0xb0
	Point			fPenSize;			// +0xc4  the pen last sent ("h v Pen")
	Point			fFillPen;			// +0xc8  the pen a fill is made with: none
	long			fPageCount;			// +0xcc
	Rect			fClipBox;			// +0xd0  the clip and visible regions' boxes the clip path was made from
	Rect			fVisBox;			// +0xd8
	Point			fPageSize;			// +0xe0  the page in the printer's dots
	long			fLevel;				// +0xe4  always 1 (a bitmap deeper than 8 bits is not printed)
	char			fBuffer[kPSPrinterBufferSize + 16];	// +0xe8  the PostScript being put together
														//  (DEVIATION: 16 bytes of slack - FlushBuffer can run 6 past the ROM's 0x100, into fEncoding)
	long			fEncoding;			// +0x1e8  the font's encoding: 1 Mac, 6 Shift-JIS (its prencoding)
	Boolean			fDoPatternFill;		// +0x1ec  the gray is a pattern: fill with PatternFill
};


// The PostScript bottlenecks: the printer port's drawing procs
void		PrStdText(TextObjectRef text, Fixed hScale, Fixed vScale);		// ROM 0x00157a68 PrStdText__FlN21
void		PrStdLine(Point to);											// ROM 0x00157de4 PrStdLine__F5Point
void		PrStdRect(GrafVerb verb, Rect* r);								// ROM 0x00157fc8 PrStdRect__FUcP4Rect
void		PrStdRRect(GrafVerb verb, Rect* r, long ovalWidth, long ovalHeight);	// ROM 0x001580e8 PrStdRRect__FUcP4RectlT3
void		PrStdOval(GrafVerb verb, Rect* r);								// ROM 0x00158524 PrStdOval__FUcP4Rect
void		PrStdArc(GrafVerb verb, Rect* r, long startAngle, long arcAngle);	// ROM 0x00158644 PrStdArc__FUcP4RectlT3
void		PrStdPoly(GrafVerb verb, PolyHandle poly);						// ROM 0x001587f4 PrStdPoly__FUcPP7Polygon
void		PrStdRgn(GrafVerb verb, RgnHandle rgn);							// ROM 0x00158920 PrStdRgn__FUcPP6Region
void		PrStdBits(PixelMap* src, Rect* srcRect, Rect* dstRect, long mode, RgnHandle mask);	// ROM 0x0015896c PrStdBits__FP8PixelMapP4RectT2lPP6Region
void		PrStdCurve(GrafVerb verb, curve* c);							// ROM 0x00159290 PrStdCurve__FUcP5curve
void		PrStdPaths(GrafVerb verb, pathsHandle p);						// ROM 0x0015943c PrStdPaths__FUcPP5paths
void		PrStdComment(short kind, short dataSize, Handle data);			// ROM 0x00159768 PrStdComment__FsT1PPc


// The ROM's constant text and tables (PSPrinterTables.cpp, generated)
struct IntCString
{
	long		fCode;
	const char*	fText;
};

extern const char			gPostscriptHeader[3502];	// the prolog's first half: DSC comments, the drawing procedures, the Mac encoding
extern const char			gPostscriptHeader2[2414];	// ... and its second: the patterns, Level 1 and Level 2
extern const char			gOpenPageHeader[102];
extern const char			gPSLandscape[12];
extern const char			gClosePageHeader[48];
extern const unsigned short	gPSBinToHex[256];			// a byte as two hex digits, big-endian
extern const unsigned int	gFourBitTable[256];			// each bit of a byte made four (qd/StretchTables.cpp)
extern const IntCString		gPSStatusStrings[15];		// a PostScript printer's status words and the problem each is


/*------------------------------------------------------------------------------
	T P S P A P D r i v e r
	The ROM's PostScript driver, over AppleTalk's Printer Access Protocol:
	the driver of the network PostScript printer (the LaserWriter the Print
	slip's "Choose Network LaserWriter" finds by NBP).  Reconstructed are
	the calls that are not AppleTalk's - the page brackets, the problem's
	resolution, the cancel, and the reading of a PostScript printer's status
	message, which a printer answers the same way over any connection
	("%%[ status: busy; source: AppleTalk ]%%", "%%[ PrinterError: out of
	paper ]%%").
	DEVIATION (the owner's decision): AppleTalk is not reconstructed, and
	PAP is replaced by IPP.  The host's stand-ins for the PAP calls
	(print/host/HostPAPDriver.cpp) find the printer the chooser picked - the
	printer frame's printerName, "name:LaserWriter@zone" - among the IPP
	printers the host's DNS-SD browse finds (the NBP lookup the chooser made
	was answered from the same browse, print/host/HostNetworkPrinters.cpp)
	and send the job to it by IPP.
------------------------------------------------------------------------------*/

PROTOCOL TPSPAPDriver : public TPSPrinterDriver
{
public:
	PROTOCOL_IMPL_HEADER_MACRO(TPSPAPDriver);

	TPSPAPDriver*	New();													// (host)
	void			Delete();												// (host stand-in for ROM 0x0021a988)

	NewtonErr		Open();													// (host stand-in for ROM 0x0021a424)
	NewtonErr		Close(Boolean abort);									// (host stand-in for ROM 0x0021a8a8)
	NewtonErr		OpenPage();												// ROM 0x0021a654 OpenPage__12TPSPAPDriverFv
	NewtonErr		ClosePage();											// ROM 0x0021a958 ClosePage__12TPSPAPDriverFv
	void			CancelJob(Boolean asyncCancel);							// ROM 0x0021a408 CancelJob__12TPSPAPDriverFUc
	PrProblemResolution	IsProblemResolved();								// ROM 0x0021abf0 IsProblemResolved__12TPSPAPDriverFv
	NewtonErr		GetStatus();											// (host stand-in for ROM 0x0021a9e8)
	NewtonErr		SendPSText(char* text, ULong& sent, Boolean eoj);		// (host stand-in for ROM 0x0021a67c)
	NewtonErr		RepeatPSPage();											// ROM 0x0021a674 RepeatPSPage__12TPSPAPDriverFv
	NewtonErr		SendPSBinary(char* data, ULong size, ULong& sent);		// (host stand-in for ROM 0x0021a75c)
	NewtonErr		RecvPSText(char* text, ULong& size);					// (host stand-in for ROM 0x0021a830)

	NewtonErr		InterpretPAPStatusString(unsigned char* status, Boolean idleIsFine);	// ROM 0x0021aa84 InterpretPAPStatusString__12TPSPAPDriverFP10TString255Uc
	NewtonErr		InterpretPAPString(char* status, Boolean idleIsFine);	// ROM 0x0021aabc InterpretPAPString__12TPSPAPDriverFPcUc

	void*			fPAP;				// +0x18  the TPAPInterface
	NewtonErr		fError;				// +0x1c
	ULong			fSent;				// +0x20  writes since the status was last asked
	char			fReply[0x200];		// +0x24  the last reply read (a Pascal string at +0x27)
	Boolean			fClosedAppleTalk;	// +0x224
	Boolean			fCancelled;			// +0x225
	void*			fHostConnection;	// (host) the IPP connection standing in for fPAP
};

#endif	/* __PRINT_PSPRINTER_H */
