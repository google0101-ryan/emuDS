#pragma

#include <cstdint>
#include <fstream>
#include <string>

namespace Bus
{

void AddARMBios(std::string file_name, bool is_arm9);

void Write32(uint32_t addr, uint32_t data);

uint32_t Read32(uint32_t addr);
uint8_t Read8(uint32_t addr);

}