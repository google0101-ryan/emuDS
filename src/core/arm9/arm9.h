#pragma once

#include <src/core/bus.h>

namespace ARM9
{


union PSR
{
    uint32_t val;
    struct
    {
        uint32_t mode : 5;
        uint32_t t : 1;
        uint32_t f : 1;
        uint32_t i : 1;
        uint32_t a : 1;
        uint32_t e : 1;
        uint32_t : 14;
        uint32_t j : 1;
        uint32_t : 2;
        uint32_t q : 1;
        uint32_t v : 1;
        uint32_t c : 1;
        uint32_t z : 1;
        uint32_t n : 1;
    } flags;
};

void Reset();
void Clock();
void Dump();

bool IsBranchExchange2(uint32_t i);
bool IsBranchLinkExchange(uint32_t i);
bool IsBranchExchange(uint32_t i);
bool IsBlockDataTransfer(uint32_t i);
bool IsBranchAndLink(uint32_t i);
bool IsSingleDataTransfer(uint32_t i);
bool IsPSRTransferMSR(uint32_t opcode);
bool IsHalfwordTransfer(uint32_t i);
bool IsHalfwordTransfer2(uint32_t i);
bool IsDataProcessing(uint32_t i);
bool IsCPTransfer(uint32_t i);

bool IsArithmeticThumb(uint16_t i);
bool IsConditionalBranch(uint16_t i);
bool IsBranchExchangeThumb(uint16_t i);
bool IsLDR_PCRel(uint16_t i);
bool IsSTR_Reg(uint16_t i);
bool IsPushPop(uint16_t i);
bool IsSTRH_Imm(uint16_t i);
bool IsBranchLink(uint16_t i);
bool IsLoadHalfwordImmediate(uint16_t i);
bool IsLSL1(uint16_t i);
bool IsLSR1(uint16_t i);
bool IsCMP2(uint16_t i);

bool CondPassed(uint8_t cond);

void ThumbPush(uint16_t i);
void ThumbPop(uint16_t i);

template<class T>
T ror(T x, unsigned int moves)
{
    return (x >> moves) | (x << sizeof(T)*__CHAR_BIT__ - moves);
}

}