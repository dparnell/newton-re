/*
	File:		views/SplashScreen.cpp

	Contains:	The splash graphic (SplashScreen.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "SplashScreen.h"
#include "Pictures.h"		// Justify
#include "PicPlay.h"		// DrawPicture
#include "ViewFlags.h"
#include "ByteOrder.h"


// ROM 0x00385824 New__17TSplashScreenInfoSFPc (the protocol glue)
TSplashScreenInfo*
TSplashScreenInfo::New(const char* implementation)
{
	TSplashScreenInfo* p = (TSplashScreenInfo*) AllocInstanceByName("TSplashScreenInfo", implementation);
	return p != nil ? (TSplashScreenInfo*) p->GlueNew() : nil;
}


// ROM 0x00385850 Delete__17TSplashScreenInfoFv (the protocol glue)
void
TSplashScreenInfo::Delete()
{
	GlueDelete();
}


// ROM 0x00146fc8 DrawSplashGraphic__FPUc5TRect
// The maker's splash picture (NewByName("TSplashScreenInfo",
// "TMainSplashScreenInfo")) drawn at its own size, centred in the box
// (Justify with 6: centred both ways); drawn says whether there was one.
// ==> the info made, for the caller to Delete (nil when there is none).
TSplashScreenInfo*
DrawSplashGraphic(UChar* drawn, Rect box)
{
	*drawn = false;
	TSplashScreenInfo* info = (TSplashScreenInfo*) NewByName("TSplashScreenInfo", "TMainSplashScreenInfo");
	if (info != nil)
	{
		const Picture* picture;
		if (info->GetBits(&picture) != 0)
		{
			// the picture's frame (the ROM: BlockMove of picFrame; a picture's
			// words are big-endian, its frame at +2)
			const unsigned char* bytes = (const unsigned char*) picture;
			Rect frame;
			frame.top = (short) GetBigEndianHalf(bytes + 2);
			frame.left = (short) GetBigEndianHalf(bytes + 4);
			frame.bottom = (short) GetBigEndianHalf(bytes + 6);
			frame.right = (short) GetBigEndianHalf(bytes + 8);
			Justify(&frame, box, 6);
			PicHandle handle = (PicHandle) &picture;
			DrawPicture(handle, &frame, false);
			*drawn = true;
		}
	}
	return info;
}
