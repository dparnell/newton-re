/*
	File:		armcpu/tests/test_ARMCPU.cpp

	Contains:	The ARM interpreter's tests: hand-assembled sequences over a
				big-endian memory, checked against what the ARM Architecture
				Reference Manual says each instruction does.
*/

#include "ARMCPU.h"
#include <stdio.h>
#include <string.h>
#include <vector>

static int gFailures = 0;
#define EXPECT(c)	do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); gFailures++; } } while (0)

// 64K of big-endian memory at 0, a trap at 0x00F00000 that doubles r0
class TestMemory : public ARMMemory
{
public:
	std::vector<uint8_t> bytes;
	int traps = 0;
	uint32_t lastSWI = 0;
	TestMemory() : bytes(0x10000, 0) { }
	bool In(uint32_t a, uint32_t n) { return a + n <= bytes.size(); }
	bool Read32(uint32_t a, uint32_t* v) override
	{
		if (!In(a, 4)) return false;
		*v = ((uint32_t) bytes[a] << 24) | ((uint32_t) bytes[a + 1] << 16) | ((uint32_t) bytes[a + 2] << 8) | bytes[a + 3];
		return true;
	}
	bool Read16(uint32_t a, uint16_t* v) override
	{
		if (!In(a, 2)) return false;
		*v = (uint16_t) ((bytes[a] << 8) | bytes[a + 1]);
		return true;
	}
	bool Read8(uint32_t a, uint8_t* v) override			{ if (!In(a, 1)) return false; *v = bytes[a]; return true; }
	bool Write32(uint32_t a, uint32_t v) override
	{
		if (!In(a, 4)) return false;
		bytes[a] = (uint8_t) (v >> 24); bytes[a + 1] = (uint8_t) (v >> 16); bytes[a + 2] = (uint8_t) (v >> 8); bytes[a + 3] = (uint8_t) v;
		return true;
	}
	bool Write16(uint32_t a, uint16_t v) override		{ if (!In(a, 2)) return false; bytes[a] = (uint8_t) (v >> 8); bytes[a + 1] = (uint8_t) v; return true; }
	bool Write8(uint32_t a, uint8_t v) override			{ if (!In(a, 1)) return false; bytes[a] = v; return true; }
	bool IsTrap(uint32_t pc) override					{ return pc == 0x00F00000; }
	bool Trap(TARMCPU* cpu, uint32_t pc) override		{ traps++; cpu->r[0] *= 2; cpu->r[15] = cpu->r[14]; return true; }
	bool SWI(TARMCPU* cpu, uint32_t n) override			{ lastSWI = n; cpu->r[0] = 0x5A; return true; }

	void Code(uint32_t at, std::initializer_list<uint32_t> words)
	{
		for (uint32_t w : words) { Write32(at, w); at += 4; }
	}
};

static const uint32_t kStop = 0xDEAD0000;	// never mapped: the return address

// run the code at 0x1000 with the registers given, to its return
static ARMStop
RunCode(TARMCPU& cpu, TestMemory& mem, std::initializer_list<uint32_t> code)
{
	mem.Code(0x1000, code);
	cpu.r[13] = 0x8000;
	return cpu.Call(0x1000, kStop, 100000);
}

static void
TestDataProcessing()
{
	TestMemory mem;
	TARMCPU cpu(&mem);
	// mov r0,#5; add r0,r0,#3; mov pc,lr
	cpu.Reset();
	EXPECT(RunCode(cpu, mem, { 0xe3a00005, 0xe2800003, 0xe1a0f00e }) == kARMReturned);
	EXPECT(cpu.r[0] == 8);
	// mvn r0,#0; adds r0,r0,#1 -> 0, Z and C set, V clear
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3e00000, 0xe2900001, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0 && (cpu.cpsr & kARMFlagZ) && (cpu.cpsr & kARMFlagC) && !(cpu.cpsr & kARMFlagV));
	// mov r0,#0x7fffffff (mvn r0,#0x80000000); adds r0,r0,#1 -> overflow, N
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3e00102, 0xe2900001, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0x80000000 && (cpu.cpsr & kARMFlagV) && (cpu.cpsr & kARMFlagN) && !(cpu.cpsr & kARMFlagC));
	// mov r1,#3; cmp r1,#5 -> borrow: C clear, N set; movlt r2,#1; movge r2,#2
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01003, 0xe3510005, 0xb3a02001, 0xa3a02002, 0xe1a0f00e });
	EXPECT(cpu.r[2] == 1 && !(cpu.cpsr & kARMFlagC) && (cpu.cpsr & kARMFlagN));
	// 64-bit add: r0:r1 = 0x00000001_ffffffff + 1 -> adds r1,r1,#1; adc r0,r0,#0
	cpu.Reset();
	cpu.r[0] = 1; cpu.r[1] = 0xffffffff;
	mem.Code(0x1000, { 0xe2911001, 0xe2a00000, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 2 && cpu.r[1] == 0);
	// 64-bit subtract: r0:r1 = 2:0 - 0:1 -> subs r1,r1,#1; sbc r0,r0,#0
	cpu.Reset();
	cpu.r[0] = 2; cpu.r[1] = 0;
	mem.Code(0x1000, { 0xe2511001, 0xe2c00000, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 1 && cpu.r[1] == 0xffffffff);
	// shifts: mov r1,#0x80000001 (0x06 ror 2); add r1,r1,#1 -> 0x80000002;
	// mov r0,r1,asr #4; movs r2,r1,lsr #1 (C from bit 0)
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01106, 0xe2811001, 0xe1a00241, 0xe1b020a1, 0xe1a0f00e });
	EXPECT(cpu.r[1] == 0x80000002);
	EXPECT(cpu.r[0] == 0xf8000000);
	EXPECT(cpu.r[2] == 0x40000001 && !(cpu.cpsr & kARMFlagC));
	// register-specified shift: mov r1,#1; mov r2,#33; mov r0,r1,lsl r2 -> 0
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01001, 0xe3a02021, 0xe1a00211, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0);
	// rrx: C set (cmp r0,r0), mov r1,#2; mov r0,r1,rrx -> 0x80000001
	cpu.Reset();
	RunCode(cpu, mem, { 0xe1500000, 0xe3a01002, 0xe1a00061, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0x80000001);
	// rsb, bic, eor, orr, mvn
	cpu.Reset();
	cpu.r[1] = 0xf0;
	mem.Code(0x1000, { 0xe2610010, 0xe3c12030, 0xe2213cff, 0xe3814c01, 0xe1e05001, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 0x10 - 0xf0);
	EXPECT(cpu.r[2] == 0xc0);
	EXPECT(cpu.r[3] == (0xf0u ^ 0xff00u));
	EXPECT(cpu.r[4] == (0xf0u | 0x100u));
	EXPECT(cpu.r[5] == ~0xf0u);
	// the pc as an operand reads 8 ahead: add r0,pc,#0 at 0x1000 -> 0x1008
	cpu.Reset();
	RunCode(cpu, mem, { 0xe28f0000, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0x1008);
}

static void
TestMultiply()
{
	TestMemory mem;
	TARMCPU cpu(&mem);
	// mul r0,r1,r2; mla r3,r1,r2,r0
	cpu.Reset();
	cpu.r[1] = 7; cpu.r[2] = 6;
	mem.Code(0x1000, { 0xe0000291, 0xe0230291, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 42 && cpu.r[3] == 84);
	// umull r0,r1,r2,r3 (r0 lo, r1 hi): 0xffffffff * 0xffffffff
	cpu.Reset();
	cpu.r[2] = 0xffffffff; cpu.r[3] = 0xffffffff;
	mem.Code(0x1000, { 0xe0810392, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 1 && cpu.r[1] == 0xfffffffe);
	// smull r0,r1,r2,r3: -2 * 3 = -6
	cpu.Reset();
	cpu.r[2] = (uint32_t) -2; cpu.r[3] = 3;
	mem.Code(0x1000, { 0xe0c10392, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == (uint32_t) -6 && cpu.r[1] == 0xffffffff);
	// umlal r0,r1,r2,r3: 1:0 + 2*3
	cpu.Reset();
	cpu.r[0] = 0; cpu.r[1] = 1; cpu.r[2] = 2; cpu.r[3] = 3;
	mem.Code(0x1000, { 0xe0a10392, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	EXPECT(cpu.r[0] == 6 && cpu.r[1] == 1);
}

static void
TestLoadStore()
{
	TestMemory mem;
	TARMCPU cpu(&mem);
	cpu.Reset();
	mem.Write32(0x2000, 0x11223344);
	mem.Write32(0x2004, 0x55667788);
	// mov r1,#0x2000; ldr r0,[r1]; ldr r2,[r1,#4]!; ldrb r3,[r1,#-3]; mov pc,lr
	RunCode(cpu, mem, { 0xe3a01a02, 0xe5910000, 0xe5b12004, 0xe5513003, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0x11223344 && cpu.r[2] == 0x55667788 && cpu.r[1] == 0x2004);
	EXPECT(cpu.r[3] == 0x22);		// big-endian: byte 0x2001
	// post-indexed store, and a store of a byte:
	// mov r1,#0x3000; mov r0,#0xab; str r0,[r1],#4; strb r0,[r1]; mov pc,lr
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01a03, 0xe3a000ab, 0xe4810004, 0xe5c10000, 0xe1a0f00e });
	uint32_t w; uint8_t b;
	mem.Read32(0x3000, &w); mem.Read8(0x3004, &b);
	EXPECT(w == 0xab && b == 0xab && cpu.r[1] == 0x3004);
	// register offset, scaled: mov r1,#0x2000; mov r2,#1; ldr r0,[r1,r2,lsl #2]
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01a02, 0xe3a02001, 0xe7910102, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 0x55667788);
	// halfwords and signed loads: ldrh r0,[r1]; ldrsh r2,[r1,#4]; ldrsb r3,[r1,#6]; strh r0,[r1,#8]
	cpu.Reset();
	mem.Write32(0x4000, 0x1234fffe);
	mem.Write32(0x4004, 0x8001ff00);
	cpu.r[1] = 0x4000;
	mem.Code(0x1000, { 0xe1d100b0, 0xe1d120f4, 0xe1d130d6, 0xe1c100b8, 0xe1a0f00e });
	cpu.r[13] = 0x8000;
	cpu.Call(0x1000, kStop);
	uint16_t h;
	mem.Read16(0x4008, &h);
	EXPECT(cpu.r[0] == 0x1234 && cpu.r[2] == 0xffff8001 && cpu.r[3] == 0xffffffff && h == 0x1234);
	// ldr from the literal pool: ldr r0,[pc,#0] (the pc reads 8 ahead); mov pc,lr; .word 0xcafef00d
	cpu.Reset();
	RunCode(cpu, mem, { 0xe59f0000, 0xe1a0f00e, 0xcafef00d });		// (the literal at 0x1008)
	EXPECT(cpu.r[0] == 0xcafef00d);
	// swp: mov r1,#0x2000; mov r2,#9; swp r0,r2,[r1]
	cpu.Reset();
	mem.Write32(0x2000, 77);
	RunCode(cpu, mem, { 0xe3a01a02, 0xe3a02009, 0xe1010092, 0xe1a0f00e });
	mem.Read32(0x2000, &w);
	EXPECT(cpu.r[0] == 77 && w == 9);
}

static void
TestBlockTransfer()
{
	TestMemory mem;
	TARMCPU cpu(&mem);
	// a function with a frame: stmfd sp!,{r4,r5,lr}; mov r4,#1; mov r5,#2;
	// add r0,r4,r5; ldmfd sp!,{r4,r5,pc}
	cpu.Reset();
	cpu.r[4] = 0x44; cpu.r[5] = 0x55;
	mem.Code(0x1000, { 0xe92d4030, 0xe3a04001, 0xe3a05002, 0xe0840005, 0xe8bd8030 });
	cpu.r[13] = 0x8000;
	EXPECT(cpu.Call(0x1000, kStop) == kARMReturned);
	EXPECT(cpu.r[0] == 3 && cpu.r[4] == 0x44 && cpu.r[5] == 0x55 && cpu.r[13] == 0x8000);
	uint32_t w;
	mem.Read32(0x8000 - 12, &w);
	EXPECT(w == 0x44);		// the lowest register at the lowest address
	// stmia/ldmdb: mov r1,#0x2000; mov r2,#7; mov r3,#8; stmia r1!,{r2,r3}; ldmdb r1,{r4,r5}
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a01a02, 0xe3a02007, 0xe3a03008, 0xe8a1000c, 0xe9110030, 0xe1a0f00e });
	EXPECT(cpu.r[1] == 0x2008 && cpu.r[4] == 7 && cpu.r[5] == 8);
}

static void
TestBranchesAndCalls()
{
	TestMemory mem;
	TARMCPU cpu(&mem);
	// a loop: mov r0,#0; mov r1,#10; add r0,r0,r1; subs r1,r1,#1; bne -3; mov pc,lr
	cpu.Reset();
	RunCode(cpu, mem, { 0xe3a00000, 0xe3a0100a, 0xe0800001, 0xe2511001, 0x1afffffc, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 55);
	// bl to a subroutine: stmfd sp!,{lr}; bl +1 (to 0x100c); ldmfd sp!,{pc};
	// 0x100c: mov r0,#9; mov pc,lr
	cpu.Reset();
	RunCode(cpu, mem, { 0xe92d4000, 0xeb000000, 0xe8bd8000, 0xe3a00009, 0xe1a0f00e });
	EXPECT(cpu.r[0] == 9);
	// a call out through a stub, as NTK's native code calls the ROM:
	// stmfd sp!,{lr}; mov r0,#21; bl stub; ldmfd sp!,{pc}; stub: ldr pc,[pc,#-4]; .word 0x00F00000
	cpu.Reset();
	mem.traps = 0;
	RunCode(cpu, mem, { 0xe92d4000, 0xe3a00015, 0xeb000000, 0xe8bd8000, 0xe51ff004, 0x00F00000 });
	EXPECT(cpu.r[0] == 42 && mem.traps == 1);
	// swi
	cpu.Reset();
	RunCode(cpu, mem, { 0xef000123, 0xe1a0f00e });
	EXPECT(mem.lastSWI == 0x123 && cpu.r[0] == 0x5A);
	// status: cmp r0,r0 (Z, C); mrs r1,cpsr; msr cpsr_f,#0; mrs r2,cpsr
	cpu.Reset();
	RunCode(cpu, mem, { 0xe1500000, 0xe10f1000, 0xe328f000, 0xe10f2000, 0xe1a0f00e });
	EXPECT((cpu.r[1] & (kARMFlagZ | kARMFlagC)) == (kARMFlagZ | kARMFlagC) && (cpu.r[2] & 0xf0000000) == 0);
	// an undefined instruction stops the CPU
	cpu.Reset();
	EXPECT(RunCode(cpu, mem, { 0xee000010 }) == kARMUndefined);
	// so does a load from memory that is not there
	cpu.Reset();
	EXPECT(RunCode(cpu, mem, { 0xe3a01440, 0xe5910000 }) == kARMDataAbort);
}

int
main()
{
	TestDataProcessing();
	TestMultiply();
	TestLoadStore();
	TestBlockTransfer();
	TestBranchesAndCalls();
	if (gFailures == 0)
		printf("test_ARMCPU: all passed\n");
	else
		printf("test_ARMCPU: %d failures\n", gFailures);
	return gFailures != 0;
}
