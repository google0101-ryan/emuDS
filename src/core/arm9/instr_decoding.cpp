#include <src/core/arm9/arm9.h>
#include "arm9.h"

namespace ARM9
{

extern PSR cpsr;

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

bool IsDataProcessing(uint32_t i)
{
    bool is_dp = ((i >> 26) & 0b11) == 0b00;
    return is_dp;
}

bool CondPassed(uint8_t cond)
{
    switch (cond)
    {
    case 0b0000:
        return cpsr.flags.z;
    case 0b1110:
        return true;
    default:
        printf("Unknown cond code %d\n", cond);
        exit(1);
    }
}

}