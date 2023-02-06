#include "arm7.h"

// For PSR
#include <src/core/arm9/arm9.h>

#include <cstring>
#include <cassert>

#include <bit>

namespace ARM7
{

uint32_t* registers[16];
uint32_t regs_sys[16];

ARM9::PSR cpsr;

uint32_t pipeline[2];

uint32_t& GetReg(int r)
{
	return *registers[r];
}

void SetReg(int r, uint32_t data)
{
	*registers[r] = data;
}

void FlushPipeline()
{
	if (!cpsr.flags.t)
	{
		pipeline[0] = Bus::Read32_ARM7(GetReg(15));
		GetReg(15) += 4;
		pipeline[1] = Bus::Read32_ARM7(GetReg(15));
		GetReg(15) += 4;
	}
	else
	{
		assert(0);
	}
}

uint32_t AdvanceARMPipeline()
{
	uint32_t i = pipeline[0];
	pipeline[0] = pipeline[1];
	pipeline[1] = Bus::Read32_ARM7(GetReg(15));
	return i;
}

void Reset()
{
	for (int i = 0; i < 16; i++)
		registers[i] = &regs_sys[i];
	
	memset(regs_sys, 0, sizeof(regs_sys));

	cpsr.val = 0;
	cpsr.flags.mode = 0x1F;

	FlushPipeline();
}

bool CondPassed(uint8_t condition)
{
	switch (condition)
	{
	case 0b0000:
		return cpsr.flags.z;
	case 0b1110:
		return true;
	default:
		printf("[emu/ARM7]: Unknown prefix %d\n", condition);
		exit(1);
	}
}

template<class T>
T sign_extend(T x, const int bits) {
    T m = 1;
    m <<= bits - 1;
    return (x ^ m) - m;
}

bool OverflowFrom(uint32_t a, uint32_t b)
{
	uint32_t s = a + b;

	if ((a & (1 << 31)) == (b & (1 << 31)) && (s & (1 << 31)) != (a & (1 << 31)))
		return true;
	
	return false;
}

void Clock()
{
	if (cpsr.flags.t)
	{
		assert(0);
	}
	else
	{
		uint32_t instr = AdvanceARMPipeline();

		if (!CondPassed((instr >> 28) & 0xF))
			return;

		if (IsBranch(instr))
		{
			bool l = (instr >> 24) & 1;
			int32_t imm = sign_extend<int32_t>((instr & 0xFFFFFF) << 2, 26);

			if (l)
				SetReg(14, GetReg(15) - 4);
			
			GetReg(15) += imm;

			printf("b 0x%08x\n", GetReg(15));

			FlushPipeline();
		}
		else if (IsSingleDataTransfer(instr))
		{
			bool i = (instr >> 25) & 1;
			bool p = (instr >> 24) & 1;
			bool u = (instr >> 23) & 1;
			bool b = (instr >> 22) & 1;
			bool w = (instr >> 21) & 1;
			bool l = (instr >> 20) & 1;

			uint8_t rn = (instr >> 16) & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;

			assert(!i);

			uint32_t offset = instr & 0xFFF;
			
			std::string op2 = "[r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
			
			uint32_t addr = GetReg(rn);

			if (p)
				addr += u ? offset : offset;
			
			if (l && !b)
			{
				SetReg(rd, Bus::Read32_ARM7(addr));
				printf("ldr r%d, %s\n", rd, op2.c_str());
			}
			else if (l && b)
			{
				SetReg(rd, Bus::Read8_ARM7(addr));
				printf("ldrb r%d, %s\n", rd, op2.c_str());
			}
			else if (!l && b)
			{
				printf("Unhandled str\n");
				exit(1);
			}
			else
			{
				printf("Unhandled strb\n");
				exit(1);
			}

			if (!p)
				addr += u ? offset : -offset;

			if (w)
				SetReg(rn, addr);
			
			if ((!l || rd != 15) && (!w || rn != 15))
			{
				GetReg(15) += 2;
			}
		}
		else if (IsDataProcessing(instr))
		{
			bool i = (instr >> 25) & 1;
			uint8_t opcode = (instr >> 21) & 0xF;
			bool s = (instr >> 20) & 1;
			uint8_t rn = (instr >> 16) & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;

			assert(i);

			uint32_t op2;
			std::string op2_disasm;

			if (i)
			{
				uint8_t imm = instr & 0xFF;
				uint8_t shift = (instr >> 8) & 0xF;
				shift <<= 1;

				op2 = std::rotr<uint32_t>(imm, shift);

				op2_disasm = "#" + std::to_string(imm);

				if (shift)
					op2_disasm += ", #" + std::to_string(shift);
			}
			else
			{
			}

			switch (opcode)
			{
			case 0x0a:
			{
				printf("cmp r%d, %s\n", rn, op2_disasm.c_str());

				uint32_t a = GetReg(rn);
				uint32_t b = op2;

				uint32_t result = a - b;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = !OverflowFrom(a, -b);
				cpsr.flags.v = OverflowFrom(a, -b);
				break;
			}
			case 0x0d:
			{
				printf("mov r%d, %s\n", rd, op2_disasm.c_str());

				SetReg(rd, op2);

				break;
			}
			default:
				printf("Unknown data processing opcode 0x%02x\n", opcode);
				exit(1);
			}

			if (rd != 15)
			{
				GetReg(15) += 4;
			}
			else
			{
				FlushPipeline();
			}
		}
		else
		{
			printf("[emu/ARM7]: Unknown instruction 0x%08x\n", instr);
			exit(1);
		}
	}
}

void Dump()
{
	for (int i = 0; i < 16; i++)
		printf("r%d\t->\t0x%08x\n", i, GetReg(i));
	printf("[%s%s%s]\n", cpsr.flags.t ? "t" : ".", cpsr.flags.c ? "c" : ".", cpsr.flags.z ? "z" : ".");
}

bool IsBranch(uint32_t i)
{
	return ((i >> 25) & 0x7) == 0b101;
}

bool IsSingleDataTransfer(uint32_t i)
{
	return ((i >> 26) & 0b11) == 0b01;
}

bool IsDataProcessing(uint32_t i)
{
	return ((i >> 26) & 0x3) == 0;
}
}