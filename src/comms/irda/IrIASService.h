/*
	File:		comms/irda/IrIASService.h

	Contains:	IrDA's Information Access Service database: what a station
				tells another that asks (its IAS server) - classes of
				attributes of values.  A MessagePad has the class
				"Device" with "DeviceName" ("Newton") and "IrLMPSupport",
				and a class for each service that registers an LSAP (the
				beamer's "BMW" with "IrDA:IrLMP:LsapSel").

				TIASNamedList is a list with a name; TIASService is the
				list of classes, TIASClass of attributes, TIASAttribute of
				elements, TIASElement a value: missing (0), an integer
				(1), a few bytes (2, kept in the integer's word) or a user
				string (3).  On the air (AddInfoToBuffer) an attribute is
				a count of elements, each an object id (0), its type and
				its value - integers and bytes big-endian as the ROM keeps
				them; only integers are read back.

	Reconstructed from the MP2x00 US ROM (0x000f1924-0x000f23dc); each
	function cites its origin.
*/

#ifndef __COMMS_IRIASSERVICE_H
#define __COMMS_IRIASSERVICE_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

#include "List.h"

class CBuffer;

extern const char	kIASDeviceClassStr[];		// "Device"
extern const char	kIASDeviceNameAttrStr[];	// "DeviceName"
extern const char	kIASLMPSupportAttrStr[];	// "IrLMPSupport"

// an element's type
enum
{
	kIASValueMissing = 0,
	kIASValueInteger = 1,
	kIASValueNBytes = 2,
	kIASValueString = 3
};


class TIASNamedList : public CList
{
public:
						TIASNamedList();
						~TIASNamedList();

	NewtonErr			Init(const char* name);
	void*				Search(const char* name);

	char*				fName;					// +0x18
};


class TIASElement
{
public:
						TIASElement();
						~TIASElement();

	void				SetInteger(ULong value);
	void				SetNBytes(ULong bytes, ULong length);
	NewtonErr			SetString(const char* string);
	NewtonErr			GetInteger(ULong* value);
	void				AddInfoToBuffer(CBuffer* buffer);
	NewtonErr			ExtractInfoFromBuffer(CBuffer* buffer);

	UByte				fType;					// +0x00
	ULong				fLength;				// +0x04
	ULong				fValue;					// +0x08  an integer, or up to four bytes big-endian
	void*				fData;					// +0x0c  &fValue, or a string of its own
};


class TIASAttribute : public TIASNamedList
{
public:
						TIASAttribute();
						~TIASAttribute();

	NewtonErr			Insert(TIASElement* element);
	void				AddInfoToBuffer(CBuffer* buffer);
	NewtonErr			ExtractInfoFromBuffer(CBuffer* buffer);
};


class TIASClass : public TIASNamedList
{
public:
						TIASClass();
						~TIASClass();

	NewtonErr			Insert(TIASAttribute* attribute);
	TIASAttribute*		FindAttribute(const char* name);
};


class TIASService : public TIASNamedList
{
public:
						TIASService();
						~TIASService();

	TIASClass*			FindClass(const char* name);
	TIASClass*			AddClass(const char* name, ULong* added);
	TIASAttribute*		AddAttribute(const char* className, const char* attrName, ULong* added);
	NewtonErr			AddAttributeEntry(const char* className, const char* attrName, TIASElement* element);
	NewtonErr			AddIntegerEntry(const char* className, const char* attrName, ULong value);
	NewtonErr			AddStringEntry(const char* className, const char* attrName, const char* string);
	NewtonErr			AddNBytesEntry(const char* className, const char* attrName, ULong bytes, ULong length);
	NewtonErr			RemoveClass(const char* className, ULong added);
	NewtonErr			RemoveAttribute(const char* className, const char* attrName, ULong added);
};

#endif	/* __COMMS_IRIASSERVICE_H */
