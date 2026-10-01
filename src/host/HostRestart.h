/*
	File:		host/HostRestart.h

	Contains:	A restart of the machine on the host (HostRestart.cpp):
				what the ROM's Reboot does by jumping to the reset vector -
				the whole system booted again with the RAM's
				gGlobalsThatLiveAcrossReboot and the stores kept - done by
				newton running itself again with the same arguments.
				DEVIATION: a new process standing for the jump to address 0
				(docs/host-runtime.md, "A restart").
*/

#ifndef __HOSTRESTART_H
#define __HOSTRESTART_H

// Before the boot: what a restarted newton was handed across (the reboot
// reason into gGlobalsThatLiveAcrossReboot, the window's place, how many
// restarts so far).
void	HostRestartReceive(void);

// How many times this run has restarted so far (0 for the first boot).
long	HostRebootCount(void);

// After the boot's tasks have stopped: when a reset stopped them (Reboot,
// Restart - not the power going off, not a script's HostQuit, not the
// time running out), newton run again with the same arguments (--erase
// left out: the restart keeps the store) and its exit status answered;
// -1 when there is to be no restart (NEWTON_REBOOT_LIMIT restarts, 5 by
// default, have been made already - 0 turns restarting off).
int		HostRestartIfReset(int argc, char** argv, bool scriptQuit);

#endif	/* __HOSTRESTART_H */
