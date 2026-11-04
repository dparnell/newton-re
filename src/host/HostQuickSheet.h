/*
	File:		host/HostQuickSheet.h

	Contains:	The quick sheet and the lock screen (HostQuickSheet.ns): a
				sheet pulled down from the top of the screen - the screen
				locked from it, and the board's radios and receivers (the
				4G module, the GPS, the LoRa radio) switched on and off -
				and the lock screen it locks to, which covers the whole
				screen with the time, the notifications applications have
				left on it while it is locked, and a slider dragged across
				to unlock.

				The lock screen is installed on every host (the desktop's
				newton, the reMarkable, the T-Deck Pro): any application can
				ask whether the screen is locked and leave a notification on
				it, through the global functions ScreenLockIsLocked,
				ScreenLockNotify, ScreenLockWithdraw, ... (the interface:
				docs/host-lock-screen.md).  It locks before the machine
				sleeps (a power-off function): the power switch, and the
				idle timer - Prefs' Sleep time, which is its auto-lock.

				A host with a touch screen and no other way to lock it (the
				T-Deck Pro: host/esp32/main/tdeck_input.cpp sees the finger
				pulled down from the screen's top edge) calls
				HostQuickSheetPulled to open the sheet; elsewhere the sheet
				is opened only by a test (demo/quicksheet.ns:
				GetGlobalVar('|QuickSheet:host|):Pulled()).

				The NewtonScript side answers the frame in the global
				QuickSheet:host - Pulled() opens the sheet (or closes it,
				open already), Lock() and Unlock() - and tells the host
				through HostScreenLocked(on), which calls the host's hook:
				the board leaves the keyboard's keys out and its light off
				while the screen is locked.

				A host whose screen keeps its picture for nothing (the
				T-Deck Pro's e-paper) can have the machine sleep while the
				screen is locked (HostQuickSheetSetLockSleeps): the lock
				puts it to sleep once drawn, the machine's own sleep (the
				power switch, the idle timer) locks the screen first, and
				while locked it sleeps again once the user has left it
				alone a few seconds (docs/host-lock-screen.md, "Sleep").

				DEVIATION (host addition): the ROM has no lock screen.
*/

#ifndef __HOSTQUICKSHEET_H
#define __HOSTQUICKSHEET_H

void	HostQuickSheetSetLockHook(void (*locked)(bool on));	// before boot: told when the screen is locked and unlocked
void	HostInstallQuickSheet(void);		// in the newt world, once its globals are built (before the Host panel's): HostScreenLocked, QuickSheet:host, the ScreenLock functions, its power-off function
bool	HostQuickSheetInstalled(void);		// whether it was installed
void	HostQuickSheetPulled(void);			// in the newt world's task: the sheet pulled down (opened, or closed if open)
void	HostQuickSheetSetPowerOff(bool (*off)(bool really));	// before boot: a board that can switch itself off (the sheet then has Power off); the hook, not really: ==> whether it could now; really: with the off screen drawn, does not return, or ==> false
void	HostQuickSheetSetLockSleeps(bool sleeps);	// before boot: the machine asleep while the screen is locked (HostLockSleeps)

#endif	/* __HOSTQUICKSHEET_H */
