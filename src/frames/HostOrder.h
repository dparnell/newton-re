/*
	File:		frames/HostOrder.h

	Contains:	The binary objects the host keeps in its own byte order.

				DEVIATION: a MessagePad keeps every binary object's bytes
				as its big-endian ARM wrote them.  The host reads three
				kinds of binary as structures, so it keeps them in its own
				order (the importer turns them round: ObjectAreaImport.cpp):

				  - strings, of 'string and every subclass of it
				    ('string.name ..., and by inheritance 'phone,
				    'name, 'company ...): UniChars; and the text engine's
				    'text, the UniChars of a document kept on a store;
				  - reals ('real): a double;
				  - the shapes' halfword structures ('boundsRect,
				    'rectangle, 'oval, 'roundRectangle, 'line,
				    'polygonShape, 'polygonData, 'regionData): shorts
				    (`analysis/nsfunctions.py --binary-classes` says which
				    classes the ROM's object area holds; 'bits and 'mask are
				    left big-endian, qd/Pictures.h reading them that way).

				Wherever such an object's bytes leave the object system as
				bytes - NSOF, a store's objects, a script reading or writing
				a binary byte by byte (BinaryBytes.h), a class changed with
				SetClass - they must be the MessagePad's, so they are turned
				round there with SwapHostOrder.  (Every other binary - bits,
				sounds, bytecode, a package's data - stays big-endian and
				needs nothing.)  Nothing is done on a big-endian host.
*/

#ifndef __HOSTORDER_H
#define __HOSTORDER_H

#ifndef __OBJECTS_H
#include "objects.h"
#endif

enum EHostOrder
{
	kROMOrder = 0,				// big-endian, as a MessagePad's
	kHostUniChars,				// a string: UniChars
	kHostReal,					// a real: one double
	kHostHalfwords				// a shape's structure: shorts
};

// how the host keeps a binary of this class
EHostOrder	HostOrderOfClass(RefArg theClass);
// ... of the class of this name, for the importer, whose objects are not
// live yet (a string by inheritance - 'phone - is known once the
// inheritance frame is)
EHostOrder	HostOrderOfClassName(const char* name);
// whether a class name is super, or a dotted subclass of it, by the name alone
Boolean		ClassNameIsNamed(const char* className, const char* super);
// ... this binary object (kROMOrder for anything else)
EHostOrder	HostOrderOf(RefArg obj);
// length bytes of the kind turned between the host's order and the
// MessagePad's (the same either way round)
void		SwapHostOrder(EHostOrder kind, void* data, long length);
// host, tests only: write a store's reals, shapes and string large
// binaries in the host's order, as the host did before 2026-10-01, so that
// the repair of such a store can be tested (NEWTON_OLD_BYTE_ORDER in the
// environment; stores/Soups.cpp RepairHostByteOrder)
Boolean		HostWriteOldByteOrder(void);

#endif	/* __HOSTORDER_H */
