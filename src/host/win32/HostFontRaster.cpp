/*
	File:		host/win32/HostFontRaster.cpp

	Contains:	The host's fonts drawn by Windows' GDI - host/HostFontRaster.h.

	The families are the TrueType and OpenType ones EnumFontFamiliesExW
	lists (not the raster fonts, nor the '@' vertical ones); a family's
	faces are the styles it lists for it, by weight and slant, so that GDI
	is never asked for a bold or italic it would fake (the Newton fakes
	those itself, on its slab, as it does for its own fonts).  A face is a
	font made at its size in pixels per em (a negative lfHeight) without
	anti-aliasing; a glyph is GetGlyphOutlineW's GGO_BITMAP - hinted, one
	bit a pixel, rows a DWORD apart - or, asked for eight bits,
	GGO_GRAY8_BITMAP's 65 levels made 0-255.  A character the font lacks
	(GetGlyphIndicesW marks it) is answered false, for the Newton to take
	from its system font.

	Elsewhere (#else) there are no host fonts.
*/

#include "HostFontRaster.h"
#include <string.h>

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdlib.h>
#include <wchar.h>

struct Family
{
	WCHAR	name[LF_FACESIZE];
	int		faces;						// -1 until asked
	LONG	weight[4];					// each face's weight, as listed
};

static HDC		gDC = NULL;
static Family*	gFamilies = NULL;
static int		gCount = 0;
static int		gCapacity = 0;


static int
FamilyIndex(const WCHAR* name)
{
	for (int i = 0; i < gCount; i++)
		if (wcscmp(gFamilies[i].name, name) == 0)
			return i;
	return -1;
}


static int CALLBACK
EnumFamily(const LOGFONTW* lf, const TEXTMETRICW* /*tm*/, DWORD type, LPARAM /*data*/)
{
	if ((type & RASTER_FONTTYPE) != 0 || lf->lfFaceName[0] == L'@' || lf->lfFaceName[0] == 0)
		return 1;
	if (FamilyIndex(lf->lfFaceName) >= 0)
		return 1;
	if (gCount == gCapacity)
	{
		int capacity = gCapacity ? gCapacity * 2 : 256;
		Family* grown = (Family*) realloc(gFamilies, sizeof(Family) * capacity);
		if (grown == NULL)
			return 0;
		gFamilies = grown;
		gCapacity = capacity;
	}
	Family* f = &gFamilies[gCount++];
	memset(f, 0, sizeof(*f));
	wcsncpy(f->name, lf->lfFaceName, LF_FACESIZE - 1);
	f->faces = -1;
	return 1;
}


static int
CompareFamilies(const void* a, const void* b)
{
	return _wcsicmp(((const Family*) a)->name, ((const Family*) b)->name);
}


bool
HostFontRasterOpen(void)
{
	if (gDC != NULL)
		return true;
	gDC = CreateCompatibleDC(NULL);
	if (gDC == NULL)
		return false;
	LOGFONTW lf;
	memset(&lf, 0, sizeof(lf));
	lf.lfCharSet = DEFAULT_CHARSET;
	EnumFontFamiliesExW(gDC, &lf, (FONTENUMPROCW) EnumFamily, 0, 0);
	qsort(gFamilies, gCount, sizeof(Family), CompareFamilies);
	return gCount > 0;
}


int
HostFontRasterCount(void)
{
	return gCount;
}


bool
HostFontRasterName(int index, uint16_t* name, int size)
{
	if (index < 0 || index >= gCount || size < 1)
		return false;
	int n = (int) wcslen(gFamilies[index].name);
	if (n > size - 1)
		return false;
	memcpy(name, gFamilies[index].name, (n + 1) * sizeof(WCHAR));
	return true;
}


static int CALLBACK
EnumStyle(const LOGFONTW* lf, const TEXTMETRICW* /*tm*/, DWORD type, LPARAM data)
{
	if ((type & RASTER_FONTTYPE) != 0)
		return 1;
	Family* f = (Family*) data;
	int face = (lf->lfWeight >= FW_SEMIBOLD ? 1 : 0) | (lf->lfItalic ? 2 : 0);
	// (the weight nearest normal or bold for each face)
	LONG target = (face & 1) ? FW_BOLD : FW_NORMAL;
	if (!(f->faces & (1 << face)) || labs(lf->lfWeight - target) < labs(f->weight[face] - target))
		f->weight[face] = lf->lfWeight;
	f->faces |= 1 << face;
	return 1;
}


static Family*
FindFamily(const uint16_t* family)
{
	int i = FamilyIndex((const WCHAR*) family);
	if (i < 0)
		return NULL;
	Family* f = &gFamilies[i];
	if (f->faces < 0)
	{
		f->faces = 0;
		LOGFONTW lf;
		memset(&lf, 0, sizeof(lf));
		lf.lfCharSet = DEFAULT_CHARSET;
		wcsncpy(lf.lfFaceName, f->name, LF_FACESIZE - 1);
		EnumFontFamiliesExW(gDC, &lf, (FONTENUMPROCW) EnumStyle, (LPARAM) f, 0);
	}
	return f;
}


int
HostFontRasterFaces(const uint16_t* family)
{
	if (gDC == NULL)
		return 0;
	Family* f = FindFamily(family);
	return f != NULL ? f->faces : 0;
}


const char* const*
HostFontRasterDefaults(void)
{
	static const char* const kDefaults[] = {
		"Arial", "Comic Sans MS", "Consolas", "Courier New", "Georgia", "Palatino Linotype",
		"Segoe UI", "Times New Roman", "Trebuchet MS", "Verdana", NULL
	};
	return kDefaults;
}


struct Face
{
	HFONT			font;
	HostFontRasterMetrics metrics;
	unsigned char*	bits;
	DWORD			size;
};


void*
HostFontRasterOpenFace(const uint16_t* family, int face, int pixelsX, int pixelsY)
{
	if (gDC == NULL)
		return NULL;
	Family* f = FindFamily(family);
	if (f == NULL || !(f->faces & (1 << (face & 3))))
		return NULL;
	// (a width only when it is not the height's: lfWidth is an average
	// character's width, not the em's)
	int width = 0;
	if (pixelsX != pixelsY)
	{
		TEXTMETRICW tm;
		HFONT probe = CreateFontW(-pixelsY, 0, 0, 0, f->weight[face & 3], (face & 2) != 0, FALSE, FALSE, DEFAULT_CHARSET,
								  OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, f->name);
		HGDIOBJ old = SelectObject(gDC, probe);
		GetTextMetricsW(gDC, &tm);
		SelectObject(gDC, old);
		DeleteObject(probe);
		width = MulDiv(tm.tmAveCharWidth, pixelsX, pixelsY);
		if (width < 1)
			width = 1;
	}
	HFONT font = CreateFontW(-pixelsY, width, 0, 0, f->weight[face & 3], (face & 2) != 0, FALSE, FALSE, DEFAULT_CHARSET,
							 OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, NONANTIALIASED_QUALITY, DEFAULT_PITCH, f->name);
	if (font == NULL)
		return NULL;
	Face* face_ = (Face*) calloc(1, sizeof(Face));
	if (face_ == NULL)
	{
		DeleteObject(font);
		return NULL;
	}
	face_->font = font;
	HGDIOBJ old = SelectObject(gDC, font);
	TEXTMETRICW tm;
	GetTextMetricsW(gDC, &tm);
	HostFontRasterMetrics* m = &face_->metrics;
	m->ascent = tm.tmAscent;
	m->descent = tm.tmDescent;
	m->leading = tm.tmExternalLeading;
	m->widMax = tm.tmMaxCharWidth;
	m->top = tm.tmAscent;
	m->bottom = -tm.tmDescent;
	m->left = 0;
	UINT otmSize = GetOutlineTextMetricsW(gDC, 0, NULL);
	if (otmSize != 0)
	{
		OUTLINETEXTMETRICW* otm = (OUTLINETEXTMETRICW*) malloc(otmSize);
		if (otm != NULL && GetOutlineTextMetricsW(gDC, otmSize, otm) != 0)
		{
			if (otm->otmrcFontBox.top > m->top)
				m->top = otm->otmrcFontBox.top;
			if (otm->otmrcFontBox.bottom < m->bottom)
				m->bottom = otm->otmrcFontBox.bottom;
			if (otm->otmrcFontBox.left < m->left)
				m->left = otm->otmrcFontBox.left;
		}
		free(otm);
	}
	SelectObject(gDC, old);
	return face_;
}


void
HostFontRasterCloseFace(void* face)
{
	Face* f = (Face*) face;
	if (f == NULL)
		return;
	DeleteObject(f->font);
	free(f->bits);
	free(f);
}


void
HostFontRasterGetMetrics(void* face, HostFontRasterMetrics* metrics)
{
	*metrics = ((Face*) face)->metrics;
}


bool
HostFontRasterGetGlyph(void* face, uint16_t ch, int depth, HostFontRasterGlyph* glyph)
{
	Face* f = (Face*) face;
	HGDIOBJ old = SelectObject(gDC, f->font);
	WCHAR c = (WCHAR) ch;
	WORD index = 0xffff;
	if (GetGlyphIndicesW(gDC, &c, 1, &index, GGI_MARK_NONEXISTING_GLYPHS) == GDI_ERROR || index == 0xffff)
	{
		SelectObject(gDC, old);
		return false;
	}
	static const MAT2 kIdentity = { { 0, 1 }, { 0, 0 }, { 0, 0 }, { 0, 1 } };
	UINT format = (depth == 8 ? GGO_GRAY8_BITMAP : GGO_BITMAP) | GGO_GLYPH_INDEX;
	GLYPHMETRICS gm;
	DWORD size = GetGlyphOutlineW(gDC, index, format, &gm, 0, NULL, &kIdentity);
	if (size == GDI_ERROR)
	{
		SelectObject(gDC, old);
		return false;
	}
	glyph->advance = (int32_t) gm.gmCellIncX << 16;
	glyph->depth = depth == 8 ? 8 : 1;
	if (size == 0)
	{
		// nothing to draw (a space): GDI still gives a one-by-one box
		glyph->width = glyph->height = 0;
		glyph->bearingX = glyph->bearingY = 0;
		glyph->rowBytes = 0;
		glyph->bits = NULL;
		SelectObject(gDC, old);
		return true;
	}
	if (size > f->size)
	{
		unsigned char* grown = (unsigned char*) realloc(f->bits, size);
		if (grown == NULL)
		{
			SelectObject(gDC, old);
			return false;
		}
		f->bits = grown;
		f->size = size;
	}
	GetGlyphOutlineW(gDC, index, format, &gm, size, f->bits, &kIdentity);
	SelectObject(gDC, old);
	glyph->width = (int) gm.gmBlackBoxX;
	glyph->height = (int) gm.gmBlackBoxY;
	glyph->bearingX = gm.gmptGlyphOrigin.x;
	glyph->bearingY = gm.gmptGlyphOrigin.y;
	if (depth == 8)
	{
		glyph->rowBytes = (glyph->width + 3) & ~3;
		for (DWORD i = 0; i < size; i++)
			f->bits[i] = (unsigned char) (f->bits[i] >= 64 ? 255 : f->bits[i] * 4);
	}
	else
		glyph->rowBytes = ((glyph->width + 31) / 32) * 4;
	glyph->bits = f->bits;
	return true;
}

#else

bool	HostFontRasterOpen(void)															{ return false; }
int		HostFontRasterCount(void)															{ return 0; }
bool	HostFontRasterName(int, uint16_t*, int)												{ return false; }
int		HostFontRasterFaces(const uint16_t*)												{ return 0; }
const char* const*	HostFontRasterDefaults(void)											{ return 0; }
void*	HostFontRasterOpenFace(const uint16_t*, int, int, int)								{ return 0; }
void	HostFontRasterCloseFace(void*)														{ }
void	HostFontRasterGetMetrics(void*, HostFontRasterMetrics* metrics)						{ memset(metrics, 0, sizeof(*metrics)); }
bool	HostFontRasterGetGlyph(void*, uint16_t, int, HostFontRasterGlyph*)					{ return false; }

#endif
