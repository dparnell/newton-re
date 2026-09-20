/*
	File:		frames/Marshalling.h

	Contains:	Turning a block of C structure bytes into NewtonScript
				objects, by a template that says what is in it.

				A template is an array whose first element names the kind
				of aggregate - 'struct or 'Array - and whose remaining
				elements are the types of the fields.  A type is a symbol
				('long, 'boolean, 'Real, 'byte, ...) looked up in the ROM's
				`marshalTypes` frame, an integer code, or another template.
				So the volume information the Extras drawer asks the system
				for is

					['struct, 'boolean, 'boolean, 'boolean, 'boolean,
					 'Real, 'long, 'long]

				and unmarshalling twenty bytes through it answers an array
				of seven: four booleans, a real and two integers.

				The same machinery runs the other way for calling a C
				function from NewtonScript (DoMarshal, MarshalValue), and
				the other aggregate kinds and the pointer types are what
				that needs; only what a structure of scalars wants is here.

	Reconstructed from the MP2x00 US ROM (0x000cd6cc-0x000ce38c); each
	function cites its origin.  NOT YET RECONSTRUCTED: the marshalling
	that goes the other way, the array aggregates (UnmarshalArray
	0x000cdaf4), and the types that are pointers into the block -
	'cstring, 'unicode, 'binary, 'ref and the split longs.
*/

#ifndef __MARSHALLING_H
#define __MARSHALLING_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif
#include "objects.h"


// The marshalling type codes, which are what the ROM's `marshalTypes`
// frame maps the type symbols to.
enum
{
	kMarshalUnicode			= 0,		// (and 'asciiEncoding is 2: the two
	kMarshalLong			= 1,		//  encodings share the scalar codes)
	kMarshalULong			= 2,
	kMarshalShort			= 3,
	kMarshalByte			= 4,
	kMarshalBoolean			= 5,
	kMarshalHighInt			= 6,		// a long whose low two bits are not part of it
	kMarshalHexLong			= 7,		// answered as eight hexadecimal characters
	kMarshalSplitLong		= 8,
	kMarshalSplitByteLong	= 9,
	kMarshalStruct			= 10,
	kMarshalArray			= 11,
	kMarshalRef				= 12,
	kMarshalChar			= 13,
	kMarshalCString			= 14,
	kMarshalUniChar			= 15,
	kMarshalBinary			= 17,
	kMarshalReal			= 18
};


// A type's code: an integer is one already, and a symbol is looked up in
// the ROM's marshalTypes frame (0 for one that is not there).
long	TranslateTypeMarshalingSymbol(RefArg type);		// ROM 0x000cd6cc TranslateTypeMarshalingSymbol__FRC6RefVar

// One value read out of the bytes and the pointer stepped past it.
// `inRegister` says the value is laid out as an argument in a register
// rather than packed in a structure, which changes where the small types
// sit and how far the pointer moves.  `failed` comes back non-zero when
// the template asks for something that cannot be read.
Ref		UnmarshalValue(void** bytes, RefArg type, int inRegister, long* failed, int encoding);	// ROM 0x000cdcb4 UnmarshalValue__FPPvRC6RefVariPlT3

// The fields of a 'struct template, as an array one shorter than the
// template (which has the 'struct at its head).
Ref		UnmarshalStruct(void** bytes, RefArg type, long* failed, int encoding);	// ROM 0x000cdbdc UnmarshalStruct__FPPvRC6RefVarPli

// What a system call's parameter block says, by its template.
Ref		ConstructReturnValue(void* bytes, RefArg type, long* failed, int encoding);	// ROM 0x000cf7c0 ConstructReturnValue__FPvRC6RefVarPli

#endif	/* __MARSHALLING_H */
