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
	case 0b0001:
		return !cpsr.flags.z;
	case 0b0010:
		return cpsr.flags.c;
	case 0b0011:
		return !cpsr.flags.c;
	case 0b0100:
		return cpsr.flags.n;
	case 0b0101:
		return !cpsr.flags.n;
	case 0b1000:
		return cpsr.flags.c && !cpsr.flags.z;
	case 0b1001:
		return !cpsr.flags.c || cpsr.flags.z;
	case 0b1010:
		return cpsr.flags.n == cpsr.flags.v;
	case 0b1011:
		return cpsr.flags.n != cpsr.flags.v;
	case 0b1100:
		return !cpsr.flags.z && (cpsr.flags.n == cpsr.flags.v);
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

unsigned int countSetBits(unsigned int n)
{
    unsigned int count = 0;
    while (n) {
        count += n & 1;
        n >>= 1;
    }
    return count;
}

void Clock()
{
	if (cpsr.flags.t)
	{
		uint16_t instr = AdvanceThumbPipeline();

		printf("(0x%08x) 0x%04x: ", GetReg(15) - 4, instr);

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
			case 0x01:
			{
				uint32_t result = GetReg(rd) - imm;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = !OverflowFrom(GetReg(rd), -imm);
				cpsr.flags.v = OverflowFrom(GetReg(rd), -imm);

				printf("cmp r%d, #%d\n", rd, imm);
				break;
			}
			case 0x02:
			{
				uint32_t result = GetReg(rd) + imm;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = !OverflowFrom(GetReg(rd), imm);
				cpsr.flags.v = OverflowFrom(GetReg(rd), imm);

				printf("add r%d, #%d\n", rd, imm);

				SetReg(rd, result);
				break;
			}
			case 0x03:
			{
				uint32_t result = GetReg(rd) - imm;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = !OverflowFrom(GetReg(rd), -imm);
				cpsr.flags.v = OverflowFrom(GetReg(rd), -imm);

				printf("sub r%d, #%d\n", rd, imm);

				SetReg(rd, result);
				break;
			}
			default:
				printf("[emu/ARM7]: Unknown sub-opcode 0x%02x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsPushPop(instr))
		{
			bool l = (instr >> 11) & 1;
			bool r = (instr >> 8) & 1;

			uint8_t reg_list = instr & 0xff;

			uint32_t addr = GetReg(13);

			if (l)
			{
				std::string registers;
				for (int i = 0; i < 8; i++)
				{
					if (reg_list & (1 << i))
					{
						registers += "r" + std::to_string(i) + ", ";
						uint32_t value = Bus::Read32_ARM7(addr);
						SetReg(i, value);
						addr += 4;
					}
				}

				registers.pop_back();
				registers.pop_back();

				if (r)
				{
					SetReg(15, Bus::Read32_ARM7(addr));
					addr += 4;
					FlushPipeline();
					registers += ", pc";
				}
				else
					GetReg(15) += 2;

				printf("pop {%s}\n", registers.c_str());

				SetReg(13, addr);
			}
			else
			{
				auto reg_count = countSetBits(reg_list);
				unsigned int regs = 0;

				addr -= countSetBits(reg_list) * 4;
				if (r)
					addr -= 4;

				SetReg(13, addr);

				printf("push {");

				for (int i = 0; i < 8; i++)
				{
					if (reg_list & (1 << i))
					{
						printf("r%d", i);
						regs++;
						if (regs != reg_count)
							printf(", ");
						Bus::Write32_ARM7(addr, GetReg(i));
						addr += 4;
					}
				}

				if (r)
				{
					Bus::Write32_ARM7(addr, GetReg(14));
					addr += 4;
					printf(", lr");
				}

				printf("}\n");

				GetReg(15) += 2;
			}
		}
		else if (IsHalfwordTransfer(instr))
		{
			bool l = (instr >> 11) & 1;
			uint8_t imm = ((instr >> 6) & 0x1F) << 1;
			uint8_t rb = (instr >> 3) & 7;
			uint8_t rd = instr & 7;

			uint32_t addr = GetReg(rb);
			addr += imm;

			printf("%s r%d, [r%d, #%d]\n", l ? "ldrh" : "strh", rd, rb, imm);

			if (l)
			{
				SetReg(rd, Bus::Read16_ARM7(addr));
			}
			else
			{
				Bus::Write16_ARM7(addr, GetReg(rd));
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
			else if (l && !b)
			{
				SetReg(rd, Bus::Read32_ARM7(addr));
				printf("ldr r%d, [r%d, r%d]\n", rd, rb, ro);
			}
			else if (l && b)
			{
				SetReg(rd, Bus::Read8_ARM7(addr));
				printf("ldrb r%d, [r%d, r%d]\n", rd, rb, ro);
			}
			else
			{
				printf("Unhandled l %d b %d combo\n", l, b);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsConditionalBranch(instr))
		{
			uint8_t cond = ((instr >> 8) & 0xF);
			int32_t offset = sign_extend<int32_t>((instr & 0xff) << 1, 9);

			printf("b 0x%08x\n", GetReg(15) + offset);

			if (!CondPassed(cond))
			{
				GetReg(15) += 2;
			}
			else
			{
				GetReg(15) += offset;
				FlushPipeline();
			}
		}
		else if (IsHighOp(instr))
		{
			uint8_t op = (instr >> 8) & 3;
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
			{
				printf("mov r%d, r%d\n", rd, rs);

				SetReg(rd, GetReg(rs) & ~1);

				if (rd == 15)
					FlushPipeline();
				else
					GetReg(15) += 2;

				break;
			}
			case 3:
			{
			 	printf("bx r%d\n", rs);
			
			 	uint32_t addr = GetReg(rs);

			 	cpsr.flags.t = addr & 1;

			 	SetReg(15, addr & ~1);
			 	FlushPipeline();
			 	break;
			}
			default:
				printf("Unknown HI opcode 0x%02x\n", op);
				exit(1);
			}
		}
		else if (IsBranchLongWithLink(instr))
		{
			bool h = (instr >> 11) & 1;
			uint32_t imm = instr & 0x7FF;

			if (!h)
			{
				imm <<= 12;
				int32_t imm_ = sign_extend<int32_t>(imm, 23);
				uint32_t addr = GetReg(15) + imm_;
				SetReg(14, addr);

				GetReg(15) += 2;

				printf("\n");
			}
			else
			{
				imm <<= 1;
				uint32_t lr = GetReg(14);
				SetReg(14, GetReg(15) - 2 | 1);
				SetReg(15, lr + imm);

				FlushPipeline();

				printf("bl 0x%08x\n", lr + imm);
			}
		}
		else if (IsMoveShifted(instr))
		{
			uint8_t op = (instr >> 11) & 0b11;
			uint8_t imm5 = (instr >> 6) & 0x1F;
			uint8_t rs = (instr >> 3) & 0x7;
			uint8_t rd = instr & 0x7;

			switch (op)
			{
			case 0:
				cpsr.flags.c = (GetReg(rs) & (1 << (32 - imm5))) != 0;
				SetReg(rd, GetReg(rs) << imm5);
				printf("lsl r%d, r%d, #%d\n", rd, rs, imm5);
				break;
			case 1:
				cpsr.flags.c = (GetReg(rs) & (1 << (imm5 - 1))) != 0;
				SetReg(rd, GetReg(rs) >> imm5);
				printf("lsr r%d, r%d, #%d\n", rd, rs, imm5);
				break;
			case 2:
			{
				int32_t v = (int32_t)GetReg(rs);
				v >>= imm5;
				SetReg(rd, v);
				printf("asr r%d, r%d, #%d\n", rd, rs, imm5);
				break;
			}
			default:
				printf("Unknown move-shifted opcode 0x%02x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsALUOperation(instr))
		{
			uint8_t op = (instr >> 6) & 0xF;
			uint8_t rs = (instr >> 3) & 0x7;
			uint8_t rd = instr & 0x7;

			switch (op)
			{
			case 0x00:
			{
				uint32_t result = GetReg(rd) & GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;

				printf("and r%d, r%d\n", rd, rs);

				SetReg(rd, result);
				break;
			}
			case 0x01:
			{
				uint32_t result = GetReg(rd) ^ GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;

				printf("eors r%d, r%d\n", rd, rs);

				SetReg(rd, result);

				break;
			}
			case 0x02:
			{
				uint32_t result = GetReg(rd) << GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = (GetReg(rd) & (1 << (GetReg(rs) - 1))) != 0;

				printf("lsl r%d, r%d\n", rd, rs);

				SetReg(rd, result);
				break;
			}
			case 0x03:
			{
				uint32_t result = GetReg(rd) >> GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = (GetReg(rd) & (1 << (32 - GetReg(rs)))) != 0;

				printf("lsr r%d, r%d\n", rd, rs);

				SetReg(rd, result);
				break;
			}
			case 0x0a:
			{
				uint32_t result = GetReg(rd) - GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.c = !OverflowFrom(GetReg(rd), -GetReg(rs));
				cpsr.flags.v = !cpsr.flags.c;

				printf("cmp r%d, r%d\n", rd, rs);

				break;
			}
			case 0x0C:
			{
				uint32_t result = GetReg(rd) | GetReg(rs);

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;

				printf("orr r%d, r%d\n", rd, rs);

				SetReg(rd, result);

				break;
			}
			case 0x0e:
				SetReg(rd, GetReg(rd) & ~GetReg(rs));
				cpsr.flags.z = (GetReg(rd) == 0);
				cpsr.flags.n = (GetReg(rd) >> 31) & 1;
				printf("bic r%d, r%d\n", rd, rs);
				break;
			case 0x0f:
				SetReg(rd, ~GetReg(rs));
				printf("mvn r%d, r%d\n", rd, rs);
				break;
			default:
				printf("Unknown ALU op 0x%x\n", op);
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsBranchLinkThumb(instr))
		{
			bool b = (instr >> 12) & 1;
			bool l = (instr >> 11) & 1;

			uint8_t offset5 = ((instr >> 6) & 0x1F);
			
			uint8_t rb = (instr >> 3) & 0x7;
			uint8_t rd = instr & 0x7;

			if (!b)
				offset5 <<= 2;

			uint32_t addr = GetReg(rb) + offset5;

			if (!b && !l)
			{
				printf("str r%d, [r%d, #%d]\n", rd, rb, offset5);
				Bus::Write32_ARM7(addr & ~3, GetReg(rd));
			}
			else if (b && !l)
			{
				printf("strb r%d, [r%d, #%d]\n", rd, rb, offset5);
				Bus::Write8_ARM7(addr, GetReg(rd));
			}
			else if (b && l)
			{
				printf("ldrb r%d, [r%d, #%d]\n", rd, rb, offset5);
				SetReg(rd, Bus::Read8_ARM7(addr));
			}
			else if (!b && l)
			{
				printf("ldr r%d, [r%d, #%d]\n", rd, rb, offset5);
				SetReg(rd, Bus::Read32_ARM7(addr & ~3));
			}
			else
			{
				printf("Unhandled combo for THUMB Load/Store imm\n");
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsAddOffsetToStackPointer(instr))
		{
			bool s = (instr >> 7) & 1;
			int16_t imm7 = (int8_t)(instr & 0x7F) << 2;

			if (s)
			{
				GetReg(13) -= imm7;
			}
			else
			{
				GetReg(13) += imm7;
			}

			printf("add sp, #%s%d\n", s ? "-" : "", imm7);

			GetReg(15) += 2;
		}
		else if (IsSPRelativeLoadStore(instr))
		{
			bool l = (instr >> 11) & 1;
			uint8_t rd = (instr >> 8) & 0x7;
			uint8_t imm8 = instr & 0xff;
			imm8 <<= 2;

			if (l)
			{
				printf("ldr r%d, [sp", rd);
				if (imm8)
					printf(", #%d", imm8);
				printf("]\n");
				SetReg(rd, Bus::Read32_ARM7(GetReg(13) + imm8));
			}
			else
			{
				printf("str r%d, [sp", rd);
				if (imm8)
					printf(", #%d", imm8);
				printf("]\n");
				Bus::Write32_ARM7(GetReg(13) + imm8, GetReg(rd));
			}

			GetReg(15) += 2;
		}
		else if (IsUnconditionalBranch(instr))
		{
			int16_t offset = (instr & 0xFFF) << 1;
			offset = sign_extend(offset, 12);

			GetReg(15) += (int32_t)offset;

			printf("b 0x%08x\n", GetReg(15));

			FlushPipeline();
		}
		else if (IsAddSubtract(instr))
		{
			bool i = (instr >> 10) & 1;
			bool op = (instr >> 9) & 1;
			uint8_t rn_or_off3 = (instr >> 6) & 7;
			uint8_t rs = (instr >> 3) & 7;
			uint8_t rd = instr & 7;

			uint32_t op2 = i ? rn_or_off3 : GetReg(rn_or_off3);

			printf("%s r%d, r%d, ", op ? "sub" : "add", rd, rs);
			if (i)
				printf("#%d\n", rn_or_off3);
			else
				printf("r%d\n", rn_or_off3);
			
			if (op)
			{
				uint32_t result = GetReg(rs) - op2;

				cpsr.flags.c = !OverflowFrom(GetReg(rs), -op2);
				cpsr.flags.v = OverflowFrom(GetReg(rs), -op2);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.z = (result == 0);

				SetReg(rd, result);
			}
			else
			{
				uint32_t result = GetReg(rs) + op2;

				cpsr.flags.c = !OverflowFrom(GetReg(rs), op2);
				cpsr.flags.v = OverflowFrom(GetReg(rs), op2);
				cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.z = (result == 0);

				SetReg(rd, result);
			}

			GetReg(15) += 2;
		}
		else if (IsLoadAddress(instr))
		{
			bool sp = (instr >> 11) & 1;
			uint8_t rd = (instr >> 8) & 0x7;
			uint8_t word = instr & 0xff;
			word <<= 2;

			printf("add r%d, %s, #%d\n", rd, sp ? "sp" : "pc", word);

			uint32_t addr;
			if (sp)
				addr = GetReg(13);
			else
				addr = GetReg(15);
			
			addr += word;

			SetReg(rd, addr);

			GetReg(15) += 2;
		}
		else if (IsLoadStoreMultiple(instr))
		{
			uint8_t reg_list = instr & 0xff;
			bool l = (instr >> 11) & 1;

			int n = countSetBits(reg_list);
			int m = (instr & 0x0700) >> 8;

			uint32_t op0 = GetReg(m);

			if (l)
			{
				printf("ldm r%d!, {", m);

				int regs = 0;

				for (int i = 0; i < 8; i++)
				{
					if (reg_list & (1 << i))
					{
						regs++;
						printf("r%d", i);
						if (regs != n)
							printf(", ");
						SetReg(i, Bus::Read32_ARM7(op0));
						op0 += 4;
					}
				}

				if (!(instr & (1 << m)))
					SetReg(m, op0);
				printf("}\n");
			}
			else
			{
				if ((instr & (1 << m)) && (instr & (1 << (m - 1))))
					SetReg(m, op0 + n * 4);
				
				int regs = 0;

				printf("stm r%d! (0x%08x), {", m, op0);
				
				for (int i = 0; i < 8; i++)
				{
					if (reg_list & (1 << i))
					{
						regs++;
						printf("r%d", i);
						if (regs != n)
							printf(", ");
						Bus::Write32_ARM7(op0, GetReg(i));
						op0 += 4;
					}
				}

				SetReg(m, op0);

				printf("}\n");
			}

			GetReg(15) += 2;
		}
		else if (IsLoadStoreHWSignExtend(instr))
		{
			bool h = (instr >> 1) & 1;
			bool s = (instr >> 10) & 1;

			uint8_t rd = instr & 7;
			uint8_t rb = (instr >> 3) & 7;
			uint8_t ro = (instr >> 6) & 7;

			if (h && !s)
			{
				uint32_t addr = GetReg(rb) + GetReg(ro);
				printf("ldrh r%d, [r%d, r%d]\n", rd, rb, ro);
				SetReg(rd, Bus::Read16_ARM7(addr));
			}
			else
			{
				printf("Unknown h %d s %d\n", h, s);
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

		printf("%d (0x%08x) 0x%08x: ", cond, instr, GetReg(15) - 8);

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

			if (GetReg(15) == 0)
			{
				printf("ERROR: Resetting to 0, something went wrong\n");
				exit(1);
			}

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

			std::string regs;
			bool modified_pc = false;

			if (!l)
			{
				for (int i = 0; i < 16; i++)
				{
					if (reg_list & (1 << i))
					{
						regs += "r" + std::to_string(i) + ", ";

						if (p)
							addr += u ? 4 : -4;
						
						Bus::Write32_ARM7(addr, GetReg(i));
						
						if (!p)
							addr += u ? 4 : -4;
					}
				}
			}
			else
			{
				for (int i = 15; i >= 0; i--)
				{
					if (reg_list & (1 << i))
					{
						if (i == 15)
							modified_pc = true;
						regs += "r" + std::to_string(i) + ", ";

						if (p)
							addr += u ? 4 : -4;
						
						SetReg(i, Bus::Read32_ARM7(addr));
						
						if (!p)
							addr += u ? 4 : -4;
					}
				}
			}

			regs.pop_back();
			regs.pop_back();

			if (w)
				SetReg(rn, addr);

			if (rn == 13)
			{
				if (l && p && u)
				{
					printf("ldmed ");
				}
				else if (l && !p && u)
				{
					printf("ldmfd ");
				}
				else if (l && p && !u)
				{
					printf("ldmea ");
				}
				else if (l && !p && !u)
				{
					printf("ldmfa ");
				}
				else if (!l && p && u)
				{
					printf("stmfa ");
				}
				else if (!l && !p && u)
				{
					printf("stmea ");
				}
				else if (!l && p && !u)
				{
					printf("stmfd ");
				}
				else if (!l && !p && !u)
				{
					printf("stmed ");
				}
			}
			else
			{
				if (l && p && u)
				{
					printf("ldmib ");
				}
				else if (l && !p && u)
				{
					printf("ldmia ");
				}
				else if (l && p && !u)
				{
					printf("ldmdb ");
				}
				else if (l && !p && !u)
				{
					printf("ldmda ");
				}
				else if (!l && p && u)
				{
					printf("stmib ");
				}
				else if (!l && !p && u)
				{
					printf("stmia ");
				}
				else if (!l && p && !u)
				{
					printf("stmdb ");
				}
				else if (!l && !p && !u)
				{
					printf("stmda ");
				}
			}

			if (GetReg(15)-4 == 0x3168)
				exit(1);

			if (!l || !modified_pc)
			{
				if (!w || rn != 15)
					GetReg(15) += 4;
			}

			printf("r%d%s, {%s}\n", rn, w ? "!" : "", regs.c_str());
		}
		else if (IsBranch(instr))
		{
			bool l = (instr >> 24) & 1;
			int32_t imm = sign_extend<int32_t>((instr & 0xFFFFFF) << 2, 26);

			if (l)
				SetReg(14, GetReg(15) - 4);
			
			GetReg(15) += imm;

			printf("b%s 0x%08x\n", l ? "l" : "", GetReg(15));

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
				printf("str r%d, %s\n", rd, op2.c_str());
				Bus::Write32_ARM7(addr, GetReg(rd));
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
				printf("Switching to mode %d\n", cpsr.flags.mode);
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
				uint8_t shift = (instr >> 4) & 0xFF;
				uint8_t rm = instr & 0xF;

				bool is_shift_by_rs = shift & 1;

				if (is_shift_by_rs)
				{
					assert(0);
				}
				else
				{
					uint8_t shift_type = (shift >> 1) & 3;
					uint8_t shamt = (shift >> 3) & 0x1F;

					switch (shift_type)
					{
					case 0:
						op2 = GetReg(rm) << shamt;
						op2_disasm += "r" + std::to_string(rm) + ", lsl #" + std::to_string(shamt);
						break;
					case 1:
						op2 = GetReg(rm) >> shamt;
						op2_disasm += "r" + std::to_string(rm) + ", lsr #" + std::to_string(shamt);
						break;
					default:
						printf("Unknown shift type %d\n", shift_type);
						exit(1);
					}
				}
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
			case 0x08:
			{
				printf("tst r%d, %s\n", rn, op2_disasm.c_str());

				uint32_t a = GetReg(rn);
				uint32_t b = op2;

				uint32_t result = a & b;

				cpsr.flags.z = (result == 0);
				cpsr.flags.n = (result >> 31) & 1;
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

				if (s)
				{
					cpsr.flags.z = (op2 == 0);
					cpsr.flags.n = (op2 >> 31) & 1;
				}

				break;
			}
			case 0x0e:
			{
				printf("bic r%d, %s\n", rd, op2_disasm.c_str());
				SetReg(rd, GetReg(rn) & ~op2);
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
	printf("[%s%s%s%s]\n", cpsr.flags.t ? "t" : ".", cpsr.flags.c ? "c" : ".", cpsr.flags.z ? "z" : ".", cpsr.flags.n ? "n" : ".");
}

bool IsBranchExchange(uint32_t i)
{
	return ((i >> 4) & 0xFFFFFF) == 0x12FFF1;
}

bool IsBlockDataTransfer(uint32_t i)
{
	return ((i >> 25) & 0b111) == 0b100;
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
bool IsConditionalBranch(uint16_t i)
{
	return ((i >> 12) & 0b1111) == 0b1101;
}
bool IsHighOp(uint16_t i)
{
	return ((i >> 10) & 0b111111) == 0b010001;
}
bool IsPushPop(uint16_t i)
{
	return (((i >> 12) & 0xF) == 0b1011) && (((i >> 9) & 3) == 0b10);
}
bool IsHalfwordTransfer(uint16_t i)
{
	return ((i >> 12) & 0xF) == 0b1000;
}
bool IsBranchLongWithLink(uint16_t i)
{
	return ((i >> 12) & 0xF) == 0xF;
}
bool IsMoveShifted(uint16_t i)
{
	return ((i >> 13) & 0x7) == 0 && ((i >> 11) & 0x3) != 3;
}
bool IsALUOperation(uint16_t i)
{
	return ((i >> 10) & 0b111111) == 0b010000;
}
bool IsBranchLinkThumb(uint16_t i)
{
	return ((i >> 13) & 0b111) == 0b011;
}
bool IsSPRelativeLoadStore(uint16_t i)
{
	return ((i >> 12) & 0b1111) == 0b1001;
}
bool IsAddOffsetToStackPointer(uint16_t i)
{
	return ((i >> 8) & 0xFF) == 0b10110000;
}
bool IsUnconditionalBranch(uint16_t i)
{
	return ((i >> 11) & 0b11111) == 0b11100;
}
bool IsAddSubtract(uint16_t i)
{
	return ((i >> 11) & 0b11111) == 0b00011;
}
bool IsLoadAddress(uint16_t i)
{
	return ((i >> 12) & 0xF) == 0b1010;
}
bool IsLoadStoreMultiple(uint16_t i)
{
	return ((i >> 12) & 0xFF) == 0b1100;
}
bool IsLoadStoreHWSignExtend(uint16_t i)
{
	bool cond1 = ((i >> 12) & 0b1111) == 0b0101;
	bool cond2 = ((i >> 9) & 0b1) == 0b1;

	return cond1 && cond2;
}
}