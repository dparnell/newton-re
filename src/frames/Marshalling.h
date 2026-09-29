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
	kMarshalUnicodeString	= 16,		// a Unicode string (behind a pointer, as 'cstring)
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

// The marshalling's errors.
enum
{
	kNSErrBadMarshalValue	= -70000,		// a value a scalar cannot be made of
	kNSErrBadMarshalType	= -70001,		// a type that is not marshalled
	kNSErrNotAnAggregate	= -70002		// (a template that is not a 'struct or 'array)
};

// Marshalling the other way (MarshalOut.cpp): values into bytes, by a
// template - an argument list and a type list, or one value and an
// aggregate template.  The bytes are the MessagePad's order (big-endian).
long	RefToULong(RefArg value, ULong* result);																	// ROM 0x000cd750 RefToULong__FRC6RefVarPUl
void	StuffScalar(ULong value, void** buf, ULong* size, ULong n);													// ROM 0x000ce85c StuffScalar__FUlPPvPUlT1
void	StuffPtr(void* ptr, void** buf, ULong* size, ULong n);														// ROM 0x000ce9ac StuffPtr__FPvPPvPUlUl
void	StuffDouble(double value, void** buf, ULong* size);															// ROM 0x000ce918 StuffDouble__FdPPvPUl
void	AlignBuffer(void** buf, ULong* size, ULong align);															// ROM 0x000ce968 AlignBuffer__FPPvPUlUl
void	AlignForType(void** buf, ULong* size, RefArg type);															// ROM 0x000ce9b0 AlignForType__FPPvPUlRC6RefVar
void	MarshalCString(RefArg value, void** buf, void** strBuf, ULong* size, ULong* strSize, int encoding, long max);	// ROM 0x000ceaf0 MarshalCString__FRC6RefVarPPvT2PUlT4il
long	MarshalAggregrate(RefArg value, RefArg type, void** buf, void** strBuf, ULong* size, ULong* strSize, long step, int encoding, RefArg locked);	// ROM 0x000cece8 MarshalAggregrate__FRC6RefVarT1PPvT3PUlT5liT1
long	Marshal1(RefArg value, RefArg type, void** buf, void** strBuf, void** regBuf, ULong* size, ULong* strSize, ULong* regSize,
				 long typeIndex, long valueIndex, long count, long step, int encoding, RefArg locked);						// ROM 0x000cf078 Marshal1__FRC6RefVarT1PPvN23PUlN26lN39iT1
long	DoMarshal(RefArg value, RefArg type, void** buf, void** strBuf, void** regBuf, ULong* size, ULong* strSize, ULong* regSize,
				  long typeIndex, long valueIndex, long count, long step, int encoding);										// ROM 0x000cda08 DoMarshal__FRC6RefVarT1PPvN23PUlN26lN39i
long	AggregateSize(RefArg type, ULong* size);																	// ROM 0x000ce38c AggregateSize__FRC6RefVarPUl
long	MarshalArgumentSize(RefArg args, RefArg types, ULong* size, int encoding);									// ROM 0x000cf694 MarshalArgumentSize__FRC6RefVarT1PUli
long	MarshalArguments(RefArg args, RefArg types, void* buf, ULong size, int encoding);							// ROM 0x000cf700 MarshalArguments__FRC6RefVarT1PvUli
long	MarshalArguments(RefArg args, RefArg types, void** block, int encoding);									// ROM 0x000cd634 MarshalArguments__FRC6RefVarT1PPvi

#endif	/* __MARSHALLING_H */
