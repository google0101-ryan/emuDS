#include <src/core/arm9/arm9.h>
#include <src/core/arm9/cp15.h>

#include <cassert>
#include <cstring>
#include <bit>

namespace ARM9
{

uint32_t r[16];
uint32_t r_svc[2];
uint32_t r_irq[2];
uint32_t r_abt[2];

uint32_t* cur_r[16];

uint32_t pipeline[2];
uint16_t t_pipeline[2];

bool is_thumb = false;

PSR cpsr, spsr_fiq, spsr_svc, spsr_abr, spsr_irq, spsr_und;
PSR* cur_spsr = nullptr;

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

	if (i & (1 << 8))
	{
		Bus::Write32(op0, GetReg(14));
		regs += "lr, ";
	}

	regs.pop_back();
	regs.pop_back();

	printf("push { %s }\n", regs.c_str());
	
}

void Clock()
{
    if (is_thumb)
    {
		printf("0x%08x: ", GetReg(15) - 4);
		uint16_t instr = AdvanceThumbPipeline();

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
				printf("mov r%d, #%d\n", rd, offset8);
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

				printf("sub r%d, #%d\n", rd, offset8);

				break;
			}
			default:
				printf("Unknown THUMB arithmetic opcode 0x%02x\n", op);
				Dump();
				exit(1);
			}

			GetReg(15) += 2;
		}
		else if (IsConditionalBranch(instr))
		{
			int8_t imm8 = instr & 0xff;

			int32_t offset = imm8 << 1;

			printf("b 0x%08x (%d, 0x%08x)\n", GetReg(15) + offset, offset, GetReg(15));

			if (!CondPassed((instr >> 8) & 0xF))
				return;
			
			GetReg(15) += offset;

			FlushPipeline();
		}
		else if (IsBranchExchangeThumb(instr))
		{
			uint8_t rm = (instr >> 3) & 0xF;

			cpsr.flags.t = 0;
			is_thumb = 0;

			GetReg(15) = GetReg(rm) & ~3;

			printf("bx r%d\n", rm);

			FlushPipeline();
		}
		else if (IsLDR_PCRel(instr))
		{
			uint8_t rt = (instr >> 8) & 0x7;
			uint32_t imm8 = instr & 0xff;
			imm8 <<= 2;

			uint32_t pc = GetReg(15) & ~3;

			SetReg(rt, Bus::Read32(pc + imm8));

			printf("ldr r%d, #%d (0x%08x)\n", rt, imm8, pc + imm8);

			GetReg(15) += 2;
		}
		else if (IsSTR_Reg(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rn = (instr >> 3) & 0x7;
			uint8_t rm = (instr >> 6) & 0x7;

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
				assert(0);
			}
			else
			{
				ThumbPush(instr);
			}

			GetReg(15) += 2;
		}
		else if (IsSTRH_Imm(instr))
		{
			uint8_t rd = instr & 0x7;
			uint8_t rn = (instr >> 3) & 0x7;
			uint8_t imm5 = ((instr >> 6) & 0x1F) << 2;
			
			std::string disasm = "strh r" + std::to_string(rd) + ", [r"
				+ std::to_string(rn);
			
			if (imm5)
				disasm += ", #" + std::to_string(imm5);
			
			disasm += "]";

			printf("%s\n", disasm.c_str());

			uint32_t addr = GetReg(rn);
			addr += imm5;

			Bus::Write16(addr, GetReg(rd));

			GetReg(15) += 2;
		}
		else
		{
			printf("Unknown THUMB instruction 0x%04x\n", instr);
			Dump();
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

		printf("0x%08x: ", GetReg(15) - 8);

		if (IsBranchExchange2(instr))
		{
			uint8_t rn = instr & 0xF;

			cpsr.flags.t = GetReg(rn) & 1;
			is_thumb = cpsr.flags.t;

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

			printf("blx 0x%08x\n", GetReg(15) + offset);

			SetReg(14, GetReg(15) - 4);

			SetReg(15, GetReg(15) + offset);

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

			for (int i = 0; i < 16; i++)
			{
				if (reg_list & (1 << i))
				{
					if (i == 15)
						modified_pc = true;
					regs += "r" + std::to_string(i) + ", ";

					if (p)
						addr += u ? 4 : -4;
					
					if (l)
					{
						SetReg(i, Bus::Read32(addr));
					}
					else
					{
						Bus::Write32(addr, GetReg(i));
					}
					
					if (!p)
						addr += u ? 4 : -4;
				}
			}

			regs.pop_back();
			regs.pop_back();

			if (w)
				SetReg(rn, addr);

			if (!l || !modified_pc)
			{
				if (!w || rn != 15)
					GetReg(15) += 2;
			}

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

			printf("r%d, {%s}\n", rn, regs.c_str());
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

			assert(sh == 1);
			
			uint8_t rn = (instr >> 16) & 0xF;
			uint8_t rd = (instr >> 12) & 0xF;
			
			uint8_t offset = instr & 0xF;
			offset |= (instr >> 8) & 0xF;

			uint32_t addr = GetReg(rn);

			printf("%s r%d, [r%d", l ? "ldrh" : "strh", rd, rn);

			if (offset)
			{
				printf(", #%d", offset);
			}

			printf("]\n");
			
			if (p)
				addr += u ? offset : -offset;
			
			if (l)
			{
				SetReg(rd, Bus::Read16(addr & ~1));
			}
			else
			{
				Bus::Write16(addr & ~1, GetReg(rd));
			}

			if (!p)
				addr += u ? offset : -offset;
			
			if (w)
				SetReg(rn, addr);
			
			if (!l || rd != 15)
			{
				if (!w || rn != 15)
					GetReg(15) += 4;
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
                disasm = "ldrb r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                printf("%s\n", disasm.c_str());
                SetReg(rd, Bus::Read8(addr));
            }
            else if (b)
            {
                disasm = "strb r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                printf("%s\n", disasm.c_str());
                
				Bus::Write8(addr, GetReg(rd));
            }
            else if (l)
            {
				disasm = "ldr r" + std::to_string(rd) + ", [r" + std::to_string(rn) + ", #" + std::to_string(offset) + "]";
                printf("%s\n", disasm.c_str());
                
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
                printf("%s\n", disasm.c_str());
                Bus::Write32(addr & ~3, GetReg(rd));
            }

            if (!p)
                addr += u ? offset : -offset;

            if (w)
                SetReg(rn, addr);
            
            if (rd != 15)
            {
                if (!w || rn != 15)
                    GetReg(15) += 4;
            }
        }
		else if (IsBranchLinkExchange(instr))
		{
			uint8_t rm = instr & 0xF;

			cpsr.flags.t = GetReg(rm) & 1;
			is_thumb = cpsr.flags.t;

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
				case 0x12:
					cur_r[13] = &r_irq[0];
					cur_r[14] = &r_irq[1];
					cur_spsr = &spsr_irq;
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

					switch (shift_type)
						{
						case 0:
							second_op <<= GetReg(rs);
							break;
						case 1:
							second_op >>= GetReg(rs);
							break;
						default:
							printf("Unknown shift type %d\n", shift_type);
							exit(1);
						}

						op2_disasm += ", r" + std::to_string(rs);
				}
				else
				{
					uint8_t shamt = (shift >> 3) & 0x1F;

					if (shamt)
					{
						uint8_t shift_type = (shift >> 1) & 3;

						switch (shift_type)
						{
						case 0:
							second_op <<= shamt;
							break;
						case 1:
							second_op >>= shamt;
							break;
						default:
							printf("Unknown shift type %d\n", shift_type);
							exit(1);
						}
						
						op2_disasm += ", #" + std::to_string(shamt);
					}
				}
            }

            switch (opcode)
            {
			case 0x02:
			{
				uint64_t result = GetReg(rn) - second_op;

                cpsr.flags.c = (result >> 32);
                cpsr.flags.z = (result & 0xffffffff) == 0;
                cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = OverflowFrom(GetReg(rn), -second_op);

				printf("sub r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				GetReg(rd) = result;

				break;
			}
			case 0x04:
			{
				uint64_t result = GetReg(rn) + second_op;

                cpsr.flags.c = (result >> 32);
                cpsr.flags.z = (result & 0xffffffff) == 0;
                cpsr.flags.n = (result >> 31) & 1;
				cpsr.flags.v = OverflowFrom(GetReg(rn), second_op);

				printf("add r%d, r%d, %s\n", rd, rn, op2_disasm.c_str());

				GetReg(rd) = result;

				break;
			}
            case 0x09:
            {
                uint64_t result = GetReg(rn) ^ second_op;

                cpsr.flags.c = 0;
                cpsr.flags.z = (result & 0xffffffff) == 0;
                cpsr.flags.n = (result >> 31) & 1;
                cpsr.flags.v = 0;

                printf("teq r%d, %s\n", rn, op2_disasm.c_str());
                break;
            }
            case 0x0a:
            {
                uint64_t result = GetReg(rn) - second_op;

                cpsr.flags.c = (result >> 32);
                cpsr.flags.z = (result & 0xffffffff) == 0;
                cpsr.flags.n = (result >> 31) & 1;

                printf("cmp r%d, %s\n", rn, op2_disasm.c_str());
                break;
            }
            case 0xd:
            {
                SetReg(rd, second_op);
                printf("mov r%d, %s\n", rd, op2_disasm.c_str());
                break;
            }
            default:
                printf("Unknown data processing opcode 0x%x (0x%08x)\n", opcode, instr);
                Dump();
                exit(1);
            }

            if (rd != 15)
                GetReg(15) += 4;
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
				printf("mrc p15, #0, r%d, c%d, c%d, #%d\n", rd, crn, crm, cp);
				SetReg(rd, CP15::ReadCP15(crn, crm, cp));
			}
			else
			{
				printf("mcr p15, #0, r%d, c%d, c%d, #%d\n", rd, crn, crm, cp);
				CP15::WriteCP15(crn, crm, cp, GetReg(rd));
			}

			GetReg(15) += 4;
		}
        else
        {
            printf("Unknown instruction 0x%08x\n", instr);
            Dump();
            exit(1);
        }
    }
}

void Dump()
{
    for (int i = 0; i < 16; i++)
        printf("r%d\t->\t0x%08x\n", i, GetReg(i));
	Bus::Dump();
}

}