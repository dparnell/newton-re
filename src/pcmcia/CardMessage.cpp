/*
	File:		pcmcia/CardMessage.cpp

	Contains:	TCardMessage (CardMessage.h).

	Reconstructed from the MP2x00 US ROM; each function cites its origin.
*/

#include "CardMessage.h"

#include <string.h>


// ROM 0x0004ed10 __ct__12TCardMessageFv
TCardMessage::TCardMessage()
{
	fAEventClass = 'newt';
	fAEventID = 'cdsv';
	fField2E = 0;
	fField2F = 0;
	fField30 = 0;
	fField34 = 0;
	Clear();
}


// ROM 0x0004ed78 __dt__12TCardMessageFv
TCardMessage::~TCardMessage()
{ }


// ROM 0x0004ed84 Clear__12TCardMessageFv
void
TCardMessage::Clear(void)
{
	fType = 0;
	fSocket = 0;
	fData = 0;
	fCardHandler = nil;
	fField2C = 0;
	fField2D = 1;
	fField14 = 0;
	fField18 = 0;
	fField20 = 0;
	fField24 = 0;
	fField28 = 0;
	memset(fDevices, 0, sizeof(fDevices));
}


// ROM 0x0004edc4 MessageStuff__12TCardMessageFUlN21
void
TCardMessage::MessageStuff(ULong type, ULong socket, ULong data)
{
	Clear();
	fType = type;
	fData = data;
	fSocket = socket;
}
