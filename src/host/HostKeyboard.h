/*
	File:		host/HostKeyboard.h

	Contains:	The host's keyboard tool: where the MessagePad's keyboard
				tool (TKeyboardTool, a task over the serial keyboard: NOT YET
				RECONSTRUCTED) sends the newt world a 'keyb event for each
				key, the host keeps a queue of the keys its window sees
				(HostKeyboardPush: a Newton key code, up or down) and a task
				of its own (HostKeyboardToolTask, run from the kernel
				services hook) sends them to the world's port as
				KeyboardEvents, the keyboard connected first.  The window's
				keys are mapped to the Newton's (ADB) key codes by letter
				(HostKeyCodeForVirtualKey).  The task also ends the run
				when the window is closed (HostKeyboardQuit).
*/

#ifndef __HOSTKEYBOARD_H
#define __HOSTKEYBOARD_H

#ifndef __NEWTON_H
#include "Newton.h"
#endif

void	HostKeyboardPush(long keyCode, Boolean down);		// from any thread: a key for the tool to send (dropped when the queue is full)
long	HostKeyCodeForVirtualKey(long virtualKey);			// a Windows virtual key (or an ASCII letter/digit) as a Newton key code; -1 for none
void	HostKeyboardQuit(void);								// from any thread: the run is to end
void	HostKeyboardKick(void);								// from any thread: something else for the task to send (a package dropped, the device turned)
void	HostKeyboardToolTask(void);							// the task: the keys sent to the newt world until the quit
class TUPort;
void	HostKeyboardSendKeys(TUPort* newtPort);				// the keys queued so far sent to the world's port (the task's step; a host with a loop of its own - src/host/esp32 - calls it)
// a host with a loop of its own waits on the keyboard's kick too (os600/user/
// host/HostKick.h): made on its task, then waited for - ==> true when
// something was queued (a key, HostKeyboardKick), false when the time ran out
void	HostKeyboardWaitInit(void);
Boolean	HostKeyboardWait(ULong timeout);
void	HostKeyboardSetTimeLimit(ULong seconds);				// the run ended after so long with the window open too (newton --limit; 0: none)

#endif	/* __HOSTKEYBOARD_H */
