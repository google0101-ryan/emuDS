#include "bus.h"

uint8_t* arm9_bios;
uint8_t* arm7_bios;

size_t arm9_bios_size;

bool postflg_arm9 = false;

void Bus::AddARMBios(std::string file_name, bool is_arm9)
{
    std::ifstream bios(file_name, std::ios::ate | std::ios::binary);

    size_t size = bios.tellg();
    bios.seekg(0, std::ios::beg);

    if (is_arm9)
    {
        arm9_bios = new uint8_t[size];
        arm9_bios_size = size;
        bios.read((char*)arm9_bios, size);
    }
    else
    {
        arm7_bios = new uint8_t[size];
        bios.read((char*)arm7_bios, size);
    }
}

void Bus::Write32(uint32_t addr, uint32_t data)
{
    switch (addr)
    {
    case 0x040001A0: // Ignore Gamecard ROM and SPI control
    case 0x040001A4: // Ignore Gamecard bus ROMCTRL
        return;
    }

    printf("[emu/Bus]: Write32 0x%08x to unknown address 0x%08x\n", data, addr);
    exit(1);
}

uint32_t Bus::Read32(uint32_t addr)
{
    if (addr >= 0xFFFF0000 && addr < 0xFFFF0000 + arm9_bios_size)
        return *(uint32_t*)&arm9_bios[addr - 0xFFFF0000];

    printf("[emu/Bus]: Read32 from unknown address 0x%08x\n", addr);
    exit(1);
}

uint8_t Bus::Read8(uint32_t addr)
{

    switch (addr)
    {
    case 0x04000300:
        return postflg_arm9;
    }

    printf("[emu/Bus]: Read8 from unknown address 0x%08x\n", addr);
    exit(1);
}
