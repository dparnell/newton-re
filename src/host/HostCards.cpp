/*
	File:		host/HostCards.cpp

	Contains:	The host's hand in the PCMCIA sockets, for scripts
				(HostCards.h): a card made, put in and taken out, as a
				person would with a MessagePad.  A card is a file in
				Einstein's TLinearCard layout (hal/host/HostCard.h); the
				card server hears of it through the socket's interrupt.
*/

#include "HostCards.h"
#include "HostCard.h"
#include "Frames.h"
#include "ObjectHeap.h"
#include "Interpreter.h"
#include "OSErrors.h"


static void
PathOf(RefArg path, char* buffer, size_t size)
{
	buffer[0] = 0;
	if (!IsString(path))
		return;
	const UniChar* text = (const UniChar*) BinaryData(path);
	long length = (Length(path) / 2) - 1;
	if (length >= (long) size)
		length = size - 1;
	for (long i = 0; i < length; i++)
		buffer[i] = (char) text[i];
	buffer[length] = 0;
}


// HostCreateCard(path, megabytes): a blank flash card made in the file.
// ==> nil, or the error.
static Ref
FHostCreateCard(RefArg /*rcvr*/, RefArg path, RefArg megabytes)
{
	char name[512];
	PathOf(path, name, sizeof(name));
	NewtonErr err = HostCardCreate(name, ISINT(megabytes) ? RINT(megabytes) : 4, "Host card");
	return err == noErr ? NILREF : MAKEINT(err);
}


// HostInsertCard(socket, path): the card in the file put in the socket.
static Ref
FHostInsertCard(RefArg /*rcvr*/, RefArg socket, RefArg path)
{
	char name[512];
	PathOf(path, name, sizeof(name));
	NewtonErr err = HostCardInsert(RINT(socket), name);
	return err == noErr ? NILREF : MAKEINT(err);
}


// HostRemoveCard(socket): the socket's card pulled out.
static Ref
FHostRemoveCard(RefArg /*rcvr*/, RefArg socket)
{
	HostCardRemove(RINT(socket));
	return NILREF;
}


void
HostRegisterCardFunctions(void)
{
	RefVar functions(gFunctionFrame);
	SetFrameSlot(functions, RefVar(Intern((char*) "HostCreateCard")), RefVar(MakeCFunction((void*) FHostCreateCard, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostInsertCard")), RefVar(MakeCFunction((void*) FHostInsertCard, 2, nil)));
	SetFrameSlot(functions, RefVar(Intern((char*) "HostRemoveCard")), RefVar(MakeCFunction((void*) FHostRemoveCard, 1, nil)));
}
