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
uint32_t r_svc[2];
uint32_t r_irq[2];
uint32_t r_abt[2];

ARM9::PSR cpsr, *cur_spsr, spsr_fiq, spsr_svc, spsr_abr, spsr_irq, spsr_und;

uint32_t pipeline[2];
uint16_t pipeline_t[2];

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
		pipeline_t[0] = Bus::Read16_ARM7(GetReg(15));
		GetReg(15) += 2;
		pipeline_t[1] = Bus::Read16_ARM7(GetReg(15));
		GetReg(15) += 2;
	}
}

uint32_t AdvanceARMPipeline()
{
	uint32_t i = pipeline[0];
	pipeline[0] = pipeline[1];
	pipeline[1] = Bus::Read32_ARM7(GetReg(15));
	return i;
}

uint32_t AdvanceThumbPipeline()
{
	auto i = pipeline_t[0];
	pipeline_t[0] = pipeline_t[1];
	pipeline_t[1] = Bus::Read16_ARM7(GetReg(15));
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
		uint16_t instr = AdvanceThumbPipeline();

		printf("0x%04x: ", instr);

		if (IsMovCmpSubAdd(instr))
		{
			uint8_t op = (instr >> 11) & 0b11;
			uint8_t rd = (instr >> 8) & 0x7;
			uint8_t imm = instr & 0xffff;

			switch (op)
			{
			case 0x00:
				SetReg(rd, imm);
				printf("mov r%d, #%d\n", rd, imm);
				break;
			default:
				printf("[emu/ARM9]: Unknown sub-opcode 0x%02x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsPCRelativeLoad(instr))
		{
			uint16_t imm = (instr & 0xFF) << 2;
			uint8_t rd = (instr >> 8) & 0x7;

			uint32_t base = GetReg(15) & ~3;

			base += imm;

			SetReg(rd, Bus::Read32_ARM7(base));

			printf("ldr r%d, _0x%08x\n", rd, base);

			GetReg(15) += 2;
		}
		else if (IsLoadStoreRegister(instr))
		{
			uint8_t rd = instr & 7;
			uint8_t rb = (instr >> 3) & 7;
			uint8_t ro = (instr >> 6) & 7;

			bool l = (instr >> 11) & 1;
			bool b = (instr >> 10) & 1;

			uint32_t addr = GetReg(rb);
			addr += (int32_t)GetReg(ro);

			if (!l && !b)
			{
				Bus::Write32_ARM7(addr, GetReg(rd));
				printf("str r%d, [r%d, r%d]\n", rd, rb, ro);
			}
			else
			{
				printf("Unhandled l %d b %d combo\n", l, b);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else
		{
			printf("[emu/ARM7]: Unknown instruction 0x%04x\n", instr);
			exit(1);
		}
	}
	else
	{
		uint32_t instr = AdvanceARMPipeline();

		uint8_t cond = (instr >> 28) & 0xF;

		printf("%d (0x%08x): ", cond, instr);

		if (!CondPassed(cond))
		{
			printf("Cond failed\n");
			GetReg(15) += 4;
			return;
		}

		if (IsBranchExchange(instr))
		{
			uint8_t rn = instr & 0xF;

			cpsr.flags.t = GetReg(rn) & 1;

			SetReg(15, GetReg(rn) & ~1);

			printf("bx r%d\n", rn);

			FlushPipeline();
		}
		else if (IsBranch(instr))
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
			else if (!l && !b)
			{
				printf("Unhandled str\n");
				exit(1);
			}
			else
			{
				printf("strb r%d, %s\n", rd, op2.c_str());
				Bus::Write8_ARM7(addr, GetReg(rd));
			}

			if (!p)
				addr += u ? offset : -offset;

			if (w)
				SetReg(rn, addr);
			
			if ((!l || rd != 15) && (!w || rn != 15))
			{
				GetReg(15) += 4;
			}
		}
		else if (IsPSRTransferMSR(instr))
		{
			bool i = (instr >> 25) & 1;
			bool _r = (instr >> 22) & 1;
			uint8_t field_mask = (instr >> 16) & 0xF;

			uint32_t operand_2;
			std::string op_2_disasm;

			int old_mode = cpsr.flags.mode;

			if (i)
			{
				uint32_t imm = instr & 0xFF;

				op_2_disasm = "#" + std::to_string(imm);

				uint8_t shamt = (instr >> 8) & 0xF;

				if (shamt)
				{
					operand_2 = std::rotr<uint32_t>(imm, shamt);
					op_2_disasm += ", #" + std::to_string(shamt);
				}
				else
					operand_2 = imm;
			}
			else
			{
				uint8_t rm = instr & 0xf;

				op_2_disasm = "r" + std::to_string(rm);
				operand_2 = GetReg(rm);
			}

			ARM9::PSR* target_psr = &cpsr;

			if (_r && cur_spsr)
			{
				cur_spsr->val = operand_2;
			}
			else
			{
				cpsr.val = operand_2;
			}
			
			bool c = (field_mask) & 1;
			bool x = (field_mask >> 1) & 1;
			bool s = (field_mask >> 2) & 1;
			bool f = (field_mask >> 3) & 1;

			std::string fields;

			if (f)
				fields += "f";
			if (s)
				fields += "s";
			if (x)
				fields += "x";
			if (c)
				fields += "c";
			
			if (!fields.empty())
				fields.insert(fields.begin(), '_');

			if (cpsr.flags.mode != old_mode)
			{
				switch (cpsr.flags.mode)
				{
				case 0:
				case 0x1f:
					for (int i = 0; i < 16; i++)
						registers[i] = &regs_sys[i];
					cur_spsr = nullptr;
					break;
				case 0x12:
					registers[13] = &r_irq[0];
					registers[14] = &r_irq[1];
					cur_spsr = &spsr_irq;
					break;
				case 0x13:
					registers[13] = &r_svc[0];
					registers[14] = &r_svc[1];
					cur_spsr = &spsr_svc;
					break;
				case 0x17:
					registers[13] = &r_abt[0];
					registers[14] = &r_abt[1];
					cur_spsr = &spsr_abr;
					break;
				default:
					printf("Unknown mode 0x%x\n", cpsr.flags.mode);
					exit(1);
				}
			}

			printf("msr %s%s, %s\n", _r ? "spsr" : "cpsr", fields.c_str(), op_2_disasm.c_str());

			GetReg(15) += 4;
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
			case 0x04:
			{
				printf("add r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				uint32_t a = GetReg(rn);
				uint32_t b = op2;

				uint32_t result = a + b;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = result & (1 << 31);
				cpsr.flags.c = !OverflowFrom(a, b);
				cpsr.flags.v = OverflowFrom(a, b);

				SetReg(rd, result);
				break;
			}
			case 0x09:
			{
				printf("teq r%d, %s\n", rn, op2_disasm.c_str());

				uint32_t a = GetReg(rn);
				uint32_t b = op2;

				uint32_t result = a | b;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				break;
			}
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

			if (rd != 15 || opcode == 0x0a || opcode == 0x09)
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

bool IsBranchExchange(uint32_t i)
{
	return ((i >> 4) & 0xFFFFFF) == 0x12FFF1;
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

bool IsPSRTransferMSR(uint32_t opcode)
{
	uint32_t msrFormat = 0x120F000;
	uint32_t formatMask = 0xDB0F000;

	uint32_t extractedFormat = opcode & formatMask;

	return extractedFormat == msrFormat;
}
bool IsMovCmpSubAdd(uint16_t i)
{
	return ((i >> 13) & 0b111) == 0b001;
}
bool IsPCRelativeLoad(uint16_t i)
{
    return ((i >> 11) & 0b11111) == 0b01001;
}
bool IsLoadStoreRegister(uint16_t i)
{
	bool cond1 = ((i >> 12) & 0b1111) == 0b0101;
	bool cond2 = ((i >> 9) & 0b1) == 0b0;

	return cond1 && cond2;
}
}