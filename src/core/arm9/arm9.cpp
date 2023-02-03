#include <src/core/arm9/arm9.h>

#include <cassert>
#include <cstring>

namespace ARM9
{

uint32_t r[16];

uint32_t* cur_r[16];

uint32_t pipeline[2];
uint16_t t_pipeline[2];

bool is_thumb = false;

PSR cpsr, spsr_fiq, spsr_svc, spsr_abr, spsr_irq, spsr_und;

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
        printf("[emu/ARM9]: Fuck thumb\n");
        exit(1);
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

void Reset()
{
    for (int i = 0; i < 16; i++)
        cur_r[i] = &r[i];
    
    memset(r, 0, sizeof(r));
    
    SetReg(15, 0xFFFF0000);

    is_thumb = false;

    FlushPipeline();
}

void Clock()
{
    if (is_thumb)
    {
        printf("[emu/ARM9]: Why thumb?\n");
        exit(1);
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

        if (IsBranchAndLink(instr))
        {
            bool is_link = (instr >> 24) & 1;

            int32_t offset = (int16_t)(instr & 0xffffff) << 2;

            if (is_link)
                SetReg(14, GetReg(15));
            
            GetReg(15) += offset;

            printf("b%s 0x%08x\n", is_link ? "l" : "", GetReg(15));

            FlushPipeline();
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
                printf("Unhandled strb\n");
                exit(1);
            }
            else if (l)
            {
                printf("Unhandled ldr\n");
                exit(1);
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
                if (w && rn == 15);
                else
                    GetReg(15) += 4;
            }
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
                assert(0 && "Unimplemented register as operand 2");
            }

            switch (opcode)
            {
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
}

}