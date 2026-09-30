/*
	File:		frames/HostOrder.cpp

	Contains:	The binary objects the host keeps in its own byte order
				(HostOrder.h).

	Host-only: there is nothing of the ROM's here.
*/

#include "HostOrder.h"
#include "ObjHeader.h"
#include "ObjectHeap.h"
#include "RSSymbols.h"
#include "ByteOrder.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>


Boolean
HostWriteOldByteOrder(void)
{
	static int old = -1;
	if (old < 0)
		old = getenv("NEWTON_OLD_BYTE_ORDER") != nil;
	return old != 0;
}


// Whether a class, by name, is super or a subclass of it by its name alone:
// symbols are the same whatever their case (symcmp - the ROM's reals are
// of class 'Real), and a dotted name is a subclass of the name it starts
// with ('string.foo is a 'string), as IsSubclassRef has it.
Boolean
ClassNameIsNamed(const char* className, const char* super)
{
	if (symcmp((char*) className, (char*) super) == 0)
		return true;
	size_t length = strlen(super);
	if (strchr(className, '.') == nil || strlen(className) <= length || className[length] != '.')
		return false;
	for (size_t i = 0; i < length; i++)
		if (toupper((unsigned char) className[i]) != toupper((unsigned char) super[i]))
			return false;
	return true;
}


static EHostOrder
HostOrderOfOtherName(const char* name)
{
	if (symcmp((char*) name, (char*) "real") == 0)
		return kHostReal;
	// the text engine's text kept on a store (text/TXVBOChars.cpp: a large
	// binary of class 'text, UniChars that the engine reads in place)
	if (symcmp((char*) name, (char*) "text") == 0)
		return kHostUniChars;
	// a text shape's data (views/DrawShape.cpp: MakeText's 'textData and
	// MakeTextBox's 'TextBox, each a copy of the string it was given), which
	// a ROM script may turn back into a 'string - the Extras drawer's icon
	// labels do (GetIconShapeText)
	if (symcmp((char*) name, (char*) "textData") == 0 || symcmp((char*) name, (char*) "TextBox") == 0)
		return kHostUniChars;
	// (and two the host makes itself as C structs of shorts: MakeWedge's
	// 'wedge, views/DrawShape.cpp, and the 'polygon binaries - Polygons -
	// of recognition/StrokeBundle.cpp's GetPolygons)
	static const char* const kShapes[] = { "boundsRect", "rectangle", "oval", "roundRectangle", "line",
											"polygonShape", "polygonData", "regionData", "wedge", "polygon" };
	for (size_t i = 0; i < sizeof(kShapes) / sizeof(kShapes[0]); i++)
		if (symcmp((char*) name, (char*) kShapes[i]) == 0)
			return kHostHalfwords;
	return kROMOrder;
}


EHostOrder
HostOrderOfClassName(const char* name)
{
	if (name == nil)
		return kROMOrder;
	if (ClassNameIsNamed(name, "string"))
		return kHostUniChars;
	// a string by inheritance ('phone, 'name, 'company ... - the ROM's
	// initialInheritanceFrame), once the frame has anything in it
	if (ISPTR(gInheritanceFrame) && IsFrame(gInheritanceFrame) && Length(gInheritanceFrame) != 0
		&& IsSubclassRef(Intern((char*) name), RSSYMstring))
		return kHostUniChars;
	return HostOrderOfOtherName(name);
}


EHostOrder
HostOrderOfClass(RefArg theClass)
{
	if (!ISPTR((Ref) theClass) || !IsSymbol(theClass))
		return kROMOrder;
	if (IsSubclass(theClass, RSSYMstring))
		return kHostUniChars;
	return HostOrderOfOtherName(SymbolName(theClass));
}


EHostOrder
HostOrderOf(RefArg obj)
{
	Ref ref = obj;
	if (!ISPTR(ref) || (ObjectFlags(ref) & kObjSlotted) != 0)
		return kROMOrder;
	return HostOrderOfClass(RefVar(ClassOf(obj)));
}


void
SwapHostOrder(EHostOrder kind, void* data, long length)
{
	if (HostIsBigEndian() || kind == kROMOrder)
		return;
	unsigned char* b = (unsigned char*) data;
	if (kind == kHostReal)
	{
		// the double's eight bytes the other way round
		for (long i = 0; i + 8 <= length; i += 8)
			for (int j = 0; j < 4; j++)
			{
				unsigned char t = b[i + j];
				b[i + j] = b[i + 7 - j];
				b[i + 7 - j] = t;
			}
	}
	else
		SwapUniChars(data, length / 2);			// (UniChars and shorts alike)
}
