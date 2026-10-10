/*
	File:		host/HostFontProvider.cpp

	Contains:	The host's fonts given to the Newton - HostFontProvider.h.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "HostFontProvider.h"
#include "HostFontRaster.h"
#include "HostFonts.h"
#include "Frames.h"

// a face of the platform's
class TRasterFace : public THostFace
{
public:
					TRasterFace(void* face) : fFace(face) { }
	virtual			~TRasterFace() { HostFontRasterCloseFace(fFace); }

	virtual void	GetMetrics(HostFaceMetrics* metrics)
	{
		HostFontRasterMetrics m;
		HostFontRasterGetMetrics(fFace, &m);
		metrics->ascent = m.ascent;
		metrics->descent = m.descent;
		metrics->leading = m.leading;
		metrics->widMax = m.widMax;
		metrics->top = m.top;
		metrics->bottom = m.bottom;
		metrics->left = m.left;
	}

	virtual bool	GetGlyph(UniChar ch, long depth, HostGlyph* glyph)
	{
		HostFontRasterGlyph g;
		if (!HostFontRasterGetGlyph(fFace, (uint16_t) ch, (int) depth, &g))
			return false;
		glyph->width = g.width;
		glyph->height = g.height;
		glyph->bearingX = g.bearingX;
		glyph->bearingY = g.bearingY;
		glyph->advance = (Fixed) g.advance;
		glyph->depth = g.depth;
		glyph->rowBytes = g.rowBytes;
		glyph->bits = g.bits;
		return true;
	}

private:
	void*			fFace;
};


// the platform's families
class TRasterProvider : public THostFontProvider
{
public:
	virtual const char*	Name(void)			{ return "host"; }
	virtual long	CountFamilies(void)		{ return HostFontRasterCount(); }
	virtual bool	GetFamilyName(long index, UniChar* name, long size)	{ return HostFontRasterName((int) index, (uint16_t*) name, (int) size); }
	virtual bool	HasFamily(const UniChar* family)	{ return HostFontRasterFaces((const uint16_t*) family) != 0; }
	virtual long	Faces(const UniChar* family)		{ return HostFontRasterFaces((const uint16_t*) family); }
	virtual THostFace*	OpenFace(const UniChar* family, long face, long pixelsX, long pixelsY)
	{
		void* f = HostFontRasterOpenFace((const uint16_t*) family, (int) face, (int) pixelsX, (int) pixelsY);
		return f != nil ? new TRasterFace(f) : nil;
	}
	virtual const char* const*	DefaultFamilies(void)	{ return HostFontRasterDefaults(); }
};

static TRasterProvider	gRasterProvider;
static bool				gAvailable = false;
static const char*		gChosen = nil;		// NEWTON_HOST_FONTS's list (nil: the usual families)


bool
HostFontsAvailable(void)
{
	return gAvailable;
}


const char*
HostFontsChosen(void)
{
	return gChosen;
}


void
HostInstallFonts(void)
{
	RegisterHostFontNatives();
	gAvailable = HostFontRasterOpen();
	if (!gAvailable)
		return;
	SetHostFontProvider(&gRasterProvider);
	const char* env = getenv("NEWTON_HOST_FONTS");
	if (env == nil || strcmp(env, "0") == 0)
		return;
	gChosen = (env[0] == 0 || strcmp(env, "1") == 0) ? nil : env;
	long added = AddHostFontFamilies(gChosen);
	fprintf(stderr, "[host] %ld of the host's font families added (NEWTON_HOST_FONTS)\n", added);
}
