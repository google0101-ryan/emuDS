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

bool IsBranchAndLink(uint32_t i);
bool IsSingleDataTransfer(uint32_t i);
bool IsDataProcessing(uint32_t i);

bool CondPassed(uint8_t cond);

template<class T>
T ror(T x, unsigned int moves)
{
    printf("RORing number by 0x%08x\n", moves);
    return (x >> moves) | (x << sizeof(T)*__CHAR_BIT__ - moves);
}

}