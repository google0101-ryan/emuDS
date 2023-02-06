#pragma once

#include <src/core/bus.h>

namespace ARM7
{

void Reset();
void Clock();
void Dump();

bool IsBranchExchange(uint32_t i);
bool IsBranch(uint32_t i);
bool IsSingleDataTransfer(uint32_t i);
bool IsDataProcessing(uint32_t i);
bool IsPSRTransferMSR(uint32_t opcode);

bool IsMovCmpSubAdd(uint16_t i);
bool IsPCRelativeLoad(uint16_t i);
bool IsLoadStoreRegister(uint16_t i);
}