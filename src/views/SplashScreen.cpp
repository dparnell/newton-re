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
#include "Unicode.h"


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


// ROM 0x003857b4 New__14TVersionStringSFPc (the protocol glue)
TVersionString*
TVersionString::New(const char* implementation)
{
	TVersionString* p = (TVersionString*) AllocInstanceByName("TVersionString", implementation);
	return p != nil ? (TVersionString*) p->GlueNew() : nil;
}


// ROM 0x003857e0 Delete__14TVersionStringFv (the protocol glue)
void
TVersionString::Delete()
{
	GlueDelete();
}


// ROM 0x00146cb8 VersionString__FP18TGestaltSystemInfoPUs
// The shipping 2.1 (ROM version 0x20002, stage 0x8000) is named outright.
// Anything else is spelt out of the words: the major version from the
// ROM version's nibbles 6..4 (leading noughts dropped), a point and the
// minor digits (nibbles 3..0 from the highest that is not nought, the
// last always), the stage's letter (0 "AS", 0x20 d, 0x40 a, 0x60 b, none
// for 0x80 or anything else) and number (its low byte as two nibbles, the
// first dropped when nought; none at all for a stage of 0x80), and a
// point and the patch version as two digits (more than 99 is 99).  A
// maker's TMainVersionString, if one is registered, has the last word.
void
VersionString(TGestaltSystemInfo* info, UniChar* text)
{
	if (info->fROMVersion == 0x20002 && info->fROMStage == 0x8000)
	{
		ConvertToUnicode("2.1 (717006)", text, kMacRomanEncoding, 0x7fffffff);
		return;
	}
	ULong nibble[7];
	for (long i = 0; i < 7; i++)
		nibble[i] = ((ULong32) info->fROMVersion >> (i * 4)) & 0xf;
	long n = 0;
	if (nibble[6] != 0)
	{
		text[n++] = (UniChar) (nibble[6] + '0');
		text[n++] = (UniChar) (nibble[5] + '0');
		text[n++] = (UniChar) (nibble[4] + '0');
	}
	else if (nibble[5] != 0)
	{
		text[n++] = (UniChar) (nibble[5] + '0');
		text[n++] = (UniChar) (nibble[4] + '0');
	}
	else
		text[n++] = (UniChar) (nibble[4] + '0');
	if (((ULong32) info->fROMVersion & 0xffff) != 0)
	{
		text[n++] = '.';
		long digit = nibble[3] != 0 ? 3 : nibble[2] != 0 ? 2 : nibble[1] != 0 ? 1 : 0;
		for ( ; digit >= 0; digit--)
			text[n++] = (UniChar) (nibble[digit] + '0');
	}
	ULong stage = ((ULong32) info->fROMStage >> 8) & 0xff;
	if (stage != 0x80)
	{
		if (stage == 0)
		{
			text[n++] = 'A';
			text[n++] = 'S';
		}
		else if (stage == 0x20)
			text[n++] = 'd';
		else if (stage == 0x40)
			text[n++] = 'a';
		else if (stage == 0x60)
			text[n++] = 'b';
		if (((ULong32) info->fROMStage & 0xff) != 0)
		{
			ULong tens = ((ULong32) info->fROMStage >> 4) & 0xf;
			if (tens != 0)
				text[n++] = (UniChar) (tens + '0');
			text[n++] = (UniChar) (((ULong32) info->fROMStage & 0xf) + '0');
		}
	}
	text[n++] = '.';
	Long32 patch = (Long32) info->fPatchVersion;
	if (patch > 99)
		patch = 99;
	text[n++] = (UniChar) (patch / 10 + '0');
	text[n++] = (UniChar) (patch % 10 + '0');
	text[n] = 0;
	TVersionString* maker = (TVersionString*) NewByName("TVersionString", "TMainVersionString");
	if (maker != nil)
	{
		maker->VersionString(text);
		maker->Delete();
	}
}
