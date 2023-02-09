#include <src/core/arm9/arm9.h>
#include "arm9.h"

namespace ARM9
{

extern PSR cpsr;

bool IsBranchExchange2(uint32_t i)
{
	uint32_t format = 0x12FFF10;
	uint32_t mask = 0xFFFFFF0;

	return (i & mask) == format;
}

bool IsBranchLinkExchange(uint32_t i)
{
	bool is_ble = ((i >> 20) & 0xFF) == 0b00010010;
	bool is_ble2 = ((i >> 4) & 0xF) == 0b0011;
	return is_ble && is_ble2;
}

bool IsBranchExchange(uint32_t i)
{
	uint32_t bxFormat = 0xFA000000;
	uint32_t formatMask = 0xFE000000;

	uint32_t extractedFormat = i & formatMask;

	return extractedFormat == bxFormat;
}

bool IsBlockDataTransfer(uint32_t i)
{
    bool is_bdt = ((i >> 25) & 0b111) == 0b100;

    return is_bdt;
}

bool IsBranchAndLink(uint32_t i)
{
    bool is_branch = ((i >> 25) & 0b111) == 0b101;

    return is_branch;
}

bool IsSingleDataTransfer(uint32_t i)
{
    bool is_sdt = ((i >> 26) & 0b11) == 0b01;
    return is_sdt;
}

bool IsPSRTransferMSR(uint32_t opcode)
{
	uint32_t msrFormat = 0x120F000;
	uint32_t formatMask = 0xDB0F000;

	uint32_t extractedFormat = opcode & formatMask;

	return extractedFormat == msrFormat;
}

bool IsDataProcessing(uint32_t i)
{
    bool is_dp = ((i >> 26) & 0b11) == 0b00;
    return is_dp;
}

bool IsHalfwordTransfer(uint32_t i)
{
	bool is_1 = (i >> 22) & 1;
	bool is_1001 = ((i >> 4) & 0b1001) == 0b1001;

	return is_1 & is_1001;
}

bool IsHalfwordTransfer2(uint32_t i)
{
	uint32_t dataTransfer = 0x90;
	uint32_t formatMask = 0xE400F90;

	return (i & formatMask) == dataTransfer;
}

bool IsCPTransfer(uint32_t i)
{
	bool is_cpt_1 = ((i >> 24) & 0xF) == 0b1110;
	bool is_cpt_2 = ((i >> 4) & 1) == 1;

	return is_cpt_1 && is_cpt_2;
}

bool IsArithmeticThumb(uint16_t i)
{
	bool is_arithmetic = ((i >> 13) & 0b111) == 0b001;
	return is_arithmetic;
}

bool IsConditionalBranch(uint16_t i)
{
	bool is_branch = (i >> 12) == 13;
	return is_branch;
}

bool IsBranchExchangeThumb(uint16_t i)
{
	bool is_bxt = ((i >> 7) & 0b111111111) == 0b010001110;
	return is_bxt;
}

bool IsLDR_PCRel(uint16_t i)
{
	bool is_ldr = ((i >> 11) & 0x1F) == 0b01001;
	return is_ldr;
}

bool IsSTR_Reg(uint16_t i)
{
	bool is_str = ((i >> 9) & 0x7F) == 0b0101000;
	return is_str;
}

bool IsPushPop(uint16_t i)
{
	bool is_misc = ((i >> 12) & 0xF) == 0b1011;
	bool is_push_pop = ((i >> 9) & 0x3) == 0b10;
	return is_misc && is_push_pop;
}

bool IsSTRH_Imm(uint16_t i)
{
	bool is_strh_imm = ((i >> 11) & 0b11111) == 0b10000;
	return is_strh_imm;
}

bool IsBranchLink(uint16_t i)
{
	return ((i >> 13) & 0x7) == 0x7;
}

bool IsLoadHalfwordImmediate(uint16_t i)
{
	return ((i >> 11) & 0b11111) == 0b10001;
}

bool IsLSL1(uint16_t i)
{
	return ((i >> 11) & 0b11111) == 0;
}

bool IsLSR1(uint16_t i)
{
	return ((i >> 11) & 0b11111) == 1;
}

bool IsCMP2(uint16_t i)
{
	return ((i >> 6) & 0x3FF) == 0b0100001010;
}

bool IsLDR_Imm(uint16_t i)
{
	return ((i >> 13) & 0b111) == 0b011;
}

bool IsALUThumb(uint16_t i)
{
	return ((i >> 10) & 0x3F) == 0b010000;
}

bool IsSPRelativeLoadStore(uint16_t i)
{
	return ((i >> 12) & 0xF) == 0b1001;
}

bool IsHiRegisterOperation(uint16_t i)
{
	return ((i >> 10) & 0x3F) == 0b010001;
}

bool CondPassed(uint8_t cond)
{
    switch (cond)
    {
    case 0b0000:
        return cpsr.flags.z;
    case 0b0001:
        return !cpsr.flags.z;
	case 0b0011:
		return !cpsr.flags.c;
	case 0b1000:
		return cpsr.flags.c && !cpsr.flags.z;
	case 0b1010:
		return cpsr.flags.v == cpsr.flags.n;
	case 0b1011:
		return cpsr.flags.v != cpsr.flags.n;
	case 0b1100:
		return !cpsr.flags.z && (cpsr.flags.n == cpsr.flags.v);
    case 0b1110:
    case 0b1111:
        return true;
    default:
        printf("Unknown cond code %d\n", cond);
        exit(1);
    }
}
}