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
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "Unicode.h"

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
static char*			gChosen = nil;		// the families chosen, comma-separated UTF-8 ("": none; nil: the usual ones)


static void
SetChosen(const char* names)
{
	free(gChosen);
	gChosen = nil;
	if (names != nil)
	{
		gChosen = (char*) malloc(strlen(names) + 1);
		if (gChosen != nil)
			strcpy(gChosen, names);
	}
}


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


/*------------------------------------------------------------------------------
	The families chosen (the Host panel's chooser)
------------------------------------------------------------------------------*/

// a name as UTF-8 onto the end of out (no more than size bytes in all)
static void
AppendUTF8(char* out, size_t size, const UniChar* name)
{
	size_t n = strlen(out);
	for (const UniChar* c = name; *c != 0 && n + 4 < size; c++)
	{
		UniChar u = *c;
		if (u < 0x80)
			out[n++] = (char) u;
		else if (u < 0x800)
		{
			out[n++] = (char) (0xc0 | (u >> 6));
			out[n++] = (char) (0x80 | (u & 0x3f));
		}
		else
		{
			out[n++] = (char) (0xe0 | (u >> 12));
			out[n++] = (char) (0x80 | ((u >> 6) & 0x3f));
			out[n++] = (char) (0x80 | (u & 0x3f));
		}
	}
	out[n] = 0;
}


// HostFontsChosen(): the names of the families the host's fonts add when
// they are turned on - those chosen, or the host's usual ones
static Ref
FHostFontsChosen(RefArg /*rcvr*/)
{
	RefVar list(MakeArray(0));
	if (!gAvailable)
		return list;
	if (gChosen == nil)
	{
		const char* const* defaults = gRasterProvider.DefaultFamilies();
		for (long i = 0; defaults != nil && defaults[i] != nil; i++)
			AddArraySlot(list, RefVar(MakeString(defaults[i])));
		return list;
	}
	// (the list is UTF-8 and comma-separated: each name made a string as
	//  AddHostFontFamilies reads it)
	const char* s = gChosen;
	while (*s != 0)
	{
		const char* end = strchr(s, ',');
		size_t length = end != nil ? (size_t) (end - s) : strlen(s);
		UniChar name[64];
		size_t n = 0;
		for (size_t i = 0; i < length && n < 63; )
		{
			unsigned char c = (unsigned char) s[i++];
			ULong u = c;
			long extra = c < 0x80 ? 0 : c < 0xe0 ? 1 : c < 0xf0 ? 2 : 3;
			if (extra > 0)
				u = c & (0x3f >> extra);
			for (long k = 0; k < extra && i < length; k++)
				u = (u << 6) | ((unsigned char) s[i++] & 0x3f);
			name[n++] = (UniChar) (u > 0xffff ? 0xfffd : u);
		}
		name[n] = 0;
		if (n > 0)
			AddArraySlot(list, RefVar(MakeString(name)));
		if (end == nil)
			break;
		s = end + 1;
	}
	return list;
}


// HostSetFontFamilies(names): the families chosen (an array of names; nil
// the host's usual ones), put into the font menus at once when the host's
// fonts are on.  ==> how many of them are in the menus
static Ref
FHostSetFontFamilies(RefArg /*rcvr*/, RefArg names)
{
	if (!gAvailable)
		return NILREF;
	if (IsArray(names))
	{
		char joined[8192] = "";
		for (ArrayIndex i = 0, count = Length(names); i < count; i++)
		{
			RefVar name(GetArraySlotRef(names, i));
			if (!IsString(name))
				continue;
			const UniChar* u = GetCString(name);
			bool comma = false;
			for (const UniChar* c = u; *c != 0; c++)
				if (*c == ',')
					comma = true;
			if (comma || *u == 0)
				continue;						// (a name the list cannot carry)
			if (joined[0] != 0 && strlen(joined) + 1 < sizeof(joined))
				strcat(joined, ",");
			AppendUTF8(joined, sizeof(joined), u);
		}
		SetChosen(joined);
	}
	else
		SetChosen(nil);
	long added = 0;
	if (HostFontFamiliesAdded())
	{
		RemoveHostFontFamilies();
		added = AddHostFontFamilies(gChosen);
		fprintf(stderr, "[host] the host's font families chosen: %ld in the font menus\n", added);
	}
	return MAKEINT(added);
}


void
HostInstallFonts(void)
{
	RegisterHostFontNatives();
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostFontsChosen")), RefVar(MakeCFunction((void*) FHostFontsChosen, 0, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostSetFontFamilies")), RefVar(MakeCFunction((void*) FHostSetFontFamilies, 1, nil)));
	gAvailable = HostFontRasterOpen();
	if (!gAvailable)
		return;
	SetHostFontProvider(&gRasterProvider);
	const char* env = getenv("NEWTON_HOST_FONTS");
	if (env == nil || strcmp(env, "0") == 0)
		return;
	SetChosen((env[0] == 0 || strcmp(env, "1") == 0) ? nil : env);
	long added = AddHostFontFamilies(gChosen);
	fprintf(stderr, "[host] %ld of the host's font families added (NEWTON_HOST_FONTS)\n", added);
}
