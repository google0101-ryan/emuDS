#pragma once

#include <src/core/bus.h>

namespace ARM7
{

void Reset();
void Clock();
void Dump();

bool IsBranchExchange(uint32_t i);
bool IsBlockDataTransfer(uint32_t i);
bool IsBranch(uint32_t i);
bool IsSingleDataTransfer(uint32_t i);
bool IsDataProcessing(uint32_t i);
bool IsPSRTransferMSR(uint32_t opcode);

bool IsMovCmpSubAdd(uint16_t i);
bool IsPCRelativeLoad(uint16_t i);
bool IsLoadStoreRegister(uint16_t i);
bool IsConditionalBranch(uint16_t i);
bool IsHighOp(uint16_t i);
bool IsPushPop(uint16_t i);
bool IsHalfwordTransfer(uint16_t i);
bool IsBranchLongWithLink(uint16_t i);
bool IsMoveShifted(uint16_t i);
bool IsALUOperation(uint16_t i);
bool IsBranchLinkThumb(uint16_t i);
bool IsSPRelativeLoadStore(uint16_t i);
bool IsAddOffsetToStackPointer(uint16_t i);
bool IsUnconditionalBranch(uint16_t i);
bool IsAddSubtract(uint16_t i);
bool IsLoadAddress(uint16_t i);
bool IsLoadStoreMultiple(uint16_t i);
bool IsLoadStoreHWSignExtend(uint16_t i);
}