#pragma once

#include <cstdint>

namespace Cartridge
{

void SendCommandByte(uint8_t data, int index);
void WriteROMCTRL(uint32_t data);
uint32_t ReadROMCTRL();
uint8_t ReadDataOut(int index);
uint32_t ReadDataOut();

}