/*
	File:		views/SplashScreen.h

	Contains:	The splash graphic: a picture the machine's maker supplies
				through the TSplashScreenInfo protocol (the implementation
				is looked for by name, "TMainSplashScreenInfo"), drawn
				centred in a box.  The MP2x00's own ROM registers no
				implementation, so there is no graphic and the script that
				asks (DrawGraphic, protoSplash... 0x5b7079's viewDrawScript)
				draws its own default picture instead; a ROM extension, or
				a host, may register one.

				The system's version as text (VersionString, which the boot's
				splash screen and Gestalt's romVersionString show) is here
				too, with the TVersionString protocol a maker may register
				("TMainVersionString") to say it differently; the MP2x00's
				own ROM registers none.

	Reconstructed from the MP2x00 US ROM (0x00146cb8-0x001470d0,
	0x003857b4-0x003858a0); each function cites its origin.
*/

#ifndef __SPLASHSCREEN_H
#define __SPLASHSCREEN_H

#include "Protocols.h"
#include "NewtQD.h"
#include "NewtonGestalt.h"

PROTOCOL TSplashScreenInfo : public TProtocol
{
public:
	static TSplashScreenInfo*	New(const char* implementation);	// ROM 0x00385824 New__17TSplashScreenInfoSFPc
	void			Delete();										// ROM 0x00385850 Delete__17TSplashScreenInfoFv

	VIRTUAL long	GetBits(const Picture** picture) ENDVIRTUAL;	// ROM 0x0038586c GetBits__17TSplashScreenInfoFPPC7Picture - the picture (big-endian, its frame at +2); ==> whether there is one
	VIRTUAL long	GetText(UniChar* text) ENDVIRTUAL;				// ROM 0x00385878 GetText__17TSplashScreenInfoFPUs
};

// the maker's splash picture drawn centred in the box, if there is one;
// ==> the TSplashScreenInfo made (the caller's to Delete), nil for none
TSplashScreenInfo*	DrawSplashGraphic(UChar* drawn, Rect box);		// ROM 0x00146fc8 DrawSplashGraphic__FPUc5TRect

PROTOCOL TVersionString : public TProtocol
{
public:
	static TVersionString*	New(const char* implementation);	// ROM 0x003857b4 New__14TVersionStringSFPc
	void			Delete();									// ROM 0x003857e0 Delete__14TVersionStringFv

	VIRTUAL void	VersionString(UniChar* text) ENDVIRTUAL;	// ROM 0x003857fc VersionString__14TVersionStringFPUs - the text rewritten as the maker would have it
};

// The version the system info describes as text ("2.1 (717006)" for this
// ROM's own), into text.
void	VersionString(TGestaltSystemInfo* info, UniChar* text);	// ROM 0x00146cb8 VersionString__FP18TGestaltSystemInfoPUs

#endif	/* __SPLASHSCREEN_H */
