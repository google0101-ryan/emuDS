#pragma once

#include <cstdint>
#include <fstream>

namespace GPU
{

void Dump();

void InitMem();

void Draw();

void WriteDISPCNT(uint32_t data);

void WriteVRAMCNT_A(uint32_t data);
void WriteVRAMCNT_B(uint32_t data);

uint16_t ReadDIPSTAT();

void WriteLCDC(uint32_t addr, uint16_t halfword);

}