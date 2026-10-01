/*
	File:		armcpu/ARMWorld.h

	Contains:	The ARM world that outlives a call - what packages' protocol
				parts (ARMProtocols.h) run in, as opposed to a native
				function's world, which lasts one call (PackageNativeCPU.h).

				It is the same 32-bit world - the ROM image at 0, the public
				jump table at 0x01800000 answered by host glue, the heap at
				0x80000000 every call shares - with three things more:

				- regions: host memory mapped at an ARM address for as long
				  as it is wanted - a protocol part's code (a relocated copy),
				  a mirror of a host object the ARM code reads fields of, a
				  card socket's window - and devices, whose every access the
				  host answers (a ROM global the ARM code reads, a view of a
				  host structure in the ROM's layout).  A card-bus region's accesses go
				  through hal/CardBus.h, so the ARM code reaching an ATA
				  card's registers reaches the host's model of the card;
				- host traps: addresses the ARM code may call that are host
				  functions (a dispatch table entry of a host object handed
				  to the ARM code, a callback), and glue registered from
				  outside PackageNativeCPU.cpp for more of the public jump
				  table;
				- ARMCall: a call into ARM code from a host task, on a world
				  of that task's own (its own stack, its own exception
				  handlers; nested calls - ARM to host to ARM - go on the
				  same stack).  A task runs ARM code only while it is the
				  task running (docs/host-runtime.md), so the heap and the
				  regions need no locking.

	docs/armcpu/README.md, "Protocol parts".
*/

#ifndef __ARMWORLD_H
#define __ARMWORLD_H

#include <stdint.h>

class TARMCPU;

// What a host trap or a registered glue function is handed: the call's
// arguments (r0-r3, then the stack) and the way to answer it.
class ARMTrapContext
{
public:
	TARMCPU*	fCPU;
	void*		fWorld;			// (the world's, opaque)
	uint32_t	Arg(int i);		// the i'th argument word
	void		Return(uint32_t value);
	uint32_t	Register(int i);
	// the calling world's memory (a native function's world has its code
	// binary and its arena too, which ARMRead32 and its kin do not see)
	bool		Read32(uint32_t a, uint32_t* v);
	bool		Write32(uint32_t a, uint32_t v);
	bool		Read8(uint32_t a, uint8_t* v);
	bool		Write8(uint32_t a, uint8_t v);
	bool		ReadCString(uint32_t a, char* buffer, uint32_t size);
};
typedef bool (*ARMTrapFn)(void* refCon, ARMTrapContext& c);	// ==> false: stop the CPU

// Regions of host memory at ARM addresses.  A memory region's bytes are as
// they lie (the ARM code sees them big-endian: a word is its four bytes,
// most significant first); a card-bus region's go through hal/CardBus.h.
// ==> the ARM address, 0 for no room.  An address range is never reused
// while the program runs.
enum EARMRegionKind { kARMRegionMemory, kARMRegionCardBus, kARMRegionDevice };
uint32_t	ARMMapRegion(void* bytes, uint32_t size, EARMRegionKind kind);
// A device: ARM addresses whose reads and writes are answered by the host -
// a word (size 4, big-endian as a word's value) or a byte (size 1) at an
// offset into it; an answer of false refuses the access (the ARM code
// faults).  `at` 0 puts it at a region address of its own; otherwise at
// that address, which must be below the regions' (a ROM global's address,
// say).  ==> where it is, 0 for none.  ARMUnmapRegion takes it away.
typedef bool	(*ARMDeviceReadFn)(void* refCon, uint32_t offset, uint32_t size, uint32_t* value);
typedef bool	(*ARMDeviceWriteFn)(void* refCon, uint32_t offset, uint32_t size, uint32_t value);
uint32_t	ARMMapDevice(uint32_t at, uint32_t size, ARMDeviceReadFn read, ARMDeviceWriteFn write, void* refCon);
void		ARMUnmapRegion(uint32_t base);
// the region (or heap block) an ARM address is in, as host memory - nil for
// none or for a card-bus region
uint8_t*	ARMHostAddress(uint32_t a, uint32_t n);

// The heap the ARM code allocates from (NewPtr, operator new).
uint32_t	ARMAlloc(uint32_t size, bool clear);
void		ARMFree(uint32_t a);

// Words and bytes of the ARM world (ROM, heap, regions), big-endian.
bool		ARMRead32(uint32_t a, uint32_t* v);
bool		ARMWrite32(uint32_t a, uint32_t v);
bool		ARMRead8(uint32_t a, uint8_t* v);
bool		ARMWrite8(uint32_t a, uint8_t v);
bool		ARMReadCString(uint32_t a, char* buffer, uint32_t size);

// A host function the ARM code may branch to: ==> its address.
uint32_t	ARMHostTrap(ARMTrapFn fn, void* refCon, const char* name);

// More of the public jump table answered: the entry of that mangled name
// calls fn.  ==> false: no such entry.
bool		ARMRegisterGlue(const char* name, ARMTrapFn fn);

// A call into ARM code on the calling task's world: up to sixteen argument
// words, the result r0.  An ARM throw no ARM handler takes is thrown on
// to the host; a fault or an unanswered call out throws evt.ex.msg.
uint32_t	ARMCall(uint32_t pc, const uint32_t* args, int count);

#endif	/* __ARMWORLD_H */
