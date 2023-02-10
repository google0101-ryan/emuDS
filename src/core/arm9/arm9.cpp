#include <src/core/arm9/arm9.h>
#include <src/core/arm9/cp15.h>
#include <src/core/gpu/gpu.h>

#include <cassert>
#include <cstring>
#include <bit>
#include <string>
#include <sstream>
#include "arm9.h"

namespace ARM9
{

bool can_disassemble = false;
bool singleStep = false;

uint32_t r[16];
uint32_t r_svc[2];
uint32_t r_irq[2];
uint32_t r_abt[2];
uint32_t r_fiq[2];

uint32_t* cur_r[16];

uint32_t pipeline[2];
uint16_t t_pipeline[2];

bool is_thumb = false;

PSR cpsr, spsr_fiq, spsr_svc, spsr_abr, spsr_irq, spsr_und;
PSR* cur_spsr = nullptr;

bool direct_booted = false;

void SetReg(int reg, uint32_t data)
{
    *cur_r[reg] = data;
}

uint32_t& GetReg(int reg)
{
    return *cur_r[reg];
}

void FlushPipeline()
{
    if (is_thumb)
    {
		t_pipeline[0] = Bus::Read16(GetReg(15));
		GetReg(15) += 2;
		t_pipeline[1] = Bus::Read16(GetReg(15));
		GetReg(15) += 2;
    }
    else
    {
        pipeline[0] = Bus::Read32(GetReg(15));
        GetReg(15) += 4;
        pipeline[1] = Bus::Read32(GetReg(15));
        GetReg(15) += 4;
    }
}

uint32_t AdvanceARMPipeline()
{
    uint32_t i = pipeline[0];
    pipeline[0] = pipeline[1];
    pipeline[1] = Bus::Read32(GetReg(15));

    return i;
}

uint16_t AdvanceThumbPipeline()
{
	uint16_t i = t_pipeline[0];
	t_pipeline[0] = t_pipeline[1];
	t_pipeline[1] = Bus::Read16(GetReg(15));

	return i;
}

void Reset()
{
	if (direct_booted)
		return;

    for (int i = 0; i < 16; i++)
        cur_r[i] = &r[i];
    
    memset(r, 0, sizeof(r));
    
    SetReg(15, 0xFFFF0000);

    is_thumb = false;
	cur_spsr = nullptr;

    FlushPipeline();
}

bool OverflowFrom(uint32_t a, uint32_t b)
{
	uint32_t s = a + b;

	if ((a & (1 << 31)) == (b & (1 << 31)) && (s & (1 << 31)) != (a & (1 << 31)))
		return true;
	
	return false;
}

unsigned int countSetBits(unsigned int n)
{
    unsigned int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

std::string convert_int(int n)
{
   std::stringstream ss;
   ss << std::hex << n;
   return ss.str();
}

void ThumbPush(uint16_t i)
{
	uint8_t reg_list = i & 0xff;
	int n = countSetBits(reg_list);
	bool r = (i >> 8) & 1;

	uint32_t op0 = GetReg(13) - n * 4 + r;

	SetReg(13, op0);

	std::string regs;

	for (int j = 0; j < 8; j++)
	{
		if (i & (1 << j))
		{
			regs += "r" + std::to_string(j) + ", ";
			Bus::Write32(op0, GetReg(j));
			op0 += 4;
		}
	}

	if (r)
	{
		Bus::Write32(op0, GetReg(14));
		regs += "lr, ";
	}

	regs.pop_back();
	regs.pop_back();

	if (can_disassemble)
		printf("push { %s }\n", regs.c_str());

	GetReg(15) += 2;
	
}

void ThumbPop(uint16_t i)
{
	uint8_t reg_list = i & 0xff;
	bool r = (i >> 8) & 1;

	uint32_t addr = GetReg(13);

	std::string regs;

	for (int i = 7; i >= 0; i--)
	{
		if (reg_list & (1 << i))
		{
			regs += "r" + std::to_string(i) + ", ";
			SetReg(i, Bus::Read32(addr));
			addr += 4;
		}
	}


	regs.pop_back();
	regs.pop_back();

	if (r)
	{
		regs += ", pc";
		SetReg(15, Bus::Read32(addr) & ~1);
		FlushPipeline();
		addr += 4;
	}
	else
		GetReg(15) += 2;

	SetReg(13, addr);
	
	if (can_disassemble)
		printf("pop {%s}\n", regs.c_str());
}

void DirectBoot(uint32_t entry)
{
    for (int i = 0; i < 16; i++)
        cur_r[i] = &r[i];
    
    memset(r, 0, sizeof(r));

	cur_spsr = nullptr;

	SetReg(15, entry);
	cpsr.val = 0;
	FlushPipeline();

	SetReg(12, entry);
	SetReg(13, entry);

	SetReg(13, 0x030027FC);
	r_irq[0] = 0x03003F80;
	r_svc[0] = 0x03003F80;

	direct_booted = true;

	Bus::RemapDTCM(0x00800000);
}

template <class T>
T sign_extend(T x, const int bits)
{
	T m = 1;
    m <<= bits - 1;
    return (x ^ m) - m;
}

void Clock()
{
	if (singleStep)
	{
		can_disassemble = true;
		getc(stdin);
		Dump();
	}

    if (is_thumb)
    {
		uint16_t instr = AdvanceThumbPipeline();
		if (can_disassemble)
			printf("0x%08x (0x%04x): ", GetReg(15) - 6, instr);

		if (IsArithmeticThumb(instr))
		{
			uint8_t op = (instr >> 11) & 0b11;
			uint8_t rd = (instr >> 8) & 0b111;
			uint8_t offset8 = instr & 0xff;

			switch (op)
			{
			case 0x00:
			{
				SetReg(rd, offset8);
				if (can_disassemble)
					printf("mov r%d, #%d\n", rd, offset8);
				break;
			}
			case 0x01:
			{
				uint32_t result = GetReg(rd) + offset8;

				cpsr.flags.c = !OverflowFrom(GetReg(rd), -offset8);
				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = OverflowFrom(GetReg(rd), -offset8);

				if (can_disassemble)
					printf("cmp r%d, #%d\n", rd, offset8);
				break;
			}
			case 0x02:
			{
				uint64_t result = GetReg(rd) + offset8;

				cpsr.flags.c = (result >> 32);
				cpsr.flags.z = (result & 0xffffffff) == 0;
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = OverflowFrom(GetReg(rd), offset8);

				SetReg(rd, result);

				if (can_disassemble)
					printf("add r%d, #%d\n", rd, offset8);

				break;
			}
			case 0x03:
			{
				uint64_t result = GetReg(rd) - offset8;

				cpsr.flags.c = (result >> 32);
				cpsr.flags.z = (result & 0xffffffff) == 0;
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = OverflowFrom(GetReg(rd), -offset8);

				SetReg(rd, result);

				if (can_disassemble)
					printf("sub r%d, #%d\n", rd, offset8);

				break;
			}
			default:
				printf("Unknown THUMB arithmetic opcode 0x%02x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsConditionalBranch(instr))
		{
			int8_t imm8 = instr & 0xff;

			int32_t offset = imm8 << 1;
			
			if (!CondPassed((instr >> 8) & 0xF))
				return;

			if (can_disassemble)
				printf("b 0x%08x (%d, 0x%08x)\n", GetReg(15) + offset, offset, GetReg(15));
			
			GetReg(15) += offset;

			FlushPipeline();
		}
		else if (IsBranchExchangeThumb(instr))
		{
			uint8_t rm = (instr >> 3) & 0xF;

			cpsr.flags.t = GetReg(rm) & 1;
			is_thumb = cpsr.flags.t;

			GetReg(15) = GetReg(rm) & ~1;

			if (can_disassemble)
				printf("bx r%d (0x%08x)\n", rm, GetReg(15));

			FlushPipeline();
		}
		else if (IsLDR_PCRel(instr))
		{
			uint8_t rt = (instr >> 8) & 0x7;
			uint32_t imm8 = instr & 0xff;
			imm8 <<= 2;

			uint32_t pc = GetReg(15) & ~3;

			SetReg(rt, Bus::Read32(pc + imm8));

			if (can_disassemble)
				printf("ldr r%d, #%d (0x%08x)\n", rt, imm8, pc + imm8);

			GetReg(15) += 2;
		}
		else if (IsSTR_Reg(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rn = (instr >> 3) & 0x7;
			uint8_t rm = (instr >> 6) & 0x7;

			if (can_disassemble)
				printf("str r%d, [r%d, r%d]\n", rd, rn, rm);

			uint32_t addr = GetReg(rn) + GetReg(rm);

			Bus::Write32(addr, GetReg(rd));
			
			GetReg(15) += 2;
		}
		else if (IsPushPop(instr))
		{
			bool l = (instr >> 11) & 1;
			if (l)
			{
				ThumbPop(instr);
			}
			else
			{
				ThumbPush(instr);
			}
		}
		else if (IsSTRH_Imm(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rn = (instr >> 3) & 0x7;
			uint8_t imm5 = ((instr >> 6) & 0x1F) << 1;
			
			std::string disasm = "strh r" + std::to_string(rd) + ", [r"
				+ std::to_string(rn);
			
			if (imm5)
				disasm += ", #" + std::to_string(imm5);
			
			disasm += "]";

			if (can_disassemble)
				printf("%s\n", disasm.c_str());

			uint32_t addr = GetReg(rn);
			addr += imm5;

			Bus::Write16(addr, GetReg(rd));

			GetReg(15) += 2;
		}
		else if (IsBranchLink(instr))
		{
			uint8_t h = (instr >> 11) & 0b11;
			uint32_t imm11 = instr & 0x7FF;

			if (h == 0b10)
			{
				uint32_t addr = GetReg(15) + (int32_t)sign_extend<uint32_t>(imm11 << 12, 23);
				if (can_disassemble)
					printf("First half: 0x%08x (%d)\n", addr, (int32_t)sign_extend<uint32_t>(imm11 << 12, 23));
				SetReg(14, addr);
				GetReg(15) += 2;
			}
			else if (h == 0b11)
			{
				uint32_t lr = GetReg(14);
				SetReg(14, (GetReg(15) - 2) | 1);
				SetReg(15, lr + (imm11 << 1));
				if (can_disassemble)
					printf("bl 0x%08x\n", GetReg(15));
				FlushPipeline();
			}
			else if (h == 0b01)
			{
				uint32_t lr = GetReg(14);
				SetReg(14, (GetReg(15) - 2) | 1);
				SetReg(15, (lr + (imm11 << 1)) & 0xFFFFFFFC);
				cpsr.flags.t = 0;
				is_thumb = false;
				if (can_disassemble)
					printf("blx 0x%08x\n", GetReg(15));
				FlushPipeline();
			}
		}
		else if (IsLoadHalfwordImmediate(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rn = (instr >> 3) & 0x7;
			uint8_t imm5 = ((instr >> 6) & 0x1F) << 1;

			std::string disasm = "";
			
			if (imm5)
				disasm += ", #" + std::to_string(imm5);

			if (can_disassemble)
				printf("ldrh r%d, [r%d%s]\n", rd, rn, disasm.c_str());

			uint32_t addr = GetReg(rn) + imm5;

			SetReg(rd, Bus::Read16(addr));

			GetReg(15) += 2;
		}
		else if (IsLSL1(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rm = (instr >> 3) & 0x7;
			uint8_t imm5 = ((instr >> 6) & 0x1F);

			if (!imm5)
				SetReg(rd, GetReg(rm));
			else
			{
				cpsr.flags.c = GetReg(rm) & (1 << (32 - imm5));
				SetReg(rd, GetReg(rm) << imm5);
			}

			uint32_t result = GetReg(rd);

			cpsr.flags.n = result & (1 << 31);
			cpsr.flags.z = (result == 0);
			
			if (can_disassemble)
				printf("lsl r%d, r%d, #%d\n", rd, rm, imm5);
			
			GetReg(15) += 2;
		}
		else if (IsLSR1(instr))
		{
			static bool alreadyDone = false;
			uint8_t rd = instr & 0x7;
			uint8_t rm = (instr >> 3) & 0x7;
			uint8_t imm5 = ((instr >> 6) & 0x1F);

			if (!imm5)
			{
				cpsr.flags.c = GetReg(rd) & (1 << 31);
				SetReg(rd, 0);
			}
			else
			{
				cpsr.flags.c = GetReg(rm) & (1 << (imm5 - 1));
				SetReg(rd, GetReg(rm) >> imm5);
			}

			uint32_t result = GetReg(rd);

			cpsr.flags.n = result & (1 << 31);
			cpsr.flags.z = (result == 0);
			
			if (can_disassemble)
				printf("lsr r%d, r%d, #%d\n", rd, rm, imm5);

			GetReg(15) += 2;
		}
		else if (IsCMP2(instr))
		{
			uint8_t rn = instr & 0x7;
			uint8_t rm = (instr >> 3) & 0x7;

			uint32_t result = GetReg(rn) - GetReg(rm);
			cpsr.flags.n = (result >> 31) & 1;
			cpsr.flags.z = (result == 0);
			cpsr.flags.c = !OverflowFrom(GetReg(rn), -GetReg(rm));
			cpsr.flags.v = OverflowFrom(GetReg(rn), -GetReg(rm));

			if (can_disassemble)
				printf("cmp r%d, r%d\n", rn, rm);

			GetReg(15) += 2;
		}
		else if (IsLDR_Imm(instr))
		{
			bool b = (instr >> 12) & 1;
			bool l = (instr >> 11) & 1;

			uint8_t offset5 = ((instr >> 6) & 0x1F);

			uint8_t rb = (instr >> 3) & 7;
			uint8_t rd = instr & 7;

			if (!b)
				offset5 <<= 2;

			uint32_t addr = GetReg(rb) + offset5;

			if (l)
			{
				if (can_disassemble)
					printf("ldr%s r%d, [r%d, #%d]\n", b ? "b" : "", rd, rb, offset5);

				if (!b)
					SetReg(rd, Bus::Read32(addr));
				else
					SetReg(rd, Bus::Read8(addr));
			}
			else
			{
				if (can_disassemble)
					printf("str%s r%d, [r%d, #%d]\n", b ? "b" : "", rd, rb, offset5);

				if (!b)
					Bus::Write32(addr, GetReg(rd));
				else
					Bus::Write8(addr, GetReg(rd));
			}

			GetReg(15) += 2;
		}
		else if (IsALUThumb(instr))
		{
			uint8_t op = (instr >> 6) & 0xF;
			uint8_t rs = (instr >> 3) & 0x7;
			uint8_t rd = instr & 0x7;

			switch (op)
			{
			case 0xF:
			{
				uint32_t result = ~GetReg(rs);

				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.z = (result == 0);

				SetReg(rd, result);
				if (can_disassemble)
					printf("mvn r%d, r%d\n", rd, rs);
				break;
			}
			default:
				printf("Unknown THUMB ALU op 0x%x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsSPRelativeLoadStore(instr))
		{
			bool l = (instr >> 11) & 1;
			uint8_t rd = (instr >> 8) & 0x7;
			uint8_t word8 = instr & 0xff;

			if (l)
			{
				if (can_disassemble)
					printf("ldr r%d, [sp", rd);
				if (word8)
					if (can_disassemble)
						printf(", #%d", word8);
				if (can_disassemble)
					printf("]\n");
				SetReg(rd, Bus::Read32(GetReg(13) + word8));
			}
			else
			{
				if (can_disassemble)
					printf("str r%d, [sp", rd);
				if (word8)
					if (can_disassemble)
						printf(", #%d", word8);
				if (can_disassemble)
					printf("]\n");
				Bus::Write32(GetReg(13) + word8, GetReg(rd));
			}

			GetReg(15) += 2;
		}
		else if (IsHiRegisterOperation(instr))
		{
			uint8_t op = (instr >> 8) & 0b11;
			bool h1 = (instr >> 7) & 1;
			bool h2 = (instr >> 6) & 1;
			uint8_t rs = (instr >> 3) & 7;
			uint8_t rd = instr & 7;

			if (h1)
				rd += 8;
			if (h2)
				rs += 8;
			
			switch (op)
			{
			case 2:
				SetReg(rd, GetReg(rs));
				if (can_disassemble)
					printf("mov r%d, r%d\n", rd, rs);
				break;
			default:
				printf("Unknown THUMB Hi-op 0x%x\n", op);
				exit(1);
			}

			if ((rd != 15 || op == 1) && op != 3)
				GetReg(15) += 2;
			else
				FlushPipeline();
		}
		else
		{
			printf("Unknown THUMB instruction 0x%04x\n", instr);
			exit(1);
		}
    }
    else
    {
        uint32_t instr = AdvanceARMPipeline();

        uint8_t cond = (instr >> 28) & 0xF;

        if (!CondPassed(cond))
        {
            GetReg(15) += 4;
            return;
        }

		if (GetReg(15)-2 == 0x42b4)
			can_disassemble = true;
		if (GetReg(15)-2 == 0x41b8)
			can_disassemble = false;

		if (can_disassemble)
			printf("(0x%08x) 0x%08x: ", instr, GetReg(15) - 8);

		if (IsBranchExchange2(instr))
		{
			uint8_t rn = instr & 0xF;

			cpsr.flags.t = GetReg(rn) & 1;
			is_thumb = cpsr.flags.t;

			if (can_disassemble)
				printf("bx r%d\n", rn);

			GetReg(15) = GetReg(rn) & ~1;

			FlushPipeline();
		}
		else if (IsBranchExchange(instr))
		{
            int32_t offset = (int16_t)(instr & 0xffffff) << 2;

			bool h = (instr >> 24) & 1;

			offset |= (h << 1);

			is_thumb = true;
			cpsr.flags.t = 1;

			if (can_disassemble)
				printf("blx 0x%08x\n", GetReg(15) + offset);

			SetReg(14, GetReg(15) - 4);

			SetReg(15, GetReg(15) + offset);

			FlushPipeline();
		}
		else if (IsMulMula(instr))
		{
			bool a = (instr >> 21) & 1;
			bool s = (instr >> 20) & 1;

			uint8_t rm = instr & 0xF;
			uint8_t rs = (instr >> 8) & 0xF;
			uint8_t rn = (instr >> 12) & 0xF;
			uint8_t rd = (instr >> 16) & 0xF;
			
			uint64_t result = (uint64_t)GetReg(rm) * (uint64_t)GetReg(rs);

			if (a)
				result += (uint64_t)GetReg(rn);

			uint32_t res32 = (uint32_t)result;
			
			if (s)
			{
				cpsr.flags.c = 0;
				cpsr.flags.n = (res32 >> 31) & 1;
				cpsr.flags.z = res32 == 0;
			}

			GetReg(rd) = result;

			if (can_disassemble)
			{
				printf("%s%s r%d, r%d, r%d", a ? "mla" : "mul", s ? "s" : "", rd, rm, rs);
				if (a)
					printf(", r%d", rn);
				printf("\n");
			}

			if (rd != 15)
				GetReg(15) += 4;
			else
				FlushPipeline();
		}
		else if (IsMullMlal(instr))
		{
			bool u = (instr >> 22) & 1;
			bool a = (instr >> 21) & 1;
			bool s = (instr >> 20) & 1;
			uint8_t rdhi = (instr >> 16) & 0xF;
			uint8_t rdlo = (instr >> 12) & 0xF;
			uint8_t rs = (instr >> 8) & 0xF;
			uint8_t rm = instr & 0xF;

			uint64_t result;

			if (u)
			{
				result = (int64_t)GetReg(rm) * (int64_t)GetReg(rs);
			}
			else
			{
				result = GetReg(rm) * GetReg(rs);
			}

			if (a)
			{
				uint64_t res = GetReg(rdlo);
				res |= ((uint64_t)GetReg(rdhi) << 32);

				result += res;
			}

			if (s)
			{
				cpsr.flags.n = (result >> 63) & 1;
				cpsr.flags.z = (result == 0);
				cpsr.flags.c = 0;
				cpsr.flags.v = 0;
			}

			SetReg(rdhi, result >> 32);
			SetReg(rdlo, result);

			if (rdhi != 15 && rdlo != 15)
				GetReg(15) += 4;
			else
				FlushPipeline();
		}
		else if (IsBlockDataTransfer(instr))
		{
			uint16_t reg_list = instr & 0xffff;
			bool p = (instr >> 24) & 1;
			bool u = (instr >> 23) & 1;
			bool s = (instr >> 22) & 1;
			bool w = (instr >> 21) & 1;
			bool l = (instr >> 20) & 1;

			uint8_t rn = (instr >> 16) & 0xF;

			uint32_t addr = GetReg(rn);
			if (rn == 15)
				addr += 4;

			std::string regs;
			bool modified_pc = false;

			if (l)
			{
				for (int i = 15; i >= 0; i--)
				{
					if (reg_list & (1 << i))
					{
						regs += "r" + std::to_string(i) + ", ";

						if (p)
							addr += u ? 4 : -4;
						
						SetReg(i, Bus::Read32(addr));
					
						if (!p)
							addr += u ? 4 : -4;
					}
				}
			}
			else
			{
				for (int i = 0; i < 16; i++)
				{
					if (reg_list & (1 << i))
					{
						regs += "r" + std::to_string(i) + ", ";

						if (p)
							addr += u ? 4 : -4;
						
						Bus::Write32(addr, GetReg(i));
					
						if (!p)
							addr += u ? 4 : -4;
					}
				}
			}

			regs.pop_back();
			regs.pop_back();

			if (w)
				SetReg(rn, addr);

			if (!l || !modified_pc)
			{
				if (!w || rn != 15)
					GetReg(15) += 4;
				else
					FlushPipeline();
			}
			else
				FlushPipeline();

			if (rn == 13)
			{
				if (l && p && u)
				{
					if (can_disassemble)
						printf("ldmed ");
				}
				else if (l && !p && u)
				{
					if (can_disassemble)
						printf("ldmfd ");
				}
				else if (l && p && !u)
				{
					if (can_disassemble)
						printf("ldmea ");
				}
				else if (l && !p && !u)
				{
					if (can_disassemble)
						printf("ldmfa ");
				}
				else if (!l && p && u)
				{
					if (can_disassemble)
						printf("stmfa ");
				}
				else if (!l && !p && u)
				{
					if (can_disassemble)
						printf("stmea ");
				}
				else if (!l && p && !u)
				{
					if (can_disassemble)
						printf("stmfd ");
				}
				else if (!l && !p && !u)
				{
					if (can_disassemble)
						printf("stmed ");
				}
			}
			else
			{
				if (l && p && u)
				{
					if (can_disassemble)
						printf("ldmib ");
				}
				else if (l && !p && u)
				{
					if (can_disassemble)
						printf("ldmia ");
				}
				else if (l && p && !u)
				{
					if (can_disassemble)
						printf("ldmdb ");
				}
				else if (l && !p && !u)
				{
					if (can_disassemble)
						printf("ldmda ");
				}
				else if (!l && p && u)
				{
					if (can_disassemble)
						printf("stmib ");
				}
				else if (!l && !p && u)
				{
					if (can_disassemble)
						printf("stmia ");
				}
				else if (!l && p && !u)
				{
					if (can_disassemble)
						printf("stmdb ");
				}
				else if (!l && !p && !u)
				{
					if (can_disassemble)
						printf("stmda ");
				}
			}

			if (can_disassemble)
				printf("r%d%s, {%s}\n", rn, w ? "!" : "", regs.c_str());
		}
        else if (IsBranchAndLink(instr))
        {
            bool is_link = (instr >> 24) & 1;

            int32_t offset = (int16_t)(instr & 0xffffff) << 2;

            if (is_link)
                SetReg(14, GetReg(15) - 4);
            
            GetReg(15) += offset;

			if (GetReg(15) == 0xffff01e0)
			{
				printf("Entering infinite loop\n");
				exit(1);
			}

            if (can_disassemble)
				printf("b%s 0x%08x\n", is_link ? "l" : "", GetReg(15));

            FlushPipeline();
        }
		else if (IsHalfwordTransfer(instr))
		{
			bool p = (instr >> 24) & 1;
			bool u = (instr >> 23) & 1;
			bool w = (instr >> 21) & 1;
			bool l = (instr >> 20) & 1;

			uint8_t sh = (instr >> 5) & 0b11;

			uint8_t rn = (instr >> 16) & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;
			
			uint8_t offset = instr & 0xF;
			offset |= ((instr >> 8) & 0xF) << 4;

			uint32_t addr = GetReg(rn);

			if (can_disassemble)
				printf("%s%s r%d, [r%d", l ? "ldrh" : "strh", w ? "!" : "", rd, rn);

			if (offset)
			{
				if (can_disassemble)
					printf(", #%d", offset);
			}
			
			if (p)
				addr += u ? offset : -offset;

			if (can_disassemble)
				printf("] (0x%08x)\n", addr);
			
			switch (sh)
			{
			case 0b00:
			{
				uint8_t rm = instr & 0xF;
				bool b = (instr >> 22) & 1;
				uint32_t addr = GetReg(rn);
				if (b)
				{
					uint8_t byte = Bus::Read8(addr);
					Bus::Write8(addr, GetReg(rm));
					SetReg(rd, byte);
				}
				else
				{
					uint32_t word = Bus::Read32(addr);
					Bus::Write32(addr, GetReg(rm));
					SetReg(rd, word);
				}

				GetReg(15) += 4;

				return;
			}
			case 0b01:
				if (l)
				{
					SetReg(rd, Bus::Read16(addr & ~1));
				}
				else
				{
					Bus::Write16(addr & ~1, GetReg(rd));
				}
				break;
			default:
				printf("Unknown SH %d\n", sh);
				exit(1);
			}

			if (!p)
			{
				addr += u ? offset : -offset;
				if (rn != rd)
					SetReg(rn, addr);
			}
			
			if (w)
				SetReg(rn, addr);
			
			if (!l || rd != 15)
			{
				if (!w || rn != 15)
					GetReg(15) += 4;
			}
		}
		else if (IsHalfwordTransfer2(instr))
		{
			bool p = (instr >> 24) & 1;
			bool u = (instr >> 23) & 1;
			bool w = (instr >> 21) & 1;
			bool l = (instr >> 20) & 1;
			uint8_t rn = (instr >> 16) & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;
			uint8_t sh = (instr >> 5) & 3;
			uint8_t rm = instr & 0xF;

			uint32_t addr = GetReg(rn);

			if (p)
				addr += u ? GetReg(rm) : -GetReg(rm);
			
			switch (sh)
			{
			case 0b01:
			{
				if (l)
				{
					if (can_disassemble)
						printf("ldrh r%d, [r%d, r%d]\n", rd, rn, rm);
					SetReg(rd, Bus::Read16(addr));
				}
				else
				{
					if (can_disassemble)
						printf("strh r%d, [r%d, r%d]\n", rd, rn, rm);
					Bus::Write16(addr, GetReg(rd));
				}
				break;
			}
			case 0b00:
			{
				bool b = (instr >> 22) & 1;
				uint32_t addr = GetReg(rn);
				if (b)
				{
					uint8_t byte = Bus::Read8(addr);
					Bus::Write8(addr, GetReg(rm));
					SetReg(rd, byte);
				}
				else
				{
					uint32_t word = Bus::Read32(addr);
					Bus::Write32(addr, GetReg(rm));
					SetReg(rd, word);
				}

				GetReg(15) += 4;

				return;
			}
			default:
				printf("Unknown sh %d (0x%08x)\n", sh, instr);
				exit(1);
			}

			if (!p)
			{
				addr += u ? GetReg(rm) : -GetReg(rm);
				if (rn != rd)
					SetReg(rn, addr);
			}
			
			if (w)
				SetReg(rn, addr);
			
			if ((!l || rd != 15) && (!w || rn != 15))
			{
				GetReg(15) += 4;
			}
			else
			{
				FlushPipeline();
			}
		}
        else if (IsSingleDataTransfer(instr))
        {
            bool i = ~((instr >> 25) & 1);
            bool p = (instr >> 24) & 1;
            bool u = (instr >> 23) & 1;
            bool b = (instr >> 22) & 1;
            bool w = (instr >> 21) & 1;
            bool l = (instr >> 20) & 1;
            
            uint8_t rn = (instr >> 16) & 0xF;
            uint8_t rd = (instr >> 12) & 0xF;

            uint32_t offset;

            uint32_t addr = GetReg(rn);

            if (i)
            {
                offset = instr & 0xfff;
            }
            else
            {
                printf("Unhandled register off!\n");
                exit(1);
            }

            if (p)
            {
                addr += u ? offset : -offset;
            }

            std::string disasm;

            if (b && l)
            {
                disasm = "ldrb";
				if (w)
					disasm += "!";
				disasm += " r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                if (can_disassemble)
					printf("%s\n", disasm.c_str());
                SetReg(rd, Bus::Read8(addr));
            }
            else if (b && !l)
            {
                disasm = "strb r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                if (can_disassemble)
					printf("%s (0x%08x, 0x%08x)\n", disasm.c_str(), addr, GetReg(rd));
                
				Bus::Write8(addr, GetReg(rd));
            }
            else if (l && !b)
            {
				disasm = "ldr r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                if (can_disassemble)
					printf("%s (0x%08x)\n", disasm.c_str(), addr);
                
				uint32_t data = Bus::Read32(addr & ~3);

				if (addr & 3)
				{
					data = std::rotr<uint32_t>(data, (addr & 3) * 8);
				}

				SetReg(rd, data);
            }
            else
            {
                disasm = "str r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                if (can_disassemble)
					printf("%s\n", disasm.c_str());
                Bus::Write32(addr & ~3, GetReg(rd));
            }

            if (!p)
			{
                addr += u ? offset : -offset;
				if (rn != rd)
					SetReg(rn, addr);
			}

            if (w)
                SetReg(rn, addr);
            
            if (!l || rd != 15)
            {
                GetReg(15) += 4;
            }
        }
		else if (IsBranchLinkExchange(instr))
		{
			uint8_t rm = instr & 0xF;

			cpsr.flags.t = GetReg(rm) & 1;
			is_thumb = cpsr.flags.t;

			if (can_disassemble)
				printf("blx r%d\n", rm);

			GetReg(14) = GetReg(15) - 4;

			GetReg(15) = GetReg(rm) & ~1;

			FlushPipeline();
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
					operand_2 = ror<uint32_t>(imm, shamt);
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

			PSR* target_psr = &cpsr;

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
						cur_r[i] = &r[i];
					cur_spsr = nullptr;
					break;
				case 0x2:
				case 0x12:
					cur_r[13] = &r_irq[0];
					cur_r[14] = &r_irq[1];
					cur_spsr = &spsr_irq;
					break;
				case 0x11:
					cur_r[13] = &r_fiq[0];
					cur_r[14] = &r_fiq[1];
					cur_spsr = &spsr_fiq;
					break;
				case 0x13:
					cur_r[13] = &r_svc[0];
					cur_r[14] = &r_svc[1];
					cur_spsr = &spsr_svc;
					break;
				case 0x17:
					cur_r[13] = &r_abt[0];
					cur_r[14] = &r_abt[1];
					cur_spsr = &spsr_abr;
					break;
				default:
					printf("Unknown mode 0x%x\n", cpsr.flags.mode);
					exit(1);
				}
			}

			if (can_disassemble)
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

            uint32_t second_op;
            
            std::string op2_disasm;

            if (i)
            {
                uint32_t imm = instr & 0xFF;
                uint8_t shift = (instr >> 8) & 0xF;

                second_op = imm;
                op2_disasm = "#" + std::to_string(imm);
                
                if (shift)
                {
                    shift <<= 1;
                    op2_disasm += ", #" + std::to_string(shift);
                    second_op = ror<uint32_t>(imm, shift);
                }
            }
            else
            {
				uint8_t rm = instr & 0xF;
				uint8_t shift = (instr >> 4) & 0xFF;

				second_op = GetReg(rm);
				op2_disasm = "r" + std::to_string(rm);

				bool is_shifted_by_rs = shift & 1;

				if (is_shifted_by_rs)
				{
					uint8_t rs = (shift >> 4) & 0xF;
					uint8_t shift_type = (shift >> 1) & 0x3;

					std::string shift_kind;

					uint32_t shamt = GetReg(rs) & 0x1F;

					if (!shamt && shift_type == 2)
						shamt = 32;
					
					if (rm == 15)
						second_op += 4;

					switch (shift_type)
						{
						case 0:
							second_op <<= shamt;
							shift_kind = "lsl";
							break;
						case 1:
							second_op >>= shamt;
							shift_kind = "lsr";
							break;
						case 2:
							second_op = ((int32_t)second_op) >> shamt;
							shift_kind = "asr";
							break;
						default:
							printf("Unknown shift type %d\n", shift_type);
							exit(1);
						}

						op2_disasm += ", r" + std::to_string(rs) + ", " + shift_kind + " #" + std::to_string(GetReg(rs));
				}
				else
				{
					uint8_t shamt = (shift >> 3) & 0x1F;
					uint8_t shift_type = (shift >> 1) & 3;

					if (!shamt && shift_type == 2)
						shamt = 32;

					if (shamt == 32 && second_op == 0x80000000 && shift_type == 2)
					{
						second_op = 0xffffffff;
						op2_disasm += ", asr #" + std::to_string(shamt);
					}
					else
					{
						switch (shift_type)
						{
						case 0:
							if (s)
								cpsr.flags.c = (second_op & (1 << (32 - shamt))) != 0;
							second_op <<= shamt;
							op2_disasm += ", lsl #" + std::to_string(shamt);
							break;
						case 1:
							if (s)
								cpsr.flags.c = (second_op & (1 << (shamt - 1))) != 0;
							second_op >>= shamt;
							op2_disasm += ", lsr #" + std::to_string(shamt);
							break;
						case 2:
						{
							if (s)
								cpsr.flags.c = (second_op & (1 << (shamt - 1))) != 0;
							int32_t s = (int32_t)second_op;
							second_op = (s >> shamt);
							op2_disasm += ", asr #" + std::to_string(shamt);
							break;
						}
						case 3:
							second_op = std::rotr(second_op, shamt);
							op2_disasm += ", ror #" + std::to_string(shamt);
							break;
						default:
							printf("Unknown shift type %d\n", shift_type);
							exit(1);
						}
					}
				}
            }

            switch (opcode)
            {
			case 0x00:
			{
				uint32_t result = GetReg(rn) & second_op;
				if (can_disassemble)
					printf("and%s r%d, r%d, %s (0x%08x, 0x%08x)\n", s ? "s" : "", rd, rn, op2_disasm.c_str(), GetReg(rn), second_op);
				
				SetReg(rd, result);
				
				if (s)
				{
					cpsr.flags.z = (result == 0);
					cpsr.flags.n = (result >> 31) & 1;
				}

				break;
			}
			case 0x01:
			{
				if (can_disassemble)
					printf("eor%s r%d, r%d, %s\n", s ? "s" : "", rd, rn, op2_disasm.c_str());
				SetReg(rd, GetReg(rn) ^ second_op);
				if (s)
				{
					cpsr.flags.z = (GetReg(rd) == 0);
					cpsr.flags.n = (GetReg(rd) >> 31) & 1;
				}
				break;
			}
			case 0x02:
			{
				uint32_t result = GetReg(rn) - second_op;

				if (s)
				{
					cpsr.flags.c = second_op >= GetReg(rn);
					cpsr.flags.z = result == 0;
					cpsr.flags.n = (result >> 31) & 1;
					cpsr.flags.v = ((second_op & (1 << 31)) != (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) == (second_op & (1 << 31)));
				}

				if (can_disassemble)
					printf("sub r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());
				GetReg(rd) = result;

				break;
			}
			case 0x04:
			{
				uint32_t result = GetReg(rn) + second_op;

				if (s)
				{
					cpsr.flags.c = GetReg(rn) > result;
					cpsr.flags.z = result == 0;
					cpsr.flags.n = ((result >> 31) & 1) != 0;
					cpsr.flags.v = ((second_op & (1 << 31)) == (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) != (second_op & (1 << 31)));
				}

				if (can_disassemble)
					printf("add r%d, r%d, %s (0x%08x, 0x%08x)\n", rd, rn, op2_disasm.c_str(), GetReg(rn), second_op);

				GetReg(rd) = result;

				break;
			}
			case 0x05:
			{
				uint32_t result = GetReg(rn) + second_op + cpsr.flags.c;

				if (s)
				{
					cpsr.flags.c = GetReg(rn) > result;
					cpsr.flags.z = result == 0;
					cpsr.flags.n = (result >> 31) & 1;
					cpsr.flags.v = ((second_op & (1 << 31)) == (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) != (second_op & (1 << 31)));
				}

				if (can_disassemble)
					printf("adc r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				GetReg(rd) = result;

				break;
			}
			case 0x06:
			{
				uint32_t result = GetReg(rn) - second_op - 1 + cpsr.flags.c;

				if (s)
				{
					cpsr.flags.c = GetReg(rn) > result;
					cpsr.flags.z = result == 0;
					cpsr.flags.n = (result >> 31) & 1;
					cpsr.flags.v = ((second_op & (1 << 31)) != (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) == (second_op & (1 << 31)));
				}

				if (can_disassemble)
					printf("sbc r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				GetReg(rd) = result;

				break;
			}
			case 0x07:
			{
				uint32_t result = second_op - GetReg(rn) - 1 + cpsr.flags.c;

				if (s)
				{
					cpsr.flags.c = second_op > result;
					cpsr.flags.z = result == 0;
					cpsr.flags.n = (result >> 31) & 1;
					cpsr.flags.v = ((second_op & (1 << 31)) != (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) == (second_op & (1 << 31)));
				}

				if (can_disassemble)
					printf("rsc r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				GetReg(rd) = result;

				break;
			}
			case 0x08:
			{
				uint32_t result = GetReg(rn) & second_op;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				
				if (can_disassemble)
					printf("tst%s r%d, %s (0x%08x)\n", s ? "s" : "", rn, op2_disasm.c_str(), result);
				
				break;
			}
            case 0x09:
            {
                uint32_t result = GetReg(rn) ^ second_op;

                cpsr.flags.c = 0;
                cpsr.flags.z = result == 0;
                cpsr.flags.n = (result >> 31) & 1;
                cpsr.flags.v = 0;

                if (can_disassemble)
					printf("teq r%d, %s\n", rn, op2_disasm.c_str());
                break;
            }
            case 0x0a:
            {
                uint32_t result = GetReg(rn) - second_op;

                //cpsr.flags.c = !OverflowFrom(GetReg(rn), -second_op);
				
                cpsr.flags.c = second_op >= GetReg(rn);
				cpsr.flags.z = result == 0;
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = ((second_op & (1 << 31)) != (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) == (second_op & (1 << 31)));
				
                if (can_disassemble)
					printf("cmp r%d, %s\n", rn, op2_disasm.c_str());
				break;
            }
            case 0x0b:
            {
                uint32_t result = GetReg(rn) + second_op;

                //cpsr.flags.c = !OverflowFrom(GetReg(rn), -second_op);
				
                cpsr.flags.c = result < GetReg(rn);
				cpsr.flags.z = result == 0;
				cpsr.flags.n = ((result >> 31) & 1) != 0;
				cpsr.flags.v = ((second_op & (1 << 31)) == (GetReg(rn) & (1 << 31)) && (result & (1 << 31)) != (second_op & (1 << 31)));
				
                if (can_disassemble)
					printf("cmn r%d, %s\n", rn, op2_disasm.c_str());
				break;
            }
            case 0xd:
            {
                SetReg(rd, second_op);

				if (s)
				{
					cpsr.flags.z = (second_op == 0);
					cpsr.flags.n = (second_op >> 31) & 1;
				}

                if (can_disassemble)
					printf("mov%s r%d, %s (0x%08x)\n", s ? "s" : "", rd, op2_disasm.c_str(), second_op);
                break;
            }
			case 0x0c:
            {
                uint32_t result = GetReg(rn) | second_op;

				if (s)
				{
					cpsr.flags.c = 0;
					cpsr.flags.z = result == 0;
					cpsr.flags.n = (result >> 31) & 1;
					cpsr.flags.v = 0;
				}

				SetReg(rd, result);

                if (can_disassemble)
					printf("orr r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());
                break;
            }
			case 0x0e:
			{
				if (can_disassemble)
					printf("bic%s r%d, r%d, %s\n", s ? "s" : "", rd, rn, op2_disasm.c_str());
				SetReg(rd, GetReg(rn) & ~second_op);
				if (s)
				{
					cpsr.flags.z = (GetReg(rd) == 0);
					cpsr.flags.n = (GetReg(rd) >> 31) & 1;
				}
				break;
			}
            case 0xf:
            {
                SetReg(rd, (~second_op));

				if (s)
				{
					cpsr.flags.z = ((~second_op) == 0);
					cpsr.flags.n = ((~second_op) >> 31) & 1;
				}

                if (can_disassemble)
					printf("mvn%s r%d, %s\n", s ? "s" : "", rd, op2_disasm.c_str());
                break;
            }
            default:
                printf("Unknown data processing opcode 0x%x (0x%08x)\n", opcode, instr);
                exit(1);
            }

            if (rd != 15)
                GetReg(15) += 4;
			else
				FlushPipeline();
        }
		else if (IsCPTransfer(instr))
		{
			bool l = (instr >> 20) & 1;

			uint8_t crn = (instr >> 16) & 0xF;
			uint8_t cp = (instr >> 5) & 0x7;
			uint8_t crm = instr & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;

			if (l)
			{
				if (can_disassemble)
					printf("mrc p15, #0, r%d, c%d, c%d, #%d\n", rd, crn, crm, cp);
				SetReg(rd, CP15::ReadCP15(crn, crm, cp));
			}
			else
			{
				if (can_disassemble)
					printf("mcr p15, #0, r%d, c%d, c%d, #%d\n", rd, crn, crm, cp);
				CP15::WriteCP15(crn, crm, cp, GetReg(rd));
			}

			GetReg(15) += 4;
		}
        else
        {
            printf("Unknown instruction 0x%08x\n", instr);
            exit(1);
        }
    }
}

void Dump()
{
    for (int i = 0; i < 16; i++)
        printf("r%d\t->\t0x%08x\n", i, GetReg(i));
	printf("[%s%s%s]\n", cpsr.flags.t ? "t" : ".", cpsr.flags.c ? "c" : ".", cpsr.flags.z ? "z" : ".");
	Bus::Dump();
	GPU::Draw();
	for (int i = 0; i < UINT32_MAX; i++)
	{
		int x = 0;
		x++;
	}
}

}