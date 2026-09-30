/*
	File:		armcpu/ARMCPU.cpp

	Contains:	The ARMv4 interpreter (ARMCPU.h).

	Written from the ARM Architecture Reference Manual's description of
	ARMv4 (ARM state) and the SA-110's: no Thumb, no DSP extensions, the
	long multiplies present.
*/

#include "ARMCPU.h"

static inline uint32_t	ROR(uint32_t v, uint32_t n)		{ n &= 31; return n == 0 ? v : (v >> n) | (v << (32 - n)); }
static inline uint32_t	Bit(uint32_t v, int n)			{ return (v >> n) & 1; }


TARMCPU::TARMCPU(ARMMemory* memory)
	: fMemory(memory)
{
	Reset();
}


void
TARMCPU::Reset(void)
{
	for (int i = 0; i < 16; i++)
		r[i] = 0;
	cpsr = 0x10;			// user mode, flags clear, interrupts irrelevant
	steps = 0;
	faultAddress = 0;
	faultPC = 0;
	for (int i = 0; i < 8; i++)
		f[i] = 0;
	fpsr = 0x01000000;		// (the floating point emulator's system id, exceptions not enabled)
	fPC = 0;
	fStop = kARMRunning;
}


// a register as an operand: the pc reads as the instruction's address + 8
uint32_t
TARMCPU::Reg(uint32_t n) const
{
	return n == 15 ? fPC + 8 : r[n];
}


void
TARMCPU::SetNZ(uint32_t value)
{
	cpsr = (cpsr & ~(kARMFlagN | kARMFlagZ)) | (value & kARMFlagN) | (value == 0 ? kARMFlagZ : 0);
}


bool
TARMCPU::Condition(uint32_t cond) const
{
	bool n = (cpsr & kARMFlagN) != 0, z = (cpsr & kARMFlagZ) != 0;
	bool c = (cpsr & kARMFlagC) != 0, v = (cpsr & kARMFlagV) != 0;
	switch (cond)
	{
	case 0x0:	return z;						// EQ
	case 0x1:	return !z;						// NE
	case 0x2:	return c;						// CS
	case 0x3:	return !c;						// CC
	case 0x4:	return n;						// MI
	case 0x5:	return !n;						// PL
	case 0x6:	return v;						// VS
	case 0x7:	return !v;						// VC
	case 0x8:	return c && !z;					// HI
	case 0x9:	return !c || z;					// LS
	case 0xa:	return n == v;					// GE
	case 0xb:	return n != v;					// LT
	case 0xc:	return !z && n == v;			// GT
	case 0xd:	return z || n != v;				// LE
	case 0xe:	return true;					// AL
	default:	return false;					// NV: never, on ARMv4
	}
}


// The second operand of a data processing instruction and the shifter's
// carry out (the C flag when the shift does not produce one).
uint32_t
TARMCPU::ShifterOperand(uint32_t insn, bool* carry)
{
	bool c = (cpsr & kARMFlagC) != 0;
	if (insn & (1 << 25))
	{
		// an 8-bit immediate rotated right by twice the rotate field
		uint32_t rot = ((insn >> 8) & 0xf) * 2;
		uint32_t value = ROR(insn & 0xff, rot);
		*carry = rot == 0 ? c : Bit(value, 31);
		return value;
	}
	uint32_t rm = insn & 0xf;
	uint32_t type = (insn >> 5) & 3;
	uint32_t value;
	uint32_t amount;
	bool byRegister = (insn & (1 << 4)) != 0;
	if (byRegister)
	{
		// (the pc read as an operand of a register-specified shift is
		//  12 ahead)
		value = rm == 15 ? fPC + 12 : r[rm];
		uint32_t rs = (insn >> 8) & 0xf;
		amount = (rs == 15 ? fPC + 12 : r[rs]) & 0xff;
		if (amount == 0)
		{
			*carry = c;
			return value;
		}
		switch (type)
		{
		case 0:		// LSL
			if (amount < 32)		{ *carry = Bit(value, 32 - amount); return value << amount; }
			if (amount == 32)		{ *carry = Bit(value, 0); return 0; }
			*carry = false;			return 0;
		case 1:		// LSR
			if (amount < 32)		{ *carry = Bit(value, amount - 1); return value >> amount; }
			if (amount == 32)		{ *carry = Bit(value, 31); return 0; }
			*carry = false;			return 0;
		case 2:		// ASR
			if (amount < 32)		{ *carry = Bit(value, amount - 1); return (uint32_t) ((int32_t) value >> amount); }
			*carry = Bit(value, 31);
			return Bit(value, 31) ? 0xffffffff : 0;
		default:	// ROR
			amount &= 31;
			if (amount == 0)		{ *carry = Bit(value, 31); return value; }
			*carry = Bit(value, amount - 1);
			return ROR(value, amount);
		}
	}
	value = Reg(rm);
	amount = (insn >> 7) & 0x1f;
	switch (type)
	{
	case 0:		// LSL #n
		if (amount == 0)			{ *carry = c; return value; }
		*carry = Bit(value, 32 - amount);
		return value << amount;
	case 1:		// LSR #n (0 means 32)
		if (amount == 0)			{ *carry = Bit(value, 31); return 0; }
		*carry = Bit(value, amount - 1);
		return value >> amount;
	case 2:		// ASR #n (0 means 32)
		if (amount == 0)			{ *carry = Bit(value, 31); return Bit(value, 31) ? 0xffffffff : 0; }
		*carry = Bit(value, amount - 1);
		return (uint32_t) ((int32_t) value >> amount);
	default:	// ROR #n, or RRX
		if (amount == 0)			{ *carry = Bit(value, 0); return (c ? 0x80000000 : 0) | (value >> 1); }
		*carry = Bit(value, amount - 1);
		return ROR(value, amount);
	}
}


bool
TARMCPU::DataProcessing(uint32_t insn)
{
	uint32_t opcode = (insn >> 21) & 0xf;
	bool setFlags = (insn & (1 << 20)) != 0;
	uint32_t rn = (insn >> 16) & 0xf;
	uint32_t rd = (insn >> 12) & 0xf;
	bool shiftCarry;
	uint32_t op2 = ShifterOperand(insn, &shiftCarry);
	// (a register-specified shift reads the pc 12 ahead as rn too)
	uint32_t a = (rn == 15 && !(insn & (1 << 25)) && (insn & (1 << 4))) ? fPC + 12 : Reg(rn);
	uint32_t result = 0;
	bool carry = shiftCarry;
	bool overflow = (cpsr & kARMFlagV) != 0;
	bool arithmetic = false;
	bool write = true;
	uint32_t c = (cpsr & kARMFlagC) ? 1 : 0;
	uint64_t wide;
	switch (opcode)
	{
	case 0x0:	result = a & op2;	break;								// AND
	case 0x1:	result = a ^ op2;	break;								// EOR
	case 0x2:	// SUB
		wide = (uint64_t) a - op2;
		result = (uint32_t) wide;
		carry = a >= op2;
		overflow = ((a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x3:	// RSB
		result = op2 - a;
		carry = op2 >= a;
		overflow = ((op2 ^ a) & (op2 ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x4:	// ADD
		wide = (uint64_t) a + op2;
		result = (uint32_t) wide;
		carry = (wide >> 32) != 0;
		overflow = (~(a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x5:	// ADC
		wide = (uint64_t) a + op2 + c;
		result = (uint32_t) wide;
		carry = (wide >> 32) != 0;
		overflow = (~(a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x6:	// SBC: a - op2 - !c
		wide = (uint64_t) a - op2 - (1 - c);
		result = (uint32_t) wide;
		carry = (uint64_t) a >= (uint64_t) op2 + (1 - c);
		overflow = ((a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x7:	// RSC: op2 - a - !c
		result = op2 - a - (1 - c);
		carry = (uint64_t) op2 >= (uint64_t) a + (1 - c);
		overflow = ((op2 ^ a) & (op2 ^ result)) >> 31;
		arithmetic = true;
		break;
	case 0x8:	result = a & op2;	write = false;	break;					// TST
	case 0x9:	result = a ^ op2;	write = false;	break;					// TEQ
	case 0xa:	// CMP
		result = a - op2;
		carry = a >= op2;
		overflow = ((a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		write = false;
		break;
	case 0xb:	// CMN
		wide = (uint64_t) a + op2;
		result = (uint32_t) wide;
		carry = (wide >> 32) != 0;
		overflow = (~(a ^ op2) & (a ^ result)) >> 31;
		arithmetic = true;
		write = false;
		break;
	case 0xc:	result = a | op2;	break;								// ORR
	case 0xd:	result = op2;		break;								// MOV
	case 0xe:	result = a & ~op2;	break;								// BIC
	default:	result = ~op2;		break;								// MVN
	}
	if (setFlags && !(write && rd == 15))
	{
		SetNZ(result);
		cpsr = (cpsr & ~kARMFlagC) | (carry ? kARMFlagC : 0);
		if (arithmetic)
			cpsr = (cpsr & ~kARMFlagV) | (overflow ? kARMFlagV : 0);
	}
	// (an S instruction writing the pc would restore the CPSR from the
	//  SPSR: there is no SPSR in user mode, so the flags are left)
	if (write)
		r[rd] = result;
	return true;
}


bool
TARMCPU::Multiply(uint32_t insn)
{
	uint32_t rd = (insn >> 16) & 0xf;
	uint32_t rn = (insn >> 12) & 0xf;
	uint32_t rs = (insn >> 8) & 0xf;
	uint32_t rm = insn & 0xf;
	uint32_t result = r[rm] * r[rs];
	if (insn & (1 << 21))		// MLA
		result += r[rn];
	r[rd] = result;
	if (insn & (1 << 20))
		SetNZ(result);			// (C is unpredictable on ARMv4: left)
	return true;
}


bool
TARMCPU::MultiplyLong(uint32_t insn)
{
	uint32_t hi = (insn >> 16) & 0xf;
	uint32_t lo = (insn >> 12) & 0xf;
	uint32_t rs = (insn >> 8) & 0xf;
	uint32_t rm = insn & 0xf;
	uint64_t result;
	if (insn & (1 << 22))		// signed
		result = (uint64_t) ((int64_t) (int32_t) r[rm] * (int64_t) (int32_t) r[rs]);
	else
		result = (uint64_t) r[rm] * (uint64_t) r[rs];
	if (insn & (1 << 21))		// accumulate
		result += ((uint64_t) r[hi] << 32) | r[lo];
	r[lo] = (uint32_t) result;
	r[hi] = (uint32_t) (result >> 32);
	if (insn & (1 << 20))
		cpsr = (cpsr & ~(kARMFlagN | kARMFlagZ)) | ((result >> 63) ? kARMFlagN : 0) | (result == 0 ? kARMFlagZ : 0);
	return true;
}


bool
TARMCPU::Swap(uint32_t insn)
{
	uint32_t rn = (insn >> 16) & 0xf;
	uint32_t rd = (insn >> 12) & 0xf;
	uint32_t rm = insn & 0xf;
	uint32_t addr = r[rn];
	if (insn & (1 << 22))
	{
		uint8_t old;
		if (!fMemory->Read8(addr, &old) || !fMemory->Write8(addr, (uint8_t) r[rm]))
		{
			faultAddress = addr;
			fStop = kARMDataAbort;
			return false;
		}
		r[rd] = old;
		return true;
	}
	uint32_t old;
	if (!fMemory->Read32(addr & ~3u, &old) || !fMemory->Write32(addr & ~3u, r[rm]))
	{
		faultAddress = addr;
		fStop = kARMDataAbort;
		return false;
	}
	r[rd] = ROR(old, (addr & 3) * 8);
	return true;
}


// LDRH, STRH, LDRSB, LDRSH
bool
TARMCPU::HalfwordTransfer(uint32_t insn)
{
	bool pre = (insn & (1 << 24)) != 0;
	bool up = (insn & (1 << 23)) != 0;
	bool immediate = (insn & (1 << 22)) != 0;
	bool writeBack = (insn & (1 << 21)) != 0;
	bool load = (insn & (1 << 20)) != 0;
	uint32_t rn = (insn >> 16) & 0xf;
	uint32_t rd = (insn >> 12) & 0xf;
	uint32_t sh = (insn >> 5) & 3;
	uint32_t offset = immediate ? (((insn >> 4) & 0xf0) | (insn & 0xf)) : r[insn & 0xf];
	uint32_t base = Reg(rn);
	uint32_t addr = pre ? (up ? base + offset : base - offset) : base;
	bool ok = true;
	uint32_t value = 0;
	if (load)
	{
		if (sh == 2)		// LDRSB
		{
			uint8_t b;
			ok = fMemory->Read8(addr, &b);
			value = (uint32_t) (int32_t) (int8_t) b;
		}
		else				// LDRH (1), LDRSH (3)
		{
			// (the SA-110 ignores address bit 0 of a halfword access)
			uint16_t h;
			ok = fMemory->Read16(addr & ~1u, &h);
			value = sh == 3 ? (uint32_t) (int32_t) (int16_t) h : h;
		}
	}
	else if (sh == 1)		// STRH
		ok = fMemory->Write16(addr & ~1u, (uint16_t) Reg(rd));
	else
	{
		faultAddress = insn;
		fStop = kARMUndefined;
		return false;
	}
	if (!ok)
	{
		faultAddress = addr;
		fStop = kARMDataAbort;
		return false;
	}
	if (!pre)
		addr = up ? base + offset : base - offset;
	if ((writeBack || !pre) && rn != 15)
		r[rn] = addr;
	if (load)
		r[rd] = value;
	return true;
}


// LDR, STR, LDRB, STRB
bool
TARMCPU::SingleTransfer(uint32_t insn)
{
	bool registerOffset = (insn & (1 << 25)) != 0;
	bool pre = (insn & (1 << 24)) != 0;
	bool up = (insn & (1 << 23)) != 0;
	bool byte = (insn & (1 << 22)) != 0;
	bool writeBack = (insn & (1 << 21)) != 0;
	bool load = (insn & (1 << 20)) != 0;
	uint32_t rn = (insn >> 16) & 0xf;
	uint32_t rd = (insn >> 12) & 0xf;
	uint32_t offset;
	if (!registerOffset)
		offset = insn & 0xfff;
	else
	{
		if (insn & (1 << 4))
		{
			faultAddress = insn;
			fStop = kARMUndefined;
			return false;
		}
		bool carry;
		// (a scaled register offset: an immediate shift of rm, as for data
		//  processing without the immediate bit)
		offset = ShifterOperand(insn & ~(1u << 25), &carry);
	}
	uint32_t base = Reg(rn);
	uint32_t addr = pre ? (up ? base + offset : base - offset) : base;
	bool ok;
	uint32_t value = 0;
	if (load)
	{
		if (byte)
		{
			uint8_t b;
			ok = fMemory->Read8(addr, &b);
			value = b;
		}
		else
		{
			// an unaligned word load reads the word it is in, rotated
			ok = fMemory->Read32(addr & ~3u, &value);
			value = ROR(value, (addr & 3) * 8);
		}
	}
	else
	{
		// (a stored pc is the instruction's address + 12 on ARMv4)
		uint32_t stored = rd == 15 ? fPC + 12 : r[rd];
		ok = byte ? fMemory->Write8(addr, (uint8_t) stored) : fMemory->Write32(addr & ~3u, stored);
	}
	if (!ok)
	{
		faultAddress = addr;
		fStop = kARMDataAbort;
		return false;
	}
	if (!pre)
		addr = up ? base + offset : base - offset;
	if ((writeBack || !pre) && rn != 15)
		r[rn] = addr;
	if (load)
		r[rd] = rd == 15 ? value & ~3u : value;
	return true;
}


// LDM, STM
bool
TARMCPU::BlockTransfer(uint32_t insn)
{
	bool pre = (insn & (1 << 24)) != 0;
	bool up = (insn & (1 << 23)) != 0;
	bool writeBack = (insn & (1 << 21)) != 0;
	bool load = (insn & (1 << 20)) != 0;
	uint32_t rn = (insn >> 16) & 0xf;
	uint32_t list = insn & 0xffff;
	uint32_t count = 0;
	for (int i = 0; i < 16; i++)
		if (list & (1 << i))
			count++;
	uint32_t base = r[rn];
	// the lowest register goes to the lowest address, whichever way
	uint32_t start;
	if (up)
		start = pre ? base + 4 : base;
	else
		start = pre ? base - count * 4 : base - count * 4 + 4;
	uint32_t newBase = up ? base + count * 4 : base - count * 4;
	uint32_t addr = start;
	uint32_t loaded[16];
	for (int i = 0; i < 16; i++)
	{
		if (!(list & (1 << i)))
			continue;
		bool ok;
		if (load)
			ok = fMemory->Read32(addr & ~3u, &loaded[i]);
		else
		{
			// (a stored base that is not the first register stored is the
			//  written-back value on ARMv4; the first stores the original)
			uint32_t v = i == 15 ? fPC + 12 : r[i];
			if (i == (int) rn && writeBack && (list & ((1 << i) - 1)))
				v = newBase;
			ok = fMemory->Write32(addr & ~3u, v);
		}
		if (!ok)
		{
			faultAddress = addr;
			fStop = kARMDataAbort;
			return false;
		}
		addr += 4;
	}
	if (writeBack)
		r[rn] = newBase;
	if (load)
		for (int i = 0; i < 16; i++)
			if (list & (1 << i))
				r[i] = i == 15 ? loaded[i] & ~3u : loaded[i];
	return true;
}


// MRS, MSR (the flags; the control bits are left alone in user mode)
bool
TARMCPU::StatusTransfer(uint32_t insn)
{
	if ((insn & 0x0fbf0fff) == 0x010f0000)		// MRS rd, CPSR
	{
		r[(insn >> 12) & 0xf] = cpsr;
		return true;
	}
	uint32_t value;
	if (insn & (1 << 25))
		value = ROR(insn & 0xff, ((insn >> 8) & 0xf) * 2);
	else
		value = r[insn & 0xf];
	if (insn & (1 << 22))						// SPSR: none in user mode
		return true;
	if (insn & (1 << 19))						// the flags field
		cpsr = (cpsr & 0x0fffffff) | (value & 0xf0000000);
	return true;
}


ARMStop
TARMCPU::Step(void)
{
	fStop = kARMRunning;
	uint32_t pc = r[15];
	if (fMemory->IsTrap(pc))
	{
		fPC = pc;
		steps++;
		if (!fMemory->Trap(this, pc))
			return fStop = kARMStoppedByHost;
		return kARMRunning;
	}
	uint32_t insn;
	if ((pc & 3) || !fMemory->Read32(pc, &insn))
	{
		faultAddress = pc;
		faultPC = pc;
		return fStop = kARMPrefetchAbort;
	}
	fPC = pc;
	r[15] = pc + 4;
	steps++;
	if (!Condition(insn >> 28))
		return kARMRunning;
	bool ok = true;
	switch ((insn >> 25) & 7)
	{
	case 0:
		if ((insn & 0x0fc000f0) == 0x00000090)
			ok = Multiply(insn);
		else if ((insn & 0x0f8000f0) == 0x00800090)
			ok = MultiplyLong(insn);
		else if ((insn & 0x0fb00ff0) == 0x01000090)
			ok = Swap(insn);
		else if ((insn & 0x90) == 0x90)
			ok = HalfwordTransfer(insn);
		else if ((insn & 0x01900000) == 0x01000000)
			ok = StatusTransfer(insn);
		else
			ok = DataProcessing(insn);
		break;
	case 1:
		if ((insn & 0x01900000) == 0x01000000)
			ok = StatusTransfer(insn);
		else
			ok = DataProcessing(insn);
		break;
	case 2:
	case 3:
		ok = SingleTransfer(insn);
		break;
	case 4:
		ok = BlockTransfer(insn);
		break;
	case 5:		// B, BL
	{
		int32_t offset = (int32_t) (insn << 8) >> 6;
		if (insn & (1 << 24))
			r[14] = pc + 4;
		r[15] = pc + 8 + offset;
		break;
	}
	case 7:
		if ((insn & 0x0f000000) == 0x0f000000)
		{
			if (!fMemory->SWI(this, insn & 0x00ffffff))
				return fStop = kARMStoppedByHost;
			break;
		}
		// (a coprocessor operation or register transfer)
	default:	// coprocessor data transfers and operations: the FPA's, or undefined
		ok = Coprocessor(insn);
		if (!ok && fStop == kARMRunning)
		{
			faultAddress = insn;
			fStop = kARMUndefined;
		}
		break;
	}
	if (!ok)
	{
		faultPC = pc;
		r[15] = pc;
		return fStop;
	}
	return kARMRunning;
}


ARMStop
TARMCPU::Run(uint32_t stopAt, uint64_t maxSteps)
{
	uint64_t limit = maxSteps == 0 ? 0 : steps + maxSteps;
	for ( ; ; )
	{
		if (r[15] == stopAt)
			return kARMReturned;
		if (limit != 0 && steps >= limit)
			return kARMOutOfSteps;
		ARMStop stop = Step();
		if (stop != kARMRunning)
			return stop;
	}
}


ARMStop
TARMCPU::Call(uint32_t function, uint32_t stopAt, uint64_t maxSteps)
{
	r[14] = stopAt;
	r[15] = function;
	return Run(stopAt, maxSteps);
}
