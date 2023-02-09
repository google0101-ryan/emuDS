#pragma once

#include <cstdint>
#include <string>

namespace Firmware
{

void LoadFirmware(std::string fw_path);

void WriteSPICNT(uint32_t data);
void WriteSPIData(uint32_t data);

uint8_t ReadSPIData();

}