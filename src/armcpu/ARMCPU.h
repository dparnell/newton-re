/*
	File:		armcpu/ARMCPU.h

	Contains:	An interpreter of the StrongARM's instruction set, for
				running ARM code the reconstruction cannot re-express:
				the native code in third-party packages (NTK's
				native-compiled NewtonScript functions, and later protocol
				parts).

				ARMv4 in ARM state only (the SA-110 has no Thumb): the data
				processing instructions with the barrel shifter and its
				carry, MRS/MSR, MUL/MLA and the long multiplies
				(UMULL/UMLAL/SMULL/SMLAL), LDR/STR and their byte forms, the
				halfword and signed loads (LDRH/STRH/LDRSB/LDRSH), LDM/STM,
				SWP/SWPB, B/BL and SWI.  There is one register bank - the
				code it runs is user code - so the exception modes, SPSR and
				the S bit of LDM/STM are not modelled; coprocessor
				instructions are undefined.

				It knows nothing of the Newton.  Memory is reached through
				an ARMMemory the caller supplies, which answers words,
				halfwords and bytes at 32-bit addresses (big-endian or not
				is the memory's business: a word read is the value of the
				word).  Before each instruction the CPU asks the memory
				whether the pc is a trap - an address with no code behind
				it that the caller implements on the host (a call out of the
				ARM code) - and hands those to ARMMemory::Trap.

				Run() executes until the pc reaches the stop address (the
				return address a caller plants in lr), a trap or SWI asks it
				to stop, or the instruction count runs out.

	No Newton dependencies; see docs/armcpu/README.md.
*/

#ifndef __ARMCPU_H
#define __ARMCPU_H

#include <stdint.h>

class TARMCPU;

// what the CPU runs over
class ARMMemory
{
public:
	virtual				~ARMMemory() { }
	// false when the address is not mapped: the CPU stops with a data abort
	virtual bool		Read32(uint32_t addr, uint32_t* value) = 0;		// addr word aligned
	virtual bool		Read16(uint32_t addr, uint16_t* value) = 0;		// addr halfword aligned
	virtual bool		Read8(uint32_t addr, uint8_t* value) = 0;
	virtual bool		Write32(uint32_t addr, uint32_t value) = 0;
	virtual bool		Write16(uint32_t addr, uint16_t value) = 0;
	virtual bool		Write8(uint32_t addr, uint8_t value) = 0;
	// the pc is a call out to the host: do it (arguments in r0-r3 and on the
	// stack, the result in r0) and set the pc (usually to lr).  ==> false to
	// stop the CPU.  IsTrap is asked before every instruction.
	virtual bool		IsTrap(uint32_t pc) { return false; }
	virtual bool		Trap(TARMCPU* cpu, uint32_t pc) { return false; }
	// SWI n.  ==> false to stop
	virtual bool		SWI(TARMCPU* cpu, uint32_t number) { return false; }
};

enum ARMStop
{
	kARMRunning = 0,
	kARMReturned,			// the pc reached the stop address
	kARMStoppedByHost,		// a trap or SWI said to stop
	kARMPrefetchAbort,		// the pc was not mapped
	kARMDataAbort,			// a load or store was not
	kARMUndefined,			// an instruction the CPU does not do
	kARMOutOfSteps
};

// the CPSR's flags
const uint32_t kARMFlagN = 0x80000000;
const uint32_t kARMFlagZ = 0x40000000;
const uint32_t kARMFlagC = 0x20000000;
const uint32_t kARMFlagV = 0x10000000;

class TARMCPU
{
public:
					TARMCPU(ARMMemory* memory);

	void			Reset(void);
	// run from the pc until it reaches stopAt (or something stops it)
	ARMStop			Run(uint32_t stopAt, uint64_t maxSteps = 0);
	// one instruction (a trap at the pc counts as one)
	ARMStop			Step(void);
	// call a function: arguments in r0-r3 (more pushed on the stack by the
	// caller), lr set to stopAt, and run until it returns
	ARMStop			Call(uint32_t function, uint32_t stopAt, uint64_t maxSteps = 0);

	uint32_t		r[16];			// r13 sp, r14 lr, r15 the pc of the next instruction
	uint32_t		cpsr;
	uint64_t		steps;			// instructions executed
	uint32_t		faultAddress;	// for an abort: the address; for undefined: the instruction
	uint32_t		faultPC;

private:
	bool			Condition(uint32_t cond) const;
	uint32_t		ShifterOperand(uint32_t insn, bool* carry);
	bool			DataProcessing(uint32_t insn);
	bool			Multiply(uint32_t insn);
	bool			MultiplyLong(uint32_t insn);
	bool			Swap(uint32_t insn);
	bool			HalfwordTransfer(uint32_t insn);
	bool			SingleTransfer(uint32_t insn);
	bool			BlockTransfer(uint32_t insn);
	bool			StatusTransfer(uint32_t insn);
	uint32_t		Reg(uint32_t n) const;		// a register as an operand (the pc reads 8 ahead)
	void			SetNZ(uint32_t value);

	ARMMemory*		fMemory;
	uint32_t		fPC;			// the instruction being executed
	ARMStop			fStop;
};

#endif	/* __ARMCPU_H */
