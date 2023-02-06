#pragma once

#include <src/core/bus.h>

namespace ARM7
{

void Reset();
void Clock();
void Dump();

bool IsBranch(uint32_t i);
bool IsSingleDataTransfer(uint32_t i);
bool IsDataProcessing(uint32_t i);

}